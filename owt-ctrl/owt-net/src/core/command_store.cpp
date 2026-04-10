#include "service/command_store.h"

#include "log.h"

#include <sqlite3.h>
#include <unistd.h>

#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

namespace service {

namespace {

constexpr const char* kSystemDbPath = "/etc/owt-net/owt_net.db";
constexpr const char* kLocalDbPath = "owt_net.db";

std::mutex g_db_mutex;
sqlite3* g_db = nullptr;
std::string g_db_path;

std::string choose_db_path() {
  if (const char* override_path = std::getenv("OWT_CTRL_DB_PATH")) {
    if (override_path[0] != '\0') {
      return override_path;
    }
  }

  try {
    const std::filesystem::path dir("/etc/owt-net");
    if (std::filesystem::exists(dir)) {
      if (::access(dir.c_str(), W_OK) == 0) {
        return kSystemDbPath;
      }
      return kLocalDbPath;
    }

    std::filesystem::create_directories(dir);
    if (::access(dir.c_str(), W_OK) == 0) {
      return kSystemDbPath;
    }
  } catch (...) {
    // fall through
  }
  return kLocalDbPath;
}

bool exec_sql(sqlite3* db, const char* sql, std::string& error) {
  char* err_msg = nullptr;
  const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
  if (rc == SQLITE_OK) {
    return true;
  }
  error = err_msg ? err_msg : "sqlite exec failed";
  sqlite3_free(err_msg);
  return false;
}

void bind_text(sqlite3_stmt* stmt, int idx, const std::string& value) {
  sqlite3_bind_text(stmt, idx, value.c_str(), -1, SQLITE_TRANSIENT);
}

std::string read_text(sqlite3_stmt* stmt, int idx) {
  const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
  return value != nullptr ? value : "";
}

bool is_terminal_status(const std::string& status) {
  return status == "SUCCEEDED" || status == "FAILED" || status == "TIMED_OUT" ||
         status == "CANCELLED";
}

bool table_has_column(sqlite3* db, const char* table, const char* column, std::string& error) {
  const std::string sql = std::string("PRAGMA table_info(") + table + ");";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(db);
    return false;
  }

  bool found = false;
  for (;;) {
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      const std::string name = read_text(stmt, 1);
      if (name == column) {
        found = true;
        break;
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    sqlite3_finalize(stmt);
    error = sqlite3_errmsg(db);
    return false;
  }
  sqlite3_finalize(stmt);
  return found;
}

bool ensure_commands_schema(sqlite3* db, std::string& error) {
  static const struct {
    const char* column;
    const char* alter_sql;
  } kMigrations[] = {
      {"agent_id", "ALTER TABLE commands ADD COLUMN agent_id TEXT NOT NULL DEFAULT '';"},
      {"issued_at_ms", "ALTER TABLE commands ADD COLUMN issued_at_ms INTEGER NOT NULL DEFAULT 0;"},
      {"expires_at_ms", "ALTER TABLE commands ADD COLUMN expires_at_ms INTEGER NOT NULL DEFAULT 0;"},
      {"timeout_ms", "ALTER TABLE commands ADD COLUMN timeout_ms INTEGER NOT NULL DEFAULT 0;"},
      {"max_retry", "ALTER TABLE commands ADD COLUMN max_retry INTEGER NOT NULL DEFAULT 0;"},
      {"retry_count", "ALTER TABLE commands ADD COLUMN retry_count INTEGER NOT NULL DEFAULT 0;"},
      {"next_retry_at_ms", "ALTER TABLE commands ADD COLUMN next_retry_at_ms INTEGER NOT NULL DEFAULT 0;"},
      {"last_error", "ALTER TABLE commands ADD COLUMN last_error TEXT NOT NULL DEFAULT '';"},
  };

  error.clear();
  for (const auto& migration : kMigrations) {
    const bool has_column = table_has_column(db, "commands", migration.column, error);
    if (!error.empty()) {
      return false;
    }
    if (has_column) {
      continue;
    }
    if (!exec_sql(db, migration.alter_sql, error)) {
      return false;
    }
  }
  return true;
}

} // namespace

bool init_command_store(std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db != nullptr) {
    return true;
  }

