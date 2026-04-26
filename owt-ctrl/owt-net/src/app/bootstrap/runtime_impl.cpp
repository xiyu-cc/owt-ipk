#include "app/bootstrap/runtime_impl.h"

#include "app/bootstrap/runtime/runtime_composition.h"
#include "app/bootstrap/runtime/ws_endpoints.h"
#include "app/runtime_log.h"
#include "owt/protocol/v5/contract.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

namespace app::bootstrap {

struct RuntimeImpl::State {
  explicit State(const Config& cfg) : runtime(cfg) {}
  runtime::RuntimeComposition runtime;
};

RuntimeImpl::RuntimeImpl(const Config& config)
    : state_(std::make_unique<State>(config)) {}

RuntimeImpl::~RuntimeImpl() {
  if (state_) {
    state_->runtime.lifecycle.stop();
    state_->runtime.event_scheduler.stop();
    state_->runtime.ui_sessions.close_all();
  }
}

int RuntimeImpl::run() {
  if (!state_) {
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

  const auto shutdown_runtime = [&rt]() {
    runtime::bind_runtime_composition(nullptr);
    rt.lifecycle.stop();
    rt.event_scheduler.stop();
    rt.ui_sessions.close_all();
  };

  try {
    rt.store.migrate();

    std::string offline_error;
    if (!rt.store.mark_all_offline(rt.clock.now_ms(), offline_error)) {
      log::warn("mark all agents offline on startup failed: {}", offline_error);
    }

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

    runtime::bind_runtime_composition(&rt);

    drogon::app().setLogLevel(trantor::Logger::kWarn);
    drogon::app().setThreadNum(std::max(1, rt.config.server.threads));
    drogon::app().addListener(
        rt.config.server.host,
        static_cast<uint16_t>(std::clamp(rt.config.server.port, 1, 65535)),
        false);
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

    shutdown_runtime();
    log::info("owt-net exit");
    log::shutdown();
    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    shutdown_runtime();
    log::error("owt-net fatal: {}", ex.what());
    log::shutdown();
    return EXIT_FAILURE;
  } catch (...) {
    shutdown_runtime();
    log::error("owt-net fatal: unknown exception");
    log::shutdown();
    return EXIT_FAILURE;
  }
}

} // namespace app::bootstrap
