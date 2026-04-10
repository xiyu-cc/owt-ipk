#include "api/control_commands_get_api.h"

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

http_deal::http::message_generator control_commands_get_api::operator()(request_t& req) {
  const auto args = utils::url_argument(std::string(req.target()));

  const std::string agent_id = args.count("agent_id") > 0 ? args.at("agent_id") : "";
  const std::string status = args.count("status") > 0 ? args.at("status") : "";
  const std::string command_type = args.count("command_type") > 0 ? args.at("command_type") : "";

  int limit = 50;
  const auto limit_it = args.find("limit");
  if (limit_it != args.end() && !limit_it->second.empty() &&
      !parse_limit_value(limit_it->second, limit)) {
    return reply_error(req, http_deal::http::status::bad_request, "limit must be positive integer");
  }

  std::vector<service::command_record> rows;
  std::string error;
  if (!service::list_commands(agent_id, status, command_type, limit, rows, error)) {
    return reply_error(
        req, http_deal::http::status::internal_server_error, "query commands failed: " + error);
  }

  nlohmann::json commands = nlohmann::json::array();
  for (const auto& row : rows) {
    commands.push_back({
        {"command_id", row.command_id},
        {"agent_id", row.agent_id},
        {"idempotency_key", row.idempotency_key},
        {"command_type", row.command_type},
        {"status", row.status},
        {"payload", parse_json_or_string(row.payload_json)},
        {"result", parse_json_or_string(row.result_json)},
        {"created_at_ms", row.created_at_ms},
        {"updated_at_ms", row.updated_at_ms},
    });
  }

  nlohmann::json data = {
      {"commands", std::move(commands)},
      {"count", rows.size()},
      {"limit", limit},
      {"filters",
       {
           {"agent_id", agent_id},
           {"status", status},
           {"command_type", command_type},
       }},
  };
  return reply_ok(req, data);
}

} // namespace api
