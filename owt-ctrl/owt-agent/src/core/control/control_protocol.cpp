#include "control/control_protocol.h"

#include <atomic>
#include <chrono>

namespace control {

namespace {

std::atomic<uint64_t> g_message_counter{0};
constexpr const char* kCurrentProtocolVersion = "v3";

} // namespace

std::string to_string(message_type value) {
  switch (value) {
    case message_type::register_agent:
      return "register";
    case message_type::register_ack:
      return "register_ack";
    case message_type::heartbeat:
      return "heartbeat";
    case message_type::heartbeat_ack:
      return "heartbeat_ack";
    case message_type::command_push:
      return "command_push";
    case message_type::command_ack:
      return "command_ack";
    case message_type::command_result:
      return "command_result";
    case message_type::error:
      return "error";
  }
  return "unknown";
}

bool try_parse_message_type(const std::string& text, message_type& out) {
  if (text == "register" || text == "REGISTER") {
    out = message_type::register_agent;
    return true;
  }
  if (text == "register_ack" || text == "REGISTER_ACK") {
    out = message_type::register_ack;
    return true;
  }
  if (text == "heartbeat" || text == "HEARTBEAT") {
    out = message_type::heartbeat;
    return true;
  }
  if (text == "heartbeat_ack" || text == "HEARTBEAT_ACK") {
    out = message_type::heartbeat_ack;
    return true;
  }
  if (text == "command_push" || text == "COMMAND_PUSH") {
    out = message_type::command_push;
    return true;
  }
  if (text == "command_ack" || text == "COMMAND_ACK") {
    out = message_type::command_ack;
    return true;
  }
  if (text == "command_result" || text == "COMMAND_RESULT") {
    out = message_type::command_result;
    return true;
  }
  if (text == "error" || text == "ERROR") {
    out = message_type::error;
    return true;
  }
  return false;
}

std::string to_string(command_type value) {
  switch (value) {
    case command_type::wol_wake:
      return "wol_wake";
    case command_type::host_reboot:
      return "host_reboot";
    case command_type::host_poweroff:
      return "host_poweroff";
    case command_type::host_probe_get:
      return "host_probe_get";
    case command_type::monitoring_set:
      return "monitoring_set";
    case command_type::params_get:
      return "params_get";
    case command_type::params_set:
      return "params_set";
  }
  return "unknown";
}

bool try_parse_command_type(const std::string& text, command_type& out) {
  if (text == "wol_wake" || text == "WOL_WAKE") {
    out = command_type::wol_wake;
    return true;
  }
  if (text == "host_reboot" || text == "HOST_REBOOT") {
    out = command_type::host_reboot;
    return true;
  }
  if (text == "host_poweroff" || text == "HOST_POWEROFF") {
    out = command_type::host_poweroff;
    return true;
  }
  if (text == "host_probe_get" || text == "HOST_PROBE_GET") {
    out = command_type::host_probe_get;
    return true;
  }
  if (text == "monitoring_set" || text == "MONITORING_SET") {
    out = command_type::monitoring_set;
    return true;
  }
  if (text == "params_get" || text == "PARAMS_GET") {
    out = command_type::params_get;
    return true;
  }
  if (text == "params_set" || text == "PARAMS_SET") {
    out = command_type::params_set;
    return true;
  }
  return false;
}

std::string to_string(command_status value) {
  switch (value) {
    case command_status::created:
      return "created";
    case command_status::dispatched:
      return "dispatched";
    case command_status::acked:
      return "acked";
    case command_status::running:
      return "running";
    case command_status::retry_pending:
      return "retry_pending";
    case command_status::succeeded:
      return "succeeded";
    case command_status::failed:
      return "failed";
    case command_status::timed_out:
      return "timed_out";
    case command_status::cancelled:
      return "cancelled";
  }
  return "unknown";
}

bool try_parse_command_status(const std::string& text, command_status& out) {
  if (text == "created" || text == "CREATED") {
    out = command_status::created;
    return true;
  }
  if (text == "dispatched" || text == "DISPATCHED") {
    out = command_status::dispatched;
    return true;
  }
  if (text == "acked" || text == "ACKED") {
    out = command_status::acked;
    return true;
  }
  if (text == "running" || text == "RUNNING") {
    out = command_status::running;
    return true;
  }
  if (text == "retry_pending" || text == "RETRY_PENDING") {
    out = command_status::retry_pending;
    return true;
  }
  if (text == "succeeded" || text == "SUCCEEDED") {
    out = command_status::succeeded;
    return true;
  }
  if (text == "failed" || text == "FAILED") {
    out = command_status::failed;
    return true;
  }
  if (text == "timed_out" || text == "TIMED_OUT") {
    out = command_status::timed_out;
    return true;
  }
  if (text == "cancelled" || text == "CANCELLED") {
    out = command_status::cancelled;
    return true;
  }
  return false;
}

const char* current_protocol_version() noexcept {
  return kCurrentProtocolVersion;
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
