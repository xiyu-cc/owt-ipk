#pragma once

#include "control/i_control_channel.h"

#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace control {

class wss_control_channel final : public i_control_channel {
public:
  ~wss_control_channel() override;

  bool start(const channel_start_options& options, channel_callbacks callbacks) override;
  void stop() override;
  bool send(const envelope& message) override;
  bool is_running() const noexcept override;

private:
  struct queued_message {
    std::string payload;
    bool drop_if_disconnected = false;
  };

  struct impl;

  void start_connect_loop();
  void schedule_reconnect();
  void on_connection_established();
  void on_connection_lost(const std::string& reason, bool emit_error);
  void maybe_start_write();
  void invoke_connected();
  void invoke_disconnected();
  void invoke_error(const std::string& err);

  mutable std::mutex mutex_;
  bool running_ = false;
  bool connected_ = false;
  channel_start_options options_;
  channel_callbacks callbacks_;
  std::deque<queued_message> outgoing_messages_;
  std::shared_ptr<impl> impl_;
};

} // namespace control
