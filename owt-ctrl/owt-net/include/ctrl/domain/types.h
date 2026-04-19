#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ctrl::domain {

enum class CommandKind {
  WakeOnLan,
  HostReboot,
  HostPoweroff,
  HostProbeGet,
  MonitoringSet,
  ParamsGet,
  ParamsSet,
};

enum class CommandState {
  Created,
  Dispatched,
  Acked,
  Running,
  RetryPending,
  Succeeded,
  Failed,
  TimedOut,
  Cancelled,
};

std::string to_string(CommandKind value);
std::string to_string(CommandState value);
bool try_parse_command_kind(std::string_view text, CommandKind& out);
bool try_parse_command_state(std::string_view text, CommandState& out);
bool is_terminal(CommandState value);

struct AgentRef {
  std::string mac;
  std::string display_id;
};

struct CommandSpec {
  std::string command_id;
  std::string trace_id;
  CommandKind kind = CommandKind::HostProbeGet;
  nlohmann::json payload = nlohmann::json::object();
  int timeout_ms = 5000;
  int max_retry = 0;
  int64_t expires_at_ms = 0;
};

struct CommandSnapshot {
  CommandSpec spec;
  AgentRef agent;
  CommandState state = CommandState::Created;
  nlohmann::json result = nullptr;
  int retry_count = 0;
  int64_t next_retry_at_ms = 0;
  std::string last_error;
  int64_t created_at_ms = 0;
  int64_t updated_at_ms = 0;
};

struct CommandEvent {
  std::string command_id;
  std::string type;
  CommandState state = CommandState::Created;
  nlohmann::json detail = nlohmann::json::object();
  int64_t created_at_ms = 0;
};

struct AgentState {
  AgentRef agent;
  bool online = false;
  std::string site_id;
  std::string version;
  std::vector<std::string> capabilities;
  nlohmann::json stats = nlohmann::json::object();
  int64_t registered_at_ms = 0;
  int64_t last_seen_at_ms = 0;
  int64_t last_heartbeat_at_ms = 0;
};

struct AuditEntry {
  int64_t id = 0;
  std::string actor_type;
  std::string actor_id;
  std::string action;
  std::string resource_type;
  std::string resource_id;
  nlohmann::json summary = nlohmann::json::object();
  int64_t created_at_ms = 0;
};

struct CommandListCursor {
  int64_t created_at_ms = 0;
  std::string command_id;
};

struct CommandListFilter {
  std::string agent_mac;
  std::optional<CommandState> state;
  std::optional<CommandKind> kind;
  int limit = 50;
  std::optional<CommandListCursor> cursor;
};

struct AuditListCursor {
  int64_t id = 0;
};

struct AuditListFilter {
  std::optional<std::string> action;
  std::optional<std::string> actor_type;
  std::optional<std::string> actor_id;
  std::optional<std::string> resource_type;
  std::optional<std::string> resource_id;
  int limit = 50;
  std::optional<AuditListCursor> cursor;
};

template <typename Item, typename Cursor>
struct ListPage {
  std::vector<Item> items;
  bool has_more = false;
  std::optional<Cursor> next_cursor;
};

} // namespace ctrl::domain
