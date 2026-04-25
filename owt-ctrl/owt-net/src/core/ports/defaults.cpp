#include "ctrl/ports/defaults.h"

#include <chrono>
#include <string>

namespace ctrl::ports {

int64_t SystemClock::now_ms() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string DefaultIdGenerator::next_command_id() {
  const auto now = static_cast<uint64_t>(SystemClock().now_ms());
  const auto seq = sequence_.fetch_add(1, std::memory_order_relaxed);
  return "cmd-" + std::to_string(now) + "-" + std::to_string(seq);
}

std::string DefaultIdGenerator::next_trace_id(std::string_view command_id) {
  if (command_id.empty()) {
    return "trc-" + next_command_id();
  }
  return "trc-" + std::string(command_id);
}

} // namespace ctrl::ports
