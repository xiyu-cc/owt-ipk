#include "control/grpc_control_channel.h"

#include "control/grpc_codec.h"
#include "log.h"

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
#include "control_channel.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#endif

#include <chrono>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace control {

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT

namespace {

namespace pb = owt::control::v1;

constexpr std::size_t kMaxOutgoingQueue = 2048;
constexpr auto kReconnectDelay = std::chrono::seconds(2);
constexpr auto kPollInterval = std::chrono::milliseconds(250);

} // namespace

grpc_control_channel::~grpc_control_channel() {
  stop();
}

bool grpc_control_channel::start(const channel_start_options& options, channel_callbacks callbacks) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
      return true;
    }
    if (options.endpoint.empty()) {
      if (callbacks.on_error) {
        callbacks.on_error("grpc endpoint is empty");
      }
      return false;
    }

    options_ = options;
    callbacks_ = std::move(callbacks);
    running_ = true;
    connected_ = false;
    outgoing_messages_.clear();
  }

  worker_ = std::thread([this]() { worker_loop(); });
  log::info("grpc channel worker started, endpoint={}", options.endpoint);
  return true;
}

void grpc_control_channel::stop() {
  std::thread worker_to_join;
  bool was_connected = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    running_ = false;
    was_connected = connected_;
    connected_ = false;
    cv_.notify_all();
    worker_to_join = std::move(worker_);
  }

  if (worker_to_join.joinable()) {
    worker_to_join.join();
  }
  if (was_connected) {
    invoke_disconnected();
  }
  log::info("grpc channel stopped");
}

bool grpc_control_channel::send(const envelope& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      log::warn("grpc send dropped: channel is not running");
      return false;
    }
    if (outgoing_messages_.size() >= kMaxOutgoingQueue) {
      log::warn("grpc send dropped: outgoing queue full");
      return false;
    }
    outgoing_messages_.push_back(message);
  }
  cv_.notify_all();
  return true;
}

bool grpc_control_channel::is_running() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

void grpc_control_channel::worker_loop() {
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

bool grpc_control_channel::run_endpoint_session(const std::string& endpoint) {
  auto channel = grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials());
  auto stub = pb::ControlChannelService::NewStub(channel);
  if (!stub) {
    invoke_error("create grpc stub failed");
    return false;
  }

  grpc::ClientContext context;
  if (!options_.auth_token.empty()) {
    context.AddMetadata("authorization", options_.auth_token);
  }

  auto stream = stub->Connect(&context);
  if (!stream) {
    invoke_error("open grpc connect stream failed");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
  }
  invoke_connected();

  std::atomic<bool> reader_done{false};
  std::thread reader([this, &stream, &reader_done]() {
    pb::Envelope in;
    while (stream->Read(&in)) {
      envelope decoded;
      std::string error;
      if (!decode_envelope_proto(in, decoded, error)) {
        invoke_error("decode grpc envelope failed: " + error);
        continue;
      }
      channel_callbacks callbacks_copy;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_copy = callbacks_;
      }
      if (callbacks_copy.on_message) {
        callbacks_copy.on_message(decoded);
      }
    }
    reader_done.store(true, std::memory_order_relaxed);
  });

  bool should_reconnect = false;
  while (true) {
    std::deque<envelope> batch;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!running_) {
        break;
      }
      cv_.wait_for(lock, kPollInterval, [this]() {
        return !running_ || !outgoing_messages_.empty();
      });
      if (!running_) {
        break;
      }
      batch.swap(outgoing_messages_);
    }

    for (const auto& message : batch) {
      pb::Envelope out;
      std::string encode_error;
      if (!encode_envelope_proto(message, out, encode_error)) {
        invoke_error("encode grpc envelope failed: " + encode_error);
        continue;
      }
      if (!stream->Write(out)) {
        invoke_error("grpc write failed, reconnecting");
        should_reconnect = true;
        break;
      }
    }

    if (should_reconnect || reader_done.load(std::memory_order_relaxed)) {
      should_reconnect = true;
      break;
    }
  }

  stream->WritesDone();
  context.TryCancel();
  if (reader.joinable()) {
    reader.join();
  }
  const auto status = stream->Finish();
  if (!status.ok()) {
    const bool running = is_running();
    if (running) {
      invoke_error("grpc stream finished with error: " + status.error_message());
    }
    should_reconnect = running;
  }

  bool was_connected = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    was_connected = connected_;
    connected_ = false;
  }
  if (was_connected) {
    invoke_disconnected();
  }
  log::info("grpc session closed");
  return !should_reconnect;
}

void grpc_control_channel::invoke_connected() {
  channel_callbacks callbacks_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_copy = callbacks_;
  }
  if (callbacks_copy.on_connected) {
    callbacks_copy.on_connected();
  }
}

void grpc_control_channel::invoke_disconnected() {
  channel_callbacks callbacks_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_copy = callbacks_;
  }
  if (callbacks_copy.on_disconnected) {
    callbacks_copy.on_disconnected();
  }
}

void grpc_control_channel::invoke_error(const std::string& err) {
  channel_callbacks callbacks_copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_copy = callbacks_;
  }
  if (callbacks_copy.on_error) {
    callbacks_copy.on_error(err);
  }
}

#else

grpc_control_channel::~grpc_control_channel() = default;

bool grpc_control_channel::start(const channel_start_options& options, channel_callbacks callbacks) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    return false;
  }
  if (options.endpoint.empty()) {
    if (callbacks.on_error) {
      callbacks.on_error("grpc endpoint is empty");
    }
    return false;
  }
  options_ = options;
  callbacks_ = std::move(callbacks);
  running_ = false;
  connected_ = false;
  if (callbacks_.on_error) {
    callbacks_.on_error("grpc transport is disabled at build time");
  }
  log::warn("grpc channel start ignored: grpc transport is disabled at build time");
  return false;
}

void grpc_control_channel::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) {
    return;
  }
  running_ = false;
  connected_ = false;
  log::info("grpc channel stopped");
}

bool grpc_control_channel::send(const envelope& message) {
  (void)message;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) {
    log::warn("grpc send dropped: channel is not running");
    return false;
  }

  log::warn("grpc send dropped: grpc transport is disabled at build time");
  return true;
}

bool grpc_control_channel::is_running() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

#endif

} // namespace control
