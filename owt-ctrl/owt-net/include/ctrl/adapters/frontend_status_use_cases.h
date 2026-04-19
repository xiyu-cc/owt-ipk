#pragma once

#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ctrl::adapters {

struct FrontendStatusMessage {
  std::string session_token;
  nlohmann::json payload = nlohmann::json::object();
};

class FrontendStatusUseCases {
public:
  void subscribe_list(std::string_view session_token);
  void subscribe_agent(std::string_view session_token, std::string_view agent_mac);
  void unsubscribe(std::string_view session_token);

  std::vector<FrontendStatusMessage> trigger_snapshot(
      std::string_view reason,
      std::string_view agent_mac,
      const nlohmann::json& snapshot_payload) const;
  std::vector<FrontendStatusMessage> trigger_agent(
      std::string_view reason,
      std::string_view agent_mac,
      const nlohmann::json& agent_payload) const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, bool> list_subscribers_;
  std::unordered_map<std::string, std::string> agent_subscribers_;
};

} // namespace ctrl::adapters
