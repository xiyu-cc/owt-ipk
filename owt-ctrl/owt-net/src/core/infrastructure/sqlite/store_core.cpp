#include "internal.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace ctrl::infrastructure {

SqliteStore::SqliteStore(std::string db_path) : db_path_(std::move(db_path)) {}

SqliteStore::~SqliteStore() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool SqliteStore::open(std::string& error) {
  if (db_ != nullptr) {
    return true;
  }
  try {
    const auto db_file = std::filesystem::path(db_path_);
    if (db_file.has_parent_path()) {
      std::filesystem::create_directories(db_file.parent_path());
    }
  } catch (const std::exception& ex) {
    error = std::string("create db directory failed: ") + ex.what();
    return false;
  }

  const auto rc = sqlite3_open_v2(
      db_path_.c_str(),
      &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  if (rc != SQLITE_OK) {
    error = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite open failed";
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }

  sqlite3_busy_timeout(db_, 5000);

  if (!exec_locked("PRAGMA journal_mode=WAL;", error) ||
      !exec_locked("PRAGMA synchronous=NORMAL;", error) ||
      !exec_locked("PRAGMA foreign_keys=ON;", error)) {
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  return true;
}

bool SqliteStore::exec_locked(const char* sql, std::string& error) const {
  char* err = nullptr;
  const auto rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    error = err != nullptr ? err : sqlite3_errmsg(db_);
    sqlite3_free(err);
    return false;
  }
  return true;
}

} // namespace ctrl::infrastructure
