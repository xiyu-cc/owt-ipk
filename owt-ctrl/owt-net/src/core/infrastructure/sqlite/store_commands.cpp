#include "internal.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ctrl::infrastructure {

namespace {

std::string invalid_transition_error(
    domain::CommandState current_state,
    domain::CommandState next_state) {
  return "invalid state transition: " + domain::to_string(current_state) + " -> " +
      domain::to_string(next_state);
}

bool try_parse_state_text_or_error(
    std::string_view text,
    domain::CommandState& out,
    std::string& error) {
  if (!domain::try_parse_command_state(text, out)) {
    error = "invalid command state in storage";
    return false;
  }
  return true;
}

} // namespace

bool SqliteStore::upsert(const domain::CommandSnapshot& row, std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement stmt;
  const std::string sql =
      "INSERT INTO commands("
      "command_id,trace_id,agent_mac,agent_id,command_type,payload_json,timeout_ms,max_retry,"
      "expires_at_ms,state,result_json,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms"
      ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(command_id) DO UPDATE SET "
      "trace_id=excluded.trace_id,"
      "agent_mac=excluded.agent_mac,"
      "agent_id=excluded.agent_id,"
      "command_type=excluded.command_type,"
      "payload_json=excluded.payload_json,"
      "timeout_ms=excluded.timeout_ms,"
      "max_retry=excluded.max_retry,"
      "expires_at_ms=excluded.expires_at_ms,"
      "state=excluded.state,"
      "result_json=excluded.result_json,"
      "retry_count=excluded.retry_count,"
      "next_retry_at_ms=excluded.next_retry_at_ms,"
      "last_error=excluded.last_error,"
      "created_at_ms=excluded.created_at_ms,"
      "updated_at_ms=excluded.updated_at_ms;";
  if (!prepare(db_, sql, stmt, error)) {
    return false;
  }

  int idx = 1;
  const auto payload = row.spec.payload.dump();
  const auto result = row.result.dump();
  if (!bind_text(stmt.ptr, idx++, row.spec.command_id, error) ||
      !bind_text(stmt.ptr, idx++, row.spec.trace_id, error) ||
      !bind_text(stmt.ptr, idx++, row.agent.mac, error) ||
      !bind_text(stmt.ptr, idx++, row.agent.display_id, error) ||
      !bind_text(stmt.ptr, idx++, domain::to_string(row.spec.kind), error) ||
      !bind_text(stmt.ptr, idx++, payload, error) ||
      !bind_int(stmt.ptr, idx++, row.spec.timeout_ms, error) ||
      !bind_int(stmt.ptr, idx++, row.spec.max_retry, error) ||
      !bind_int64(stmt.ptr, idx++, row.spec.expires_at_ms, error) ||
      !bind_text(stmt.ptr, idx++, domain::to_string(row.state), error) ||
      !bind_text(stmt.ptr, idx++, result, error) ||
      !bind_int(stmt.ptr, idx++, row.retry_count, error) ||
      !bind_int64(stmt.ptr, idx++, row.next_retry_at_ms, error) ||
      !bind_text(stmt.ptr, idx++, row.last_error, error) ||
      !bind_int64(stmt.ptr, idx++, row.created_at_ms, error) ||
      !bind_int64(stmt.ptr, idx++, row.updated_at_ms, error)) {
    return false;
  }

  return step_done(db_, stmt.ptr, error);
}

