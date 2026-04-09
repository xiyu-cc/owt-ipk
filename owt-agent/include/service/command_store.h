#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace service {

struct command_record {
  std::string command_id;
  std::string idempotency_key;
  std::string command_type;
  std::string status;
  std::string channel_type;
  std::string payload_json;
  std::string result_json;
  int64_t created_at_ms = 0;
  int64_t updated_at_ms = 0;
};

struct command_event_record {
  int64_t id = 0;
  std::string command_id;
  std::string event_type;
  std::string status;
  std::string channel_type;
  std::string detail_json;
  int64_t created_at_ms = 0;
};

bool init_command_store(std::string& error);
void shutdown_command_store();

bool upsert_command(const command_record& record, std::string& error);
bool update_command_status(
    const std::string& command_id,
    const std::string& status,
    const std::string& channel_type,
    const std::string& result_json,
    int64_t updated_at_ms,
    std::string& error);
bool append_command_event(
    const std::string& command_id,
    const std::string& event_type,
    const std::string& status,
    const std::string& channel_type,
    const std::string& detail_json,
    int64_t created_at_ms,
    std::string& error);
bool get_command(const std::string& command_id, command_record& out, std::string& error);
bool list_command_events(
    const std::string& command_id,
    int limit,
    std::vector<command_event_record>& out,
    std::string& error);

} // namespace service
