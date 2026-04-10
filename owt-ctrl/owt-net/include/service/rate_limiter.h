#pragma once

#include <cstdint>
#include <string>

namespace service {

void configure_http_rate_limit(bool enabled, int requests_per_second, int burst);
bool is_http_rate_limit_enabled();
bool allow_http_request(const std::string& key, int64_t now_ms, int64_t& retry_after_ms);

} // namespace service