bool SqliteStore::get(
    std::string_view command_id,
    domain::CommandSnapshot& out,
    std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  statement stmt;
  const std::string sql =
      "SELECT command_id,trace_id,agent_mac,agent_id,command_type,payload_json,timeout_ms,max_retry,"
      "expires_at_ms,state,result_json,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms "
      "FROM commands WHERE command_id=? LIMIT 1;";
  if (!prepare(db_, sql, stmt, error) || !bind_text(stmt.ptr, 1, command_id, error)) {
    return false;
  }

  const auto rc = sqlite3_step(stmt.ptr);
  if (rc == SQLITE_DONE) {
    error = "command not found";
    return false;
  }
  if (rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (!read_command_row(stmt.ptr, out, error)) {
    return false;
  }
  error.clear();
  return true;
}

bool SqliteStore::list(
    const domain::CommandListFilter& filter,
    domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor>& out,
    std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  const int safe_limit = std::max(1, std::min(filter.limit, 500));

  std::ostringstream sql;
  sql << "SELECT command_id,trace_id,agent_mac,agent_id,command_type,payload_json,timeout_ms,max_retry,"
         "expires_at_ms,state,result_json,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms "
         "FROM commands WHERE 1=1";

  if (!filter.agent_mac.empty()) {
    sql << " AND agent_mac=?";
  }
  if (filter.state.has_value()) {
    sql << " AND state=?";
  }
  if (filter.kind.has_value()) {
    sql << " AND command_type=?";
  }
  if (filter.cursor.has_value()) {
    sql << " AND (created_at_ms < ? OR (created_at_ms = ? AND command_id < ?))";
  }
  sql << " ORDER BY created_at_ms DESC, command_id DESC LIMIT ?;";

  statement stmt;
  if (!prepare(db_, sql.str(), stmt, error)) {
    return false;
  }

  int idx = 1;
  if (!filter.agent_mac.empty() && !bind_text(stmt.ptr, idx++, filter.agent_mac, error)) {
    return false;
  }
  if (filter.state.has_value() &&
      !bind_text(stmt.ptr, idx++, domain::to_string(*filter.state), error)) {
    return false;
  }
  if (filter.kind.has_value() && !bind_text(stmt.ptr, idx++, domain::to_string(*filter.kind), error)) {
    return false;
  }
  if (filter.cursor.has_value()) {
    if (!bind_int64(stmt.ptr, idx++, filter.cursor->created_at_ms, error) ||
        !bind_int64(stmt.ptr, idx++, filter.cursor->created_at_ms, error) ||
        !bind_text(stmt.ptr, idx++, filter.cursor->command_id, error)) {
      return false;
    }
  }
  if (!bind_int(stmt.ptr, idx++, safe_limit + 1, error)) {
    return false;
  }

  out = {};
  while (true) {
    const auto rc = sqlite3_step(stmt.ptr);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      error = sqlite3_errmsg(db_);
      return false;
    }

    domain::CommandSnapshot row;
    if (!read_command_row(stmt.ptr, row, error)) {
      return false;
    }

    if (static_cast<int>(out.items.size()) < safe_limit) {
      out.items.push_back(std::move(row));
    } else {
      out.has_more = true;
      break;
    }
  }

  if (out.has_more && !out.items.empty()) {
    const auto& last = out.items.back();
    out.next_cursor = domain::CommandListCursor{last.created_at_ms, last.spec.command_id};
  }
  error.clear();
  return true;
}

bool SqliteStore::append_event(const domain::CommandEvent& event, std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(
          db_,
          "INSERT INTO command_events(command_id,event_type,state,detail_json,created_at_ms) "
          "VALUES(?,?,?,?,?);",
          stmt,
          error)) {
    return false;
  }

  const auto detail = event.detail.dump();
  if (!bind_text(stmt.ptr, 1, event.command_id, error) ||
      !bind_text(stmt.ptr, 2, event.type, error) ||
      !bind_text(stmt.ptr, 3, domain::to_string(event.state), error) ||
      !bind_text(stmt.ptr, 4, detail, error) ||
      !bind_int64(stmt.ptr, 5, event.created_at_ms, error)) {
    return false;
  }

  return step_done(db_, stmt.ptr, error);
}

bool SqliteStore::list_events(
    std::string_view command_id,
    int limit,
    std::vector<domain::CommandEvent>& out,
    std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  const int safe_limit = std::max(1, std::min(limit, 500));

  statement stmt;
  if (!prepare(
          db_,
          "SELECT command_id,event_type,state,detail_json,created_at_ms "
          "FROM command_events WHERE command_id=? ORDER BY id ASC LIMIT ?;",
          stmt,
          error) ||
      !bind_text(stmt.ptr, 1, command_id, error) ||
      !bind_int(stmt.ptr, 2, safe_limit, error)) {
    return false;
  }

  out.clear();
  while (true) {
    const auto rc = sqlite3_step(stmt.ptr);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      error = sqlite3_errmsg(db_);
      return false;
    }

    domain::CommandEvent row;
    if (!read_event_row(stmt.ptr, row, error)) {
      return false;
    }
    out.push_back(std::move(row));
  }

  error.clear();
  return true;
}

