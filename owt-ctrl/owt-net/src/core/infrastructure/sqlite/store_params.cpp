#include "internal.h"

namespace ctrl::infrastructure {

bool SqliteStore::load(std::string_view agent_mac, nlohmann::json& out, std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(db_, "SELECT params_json FROM params WHERE agent_mac=? LIMIT 1;", stmt, error) ||
      !bind_text(stmt.ptr, 1, agent_mac, error)) {
    return false;
  }

  const auto rc = sqlite3_step(stmt.ptr);
  if (rc == SQLITE_DONE) {
    error = "agent params not found";
    return false;
  }
  if (rc != SQLITE_ROW) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (!parse_json_text(column_text(stmt.ptr, 0), out, error, "params_json")) {
    return false;
  }
  error.clear();
  return true;
}

bool SqliteStore::save(
    std::string_view agent_mac,
    const nlohmann::json& params,
    int64_t updated_at_ms,
    std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement stmt;
  if (!prepare(
          db_,
          "INSERT INTO params(agent_mac,params_json,updated_at_ms) VALUES(?,?,?) "
          "ON CONFLICT(agent_mac) DO UPDATE SET params_json=excluded.params_json, "
          "updated_at_ms=excluded.updated_at_ms;",
          stmt,
          error) ||
      !bind_text(stmt.ptr, 1, agent_mac, error) ||
      !bind_text(stmt.ptr, 2, params.dump(), error) ||
      !bind_int64(stmt.ptr, 3, updated_at_ms, error)) {
    return false;
  }

  return step_done(db_, stmt.ptr, error);
}

} // namespace ctrl::infrastructure