  g_db_path = choose_db_path();
  if (sqlite3_open_v2(
          g_db_path.c_str(),
          &g_db,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
          nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    if (g_db != nullptr) {
      sqlite3_close(g_db);
      g_db = nullptr;
    }
    return false;
  }

  if (!exec_sql(g_db, "PRAGMA journal_mode=WAL;", error) ||
      !exec_sql(g_db, "PRAGMA synchronous=NORMAL;", error) ||
      !exec_sql(g_db, "PRAGMA foreign_keys=ON;", error)) {
    sqlite3_close(g_db);
    g_db = nullptr;
    return false;
  }
  sqlite3_busy_timeout(g_db, 3000);

  static const char* kCreateCommandsSql = R"SQL(
CREATE TABLE IF NOT EXISTS commands (
  command_id TEXT PRIMARY KEY,
  agent_id TEXT NOT NULL DEFAULT '',
  idempotency_key TEXT NOT NULL,
  command_type TEXT NOT NULL,
  status TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  result_json TEXT NOT NULL DEFAULT '',
  issued_at_ms INTEGER NOT NULL DEFAULT 0,
  expires_at_ms INTEGER NOT NULL DEFAULT 0,
  timeout_ms INTEGER NOT NULL DEFAULT 0,
  max_retry INTEGER NOT NULL DEFAULT 0,
  retry_count INTEGER NOT NULL DEFAULT 0,
  next_retry_at_ms INTEGER NOT NULL DEFAULT 0,
  last_error TEXT NOT NULL DEFAULT '',
  created_at_ms INTEGER NOT NULL,
  updated_at_ms INTEGER NOT NULL
);
)SQL";

  static const char* kCreateCommandsCreatedIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_commands_created
ON commands(created_at_ms DESC, command_id DESC);
)SQL";

  static const char* kCreateCommandsAgentIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_commands_agent_status_created
ON commands(agent_id, status, created_at_ms DESC, command_id DESC);
)SQL";

  static const char* kCreateCommandsRetryIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_commands_retry_pending
ON commands(status, next_retry_at_ms, updated_at_ms, command_id);
)SQL";

  static const char* kCreateEventsSql = R"SQL(
CREATE TABLE IF NOT EXISTS command_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  command_id TEXT NOT NULL,
  event_type TEXT NOT NULL,
  status TEXT NOT NULL,
  detail_json TEXT NOT NULL,
  created_at_ms INTEGER NOT NULL
);
)SQL";

  static const char* kCreateEventsIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_command_events_command_id
ON command_events(command_id, id);
)SQL";

  static const char* kCreateAgentsSql = R"SQL(
CREATE TABLE IF NOT EXISTS agents (
  agent_id TEXT PRIMARY KEY,
  site_id TEXT NOT NULL DEFAULT '',
  agent_version TEXT NOT NULL DEFAULT '',
  capabilities_json TEXT NOT NULL DEFAULT '[]',
  online INTEGER NOT NULL DEFAULT 0,
  registered_at_ms INTEGER NOT NULL DEFAULT 0,
  last_heartbeat_at_ms INTEGER NOT NULL DEFAULT 0,
  last_seen_at_ms INTEGER NOT NULL DEFAULT 0,
  stats_json TEXT NOT NULL DEFAULT '',
  updated_at_ms INTEGER NOT NULL DEFAULT 0
);
)SQL";

  static const char* kCreateSitesSql = R"SQL(
CREATE TABLE IF NOT EXISTS sites (
  site_id TEXT PRIMARY KEY,
  site_name TEXT NOT NULL DEFAULT '',
  created_at_ms INTEGER NOT NULL DEFAULT 0,
  updated_at_ms INTEGER NOT NULL DEFAULT 0
);
)SQL";

  static const char* kCreateAgentParamsSql = R"SQL(
CREATE TABLE IF NOT EXISTS agent_params (
  agent_id TEXT PRIMARY KEY,
  params_json TEXT NOT NULL DEFAULT '{}',
  updated_at_ms INTEGER NOT NULL DEFAULT 0
);
)SQL";

  static const char* kCreateAuditLogsSql = R"SQL(
