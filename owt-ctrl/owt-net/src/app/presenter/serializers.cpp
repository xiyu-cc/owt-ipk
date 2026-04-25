#include "app/presenter/serializers.h"
#include "ctrl/application/redaction_service.h"

namespace app::presenter {

namespace {

const ctrl::application::RedactionService& redaction() {
  static const ctrl::application::RedactionService s;
  return s;
}

} // namespace

nlohmann::json to_agent_json(const ctrl::domain::AgentState& state) {
  return {
      {"agent_mac", state.agent.mac},
      {"agent_id", state.agent.display_id},
      {"site_id", state.site_id},
      {"agent_version", state.version},
      {"capabilities", state.capabilities},
      {"online", state.online},
      {"registered_at_ms", state.registered_at_ms},
      {"last_heartbeat_at_ms", state.last_heartbeat_at_ms},
      {"last_seen_at_ms", state.last_seen_at_ms},
      {"stats", state.stats},
  };
}

nlohmann::json to_command_json(const ctrl::domain::CommandSnapshot& row) {
  return {
      {"command_id", row.spec.command_id},
      {"trace_id", row.spec.trace_id},
      {"agent_mac", row.agent.mac},
      {"agent_id", row.agent.display_id},
      {"command_type", ctrl::domain::to_string(row.spec.kind)},
      {"status", ctrl::domain::to_string(row.state)},
      {"payload", redaction().redact_json(row.spec.payload)},
      {"result", redaction().redact_json(row.result)},
      {"timeout_ms", row.spec.timeout_ms},
      {"max_retry", row.spec.max_retry},
      {"expires_at_ms", row.spec.expires_at_ms},
      {"retry_count", row.retry_count},
      {"next_retry_at_ms", row.next_retry_at_ms},
      {"last_error", row.last_error},
      {"created_at_ms", row.created_at_ms},
      {"updated_at_ms", row.updated_at_ms},
  };
}

nlohmann::json to_command_event_json(const ctrl::domain::CommandEvent& event) {
  return {
      {"command_id", event.command_id},
      {"event_type", event.type},
      {"status", ctrl::domain::to_string(event.state)},
      {"detail", redaction().redact_json(event.detail)},
      {"created_at_ms", event.created_at_ms},
  };
}

nlohmann::json to_audit_json(const ctrl::domain::AuditEntry& row) {
  return {
      {"id", row.id},
      {"actor_type", row.actor_type},
      {"actor_id", row.actor_id},
      {"action", row.action},
      {"resource_type", row.resource_type},
      {"resource_id", row.resource_id},
      {"summary", row.summary},
      {"created_at_ms", row.created_at_ms},
  };
}

nlohmann::json to_command_event_notification(
    const ctrl::domain::CommandSnapshot& command,
    const ctrl::domain::CommandEvent& event) {
  return {
      {"command", to_command_json(command)},
      {"event", to_command_event_json(event)},
  };
}

} // namespace app::presenter
