#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace app::bootstrap::runtime {

class RuntimeLifecycle {
public:
  struct Options {
    int retry_tick_ms = 250;
    int retry_batch = 100;
    int cleanup_interval_sec = 3600;
    int retention_days = 30;
  };

  using TickRetryFn = std::function<void(int batch)>;
  using CleanupFn = std::function<void(int retention_days, int64_t now_ms)>;
  using NowFn = std::function<int64_t()>;
  using LogFn = std::function<void(const char* worker, const char* message)>;

  RuntimeLifecycle(Options options, TickRetryFn tick_retry, CleanupFn cleanup, NowFn now, LogFn log_warn);
  ~RuntimeLifecycle();

  RuntimeLifecycle(const RuntimeLifecycle&) = delete;
  RuntimeLifecycle& operator=(const RuntimeLifecycle&) = delete;

  void start();
  void stop();

private:
  void run_retry_loop();
  void run_cleanup_loop();

  Options options_;
  TickRetryFn tick_retry_;
  CleanupFn cleanup_;
  NowFn now_;
  LogFn log_warn_;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_requested_ = false;
  std::thread retry_thread_;
  std::thread cleanup_thread_;
};

} // namespace app::bootstrap::runtime
