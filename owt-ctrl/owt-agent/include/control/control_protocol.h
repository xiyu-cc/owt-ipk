#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace control {

enum class message_type {
  agent_register,
  server_register_ack,
  agent_heartbeat,
  server_command_dispatch,
  agent_command_ack,
  agent_command_result,
  server_error,
};

enum class command_type {
  wol_wake,
  host_reboot,
  host_poweroff,
  monitoring_set,
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
  std::string agent_mac;
  std::string agent_id;
  int64_t heartbeat_at_ms = 0;
  nlohmann::json stats = nlohmann::json::object();
};

struct register_ack_payload {
  bool ok = false;
  std::string message;
};

struct command {
  std::string command_id;
  std::string idempotency_key;
  command_type type = command_type::wol_wake;
  int64_t issued_at_ms = 0;
  int64_t expires_at_ms = 0;
  int timeout_ms = 0;
  int max_retry = 0;
  nlohmann::json payload = nlohmann::json::object();
};

struct command_ack_payload {
  std::string agent_mac;
  std::string agent_id;
  std::string command_id;
  command_status status = command_status::acked;
  std::string message;
};

struct command_result_payload {
  std::string agent_mac;
  std::string agent_id;
  std::string command_id;
  command_status final_status = command_status::failed;
  int exit_code = 0;
  nlohmann::json result = nlohmann::json::object();
};

struct error_payload {
  std::string code;
  std::string message;
  nlohmann::json detail = nlohmann::json::object();
};

using payload_variant = std::variant<
    std::monostate,
    register_payload,
    heartbeat_payload,
    register_ack_payload,
    command,
    command_ack_payload,
    command_result_payload,
    error_payload>;

struct envelope {
  message_type type = message_type::agent_heartbeat;
  std::string version = "v5";
  nlohmann::json id = nullptr;
  int64_t ts_ms = 0;
  std::string target;
  payload_variant payload;
};

} // namespace control
