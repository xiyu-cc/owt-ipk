#include "control/wss_control_channel.h"

#include "control/control_json_codec.h"
#include "log.h"
#include "owt/protocol/v5/contract.h"

#include <libwebsockets.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <utility>
#include <vector>

namespace control {

namespace {

constexpr std::size_t kMaxOutgoingQueue = 2048;
constexpr auto kReconnectDelay = std::chrono::seconds(2);
constexpr int kDefaultWsPort = 80;
constexpr int kDefaultWssPort = 443;
constexpr const char* kLwsProtocolName = "owt-agent-control";

struct endpoint_parts {
  std::string scheme;
  std::string authority;
  std::string host;
  int port = 0;
  bool use_ssl = false;
  std::string target;
};

bool parse_port_text(const std::string& value, int& out, std::string& err) {
  if (value.empty()) {
    err = "endpoint port is empty";
    return false;
  }
  for (const char c : value) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      err = "endpoint port is not numeric";
      return false;
    }
  }
  try {
    const int parsed = std::stoi(value);
    if (parsed <= 0 || parsed > 65535) {
      err = "endpoint port out of range";
      return false;
    }
    out = parsed;
    return true;
  } catch (...) {
    err = "endpoint port invalid";
    return false;
  }
}

bool parse_authority(const std::string& authority, endpoint_parts& out, std::string& err) {
  if (authority.empty()) {
    err = "endpoint missing host";
    return false;
  }

  out.authority = authority;
  if (!authority.empty() && authority.front() == '[') {
    const auto closing = authority.find(']');
    if (closing == std::string::npos) {
      err = "endpoint IPv6 host invalid";
      return false;
    }
    out.host = authority.substr(1, closing - 1);
    if (out.host.empty()) {
      err = "endpoint host is empty";
      return false;
    }
    if (closing + 1 < authority.size()) {
      if (authority[closing + 1] != ':') {
        err = "endpoint authority invalid";
        return false;
      }
      const std::string port_text = authority.substr(closing + 2);
      if (!parse_port_text(port_text, out.port, err)) {
        return false;
      }
    }
    return true;
  }

  const auto first_colon = authority.find(':');
  const auto last_colon = authority.rfind(':');
  if (first_colon != std::string::npos) {
    if (first_colon != last_colon) {
      err = "endpoint IPv6 host must be bracketed";
      return false;
    }

    out.host = authority.substr(0, first_colon);
    if (out.host.empty()) {
      err = "endpoint host is empty";
      return false;
    }

    const std::string port_text = authority.substr(first_colon + 1);
    if (!parse_port_text(port_text, out.port, err)) {
      return false;
    }
    return true;
  }

  out.host = authority;
  if (out.host.empty()) {
    err = "endpoint host is empty";
    return false;
  }
  return true;
}

bool parse_endpoint(const std::string& endpoint, endpoint_parts& out, std::string& err) {
  const auto scheme_pos = endpoint.find("://");
  if (scheme_pos == std::string::npos) {
    err = "endpoint missing scheme";
    return false;
  }

  out.scheme = endpoint.substr(0, scheme_pos);
  if (out.scheme != "wss" && out.scheme != "ws") {
    err = "endpoint scheme must be ws or wss";
    return false;
  }

  out.use_ssl = (out.scheme == "wss");
  out.port = out.use_ssl ? kDefaultWssPort : kDefaultWsPort;

  const auto rest = endpoint.substr(scheme_pos + 3);
  if (rest.empty()) {
    err = "endpoint missing host";
    return false;
  }

  const auto target_pos = rest.find('/');
  const auto authority = rest.substr(0, target_pos);
  out.target = (target_pos == std::string::npos)
      ? std::string(owt::protocol::v5::kWsRouteAgent)
      : rest.substr(target_pos);

  if (out.target.empty()) {
    err = "endpoint authority/target invalid";
    return false;
  }

  if (!parse_authority(authority, out, err)) {
    if (err.empty()) {
      err = "endpoint authority/target invalid";
    }
    return false;
  }

  return true;
}

void lws_log_sink(int, const char*) {}

} // namespace

struct wss_control_channel::impl {
  explicit impl(wss_control_channel& owner_ref) : owner(owner_ref) {}

  wss_control_channel& owner;
  std::mutex context_mutex;
  lws_context* context = nullptr;
  lws* client_wsi = nullptr;
  endpoint_parts endpoint;
  std::thread io_thread;
  std::mutex init_mutex;
  std::condition_variable init_cv;
  bool init_done = false;
  bool init_ok = false;
  std::string recv_buffer;
  std::chrono::steady_clock::time_point reconnect_due_at{};
  std::atomic<bool> reconnect_pending{false};
  std::atomic<bool> write_kick_pending{false};
  bool connect_in_progress = false;
  std::atomic<bool> stop_requested{false};

