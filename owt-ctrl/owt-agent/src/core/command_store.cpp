#include "service/command_store.h"

#include "log.h"

#include <sqlite3.h>
#include <unistd.h>

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

namespace service {

namespace {

constexpr const char* kSystemDbPath = "/etc/owt-agent/owt_agent.db";
constexpr const char* kLocalDbPath = "owt_agent.db";

std::mutex g_db_mutex;
sqlite3* g_db = nullptr;
std::string g_db_path;

std::string choose_db_path() {
  try {
    const std::filesystem::path dir("/etc/owt-agent");
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
  idempotency_key TEXT NOT NULL,
  command_type TEXT NOT NULL,
  status TEXT NOT NULL,
  channel_type TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  result_json TEXT NOT NULL DEFAULT '',
  created_at_ms INTEGER NOT NULL,
  updated_at_ms INTEGER NOT NULL
);
)SQL";

  static const char* kCreateEventsSql = R"SQL(
CREATE TABLE IF NOT EXISTS command_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  command_id TEXT NOT NULL,
  event_type TEXT NOT NULL,
  status TEXT NOT NULL,
  channel_type TEXT NOT NULL,
  detail_json TEXT NOT NULL,
  created_at_ms INTEGER NOT NULL
);
)SQL";

  static const char* kCreateEventsIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_command_events_command_id
ON command_events(command_id, id);
)SQL";

  if (!exec_sql(g_db, kCreateCommandsSql, error) || !exec_sql(g_db, kCreateEventsSql, error) ||
      !exec_sql(g_db, kCreateEventsIndexSql, error)) {
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
  command_id,idempotency_key,command_type,status,channel_type,payload_json,result_json,created_at_ms,updated_at_ms
) VALUES(?,?,?,?,?,?,?,?,?)
ON CONFLICT(command_id) DO UPDATE SET
  idempotency_key=excluded.idempotency_key,
  command_type=excluded.command_type,
  status=excluded.status,
  channel_type=excluded.channel_type,
  payload_json=excluded.payload_json,
  result_json=excluded.result_json,
  updated_at_ms=excluded.updated_at_ms;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, record.command_id);
  bind_text(stmt, 2, record.idempotency_key);
  bind_text(stmt, 3, record.command_type);
  bind_text(stmt, 4, record.status);
  bind_text(stmt, 5, record.channel_type);
  bind_text(stmt, 6, record.payload_json);
  bind_text(stmt, 7, record.result_json);
  sqlite3_bind_int64(stmt, 8, record.created_at_ms);
  sqlite3_bind_int64(stmt, 9, record.updated_at_ms);

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
    const std::string& channel_type,
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
SET status=?, channel_type=?, result_json=?, updated_at_ms=?
WHERE command_id=?;
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, status);
  bind_text(stmt, 2, channel_type);
  bind_text(stmt, 3, result_json);
  sqlite3_bind_int64(stmt, 4, updated_at_ms);
  bind_text(stmt, 5, command_id);

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

bool append_command_event(
    const std::string& command_id,
    const std::string& event_type,
    const std::string& status,
    const std::string& channel_type,
    const std::string& detail_json,
    int64_t created_at_ms,
    std::string& error) {
  std::lock_guard<std::mutex> lk(g_db_mutex);
  if (g_db == nullptr) {
    error = "command store is not initialized";
    return false;
  }

  static const char* kSql = R"SQL(
INSERT INTO command_events(command_id,event_type,status,channel_type,detail_json,created_at_ms)
VALUES(?,?,?,?,?,?);
)SQL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(g_db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(g_db);
    return false;
  }

  bind_text(stmt, 1, command_id);
  bind_text(stmt, 2, event_type);
  bind_text(stmt, 3, status);
  bind_text(stmt, 4, channel_type);
  bind_text(stmt, 5, detail_json);
  sqlite3_bind_int64(stmt, 6, created_at_ms);

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
SELECT command_id,idempotency_key,command_type,status,channel_type,payload_json,result_json,created_at_ms,updated_at_ms
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
    out.idempotency_key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    out.command_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    out.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    out.channel_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    out.payload_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    out.result_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    out.created_at_ms = sqlite3_column_int64(stmt, 7);
    out.updated_at_ms = sqlite3_column_int64(stmt, 8);
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
SELECT id,command_id,event_type,status,channel_type,detail_json,created_at_ms
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
      row.channel_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
      row.detail_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
      row.created_at_ms = sqlite3_column_int64(stmt, 6);
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

} // namespace service
