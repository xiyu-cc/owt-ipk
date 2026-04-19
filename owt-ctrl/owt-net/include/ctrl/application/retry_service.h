#pragma once

#include "ctrl/ports/interfaces.h"

#include <nlohmann/json.hpp>

namespace ctrl::application {

class RetryService {
public:
  RetryService(
      ports::ICommandRepository& commands,
      ports::IAgentChannel& channel,
      ports::IStatusPublisher& publisher,
      ports::IMetrics& metrics,
      const ports::IClock& clock);

  void tick_once(int limit = 100);
  void recover_inflight_on_boot();

private:
  static int64_t compute_retry_delay_ms(int retry_count);
  void process_retry_command(const domain::CommandSnapshot& row, int64_t now_ms);
  domain::CommandEvent append_event(
      std::string_view command_id,
      std::string_view event_type,
      domain::CommandState state,
      const nlohmann::json& detail,
      int64_t created_at_ms);

  ports::ICommandRepository& commands_;
  ports::IAgentChannel& channel_;
  ports::IStatusPublisher& publisher_;
  ports::IMetrics& metrics_;
  const ports::IClock& clock_;
};

} // namespace ctrl::application
