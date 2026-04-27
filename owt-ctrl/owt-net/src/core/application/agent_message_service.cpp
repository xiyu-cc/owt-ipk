#include "ctrl/application/agent_message_service.h"

#include "app/runtime_log.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace ctrl::application {

AgentMessageService::AgentMessageService(
    ports::ICommandRepository& commands,
    AgentRegistryService& registry,
    ports::IStatusPublisher& publisher,
    ports::IMetrics& metrics,
    const ports::IClock& clock)
    : commands_(commands),
      registry_(registry),
      publisher_(publisher),
      metrics_(metrics),
      clock_(clock) {}

void AgentMessageService::on_agent_registered(
    std::string_view agent_mac,
    std::string_view display_id,
    const nlohmann::json& meta) {
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }

  domain::AgentState state;
  state.agent.mac = std::string(agent_mac);
  state.agent.display_id = display_id.empty() ? std::string(agent_mac) : std::string(display_id);
  state.online = true;
  state.registered_at_ms = clock_.now_ms();
  state.last_seen_at_ms = state.registered_at_ms;
  state.last_heartbeat_at_ms = state.registered_at_ms;

  if (meta.is_object()) {
    if (meta.contains("site_id") && meta["site_id"].is_string()) {
      state.site_id = meta["site_id"].get<std::string>();
    }
    if (meta.contains("agent_version") && meta["agent_version"].is_string()) {
      state.version = meta["agent_version"].get<std::string>();
    } else if (meta.contains("version") && meta["version"].is_string()) {
      state.version = meta["version"].get<std::string>();
    }
    if (meta.contains("capabilities") && meta["capabilities"].is_array()) {
      for (const auto& item : meta["capabilities"]) {
        if (item.is_string()) {
          state.capabilities.push_back(item.get<std::string>());
        }
      }
    }
    if (meta.contains("stats") && meta["stats"].is_object()) {
      state.stats = meta["stats"];
    }
  }

  std::string persist_error;
  if (!registry_.on_register(state, &persist_error)) {
    log::warn(
        "agent register persisted in memory but db upsert failed: agent_mac={}, error={}, persist_failure_count={}",
        agent_mac,
        persist_error.empty() ? "unknown" : persist_error,
        registry_.persist_failure_count());
  }
  publisher_.publish_agent("agent_register", agent_mac);
  publisher_.publish_snapshot("agent_register", agent_mac);
}

void AgentMessageService::on_agent_heartbeat(
    std::string_view agent_mac,
    const nlohmann::json& heartbeat_stats,
    int64_t heartbeat_at_ms) {
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  std::string persist_error;
  if (!registry_.on_heartbeat(agent_mac, heartbeat_stats, heartbeat_at_ms, &persist_error)) {
    log::warn(
        "agent heartbeat persisted in memory but db upsert failed: agent_mac={}, error={}, persist_failure_count={}",
        agent_mac,
        persist_error.empty() ? "unknown" : persist_error,
        registry_.persist_failure_count());
  }
  publisher_.publish_agent("agent_heartbeat", agent_mac);
}

void AgentMessageService::on_command_ack(
    std::string_view agent_mac,
    std::string_view command_id,
    domain::CommandState ack_state,
    std::string_view trace_id,
    std::string_view message) {
  if (command_id.empty()) {
    throw std::invalid_argument("command_id is required");
  }
  if (ack_state != domain::CommandState::Acked && ack_state != domain::CommandState::Running) {
    throw std::invalid_argument("ack_state must be Acked or Running");
  }

  bool applied = false;
  std::string error;
  if (!commands_.update_state_if_not_terminal(
          command_id,
          ack_state,
          nlohmann::json::object(),
          clock_.now_ms(),
          applied,
          error)) {
    throw std::runtime_error("update ack state failed: " + error);
  }

  const auto event = append_event(
      command_id,
      applied ? "command_ack_received" : "command_ack_ignored_terminal",
      ack_state,
      nlohmann::json{
          {"agent_mac", std::string(agent_mac)},
          {"trace_id", std::string(trace_id)},
          {"message", std::string(message)},
      });
  domain::CommandSnapshot snapshot;
  std::string load_error;
  if (commands_.get(command_id, snapshot, load_error)) {
    publisher_.publish_command_event(snapshot, event);
  }
}

void AgentMessageService::on_command_result(
    std::string_view agent_mac,
    std::string_view command_id,
    domain::CommandState final_state,
    int exit_code,
    const nlohmann::json& result,
    std::string_view trace_id) {
  if (command_id.empty()) {
    throw std::invalid_argument("command_id is required");
  }
  if (!domain::is_terminal(final_state)) {
    throw std::invalid_argument("final_state must be terminal");
  }

  bool applied = false;
  std::string error;
  if (!commands_.update_terminal_state_once(
          command_id,
          final_state,
          result,
          clock_.now_ms(),
          applied,
          error)) {
    throw std::runtime_error("update terminal state failed: " + error);
  }

  const auto event = append_event(
      command_id,
      applied ? "command_result_received" : "command_result_duplicate",
      final_state,
      nlohmann::json{
          {"agent_mac", std::string(agent_mac)},
          {"trace_id", std::string(trace_id)},
          {"exit_code", exit_code},
          {"result", result},
      });
  domain::CommandSnapshot snapshot;
  std::string load_error;
  if (commands_.get(command_id, snapshot, load_error)) {
    publisher_.publish_command_event(snapshot, event);
  }

  if (applied) {
    metrics_.record_command_terminal_status(command_id, final_state, result);
  }
  publisher_.publish_snapshot(
      applied ? "command_result" : "command_result_duplicate",
      agent_mac);
}

domain::CommandEvent AgentMessageService::append_event(
    std::string_view command_id,
    std::string_view type,
    domain::CommandState state,
    const nlohmann::json& detail) {
  domain::CommandEvent event;
  event.command_id = std::string(command_id);
  event.type = std::string(type);
  event.state = state;
  event.detail = detail;
  event.created_at_ms = clock_.now_ms();

  std::string error;
  if (!commands_.append_event(event, error)) {
    throw std::runtime_error("append event failed: " + error);
  }
  return event;
}

} // namespace ctrl::application
