#include "app/bootstrap/runtime/ui_action_gateway.h"

#include "app/presenter/serializers.h"
#include "app/runtime_log.h"
#include "owt/protocol/v5/contract.h"

#include <algorithm>
#include <stdexcept>

namespace app::bootstrap::runtime {

UiActionGateway::UiActionGateway(
    UiSubscriptionStore& subscriptions,
    UiSessionRegistry& ui_sessions,
    app::ws::scheduler::EventScheduler& event_scheduler,
    BusStatusPublisher& status_publisher,
    ctrl::application::AgentRegistryService& registry_service,
    ctrl::application::ParamsService& params_service,
    ctrl::application::RateLimiterService& rate_limiter_service,
    ctrl::application::CommandOrchestrator& command_orchestrator,
    ctrl::application::RedactionService& redaction_service,
    ctrl::ports::IMetrics& metrics,
    const ctrl::ports::IClock& clock)
    : subscriptions_(subscriptions),
      ui_sessions_(ui_sessions),
      event_scheduler_(event_scheduler),
      status_publisher_(status_publisher),
      registry_service_(registry_service),
      params_service_(params_service),
      rate_limiter_service_(rate_limiter_service),
      command_orchestrator_(command_orchestrator),
      redaction_service_(redaction_service),
      metrics_(metrics),
      clock_(clock) {
  handlers_ = {
      {std::string(owt::protocol::v5::ui::kActionSessionSubscribe), [this](const auto& s, const auto& a, const auto& r) {
         handle_session_subscribe(s, a, r);
       }},
      {std::string(owt::protocol::v5::ui::kActionAgentList), [this](const auto& s, const auto& a, const auto& r) {
         handle_agent_list(s, a, r);
       }},
      {std::string(owt::protocol::v5::ui::kActionParamsGet), [this](const auto& s, const auto& a, const auto& r) {
         handle_params_get(s, a, r);
       }},
      {std::string(owt::protocol::v5::ui::kActionParamsUpdate), [this](const auto& s, const auto& a, const auto& r) {
         handle_params_update(s, a, r);
       }},
      {std::string(owt::protocol::v5::ui::kActionCommandSubmit), [this](const auto& s, const auto& a, const auto& r) {
         handle_command_submit(s, a, r);
       }},
  };
}

void UiActionGateway::send_ui_result(
    std::string_view session_id,
    std::string_view name,
    const nlohmann::json& id,
    const nlohmann::json& payload) {
  (void)ui_sessions_.enqueue(
      session_id,
      ws::bus_result(name, id, clock_.now_ms(), payload));
}

void UiActionGateway::send_ui_error(
    std::string_view session_id,
    std::string_view name,
    const nlohmann::json& id,
    std::string_view code,
    std::string message,
    const nlohmann::json& detail) {
  (void)ui_sessions_.enqueue(
      session_id,
      ws::bus_error(name, id, clock_.now_ms(), code, std::move(message), detail));
}

bool UiActionGateway::enforce_ui_rate_limit(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  int64_t retry_after_ms = 0;
  const auto key = actor_id + ":" + req.name;
  if (rate_limiter_service_.allow(key, clock_.now_ms(), retry_after_ms)) {
    return true;
  }
  metrics_.record_rate_limited(actor_id, retry_after_ms);
  send_ui_error(
      session_id,
      req.name,
      req.id,
      owt::protocol::v5::error_code::kRateLimited,
      "rate limited",
      nlohmann::json{{"retry_after_ms", retry_after_ms}});
  return false;
}

void UiActionGateway::handle_session_subscribe(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  (void)actor_id;
  const auto scope = req.payload.value("scope", std::string{"all"});
  if (scope == "all") {
    subscriptions_.subscribe_all(session_id);
    const auto post_result = event_scheduler_.post(
        session_id,
        app::ws::scheduler::EventPriority::Low,
        [this, session_id] {
          status_publisher_.push_snapshot_to_session(session_id, "subscribe");
        });
    if (post_result != app::ws::scheduler::PostResult::Accepted) {
      log::warn(
          "subscribe snapshot enqueue failed: session_id={}, result={}",
          session_id,
          to_post_result_string(post_result));
    }
    send_ui_result(session_id, req.name, req.id, nlohmann::json{{"scope", "all"}});
    return;
  }

  if (scope == "agent") {
    if (!req.payload.contains("agent_mac") || !req.payload["agent_mac"].is_string() ||
        req.payload["agent_mac"].get<std::string>().empty()) {
      throw std::invalid_argument("agent_mac is required when scope=agent");
    }
    const auto agent_mac = req.payload["agent_mac"].get<std::string>();
    subscriptions_.subscribe_agent(session_id, agent_mac);
    const auto post_result = event_scheduler_.post(
        session_id,
        app::ws::scheduler::EventPriority::High,
        [this, session_id, agent_mac] {
          status_publisher_.push_agent_to_session(session_id, agent_mac, "subscribe");
        });
    if (post_result != app::ws::scheduler::PostResult::Accepted) {
      log::warn(
          "subscribe agent enqueue failed: session_id={}, result={}, agent_mac={}",
          session_id,
          to_post_result_string(post_result),
          agent_mac);
    }
    send_ui_result(
        session_id,
        req.name,
        req.id,
        nlohmann::json{{"scope", "agent"}, {"agent_mac", agent_mac}});
    return;
  }

  throw std::invalid_argument("scope must be all or agent");
}

void UiActionGateway::handle_agent_list(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  (void)actor_id;

  bool include_offline = true;
  if (req.payload.contains("include_offline")) {
    if (!req.payload["include_offline"].is_boolean()) {
      throw std::invalid_argument("include_offline must be boolean");
    }
    include_offline = req.payload["include_offline"].get<bool>();
  }

  const auto rows = registry_service_.list_agents(include_offline);
  nlohmann::json agents = nlohmann::json::array();
  size_t online_count = 0;
  for (const auto& row : rows) {
    if (row.online) {
      ++online_count;
    }
    agents.push_back(presenter::to_agent_json(row));
  }
  send_ui_result(
      session_id,
      req.name,
      req.id,
      nlohmann::json{
          {"agents", std::move(agents)},
          {"online_count", online_count},
          {"total_count", rows.size()},
          {"include_offline", include_offline},
      });
}

void UiActionGateway::handle_params_get(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  (void)actor_id;
  if (!req.payload.contains("agent_mac") || !req.payload["agent_mac"].is_string() ||
      req.payload["agent_mac"].get<std::string>().empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  const auto agent_mac = req.payload["agent_mac"].get<std::string>();
  auto params = params_service_.load_or_init(agent_mac);
  send_ui_result(
      session_id,
      req.name,
      req.id,
      nlohmann::json{{"agent_mac", agent_mac}, {"params", std::move(params)}});
}

void UiActionGateway::handle_params_update(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  if (!enforce_ui_rate_limit(session_id, actor_id, req)) {
    return;
  }
  if (!req.payload.contains("agent_mac") || !req.payload["agent_mac"].is_string() ||
      req.payload["agent_mac"].get<std::string>().empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  if (!req.payload.contains("params") || !req.payload["params"].is_object()) {
    throw std::invalid_argument("params is required and must be object");
  }
  const auto agent_mac = req.payload["agent_mac"].get<std::string>();
  const auto merged = params_service_.merge_and_validate(agent_mac, req.payload["params"]);
  params_service_.save(agent_mac, merged);

  ctrl::application::SubmitCommandInput input;
  input.agent.mac = agent_mac;
  ctrl::domain::AgentState agent_state;
  if (registry_service_.get_agent(agent_mac, agent_state)) {
    input.agent.display_id = agent_state.agent.display_id;
  }
  if (input.agent.display_id.empty()) {
    input.agent.display_id = req.payload.value("agent_id", agent_mac);
  }
  input.kind = ctrl::domain::CommandKind::ParamsSet;
  input.payload = merged;
  input.timeout_ms = std::max(100, req.payload.value("timeout_ms", 5000));
  input.max_retry = std::max(1, req.payload.value("max_retry", 1));
  input.wait_result = false;
  input.actor_type = "ui";
  input.actor_id = actor_id;

  const auto out = command_orchestrator_.submit(input);
  send_ui_result(
      session_id,
      req.name,
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
      });
}

void UiActionGateway::handle_command_submit(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  if (!enforce_ui_rate_limit(session_id, actor_id, req)) {
    return;
  }

  if (!req.payload.contains("agent_mac") || !req.payload["agent_mac"].is_string() ||
      req.payload["agent_mac"].get<std::string>().empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  if (!req.payload.contains("command_type") || !req.payload["command_type"].is_string()) {
    throw std::invalid_argument("command_type is required");
  }

  ctrl::domain::CommandKind kind = ctrl::domain::CommandKind::WakeOnLan;
  if (!ctrl::domain::try_parse_command_kind(req.payload["command_type"].get<std::string>(), kind)) {
    throw std::invalid_argument("invalid command_type");
  }

  ctrl::application::SubmitCommandInput input;
  input.agent.mac = req.payload["agent_mac"].get<std::string>();
  input.agent.display_id = req.payload.value("agent_id", input.agent.mac);
  input.kind = kind;
  input.payload = req.payload.value("payload", nlohmann::json::object());
  if (!input.payload.is_object()) {
    throw std::invalid_argument("payload must be object");
  }
  input.timeout_ms = std::max(100, req.payload.value("timeout_ms", 5000));
  input.max_retry = std::max(0, req.payload.value("max_retry", 1));
  input.wait_result = false;
  input.actor_type = "ui";
  input.actor_id = actor_id;

  const auto out = command_orchestrator_.submit(input);
  send_ui_result(
      session_id,
      req.name,
      req.id,
      nlohmann::json{
          {"accepted", true},
          {"agent_mac", input.agent.mac},
          {"agent_id", input.agent.display_id},
          {"command_type", ctrl::domain::to_string(input.kind)},
          {"command_id", out.command_id},
          {"trace_id", out.trace_id},
          {"status", ctrl::domain::to_string(out.state)},
          {"updated_at_ms", out.updated_at_ms},
          {"result_meta",
           {
               {"terminal", out.terminal},
               {"wait_timed_out", out.wait_timed_out},
           }},
      });
}

void UiActionGateway::handle(
    const std::string& session_id,
    const std::string& actor_id,
    const ws::BusEnvelope& req) {
  try {
    const auto it = handlers_.find(req.name);
    if (it == handlers_.end()) {
      send_ui_error(
          session_id,
          req.name,
          req.id,
          owt::protocol::v5::error_code::kMethodNotFound,
          "method not found");
      return;
    }
    it->second(session_id, actor_id, req);
  } catch (const std::invalid_argument& ex) {
    send_ui_error(
        session_id,
        req.name,
        req.id,
        owt::protocol::v5::error_code::kInvalidParams,
        redaction_service_.redact_text(ex.what()));
  } catch (const std::exception& ex) {
    send_ui_error(
        session_id,
        req.name,
        req.id,
        owt::protocol::v5::error_code::kInternalError,
        redaction_service_.redact_text(ex.what()));
  }
}

} // namespace app::bootstrap::runtime
