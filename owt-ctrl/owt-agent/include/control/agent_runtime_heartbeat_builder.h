#pragma once

#include "control/ports/interfaces.h"

#include <memory>

#include <nlohmann/json.hpp>

namespace control {

class agent_runtime_heartbeat_builder {
public:
  explicit agent_runtime_heartbeat_builder(
      std::shared_ptr<ports::i_agent_service_port> service_port = {});

  nlohmann::json build_stats() const;

private:
  std::shared_ptr<ports::i_agent_service_port> service_port_;
};

} // namespace control
