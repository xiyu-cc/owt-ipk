#include "app/ws/scheduler/event_scheduler.h"

#include "app/runtime_log.h"

namespace app::ws::scheduler {

EventScheduler::EventScheduler() = default;

EventScheduler::~EventScheduler() {
  stop();
}

bool EventScheduler::start(const EventSchedulerConfig& config) {
  const owt::common::PartitionedSchedulerConfig mapped{
      .workers = config.workers,
      .queue_capacity = config.queue_capacity,
      .low_priority_drop_threshold_pct = config.low_priority_drop_threshold_pct,
  };

  const auto ok = impl_.start(mapped, [](std::string_view reason) {
    log::warn("ws scheduler task failed: {}", reason);
  });
  if (!ok) {
    return false;
  }

  const auto applied = impl_.config();
  log::info(
      "ws event scheduler started: workers={}, queue_capacity={}, low_priority_drop_threshold_pct={}",
      applied.workers,
      applied.queue_capacity,
      applied.low_priority_drop_threshold_pct);
  return true;
}

void EventScheduler::stop() {
  if (!impl_.running()) {
    return;
  }
  impl_.stop();
  log::info(
      "ws event scheduler stopped: dropped_low_priority={}, rejected_high_priority={}",
      impl_.dropped_low_priority(),
      impl_.rejected_high_priority());
}

PostResult EventScheduler::post(std::string partition_key, EventPriority priority, task_t task) {
  const auto mapped_priority = priority == EventPriority::Low
      ? owt::common::PartitionTaskPriority::Low
      : owt::common::PartitionTaskPriority::High;
  const auto result = impl_.post(std::move(partition_key), mapped_priority, std::move(task));
  switch (result) {
    case owt::common::PartitionedSchedulerPostResult::Accepted:
      return PostResult::Accepted;
    case owt::common::PartitionedSchedulerPostResult::DroppedLowPriority:
      return PostResult::DroppedLowPriority;
    case owt::common::PartitionedSchedulerPostResult::RejectedHighPriority:
      return PostResult::RejectedHighPriority;
    case owt::common::PartitionedSchedulerPostResult::Stopped:
      return PostResult::Stopped;
  }
  return PostResult::RejectedHighPriority;
}

bool EventScheduler::running() const noexcept {
  return impl_.running();
}

std::uint64_t EventScheduler::dropped_low_priority() const noexcept {
  return impl_.dropped_low_priority();
}

std::uint64_t EventScheduler::rejected_high_priority() const noexcept {
  return impl_.rejected_high_priority();
}

} // namespace app::ws::scheduler
