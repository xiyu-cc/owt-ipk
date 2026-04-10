#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace service {

struct command_record {
  std::string command_id;
  std::string agent_id;
  std::string idempotency_key;
  std::string command_type;
  std::string status;
  std::string payload_json;
  std::string result_json;
  int64_t issued_at_ms = 0;
  int64_t expires_at_ms = 0;
  int timeout_ms = 0;
  int max_retry = 0;
  int retry_count = 0;
  int64_t next_retry_at_ms = 0;
  std::string last_error;
  int64_t created_at_ms = 0;
  int64_t updated_at_ms = 0;
};

struct command_event_record {
  int64_t id = 0;
  std::string command_id;
  std::string event_type;
  std::string status;
  std::string detail_json;
  int64_t created_at_ms = 0;
};

struct agent_record {
  std::string agent_id;
  std::string site_id;
  std::string agent_version;
  std::string capabilities_json;
  bool online = false;
  int64_t registered_at_ms = 0;
  int64_t last_heartbeat_at_ms = 0;
  int64_t last_seen_at_ms = 0;
  std::string stats_json;
  int64_t updated_at_ms = 0;
};

struct audit_log_record {
  int64_t id = 0;
  std::string actor_type;
  std::string actor_id;
  std::string action;
  std::string resource_type;
  std::string resource_id;
  std::string summary_json;
  int64_t created_at_ms = 0;
};

struct agent_params_record {
  std::string agent_id;
  std::string params_json;
  int64_t updated_at_ms = 0;
};

bool init_command_store(std::string& error);
void shutdown_command_store();

bool upsert_command(const command_record& record, std::string& error);
bool update_command_status(
    const std::string& command_id,
    const std::string& status,
    const std::string& result_json,
    int64_t updated_at_ms,
    std::string& error);
bool update_command_terminal_status_once(
    const std::string& command_id,
    const std::string& final_status,
    const std::string& result_json,
    int64_t updated_at_ms,
    bool& applied,
    std::string& error);
bool append_command_event(
    const std::string& command_id,
    const std::string& event_type,
    const std::string& status,
    const std::string& detail_json,
    int64_t created_at_ms,
    std::string& error);
bool get_command(const std::string& command_id, command_record& out, std::string& error);
bool list_command_events(
    const std::string& command_id,
    int limit,
    std::vector<command_event_record>& out,
    std::string& error);
bool list_commands(
    const std::string& agent_id,
    const std::string& status,
    const std::string& command_type,
    int limit,
    std::vector<command_record>& out,
    std::string& error);
bool list_retry_ready_commands(
    int64_t now_ms,
    int limit,
    std::vector<command_record>& out,
    std::string& error);
bool update_command_retry_state(
    const std::string& command_id,
    const std::string& status,
    int retry_count,
    int64_t next_retry_at_ms,
    const std::string& last_error,
    int64_t updated_at_ms,
    std::string& error);
bool recover_inflight_commands(
    int64_t recovered_at_ms,
    int& recovered_count,
    std::string& error);

bool upsert_agent(const agent_record& record, std::string& error);
bool list_agents(std::vector<agent_record>& out, std::string& error);
bool set_all_agents_offline(int64_t updated_at_ms, std::string& error);

bool append_audit_log(
    const std::string& actor_type,
    const std::string& actor_id,
    const std::string& action,
    const std::string& resource_type,
    const std::string& resource_id,
    const std::string& summary_json,
    int64_t created_at_ms,
    std::string& error);
bool list_audit_logs(
    const std::string& action,
    const std::string& actor_id,
    const std::string& resource_type,
    const std::string& resource_id,
    int limit,
    std::vector<audit_log_record>& out,
    std::string& error);

bool upsert_agent_params(
    const std::string& agent_id,
    const std::string& params_json,
    int64_t updated_at_ms,
    std::string& error);
bool get_agent_params(
    const std::string& agent_id,
    agent_params_record& out,
    std::string& error);

} // namespace service
