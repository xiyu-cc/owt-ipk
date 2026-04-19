#pragma once

#include "ctrl/domain/types.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/application/audit_query_service.h"
#include "ctrl/application/command_orchestrator.h"

#include <string>
#include <vector>

namespace ctrl::adapters {

struct HttpError {
  int code = 500;
  std::string message;
};

template <typename T>
struct HttpResult {
  bool ok = false;
  T data{};
  HttpError error{};
};

struct PushCommandRequest {
  application::SubmitCommandInput input;
};

struct PushCommandResponse {
  application::SubmitCommandOutput output;
};

struct CommandDetails {
  domain::CommandSnapshot command;
  std::vector<domain::CommandEvent> events;
};

class HttpUseCases {
public:
  HttpUseCases(
      application::AgentRegistryService& agents,
      application::CommandOrchestrator& commands,
      application::AuditQueryService& audits);

  HttpResult<domain::AgentState> get_agent(const std::string& agent_mac) const;
  HttpResult<std::vector<domain::AgentState>> list_agents(bool include_offline) const;
  HttpResult<PushCommandResponse> push_command(const PushCommandRequest& request) const;
  HttpResult<CommandDetails> get_command(const std::string& command_id, int event_limit) const;
  HttpResult<domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor>> list_commands(
      const domain::CommandListFilter& filter) const;
  HttpResult<domain::ListPage<domain::AuditEntry, domain::AuditListCursor>> list_audits(
      const domain::AuditListFilter& filter) const;

private:
  application::AgentRegistryService& agents_;
  application::CommandOrchestrator& commands_;
  application::AuditQueryService& audits_;
};

} // namespace ctrl::adapters
