#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace service {

struct metrics_snapshot {
  uint64_t http_requests_total = 0;
  uint64_t auth_failed_total = 0;
  uint64_t rate_limited_total = 0;
  uint64_t command_push_total = 0;
  uint64_t command_retry_total = 0;
  uint64_t command_retry_exhausted_total = 0;
  uint64_t command_succeeded_total = 0;
  uint64_t command_failed_total = 0;
  uint64_t command_timed_out_total = 0;
};

struct alert_record {
  int64_t id = 0;
  std::string level;
  std::string alert_type;
  std::string message;
  std::string detail_json;
  int64_t created_at_ms = 0;
};

void record_http_request();
void record_auth_failed(const std::string& actor_id);
void record_rate_limited(const std::string& actor_id, int64_t retry_after_ms);
void record_command_push();
void record_command_retry(const std::string& command_id, int retry_count, const std::string& reason);
void record_command_retry_exhausted(const std::string& command_id, const std::string& reason);
void record_command_terminal_status(
    const std::string& command_id,
    const std::string& status,
    const std::string& detail_json);

void append_alert(
    const std::string& level,
    const std::string& alert_type,
    const std::string& message,
    const std::string& detail_json = "");

metrics_snapshot snapshot_metrics();
void list_alerts(std::vector<alert_record>& out, int limit);

} // namespace service
