#include "internal.h"

#include "detail/runtime/log.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace app::bootstrap::runtime_internal {

void RuntimeImplState::start_workers() {
  std::lock_guard<std::mutex> lk(worker_mutex);
  if (retry_worker.joinable() || cleanup_worker.joinable()) {
    return;
  }
  worker_stop = false;

  retry_worker = std::thread([this] {
    const auto tick = std::chrono::milliseconds(std::max(50, config.server.retry_tick_ms));
    for (;;) {
      std::unique_lock<std::mutex> lk(worker_mutex);
      if (worker_cv.wait_for(lk, tick, [this] { return worker_stop; })) {
        return;
      }
      lk.unlock();

      try {
        retry_service.tick_once(std::max(1, config.server.retry_batch));
      } catch (const std::exception& ex) {
        log::warn("retry worker tick failed: {}", ex.what());
      }
    }
  });

  cleanup_worker = std::thread([this] {
    const auto interval = std::chrono::seconds(std::max(60, config.storage.cleanup_interval_sec));
    for (;;) {
      std::unique_lock<std::mutex> lk(worker_mutex);
      if (worker_cv.wait_for(lk, interval, [this] { return worker_stop; })) {
        return;
      }
      lk.unlock();

      try {
        store.cleanup_retention(config.storage.retention_days, clock.now_ms());
      } catch (const std::exception& ex) {
        log::warn("cleanup worker tick failed: {}", ex.what());
      }
    }
  });
}

void RuntimeImplState::stop_workers() {
  {
    std::lock_guard<std::mutex> lk(worker_mutex);
    worker_stop = true;
  }
  worker_cv.notify_all();
  if (retry_worker.joinable()) {
    retry_worker.join();
  }
  if (cleanup_worker.joinable()) {
    cleanup_worker.join();
  }
}

} // namespace app::bootstrap::runtime_internal
