#include "control/runtime_event_dispatcher.h"

#include "log.h"

namespace control {

runtime_event_dispatcher::runtime_event_dispatcher() = default;

runtime_event_dispatcher::~runtime_event_dispatcher() {
  stop();
}

bool runtime_event_dispatcher::start(const runtime_event_dispatcher_options& options) {
  const owt::common::PartitionedSchedulerConfig mapped{
      .workers = options.workers,
      .queue_capacity = options.queue_capacity,
      .low_priority_drop_threshold_pct = options.low_priority_drop_threshold_pct,
  };
  const auto ok = impl_.start(mapped, [](std::string_view reason) {
    log::warn("runtime event task failed: {}", reason);
  });
  if (!ok) {
    return false;
  }

  const auto applied = impl_.config();
  log::info(
      "runtime event dispatcher started: workers={}, queue_capacity={}, low_priority_drop_threshold_pct={}",
      applied.workers,
      applied.queue_capacity,
      applied.low_priority_drop_threshold_pct);
  return true;
}

void runtime_event_dispatcher::stop() {
  if (!impl_.running()) {
    return;
  }
  impl_.stop();
  log::info(
      "runtime event dispatcher stopped: dropped_low_priority={}, rejected_high_priority={}",
      impl_.dropped_low_priority(),
      impl_.rejected_high_priority());
}

runtime_event_post_result runtime_event_dispatcher::post(
    std::string partition_key,
    runtime_event_priority priority,
    task_t task) {
  const auto mapped_priority = priority == runtime_event_priority::low
      ? owt::common::PartitionTaskPriority::Low
      : owt::common::PartitionTaskPriority::High;
  const auto result = impl_.post(std::move(partition_key), mapped_priority, std::move(task));
  switch (result) {
    case owt::common::PartitionedSchedulerPostResult::Accepted:
      return runtime_event_post_result::accepted;
    case owt::common::PartitionedSchedulerPostResult::DroppedLowPriority:
      return runtime_event_post_result::dropped_low_priority;
    case owt::common::PartitionedSchedulerPostResult::RejectedHighPriority:
      return runtime_event_post_result::rejected_high_priority;
    case owt::common::PartitionedSchedulerPostResult::Stopped:
      return runtime_event_post_result::stopped;
  }
  return runtime_event_post_result::rejected_high_priority;
}

bool runtime_event_dispatcher::running() const noexcept {
  return impl_.running();
}

std::uint64_t runtime_event_dispatcher::dropped_low_priority() const noexcept {
  return impl_.dropped_low_priority();
}

std::uint64_t runtime_event_dispatcher::rejected_high_priority() const noexcept {
  return impl_.rejected_high_priority();
}

} // namespace control
