#include "control/default_control_channel_factory.h"

#include "control/wss_control_channel.h"

#include <memory>

namespace control {

std::unique_ptr<i_control_channel> make_default_control_channel() {
  return std::make_unique<wss_control_channel>();
}

} // namespace control
