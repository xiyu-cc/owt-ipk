#include "api/control_command_push_api.h"

#include "api/api_common.h"
#include "control/control_protocol.h"
#include "log.h"
#include "service/command_store.h"
#include "service/control_hub.h"
#include "service/observability.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

namespace api {

namespace {

std::atomic<uint64_t> g_command_counter{0};

std::string make_command_id() {
  const auto ts = static_cast<uint64_t>(control::unix_time_ms_now());
  const auto seq = g_command_counter.fetch_add(1, std::memory_order_relaxed);
  return "cmd-" + std::to_string(ts) + "-" + std::to_string(seq);
}

nlohmann::json make_default_params_payload() {
  return {
      {"wol",
       {
           {"mac", "AA:BB:CC:DD:EE:FF"},
           {"broadcast", "192.168.1.255"},
           {"port", 9},
       }},
      {"ssh",
       {
           {"host", "192.168.1.10"},
           {"port", 22},
           {"user", "root"},
           {"password", "password"},
           {"timeout_ms", 5000},
       }},
  };
}

bool load_or_init_params(const std::string& agent_id, nlohmann::json& out, std::string& error) {
  service::agent_params_record row;
  if (service::get_agent_params(agent_id, row, error)) {
    auto parsed = nlohmann::json::parse(row.params_json, nullptr, false);
    if (parsed.is_object()) {
      out = std::move(parsed);
      return true;
    }
  } else if (error != "agent params not found") {
    return false;
  }

  out = make_default_params_payload();
  error.clear();
  if (!service::upsert_agent_params(agent_id, out.dump(), control::unix_time_ms_now(), error)) {
    return false;
  }
  return true;
}

bool update_string_field(
    nlohmann::json& target,
    const nlohmann::json& patch,
    const char* key,
    std::string& error) {
  if (!patch.contains(key)) {
    return true;
  }
  if (!patch[key].is_string()) {
    error = std::string("field ") + key + " must be string";
    return false;
  }
  target[key] = patch[key].get<std::string>();
  return true;
}

bool update_int_field(
    nlohmann::json& target,
    const nlohmann::json& patch,
    const char* key,
    int min_value,
    int max_value,
    std::string& error) {
  if (!patch.contains(key)) {
    return true;
  }
  if (!patch[key].is_number_integer()) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }
  const int value = patch[key].get<int>();
  if (value < min_value || value > max_value) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  target[key] = value;
  return true;
}

bool merge_params_patch(
    nlohmann::json& current,
    const nlohmann::json& patch,
    std::string& error) {
  if (!patch.is_object()) {
    error = "params payload must be object";
    return false;
  }
  if (!current.is_object()) {
    current = make_default_params_payload();
  }

  const auto defaults = make_default_params_payload();
  if (!current.contains("wol") || !current["wol"].is_object()) {
    current["wol"] = defaults["wol"];
  }
  if (!current.contains("ssh") || !current["ssh"].is_object()) {
    current["ssh"] = defaults["ssh"];
  }

  if (patch.contains("wol")) {
    if (!patch["wol"].is_object()) {
      error = "field wol must be object";
      return false;
    }
    auto& wol = current["wol"];
    const auto& wol_patch = patch["wol"];
    if (!update_string_field(wol, wol_patch, "mac", error) ||
        !update_string_field(wol, wol_patch, "broadcast", error) ||
        !update_int_field(wol, wol_patch, "port", 1, 65535, error)) {
      return false;
    }
  }

  if (patch.contains("ssh")) {
    if (!patch["ssh"].is_object()) {
      error = "field ssh must be object";
      return false;
    }
    auto& ssh = current["ssh"];
    const auto& ssh_patch = patch["ssh"];
    if (!update_string_field(ssh, ssh_patch, "host", error) ||
        !update_int_field(ssh, ssh_patch, "port", 1, 65535, error) ||
        !update_string_field(ssh, ssh_patch, "user", error) ||
        !update_string_field(ssh, ssh_patch, "password", error) ||
        !update_int_field(ssh, ssh_patch, "timeout_ms", 100, INT32_MAX, error)) {
      return false;
    }
  }

  return true;
}

bool persist_local_terminal_command(
    const std::string& agent_id,
    const control::command& command,
    const std::string& result_json,
    std::string& error) {
  service::command_record row;
  row.command_id = command.command_id;
  row.agent_id = agent_id;
  row.idempotency_key = command.idempotency_key;
  row.command_type = control::to_string(command.type);
  row.status = "SUCCEEDED";
  row.payload_json = command.payload_json;
  row.result_json = result_json;
  row.issued_at_ms = command.issued_at_ms;
  row.expires_at_ms = command.expires_at_ms;
  row.timeout_ms = command.timeout_ms;
  row.max_retry = command.max_retry;
  row.retry_count = 0;
  row.next_retry_at_ms = 0;
  row.last_error = "";
  row.created_at_ms = command.issued_at_ms;
  row.updated_at_ms = command.issued_at_ms;

  if (!service::upsert_command(row, error)) {
    return false;
  }

  nlohmann::json detail = {
      {"event", "COMMAND_RESULT_LOCAL"},
      {"source", "owt-net"},
      {"command_type", control::to_string(command.type)},
  };
  error.clear();
  if (!service::append_command_event(
          command.command_id,
          "COMMAND_RESULT_LOCAL",
          "SUCCEEDED",
          detail.dump(),
          command.issued_at_ms,
          error)) {
    return false;
  }

  return true;
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
  command.timeout_ms = std::max(100, body.value("timeout_ms", 5000));
  command.max_retry = std::max(0, body.value("max_retry", 1));
  const auto expire_after_ms = std::max<int64_t>(1000, body.value("expire_after_ms", 60000));
  command.expires_at_ms = body.value("expires_at_ms", command.issued_at_ms + expire_after_ms);

  if (body.contains("payload_json") && body["payload_json"].is_string()) {
    command.payload_json = body["payload_json"].get<std::string>();
  } else if (body.contains("payload")) {
    command.payload_json = body["payload"].dump();
  } else {
    command.payload_json = nlohmann::json::object().dump();
  }

  std::string error;
  if (command.type == control::command_type::params_get) {
    nlohmann::json stored_params;
    if (!load_or_init_params(agent_id, stored_params, error)) {
      return reply_error(req, http_deal::http::status::internal_server_error, error);
    }
    if (!persist_local_terminal_command(agent_id, command, stored_params.dump(), error)) {
      return reply_error(req, http_deal::http::status::internal_server_error, error);
    }
  } else {
    if (command.type == control::command_type::params_set) {
      const auto params_patch = nlohmann::json::parse(command.payload_json, nullptr, false);
      if (!params_patch.is_object()) {
        return reply_error(req, http_deal::http::status::bad_request, "PARAMS_SET payload must be object");
      }

      nlohmann::json merged_params;
      if (!load_or_init_params(agent_id, merged_params, error)) {
        return reply_error(req, http_deal::http::status::internal_server_error, error);
      }
      error.clear();
      if (!merge_params_patch(merged_params, params_patch, error)) {
        return reply_error(req, http_deal::http::status::bad_request, error);
      }

      error.clear();
      if (!service::upsert_agent_params(agent_id, merged_params.dump(), control::unix_time_ms_now(), error)) {
        return reply_error(req, http_deal::http::status::internal_server_error, error);
      }

      command.payload_json = merged_params.dump();
      command.max_retry = std::max(1, command.max_retry);
    }

    if (!service::push_command_to_agent(agent_id, command, error)) {
      return reply_error(req, http_deal::http::status::service_unavailable, error);
    }
    service::record_command_push();
  }

  nlohmann::json audit_summary = {
      {"agent_id", agent_id},
      {"command_id", command.command_id},
      {"command_type", control::to_string(command.type)},
  };
  std::string actor_id = "unknown";
  const auto xff_it = req.find("X-Forwarded-For");
  if (xff_it != req.end() && !xff_it->value().empty()) {
    actor_id = std::string(xff_it->value());
  }
  error.clear();
  if (!service::append_audit_log(
          "network",
          actor_id,
          "CONTROL_COMMAND_PUSH",
          "command",
          command.command_id,
          audit_summary.dump(),
          command.issued_at_ms,
          error)) {
    log::warn("persist audit log failed: command_id={}, err={}", command.command_id, error);
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
      {"queued", command.type != control::command_type::params_get},
  };
  return reply_ok(req, data);
}

} // namespace api
