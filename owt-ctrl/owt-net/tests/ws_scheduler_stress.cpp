#include "app/ws/scheduler/event_scheduler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main() {
  app::ws::scheduler::EventScheduler scheduler;
  app::ws::scheduler::EventSchedulerConfig config;
  config.workers = 8;
  config.queue_capacity = 20000;
  config.low_priority_drop_threshold_pct = 85;
  if (!scheduler.start(config)) {
    std::cerr << "failed to start scheduler\n";
    return 1;
  }

  constexpr int kAgents = 128;
  constexpr int kEventsPerAgent = 300;
  constexpr int kTotal = kAgents * kEventsPerAgent;

  std::vector<int> last_seen(static_cast<std::size_t>(kAgents), -1);
  std::atomic<int> completed{0};
  std::atomic<int> post_rejected{0};
  std::atomic<int> post_dropped{0};
  std::atomic<int> order_errors{0};

  std::mutex done_mutex;
  std::condition_variable done_cv;

  for (int agent = 0; agent < kAgents; ++agent) {
    const auto key = std::string("agent-") + std::to_string(agent);
    for (int seq = 0; seq < kEventsPerAgent; ++seq) {
      const auto result = scheduler.post(
          key,
          app::ws::scheduler::EventPriority::High,
          [&last_seen, &completed, &done_cv, &order_errors, &done_mutex, agent, seq] {
            const auto idx = static_cast<std::size_t>(agent);
            const int expect = last_seen[idx] + 1;
            if (expect != seq) {
              order_errors.fetch_add(1, std::memory_order_relaxed);
            }
            last_seen[idx] = seq;

            const int done = completed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (done == kTotal) {
              std::lock_guard<std::mutex> lk(done_mutex);
              done_cv.notify_all();
            }
          });
      if (result == app::ws::scheduler::PostResult::RejectedHighPriority) {
        post_rejected.fetch_add(1, std::memory_order_relaxed);
      } else if (result == app::ws::scheduler::PostResult::DroppedLowPriority) {
        post_dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  {
    std::unique_lock<std::mutex> lk(done_mutex);
    done_cv.wait_for(lk, std::chrono::seconds(20), [&completed] {
      return completed.load(std::memory_order_relaxed) == kTotal;
    });
  }

  scheduler.stop();

  const int done = completed.load(std::memory_order_relaxed);
  const int rejected = post_rejected.load(std::memory_order_relaxed);
  const int dropped = post_dropped.load(std::memory_order_relaxed);
  const int errors = order_errors.load(std::memory_order_relaxed);
  std::cout << "ws scheduler stress done=" << done
            << " total=" << kTotal
            << " rejected=" << rejected
            << " dropped=" << dropped
            << " order_errors=" << errors << '\n';

  if (done != kTotal || rejected != 0 || dropped != 0 || errors != 0) {
    return 1;
  }
  return 0;
}
