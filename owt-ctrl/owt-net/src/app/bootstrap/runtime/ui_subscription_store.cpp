#include "internal.h"

namespace app::bootstrap::runtime_internal {

void UiSubscriptionStore::subscribe_all(std::string_view session_id) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  rows_[std::string(session_id)] = Subscription{Scope::All, ""};
}

void UiSubscriptionStore::subscribe_agent(std::string_view session_id, std::string_view agent_mac) {
  if (session_id.empty() || agent_mac.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  rows_[std::string(session_id)] = Subscription{Scope::Agent, std::string(agent_mac)};
}

void UiSubscriptionStore::unsubscribe(std::string_view session_id) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  rows_.erase(std::string(session_id));
}

std::vector<std::pair<std::string, UiSubscriptionStore::Subscription>> UiSubscriptionStore::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  std::vector<std::pair<std::string, Subscription>> out;
  out.reserve(rows_.size());
  for (const auto& [session_id, sub] : rows_) {
    out.emplace_back(session_id, sub);
  }
  return out;
}

} // namespace app::bootstrap::runtime_internal
