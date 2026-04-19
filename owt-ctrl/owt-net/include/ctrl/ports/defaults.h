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

class NullStatusPublisher final : public IStatusPublisher {
public:
  void publish_snapshot(std::string_view reason, std::string_view agent_mac) override;
  void publish_agent(std::string_view reason, std::string_view agent_mac) override;
};

class NullMetrics final : public IMetrics {
public:
  void record_http_request() override;
  void record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) override;
  void record_command_push() override;
  void record_command_retry(
      std::string_view command_id,
      int retry_count,
      std::string_view reason) override;
  void record_command_retry_exhausted(
      std::string_view command_id,
      std::string_view reason) override;
  void record_command_terminal_status(
      std::string_view command_id,
      domain::CommandState state,
      const nlohmann::json& detail) override;
};

} // namespace ctrl::ports