CREATE TABLE IF NOT EXISTS audit_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  actor_type TEXT NOT NULL,
  actor_id TEXT NOT NULL,
  action TEXT NOT NULL,
  resource_type TEXT NOT NULL,
  resource_id TEXT NOT NULL,
  summary_json TEXT NOT NULL,
  created_at_ms INTEGER NOT NULL
);
)SQL";

  static const char* kCreateAuditLogsIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_audit_logs_created_at
ON audit_logs(created_at_ms, id);
)SQL";

  if (!exec_sql(g_db, kCreateCommandsSql, error) || !exec_sql(g_db, kCreateEventsSql, error) ||
      !exec_sql(g_db, kCreateEventsIndexSql, error) || !exec_sql(g_db, kCreateAgentsSql, error) ||
      !exec_sql(g_db, kCreateSitesSql, error) || !exec_sql(g_db, kCreateAgentParamsSql, error) ||
      !exec_sql(g_db, kCreateAuditLogsSql, error) ||
      !exec_sql(g_db, kCreateAuditLogsIndexSql, error)) {
    sqlite3_close(g_db);
    g_db = nullptr;
    return false;
  }

  if (!ensure_commands_schema(g_db, error) || !exec_sql(g_db, kCreateCommandsCreatedIndexSql, error) ||
      !exec_sql(g_db, kCreateCommandsAgentIndexSql, error) ||
      !exec_sql(g_db, kCreateCommandsRetryIndexSql, error)) {
    sqlite3_close(g_db);
    g_db = nullptr;
    return false;
  }

  log::info("command store initialized: {}", g_db_path);
  return true;
}

void shutdown_command_store() {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    return;
  }
  sqlite3_close(g_db);
  g_db = nullptr;
}

bool upsert_command(const command_record& record, std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
INSERT INTO commands(
  command_id,agent_id,idempotency_key,command_type,status,payload_json,result_json,
  issued_at_ms,expires_at_ms,timeout_ms,max_retry,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms
) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(command_id) DO UPDATE SET
  agent_id=excluded.agent_id,
  idempotency_key=excluded.idempotency_key,
  command_type=excluded.command_type,
  status=excluded.status,
  payload_json=excluded.payload_json,
  result_json=excluded.result_json,
  issued_at_ms=excluded.issued_at_ms,
  expires_at_ms=excluded.expires_at_ms,
  timeout_ms=excluded.timeout_ms,
  max_retry=excluded.max_retry,
  retry_count=excluded.retry_count,
  next_retry_at_ms=excluded.next_retry_at_ms,
  last_error=excluded.last_error,
  updated_at_ms=excluded.updated_at_ms;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, record.command_id);
  bind_text(stmt, 2, record.agent_id);
  bind_text(stmt, 3, record.idempotency_key);
  bind_text(stmt, 4, record.command_type);
  bind_text(stmt, 5, record.status);
  bind_text(stmt, 6, record.payload_json);
  bind_text(stmt, 7, record.result_json);
  sqlite3_bind_int64(stmt, 8, record.issued_at_ms);
  sqlite3_bind_int64(stmt, 9, record.expires_at_ms);
  sqlite3_bind_int(stmt, 10, record.timeout_ms);
  sqlite3_bind_int(stmt, 11, record.max_retry);
  sqlite3_bind_int(stmt, 12, record.retry_count);
  sqlite3_bind_int64(stmt, 13, record.next_retry_at_ms);
  bind_text(stmt, 14, record.last_error);
  sqlite3_bind_int64(stmt, 15, record.created_at_ms);
  sqlite3_bind_int64(stmt, 16, record.updated_at_ms);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  return true;
}

bool update_command_status(
    const std::string& command_id,
    const std::string& status,
    const std::string& result_json,
    int64_t updated_at_ms,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
UPDATE commands
SET status=?, result_json=?, next_retry_at_ms=0, last_error='', updated_at_ms=?
WHERE command_id=?;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, status);
  bind_text(stmt, 2, result_json);
  sqlite3_bind_int64(stmt, 3, updated_at_ms);
  bind_text(stmt, 4, command_id);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  if (sqlite3_changes(g_db) <= 0) {
    error = "command not found";
    return false;
  }
  return true;
}

