#pragma once

#include "ctrl/infrastructure/sqlite_store.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace ctrl::infrastructure::sqlite_detail {

inline constexpr const char* kTerminalSucceeded = "succeeded";
inline constexpr const char* kTerminalFailed = "failed";
inline constexpr const char* kTerminalTimedOut = "timed_out";
inline constexpr const char* kTerminalCancelled = "cancelled";
inline constexpr const char* kNonTerminalPredicate =
    "state NOT IN ('succeeded','failed','timed_out','cancelled')";

int64_t unix_time_ms_now();

struct statement final {
  sqlite3_stmt* ptr = nullptr;

  statement() = default;
  statement(const statement&) = delete;
  statement& operator=(const statement&) = delete;

  ~statement();
};

bool begin_tx(sqlite3* db, std::string& error);
bool commit_tx(sqlite3* db, std::string& error);
void rollback_tx(sqlite3* db);
bool prepare(sqlite3* db, const std::string& sql, statement& stmt, std::string& error);
bool bind_int64(sqlite3_stmt* stmt, int idx, int64_t value, std::string& error);
bool bind_int(sqlite3_stmt* stmt, int idx, int value, std::string& error);
bool bind_text(sqlite3_stmt* stmt, int idx, std::string_view value, std::string& error);
bool step_done(sqlite3* db, sqlite3_stmt* stmt, std::string& error);

std::string column_text(sqlite3_stmt* stmt, int col);

bool parse_json_text(
    const std::string& text,
    nlohmann::json& out,
    std::string& error,
    std::string_view field);

bool read_command_row(sqlite3_stmt* stmt, domain::CommandSnapshot& out, std::string& error);
bool read_event_row(sqlite3_stmt* stmt, domain::CommandEvent& out, std::string& error);
bool read_agent_row(sqlite3_stmt* stmt, domain::AgentState& out, std::string& error);
bool read_audit_row(sqlite3_stmt* stmt, domain::AuditEntry& out, std::string& error);

bool is_terminal_state_text(std::string_view text);

} // namespace ctrl::infrastructure::sqlite_detail
