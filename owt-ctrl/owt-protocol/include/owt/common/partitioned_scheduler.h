#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace owt::common {

enum class PartitionTaskPriority {
  High,
  Low,
};

enum class PartitionedSchedulerPostResult {
  Accepted,
  DroppedLowPriority,
  RejectedHighPriority,
  Stopped,
};

struct PartitionedSchedulerConfig {
  int workers = 2;
  std::size_t queue_capacity = 4096;
  int low_priority_drop_threshold_pct = 80;
};

class PartitionedScheduler {
public:
  using task_t = std::function<void()>;
  using task_error_handler_t = std::function<void(std::string_view)>;

  PartitionedScheduler() = default;
  ~PartitionedScheduler() {
    stop();
  }

  PartitionedScheduler(const PartitionedScheduler&) = delete;
  PartitionedScheduler& operator=(const PartitionedScheduler&) = delete;

  bool start(
      const PartitionedSchedulerConfig& input,
      task_error_handler_t error_handler = {}) {
    if (running_.load(std::memory_order_acquire)) {
      return true;
    }

    PartitionedSchedulerConfig cfg = input;
    cfg.workers = std::max(1, cfg.workers);
    cfg.queue_capacity = std::max<std::size_t>(64, cfg.queue_capacity);
    cfg.low_priority_drop_threshold_pct = std::clamp(cfg.low_priority_drop_threshold_pct, 1, 100);

    config_ = cfg;
    error_handler_ = std::move(error_handler);
    dropped_low_priority_.store(0, std::memory_order_release);
    rejected_high_priority_.store(0, std::memory_order_release);

    workers_.clear();
    workers_.reserve(static_cast<std::size_t>(config_.workers));
    for (int i = 0; i < config_.workers; ++i) {
      workers_.push_back(std::make_unique<WorkerState>());
    }

    running_.store(true, std::memory_order_release);
    for (auto& worker : workers_) {
      worker->thread = std::thread([this, raw = worker.get()] {
        worker_loop(*raw);
      });
    }
    return true;
  }

  void stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    for (auto& worker : workers_) {
      {
        std::lock_guard<std::mutex> lk(worker->mutex);
        worker->stop = true;
      }
      worker->cv.notify_all();
    }

    for (auto& worker : workers_) {
      if (worker->thread.joinable()) {
        worker->thread.join();
      }
    }
    workers_.clear();
  }

  PartitionedSchedulerPostResult post(
      std::string partition_key,
      PartitionTaskPriority priority,
      task_t task) {
    if (!task) {
      return PartitionedSchedulerPostResult::RejectedHighPriority;
    }
    if (!running_.load(std::memory_order_acquire) || workers_.empty()) {
      return PartitionedSchedulerPostResult::Stopped;
    }

    if (partition_key.empty()) {
      partition_key = "default";
    }
    auto& worker = *workers_[pick_worker_index(partition_key)];

    std::unique_lock<std::mutex> lk(worker.mutex);
    const auto size = worker.queue.size();
    const auto capacity = config_.queue_capacity;
    if (size >= capacity) {
      if (priority == PartitionTaskPriority::Low) {
        dropped_low_priority_.fetch_add(1, std::memory_order_relaxed);
        return PartitionedSchedulerPostResult::DroppedLowPriority;
      }
      rejected_high_priority_.fetch_add(1, std::memory_order_relaxed);
      return PartitionedSchedulerPostResult::RejectedHighPriority;
    }

    if (priority == PartitionTaskPriority::Low) {
      const auto occupancy_pct = static_cast<int>((size * 100) / capacity);
      if (occupancy_pct >= config_.low_priority_drop_threshold_pct) {
        dropped_low_priority_.fetch_add(1, std::memory_order_relaxed);
        return PartitionedSchedulerPostResult::DroppedLowPriority;
      }
    }

    worker.queue.push_back(std::move(task));
    lk.unlock();
    worker.cv.notify_one();
    return PartitionedSchedulerPostResult::Accepted;
  }

  bool running() const noexcept {
    return running_.load(std::memory_order_acquire);
  }

  std::uint64_t dropped_low_priority() const noexcept {
    return dropped_low_priority_.load(std::memory_order_acquire);
  }

  std::uint64_t rejected_high_priority() const noexcept {
    return rejected_high_priority_.load(std::memory_order_acquire);
  }

  PartitionedSchedulerConfig config() const noexcept {
    return config_;
  }

private:
  struct WorkerState {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<task_t> queue;
    std::thread thread;
    bool stop = false;
  };

  std::size_t pick_worker_index(const std::string& partition_key) const {
    return std::hash<std::string>{}(partition_key) % workers_.size();
  }

  void worker_loop(WorkerState& worker) noexcept {
    while (true) {
      task_t task;
      {
        std::unique_lock<std::mutex> lk(worker.mutex);
        worker.cv.wait(lk, [&worker] { return worker.stop || !worker.queue.empty(); });
        if (worker.stop && worker.queue.empty()) {
          return;
        }
        task = std::move(worker.queue.front());
        worker.queue.pop_front();
      }

      try {
        task();
      } catch (const std::exception& ex) {
        if (error_handler_) {
          error_handler_(ex.what());
        }
      } catch (...) {
        if (error_handler_) {
          error_handler_("unknown exception");
        }
      }
    }
  }

  std::vector<std::unique_ptr<WorkerState>> workers_;
  PartitionedSchedulerConfig config_{};
  task_error_handler_t error_handler_{};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_low_priority_{0};
  std::atomic<std::uint64_t> rejected_high_priority_{0};
};

} // namespace owt::common
