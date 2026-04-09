#include "api/control_command_push_api.h"

#include "api/api_common.h"
#include "control/control_protocol.h"
#include "service/control_hub.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <string>

namespace api {

namespace {

std::atomic<uint64_t> g_command_counter{0};

std::string make_command_id() {
  const auto ts = static_cast<uint64_t>(control::unix_time_ms_now());
  const auto seq = g_command_counter.fetch_add(1, std::memory_order_relaxed);
  return "cmd-" + std::to_string(ts) + "-" + std::to_string(seq);
}

} // namespace

http_deal::http::message_generator control_command_push_api::operator()(request_t& req) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(body_as_string(req));
  } catch (const std::exception&) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid json body");
  }

  if (!body.is_object()) {
    return reply_error(req, http_deal::http::status::bad_request, "json body must be object");
  }

  const auto agent_id = body.value("agent_id", std::string{});
  const auto command_type_text = body.value("command_type", std::string{});
  if (agent_id.empty() || command_type_text.empty()) {
    return reply_error(
        req, http_deal::http::status::bad_request, "agent_id and command_type are required");
  }

  control::command_type command_type{};
  if (!control::try_parse_command_type(command_type_text, command_type)) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid command_type");
  }

  control::command command;
  command.command_id = body.value("command_id", make_command_id());
  command.idempotency_key = body.value("idempotency_key", command.command_id);
  command.type = command_type;
  command.issued_at_ms = control::unix_time_ms_now();
  command.timeout_ms = body.value("timeout_ms", 5000);
  command.max_retry = body.value("max_retry", 0);
  const auto expire_after_ms = body.value("expire_after_ms", 60000);
  command.expires_at_ms = body.value("expires_at_ms", command.issued_at_ms + expire_after_ms);

  if (body.contains("payload_json") && body["payload_json"].is_string()) {
    command.payload_json = body["payload_json"].get<std::string>();
  } else if (body.contains("payload")) {
    command.payload_json = body["payload"].dump();
  } else {
    command.payload_json = nlohmann::json::object().dump();
  }

  std::string error;
  if (!service::push_command_to_agent(agent_id, command, error)) {
    return reply_error(req, http_deal::http::status::service_unavailable, error);
  }

  nlohmann::json data = {
      {"agent_id", agent_id},
      {"command_id", command.command_id},
      {"idempotency_key", command.idempotency_key},
      {"command_type", control::to_string(command.type)},
      {"issued_at_ms", command.issued_at_ms},
      {"expires_at_ms", command.expires_at_ms},
      {"timeout_ms", command.timeout_ms},
      {"max_retry", command.max_retry},
      {"queued", true},
  };
  return reply_ok(req, data);
}

} // namespace api

