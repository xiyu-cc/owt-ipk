#include "service/rate_limiter.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

namespace service {

namespace {

struct token_bucket_state {
  double tokens = 0.0;
  int64_t last_refill_ms = 0;
  int64_t last_seen_ms = 0;
};

std::mutex g_rate_limit_mutex;
bool g_rate_limit_enabled = true;
int g_rate_limit_rps = 20;
int g_rate_limit_burst = 40;
std::unordered_map<std::string, token_bucket_state> g_buckets;

void prune_stale_buckets_locked(int64_t now_ms) {
  static constexpr int64_t kBucketTtlMs = 10 * 60 * 1000;
  for (auto it = g_buckets.begin(); it != g_buckets.end();) {
    if (now_ms - it->second.last_seen_ms > kBucketTtlMs) {
      it = g_buckets.erase(it);
      continue;
    }
    ++it;
  }
}

} // namespace

void configure_http_rate_limit(bool enabled, int requests_per_second, int burst) {
  std::lock_guard<std::mutex> lock(g_rate_limit_mutex);
  g_rate_limit_enabled = enabled;
  g_rate_limit_rps = std::max(1, requests_per_second);
  g_rate_limit_burst = std::max(g_rate_limit_rps, burst);
  g_buckets.clear();
}

bool is_http_rate_limit_enabled() {
  std::lock_guard<std::mutex> lock(g_rate_limit_mutex);
  return g_rate_limit_enabled;
}

bool allow_http_request(const std::string& key, int64_t now_ms, int64_t& retry_after_ms) {
  std::lock_guard<std::mutex> lock(g_rate_limit_mutex);
  retry_after_ms = 0;
  if (!g_rate_limit_enabled) {
    return true;
  }

  const std::string effective_key = key.empty() ? "unknown" : key;
  auto& bucket = g_buckets[effective_key];
  if (bucket.last_refill_ms <= 0) {
    bucket.tokens = static_cast<double>(g_rate_limit_burst);
    bucket.last_refill_ms = now_ms;
  }

  const int64_t elapsed_ms = std::max<int64_t>(0, now_ms - bucket.last_refill_ms);
  if (elapsed_ms > 0) {
    const double refill = (static_cast<double>(elapsed_ms) * static_cast<double>(g_rate_limit_rps)) / 1000.0;
    bucket.tokens = std::min(static_cast<double>(g_rate_limit_burst), bucket.tokens + refill);
    bucket.last_refill_ms = now_ms;
  }
  bucket.last_seen_ms = now_ms;

  if (bucket.tokens >= 1.0) {
    bucket.tokens -= 1.0;
    prune_stale_buckets_locked(now_ms);
    return true;
  }

  const double missing_tokens = std::max(0.0, 1.0 - bucket.tokens);
  retry_after_ms = static_cast<int64_t>(std::ceil(missing_tokens * 1000.0 / static_cast<double>(g_rate_limit_rps)));
  if (retry_after_ms < 1) {
    retry_after_ms = 1;
  }
  prune_stale_buckets_locked(now_ms);
  return false;
}

} // namespace service
