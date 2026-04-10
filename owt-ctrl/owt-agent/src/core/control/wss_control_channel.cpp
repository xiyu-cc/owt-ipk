#include "control/wss_control_channel.h"

#include "control/control_json_codec.h"
#include "log.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <boost/beast/websocket/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <chrono>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace control {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace ssl = asio::ssl;

constexpr std::size_t kMaxOutgoingQueue = 2048;
constexpr auto kReconnectDelay = std::chrono::seconds(2);
constexpr auto kPollInterval = std::chrono::milliseconds(250);

struct endpoint_parts {
  std::string scheme;
  std::string host;
  std::string port;
  std::string target;
};

bool parse_endpoint(const std::string& endpoint, endpoint_parts& out, std::string& err) {
  const auto scheme_pos = endpoint.find("://");
  if (scheme_pos == std::string::npos) {
    err = "endpoint missing scheme";
    return false;
  }

  out.scheme = endpoint.substr(0, scheme_pos);
  if (out.scheme != "wss") {
    err = "endpoint scheme must be wss";
    return false;
  }
  auto rest = endpoint.substr(scheme_pos + 3);
  if (rest.empty()) {
    err = "endpoint missing host";
    return false;
  }

  auto target_pos = rest.find('/');
  auto authority = rest.substr(0, target_pos);
  // If target path is omitted, use control-channel default path.
  out.target = (target_pos == std::string::npos) ? "/ws/control" : rest.substr(target_pos);

  if (authority.empty()) {
    err = "endpoint missing authority";
    return false;
  }

  auto colon = authority.rfind(':');
  if (colon != std::string::npos && colon + 1 < authority.size()) {
    out.host = authority.substr(0, colon);
    out.port = authority.substr(colon + 1);
  } else {
    out.host = authority;
    out.port = "443";
  }

  if (out.host.empty() || out.port.empty() || out.target.empty()) {
    err = "endpoint host/port/target invalid";
    return false;
  }
  return true;
}

bool is_timeout(const beast::error_code& ec) {
  return ec == beast::error::timeout || ec == asio::error::timed_out;
}

template <typename WsStream>
bool flush_outgoing(
    WsStream& ws,
    std::deque<std::string>& queue,
    std::mutex& mutex,
    std::size_t& sent_count) {
  std::vector<std::string> batch;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty()) {
      return true;
    }
    batch.assign(queue.begin(), queue.end());
    queue.clear();
  }

  for (const auto& payload : batch) {
    beast::error_code ec;
    ws.text(true);
    ws.write(asio::buffer(payload), ec);
    if (ec) {
      std::lock_guard<std::mutex> lock(mutex);
      queue.insert(queue.begin(), payload);
      return false;
    }
    ++sent_count;
  }
  return true;
}

template <typename WsStream>
bool poll_receive(WsStream& ws, channel_callbacks& callbacks) {
  beast::flat_buffer buffer;
  beast::error_code ec;
  beast::get_lowest_layer(ws).expires_after(kPollInterval);
  ws.read(buffer, ec);
  if (!ec) {
    const auto text = beast::buffers_to_string(buffer.data());
    envelope message;
    std::string err;
    if (!decode_envelope_json(text, message, err)) {
      if (callbacks.on_error) {
        callbacks.on_error("decode message failed: " + err);
      }
      return true;
    }
    if (callbacks.on_message) {
      callbacks.on_message(message);
    }
    return true;
  }

  if (is_timeout(ec)) {
    return true;
  }
  if (ec == websocket::error::closed) {
    return false;
  }
  if (callbacks.on_error) {
    callbacks.on_error("websocket read failed: " + ec.message());
  }
  return false;
}

} // namespace

wss_control_channel::~wss_control_channel() {
  stop();
}

bool wss_control_channel::start(const channel_start_options& options, channel_callbacks callbacks) {
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

  if (options.endpoint.empty()) {
    invoke_error("wss endpoint is empty");
    stop();
    return false;
  }

  worker_ = std::thread([this]() { worker_loop(); });
  log::info("wss channel worker started, endpoint={}", options.endpoint);
  return true;
}