  static impl* from_wsi(const lws* wsi) {
    if (wsi == nullptr) {
      return nullptr;
    }
    auto* ctx = lws_get_context(wsi);
    if (ctx == nullptr) {
      return nullptr;
    }
    return static_cast<impl*>(lws_context_user(ctx));
  }

  static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    auto* state = from_wsi(wsi);
    if (state == nullptr) {
      return 0;
    }

    switch (reason) {
      case LWS_CALLBACK_CLIENT_ESTABLISHED:
        state->on_client_established(wsi);
        break;
      case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        state->on_client_connection_error(wsi, in, len);
        break;
      case LWS_CALLBACK_CLIENT_CLOSED:
        state->on_client_closed(wsi);
        break;
      case LWS_CALLBACK_CLIENT_RECEIVE:
        state->on_client_receive(wsi, user, in, len);
        break;
      case LWS_CALLBACK_CLIENT_WRITEABLE:
        return state->on_client_writeable(wsi);
      default:
        break;
    }

    return 0;
  }

  static const lws_protocols* protocols() {
    static const lws_protocols kProtocols[] = {
        {kLwsProtocolName, &impl::ws_callback, 0, 0, 0, nullptr, 0},
        LWS_PROTOCOL_LIST_TERM,
    };
    return kProtocols;
  }

  void run() {
    lws_set_log_level(LLL_ERR, lws_log_sink);

    lws_context_creation_info info{};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols();
    info.gid = static_cast<gid_t>(-1);
    info.uid = static_cast<uid_t>(-1);
    info.user = this;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    auto* created_context = lws_create_context(&info);
    {
      std::lock_guard<std::mutex> context_lock(context_mutex);
      context = created_context;
    }
    if (created_context == nullptr) {
      {
        std::lock_guard<std::mutex> init_lock(init_mutex);
        init_done = true;
        init_ok = false;
      }
      init_cv.notify_one();
      owner.invoke_error("event loop unavailable");
      std::lock_guard<std::mutex> lock(owner.mutex_);
      owner.running_ = false;
      return;
    }

    {
      std::lock_guard<std::mutex> init_lock(init_mutex);
      init_done = true;
      init_ok = true;
    }
    init_cv.notify_one();

    owner.start_connect_loop();

    while (true) {
      {
        std::lock_guard<std::mutex> lock(owner.mutex_);
        if (!owner.running_ || stop_requested.load(std::memory_order_relaxed)) {
          break;
        }
      }

      const auto now = std::chrono::steady_clock::now();
      if (reconnect_pending.load(std::memory_order_relaxed) && now >= reconnect_due_at) {
        reconnect_pending.store(false, std::memory_order_relaxed);
        owner.start_connect_loop();
      }

      if (write_kick_pending.exchange(false, std::memory_order_relaxed)) {
        owner.maybe_start_write();
      }

      const int rc = lws_service(created_context, 100);
      if (rc < 0) {
        owner.on_connection_lost("connection closed", false);
      }
    }

    if (client_wsi != nullptr) {
      lws_wsi_close(client_wsi, LWS_TO_KILL_ASYNC);
      lws_service(created_context, 0);
      client_wsi = nullptr;
    }

    {
      std::lock_guard<std::mutex> context_lock(context_mutex);
      context = nullptr;
      if (created_context != nullptr) {
        lws_context_destroy(created_context);
        created_context = nullptr;
      }
    }
  }

  void request_wake_for_write() {
    write_kick_pending.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> context_lock(context_mutex);
    if (context != nullptr) {
      lws_cancel_service(context);
    }
  }

  void request_stop() {
    stop_requested.store(true, std::memory_order_relaxed);
    reconnect_pending.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> context_lock(context_mutex);
    if (context != nullptr) {
      lws_cancel_service(context);
    }
  }

  void schedule_reconnect() {
    reconnect_due_at = std::chrono::steady_clock::now() + kReconnectDelay;
    reconnect_pending.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> context_lock(context_mutex);
    if (context != nullptr) {
      lws_cancel_service(context);
    }
  }

  void on_client_established(lws* wsi) {
    if (wsi != client_wsi) {
      return;
    }
    connect_in_progress = false;
    reconnect_pending.store(false, std::memory_order_relaxed);
    recv_buffer.clear();
    owner.on_connection_established();
    owner.maybe_start_write();
  }

  void on_client_connection_error(lws* wsi, void* in, size_t len) {
    if (wsi != client_wsi && client_wsi != nullptr) {
      return;
    }
    connect_in_progress = false;
    client_wsi = nullptr;

    std::string detail;
    if (in != nullptr && len > 0) {
      detail.assign(static_cast<const char*>(in), len);
    } else if (in != nullptr) {
      detail = static_cast<const char*>(in);
    } else {
      detail = "network_failure";
    }
    if (detail.empty()) {
      detail = "network_failure";
    }
    owner.on_connection_lost("connect failed: " + detail, true);
  }

  void on_client_closed(lws* wsi) {
    if (wsi != client_wsi && client_wsi != nullptr) {
      return;
    }
    connect_in_progress = false;
    client_wsi = nullptr;
    recv_buffer.clear();
    owner.on_connection_lost("connection closed", false);
  }

  void on_client_receive(lws* wsi, void*, void* in, size_t len) {
    if (wsi != client_wsi) {
      return;
    }
    if (lws_frame_is_binary(wsi)) {
      return;
    }

    if (len > 0 && in != nullptr) {
      recv_buffer.append(static_cast<const char*>(in), len);
    }

    if (!lws_is_final_fragment(wsi) || lws_remaining_packet_payload(wsi) != 0) {
      return;
    }

    std::string text = std::move(recv_buffer);
    recv_buffer.clear();

    envelope message;
    std::string error;
    if (!decode_envelope_json(text, message, error)) {
      owner.invoke_error("decode message failed: " + error);
      return;
    }

    channel_callbacks callbacks;
    {
      std::lock_guard<std::mutex> lock(owner.mutex_);
      callbacks = owner.callbacks_;
    }
    if (callbacks.on_message) {
      callbacks.on_message(message);
    }
  }

  int on_client_writeable(lws* wsi) {
    if (wsi != client_wsi) {
      return 0;
    }

    while (true) {
      queued_message pending;
      {
        std::lock_guard<std::mutex> lock(owner.mutex_);
        if (!owner.running_ || !owner.connected_ || owner.impl_.get() != this) {
          return 0;
        }
        if (owner.outgoing_messages_.empty()) {
          return 0;
        }
        pending = std::move(owner.outgoing_messages_.front());
        owner.outgoing_messages_.pop_front();
      }

      std::vector<unsigned char> buffer(LWS_PRE + pending.payload.size());
      std::memcpy(buffer.data() + LWS_PRE, pending.payload.data(), pending.payload.size());

      const int wrote = lws_write(
          wsi,
          buffer.data() + LWS_PRE,
          pending.payload.size(),
          LWS_WRITE_TEXT);

      if (wrote < 0 || static_cast<std::size_t>(wrote) != pending.payload.size()) {
        {
          std::lock_guard<std::mutex> lock(owner.mutex_);
          if (owner.running_ && !pending.drop_if_disconnected) {
            owner.outgoing_messages_.push_front(std::move(pending));
          }
        }
        owner.on_connection_lost("send failed: write error", true);
        return -1;
      }

      {
        std::lock_guard<std::mutex> lock(owner.mutex_);
        if (owner.outgoing_messages_.empty()) {
          return 0;
        }
      }
    }
  }
};

