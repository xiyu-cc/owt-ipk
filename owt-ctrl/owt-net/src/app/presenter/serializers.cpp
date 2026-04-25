#include "app/presenter/serializers.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace app::presenter {

namespace {

std::string to_lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

bool is_sensitive_key(std::string_view key) {
  static const std::vector<std::string> tokens = {
      "password",
      "passwd",
      "pwd",
      "token",
      "secret",
      "private_key",
      "private-key",
  };
  const auto lowered = to_lower(std::string(key));
  for (const auto& token : tokens) {
    if (lowered.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

nlohmann::json redact_sensitive(const nlohmann::json& value) {
  if (value.is_object()) {
    nlohmann::json out = nlohmann::json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (is_sensitive_key(it.key())) {
        out[it.key()] = "***";
      } else {
        out[it.key()] = redact_sensitive(it.value());
      }
    }
    return out;
  }
  if (value.is_array()) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& item : value) {
      out.push_back(redact_sensitive(item));
    }
    return out;
  }
  return value;
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
      {"payload", redact_sensitive(row.spec.payload)},
      {"result", redact_sensitive(row.result)},
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
      {"detail", redact_sensitive(event.detail)},
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
