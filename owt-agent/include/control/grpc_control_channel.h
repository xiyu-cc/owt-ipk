#pragma once

#include "control/i_control_channel.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace control {

class grpc_control_channel final : public i_control_channel {
public:
  ~grpc_control_channel() override;
  channel_type type() const noexcept override { return channel_type::grpc; }
  bool start(const channel_start_options& options, channel_callbacks callbacks) override;
  void stop() override;
  bool send(const envelope& message) override;
  bool is_running() const noexcept override;

private:
  void worker_loop();
  bool run_endpoint_session(const std::string& endpoint);
  void invoke_connected();
  void invoke_disconnected();
  void invoke_error(const std::string& err);

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool running_ = false;
  bool connected_ = false;
  channel_start_options options_;
  channel_callbacks callbacks_;
  std::deque<envelope> outgoing_messages_;
  std::thread worker_;
};

} // namespace control
