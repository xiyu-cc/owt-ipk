#include "api/control_agents_get_api.h"

#include "api/api_common.h"
#include "service/control_hub.h"
#include "utils.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace api {

namespace {

bool parse_include_offline(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  return text == "1" || text == "true" || text == "yes" || text == "on";
}

nlohmann::json parse_json_or_string(const std::string& text) {
  if (text.empty()) {
    return nullptr;
  }
  auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (parsed.is_discarded()) {
    return text;
  }
  return parsed;
}

} // namespace

http_deal::http::message_generator control_agents_get_api::operator()(request_t& req) {
  const auto args = utils::url_argument(std::string(req.target()));
  const bool include_offline = parse_include_offline(
      args.count("include_offline") > 0 ? args.at("include_offline") : "");

  std::vector<service::agent_runtime_state> states;
  service::list_agent_runtime_states(states, include_offline);

  nlohmann::json agents = nlohmann::json::array();
  std::size_t online_count = 0;
  for (const auto& state : states) {
    if (state.online) {
      ++online_count;
    }
    agents.push_back({
        {"agent_id", state.agent_id},
        {"site_id", state.site_id},
        {"agent_version", state.agent_version},
        {"capabilities", state.capabilities},
        {"online", state.online},
        {"registered_at_ms", state.registered_at_ms},
        {"last_heartbeat_at_ms", state.last_heartbeat_at_ms},
        {"last_seen_at_ms", state.last_seen_at_ms},
        {"stats", parse_json_or_string(state.stats_json)},
    });
  }

  nlohmann::json data = {
      {"agents", std::move(agents)},
      {"online_count", online_count},
      {"total_count", states.size()},
      {"include_offline", include_offline},
  };
  return reply_ok(req, data);
}

} // namespace api
