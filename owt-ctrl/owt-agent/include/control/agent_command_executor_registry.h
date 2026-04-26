#pragma once

#include "control/control_protocol.h"

#include <functional>
#include <map>

namespace control {

struct command_execution_result {
  command_status status = command_status::failed;
  int exit_code = -1;
  nlohmann::json result = nlohmann::json::object();
};

using command_executor = std::function<command_execution_result(const command&, const nlohmann::json&)>;

class agent_command_executor_registry {
public:
  agent_command_executor_registry() = default;

  void register_executor(command_type type, command_executor executor);
  bool execute(const command& cmd, const nlohmann::json& payload, command_execution_result& out) const;
  void clear();

  std::map<command_type, command_executor> executors_;
};

} // namespace control
