#include "api/control_agent_get_api.h"

#include "api/api_common.h"
#include "service/control_hub.h"
#include "utils.h"

#include <nlohmann/json.hpp>

#include <string>

namespace api {

namespace {

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

http_deal::http::message_generator control_agent_get_api::operator()(request_t& req) {
  const auto args = utils::url_argument(std::string(req.target()));
  const auto it = args.find("agent_id");
  if (it == args.end() || it->second.empty()) {
    return reply_error(req, http_deal::http::status::bad_request, "agent_id is required");
  }

  service::agent_runtime_state state;
  if (!service::get_agent_runtime_state(it->second, state)) {
    return reply_error(req, http_deal::http::status::not_found, "agent not found");
  }

  nlohmann::json data = {
      {"agent_id", state.agent_id},
      {"site_id", state.site_id},
      {"agent_version", state.agent_version},
      {"capabilities", state.capabilities},
      {"online", state.online},
      {"registered_at_ms", state.registered_at_ms},
      {"last_heartbeat_at_ms", state.last_heartbeat_at_ms},
      {"last_seen_at_ms", state.last_seen_at_ms},
      {"stats", parse_json_or_string(state.stats_json)},
  };
  return reply_ok(req, data);
}

} // namespace api