wss_control_channel::~wss_control_channel() {
  stop();
}

bool wss_control_channel::start(const channel_start_options& options, channel_callbacks callbacks) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
      return true;
    }
  }

  if (options.endpoint.empty()) {
    if (callbacks.on_error) {
      callbacks.on_error("wss endpoint is empty");
    }
    return false;
  }

  std::string parse_err;
  endpoint_parts parsed_endpoint;
  if (!parse_endpoint(options.endpoint, parsed_endpoint, parse_err)) {
    if (callbacks.on_error) {
      callbacks.on_error("invalid endpoint: " + parse_err);
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
      return true;
    }
    options_ = options;
    callbacks_ = std::move(callbacks);
    running_ = true;
    connected_ = false;
    outgoing_messages_.clear();
  }

  auto state = std::make_shared<impl>(*this);
  state->endpoint = std::move(parsed_endpoint);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    impl_ = state;
  }

  state->io_thread = std::thread([state]() { state->run(); });

  bool init_ok = false;
  {
    std::unique_lock<std::mutex> init_lock(state->init_mutex);
    state->init_cv.wait(init_lock, [&state]() { return state->init_done; });
    init_ok = state->init_ok;
  }
  if (!init_ok) {
    stop();
    return false;
  }

  log::info("wss channel started (lws): endpoint={}", options.endpoint);
  return true;
}