bool SqliteStore::update_state_if_not_terminal(
    std::string_view command_id,
    domain::CommandState next_state,
    const nlohmann::json& result,
    int64_t updated_at_ms,
    bool& applied,
    std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement read_stmt;
  if (!prepare(db_, "SELECT state FROM commands WHERE command_id=? LIMIT 1;", read_stmt, error) ||
      !bind_text(read_stmt.ptr, 1, command_id, error)) {
    return false;
  }

  const auto read_rc = sqlite3_step(read_stmt.ptr);
  if (read_rc == SQLITE_DONE) {
    error = "command not found";
    return false;
  }
  if (read_rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  const auto current_state = column_text(read_stmt.ptr, 0);
  if (is_terminal_state_text(current_state)) {
    applied = false;
    error.clear();
    return true;
  }

  domain::CommandState current_state_enum = domain::CommandState::Created;
  if (!try_parse_state_text_or_error(current_state, current_state_enum, error)) {
    return false;
  }
  if (!domain::is_allowed_non_terminal_transition(current_state_enum, next_state)) {
    error = invalid_transition_error(current_state_enum, next_state);
    return false;
  }

  statement write_stmt;
  if (!prepare(
          db_,
          "UPDATE commands SET state=?, result_json=?, next_retry_at_ms=0, last_error='', updated_at_ms=? "
          "WHERE command_id=?;",
          write_stmt,
          error) ||
      !bind_text(write_stmt.ptr, 1, domain::to_string(next_state), error) ||
      !bind_text(write_stmt.ptr, 2, result.dump(), error) ||
      !bind_int64(write_stmt.ptr, 3, updated_at_ms, error) ||
      !bind_text(write_stmt.ptr, 4, command_id, error) ||
      !step_done(db_, write_stmt.ptr, error)) {
    return false;
  }

  applied = sqlite3_changes(db_) > 0;
  error.clear();
  return true;
}

bool SqliteStore::update_terminal_state_once(
    std::string_view command_id,
    domain::CommandState terminal_state,
    const nlohmann::json& result,
    int64_t updated_at_ms,
    bool& applied,
    std::string& error) {
  using namespace sqlite_detail;

  if (!domain::is_terminal(terminal_state)) {
    error = "terminal_state is not terminal";
    return false;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement read_stmt;
  if (!prepare(db_, "SELECT state FROM commands WHERE command_id=? LIMIT 1;", read_stmt, error) ||
      !bind_text(read_stmt.ptr, 1, command_id, error)) {
    return false;
  }

  const auto read_rc = sqlite3_step(read_stmt.ptr);
  if (read_rc == SQLITE_DONE) {
    error = "command not found";
    return false;
  }
  if (read_rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  const auto current_state = column_text(read_stmt.ptr, 0);
  if (is_terminal_state_text(current_state)) {
    applied = false;
    error.clear();
    return true;
  }

  domain::CommandState current_state_enum = domain::CommandState::Created;
  if (!try_parse_state_text_or_error(current_state, current_state_enum, error)) {
    return false;
  }
  if (!domain::is_allowed_terminal_transition(current_state_enum)) {
    error = invalid_transition_error(current_state_enum, terminal_state);
    return false;
  }

  statement write_stmt;
  if (!prepare(
          db_,
          "UPDATE commands SET state=?, result_json=?, next_retry_at_ms=0, last_error='', updated_at_ms=? "
          "WHERE command_id=?;",
          write_stmt,
          error) ||
      !bind_text(write_stmt.ptr, 1, domain::to_string(terminal_state), error) ||
      !bind_text(write_stmt.ptr, 2, result.dump(), error) ||
      !bind_int64(write_stmt.ptr, 3, updated_at_ms, error) ||
      !bind_text(write_stmt.ptr, 4, command_id, error) ||
      !step_done(db_, write_stmt.ptr, error)) {
    return false;
  }

  applied = sqlite3_changes(db_) > 0;
  error.clear();
  return true;
}

bool SqliteStore::list_retry_ready(
    int64_t now_ms,
    int limit,
    std::vector<domain::CommandSnapshot>& out,
    std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  const int safe_limit = std::max(1, std::min(limit, 500));

  statement stmt;
  if (!prepare(
          db_,
          "SELECT command_id,trace_id,agent_mac,agent_id,command_type,payload_json,timeout_ms,max_retry,"
          "expires_at_ms,state,result_json,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms "
          "FROM commands WHERE state='retry_pending' AND retry_count < max_retry AND next_retry_at_ms <= ? "
          "ORDER BY next_retry_at_ms ASC, updated_at_ms ASC LIMIT ?;",
          stmt,
          error) ||
      !bind_int64(stmt.ptr, 1, now_ms, error) ||
      !bind_int(stmt.ptr, 2, safe_limit, error)) {
    return false;
  }

  out.clear();
  while (true) {
    const auto rc = sqlite3_step(stmt.ptr);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      error = sqlite3_errmsg(db_);
      return false;
    }

    domain::CommandSnapshot row;
    if (!read_command_row(stmt.ptr, row, error)) {
      return false;
    }
    out.push_back(std::move(row));
  }

  error.clear();
  return true;
}

bool SqliteStore::update_retry_state(
    std::string_view command_id,
    domain::CommandState next_state,
    int retry_count,
    int64_t next_retry_at_ms,
    std::string_view last_error,
    int64_t updated_at_ms,
    std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement read_stmt;
  if (!prepare(db_, "SELECT state FROM commands WHERE command_id=? LIMIT 1;", read_stmt, error) ||
      !bind_text(read_stmt.ptr, 1, command_id, error)) {
    return false;
  }

  const auto read_rc = sqlite3_step(read_stmt.ptr);
  if (read_rc == SQLITE_DONE) {
    error = "command not found or already terminal";
    return false;
  }
  if (read_rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (is_terminal_state_text(column_text(read_stmt.ptr, 0))) {
    error = "command not found or already terminal";
    return false;
  }
  const auto current_state_text = column_text(read_stmt.ptr, 0);
  domain::CommandState current_state = domain::CommandState::Created;
  if (!try_parse_state_text_or_error(current_state_text, current_state, error)) {
    return false;
  }
  if (next_state != domain::CommandState::RetryPending ||
      current_state != domain::CommandState::RetryPending) {
    error = invalid_transition_error(current_state, next_state);
    return false;
  }

  statement write_stmt;
  if (!prepare(
          db_,
          "UPDATE commands SET state=?, retry_count=?, next_retry_at_ms=?, last_error=?, updated_at_ms=? "
          "WHERE command_id=?;",
          write_stmt,
          error) ||
      !bind_text(write_stmt.ptr, 1, domain::to_string(next_state), error) ||
      !bind_int(write_stmt.ptr, 2, retry_count, error) ||
      !bind_int64(write_stmt.ptr, 3, next_retry_at_ms, error) ||
      !bind_text(write_stmt.ptr, 4, last_error, error) ||
      !bind_int64(write_stmt.ptr, 5, updated_at_ms, error) ||
      !bind_text(write_stmt.ptr, 6, command_id, error) ||
      !step_done(db_, write_stmt.ptr, error)) {
    return false;
  }

  error.clear();
  return true;
}

bool SqliteStore::recover_inflight(int64_t recovered_at_ms, int& recovered_count, std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement list_stmt;
  if (!prepare(
          db_,
          std::string("SELECT command_id FROM commands WHERE ") + kNonTerminalPredicate + " ORDER BY created_at_ms ASC;",
          list_stmt,
          error)) {
    return false;
  }

  std::vector<std::string> command_ids;
  while (true) {
    const auto rc = sqlite3_step(list_stmt.ptr);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      error = sqlite3_errmsg(db_);
      return false;
    }
    command_ids.push_back(column_text(list_stmt.ptr, 0));
  }

  recovered_count = 0;
  if (command_ids.empty()) {
    error.clear();
    return true;
  }

  if (!begin_tx(db_, error)) {
    return false;
  }

  statement update_stmt;
  statement event_stmt;
  if (!prepare(
          db_,
          "UPDATE commands SET state='timed_out', result_json=?, next_retry_at_ms=0, last_error='', updated_at_ms=? "
          "WHERE command_id=? AND " + std::string(kNonTerminalPredicate) + ";",
          update_stmt,
          error) ||
      !prepare(
          db_,
          "INSERT INTO command_events(command_id,event_type,state,detail_json,created_at_ms) VALUES(?,?,?,?,?);",
          event_stmt,
          error)) {
    rollback_tx(db_);
    return false;
  }

  const auto result_json = nlohmann::json{
      {"error_code", "ctrl_restart_recovery"},
      {"message", "controller restarted before terminal result"},
  }
                               .dump();
  const auto event_detail = nlohmann::json{{"reason", "ctrl_restart_recovery"}}.dump();

  for (const auto& command_id : command_ids) {
    sqlite3_reset(update_stmt.ptr);
    sqlite3_clear_bindings(update_stmt.ptr);
    if (!bind_text(update_stmt.ptr, 1, result_json, error) ||
        !bind_int64(update_stmt.ptr, 2, recovered_at_ms, error) ||
        !bind_text(update_stmt.ptr, 3, command_id, error) ||
        !step_done(db_, update_stmt.ptr, error)) {
      rollback_tx(db_);
      return false;
    }

    if (sqlite3_changes(db_) <= 0) {
      continue;
    }

    sqlite3_reset(event_stmt.ptr);
    sqlite3_clear_bindings(event_stmt.ptr);
    if (!bind_text(event_stmt.ptr, 1, command_id, error) ||
        !bind_text(event_stmt.ptr, 2, "command_recovery_timeout", error) ||
        !bind_text(event_stmt.ptr, 3, "timed_out", error) ||
        !bind_text(event_stmt.ptr, 4, event_detail, error) ||
        !bind_int64(event_stmt.ptr, 5, recovered_at_ms, error) ||
        !step_done(db_, event_stmt.ptr, error)) {
      rollback_tx(db_);
      return false;
    }

    ++recovered_count;
  }

  if (!commit_tx(db_, error)) {
    rollback_tx(db_);
    return false;
  }

  error.clear();
  return true;
}

} // namespace ctrl::infrastructure
