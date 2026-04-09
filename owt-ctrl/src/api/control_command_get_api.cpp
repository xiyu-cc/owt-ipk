#include "api/control_command_get_api.h"

#include "api/api_common.h"
#include "service/command_store.h"
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
    return text;
  }
  return parsed;
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

http_deal::http::message_generator control_command_get_api::operator()(request_t& req) {
  const auto args = utils::url_argument(std::string(req.target()));
  const auto it = args.find("command_id");
  if (it == args.end() || it->second.empty()) {
    return reply_error(req, http_deal::http::status::bad_request, "command_id is required");
  }

  int event_limit = 100;
  const auto limit_it = args.find("event_limit");
  if (limit_it != args.end() && !limit_it->second.empty() &&
      !parse_limit_value(limit_it->second, event_limit)) {
    return reply_error(req, http_deal::http::status::bad_request, "event_limit must be positive integer");
  }
  const auto legacy_limit_it = args.find("limit");
  if (legacy_limit_it != args.end() && !legacy_limit_it->second.empty() &&
      !parse_limit_value(legacy_limit_it->second, event_limit)) {
    return reply_error(req, http_deal::http::status::bad_request, "limit must be positive integer");
  }

  service::command_record command;
  std::string error;
  if (!service::get_command(it->second, command, error)) {
    if (error == "command not found") {
      return reply_error(req, http_deal::http::status::not_found, error);
    }
    return reply_error(
        req, http_deal::http::status::internal_server_error, "query command failed: " + error);
  }

  std::vector<service::command_event_record> events;
  error.clear();
  if (!service::list_command_events(command.command_id, event_limit, events, error)) {
    return reply_error(
        req, http_deal::http::status::internal_server_error, "query command events failed: " + error);
  }

  nlohmann::json events_json = nlohmann::json::array();
  for (const auto& event : events) {
    events_json.push_back({
        {"id", event.id},
        {"command_id", event.command_id},
        {"event_type", event.event_type},
        {"status", event.status},
        {"channel_type", event.channel_type},
        {"detail", parse_json_or_string(event.detail_json)},
        {"created_at_ms", event.created_at_ms},
    });
  }

  nlohmann::json data = {
      {"command",
       {
           {"command_id", command.command_id},
           {"idempotency_key", command.idempotency_key},
           {"command_type", command.command_type},
           {"status", command.status},
           {"channel_type", command.channel_type},
           {"payload", parse_json_or_string(command.payload_json)},
           {"result", parse_json_or_string(command.result_json)},
           {"created_at_ms", command.created_at_ms},
           {"updated_at_ms", command.updated_at_ms},
       }},
      {"events", std::move(events_json)},
      {"event_limit", event_limit},
  };
  return reply_ok(req, data);
}

} // namespace api
