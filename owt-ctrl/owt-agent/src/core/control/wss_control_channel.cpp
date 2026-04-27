#include "control/wss_control_channel.h"

#include "control/control_json_codec.h"
#include "log.h"
#include "owt/protocol/v5/contract.h"

#include <drogon/WebSocketClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoopThread.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace control {

namespace {

constexpr std::size_t kMaxOutgoingQueue = 2048;
constexpr auto kReconnectDelay = std::chrono::seconds(2);

struct endpoint_parts {
  std::string host_string;
  std::string target;
};

bool parse_endpoint(const std::string& endpoint, endpoint_parts& out, std::string& err) {
  const auto scheme_pos = endpoint.find("://");
  if (scheme_pos == std::string::npos) {
    err = "endpoint missing scheme";
    return false;
  }

  const auto scheme = endpoint.substr(0, scheme_pos);
  if (scheme != "wss" && scheme != "ws") {
    err = "endpoint scheme must be ws or wss";
    return false;
  }

  auto rest = endpoint.substr(scheme_pos + 3);
  if (rest.empty()) {
    err = "endpoint missing host";
    return false;
  }

  const auto target_pos = rest.find('/');
  const auto authority = rest.substr(0, target_pos);
  out.target = (target_pos == std::string::npos)
      ? std::string(owt::protocol::v5::kWsRouteAgent)
      : rest.substr(target_pos);

  if (authority.empty() || out.target.empty()) {
    err = "endpoint authority/target invalid";
    return false;
  }

  out.host_string = scheme + "://" + authority;
  return true;
}

std::string req_result_to_string(drogon::ReqResult r) {
  switch (r) {
    case drogon::ReqResult::Ok:
      return "ok";
    case drogon::ReqResult::BadResponse:
      return "bad_response";
    case drogon::ReqResult::NetworkFailure:
      return "network_failure";
    case drogon::ReqResult::BadServerAddress:
      return "bad_server_address";
    case drogon::ReqResult::Timeout:
      return "timeout";
    case drogon::ReqResult::HandshakeError:
      return "handshake_error";
    case drogon::ReqResult::InvalidCertificate:
      return "invalid_certificate";
    default:
      return "unknown";
  }
}

} // namespace

struct wss_control_channel::impl {
  explicit impl(wss_control_channel& owner_ref) : owner(owner_ref), loop_thread("owt-agent-ws") {}

  wss_control_channel& owner;
  trantor::EventLoopThread loop_thread;
  trantor::EventLoop* loop = nullptr;
  drogon::WebSocketClientPtr client;
  endpoint_parts endpoint;
  bool reconnect_pending = false;
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

  auto state = std::make_shared<impl>(*this);
  state->loop_thread.run();
  state->loop = state->loop_thread.getLoop();
  if (state->loop == nullptr) {
    invoke_error("event loop unavailable");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    impl_ = state;
  }

  state->loop->queueInLoop([this]() { start_connect_loop(); });
  log::info("wss channel started (drogon): endpoint={}", options.endpoint);
  return true;
}

void wss_control_channel::stop() {
  std::shared_ptr<impl> state;
  bool was_connected = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
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
    if (state->loop != nullptr) {
      state->loop->queueInLoop([state]() {
        if (state->client) {
          state->client->stop();
          state->client.reset();
        }
        if (state->loop != nullptr) {
          state->loop->quit();
        }
      });
    }
    state->loop_thread.wait();
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

  if (state && state->loop) {
    state->loop->queueInLoop([this]() { maybe_start_write(); });
  }
  return true;
}

bool wss_control_channel::is_running() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

void wss_control_channel::start_connect_loop() {
  std::shared_ptr<impl> state;
  std::string endpoint;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !impl_) {
      return;
    }
    endpoint = options_.endpoint;
    state = impl_;
  }

  std::string parse_err;
  if (!parse_endpoint(endpoint, state->endpoint, parse_err)) {
    on_connection_lost("invalid endpoint: " + parse_err, true);
    return;
  }

  const bool validate_cert = state->endpoint.host_string.rfind("wss://", 0) == 0;
  state->client = drogon::WebSocketClient::newWebSocketClient(
      state->endpoint.host_string,
      state->loop,
      false,
      validate_cert);

  state->client->setMessageHandler(
      [this](std::string&& text,
             const drogon::WebSocketClientPtr&,
             const drogon::WebSocketMessageType& type) {
        if (type != drogon::WebSocketMessageType::Text) {
          return;
        }

        envelope message;
        std::string error;
        if (!decode_envelope_json(text, message, error)) {
          invoke_error("decode message failed: " + error);
          return;
        }

        channel_callbacks callbacks;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          callbacks = callbacks_;
        }
        if (callbacks.on_message) {
          callbacks.on_message(message);
        }
      });

  state->client->setConnectionClosedHandler([this](const drogon::WebSocketClientPtr&) {
    on_connection_lost("connection closed", false);
  });

  auto req = drogon::HttpRequest::newHttpRequest();
  req->setPath(state->endpoint.target);

  state->client->connectToServer(
      req,
      [this](drogon::ReqResult r,
             const drogon::HttpResponsePtr&,
             const drogon::WebSocketClientPtr& ws_client) {
        if (r != drogon::ReqResult::Ok || !ws_client || !ws_client->getConnection()) {
          on_connection_lost("connect failed: " + req_result_to_string(r), true);
          return;
        }

        on_connection_established();
        maybe_start_write();
      });
}

void wss_control_channel::schedule_reconnect() {
  std::shared_ptr<impl> state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !impl_) {
      return;
    }
    state = impl_;
    if (state->reconnect_pending || !state->loop) {
      return;
    }
    state->reconnect_pending = true;
  }

  state->loop->runAfter(
      std::chrono::duration<double>(kReconnectDelay),
      [this, weak = std::weak_ptr<impl>(state)]() {
        auto s = weak.lock();
        if (!s) {
          return;
        }
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (!running_ || impl_ != s) {
            return;
          }
          s->reconnect_pending = false;
        }
        start_connect_loop();
      });
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
  while (true) {
    std::shared_ptr<impl> state;
    queued_message pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_ || !connected_ || !impl_ || !impl_->client) {
        return;
      }
      if (outgoing_messages_.empty()) {
        return;
      }
      state = impl_;
      pending = std::move(outgoing_messages_.front());
      outgoing_messages_.pop_front();
    }

    auto conn = state->client->getConnection();
    if (!conn || !conn->connected()) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ && !pending.drop_if_disconnected) {
          outgoing_messages_.push_front(std::move(pending));
        }
      }
      on_connection_lost("connection unavailable while sending", false);
      return;
    }

    try {
      conn->send(pending.payload, drogon::WebSocketMessageType::Text);
    } catch (const std::exception& ex) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ && !pending.drop_if_disconnected) {
          outgoing_messages_.push_front(std::move(pending));
        }
      }
      on_connection_lost(std::string("send failed: ") + ex.what(), true);
      return;
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ && !pending.drop_if_disconnected) {
          outgoing_messages_.push_front(std::move(pending));
        }
      }
      on_connection_lost("send failed: unknown exception", true);
      return;
    }
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