bool update_command_terminal_status_once(
    const std::string& command_id,
    const std::string& final_status,
    const std::string& result_json,
    int64_t updated_at_ms,
    bool& applied,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  applied = false;
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kUpdateSql = R"SQL(
UPDATE commands
SET status=?, result_json=?, next_retry_at_ms=0, last_error='', updated_at_ms=?
WHERE command_id=?
  AND status NOT IN ('SUCCEEDED','FAILED','TIMED_OUT','CANCELLED');
)SQL";

  sqlite3_stmt* update_stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kUpdateSql, -1, &update_stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(update_stmt, 1, final_status);
  bind_text(update_stmt, 2, result_json);
  sqlite3_bind_int64(update_stmt, 3, updated_at_ms);
  bind_text(update_stmt, 4, command_id);

  const auto update_rc = sqlite3_step(update_stmt);
  sqlite3_finalize(update_stmt);
  if (update_rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  if (sqlite3_changes(g_db) > 0) {
    applied = true;
    return true;
  }

  static const char* kSelectSql = R"SQL(
SELECT status
FROM commands
WHERE command_id=?
LIMIT 1;
)SQL";

  sqlite3_stmt* select_stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSelectSql, -1, &select_stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(select_stmt, 1, command_id);
  const auto select_rc = sqlite3_step(select_stmt);
  if (select_rc == SQLITE_ROW) {
    const auto* status_text = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 0));
    const std::string current_status = (status_text != nullptr) ? status_text : "";
    sqlite3_finalize(select_stmt);
    if (is_terminal_status(current_status)) {
      applied = false;
      return true;
    }
    error = "command terminal status update rejected";
    return false;
  }

  sqlite3_finalize(select_stmt);
  if (select_rc == SQLITE_DONE) {
    error = "command not found";
  } else {
    error = sqlite3_errmsg(g_db);
  }
  return false;
}

bool append_command_event(
    const std::string& command_id,
    const std::string& event_type,
    const std::string& status,
    const std::string& detail_json,
    int64_t created_at_ms,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
INSERT INTO command_events(command_id,event_type,status,detail_json,created_at_ms)
VALUES(?,?,?,?,?);
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, command_id);
  bind_text(stmt, 2, event_type);
  bind_text(stmt, 3, status);
  bind_text(stmt, 4, detail_json);
  sqlite3_bind_int64(stmt, 5, created_at_ms);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  return true;
}

bool get_command(const std::string& command_id, command_record& out, std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
SELECT command_id,agent_id,idempotency_key,command_type,status,payload_json,result_json,
       issued_at_ms,expires_at_ms,timeout_ms,max_retry,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms
FROM commands
WHERE command_id=?
LIMIT 1;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, command_id);
  const auto rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    out.command_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    out.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    out.idempotency_key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    out.command_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    out.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    out.payload_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    out.result_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    out.issued_at_ms = sqlite3_column_int64(stmt, 7);
    out.expires_at_ms = sqlite3_column_int64(stmt, 8);
    out.timeout_ms = sqlite3_column_int(stmt, 9);
    out.max_retry = sqlite3_column_int(stmt, 10);
    out.retry_count = sqlite3_column_int(stmt, 11);
    out.next_retry_at_ms = sqlite3_column_int64(stmt, 12);
    out.last_error = read_text(stmt, 13);
    out.created_at_ms = sqlite3_column_int64(stmt, 14);
    out.updated_at_ms = sqlite3_column_int64(stmt, 15);
    sqlite3_finalize(stmt);
    return true;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    error = "command not found";
  } else {
    error = sqlite3_errmsg(g_db);
  }
  return false;
}

