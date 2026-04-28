#include "ctrl/application/agent_message_service.h"

#include "app/runtime_log.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ctrl::application {

namespace {

bool is_invalid_transition_error(std::string_view error) {
  return error.rfind("invalid state transition:", 0) == 0;
}

} // namespace

AgentMessageService::AgentMessageService(
    ports::ICommandRepository& commands,
    AgentRegistryService& registry,
    ports::IStatusPublisher& publisher,
    ports::IMetrics& metrics,
    const ports::IClock& clock,
    RegisterSuccessCallback on_register_success)
    : commands_(commands),
      registry_(registry),
      publisher_(publisher),
      metrics_(metrics),
      clock_(clock),
      on_register_success_(std::move(on_register_success)) {}

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
    }
    if (meta.contains("capabilities") && meta["capabilities"].is_array()) {
      for (const auto& item : meta["capabilities"]) {
        if (item.is_string()) {
          state.capabilities.push_back(item.get<std::string>());
        }
      }
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
  if (on_register_success_) {
    try {
      on_register_success_(agent_mac, state.agent.display_id, meta);
    } catch (const std::exception& ex) {
      log::warn(
          "agent register callback failed: agent_mac={}, error={}",
          agent_mac,
          ex.what());
    } catch (...) {
      log::warn(
          "agent register callback failed: agent_mac={}, error=unknown",
          agent_mac);
    }
  }
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
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  if (command_id.empty()) {
    throw std::invalid_argument("command_id is required");
  }
  if (ack_state != domain::CommandState::Acked && ack_state != domain::CommandState::Running) {
    throw std::invalid_argument("ack_state must be Acked or Running");
  }

  domain::CommandSnapshot snapshot;
  std::string load_error;
  if (!commands_.get(command_id, snapshot, load_error)) {
    throw std::runtime_error("load command failed: " + load_error);
  }

  if (snapshot.agent.mac != agent_mac) {
    const auto event = append_event(
        command_id,
        "command_ack_rejected_agent_mismatch",
        snapshot.state,
        nlohmann::json{
            {"expected_agent_mac", snapshot.agent.mac},
            {"reported_agent_mac", std::string(agent_mac)},
            {"trace_id", std::string(trace_id)},
            {"message", std::string(message)},
        });
    publisher_.publish_command_event(snapshot, event);
    throw std::invalid_argument("agent_mac does not match command owner");
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
    if (is_invalid_transition_error(error)) {
      const auto event = append_event(
          command_id,
          "command_ack_rejected_invalid_transition",
          snapshot.state,
          nlohmann::json{
              {"current_state", domain::to_string(snapshot.state)},
              {"next_state", domain::to_string(ack_state)},
              {"trace_id", std::string(trace_id)},
              {"message", std::string(message)},
              {"error", error},
          });
      publisher_.publish_command_event(snapshot, event);
      throw std::invalid_argument(error);
    }
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
  domain::CommandSnapshot latest_snapshot;
  if (commands_.get(command_id, latest_snapshot, load_error)) {
    publisher_.publish_command_event(latest_snapshot, event);
  }
}

void AgentMessageService::on_command_result(
    std::string_view agent_mac,
    std::string_view command_id,
    domain::CommandState final_state,
    int exit_code,
    const nlohmann::json& result,
    std::string_view trace_id) {
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  if (command_id.empty()) {
    throw std::invalid_argument("command_id is required");
  }
  if (!domain::is_terminal(final_state)) {
    throw std::invalid_argument("final_state must be terminal");
  }

  domain::CommandSnapshot snapshot;
  std::string load_error;
  if (!commands_.get(command_id, snapshot, load_error)) {
    throw std::runtime_error("load command failed: " + load_error);
  }

  if (snapshot.agent.mac != agent_mac) {
    const auto event = append_event(
        command_id,
        "command_result_rejected_agent_mismatch",
        snapshot.state,
        nlohmann::json{
            {"expected_agent_mac", snapshot.agent.mac},
            {"reported_agent_mac", std::string(agent_mac)},
            {"trace_id", std::string(trace_id)},
            {"exit_code", exit_code},
            {"result", result},
        });
    publisher_.publish_command_event(snapshot, event);
    throw std::invalid_argument("agent_mac does not match command owner");
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
    if (is_invalid_transition_error(error)) {
      const auto event = append_event(
          command_id,
          "command_result_rejected_invalid_transition",
          snapshot.state,
          nlohmann::json{
              {"current_state", domain::to_string(snapshot.state)},
              {"next_state", domain::to_string(final_state)},
              {"trace_id", std::string(trace_id)},
              {"exit_code", exit_code},
              {"result", result},
              {"error", error},
          });
      publisher_.publish_command_event(snapshot, event);
      throw std::invalid_argument(error);
    }
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
