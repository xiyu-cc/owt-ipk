#include "internal.h"

#include <vector>

namespace ctrl::infrastructure {

bool SqliteStore::upsert(const domain::AgentState& row, std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(
          db_,
          "INSERT INTO agents(agent_mac,agent_id,online,site_id,agent_version,capabilities_json,stats_json,"
          "registered_at_ms,last_seen_at_ms,last_heartbeat_at_ms) VALUES(?,?,?,?,?,?,?,?,?,?) "
          "ON CONFLICT(agent_mac) DO UPDATE SET "
          "agent_id=excluded.agent_id, online=excluded.online, site_id=excluded.site_id, "
          "agent_version=excluded.agent_version, capabilities_json=excluded.capabilities_json, stats_json=excluded.stats_json, "
          "registered_at_ms=excluded.registered_at_ms, last_seen_at_ms=excluded.last_seen_at_ms, "
          "last_heartbeat_at_ms=excluded.last_heartbeat_at_ms;",
          stmt,
          error)) {
    return false;
  }

  const auto capabilities_json = nlohmann::json(row.capabilities).dump();
  const auto stats_json = row.stats.dump();

  if (!bind_text(stmt.ptr, 1, row.agent.mac, error) ||
      !bind_text(stmt.ptr, 2, row.agent.display_id, error) ||
      !bind_int(stmt.ptr, 3, row.online ? 1 : 0, error) ||
      !bind_text(stmt.ptr, 4, row.site_id, error) ||
      !bind_text(stmt.ptr, 5, row.version, error) ||
      !bind_text(stmt.ptr, 6, capabilities_json, error) ||
      !bind_text(stmt.ptr, 7, stats_json, error) ||
      !bind_int64(stmt.ptr, 8, row.registered_at_ms, error) ||
      !bind_int64(stmt.ptr, 9, row.last_seen_at_ms, error) ||
      !bind_int64(stmt.ptr, 10, row.last_heartbeat_at_ms, error)) {
    return false;
  }

  return step_done(db_, stmt.ptr, error);
}

bool SqliteStore::get(std::string_view agent_mac, domain::AgentState& out, std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(
          db_,
          "SELECT agent_mac,agent_id,online,site_id,agent_version,capabilities_json,stats_json,"
          "registered_at_ms,last_seen_at_ms,last_heartbeat_at_ms "
          "FROM agents WHERE agent_mac=? LIMIT 1;",
          stmt,
          error) ||
      !bind_text(stmt.ptr, 1, agent_mac, error)) {
    return false;
  }

  const auto rc = sqlite3_step(stmt.ptr);
  if (rc == SQLITE_DONE) {
    error = "agent not found";
    return false;
  }
  if (rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (!read_agent_row(stmt.ptr, out, error)) {
    return false;
  }
  error.clear();
  return true;
}

bool SqliteStore::list(std::vector<domain::AgentState>& out, std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(
          db_,
          "SELECT agent_mac,agent_id,online,site_id,agent_version,capabilities_json,stats_json,"
          "registered_at_ms,last_seen_at_ms,last_heartbeat_at_ms "
          "FROM agents;",
          stmt,
          error)) {
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

    domain::AgentState row;
    if (!read_agent_row(stmt.ptr, row, error)) {
      return false;
    }
    out.push_back(std::move(row));
  }

  error.clear();
  return true;
}

bool SqliteStore::mark_all_offline(int64_t updated_at_ms, std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(
          db_,
          "UPDATE agents SET online=0, last_seen_at_ms=?;",
          stmt,
          error) ||
      !bind_int64(stmt.ptr, 1, updated_at_ms, error)) {
    return false;
  }

  return step_done(db_, stmt.ptr, error);
}

} // namespace ctrl::infrastructure