void wss_control_channel::stop() {
  std::thread worker_to_join;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    running_ = false;
    cv_.notify_all();
    worker_to_join = std::move(worker_);
  }

  if (worker_to_join.joinable()) {
    worker_to_join.join();
  }

  const bool was_connected = [this]() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool value = connected_;
    connected_ = false;
    return value;
  }();
  if (was_connected) {
    invoke_disconnected();
  }
  log::info("wss channel stopped");
}

bool wss_control_channel::send(const envelope& message) {
  const auto payload = encode_envelope_json(message);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      log::warn("wss send dropped: channel is not running");
      return false;
    }
    if (outgoing_messages_.size() >= kMaxOutgoingQueue) {
      log::warn("wss send dropped: outgoing queue full");
      return false;
    }
    outgoing_messages_.push_back(payload);
  }
  cv_.notify_all();
  return true;
}

bool wss_control_channel::is_running() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

void wss_control_channel::worker_loop() {
  while (true) {
    std::string endpoint;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_) {
        break;
      }
      endpoint = options_.endpoint;
    }

    const auto ok = run_endpoint_session(endpoint);
    if (!ok) {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!running_) {
        break;
      }
      cv_.wait_for(lock, kReconnectDelay, [this]() { return !running_; });
    }
  }
}

bool wss_control_channel::run_endpoint_session(const std::string& endpoint) {
  endpoint_parts ep;
  std::string parse_err;
  if (!parse_endpoint(endpoint, ep, parse_err)) {
    invoke_error("invalid endpoint: " + parse_err);
    return false;
  }

  asio::io_context ioc;
  tcp::resolver resolver(ioc);

  std::size_t sent_count = 0;
  ssl::context ssl_ctx(ssl::context::tls_client);
  try {
    ssl_ctx.set_default_verify_paths();
  } catch (const std::exception& ex) {
    invoke_error(std::string("load trust store failed: ") + ex.what());
    return false;
  }
  ssl_ctx.set_verify_mode(ssl::verify_peer);
  websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(ioc, ssl_ctx);

  beast::error_code ec;
  const auto results = resolver.resolve(ep.host, ep.port, ec);
  if (ec) {
    invoke_error("resolve failed: " + ec.message());
    return false;
  }
  beast::get_lowest_layer(ws).connect(results, ec);
  if (ec) {
    invoke_error("connect failed: " + ec.message());
    return false;
  }
  if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), ep.host.c_str())) {
    invoke_error("sni setup failed: " + std::to_string(static_cast<unsigned long>(ERR_get_error())));
    return false;
  }
  ws.next_layer().set_verify_mode(ssl::verify_peer);
  ws.next_layer().set_verify_callback(ssl::host_name_verification(ep.host));
  ws.next_layer().handshake(ssl::stream_base::client, ec);
  if (ec) {
    invoke_error("tls handshake failed: " + ec.message());
    return false;
  }
  ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
  ws.handshake(ep.host, ep.target, ec);
  if (ec) {
    invoke_error("websocket handshake failed: " + ec.message());
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
  }
  invoke_connected();

  while (true) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!running_) {
        break;
      }
      cv_.wait_for(
          lock, kPollInterval, [this]() { return !running_ || !outgoing_messages_.empty(); });
      if (!running_) {
        break;
      }
    }

    if (!flush_outgoing(ws, outgoing_messages_, mutex_, sent_count)) {
      invoke_error("websocket write failed, reconnecting");
      break;
    }

    channel_callbacks callbacks_copy;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callbacks_copy = callbacks_;
    }
    if (!poll_receive(ws, callbacks_copy)) {
      break;
    }
  }

  ws.close(websocket::close_code::normal, ec);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
      connected_ = false;
    }
  }
  invoke_disconnected();
  log::info("wss session closed, sent_messages={}", sent_count);
  return false;
}

void wss_control_channel::invoke_connected() {
  channel_callbacks callbacks_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_copy = callbacks_;
  }
  if (callbacks_copy.on_connected) {
    callbacks_copy.on_connected();
  }
}

void wss_control_channel::invoke_disconnected() {
  channel_callbacks callbacks_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_copy = callbacks_;
  }
  if (callbacks_copy.on_disconnected) {
    callbacks_copy.on_disconnected();
  }
}

void wss_control_channel::invoke_error(const std::string& err) {
  channel_callbacks callbacks_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_copy = callbacks_;
  }
  if (callbacks_copy.on_error) {
    callbacks_copy.on_error(err);
  }
}

} // namespace control
