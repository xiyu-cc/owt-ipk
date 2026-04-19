#include "internal.h"

#include <algorithm>
#include <sstream>

namespace ctrl::infrastructure {

bool SqliteStore::append(const domain::AuditEntry& row, std::string& error) {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!open(error)) {
    return false;
  }

  statement stmt;
  if (row.id > 0) {
    if (!prepare(
            db_,
            "INSERT INTO audits(id,actor_type,actor_id,action,resource_type,resource_id,summary_json,created_at_ms) "
            "VALUES(?,?,?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET actor_type=excluded.actor_type, actor_id=excluded.actor_id, "
            "action=excluded.action, resource_type=excluded.resource_type, resource_id=excluded.resource_id, "
            "summary_json=excluded.summary_json, created_at_ms=excluded.created_at_ms;",
            stmt,
            error) ||
        !bind_int64(stmt.ptr, 1, row.id, error) ||
        !bind_text(stmt.ptr, 2, row.actor_type, error) ||
        !bind_text(stmt.ptr, 3, row.actor_id, error) ||
        !bind_text(stmt.ptr, 4, row.action, error) ||
        !bind_text(stmt.ptr, 5, row.resource_type, error) ||
        !bind_text(stmt.ptr, 6, row.resource_id, error) ||
        !bind_text(stmt.ptr, 7, row.summary.dump(), error) ||
        !bind_int64(stmt.ptr, 8, row.created_at_ms, error)) {
      return false;
    }
  } else {
    if (!prepare(
            db_,
            "INSERT INTO audits(actor_type,actor_id,action,resource_type,resource_id,summary_json,created_at_ms) "
            "VALUES(?,?,?,?,?,?,?);",
            stmt,
            error) ||
        !bind_text(stmt.ptr, 1, row.actor_type, error) ||
        !bind_text(stmt.ptr, 2, row.actor_id, error) ||
        !bind_text(stmt.ptr, 3, row.action, error) ||
        !bind_text(stmt.ptr, 4, row.resource_type, error) ||
        !bind_text(stmt.ptr, 5, row.resource_id, error) ||
        !bind_text(stmt.ptr, 6, row.summary.dump(), error) ||
        !bind_int64(stmt.ptr, 7, row.created_at_ms, error)) {
      return false;
    }
  }

  return step_done(db_, stmt.ptr, error);
}

bool SqliteStore::list(
    const domain::AuditListFilter& filter,
    domain::ListPage<domain::AuditEntry, domain::AuditListCursor>& out,
    std::string& error) const {
  using namespace sqlite_detail;

  std::lock_guard<std::mutex> lk(mutex_);
  if (!const_cast<SqliteStore*>(this)->open(error)) {
    return false;
  }

  const int safe_limit = std::max(1, std::min(filter.limit, 500));

  std::ostringstream sql;
  sql << "SELECT id,actor_type,actor_id,action,resource_type,resource_id,summary_json,created_at_ms "
         "FROM audits WHERE 1=1";

  if (filter.action.has_value()) {
    sql << " AND action=?";
  }
  if (filter.actor_type.has_value()) {
    sql << " AND actor_type=?";
  }
  if (filter.actor_id.has_value()) {
    sql << " AND actor_id=?";
  }
  if (filter.resource_type.has_value()) {
    sql << " AND resource_type=?";
  }
  if (filter.resource_id.has_value()) {
    sql << " AND resource_id=?";
  }
  if (filter.cursor.has_value()) {
    sql << " AND id < ?";
  }
  sql << " ORDER BY id DESC LIMIT ?;";

  statement stmt;
  if (!prepare(db_, sql.str(), stmt, error)) {
    return false;
  }

  int idx = 1;
  if (filter.action.has_value() && !bind_text(stmt.ptr, idx++, *filter.action, error)) {
    return false;
  }
  if (filter.actor_type.has_value() && !bind_text(stmt.ptr, idx++, *filter.actor_type, error)) {
    return false;
  }
  if (filter.actor_id.has_value() && !bind_text(stmt.ptr, idx++, *filter.actor_id, error)) {
    return false;
  }
  if (filter.resource_type.has_value() && !bind_text(stmt.ptr, idx++, *filter.resource_type, error)) {
    return false;
  }
  if (filter.resource_id.has_value() && !bind_text(stmt.ptr, idx++, *filter.resource_id, error)) {
    return false;
  }
  if (filter.cursor.has_value() && !bind_int64(stmt.ptr, idx++, filter.cursor->id, error)) {
    return false;
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

    domain::AuditEntry row;
    if (!read_audit_row(stmt.ptr, row, error)) {
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
    out.next_cursor = domain::AuditListCursor{out.items.back().id};
  }

  error.clear();
  return true;
}

} // namespace ctrl::infrastructure