void wss_control_channel::stop() {
  std::shared_ptr<impl> state;
  bool was_connected = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ && !impl_) {
      return;
    }
    running_ = false;
    was_connected = connected_;
    connected_ = false;
    state = impl_;
    impl_.reset();
    outgoing_messages_.clear();
  }

  if (state) {
    state->request_stop();
    if (state->io_thread.joinable()) {
      state->io_thread.join();
    }
  }

  if (was_connected) {
    invoke_disconnected();
  }
  log::info("wss channel stopped");
}

bool wss_control_channel::send(const envelope& message) {
  queued_message payload;
  payload.payload = encode_envelope_json(message);
  payload.drop_if_disconnected = (message.type == message_type::agent_heartbeat);

  std::shared_ptr<impl> state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      log::warn("wss send dropped: channel is not running");
      return false;
    }
    if (message.type == message_type::agent_heartbeat && !connected_) {
      log::warn("heartbeat dropped: channel not connected");
      return false;
    }

    if (message.type == message_type::agent_register) {
      if (outgoing_messages_.size() >= kMaxOutgoingQueue) {
        outgoing_messages_.pop_back();
      }
      outgoing_messages_.push_front(std::move(payload));
    } else {
      if (outgoing_messages_.size() >= kMaxOutgoingQueue) {
        log::warn("wss send dropped: outgoing queue full");
        return false;
      }
      outgoing_messages_.push_back(std::move(payload));
    }

    state = impl_;
  }

  if (state) {
    state->request_wake_for_write();
  }
  return true;
}

bool wss_control_channel::is_running() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

void wss_control_channel::start_connect_loop() {
  std::shared_ptr<impl> state;
  lws_context* context = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !impl_) {
      return;
    }
    state = impl_;
  }

  {
    std::lock_guard<std::mutex> context_lock(state->context_mutex);
    context = state->context;
  }
  if (context == nullptr) {
    return;
  }
  state->connect_in_progress = true;

  lws_client_connect_info info{};
  info.context = context;
  info.address = state->endpoint.host.c_str();
  info.port = state->endpoint.port;
  info.path = state->endpoint.target.c_str();
  info.host = state->endpoint.authority.c_str();
  info.origin = state->endpoint.authority.c_str();
  info.protocol = kLwsProtocolName;
  info.local_protocol_name = kLwsProtocolName;
  info.ssl_connection = state->endpoint.use_ssl ? LCCSCF_USE_SSL : 0;
  info.pwsi = &state->client_wsi;

  if (lws_client_connect_via_info(&info) == nullptr) {
    state->connect_in_progress = false;
    state->client_wsi = nullptr;
    on_connection_lost("connect failed: create_client_failed", true);
  }
}

void wss_control_channel::schedule_reconnect() {
  std::shared_ptr<impl> state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !impl_) {
      return;
    }
    state = impl_;
  }

  state->schedule_reconnect();
}

void wss_control_channel::on_connection_established() {
  bool should_notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || connected_) {
      return;
    }
    connected_ = true;
    should_notify = true;
  }

  if (should_notify) {
    invoke_connected();
  }
}

void wss_control_channel::on_connection_lost(const std::string& reason, bool emit_error) {
  bool notify_disconnected = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    if (connected_) {
      connected_ = false;
      notify_disconnected = true;
    }
  }

  if (emit_error) {
    invoke_error(reason);
  }
  if (notify_disconnected) {
    invoke_disconnected();
  }

  schedule_reconnect();
}

void wss_control_channel::maybe_start_write() {
  std::shared_ptr<impl> state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !connected_ || !impl_) {
      return;
    }
    if (outgoing_messages_.empty()) {
      return;
    }
    state = impl_;
  }

  if (state->client_wsi != nullptr) {
    lws_callback_on_writable(state->client_wsi);
  }
}

void wss_control_channel::invoke_connected() {
  channel_callbacks callbacks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks = callbacks_;
  }
  if (callbacks.on_connected) {
    callbacks.on_connected();
  }
}

void wss_control_channel::invoke_disconnected() {
  channel_callbacks callbacks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks = callbacks_;
  }
  if (callbacks.on_disconnected) {
    callbacks.on_disconnected();
  }
}

void wss_control_channel::invoke_error(const std::string& err) {
  channel_callbacks callbacks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks = callbacks_;
  }
  if (callbacks.on_error) {
    callbacks.on_error(err);
  }
}

} // namespace control
