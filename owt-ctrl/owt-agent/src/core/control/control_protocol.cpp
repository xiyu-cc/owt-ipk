#include "control/control_protocol.h"

#include "owt/protocol/v5/contract.h"

#include <atomic>
#include <chrono>

namespace control {

namespace {

std::atomic<uint64_t> g_message_counter{0};
constexpr auto kCurrentProtocolVersion = owt::protocol::v5::kProtocol;

} // namespace

std::string to_string(message_type value) {
  switch (value) {
    case message_type::agent_register:
      return std::string(owt::protocol::v5::agent::kActionAgentRegister);
    case message_type::server_register_ack:
      return std::string(owt::protocol::v5::agent::kEventAgentRegistered);
    case message_type::agent_heartbeat:
      return std::string(owt::protocol::v5::agent::kActionAgentHeartbeat);
    case message_type::server_command_dispatch:
      return std::string(owt::protocol::v5::agent::kEventCommandDispatch);
    case message_type::agent_command_ack:
      return std::string(owt::protocol::v5::agent::kActionCommandAck);
    case message_type::agent_command_result:
      return std::string(owt::protocol::v5::agent::kActionCommandResult);
    case message_type::server_error:
      return std::string(owt::protocol::v5::agent::kErrorServerError);
  }
  return "unknown";
}

bool try_parse_message_type(const std::string& text, message_type& out) {
  if (text == owt::protocol::v5::agent::kActionAgentRegister) {
    out = message_type::agent_register;
    return true;
  }
  if (text == owt::protocol::v5::agent::kEventAgentRegistered) {
    out = message_type::server_register_ack;
    return true;
  }
  if (text == owt::protocol::v5::agent::kActionAgentHeartbeat) {
    out = message_type::agent_heartbeat;
    return true;
  }
  if (text == owt::protocol::v5::agent::kEventCommandDispatch) {
    out = message_type::server_command_dispatch;
    return true;
  }
  if (text == owt::protocol::v5::agent::kActionCommandAck) {
    out = message_type::agent_command_ack;
    return true;
  }
  if (text == owt::protocol::v5::agent::kActionCommandResult) {
    out = message_type::agent_command_result;
    return true;
  }
  if (text == owt::protocol::v5::agent::kErrorServerError) {
    out = message_type::server_error;
    return true;
  }
  return false;
}

std::string to_string(command_type value) {
  switch (value) {
    case command_type::wol_wake:
      return std::string(owt::protocol::v5::command::type::kWolWake);
    case command_type::host_reboot:
      return std::string(owt::protocol::v5::command::type::kHostReboot);
    case command_type::host_poweroff:
      return std::string(owt::protocol::v5::command::type::kHostPoweroff);
    case command_type::monitoring_set:
      return std::string(owt::protocol::v5::command::type::kMonitoringSet);
    case command_type::params_set:
      return std::string(owt::protocol::v5::command::type::kParamsSet);
  }
  return "unknown";
}

bool try_parse_command_type(const std::string& text, command_type& out) {
  if (text == owt::protocol::v5::command::type::kWolWake || text == "WOL_WAKE") {
    out = command_type::wol_wake;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kHostReboot || text == "HOST_REBOOT") {
    out = command_type::host_reboot;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kHostPoweroff || text == "HOST_POWEROFF") {
    out = command_type::host_poweroff;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kMonitoringSet || text == "MONITORING_SET") {
    out = command_type::monitoring_set;
    return true;
  }
  if (text == owt::protocol::v5::command::type::kParamsSet || text == "PARAMS_SET") {
    out = command_type::params_set;
    return true;
  }
  return false;
}

std::string to_string(command_status value) {
  switch (value) {
    case command_status::created:
      return std::string(owt::protocol::v5::command::state::kCreated);
    case command_status::dispatched:
      return std::string(owt::protocol::v5::command::state::kDispatched);
    case command_status::acked:
      return std::string(owt::protocol::v5::command::state::kAcked);
    case command_status::running:
      return std::string(owt::protocol::v5::command::state::kRunning);
    case command_status::retry_pending:
      return std::string(owt::protocol::v5::command::state::kRetryPending);
    case command_status::succeeded:
      return std::string(owt::protocol::v5::command::state::kSucceeded);
    case command_status::failed:
      return std::string(owt::protocol::v5::command::state::kFailed);
    case command_status::timed_out:
      return std::string(owt::protocol::v5::command::state::kTimedOut);
    case command_status::cancelled:
      return std::string(owt::protocol::v5::command::state::kCancelled);
  }
  return "unknown";
}

bool try_parse_command_status(const std::string& text, command_status& out) {
  if (text == owt::protocol::v5::command::state::kCreated || text == "CREATED") {
    out = command_status::created;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kDispatched || text == "DISPATCHED") {
    out = command_status::dispatched;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kAcked || text == "ACKED") {
    out = command_status::acked;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kRunning || text == "RUNNING") {
    out = command_status::running;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kRetryPending || text == "RETRY_PENDING") {
    out = command_status::retry_pending;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kSucceeded || text == "SUCCEEDED") {
    out = command_status::succeeded;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kFailed || text == "FAILED") {
    out = command_status::failed;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kTimedOut || text == "TIMED_OUT") {
    out = command_status::timed_out;
    return true;
  }
  if (text == owt::protocol::v5::command::state::kCancelled || text == "CANCELLED") {
    out = command_status::cancelled;
    return true;
  }
  return false;
}

const char* current_protocol_version() noexcept {
  return kCurrentProtocolVersion.data();
}

bool is_supported_protocol_version(const std::string& version) noexcept {
  return version == kCurrentProtocolVersion;
}

int64_t unix_time_ms_now() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string make_message_id() {
  const auto now = static_cast<uint64_t>(unix_time_ms_now());
  const auto seq = g_message_counter.fetch_add(1, std::memory_order_relaxed);
  return "msg-" + std::to_string(now) + "-" + std::to_string(seq);
}

} // namespace control
