#include "api/control_audit_get_api.h"

#include "api/api_common.h"
#include "service/command_store.h"
#include "service/sensitive_json.h"
#include "utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace api {

namespace {

nlohmann::json parse_json_or_string(const std::string& text) {
  if (text.empty()) {
    return nullptr;
  }
  auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (parsed.is_discarded()) {
    return service::redact_sensitive_json_text(text);
  }
  return service::redact_sensitive_json(parsed);
}

bool parse_limit_value(const std::string& text, int& value) {
  try {
    const int parsed = std::stoi(text);
    if (parsed <= 0) {
      return false;
    }
    value = std::min(parsed, 500);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace

http_deal::http::message_generator control_audit_get_api::operator()(request_t& req) {
  const auto args = utils::url_argument(std::string(req.target()));

  const std::string action = args.count("action") > 0 ? args.at("action") : "";
  const std::string actor_id = args.count("actor_id") > 0 ? args.at("actor_id") : "";
  const std::string resource_type = args.count("resource_type") > 0 ? args.at("resource_type") : "";
  const std::string resource_id = args.count("resource_id") > 0 ? args.at("resource_id") : "";

  int limit = 50;
  const auto limit_it = args.find("limit");
  if (limit_it != args.end() && !limit_it->second.empty() &&
      !parse_limit_value(limit_it->second, limit)) {
    return reply_error(req, http_deal::http::status::bad_request, "limit must be positive integer");
  }

  std::vector<service::audit_log_record> rows;
  std::string error;
  if (!service::list_audit_logs(action, actor_id, resource_type, resource_id, limit, rows, error)) {
    return reply_error(
        req, http_deal::http::status::internal_server_error, "query audit logs failed: " + error);
  }

  nlohmann::json logs = nlohmann::json::array();
  for (const auto& row : rows) {
    logs.push_back({
        {"id", row.id},
        {"actor_type", row.actor_type},
        {"actor_id", row.actor_id},
        {"action", row.action},
        {"resource_type", row.resource_type},
        {"resource_id", row.resource_id},
        {"summary", parse_json_or_string(row.summary_json)},
        {"created_at_ms", row.created_at_ms},
    });
  }

  nlohmann::json data = {
      {"logs", std::move(logs)},
      {"count", rows.size()},
      {"limit", limit},
      {"filters",
       {
           {"action", action},
           {"actor_id", actor_id},
           {"resource_type", resource_type},
           {"resource_id", resource_id},
       }},
  };
  return reply_ok(req, data);
}

} // namespace api
