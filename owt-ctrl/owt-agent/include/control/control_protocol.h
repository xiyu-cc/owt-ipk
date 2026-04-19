#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace control {

enum class message_type {
  register_agent,
  register_ack,
  heartbeat,
  heartbeat_ack,
  command_push,
  command_ack,
  command_result,
  error,
};

enum class command_type {
  wol_wake,
  host_reboot,
  host_poweroff,
  host_probe_get,
  monitoring_set,
  params_get,
  params_set,
};

enum class command_status {
  created,
  dispatched,
  acked,
  running,
  retry_pending,
  succeeded,
  failed,
  timed_out,
  cancelled,
};

std::string to_string(message_type value);
std::string to_string(command_type value);
std::string to_string(command_status value);
bool try_parse_message_type(const std::string& text, message_type& out);
bool try_parse_command_type(const std::string& text, command_type& out);
bool try_parse_command_status(const std::string& text, command_status& out);

const char* current_protocol_version() noexcept;
bool is_supported_protocol_version(const std::string& version) noexcept;

int64_t unix_time_ms_now();
std::string make_message_id();

struct register_payload {
  std::string agent_mac;
  std::string agent_id;
  std::string site_id;
  std::string agent_version;
  std::vector<std::string> capabilities;
};

struct heartbeat_payload {
  int64_t heartbeat_at_ms = 0;
  nlohmann::json stats = nlohmann::json::object();
};

struct register_ack_payload {
  bool ok = false;
  std::string message;
};

struct heartbeat_ack_payload {
  int64_t server_time_ms = 0;
};

struct command {
  std::string command_id;
  std::string idempotency_key;
  command_type type = command_type::host_probe_get;
  int64_t issued_at_ms = 0;
  int64_t expires_at_ms = 0;
  int timeout_ms = 0;
  int max_retry = 0;
  nlohmann::json payload = nlohmann::json::object();
};

struct command_ack_payload {
  std::string command_id;
  command_status status = command_status::acked;
  std::string message;
};

struct command_result_payload {
  std::string command_id;
  command_status final_status = command_status::failed;
  int exit_code = 0;
  nlohmann::json result = nlohmann::json::object();
};

struct error_payload {
  std::string code;
  std::string message;
  std::string detail;
};

using payload_variant = std::variant<
    std::monostate,
    register_payload,
    heartbeat_payload,
    register_ack_payload,
    heartbeat_ack_payload,
    command,
    command_ack_payload,
    command_result_payload,
    error_payload>;

struct envelope {
  message_type type = message_type::heartbeat;
  std::string protocol_version = "v3";
  int64_t sent_at_ms = 0;
  std::string trace_id;
  std::string agent_id;
  payload_variant payload;
};

} // namespace control
