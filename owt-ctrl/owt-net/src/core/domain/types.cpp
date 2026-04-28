#include "ctrl/domain/types.h"

#include "owt/protocol/v5/contract.h"

#include <string>

namespace ctrl::domain {

std::string to_string(CommandKind value) {
  switch (value) {
    case CommandKind::WakeOnLan:
      return std::string(owt::protocol::v5::command::type::kWolWake);
    case CommandKind::HostReboot:
      return std::string(owt::protocol::v5::command::type::kHostReboot);
    case CommandKind::HostPoweroff:
      return std::string(owt::protocol::v5::command::type::kHostPoweroff);
    case CommandKind::MonitoringSet:
      return std::string(owt::protocol::v5::command::type::kMonitoringSet);
    case CommandKind::ParamsSet:
      return std::string(owt::protocol::v5::command::type::kParamsSet);
  }
  return "unknown";
}

std::string to_string(CommandState value) {
  switch (value) {
    case CommandState::Created:
      return std::string(owt::protocol::v5::command::state::kCreated);
    case CommandState::Dispatched:
      return std::string(owt::protocol::v5::command::state::kDispatched);
    case CommandState::Acked:
      return std::string(owt::protocol::v5::command::state::kAcked);
    case CommandState::Running:
      return std::string(owt::protocol::v5::command::state::kRunning);
    case CommandState::RetryPending:
      return std::string(owt::protocol::v5::command::state::kRetryPending);
    case CommandState::Succeeded:
      return std::string(owt::protocol::v5::command::state::kSucceeded);
    case CommandState::Failed:
      return std::string(owt::protocol::v5::command::state::kFailed);
    case CommandState::TimedOut:
      return std::string(owt::protocol::v5::command::state::kTimedOut);
    case CommandState::Cancelled:
      return std::string(owt::protocol::v5::command::state::kCancelled);
  }
  return "unknown";
}

bool try_parse_command_kind(std::string_view text, CommandKind& out) {
  if (text == owt::protocol::v5::command::type::kWolWake || text == "WOL_WAKE") {
    out = CommandKind::WakeOnLan;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kHostReboot || text == "HOST_REBOOT") {
    out = CommandKind::HostReboot;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kHostPoweroff || text == "HOST_POWEROFF") {
    out = CommandKind::HostPoweroff;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kMonitoringSet || text == "MONITORING_SET") {
    out = CommandKind::MonitoringSet;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kParamsSet || text == "PARAMS_SET") {
    out = CommandKind::ParamsSet;
    return true;
  }
  return false;
}

bool try_parse_command_state(std::string_view text, CommandState& out) {
  if (text == owt::protocol::v5::command::state::kCreated || text == "CREATED") {
    out = CommandState::Created;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kDispatched || text == "DISPATCHED") {
    out = CommandState::Dispatched;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kAcked || text == "ACKED") {
    out = CommandState::Acked;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kRunning || text == "RUNNING") {
    out = CommandState::Running;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kRetryPending || text == "RETRY_PENDING") {
    out = CommandState::RetryPending;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kSucceeded || text == "SUCCEEDED") {
    out = CommandState::Succeeded;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kFailed || text == "FAILED") {
    out = CommandState::Failed;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kTimedOut || text == "TIMED_OUT") {
    out = CommandState::TimedOut;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kCancelled || text == "CANCELLED") {
    out = CommandState::Cancelled;
    return true;
  }
  return false;
}

bool is_terminal(CommandState value) {
  return value == CommandState::Succeeded || value == CommandState::Failed ||
         value == CommandState::TimedOut || value == CommandState::Cancelled;
}

bool is_allowed_non_terminal_transition(
    CommandState current_state,
    CommandState next_state) {
  if (next_state == CommandState::Dispatched) {
    return current_state == CommandState::RetryPending;
  }
  if (next_state == CommandState::Acked) {
    return current_state == CommandState::Dispatched;
  }
  if (next_state == CommandState::Running) {
    return current_state == CommandState::Acked;
  }
  return false;
}

bool is_allowed_terminal_transition(CommandState current_state) {
  return current_state == CommandState::Dispatched ||
      current_state == CommandState::Acked ||
      current_state == CommandState::Running ||
      current_state == CommandState::RetryPending;
}

} // namespace ctrl::domain
