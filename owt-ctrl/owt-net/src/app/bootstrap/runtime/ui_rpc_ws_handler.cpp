#include "internal.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace app::bootstrap::runtime_internal {

UiRpcWsHandler::UiRpcWsHandler(RuntimeImplState& state) : state_(state) {}

void UiRpcWsHandler::on_join(ws_deal::ws_hub_api& hub, std::string_view session_id) {
  state_.status_publisher.set_hub(&hub);
  state_.subscriptions.subscribe_all(session_id);
  state_.status_publisher.push_snapshot_to_session(session_id, "session_open");
}

void UiRpcWsHandler::on_leave(ws_deal::ws_hub_api& hub, std::string_view session_id) {
  (void)hub;
  state_.subscriptions.unsubscribe(session_id);
}

void UiRpcWsHandler::on_message(ws_deal::ws_hub_api& hub, ws_deal::inbound_message message) {
  state_.status_publisher.set_hub(&hub);
  if (!message.text) {
    return;
  }

  ws::JsonRpcRequest req;
  std::string parse_error;
  int parse_error_code = -32600;
  if (!ws::parse_jsonrpc_request(message.payload, req, parse_error, parse_error_code)) {
    hub.publish_to_session(
        message.session_id,
        true,
        ws::jsonrpc_error(nullptr, parse_error_code, parse_error));
    return;
  }

  const auto respond = [&](const std::string& text) {
    if (req.notification) {
      return;
    }
    hub.publish_to_session(message.session_id, true, text);
  };

  try {
    if (req.method == "subscribe") {
      const auto scope = req.params.value("scope", std::string{"all"});
      if (scope == "all") {
        state_.subscriptions.subscribe_all(message.session_id);
        state_.status_publisher.push_snapshot_to_session(message.session_id, "subscribe");
        respond(ws::jsonrpc_result(req.id, nlohmann::json{{"scope", "all"}}));
        return;
      }
      if (scope == "agent") {
        if (!req.params.contains("agent_mac") || !req.params["agent_mac"].is_string() ||
            req.params["agent_mac"].get<std::string>().empty()) {
          throw std::invalid_argument("agent_mac is required when scope=agent");
        }
        const auto agent_mac = req.params["agent_mac"].get<std::string>();
        state_.subscriptions.subscribe_agent(message.session_id, agent_mac);
        state_.status_publisher.push_agent_to_session(message.session_id, agent_mac, "subscribe");
        respond(ws::jsonrpc_result(
            req.id,
            nlohmann::json{{"scope", "agent"}, {"agent_mac", agent_mac}}));
        return;
      }
      throw std::invalid_argument("scope must be all or agent");
    }

    if (req.method == "unsubscribe") {
      state_.subscriptions.unsubscribe(message.session_id);
      respond(ws::jsonrpc_result(req.id, nlohmann::json{{"ok", true}}));
      return;
    }

    if (req.method == "agent_list") {
      const bool include_offline = !req.params.contains("include_offline") ||
          !req.params["include_offline"].is_boolean() ||
          req.params["include_offline"].get<bool>();
      const auto rows = state_.registry_service.list_agents(include_offline);
      nlohmann::json agents = nlohmann::json::array();
      size_t online_count = 0;
      for (const auto& row : rows) {
        if (row.online) {
          ++online_count;
        }
        agents.push_back(presenter::to_agent_json(row));
      }
      respond(ws::jsonrpc_result(
          req.id,
          nlohmann::json{
              {"agents", std::move(agents)},
              {"online_count", online_count},
              {"total_count", rows.size()},
              {"include_offline", include_offline},
          }));
      return;
    }

    if (req.method == "agent_get") {
      if (!req.params.contains("agent_mac") || !req.params["agent_mac"].is_string() ||
          req.params["agent_mac"].get<std::string>().empty()) {
        throw std::invalid_argument("agent_mac is required");
      }
      ctrl::domain::AgentState row;
      const auto agent_mac = req.params["agent_mac"].get<std::string>();
      if (!state_.registry_service.get_agent(agent_mac, row)) {
        respond(ws::jsonrpc_error(req.id, -32004, "agent not found"));
        return;
      }
      respond(ws::jsonrpc_result(req.id, presenter::to_agent_json(row)));
      return;
    }

    if (req.method == "params_get") {
      if (!req.params.contains("agent_mac") || !req.params["agent_mac"].is_string() ||
          req.params["agent_mac"].get<std::string>().empty()) {
        throw std::invalid_argument("agent_mac is required");
      }
      const auto agent_mac = req.params["agent_mac"].get<std::string>();
      auto params = state_.params_service.load_or_init(agent_mac);
      respond(ws::jsonrpc_result(req.id, nlohmann::json{{"agent_mac", agent_mac}, {"params", std::move(params)}}));
      return;
    }

    if (req.method == "params_put") {
      if (!req.params.contains("agent_mac") || !req.params["agent_mac"].is_string() ||
          req.params["agent_mac"].get<std::string>().empty()) {
        throw std::invalid_argument("agent_mac is required");
      }
      if (!req.params.contains("params") || !req.params["params"].is_object()) {
        throw std::invalid_argument("params is required and must be object");
      }
      const auto agent_mac = req.params["agent_mac"].get<std::string>();
      const auto merged = state_.params_service.merge_and_validate(agent_mac, req.params["params"]);
      state_.params_service.save(agent_mac, merged);

      ctrl::application::SubmitCommandInput input;
      input.agent.mac = agent_mac;
      ctrl::domain::AgentState agent_state;
      if (state_.registry_service.get_agent(agent_mac, agent_state)) {
        input.agent.display_id = agent_state.agent.display_id;
      }
      if (input.agent.display_id.empty()) {
        input.agent.display_id = req.params.value("agent_id", agent_mac);
      }
      input.kind = ctrl::domain::CommandKind::ParamsSet;
      input.payload = merged;
      input.timeout_ms = std::max(100, req.params.value("timeout_ms", 5000));
      input.max_retry = std::max(1, req.params.value("max_retry", 1));
      input.wait_result = false;
      input.actor_type = "ui_rpc";
      input.actor_id = message.session_id;
      const auto out = state_.command_orchestrator.submit(input);

      respond(ws::jsonrpc_result(
          req.id,
          nlohmann::json{
              {"agent_mac", agent_mac},
              {"params", merged},
              {"command",
               {
                   {"command_id", out.command_id},
                   {"trace_id", out.trace_id},
                   {"status", ctrl::domain::to_string(out.state)},
                   {"updated_at_ms", out.updated_at_ms},
               }},
          }));
      return;
    }

    if (req.method == "command_submit") {
      if (!req.params.contains("agent_mac") || !req.params["agent_mac"].is_string() ||
          req.params["agent_mac"].get<std::string>().empty()) {
        throw std::invalid_argument("agent_mac is required");
      }
      if (!req.params.contains("command_type") || !req.params["command_type"].is_string()) {
        throw std::invalid_argument("command_type is required");
      }

      ctrl::domain::CommandKind kind = ctrl::domain::CommandKind::HostProbeGet;
      if (!ctrl::domain::try_parse_command_kind(req.params["command_type"].get<std::string>(), kind)) {
        throw std::invalid_argument("invalid command_type");
      }

      ctrl::application::SubmitCommandInput input;
      input.agent.mac = req.params["agent_mac"].get<std::string>();
      input.agent.display_id = req.params.value("agent_id", input.agent.mac);
      input.kind = kind;
      input.payload = req.params.value("payload", nlohmann::json::object());
      if (!input.payload.is_object()) {
        throw std::invalid_argument("payload must be object");
      }
      input.timeout_ms = std::max(100, req.params.value("timeout_ms", 5000));
      input.max_retry = std::max(0, req.params.value("max_retry", 1));
      input.wait_result = false;
      input.actor_type = "ui_rpc";
      input.actor_id = message.session_id;

      const auto out = state_.command_orchestrator.submit(input);
      respond(ws::jsonrpc_result(
          req.id,
          nlohmann::json{
              {"agent_mac", input.agent.mac},
              {"agent_id", input.agent.display_id},
              {"command_type", ctrl::domain::to_string(input.kind)},
              {"command_id", out.command_id},
              {"trace_id", out.trace_id},
              {"status", ctrl::domain::to_string(out.state)},
              {"updated_at_ms", out.updated_at_ms},
          }));
      return;
    }

    if (req.method == "command_get") {
      if (!req.params.contains("command_id") || !req.params["command_id"].is_string() ||
          req.params["command_id"].get<std::string>().empty()) {
        throw std::invalid_argument("command_id is required");
      }
      int event_limit = 100;
      if (req.params.contains("event_limit") && !parse_int(req.params["event_limit"], event_limit)) {
        throw std::invalid_argument("event_limit must be integer");
      }
      event_limit = std::clamp(event_limit, 1, 500);
      const auto command_id = req.params["command_id"].get<std::string>();
      const auto command = state_.command_orchestrator.get(command_id);
      const auto events = state_.command_orchestrator.events(command_id, event_limit);
      nlohmann::json rows = nlohmann::json::array();
      for (const auto& row : events) {
        rows.push_back(presenter::to_command_event_json(row));
      }
      respond(ws::jsonrpc_result(
          req.id,
          nlohmann::json{{"command", presenter::to_command_json(command)}, {"events", std::move(rows)}}));
      return;
    }

    if (req.method == "command_list") {
      ctrl::domain::CommandListFilter filter;
      if (req.params.contains("agent_mac")) {
        if (!req.params["agent_mac"].is_string()) {
          throw std::invalid_argument("agent_mac must be string");
        }
        filter.agent_mac = req.params["agent_mac"].get<std::string>();
      }
      if (req.params.contains("status")) {
        if (!req.params["status"].is_string()) {
          throw std::invalid_argument("status must be string");
        }
        ctrl::domain::CommandState state;
        if (!ctrl::domain::try_parse_command_state(req.params["status"].get<std::string>(), state)) {
          throw std::invalid_argument("invalid status");
        }
        filter.state = state;
      }
      if (req.params.contains("command_type")) {
        if (!req.params["command_type"].is_string()) {
          throw std::invalid_argument("command_type must be string");
        }
        ctrl::domain::CommandKind kind;
        if (!ctrl::domain::try_parse_command_kind(req.params["command_type"].get<std::string>(), kind)) {
          throw std::invalid_argument("invalid command_type");
        }
        filter.kind = kind;
      }
      if (req.params.contains("limit")) {
        if (!parse_int(req.params["limit"], filter.limit) || filter.limit <= 0) {
          throw std::invalid_argument("invalid limit");
        }
      }
      filter.limit = std::clamp(filter.limit, 1, 500);
      if (req.params.contains("cursor")) {
        if (!req.params["cursor"].is_object()) {
          throw std::invalid_argument("cursor must be object");
        }
        const auto& cursor = req.params["cursor"];
        if (!cursor.contains("created_at_ms") || !cursor.contains("command_id") ||
            !cursor["command_id"].is_string()) {
          throw std::invalid_argument("cursor.created_at_ms and cursor.command_id are required");
        }
        int64_t created_at_ms = 0;
        if (!parse_int64(cursor["created_at_ms"], created_at_ms)) {
          throw std::invalid_argument("cursor.created_at_ms must be integer");
        }
        filter.cursor = ctrl::domain::CommandListCursor{created_at_ms, cursor["command_id"].get<std::string>()};
      }

      const auto page = state_.command_orchestrator.list(filter);
      nlohmann::json items = nlohmann::json::array();
      for (const auto& row : page.items) {
        items.push_back(presenter::to_command_json(row));
      }

      nlohmann::json next_cursor = nullptr;
      if (page.next_cursor.has_value()) {
        next_cursor = nlohmann::json{
            {"created_at_ms", page.next_cursor->created_at_ms},
            {"command_id", page.next_cursor->command_id},
        };
      }

      respond(ws::jsonrpc_result(
          req.id,
          nlohmann::json{{"items", std::move(items)}, {"has_more", page.has_more}, {"next_cursor", std::move(next_cursor)}}));
      return;
    }

    if (req.method == "audit_list") {
      ctrl::domain::AuditListFilter filter;
      if (req.params.contains("action")) {
        if (!req.params["action"].is_string()) {
          throw std::invalid_argument("action must be string");
        }
        filter.action = req.params["action"].get<std::string>();
      }
      if (req.params.contains("actor_type")) {
        if (!req.params["actor_type"].is_string()) {
          throw std::invalid_argument("actor_type must be string");
        }
        filter.actor_type = req.params["actor_type"].get<std::string>();
      }
      if (req.params.contains("actor_id")) {
        if (!req.params["actor_id"].is_string()) {
          throw std::invalid_argument("actor_id must be string");
        }
        filter.actor_id = req.params["actor_id"].get<std::string>();
      }
      if (req.params.contains("resource_type")) {
        if (!req.params["resource_type"].is_string()) {
          throw std::invalid_argument("resource_type must be string");
        }
        filter.resource_type = req.params["resource_type"].get<std::string>();
      }
      if (req.params.contains("resource_id")) {
        if (!req.params["resource_id"].is_string()) {
          throw std::invalid_argument("resource_id must be string");
        }
        filter.resource_id = req.params["resource_id"].get<std::string>();
      }
      if (req.params.contains("limit")) {
        if (!parse_int(req.params["limit"], filter.limit) || filter.limit <= 0) {
          throw std::invalid_argument("invalid limit");
        }
      }
      filter.limit = std::clamp(filter.limit, 1, 500);
      if (req.params.contains("cursor")) {
        if (!req.params["cursor"].is_object()) {
          throw std::invalid_argument("cursor must be object");
        }
        const auto& cursor = req.params["cursor"];
        if (!cursor.contains("id")) {
          throw std::invalid_argument("cursor.id is required");
        }
        int64_t id = 0;
        if (!parse_int64(cursor["id"], id)) {
          throw std::invalid_argument("cursor.id must be integer");
        }
        filter.cursor = ctrl::domain::AuditListCursor{id};
      }

      const auto page = state_.audit_query_service.list(filter);
      nlohmann::json items = nlohmann::json::array();
      for (const auto& row : page.items) {
        items.push_back(presenter::to_audit_json(row));
      }

      nlohmann::json next_cursor = nullptr;
      if (page.next_cursor.has_value()) {
        next_cursor = nlohmann::json{{"id", page.next_cursor->id}};
      }

      respond(ws::jsonrpc_result(
          req.id,
          nlohmann::json{{"items", std::move(items)}, {"has_more", page.has_more}, {"next_cursor", std::move(next_cursor)}}));
      return;
    }

    respond(ws::jsonrpc_error(req.id, -32601, "method not found"));
  } catch (const std::invalid_argument& ex) {
    respond(ws::jsonrpc_error(req.id, -32602, ex.what()));
  } catch (const std::exception& ex) {
    respond(ws::jsonrpc_error(req.id, -32000, ex.what()));
  }
}

} // namespace app::bootstrap::runtime_internal
