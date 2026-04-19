#include "internal.h"

#include "ctrl/domain/types.h"

#include <chrono>

namespace ctrl::infrastructure::sqlite_detail {

int64_t unix_time_ms_now() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

statement::~statement() {
  if (ptr != nullptr) {
    sqlite3_finalize(ptr);
    ptr = nullptr;
  }
}

bool begin_tx(sqlite3* db, std::string& error) {
  char* err = nullptr;
  const auto rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    error = err != nullptr ? err : "begin transaction failed";
    sqlite3_free(err);
    return false;
  }
  return true;
}

bool commit_tx(sqlite3* db, std::string& error) {
  char* err = nullptr;
  const auto rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    error = err != nullptr ? err : "commit transaction failed";
    sqlite3_free(err);
    return false;
  }
  return true;
}

void rollback_tx(sqlite3* db) {
  char* err = nullptr;
  (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, &err);
  sqlite3_free(err);
}

bool prepare(sqlite3* db, const std::string& sql, statement& stmt, std::string& error) {
  const auto rc = sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &stmt.ptr, nullptr);
  if (rc != SQLITE_OK) {
    error = sqlite3_errmsg(db);
    return false;
  }
  return true;
}

bool bind_int64(sqlite3_stmt* stmt, int idx, int64_t value, std::string& error) {
  const auto rc = sqlite3_bind_int64(stmt, idx, value);
  if (rc != SQLITE_OK) {
    error = sqlite3_errstr(rc);
    return false;
  }
  return true;
}

bool bind_int(sqlite3_stmt* stmt, int idx, int value, std::string& error) {
  const auto rc = sqlite3_bind_int(stmt, idx, value);
  if (rc != SQLITE_OK) {
    error = sqlite3_errstr(rc);
    return false;
  }
  return true;
}

bool bind_text(sqlite3_stmt* stmt, int idx, std::string_view value, std::string& error) {
  const auto rc = sqlite3_bind_text(
      stmt,
      idx,
      value.data(),
      static_cast<int>(value.size()),
      SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    error = sqlite3_errstr(rc);
    return false;
  }
  return true;
}

bool step_done(sqlite3* db, sqlite3_stmt* stmt, std::string& error) {
  const auto rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(db);
    return false;
  }
  return true;
}

std::string column_text(sqlite3_stmt* stmt, int col) {
  const auto* raw = sqlite3_column_text(stmt, col);
  if (raw == nullptr) {
    return {};
  }
  return reinterpret_cast<const char*>(raw);
}

bool parse_json_text(const std::string& text, nlohmann::json& out, std::string& error, std::string_view field) {
  auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (parsed.is_discarded()) {
    error = "invalid json field: " + std::string(field);
    return false;
  }
  out = std::move(parsed);
  return true;
}

bool read_command_row(sqlite3_stmt* stmt, domain::CommandSnapshot& out, std::string& error) {
  out = {};
  out.spec.command_id = column_text(stmt, 0);
  out.spec.trace_id = column_text(stmt, 1);
  out.agent.mac = column_text(stmt, 2);
  out.agent.display_id = column_text(stmt, 3);

  if (!domain::try_parse_command_kind(column_text(stmt, 4), out.spec.kind)) {
    error = "invalid command kind in storage";
    return false;
  }

  if (!parse_json_text(column_text(stmt, 5), out.spec.payload, error, "payload_json")) {
    return false;
  }

  out.spec.timeout_ms = sqlite3_column_int(stmt, 6);
  out.spec.max_retry = sqlite3_column_int(stmt, 7);
  out.spec.expires_at_ms = sqlite3_column_int64(stmt, 8);

  if (!domain::try_parse_command_state(column_text(stmt, 9), out.state)) {
    error = "invalid command state in storage";
    return false;
  }

  if (!parse_json_text(column_text(stmt, 10), out.result, error, "result_json")) {
    return false;
  }

  out.retry_count = sqlite3_column_int(stmt, 11);
  out.next_retry_at_ms = sqlite3_column_int64(stmt, 12);
  out.last_error = column_text(stmt, 13);
  out.created_at_ms = sqlite3_column_int64(stmt, 14);
  out.updated_at_ms = sqlite3_column_int64(stmt, 15);
  return true;
}

bool read_event_row(sqlite3_stmt* stmt, domain::CommandEvent& out, std::string& error) {
  out = {};
  out.command_id = column_text(stmt, 0);
  out.type = column_text(stmt, 1);
  if (!domain::try_parse_command_state(column_text(stmt, 2), out.state)) {
    error = "invalid command event state in storage";
    return false;
  }
  if (!parse_json_text(column_text(stmt, 3), out.detail, error, "detail_json")) {
    return false;
  }
  out.created_at_ms = sqlite3_column_int64(stmt, 4);
  return true;
}

bool read_agent_row(sqlite3_stmt* stmt, domain::AgentState& out, std::string& error) {
  out = {};
  out.agent.mac = column_text(stmt, 0);
  out.agent.display_id = column_text(stmt, 1);
  out.online = sqlite3_column_int(stmt, 2) != 0;
  out.site_id = column_text(stmt, 3);
  out.version = column_text(stmt, 4);

  nlohmann::json capabilities_json;
  if (!parse_json_text(column_text(stmt, 5), capabilities_json, error, "capabilities_json")) {
    return false;
  }
  if (!capabilities_json.is_array()) {
    error = "invalid capabilities_json type in storage";
    return false;
  }
  out.capabilities.clear();
  for (const auto& item : capabilities_json) {
    if (!item.is_string()) {
      error = "invalid capabilities_json item in storage";
      return false;
    }
    out.capabilities.push_back(item.get<std::string>());
  }

  if (!parse_json_text(column_text(stmt, 6), out.stats, error, "stats_json")) {
    return false;
  }
  if (!out.stats.is_object()) {
    error = "invalid stats_json type in storage";
    return false;
  }

  out.registered_at_ms = sqlite3_column_int64(stmt, 7);
  out.last_seen_at_ms = sqlite3_column_int64(stmt, 8);
  out.last_heartbeat_at_ms = sqlite3_column_int64(stmt, 9);
  return true;
}

bool read_audit_row(sqlite3_stmt* stmt, domain::AuditEntry& out, std::string& error) {
  out = {};
  out.id = sqlite3_column_int64(stmt, 0);
  out.actor_type = column_text(stmt, 1);
  out.actor_id = column_text(stmt, 2);
  out.action = column_text(stmt, 3);
  out.resource_type = column_text(stmt, 4);
  out.resource_id = column_text(stmt, 5);
  if (!parse_json_text(column_text(stmt, 6), out.summary, error, "summary_json")) {
    return false;
  }
  out.created_at_ms = sqlite3_column_int64(stmt, 7);
  return true;
}

bool is_terminal_state_text(std::string_view text) {
  return text == kTerminalSucceeded || text == kTerminalFailed || text == kTerminalTimedOut ||
         text == kTerminalCancelled;
}

} // namespace ctrl::infrastructure::sqlite_detail
