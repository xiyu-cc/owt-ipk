#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ctrl::application {

class RateLimiterService {
public:
  void configure(bool enabled, int rps, int burst);
  bool allow(std::string_view key, int64_t now_ms, int64_t& retry_after_ms);

private:
  struct TokenBucket {
    double tokens = 0.0;
    int64_t last_refill_ms = 0;
    int64_t last_seen_ms = 0;
  };

  void prune_stale_locked(int64_t now_ms);

  mutable std::mutex mutex_;
  bool enabled_ = true;
  int rps_ = 20;
  int burst_ = 40;
  std::unordered_map<std::string, TokenBucket> buckets_;
};

} // namespace ctrl::application