bool list_command_events(
    const std::string& command_id,
    int limit,
    std::vector<command_event_record>& out,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  const int safe_limit = (limit <= 0) ? 50 : std::min(limit, 500);
  static const char* kSql = R"SQL(
SELECT id,command_id,event_type,status,detail_json,created_at_ms
FROM command_events
WHERE command_id=?
ORDER BY id ASC
LIMIT ?;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, command_id);
  sqlite3_bind_int(stmt, 2, safe_limit);

  out.clear();
  for (;;) {
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      command_event_record row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.command_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      row.event_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      row.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
      row.detail_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
      row.created_at_ms = sqlite3_column_int64(stmt, 5);
      out.push_back(std::move(row));
      continue;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      return true;
    }
    error = sqlite3_errmsg(g_db);
    return false;
  }
}

bool list_commands(
    const std::string& agent_id,
    const std::string& status,
    const std::string& command_type,
    int limit,
    std::vector<command_record>& out,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  const int safe_limit = (limit <= 0) ? 50 : std::min(limit, 500);
  std::string sql =
      "SELECT command_id,agent_id,idempotency_key,command_type,status,payload_json,result_json,"
      "issued_at_ms,expires_at_ms,timeout_ms,max_retry,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms "
      "FROM commands";

  std::vector<std::string> filters;
  if (!agent_id.empty()) {
    filters.push_back("agent_id=?");
  }
  if (!status.empty()) {
    filters.push_back("status=?");
  }
  if (!command_type.empty()) {
    filters.push_back("command_type=?");
  }

  if (!filters.empty()) {
    sql += " WHERE ";
    for (std::size_t i = 0; i < filters.size(); ++i) {
      if (i > 0) {
        sql += " AND ";
      }
      sql += filters[i];
    }
  }
  sql += " ORDER BY created_at_ms DESC, command_id DESC LIMIT ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  int bind_index = 1;
  if (!agent_id.empty()) {
    bind_text(stmt, bind_index++, agent_id);
  }
  if (!status.empty()) {
    bind_text(stmt, bind_index++, status);
  }
  if (!command_type.empty()) {
    bind_text(stmt, bind_index++, command_type);
  }
  sqlite3_bind_int(stmt, bind_index, safe_limit);

  out.clear();
  for (;;) {
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      command_record row;
      row.command_id = read_text(stmt, 0);
      row.agent_id = read_text(stmt, 1);
      row.idempotency_key = read_text(stmt, 2);
      row.command_type = read_text(stmt, 3);
      row.status = read_text(stmt, 4);
      row.payload_json = read_text(stmt, 5);
      row.result_json = read_text(stmt, 6);
      row.issued_at_ms = sqlite3_column_int64(stmt, 7);
      row.expires_at_ms = sqlite3_column_int64(stmt, 8);
      row.timeout_ms = sqlite3_column_int(stmt, 9);
      row.max_retry = sqlite3_column_int(stmt, 10);
      row.retry_count = sqlite3_column_int(stmt, 11);
      row.next_retry_at_ms = sqlite3_column_int64(stmt, 12);
      row.last_error = read_text(stmt, 13);
      row.created_at_ms = sqlite3_column_int64(stmt, 14);
      row.updated_at_ms = sqlite3_column_int64(stmt, 15);
      out.push_back(std::move(row));
      continue;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      return true;
    }
    error = sqlite3_errmsg(g_db);
    return false;
  }
}

bool list_retry_ready_commands(
    int64_t now_ms,
    int limit,
    std::vector<command_record>& out,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  const int safe_limit = (limit <= 0) ? 50 : std::min(limit, 500);
  static const char* kSql = R"SQL(
SELECT command_id,agent_id,idempotency_key,command_type,status,payload_json,result_json,
       issued_at_ms,expires_at_ms,timeout_ms,max_retry,retry_count,next_retry_at_ms,last_error,created_at_ms,updated_at_ms
FROM commands
WHERE status='RETRY_PENDING'
  AND retry_count < max_retry
  AND next_retry_at_ms <= ?
ORDER BY next_retry_at_ms ASC, updated_at_ms ASC, command_id ASC
LIMIT ?;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  sqlite3_bind_int64(stmt, 1, now_ms);
  sqlite3_bind_int(stmt, 2, safe_limit);

  out.clear();
  for (;;) {
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      command_record row;
      row.command_id = read_text(stmt, 0);
      row.agent_id = read_text(stmt, 1);
      row.idempotency_key = read_text(stmt, 2);
      row.command_type = read_text(stmt, 3);
      row.status = read_text(stmt, 4);
      row.payload_json = read_text(stmt, 5);
      row.result_json = read_text(stmt, 6);
      row.issued_at_ms = sqlite3_column_int64(stmt, 7);
      row.expires_at_ms = sqlite3_column_int64(stmt, 8);
      row.timeout_ms = sqlite3_column_int(stmt, 9);
      row.max_retry = sqlite3_column_int(stmt, 10);
      row.retry_count = sqlite3_column_int(stmt, 11);
      row.next_retry_at_ms = sqlite3_column_int64(stmt, 12);
      row.last_error = read_text(stmt, 13);
      row.created_at_ms = sqlite3_column_int64(stmt, 14);
      row.updated_at_ms = sqlite3_column_int64(stmt, 15);
      out.push_back(std::move(row));
      continue;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      return true;
    }
    error = sqlite3_errmsg(g_db);
    return false;
  }
}

