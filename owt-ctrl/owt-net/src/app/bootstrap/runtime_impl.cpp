#include "app/bootstrap/runtime_impl.h"

#include "runtime/internal.h"
#include "detail/runtime/log.h"
#include "detail/transport/controller.h"

#include <cstdlib>
#include <exception>
#include <memory>

namespace app::bootstrap {

struct RuntimeImpl::State : runtime_internal::RuntimeImplState {
  explicit State(const Config& config) : runtime_internal::RuntimeImplState(config) {}
};

RuntimeImpl::RuntimeImpl(const Config& config) : state_(new State(config)) {}

RuntimeImpl::~RuntimeImpl() {
  if (state_ != nullptr) {
    state_->stop_workers();
    delete state_;
    state_ = nullptr;
  }
}

int RuntimeImpl::run() {
  if (state_ == nullptr) {
    return EXIT_FAILURE;
  }

  log::init();
  log::info("owt-net start (v4 ws-only)");
  log::info(
      "server config: host={}, port={}, threads={}",
      state_->config.server.host,
      state_->config.server.port,
      state_->config.server.threads);
  log::info(
      "storage config: db_path={}, retention_days={}, cleanup_interval_sec={}",
      state_->config.storage.db_path,
      state_->config.storage.retention_days,
      state_->config.storage.cleanup_interval_sec);

  try {
    state_->store.migrate();

    state_->ws_dispatcher = std::make_shared<ws_deal::handler_dispatcher>();
    state_->ws_dispatcher->install<runtime_internal::AgentWsHandler>(
        std::string(runtime_internal::kWsAgentRoute),
        *state_);
    state_->ws_dispatcher->install<runtime_internal::UiRpcWsHandler>(
        std::string(runtime_internal::kWsUiRoute),
        *state_);

    try {
      state_->retry_service.recover_inflight_on_boot();
    } catch (const std::exception& ex) {
      log::warn("recover inflight on boot failed: {}", ex.what());
    }

    try {
      state_->store.cleanup_retention(state_->config.storage.retention_days, state_->clock.now_ms());
    } catch (const std::exception& ex) {
      log::warn("initial cleanup failed: {}", ex.what());
    }

    state_->start_workers();

    const bool ok = transport::controller::init(
        state_->config.server.host,
        state_->config.server.port,
        state_->config.server.threads,
        state_->ws_dispatcher);

    state_->stop_workers();
    if (state_->ws_dispatcher) {
      state_->ws_dispatcher->reset();
      state_->ws_dispatcher.reset();
    }
    log::info("owt-net exit");
    log::shutdown();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception& ex) {
    state_->stop_workers();
    if (state_->ws_dispatcher) {
      state_->ws_dispatcher->reset();
      state_->ws_dispatcher.reset();
    }
    log::error("owt-net fatal: {}", ex.what());
    log::shutdown();
    return EXIT_FAILURE;
  } catch (...) {
    state_->stop_workers();
    if (state_->ws_dispatcher) {
      state_->ws_dispatcher->reset();
      state_->ws_dispatcher.reset();
    }
    log::error("owt-net fatal: unknown exception");
    log::shutdown();
    return EXIT_FAILURE;
  }
}

} // namespace app::bootstrap
