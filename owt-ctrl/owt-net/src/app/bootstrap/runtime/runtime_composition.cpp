#include "app/bootstrap/runtime/runtime_composition.h"

#include "app/runtime_log.h"
#include "owt/protocol/v5/contract.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace app::bootstrap::runtime {

RuntimeComposition::RuntimeComposition(const Config& cfg)
    : config(cfg),
      store(config.storage.db_path),
      ui_sessions(UiSessionRegistry::Config{
          .queue_limit = std::max(32, config.server.ui_session_queue_limit),
          .send_timeout_ms = std::max(100, config.server.ui_event_send_timeout_ms),
      }),
      agent_channel(clock, agent_sessions),
      registry_service(store, clock),
      status_publisher(registry_service, clock, subscriptions, ui_sessions, event_scheduler),
      params_service(store, clock),
      command_orchestrator(
          store,
          agent_channel,
          params_service,
          store,
          status_publisher,
          metrics,
          clock,
          id_generator),
      agent_message_service(
          store,
          registry_service,
          status_publisher,
          metrics,
          clock,
          [this](std::string_view agent_mac, std::string_view display_id, const nlohmann::json&) {
            if (agent_mac.empty()) {
              return;
            }
            const std::string agent_mac_key(agent_mac);
            const std::string agent_display_id = display_id.empty()
                ? agent_mac_key
                : std::string(display_id);

            const auto post_result = event_scheduler.post(
                agent_mac_key,
                app::ws::scheduler::EventPriority::High,
                [this, agent_mac_key, agent_display_id] {
                  try {
                    const auto persisted = params_service.load_existing(agent_mac_key);
                    if (!persisted.has_value()) {
                      return;
                    }
                    ctrl::application::SubmitCommandInput input;
                    input.agent.mac = agent_mac_key;
                    input.agent.display_id = agent_display_id;
                    input.kind = ctrl::domain::CommandKind::ParamsSet;
                    input.payload = *persisted;
                    input.wait_result = false;
                    input.max_retry = 1;
                    input.actor_type = "system";
                    input.actor_id = "agent-register-resync";
                    (void)command_orchestrator.submit(input);
                  } catch (const std::exception& ex) {
                    log::warn(
                        "agent register params resync failed: agent_mac={}, error={}",
                        agent_mac_key,
                        ex.what());
                  } catch (...) {
                    log::warn(
                        "agent register params resync failed: agent_mac={}, error=unknown",
                        agent_mac_key);
                  }
                });

            if (post_result != app::ws::scheduler::PostResult::Accepted) {
              log::warn(
                  "agent register params resync enqueue failed: agent_mac={}, result={}",
                  agent_mac_key,
                  to_post_result_string(post_result));
            }
          }),
      control_ws_use_cases(registry_service, agent_message_service),
      retry_service(store, agent_channel, status_publisher, metrics, clock),
      lifecycle(
          RuntimeLifecycle::Options{
              .retry_tick_ms = config.server.retry_tick_ms,
              .retry_batch = config.server.retry_batch,
              .cleanup_interval_sec = config.storage.cleanup_interval_sec,
              .retention_days = config.storage.retention_days,
          },
          [this](int batch) { retry_service.tick_once(batch); },
          [this](int retention_days, int64_t now_ms) { store.cleanup_retention(retention_days, now_ms); },
          [this]() { return clock.now_ms(); },
          [](const char* worker, const char* message) {
            log::warn("{} worker {}", worker, message);
          }),
      ui_gateway(
          subscriptions,
          ui_sessions,
          event_scheduler,
          status_publisher,
          registry_service,
          params_service,
          rate_limiter_service,
          command_orchestrator,
          redaction_service,
          metrics,
          clock),
      agent_gateway(
          agent_sessions,
          control_ws_use_cases,
          redaction_service,
          clock,
          [this]() { return trace_id(); }) {
  rate_limiter_service.configure(
      config.server.enable_rate_limit,
      config.server.rate_limit_rps,
      config.server.rate_limit_burst);
}

std::string RuntimeComposition::trace_id() {
  const auto now = clock.now_ms();
  const auto id = trace_id_seq_.fetch_add(1, std::memory_order_relaxed);
  return "trc-" + std::to_string(now) + "-" + std::to_string(id);
}

void RuntimeComposition::on_ui_open(
    const drogon::HttpRequestPtr& req,
    const drogon::WebSocketConnectionPtr& conn) {
  const auto session_id = next_session_id();
  const auto actor_id = normalize_actor_id(req, session_id);

  conn->setContext(std::make_shared<ConnectionContext>(ConnectionContext{session_id, actor_id}));
  subscriptions.subscribe_all(session_id);
  ui_sessions.add_session(session_id, conn, actor_id);

  const auto post_result = event_scheduler.post(
      session_id,
      app::ws::scheduler::EventPriority::Low,
      [this, session_id] {
        status_publisher.push_snapshot_to_session(session_id, "session_open");
      });
  if (post_result != app::ws::scheduler::PostResult::Accepted) {
    log::warn(
        "initial snapshot enqueue failed: session_id={}, result={}",
        session_id,
        to_post_result_string(post_result));
  }
}

