#include "internal.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace ctrl::infrastructure {

void SqliteStore::migrate() {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  std::string error;
  if (!open(error)) {
    throw std::runtime_error(error);
  }

  constexpr std::string_view kTargetSchemaVersion = "3";

  bool has_schema_meta = false;
  {
    statement stmt;
    if (!prepare(
            db_,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_meta' LIMIT 1;",
            stmt,
            error)) {
      throw std::runtime_error("inspect schema_meta failed: " + error);
    }
    has_schema_meta = sqlite3_step(stmt.ptr) == SQLITE_ROW;
  }

  std::string current_schema_version;
  if (has_schema_meta) {
    statement stmt;
    if (!prepare(
            db_,
            "SELECT value FROM schema_meta WHERE key='schema_version' LIMIT 1;",
            stmt,
            error)) {
      throw std::runtime_error("read schema version failed: " + error);
    }
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW) {
      current_schema_version = column_text(stmt.ptr, 0);
    }
  }

  const auto assert_no_removed_command_types = [this, &error]() {
    statement stmt;
    if (!prepare(
            db_,
            "SELECT COUNT(1) FROM commands WHERE command_type IN ('host_probe_get','params_get');",
            stmt,
            error)) {
      throw std::runtime_error("inspect removed command types failed: " + error);
    }
    if (sqlite3_step(stmt.ptr) != SQLITE_ROW) {
      throw std::runtime_error("inspect removed command types failed: no row returned");
    }
    const auto count = sqlite3_column_int64(stmt.ptr, 0);
    if (count > 0) {
      throw std::runtime_error(
          "legacy command_type detected in commands table (host_probe_get/params_get); "
          "hard-delete mode requires manual cleanup before startup");
    }
  };

  if (current_schema_version == kTargetSchemaVersion) {
    assert_no_removed_command_types();
    return;
  }

  bool has_legacy_tables = false;
  {
    statement stmt;
    if (!prepare(
            db_,
            "SELECT name FROM sqlite_master WHERE type='table' AND name IN "
            "('schema_migrations','commands','command_events','agents','params','audits') LIMIT 1;",
            stmt,
            error)) {
      throw std::runtime_error("inspect legacy tables failed: " + error);
    }
    has_legacy_tables = sqlite3_step(stmt.ptr) == SQLITE_ROW;
  }

  if (has_legacy_tables || (has_schema_meta && current_schema_version != kTargetSchemaVersion)) {
    try {
      const auto db_file = std::filesystem::path(db_path_);
      if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      if (std::filesystem::exists(db_file)) {
        const auto backup =
            db_file.string() + ".bak." + std::to_string(unix_time_ms_now());
        std::filesystem::copy_file(
            db_file,
            std::filesystem::path(backup),
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(db_file);
      }
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("backup/reset legacy db failed: ") + ex.what());
    }

    if (!open(error)) {
      throw std::runtime_error("open sqlite after reset failed: " + error);
    }
  }

  if (!begin_tx(db_, error)) {
    throw std::runtime_error("begin migration tx failed: " + error);
  }

  const std::array<const char*, 11> ddl = {
      "CREATE TABLE IF NOT EXISTS schema_meta("
      "key TEXT PRIMARY KEY,"
      "value TEXT NOT NULL"
      ");",
      "CREATE TABLE IF NOT EXISTS commands("
      "command_id TEXT PRIMARY KEY,"
      "trace_id TEXT NOT NULL,"
      "agent_mac TEXT NOT NULL,"
      "agent_id TEXT NOT NULL,"
      "command_type TEXT NOT NULL,"
      "payload_json TEXT NOT NULL,"
      "timeout_ms INTEGER NOT NULL,"
      "max_retry INTEGER NOT NULL,"
      "expires_at_ms INTEGER NOT NULL,"
      "state TEXT NOT NULL,"
      "result_json TEXT NOT NULL,"
      "retry_count INTEGER NOT NULL,"
      "next_retry_at_ms INTEGER NOT NULL,"
      "last_error TEXT NOT NULL,"
      "created_at_ms INTEGER NOT NULL,"
      "updated_at_ms INTEGER NOT NULL"
      ");",
      "CREATE INDEX IF NOT EXISTS idx_commands_created_desc ON commands(created_at_ms DESC, command_id DESC);",
      "CREATE INDEX IF NOT EXISTS idx_commands_retry ON commands(state, next_retry_at_ms, updated_at_ms);",
      "CREATE TABLE IF NOT EXISTS command_events("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "command_id TEXT NOT NULL,"
      "event_type TEXT NOT NULL,"
      "state TEXT NOT NULL,"
      "detail_json TEXT NOT NULL,"
      "created_at_ms INTEGER NOT NULL,"
      "FOREIGN KEY(command_id) REFERENCES commands(command_id) ON DELETE CASCADE"
      ");",
      "CREATE INDEX IF NOT EXISTS idx_command_events_command_id_id ON command_events(command_id, id ASC);",
      "CREATE TABLE IF NOT EXISTS agents("
      "agent_mac TEXT PRIMARY KEY,"
      "agent_id TEXT NOT NULL,"
      "online INTEGER NOT NULL,"
      "site_id TEXT NOT NULL,"
      "agent_version TEXT NOT NULL,"
      "capabilities_json TEXT NOT NULL,"
      "stats_json TEXT NOT NULL,"
      "registered_at_ms INTEGER NOT NULL,"
      "last_seen_at_ms INTEGER NOT NULL,"
      "last_heartbeat_at_ms INTEGER NOT NULL"
      ");",
      "CREATE TABLE IF NOT EXISTS params("
      "agent_mac TEXT PRIMARY KEY,"
      "params_json TEXT NOT NULL,"
      "updated_at_ms INTEGER NOT NULL"
      ");",
      "CREATE TABLE IF NOT EXISTS audits("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "actor_type TEXT NOT NULL,"
      "actor_id TEXT NOT NULL,"
      "action TEXT NOT NULL,"
      "resource_type TEXT NOT NULL,"
      "resource_id TEXT NOT NULL,"
      "summary_json TEXT NOT NULL,"
      "created_at_ms INTEGER NOT NULL"
      ");",
      "CREATE INDEX IF NOT EXISTS idx_audits_id_desc ON audits(id DESC);",
      "CREATE INDEX IF NOT EXISTS idx_audits_created_desc ON audits(created_at_ms DESC);",
  };

  for (const auto* sql : ddl) {
    if (!exec_locked(sql, error)) {
      rollback_tx(db_);
      throw std::runtime_error("migration failed: " + error);
    }
  }

  {
    statement stmt;
    if (!prepare(
            db_,
            "INSERT INTO schema_meta(key,value) VALUES('schema_version',?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
            stmt,
            error)) {
      rollback_tx(db_);
      throw std::runtime_error("insert migration version failed: " + error);
    }
    if (!bind_text(stmt.ptr, 1, kTargetSchemaVersion, error) ||
        !step_done(db_, stmt.ptr, error)) {
      rollback_tx(db_);
      throw std::runtime_error("insert migration version failed: " + error);
    }
  }

  if (!commit_tx(db_, error)) {
    rollback_tx(db_);
    throw std::runtime_error("commit migration failed: " + error);
  }

  assert_no_removed_command_types();
}

