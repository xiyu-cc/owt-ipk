#include "app/bootstrap/runtime/lifecycle.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace app::bootstrap::runtime {

RuntimeLifecycle::RuntimeLifecycle(
    Options options,
    TickRetryFn tick_retry,
    CleanupFn cleanup,
    NowFn now,
    LogFn log_warn)
    : options_(options),
      tick_retry_(std::move(tick_retry)),
      cleanup_(std::move(cleanup)),
      now_(std::move(now)),
      log_warn_(std::move(log_warn)) {}

RuntimeLifecycle::~RuntimeLifecycle() {
  stop();
}

void RuntimeLifecycle::start() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (retry_thread_.joinable() || cleanup_thread_.joinable()) {
    return;
  }
  stop_requested_ = false;
  retry_thread_ = std::thread([this] { run_retry_loop(); });
  cleanup_thread_ = std::thread([this] { run_cleanup_loop(); });
}

void RuntimeLifecycle::stop() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (retry_thread_.joinable()) {
    retry_thread_.join();
  }
  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }
}

void RuntimeLifecycle::run_retry_loop() {
  const auto tick = std::chrono::milliseconds(std::max(50, options_.retry_tick_ms));
  for (;;) {
    std::unique_lock<std::mutex> lk(mutex_);
    if (cv_.wait_for(lk, tick, [this] { return stop_requested_; })) {
      return;
    }
    lk.unlock();

    try {
      tick_retry_(std::max(1, options_.retry_batch));
    } catch (const std::exception&) {
      if (log_warn_) {
        log_warn_("retry", "tick failed");
      }
    } catch (...) {
      if (log_warn_) {
        log_warn_("retry", "tick failed (unknown)");
      }
    }
  }
}

void RuntimeLifecycle::run_cleanup_loop() {
  const auto interval = std::chrono::seconds(std::max(60, options_.cleanup_interval_sec));
  for (;;) {
    std::unique_lock<std::mutex> lk(mutex_);
    if (cv_.wait_for(lk, interval, [this] { return stop_requested_; })) {
      return;
    }
    lk.unlock();

    try {
      cleanup_(options_.retention_days, now_());
    } catch (const std::exception&) {
      if (log_warn_) {
        log_warn_("cleanup", "tick failed");
      }
    } catch (...) {
      if (log_warn_) {
        log_warn_("cleanup", "tick failed (unknown)");
      }
    }
  }
}

} // namespace app::bootstrap::runtime