bool update_command_retry_state(
    const std::string& command_id,
    const std::string& status,
    int retry_count,
    int64_t next_retry_at_ms,
    const std::string& last_error,
    int64_t updated_at_ms,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
UPDATE commands
SET status=?, retry_count=?, next_retry_at_ms=?, last_error=?, updated_at_ms=?
WHERE command_id=?
  AND status NOT IN ('SUCCEEDED','FAILED','TIMED_OUT','CANCELLED');
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, status);
  sqlite3_bind_int(stmt, 2, retry_count);
  sqlite3_bind_int64(stmt, 3, next_retry_at_ms);
  bind_text(stmt, 4, last_error);
  sqlite3_bind_int64(stmt, 5, updated_at_ms);
  bind_text(stmt, 6, command_id);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  if (sqlite3_changes(g_db) <= 0) {
    error = "command not found or already terminal";
    return false;
  }
  return true;
}

bool recover_inflight_commands(
    int64_t recovered_at_ms,
    int& recovered_count,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  recovered_count = 0;
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSelectSql = R"SQL(
SELECT command_id
FROM commands
WHERE status NOT IN ('SUCCEEDED','FAILED','TIMED_OUT','CANCELLED');
)SQL";
  sqlite3_stmt* select_stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSelectSql, -1, &select_stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  std::vector<std::string> pending_command_ids;
  for (;;) {
    const auto rc = sqlite3_step(select_stmt);
    if (rc == SQLITE_ROW) {
      pending_command_ids.push_back(read_text(select_stmt, 0));
      continue;
    }
    sqlite3_finalize(select_stmt);
    if (rc != SQLITE_DONE) {
      error = sqlite3_errmsg(g_db);
      return false;
    }
    break;
  }

  if (pending_command_ids.empty()) {
    return true;
  }

  static const char* kUpdateSql = R"SQL(
UPDATE commands
SET status='TIMED_OUT',
    result_json=?,
    next_retry_at_ms=0,
    last_error='',
    updated_at_ms=?
WHERE command_id=?
  AND status NOT IN ('SUCCEEDED','FAILED','TIMED_OUT','CANCELLED');
)SQL";
  static const char* kInsertEventSql = R"SQL(
