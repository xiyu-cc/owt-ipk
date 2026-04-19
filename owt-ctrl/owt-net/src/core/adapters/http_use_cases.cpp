#include "ctrl/adapters/http_use_cases.h"

#include <exception>
#include <stdexcept>

namespace ctrl::adapters {

HttpUseCases::HttpUseCases(
    application::AgentRegistryService& agents,
    application::CommandOrchestrator& commands,
    application::AuditQueryService& audits)
    : agents_(agents), commands_(commands), audits_(audits) {}

HttpResult<domain::AgentState> HttpUseCases::get_agent(const std::string& agent_mac) const {
  HttpResult<domain::AgentState> out;
  try {
    if (agent_mac.empty()) {
      out.error = HttpError{400, "agent_mac is required"};
      return out;
    }
    domain::AgentState state;
    if (!agents_.get_agent(agent_mac, state)) {
      out.error = HttpError{404, "agent not found"};
      return out;
    }
    out.ok = true;
    out.data = std::move(state);
    return out;
  } catch (const std::exception& ex) {
    out.error = HttpError{500, ex.what()};
    return out;
  }
}

HttpResult<std::vector<domain::AgentState>> HttpUseCases::list_agents(bool include_offline) const {
  HttpResult<std::vector<domain::AgentState>> out;
  try {
    out.data = agents_.list_agents(include_offline);
    out.ok = true;
    return out;
  } catch (const std::exception& ex) {
    out.error = HttpError{500, ex.what()};
    return out;
  }
}

HttpResult<PushCommandResponse> HttpUseCases::push_command(const PushCommandRequest& request) const {
  HttpResult<PushCommandResponse> out;
  try {
    out.data.output = commands_.submit(request.input);
    out.ok = true;
    return out;
  } catch (const std::invalid_argument& ex) {
    out.error = HttpError{400, ex.what()};
    return out;
  } catch (const std::exception& ex) {
    out.error = HttpError{500, ex.what()};
    return out;
  }
}

HttpResult<CommandDetails> HttpUseCases::get_command(
    const std::string& command_id,
    int event_limit) const {
  HttpResult<CommandDetails> out;
  try {
    if (command_id.empty()) {
      out.error = HttpError{400, "command_id is required"};
      return out;
    }
    out.data.command = commands_.get(command_id);
    out.data.events = commands_.events(command_id, event_limit);
    out.ok = true;
    return out;
  } catch (const std::invalid_argument& ex) {
    out.error = HttpError{400, ex.what()};
    return out;
  } catch (const std::exception& ex) {
    out.error = HttpError{500, ex.what()};
    return out;
  }
}

HttpResult<domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor>>
HttpUseCases::list_commands(const domain::CommandListFilter& filter) const {
  HttpResult<domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor>> out;
  try {
    out.data = commands_.list(filter);
    out.ok = true;
    return out;
  } catch (const std::exception& ex) {
    out.error = HttpError{500, ex.what()};
    return out;
  }
}

HttpResult<domain::ListPage<domain::AuditEntry, domain::AuditListCursor>>
HttpUseCases::list_audits(const domain::AuditListFilter& filter) const {
  HttpResult<domain::ListPage<domain::AuditEntry, domain::AuditListCursor>> out;
  try {
    out.data = audits_.list(filter);
    out.ok = true;
    return out;
  } catch (const std::exception& ex) {
    out.error = HttpError{500, ex.what()};
    return out;
  }
}

} // namespace ctrl::adapters
