#pragma once

#include "ctrl/ports/interfaces.h"

#include <atomic>

namespace ctrl::ports {

class SystemClock final : public IClock {
public:
  int64_t now_ms() const override;
};

class DefaultIdGenerator final : public IIdGenerator {
public:
  std::string next_command_id() override;
  std::string next_trace_id(std::string_view command_id) override;

private:
  std::atomic<uint64_t> sequence_{0};
};

} // namespace ctrl::ports