INSERT INTO command_events(command_id,event_type,status,detail_json,created_at_ms)
VALUES(?,?,?,?,?);
)SQL";

  sqlite3_stmt* update_stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kUpdateSql, -1, &update_stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  sqlite3_stmt* insert_stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kInsertEventSql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
    sqlite3_finalize(update_stmt);
    error = sqlite3_errmsg(g_db);
    return false;
  }

  static const std::string kRecoveryResult =
      "{\"error_code\":\"CTRL_RESTART_RECOVERY\",\"message\":\"controller restarted before terminal result\"}";
  static const std::string kRecoveryDetail =
      "{\"event\":\"COMMAND_RECOVERY_TIMEOUT\",\"reason\":\"CTRL_RESTART_RECOVERY\"}";
  for (const auto& command_id : pending_command_ids) {
    bind_text(update_stmt, 1, kRecoveryResult);
    sqlite3_bind_int64(update_stmt, 2, recovered_at_ms);
    bind_text(update_stmt, 3, command_id);

    const auto update_rc = sqlite3_step(update_stmt);
    if (update_rc != SQLITE_DONE) {
      sqlite3_finalize(update_stmt);
      sqlite3_finalize(insert_stmt);
      error = sqlite3_errmsg(g_db);
      return false;
    }
    const bool changed = sqlite3_changes(g_db) > 0;
    sqlite3_reset(update_stmt);
    sqlite3_clear_bindings(update_stmt);

    if (!changed) {
      continue;
    }

    bind_text(insert_stmt, 1, command_id);
    bind_text(insert_stmt, 2, "COMMAND_RECOVERY_TIMEOUT");
    bind_text(insert_stmt, 3, "TIMED_OUT");
    bind_text(insert_stmt, 4, kRecoveryDetail);
    sqlite3_bind_int64(insert_stmt, 5, recovered_at_ms);

    const auto insert_rc = sqlite3_step(insert_stmt);
    if (insert_rc != SQLITE_DONE) {
      sqlite3_finalize(update_stmt);
      sqlite3_finalize(insert_stmt);
      error = sqlite3_errmsg(g_db);
      return false;
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    ++recovered_count;
  }

  sqlite3_finalize(update_stmt);
  sqlite3_finalize(insert_stmt);
  return true;
}

bool upsert_agent(const agent_record& record, std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
INSERT INTO agents(
  agent_id,site_id,agent_version,capabilities_json,online,registered_at_ms,last_heartbeat_at_ms,last_seen_at_ms,stats_json,updated_at_ms
) VALUES(?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(agent_id) DO UPDATE SET
  site_id=excluded.site_id,
  agent_version=excluded.agent_version,
  capabilities_json=excluded.capabilities_json,
  online=excluded.online,
  registered_at_ms=excluded.registered_at_ms,
  last_heartbeat_at_ms=excluded.last_heartbeat_at_ms,
  last_seen_at_ms=excluded.last_seen_at_ms,
  stats_json=excluded.stats_json,
  updated_at_ms=excluded.updated_at_ms;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, record.agent_id);
  bind_text(stmt, 2, record.site_id);
  bind_text(stmt, 3, record.agent_version);
  bind_text(stmt, 4, record.capabilities_json);
  sqlite3_bind_int(stmt, 5, record.online ? 1 : 0);
  sqlite3_bind_int64(stmt, 6, record.registered_at_ms);
  sqlite3_bind_int64(stmt, 7, record.last_heartbeat_at_ms);
  sqlite3_bind_int64(stmt, 8, record.last_seen_at_ms);
  bind_text(stmt, 9, record.stats_json);
  sqlite3_bind_int64(stmt, 10, record.updated_at_ms);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  return true;
}

bool list_agents(std::vector<agent_record>& out, std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
SELECT agent_id,site_id,agent_version,capabilities_json,online,registered_at_ms,last_heartbeat_at_ms,last_seen_at_ms,stats_json,updated_at_ms
FROM agents
ORDER BY agent_id ASC;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  out.clear();
  for (;;) {
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      agent_record row;
      row.agent_id = read_text(stmt, 0);
      row.site_id = read_text(stmt, 1);
      row.agent_version = read_text(stmt, 2);
      row.capabilities_json = read_text(stmt, 3);
      row.online = sqlite3_column_int(stmt, 4) != 0;
      row.registered_at_ms = sqlite3_column_int64(stmt, 5);
      row.last_heartbeat_at_ms = sqlite3_column_int64(stmt, 6);
      row.last_seen_at_ms = sqlite3_column_int64(stmt, 7);
      row.stats_json = read_text(stmt, 8);
      row.updated_at_ms = sqlite3_column_int64(stmt, 9);
      out.push_back(std::move(row));
      continue;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      return true;
    }
    error = sqlite3_errmsg(g_db);
    return false;
  }
}

bool set_all_agents_offline(int64_t updated_at_ms, std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
UPDATE agents
SET online=0, updated_at_ms=?
WHERE online<>0;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  sqlite3_bind_int64(stmt, 1, updated_at_ms);
  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  return true;
}

