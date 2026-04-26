#include "control/agent_command_executor_registry.h"

#include <utility>

namespace control {

void agent_command_executor_registry::register_executor(command_type type, command_executor executor) {
  if (!executor) {
    executors_.erase(type);
    return;
  }
  executors_[type] = std::move(executor);
}

bool agent_command_executor_registry::execute(
    const command& cmd,
    const nlohmann::json& payload,
    command_execution_result& out) const {
  const auto it = executors_.find(cmd.type);
  if (it == executors_.end() || !it->second) {
    return false;
  }
  out = it->second(cmd, payload);
  return true;
}

void agent_command_executor_registry::clear() {
  executors_.clear();
}

} // namespace control
