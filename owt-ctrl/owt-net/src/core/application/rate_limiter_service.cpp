#include "ctrl/application/rate_limiter_service.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ctrl::application {

void RateLimiterService::configure(bool enabled, int rps, int burst) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
  rps_ = std::max(1, rps);
  burst_ = std::max(rps_, burst);
  buckets_.clear();
}

bool RateLimiterService::allow(std::string_view key, int64_t now_ms, int64_t& retry_after_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  retry_after_ms = 0;
  if (!enabled_) {
    return true;
  }

  std::string effective_key = std::string(key);
  if (effective_key.empty()) {
    effective_key = "unknown";
  }
  auto& bucket = buckets_[effective_key];
  if (bucket.last_refill_ms <= 0) {
    bucket.tokens = static_cast<double>(burst_);
    bucket.last_refill_ms = now_ms;
  }

  const int64_t elapsed_ms = std::max<int64_t>(0, now_ms - bucket.last_refill_ms);
  if (elapsed_ms > 0) {
    const double refill = (static_cast<double>(elapsed_ms) * static_cast<double>(rps_)) / 1000.0;
    bucket.tokens = std::min(static_cast<double>(burst_), bucket.tokens + refill);
    bucket.last_refill_ms = now_ms;
  }
  bucket.last_seen_ms = now_ms;

  if (bucket.tokens >= 1.0) {
    bucket.tokens -= 1.0;
    prune_stale_locked(now_ms);
    return true;
  }

  const double missing_tokens = std::max(0.0, 1.0 - bucket.tokens);
  retry_after_ms =
      static_cast<int64_t>(std::ceil(missing_tokens * 1000.0 / static_cast<double>(rps_)));
  if (retry_after_ms < 1) {
    retry_after_ms = 1;
  }
  prune_stale_locked(now_ms);
  return false;
}

void RateLimiterService::prune_stale_locked(int64_t now_ms) {
  static constexpr int64_t kBucketTtlMs = 10 * 60 * 1000;
  for (auto it = buckets_.begin(); it != buckets_.end();) {
    if (now_ms - it->second.last_seen_ms > kBucketTtlMs) {
      it = buckets_.erase(it);
      continue;
    }
    ++it;
  }
}

} // namespace ctrl::application
