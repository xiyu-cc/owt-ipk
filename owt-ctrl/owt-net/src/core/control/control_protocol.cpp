#include "control/control_protocol.h"

#include <atomic>
#include <chrono>

namespace control {

namespace {

std::atomic<uint64_t> g_message_counter{0};

} // namespace

std::string to_string(channel_type value) {
  switch (value) {
    case channel_type::wss:
      return "wss";
    case channel_type::grpc:
      return "grpc";
  }
  return "unknown";
}

bool try_parse_channel_type(const std::string& text, channel_type& out) {
  if (text == "wss") {
    out = channel_type::wss;
    return true;
  }
  if (text == "grpc") {
    out = channel_type::grpc;
    return true;
  }
  return false;
}

std::string to_string(message_type value) {
  switch (value) {
    case message_type::register_agent:
      return "REGISTER";
    case message_type::register_ack:
      return "REGISTER_ACK";
    case message_type::heartbeat:
      return "HEARTBEAT";
    case message_type::heartbeat_ack:
      return "HEARTBEAT_ACK";
    case message_type::command_push:
      return "COMMAND_PUSH";
    case message_type::command_ack:
      return "COMMAND_ACK";
    case message_type::command_result:
      return "COMMAND_RESULT";
    case message_type::error:
      return "ERROR";
  }
  return "UNKNOWN";
}

bool try_parse_message_type(const std::string& text, message_type& out) {
  if (text == "REGISTER") {
    out = message_type::register_agent;
    return true;
  }
  if (text == "REGISTER_ACK") {
    out = message_type::register_ack;
    return true;
  }
  if (text == "HEARTBEAT") {
    out = message_type::heartbeat;
    return true;
  }
  if (text == "HEARTBEAT_ACK") {
    out = message_type::heartbeat_ack;
    return true;
  }
  if (text == "COMMAND_PUSH") {
    out = message_type::command_push;
    return true;
  }
  if (text == "COMMAND_ACK") {
    out = message_type::command_ack;
    return true;
  }
  if (text == "COMMAND_RESULT") {
    out = message_type::command_result;
    return true;
  }
  if (text == "ERROR") {
    out = message_type::error;
    return true;
  }
  return false;
}

std::string to_string(command_type value) {
  switch (value) {
    case command_type::wol_wake:
      return "WOL_WAKE";
    case command_type::host_reboot:
      return "HOST_REBOOT";
    case command_type::host_poweroff:
      return "HOST_POWEROFF";
    case command_type::host_probe_get:
      return "HOST_PROBE_GET";
    case command_type::monitoring_set:
      return "MONITORING_SET";
    case command_type::params_get:
      return "PARAMS_GET";
    case command_type::params_set:
      return "PARAMS_SET";
  }
  return "UNKNOWN";
}

bool try_parse_command_type(const std::string& text, command_type& out) {
  if (text == "WOL_WAKE") {
    out = command_type::wol_wake;
    return true;
  }
  if (text == "HOST_REBOOT") {
    out = command_type::host_reboot;
    return true;
  }
  if (text == "HOST_POWEROFF") {
    out = command_type::host_poweroff;
    return true;
  }
  if (text == "HOST_PROBE_GET") {
    out = command_type::host_probe_get;
    return true;
  }
  if (text == "MONITORING_SET") {
    out = command_type::monitoring_set;
    return true;
  }
  if (text == "PARAMS_GET") {
    out = command_type::params_get;
    return true;
  }
  if (text == "PARAMS_SET") {
    out = command_type::params_set;
    return true;
  }
  return false;
}

std::string to_string(command_status value) {
  switch (value) {
    case command_status::created:
      return "CREATED";
    case command_status::dispatched:
      return "DISPATCHED";
    case command_status::acked:
      return "ACKED";
    case command_status::running:
      return "RUNNING";
    case command_status::succeeded:
      return "SUCCEEDED";
    case command_status::failed:
      return "FAILED";
    case command_status::timed_out:
      return "TIMED_OUT";
    case command_status::cancelled:
      return "CANCELLED";
  }
  return "UNKNOWN";
}

bool try_parse_command_status(const std::string& text, command_status& out) {
  if (text == "CREATED") {
    out = command_status::created;
    return true;
  }
  if (text == "DISPATCHED") {
    out = command_status::dispatched;
    return true;
  }
  if (text == "ACKED") {
    out = command_status::acked;
    return true;
  }
  if (text == "RUNNING") {
    out = command_status::running;
    return true;
  }
  if (text == "SUCCEEDED") {
    out = command_status::succeeded;
    return true;
  }
  if (text == "FAILED") {
    out = command_status::failed;
    return true;
  }
  if (text == "TIMED_OUT") {
    out = command_status::timed_out;
    return true;
  }
  if (text == "CANCELLED") {
    out = command_status::cancelled;
    return true;
  }
  return false;
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
