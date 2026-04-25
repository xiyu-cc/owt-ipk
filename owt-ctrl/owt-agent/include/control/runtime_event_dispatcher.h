#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "owt/common/partitioned_scheduler.h"

namespace control {

enum class runtime_event_priority {
  high,
  low,
};

enum class runtime_event_post_result {
  accepted,
  dropped_low_priority,
  rejected_high_priority,
  stopped,
};

struct runtime_event_dispatcher_options {
  int workers = 2;
  std::size_t queue_capacity = 4096;
  int low_priority_drop_threshold_pct = 80;
};

class runtime_event_dispatcher {
public:
  using task_t = std::function<void()>;

  runtime_event_dispatcher();
  ~runtime_event_dispatcher();

  runtime_event_dispatcher(const runtime_event_dispatcher&) = delete;
  runtime_event_dispatcher& operator=(const runtime_event_dispatcher&) = delete;

  bool start(const runtime_event_dispatcher_options& options);
  void stop();

  runtime_event_post_result post(
      std::string partition_key,
      runtime_event_priority priority,
      task_t task);

  bool running() const noexcept;
  std::uint64_t dropped_low_priority() const noexcept;
  std::uint64_t rejected_high_priority() const noexcept;

private:
  owt::common::PartitionedScheduler impl_;
};

} // namespace control
