#pragma once

#include "control/control_protocol.h"
#include "service/host_probe_agent.h"
#include "service/params_store.h"
#include "service/ssh_executor.h"
#include "service/wakeonlan_sender.h"

#include <functional>
#include <map>

namespace control {

struct command_execution_result {
  command_status status = command_status::failed;
  int exit_code = -1;
  nlohmann::json result = nlohmann::json::object();
};

using command_executor = std::function<command_execution_result(const command&, const nlohmann::json&)>;

struct agent_command_executor_deps {
  std::function<service::control_params()> load_params;
  std::function<bool(const service::control_params&, std::string&)> save_params;
  std::function<service::wakeonlan_result(const service::wakeonlan_request&)> send_magic_packet;
  std::function<service::ssh_result(const service::ssh_request&)> run_ssh_command;
  std::function<service::host_probe_snapshot()> get_host_probe_snapshot;
  std::function<bool()> is_monitoring_enabled;
  std::function<void(bool)> set_monitoring_enabled;
};

class agent_command_executor_registry {
public:
  explicit agent_command_executor_registry(agent_command_executor_deps deps = {});

  void register_executor(command_type type, command_executor executor);
  bool execute(const command& cmd, const nlohmann::json& payload, command_execution_result& out) const;

private:
  void install_defaults();

  agent_command_executor_deps deps_;
  std::map<command_type, command_executor> executors_;
};

} // namespace control