bool append_audit_log(
    const std::string& actor_type,
    const std::string& actor_id,
    const std::string& action,
    const std::string& resource_type,
    const std::string& resource_id,
    const std::string& summary_json,
    int64_t created_at_ms,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
INSERT INTO audit_logs(actor_type,actor_id,action,resource_type,resource_id,summary_json,created_at_ms)
VALUES(?,?,?,?,?,?,?);
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, actor_type);
  bind_text(stmt, 2, actor_id);
  bind_text(stmt, 3, action);
  bind_text(stmt, 4, resource_type);
  bind_text(stmt, 5, resource_id);
  bind_text(stmt, 6, summary_json);
  sqlite3_bind_int64(stmt, 7, created_at_ms);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  return true;
}

bool list_audit_logs(
    const std::string& action,
    const std::string& actor_id,
    const std::string& resource_type,
    const std::string& resource_id,
    int limit,
    std::vector<audit_log_record>& out,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  const int safe_limit = (limit <= 0) ? 50 : std::min(limit, 500);
  std::string sql =
      "SELECT id,actor_type,actor_id,action,resource_type,resource_id,summary_json,created_at_ms "
      "FROM audit_logs";

  std::vector<std::string> filters;
  if (!action.empty()) {
    filters.push_back("action=?");
  }
  if (!actor_id.empty()) {
    filters.push_back("actor_id=?");
  }
  if (!resource_type.empty()) {
    filters.push_back("resource_type=?");
  }
  if (!resource_id.empty()) {
    filters.push_back("resource_id=?");
  }

  if (!filters.empty()) {
    sql += " WHERE ";
    for (std::size_t i = 0; i < filters.size(); ++i) {
      if (i > 0) {
        sql += " AND ";
      }
      sql += filters[i];
    }
  }
  sql += " ORDER BY id DESC LIMIT ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  int bind_index = 1;
  if (!action.empty()) {
    bind_text(stmt, bind_index++, action);
  }
  if (!actor_id.empty()) {
    bind_text(stmt, bind_index++, actor_id);
  }
  if (!resource_type.empty()) {
    bind_text(stmt, bind_index++, resource_type);
  }
  if (!resource_id.empty()) {
    bind_text(stmt, bind_index++, resource_id);
  }
  sqlite3_bind_int(stmt, bind_index, safe_limit);

  out.clear();
  for (;;) {
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      audit_log_record row;
      row.id = sqlite3_column_int64(stmt, 0);
      row.actor_type = read_text(stmt, 1);
      row.actor_id = read_text(stmt, 2);
      row.action = read_text(stmt, 3);
      row.resource_type = read_text(stmt, 4);
      row.resource_id = read_text(stmt, 5);
      row.summary_json = read_text(stmt, 6);
      row.created_at_ms = sqlite3_column_int64(stmt, 7);
      out.push_back(std::move(row));
      continue;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      return true;
    }
    error = sqlite3_errmsg(g_db);
    return false;
  }
}

bool upsert_agent_params(
    const std::string& agent_id,
    const std::string& params_json,
    int64_t updated_at_ms,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
INSERT INTO agent_params(agent_id,params_json,updated_at_ms)
VALUES(?,?,?)
ON CONFLICT(agent_id) DO UPDATE SET
  params_json=excluded.params_json,
  updated_at_ms=excluded.updated_at_ms;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, agent_id);
  bind_text(stmt, 2, params_json);
  sqlite3_bind_int64(stmt, 3, updated_at_ms);

  const auto rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    error = sqlite3_errmsg(g_db);
    return false;
  }
  return true;
}

bool get_agent_params(
    const std::string& agent_id,
    agent_params_record& out,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
SELECT agent_id,params_json,updated_at_ms
FROM agent_params
WHERE agent_id=?
LIMIT 1;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, agent_id);
  const auto rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    out.agent_id = read_text(stmt, 0);
    out.params_json = read_text(stmt, 1);
    out.updated_at_ms = sqlite3_column_int64(stmt, 2);
    sqlite3_finalize(stmt);
    return true;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    error = "agent params not found";
  } else {
    error = sqlite3_errmsg(g_db);
  }
  return false;
}

} // namespace service
