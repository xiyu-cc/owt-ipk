#include "app/bootstrap/runtime_impl.h"

#include "app/bootstrap/runtime/agent_action_gateway.h"
#include "app/bootstrap/runtime/lifecycle.h"
#include "app/bootstrap/runtime/session_runtime.h"
#include "app/bootstrap/runtime/ui_action_gateway.h"
#include "app/ws/command_bus_protocol.h"
#include "app/ws/scheduler/event_scheduler.h"
#include "ctrl/adapters/control_ws_use_cases.h"
#include "ctrl/application/agent_message_service.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/application/audit_query_service.h"
#include "ctrl/application/command_orchestrator.h"
#include "ctrl/application/params_service.h"
#include "ctrl/application/rate_limiter_service.h"
#include "ctrl/application/redaction_service.h"
#include "ctrl/application/retry_service.h"
#include "ctrl/infrastructure/sqlite_store.h"
#include "ctrl/ports/defaults.h"
#include "app/runtime_log.h"
#include "owt/protocol/v5/contract.h"

#include <drogon/WebSocketConnection.h>
#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <string>

namespace app::bootstrap {

namespace {

using runtime::to_post_result_string;

struct RuntimeState {
  explicit RuntimeState(const Config& cfg)
      : config(cfg),
        store(config.storage.db_path),
        ui_sessions(runtime::UiSessionRegistry::Config{
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
            clock),
        control_ws_use_cases(registry_service, agent_message_service),
        retry_service(store, agent_channel, status_publisher, metrics, clock),
        audit_query_service(store),
        lifecycle(
            runtime::RuntimeLifecycle::Options{
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
            audit_query_service,
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

  Config config;
  ctrl::ports::SystemClock clock;
  ctrl::ports::DefaultIdGenerator id_generator;
  runtime::RuntimeMetrics metrics;
  ctrl::infrastructure::SqliteStore store;
  runtime::UiSubscriptionStore subscriptions;
  runtime::UiSessionRegistry ui_sessions;
  app::ws::scheduler::EventScheduler event_scheduler;
  runtime::AgentSessionRegistry agent_sessions;
  runtime::DrogonAgentChannel agent_channel;
  ctrl::application::AgentRegistryService registry_service;
  runtime::BusStatusPublisher status_publisher;
  ctrl::application::ParamsService params_service;
  ctrl::application::RateLimiterService rate_limiter_service;
  ctrl::application::RedactionService redaction_service;
  ctrl::application::CommandOrchestrator command_orchestrator;
  ctrl::application::AgentMessageService agent_message_service;
  ctrl::adapters::ControlWsUseCases control_ws_use_cases;
  ctrl::application::RetryService retry_service;
  ctrl::application::AuditQueryService audit_query_service;
  runtime::RuntimeLifecycle lifecycle;
  runtime::UiActionGateway ui_gateway;
  runtime::AgentActionGateway agent_gateway;

  std::string trace_id() {
    static std::atomic<uint64_t> seq{0};
    const auto now = clock.now_ms();
    const auto id = seq.fetch_add(1, std::memory_order_relaxed);
    return "trc-" + std::to_string(now) + "-" + std::to_string(id);
  }

  void on_ui_open(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn) {
    const auto session_id = runtime::next_session_id();
    const auto actor_id = runtime::normalize_actor_id(req, session_id);

    conn->setContext(std::make_shared<runtime::ConnectionContext>(runtime::ConnectionContext{session_id, actor_id}));
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

  void on_ui_close(const drogon::WebSocketConnectionPtr& conn) {
    if (!conn || !conn->hasContext()) {
      return;
    }
    const auto ctx = conn->getContext<runtime::ConnectionContext>();
    if (!ctx) {
      return;
    }
    subscriptions.unsubscribe(ctx->session_id);
    ui_sessions.remove_session(ctx->session_id);
  }

  void on_ui_message(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type) {
    if (type != drogon::WebSocketMessageType::Text) {
      return;
    }
    if (!conn || !conn->hasContext()) {
      return;
    }
    const auto ctx = conn->getContext<runtime::ConnectionContext>();
    if (!ctx) {
      return;
    }

    ws::BusEnvelope req;
    std::string parse_error;
    if (!ws::parse_bus_envelope(message, req, parse_error)) {
      const auto err = ws::bus_error(
          "unknown",
          nullptr,
          clock.now_ms(),
          owt::protocol::v5::error_code::kBadEnvelope,
          parse_error);
      conn->send(err, drogon::WebSocketMessageType::Text);
      return;
    }

    if (req.version != owt::protocol::v5::kProtocol) {
      const auto err = ws::bus_error(
          req.name,
          req.id,
          clock.now_ms(),
          owt::protocol::v5::error_code::kUnsupportedVersion,
          "unsupported protocol version",
          nlohmann::json{{"expected", std::string(owt::protocol::v5::kProtocol)}, {"got", req.version}});
      conn->send(err, drogon::WebSocketMessageType::Text);
      return;
    }

    if (req.kind != owt::protocol::v5::kind::kAction) {
      const auto err = ws::bus_error(
          req.name,
          req.id,
          clock.now_ms(),
          owt::protocol::v5::error_code::kBadKind,
          "ui message kind must be action");
      conn->send(err, drogon::WebSocketMessageType::Text);
      return;
    }

    auto partition_key = ctx->session_id;
    if (req.payload.contains("agent_mac") && req.payload["agent_mac"].is_string() &&
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

  void on_agent_open(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn) {
    const auto session_id = runtime::next_session_id();
    const auto actor_id = runtime::normalize_actor_id(req, "owt-agent");
    conn->setContext(std::make_shared<runtime::ConnectionContext>(runtime::ConnectionContext{session_id, actor_id}));
    agent_sessions.add_connection(session_id, conn);
    control_ws_use_cases.on_open(session_id);
  }

  void on_agent_close(const drogon::WebSocketConnectionPtr& conn) {
    if (!conn || !conn->hasContext()) {
      return;
    }
    const auto ctx = conn->getContext<runtime::ConnectionContext>();
    if (!ctx) {
      return;
    }
    control_ws_use_cases.on_close(ctx->session_id);
    (void)agent_sessions.unbind_session(ctx->session_id);
  }

  void on_agent_message(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type) {
    if (type != drogon::WebSocketMessageType::Text) {
      return;
    }
    if (!conn || !conn->hasContext()) {
      return;
    }
    const auto ctx = conn->getContext<runtime::ConnectionContext>();
    if (!ctx) {
      return;
    }

    ws::BusEnvelope req;
    std::string parse_error;
    if (!ws::parse_bus_envelope(message, req, parse_error)) {
      conn->send(
          ws::bus_error(
              owt::protocol::v5::agent::kErrorServerError,
              nullptr,
              clock.now_ms(),
              owt::protocol::v5::error_code::kBadEnvelope,
              parse_error),
          drogon::WebSocketMessageType::Text);
      return;
    }

    if (req.version != owt::protocol::v5::kProtocol) {
      conn->send(
          ws::bus_error(
              req.name,
              req.id,
              clock.now_ms(),
              owt::protocol::v5::error_code::kUnsupportedVersion,
              "unsupported protocol version",
              nlohmann::json{{"expected", std::string(owt::protocol::v5::kProtocol)}, {"got", req.version}}),
          drogon::WebSocketMessageType::Text);
      return;
    }

    if (req.kind != owt::protocol::v5::kind::kAction) {
      conn->send(
          ws::bus_error(
              req.name,
              req.id,
              clock.now_ms(),
              owt::protocol::v5::error_code::kBadKind,
              "agent message kind must be action"),
          drogon::WebSocketMessageType::Text);
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
};

class RuntimeRouter {
public:
  static RuntimeRouter& instance() {
    static RuntimeRouter s;
    return s;
  }

  void bind(RuntimeState* state) {
    std::lock_guard<std::mutex> lk(mutex_);
    state_ = state;
  }

  RuntimeState* get() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
  }

private:
  mutable std::mutex mutex_;
  RuntimeState* state_ = nullptr;
};

class UiWsController final : public drogon::WebSocketController<UiWsController> {
public:
  void handleNewMessage(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_ui_message(conn, std::move(message), type);
  }

  void handleNewConnection(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_ui_open(req, conn);
  }

  void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      return;
    }
    state->on_ui_close(conn);
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws/v5/ui");
  WS_PATH_LIST_END
};

class AgentWsController final : public drogon::WebSocketController<AgentWsController> {
public:
  void handleNewMessage(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_agent_message(conn, std::move(message), type);
  }

  void handleNewConnection(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_agent_open(req, conn);
  }

  void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      return;
    }
    state->on_agent_close(conn);
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws/v5/agent");
  WS_PATH_LIST_END
};

} // namespace

struct RuntimeImpl::State {
  explicit State(const Config& cfg) : runtime(cfg) {}
  RuntimeState runtime;
};

RuntimeImpl::RuntimeImpl(const Config& config) : state_(new State(config)) {}

RuntimeImpl::~RuntimeImpl() {
  if (state_ != nullptr) {
    state_->runtime.lifecycle.stop();
    state_->runtime.event_scheduler.stop();
    state_->runtime.ui_sessions.close_all();
    delete state_;
    state_ = nullptr;
  }
}

int RuntimeImpl::run() {
  if (state_ == nullptr) {
    return EXIT_FAILURE;
  }

  auto& rt = state_->runtime;

  log::init();
  log::info("owt-net start (v5 command-bus / drogon ws-only)");
  log::info(
      "server config: host={}, port={}, threads={}, event_workers={}, event_queue_capacity={}, event_low_drop_pct={}, ui_queue_limit={}, ui_send_timeout_ms={}",
      rt.config.server.host,
      rt.config.server.port,
      rt.config.server.threads,
      rt.config.server.ws_event_workers,
      rt.config.server.ws_event_queue_capacity,
      rt.config.server.ws_event_low_priority_drop_threshold_pct,
      rt.config.server.ui_session_queue_limit,
      rt.config.server.ui_event_send_timeout_ms);

  try {
    rt.store.migrate();
    rt.event_scheduler.start(app::ws::scheduler::EventSchedulerConfig{
        .workers = rt.config.server.ws_event_workers > 0
            ? rt.config.server.ws_event_workers
            : std::max(2, rt.config.server.threads),
        .queue_capacity = static_cast<std::size_t>(std::max(64, rt.config.server.ws_event_queue_capacity)),
        .low_priority_drop_threshold_pct = std::clamp(
            rt.config.server.ws_event_low_priority_drop_threshold_pct,
            1,
            100),
    });

    try {
      rt.retry_service.recover_inflight_on_boot();
    } catch (const std::exception& ex) {
      log::warn("recover inflight on boot failed: {}", ex.what());
    }

    try {
      rt.store.cleanup_retention(rt.config.storage.retention_days, rt.clock.now_ms());
    } catch (const std::exception& ex) {
      log::warn("initial cleanup failed: {}", ex.what());
    }

    rt.lifecycle.start();

    RuntimeRouter::instance().bind(&rt);

    drogon::app().setLogLevel(trantor::Logger::kWarn);
    drogon::app().setThreadNum(std::max(1, rt.config.server.threads));
    drogon::app().addListener(rt.config.server.host, static_cast<uint16_t>(std::clamp(rt.config.server.port, 1, 65535)), false);
    drogon::app().setClientMaxWebSocketMessageSize(10 * 1024 * 1024);
    drogon::app().registerHandler(
        "/*",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
          auto resp = drogon::HttpResponse::newHttpResponse();
          resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
          resp->setBody(nlohmann::json{
                            {"code", "ws_upgrade_required"},
                            {"message", "upgrade to websocket endpoint /ws/v5/ui or /ws/v5/agent"},
                        }.dump());
          resp->setStatusCode(drogon::k426UpgradeRequired);
          callback(resp);
        },
        {drogon::Get, drogon::Post, drogon::Put, drogon::Delete, drogon::Patch, drogon::Options, drogon::Head});

    drogon::app().run();

    RuntimeRouter::instance().bind(nullptr);
    rt.lifecycle.stop();
    rt.event_scheduler.stop();
    rt.ui_sessions.close_all();
    log::info("owt-net exit");
    log::shutdown();
    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    RuntimeRouter::instance().bind(nullptr);
    rt.lifecycle.stop();
    rt.event_scheduler.stop();
    rt.ui_sessions.close_all();
    log::error("owt-net fatal: {}", ex.what());
    log::shutdown();
    return EXIT_FAILURE;
  } catch (...) {
    RuntimeRouter::instance().bind(nullptr);
    rt.lifecycle.stop();
    rt.event_scheduler.stop();
    rt.ui_sessions.close_all();
    log::error("owt-net fatal: unknown exception");
    log::shutdown();
    return EXIT_FAILURE;
  }
}

} // namespace app::bootstrap
