#pragma once

#include "control/control_protocol.h"

#include <functional>
#include <string>

namespace control {

struct channel_start_options {
  std::string agent_id;
  std::string endpoint;
  std::string protocol_version = "v3";
};

struct channel_callbacks {
  std::function<void()> on_connected;
  std::function<void()> on_disconnected;
  std::function<void(const std::string&)> on_error;
  std::function<void(const envelope&)> on_message;
};

class i_control_channel {
public:
  virtual ~i_control_channel() = default;

  virtual bool start(const channel_start_options& options, channel_callbacks callbacks) = 0;
  virtual void stop() = 0;
  virtual bool send(const envelope& message) = 0;
  virtual bool is_running() const noexcept = 0;
};

} // namespace control
