#pragma once

#include "ctrl/domain/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ctrl::ports {

class IClock {
public:
  virtual ~IClock() = default;
  virtual int64_t now_ms() const = 0;
};

class IIdGenerator {
public:
  virtual ~IIdGenerator() = default;
  virtual std::string next_command_id() = 0;
  virtual std::string next_trace_id(std::string_view command_id) = 0;
};

class ICommandRepository {
public:
  virtual ~ICommandRepository() = default;

  virtual bool upsert(const domain::CommandSnapshot& row, std::string& error) = 0;
  virtual bool get(
      std::string_view command_id,
      domain::CommandSnapshot& out,
      std::string& error) const = 0;
  virtual bool list(
      const domain::CommandListFilter& filter,
      domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor>& out,
      std::string& error) const = 0;
  virtual bool append_event(const domain::CommandEvent& event, std::string& error) = 0;
  virtual bool list_events(
      std::string_view command_id,
      int limit,
      std::vector<domain::CommandEvent>& out,
      std::string& error) const = 0;

  virtual bool update_state_if_not_terminal(
      std::string_view command_id,
      domain::CommandState next_state,
      const nlohmann::json& result,
      int64_t updated_at_ms,
      bool& applied,
      std::string& error) = 0;
  virtual bool update_terminal_state_once(
      std::string_view command_id,
      domain::CommandState terminal_state,
      const nlohmann::json& result,
      int64_t updated_at_ms,
      bool& applied,
      std::string& error) = 0;

  virtual bool list_retry_ready(
      int64_t now_ms,
      int limit,
      std::vector<domain::CommandSnapshot>& out,
      std::string& error) const = 0;
  virtual bool update_retry_state(
      std::string_view command_id,
      domain::CommandState next_state,
      int retry_count,
      int64_t next_retry_at_ms,
      std::string_view last_error,
      int64_t updated_at_ms,
      std::string& error) = 0;
  virtual bool recover_inflight(
      int64_t recovered_at_ms,
      int& recovered_count,
      std::string& error) = 0;
};

class IAgentRepository {
public:
  virtual ~IAgentRepository() = default;

  virtual bool upsert(const domain::AgentState& row, std::string& error) = 0;
  virtual bool get(std::string_view agent_mac, domain::AgentState& out, std::string& error) const = 0;
  virtual bool list(std::vector<domain::AgentState>& out, std::string& error) const = 0;
  virtual bool mark_all_offline(int64_t updated_at_ms, std::string& error) = 0;
};

class IParamsRepository {
public:
  virtual ~IParamsRepository() = default;

  virtual bool load(std::string_view agent_mac, nlohmann::json& out, std::string& error) const = 0;
  virtual bool save(
      std::string_view agent_mac,
      const nlohmann::json& params,
      int64_t updated_at_ms,
      std::string& error) = 0;
};

class IAuditRepository {
public:
  virtual ~IAuditRepository() = default;

  virtual bool append(const domain::AuditEntry& row, std::string& error) = 0;
  virtual bool list(
      const domain::AuditListFilter& filter,
      domain::ListPage<domain::AuditEntry, domain::AuditListCursor>& out,
      std::string& error) const = 0;
};

class IAgentChannel {
public:
  virtual ~IAgentChannel() = default;
  virtual bool is_online(std::string_view agent_mac) const = 0;
  virtual bool send_command(
      std::string_view agent_mac,
      const domain::CommandSpec& cmd,
      std::string& error) = 0;
};

class IStatusPublisher {
public:
  virtual ~IStatusPublisher() = default;
  virtual void publish_snapshot(std::string_view reason, std::string_view agent_mac) = 0;
  virtual void publish_agent(std::string_view reason, std::string_view agent_mac) = 0;
  virtual void publish_command_event(
      const domain::CommandSnapshot& command,
      const domain::CommandEvent& event) {
    (void)command;
    (void)event;
  }
};

class IMetrics {
public:
  virtual ~IMetrics() = default;
  virtual void record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) = 0;
  virtual void record_command_push() = 0;
  virtual void record_command_retry(
      std::string_view command_id,
      int retry_count,
      std::string_view reason) = 0;
  virtual void record_command_retry_exhausted(
      std::string_view command_id,
      std::string_view reason) = 0;
  virtual void record_command_terminal_status(
      std::string_view command_id,
      domain::CommandState state,
      const nlohmann::json& detail) = 0;
};

} // namespace ctrl::ports