void SqliteStore::cleanup_retention(int retention_days, int64_t now_ms) {
  using namespace sqlite_detail;

  if (retention_days <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  std::string error;
  if (!open(error)) {
    throw std::runtime_error("open sqlite failed: " + error);
  }

  const int64_t retention_ms = static_cast<int64_t>(retention_days) * 24LL * 60LL * 60LL * 1000LL;
  const int64_t threshold = now_ms - retention_ms;

  if (!begin_tx(db_, error)) {
    throw std::runtime_error("begin cleanup tx failed: " + error);
  }

  const std::array<std::string, 3> dml = {
      "DELETE FROM command_events WHERE created_at_ms < ?;",
      "DELETE FROM audits WHERE created_at_ms < ?;",
      "DELETE FROM commands WHERE updated_at_ms < ? "
      "AND state IN ('succeeded','failed','timed_out','cancelled');",
  };

  for (const auto& sql : dml) {
    statement stmt;
    if (!prepare(db_, sql, stmt, error) ||
        !bind_int64(stmt.ptr, 1, threshold, error) ||
        !step_done(db_, stmt.ptr, error)) {
      rollback_tx(db_);
      throw std::runtime_error("cleanup retention failed: " + error);
    }
  }

  if (!commit_tx(db_, error)) {
    rollback_tx(db_);
    throw std::runtime_error("commit cleanup tx failed: " + error);
  }
}

} // namespace ctrl::infrastructure
