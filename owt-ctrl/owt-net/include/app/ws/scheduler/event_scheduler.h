#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "owt/common/partitioned_scheduler.h"

namespace app::ws::scheduler {

enum class EventPriority {
  High,
  Low,
};

enum class PostResult {
  Accepted,
  DroppedLowPriority,
  RejectedHighPriority,
  Stopped,
};

struct EventSchedulerConfig {
  int workers = 2;
  std::size_t queue_capacity = 4096;
  int low_priority_drop_threshold_pct = 80;
};

class EventScheduler {
public:
  using task_t = std::function<void()>;

  EventScheduler();
  ~EventScheduler();

  EventScheduler(const EventScheduler&) = delete;
  EventScheduler& operator=(const EventScheduler&) = delete;

  bool start(const EventSchedulerConfig& config);
  void stop();

  PostResult post(std::string partition_key, EventPriority priority, task_t task);

  bool running() const noexcept;
  std::uint64_t dropped_low_priority() const noexcept;
  std::uint64_t rejected_high_priority() const noexcept;

private:
  owt::common::PartitionedScheduler impl_;
};

} // namespace app::ws::scheduler
