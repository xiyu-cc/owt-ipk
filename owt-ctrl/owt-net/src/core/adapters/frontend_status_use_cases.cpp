#include "ctrl/adapters/frontend_status_use_cases.h"

#include <string>
#include <vector>

namespace ctrl::adapters {

void FrontendStatusUseCases::subscribe_list(std::string_view session_token) {
  if (session_token.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  list_subscribers_[std::string(session_token)] = true;
}

void FrontendStatusUseCases::subscribe_agent(
    std::string_view session_token,
    std::string_view agent_mac) {
  if (session_token.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  agent_subscribers_[std::string(session_token)] = std::string(agent_mac);
}

void FrontendStatusUseCases::unsubscribe(std::string_view session_token) {
  if (session_token.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  list_subscribers_.erase(std::string(session_token));
  agent_subscribers_.erase(std::string(session_token));
}

std::vector<FrontendStatusMessage> FrontendStatusUseCases::trigger_snapshot(
    std::string_view reason,
    std::string_view agent_mac,
    const nlohmann::json& snapshot_payload) const {
  std::vector<FrontendStatusMessage> out;
  std::lock_guard<std::mutex> lk(mutex_);
  out.reserve(list_subscribers_.size());
  for (const auto& [session_token, _] : list_subscribers_) {
    (void)_;
    out.push_back(FrontendStatusMessage{
        session_token,
        nlohmann::json{
            {"type", "STATUS_SNAPSHOT"},
            {"reason", std::string(reason)},
            {"agent_mac", std::string(agent_mac)},
            {"data", snapshot_payload},
        },
    });
  }
  return out;
}

std::vector<FrontendStatusMessage> FrontendStatusUseCases::trigger_agent(
    std::string_view reason,
    std::string_view agent_mac,
    const nlohmann::json& agent_payload) const {
  std::vector<FrontendStatusMessage> out;
  std::lock_guard<std::mutex> lk(mutex_);
  for (const auto& [session_token, observed_agent] : agent_subscribers_) {
    if (observed_agent != agent_mac) {
      continue;
    }
    out.push_back(FrontendStatusMessage{
        session_token,
        nlohmann::json{
            {"type", "AGENT_STATUS_UPDATE"},
            {"reason", std::string(reason)},
            {"agent_mac", std::string(agent_mac)},
            {"data", agent_payload},
        },
    });
  }
  return out;
}

} // namespace ctrl::adapters