void RuntimeComposition::on_ui_close(const drogon::WebSocketConnectionPtr& conn) {
  if (!conn || !conn->hasContext()) {
    return;
  }
  const auto ctx = conn->getContext<ConnectionContext>();
  if (!ctx) {
    return;
  }
  subscriptions.unsubscribe(ctx->session_id);
  ui_sessions.remove_session(ctx->session_id);
}

void RuntimeComposition::on_ui_message(
    const drogon::WebSocketConnectionPtr& conn,
    std::string&& message,
    const drogon::WebSocketMessageType& type) {
  if (type != drogon::WebSocketMessageType::Text) {
    return;
  }
  if (!conn || !conn->hasContext()) {
    return;
  }
  const auto ctx = conn->getContext<ConnectionContext>();
  if (!ctx) {
    return;
  }

  ws::BusEnvelope req;
  if (!parse_and_validate_action_envelope(WsPeer::Ui, message, clock, conn, req)) {
    return;
  }

  auto partition_key = ctx->session_id;
  if (req.name != owt::protocol::v5::ui::kActionSessionSubscribe &&
      req.payload.contains("agent_mac") && req.payload["agent_mac"].is_string() &&
      !req.payload["agent_mac"].get<std::string>().empty()) {
    partition_key = req.payload["agent_mac"].get<std::string>();
  }

  const auto req_name = req.name;
  const auto req_id = req.id;
  const auto post_result = event_scheduler.post(
      partition_key,
      app::ws::scheduler::EventPriority::High,
      [this, session_id = ctx->session_id, actor_id = ctx->actor_id, req = std::move(req)] {
        ui_gateway.handle(session_id, actor_id, req);
      });
  if (post_result != app::ws::scheduler::PostResult::Accepted) {
    conn->send(
        ws::bus_error(
            req_name,
            req_id,
            clock.now_ms(),
            owt::protocol::v5::error_code::kInternalError,
            "server busy",
            nlohmann::json{{"scheduler_result", to_post_result_string(post_result)}}),
        drogon::WebSocketMessageType::Text);
  }
}

void RuntimeComposition::on_agent_open(
    const drogon::HttpRequestPtr& req,
    const drogon::WebSocketConnectionPtr& conn) {
  const auto session_id = next_session_id();
  const auto actor_id = normalize_actor_id(req, "owt-agent");
  conn->setContext(std::make_shared<ConnectionContext>(ConnectionContext{session_id, actor_id}));
  agent_sessions.add_connection(session_id, conn);
  control_ws_use_cases.on_open(session_id);
}

void RuntimeComposition::on_agent_close(const drogon::WebSocketConnectionPtr& conn) {
  if (!conn || !conn->hasContext()) {
    return;
  }
  const auto ctx = conn->getContext<ConnectionContext>();
  if (!ctx) {
    return;
  }
  control_ws_use_cases.on_close(ctx->session_id);
  (void)agent_sessions.unbind_session(ctx->session_id);
}

void RuntimeComposition::on_agent_message(
    const drogon::WebSocketConnectionPtr& conn,
    std::string&& message,
    const drogon::WebSocketMessageType& type) {
  if (type != drogon::WebSocketMessageType::Text) {
    return;
  }
  if (!conn || !conn->hasContext()) {
    return;
  }
  const auto ctx = conn->getContext<ConnectionContext>();
  if (!ctx) {
    return;
  }

  ws::BusEnvelope req;
  if (!parse_and_validate_action_envelope(WsPeer::Agent, message, clock, conn, req)) {
    return;
  }

  std::string partition_key = ctx->session_id;
  if (req.payload.contains("agent_mac") && req.payload["agent_mac"].is_string() &&
      !req.payload["agent_mac"].get<std::string>().empty()) {
    partition_key = req.payload["agent_mac"].get<std::string>();
  } else {
    const auto session_agent = agent_sessions.find_agent_for_session(ctx->session_id);
    if (!session_agent.empty()) {
      partition_key = session_agent;
    }
  }

  const auto req_name = req.name;
  const auto req_id = req.id;
  const auto post_result = event_scheduler.post(
      partition_key,
      app::ws::scheduler::EventPriority::High,
      [this, conn, session_id = ctx->session_id, req = std::move(req)] {
        agent_gateway.handle(conn, session_id, req);
      });
  if (post_result != app::ws::scheduler::PostResult::Accepted) {
    conn->send(
        ws::bus_error(
            req_name,
            req_id,
            clock.now_ms(),
            owt::protocol::v5::error_code::kInternalError,
            "server busy",
            nlohmann::json{{"scheduler_result", to_post_result_string(post_result)}}),
        drogon::WebSocketMessageType::Text);
  }
}

} // namespace app::bootstrap::runtime
