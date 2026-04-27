#include "ctrl/domain/types.h"

#include <string>

namespace ctrl::domain {

std::string to_string(CommandKind value) {
  switch (value) {
    case CommandKind::WakeOnLan:
      return "wol_wake";
    case CommandKind::HostReboot:
      return "host_reboot";
    case CommandKind::HostPoweroff:
      return "host_poweroff";
    case CommandKind::MonitoringSet:
      return "monitoring_set";
    case CommandKind::ParamsSet:
      return "params_set";
  }
  return "unknown";
}

std::string to_string(CommandState value) {
  switch (value) {
    case CommandState::Created:
      return "created";
    case CommandState::Dispatched:
      return "dispatched";
    case CommandState::Acked:
      return "acked";
    case CommandState::Running:
      return "running";
    case CommandState::RetryPending:
      return "retry_pending";
    case CommandState::Succeeded:
      return "succeeded";
    case CommandState::Failed:
      return "failed";
    case CommandState::TimedOut:
      return "timed_out";
    case CommandState::Cancelled:
      return "cancelled";
  }
  return "unknown";
}

bool try_parse_command_kind(std::string_view text, CommandKind& out) {
  if (text == "wol_wake" || text == "WOL_WAKE") {
    out = CommandKind::WakeOnLan;
    return true;
  }
  if (text == "host_reboot" || text == "HOST_REBOOT") {
    out = CommandKind::HostReboot;
    return true;
  }
  if (text == "host_poweroff" || text == "HOST_POWEROFF") {
    out = CommandKind::HostPoweroff;
    return true;
  }
  if (text == "monitoring_set" || text == "MONITORING_SET") {
    out = CommandKind::MonitoringSet;
    return true;
  }
  if (text == "params_set" || text == "PARAMS_SET") {
    out = CommandKind::ParamsSet;
    return true;
  }
  return false;
}

bool try_parse_command_state(std::string_view text, CommandState& out) {
  if (text == "created" || text == "CREATED") {
    out = CommandState::Created;
    return true;
  }
  if (text == "dispatched" || text == "DISPATCHED") {
    out = CommandState::Dispatched;
    return true;
  }
  if (text == "acked" || text == "ACKED") {
    out = CommandState::Acked;
    return true;
  }
  if (text == "running" || text == "RUNNING") {
    out = CommandState::Running;
    return true;
  }
  if (text == "retry_pending" || text == "RETRY_PENDING") {
    out = CommandState::RetryPending;
    return true;
  }
  if (text == "succeeded" || text == "SUCCEEDED") {
    out = CommandState::Succeeded;
    return true;
  }
  if (text == "failed" || text == "FAILED") {
    out = CommandState::Failed;
    return true;
  }
  if (text == "timed_out" || text == "TIMED_OUT") {
    out = CommandState::TimedOut;
    return true;
  }
  if (text == "cancelled" || text == "CANCELLED") {
    out = CommandState::Cancelled;
    return true;
  }
  return false;
}

bool is_terminal(CommandState value) {
  return value == CommandState::Succeeded || value == CommandState::Failed ||
         value == CommandState::TimedOut || value == CommandState::Cancelled;
}

} // namespace ctrl::domain
