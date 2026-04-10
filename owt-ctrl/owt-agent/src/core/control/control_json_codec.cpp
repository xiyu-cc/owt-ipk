#include "control/control_json_codec.h"

#include <nlohmann/json.hpp>

namespace control {

namespace {

using json = nlohmann::json;

template <typename T>
T get_or(const json& j, const char* key, const T& fallback) {
  if (!j.is_object() || !j.contains(key)) {
    return fallback;
  }
  try {
    return j.at(key).get<T>();
  } catch (...) {
    return fallback;
  }
}

json command_to_json(const command& c) {
  return {
      {"command_id", c.command_id},
      {"idempotency_key", c.idempotency_key},
      {"command_type", to_string(c.type)},
      {"issued_at_ms", c.issued_at_ms},
      {"expires_at_ms", c.expires_at_ms},
      {"timeout_ms", c.timeout_ms},
      {"max_retry", c.max_retry},
      {"payload_json", c.payload_json},
  };
}

bool command_from_json(const json& j, command& out) {
  if (!j.is_object()) {
    return false;
  }
  if (!j.contains("command_id") || !j["command_id"].is_string() ||
      !j.contains("idempotency_key") || !j["idempotency_key"].is_string() ||
      !j.contains("command_type") || !j["command_type"].is_string() ||
      !j.contains("issued_at_ms") || !j["issued_at_ms"].is_number_integer() ||
      !j.contains("expires_at_ms") || !j["expires_at_ms"].is_number_integer() ||
      !j.contains("timeout_ms") || !j["timeout_ms"].is_number_integer() ||
      !j.contains("max_retry") || !j["max_retry"].is_number_integer() ||
      !j.contains("payload_json") || !j["payload_json"].is_string()) {
    return false;
  }

  out.command_id = j["command_id"].get<std::string>();
  out.idempotency_key = j["idempotency_key"].get<std::string>();
  out.issued_at_ms = j["issued_at_ms"].get<int64_t>();
  out.expires_at_ms = j["expires_at_ms"].get<int64_t>();
  out.timeout_ms = j["timeout_ms"].get<int>();
  out.max_retry = j["max_retry"].get<int>();
  out.payload_json = j["payload_json"].get<std::string>();
  const auto type_text = j["command_type"].get<std::string>();
  if (!try_parse_command_type(type_text, out.type)) {
    return false;
  }
  return !out.command_id.empty() && !out.idempotency_key.empty();
}

json payload_to_json(message_type type, const payload_variant& payload) {
  switch (type) {
    case message_type::register_agent: {
      if (const auto* p = std::get_if<register_payload>(&payload)) {
        return {
            {"agent_id", p->agent_id},
            {"site_id", p->site_id},
            {"agent_version", p->agent_version},
            {"capabilities", p->capabilities},
        };
      }
      return json::object();
    }
    case message_type::register_ack: {
      if (const auto* p = std::get_if<register_ack_payload>(&payload)) {
        return {{"ok", p->ok}, {"message", p->message}};
      }
      return json::object();
    }
    case message_type::heartbeat: {
      if (const auto* p = std::get_if<heartbeat_payload>(&payload)) {
        return {{"heartbeat_at_ms", p->heartbeat_at_ms}, {"stats_json", p->stats_json}};
      }
      return json::object();
    }
    case message_type::heartbeat_ack: {
      if (const auto* p = std::get_if<heartbeat_ack_payload>(&payload)) {
        return {{"server_time_ms", p->server_time_ms}};
      }
      return json::object();
    }
    case message_type::command_push: {
      if (const auto* p = std::get_if<command>(&payload)) {
        return {{"command", command_to_json(*p)}};
      }
      return json::object();
    }
    case message_type::command_ack: {
      if (const auto* p = std::get_if<command_ack_payload>(&payload)) {
        return {
            {"command_id", p->command_id},
            {"status", to_string(p->status)},
            {"message", p->message},
        };
      }
      return json::object();
    }
    case message_type::command_result: {
      if (const auto* p = std::get_if<command_result_payload>(&payload)) {
        return {
            {"command_id", p->command_id},
            {"final_status", to_string(p->final_status)},
            {"exit_code", p->exit_code},
            {"result_json", p->result_json},
        };
      }
      return json::object();
    }
    case message_type::error: {
      if (const auto* p = std::get_if<error_payload>(&payload)) {
        return {
            {"code", p->code},
            {"message", p->message},
            {"detail", p->detail},
        };
      }
      return json::object();
    }
  }
  return json::object();
}

payload_variant payload_from_json(message_type type, const json& payload, bool& ok) {
  ok = true;
  switch (type) {
    case message_type::register_agent: {
      register_payload p;
      p.agent_id = get_or<std::string>(payload, "agent_id", "");
      p.site_id = get_or<std::string>(payload, "site_id", "");
      p.agent_version = get_or<std::string>(payload, "agent_version", "");
      if (payload.contains("capabilities") && payload["capabilities"].is_array()) {
        try {
          p.capabilities = payload["capabilities"].get<std::vector<std::string>>();
        } catch (...) {
          p.capabilities.clear();
        }
      }
      return p;
    }
    case message_type::register_ack: {
      register_ack_payload p;
      p.ok = get_or<bool>(payload, "ok", false);
      p.message = get_or<std::string>(payload, "message", "");
      return p;
    }
    case message_type::heartbeat: {
      heartbeat_payload p;
      p.heartbeat_at_ms = get_or<int64_t>(payload, "heartbeat_at_ms", 0);
      p.stats_json = get_or<std::string>(payload, "stats_json", "");
      return p;
    }
    case message_type::heartbeat_ack: {
      heartbeat_ack_payload p;
      p.server_time_ms = get_or<int64_t>(payload, "server_time_ms", 0);
      return p;
    }
    case message_type::command_push: {
      if (!payload.is_object() || !payload.contains("command") || !payload["command"].is_object()) {
        ok = false;
        return std::monostate{};
      }
      command c;
      const auto& command_json = payload["command"];
      if (!command_from_json(command_json, c)) {
        ok = false;
        return std::monostate{};
      }
      return c;
    }
    case message_type::command_ack: {
      command_ack_payload p;
      p.command_id = get_or<std::string>(payload, "command_id", "");
      p.message = get_or<std::string>(payload, "message", "");
      command_status status = command_status::acked;
      const auto status_text = get_or<std::string>(payload, "status", "");
      if (!try_parse_command_status(status_text, status)) {
        ok = false;
        return std::monostate{};
      }
      if (p.command_id.empty()) {
        ok = false;
        return std::monostate{};
      }
      p.status = status;
      return p;
    }
    case message_type::command_result: {
      if (!payload.contains("exit_code") || !payload["exit_code"].is_number_integer()) {
        ok = false;
        return std::monostate{};
      }
      command_result_payload p;
      p.command_id = get_or<std::string>(payload, "command_id", "");
      p.exit_code = payload["exit_code"].get<int>();
      p.result_json = get_or<std::string>(payload, "result_json", "");
      command_status status = command_status::failed;
      const auto status_text = get_or<std::string>(payload, "final_status", "");
      if (!try_parse_command_status(status_text, status)) {
        ok = false;
        return std::monostate{};
      }
      if (p.command_id.empty()) {
        ok = false;
        return std::monostate{};
      }
      p.final_status = status;
      return p;
    }
    case message_type::error: {
      error_payload p;
      p.code = get_or<std::string>(payload, "code", "");
      p.message = get_or<std::string>(payload, "message", "");
      p.detail = get_or<std::string>(payload, "detail", "");
      return p;
    }
  }
  return std::monostate{};
}

} // namespace

std::string encode_envelope_json(const envelope& value) {
  json j = {
      {"message_id", value.message_id},
      {"message_type", to_string(value.type)},
      {"protocol_version", value.protocol_version},
      {"sent_at_ms", value.sent_at_ms},
      {"trace_id", value.trace_id},
      {"agent_id", value.agent_id},
      {"payload", payload_to_json(value.type, value.payload)},
  };
  return j.dump();
}

bool decode_envelope_json(const std::string& text, envelope& out, std::string& error) {
  json j;
  try {
    j = json::parse(text);
  } catch (const std::exception& e) {
    error = std::string("parse json failed: ") + e.what();
    return false;
  }
  if (!j.is_object()) {
    error = "json root is not object";
    return false;
  }

  if (!j.contains("message_id") || !j["message_id"].is_string()) {
    error = "message_id is required";
    return false;
  }
  out.message_id = j["message_id"].get<std::string>();
  if (out.message_id.empty()) {
    error = "message_id is required";
    return false;
  }
  if (!j.contains("protocol_version") || !j["protocol_version"].is_string()) {
    error = "protocol_version is required";
    return false;
  }
  out.protocol_version = j["protocol_version"].get<std::string>();
  if (!j.contains("sent_at_ms") || !j["sent_at_ms"].is_number_integer()) {
    error = "sent_at_ms is required";
    return false;
  }
  out.sent_at_ms = j["sent_at_ms"].get<int64_t>();
  out.trace_id = get_or<std::string>(j, "trace_id", "");
  out.agent_id = get_or<std::string>(j, "agent_id", "");

  if (!j.contains("message_type") || !j["message_type"].is_string()) {
    error = "message_type is required";
    return false;
  }
  const auto type_text = j["message_type"].get<std::string>();
  if (!try_parse_message_type(type_text, out.type)) {
    error = "invalid message_type";
    return false;
  }

  bool payload_ok = true;
  if (!j.contains("payload") || !j["payload"].is_object()) {
    error = "payload is required";
    return false;
  }
  out.payload = payload_from_json(out.type, j["payload"], payload_ok);
  if (!payload_ok) {
    error = "invalid payload";
    return false;
  }
  return true;
}

} // namespace control
