#pragma once

#include "ctrl/ports/interfaces.h"

#include <sqlite3.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ctrl::infrastructure {

class SqliteStore final : public ports::ICommandRepository,
                          public ports::IAgentRepository,
                          public ports::IParamsRepository,
                          public ports::IAuditRepository {
public:
  explicit SqliteStore(std::string db_path);
  ~SqliteStore() override;

  SqliteStore(const SqliteStore&) = delete;
  SqliteStore& operator=(const SqliteStore&) = delete;

  void migrate();
  void cleanup_retention(int retention_days, int64_t now_ms);

  bool upsert(const domain::CommandSnapshot& row, std::string& error) override;
  bool get(
      std::string_view command_id,
      domain::CommandSnapshot& out,
      std::string& error) const override;
  bool list(
      const domain::CommandListFilter& filter,
      domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor>& out,
      std::string& error) const override;
  bool append_event(const domain::CommandEvent& event, std::string& error) override;
  bool list_events(
      std::string_view command_id,
      int limit,
      std::vector<domain::CommandEvent>& out,
      std::string& error) const override;

  bool update_state_if_not_terminal(
      std::string_view command_id,
      domain::CommandState next_state,
      const nlohmann::json& result,
      int64_t updated_at_ms,
      bool& applied,
      std::string& error) override;
  bool update_terminal_state_once(
      std::string_view command_id,
      domain::CommandState terminal_state,
      const nlohmann::json& result,
      int64_t updated_at_ms,
      bool& applied,
      std::string& error) override;

  bool list_retry_ready(
      int64_t now_ms,
      int limit,
      std::vector<domain::CommandSnapshot>& out,
      std::string& error) const override;
  bool update_retry_state(
      std::string_view command_id,
      domain::CommandState next_state,
      int retry_count,
      int64_t next_retry_at_ms,
      std::string_view last_error,
      int64_t updated_at_ms,
      std::string& error) override;
  bool recover_inflight(
      int64_t recovered_at_ms,
      int& recovered_count,
      std::string& error) override;

  bool upsert(const domain::AgentState& row, std::string& error) override;
  bool get(std::string_view agent_mac, domain::AgentState& out, std::string& error) const override;
  bool list(std::vector<domain::AgentState>& out, std::string& error) const override;
  bool mark_all_offline(int64_t updated_at_ms, std::string& error) override;

  bool load(std::string_view agent_mac, nlohmann::json& out, std::string& error) const override;
  bool save(
      std::string_view agent_mac,
      const nlohmann::json& params,
      int64_t updated_at_ms,
      std::string& error) override;

  bool append(const domain::AuditEntry& row, std::string& error) override;
  bool list(
      const domain::AuditListFilter& filter,
      domain::ListPage<domain::AuditEntry, domain::AuditListCursor>& out,
      std::string& error) const override;

private:
  bool open(std::string& error);
  bool exec_locked(const char* sql, std::string& error) const;

private:
  std::string db_path_;
  mutable std::mutex mutex_;
  sqlite3* db_ = nullptr;
};

} // namespace ctrl::infrastructure
