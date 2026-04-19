#include "control/control_json_codec.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <set>
#include <string>
#include <string_view>

namespace control {

namespace {

using json = nlohmann::json;

bool reject_unknown_fields(
    const json& obj,
    std::initializer_list<std::string_view> allowed,
    std::string& unknown_key) {
  if (!obj.is_object()) {
    unknown_key.clear();
    return false;
  }

  const std::set<std::string_view> allowed_set(allowed.begin(), allowed.end());
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (allowed_set.find(it.key()) == allowed_set.end()) {
      unknown_key = it.key();
      return false;
    }
  }
  unknown_key.clear();
  return true;
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
      {"payload", c.payload},
  };
}

bool command_from_json(const json& j, command& out, std::string& error) {
  std::string unknown;
  if (!reject_unknown_fields(
          j,
          {"command_id", "idempotency_key", "command_type", "issued_at_ms", "expires_at_ms", "timeout_ms", "max_retry", "payload"},
          unknown)) {
    error = "unknown field in command: " + unknown;
    return false;
  }

  if (!j.contains("command_id") || !j["command_id"].is_string() ||
      !j.contains("idempotency_key") || !j["idempotency_key"].is_string() ||
      !j.contains("command_type") || !j["command_type"].is_string() ||
      !j.contains("issued_at_ms") || !j["issued_at_ms"].is_number_integer() ||
      !j.contains("expires_at_ms") || !j["expires_at_ms"].is_number_integer() ||
      !j.contains("timeout_ms") || !j["timeout_ms"].is_number_integer() ||
      !j.contains("max_retry") || !j["max_retry"].is_number_integer() ||
      !j.contains("payload")) {
    error = "invalid command payload";
    return false;
  }

  out.command_id = j["command_id"].get<std::string>();
  out.idempotency_key = j["idempotency_key"].get<std::string>();
  out.issued_at_ms = j["issued_at_ms"].get<int64_t>();
  out.expires_at_ms = j["expires_at_ms"].get<int64_t>();
  out.timeout_ms = j["timeout_ms"].get<int>();
  out.max_retry = j["max_retry"].get<int>();
  out.payload = j["payload"];

  const auto type_text = j["command_type"].get<std::string>();
  if (!try_parse_command_type(type_text, out.type)) {
    error = "invalid command_type";
    return false;
  }
  if (out.command_id.empty() || out.idempotency_key.empty()) {
    error = "command_id and idempotency_key are required";
    return false;
  }

  error.clear();
  return true;
}

json payload_to_json(message_type type, const payload_variant& payload) {
  switch (type) {
    case message_type::register_agent: {
      if (const auto* p = std::get_if<register_payload>(&payload)) {
        return {
            {"agent_mac", p->agent_mac},
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
        return {{"heartbeat_at_ms", p->heartbeat_at_ms}, {"stats", p->stats}};
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
            {"result", p->result},
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

payload_variant payload_from_json(message_type type, const json& payload, bool& ok, std::string& error) {
  ok = true;
  std::string unknown;

  switch (type) {
    case message_type::register_agent: {
      if (!reject_unknown_fields(
              payload,
              {"agent_mac", "agent_id", "site_id", "agent_version", "capabilities", "stats"},
              unknown)) {
        ok = false;
        error = "unknown field in register payload: " + unknown;
        return std::monostate{};
      }
      register_payload p;
      if (!payload.contains("agent_mac") || !payload["agent_mac"].is_string()) {
        ok = false;
        error = "register.agent_mac is required";
        return std::monostate{};
      }
      p.agent_mac = payload["agent_mac"].get<std::string>();
      p.agent_id = payload.value("agent_id", std::string{});
      p.site_id = payload.value("site_id", std::string{});
      p.agent_version = payload.value("agent_version", std::string{});
      if (payload.contains("capabilities")) {
        if (!payload["capabilities"].is_array()) {
          ok = false;
          error = "register.capabilities must be array";
          return std::monostate{};
        }
        for (const auto& item : payload["capabilities"]) {
          if (!item.is_string()) {
            ok = false;
            error = "register.capabilities item must be string";
            return std::monostate{};
          }
          p.capabilities.push_back(item.get<std::string>());
        }
      }
      return p;
    }
    case message_type::register_ack: {
      if (!reject_unknown_fields(payload, {"ok", "message"}, unknown)) {
        ok = false;
        error = "unknown field in register_ack payload: " + unknown;
        return std::monostate{};
      }
      register_ack_payload p;
      if (payload.contains("ok") && !payload["ok"].is_boolean()) {
        ok = false;
        error = "register_ack.ok must be boolean";
        return std::monostate{};
      }
      if (payload.contains("message") && !payload["message"].is_string()) {
        ok = false;
        error = "register_ack.message must be string";
        return std::monostate{};
      }
      p.ok = payload.value("ok", false);
      p.message = payload.value("message", std::string{});
      return p;
    }
    case message_type::heartbeat: {
      if (!reject_unknown_fields(payload, {"heartbeat_at_ms", "stats"}, unknown)) {
        ok = false;
        error = "unknown field in heartbeat payload: " + unknown;
        return std::monostate{};
      }
      heartbeat_payload p;
      if (!payload.contains("heartbeat_at_ms") || !payload["heartbeat_at_ms"].is_number_integer()) {
        ok = false;
        error = "heartbeat.heartbeat_at_ms is required";
        return std::monostate{};
      }
      p.heartbeat_at_ms = payload["heartbeat_at_ms"].get<int64_t>();
      if (payload.contains("stats")) {
        if (!payload["stats"].is_object()) {
          ok = false;
          error = "heartbeat.stats must be object";
          return std::monostate{};
        }
        p.stats = payload["stats"];
      }
      return p;
    }
    case message_type::heartbeat_ack: {
      if (!reject_unknown_fields(payload, {"server_time_ms"}, unknown)) {
        ok = false;
        error = "unknown field in heartbeat_ack payload: " + unknown;
        return std::monostate{};
      }
      heartbeat_ack_payload p;
      if (payload.contains("server_time_ms") && !payload["server_time_ms"].is_number_integer()) {
        ok = false;
        error = "heartbeat_ack.server_time_ms must be integer";
        return std::monostate{};
      }
      p.server_time_ms = payload.value("server_time_ms", static_cast<int64_t>(0));
      return p;
    }
    case message_type::command_push: {
      if (!reject_unknown_fields(payload, {"command"}, unknown)) {
        ok = false;
        error = "unknown field in command_push payload: " + unknown;
        return std::monostate{};
      }
      if (!payload.contains("command") || !payload["command"].is_object()) {
        ok = false;
        error = "command_push.command is required";
        return std::monostate{};
      }
      command c;
      if (!command_from_json(payload["command"], c, error)) {
        ok = false;
        return std::monostate{};
      }
      return c;
    }
    case message_type::command_ack: {
      if (!reject_unknown_fields(payload, {"command_id", "status", "message"}, unknown)) {
        ok = false;
        error = "unknown field in command_ack payload: " + unknown;
        return std::monostate{};
      }
      if (!payload.contains("command_id") || !payload["command_id"].is_string() ||
          !payload.contains("status") || !payload["status"].is_string()) {
        ok = false;
        error = "command_ack.command_id and status are required";
        return std::monostate{};
      }
      if (payload.contains("message") && !payload["message"].is_string()) {
        ok = false;
        error = "command_ack.message must be string";
        return std::monostate{};
      }
      command_ack_payload p;
      p.command_id = payload["command_id"].get<std::string>();
      p.message = payload.value("message", std::string{});
      if (!try_parse_command_status(payload["status"].get<std::string>(), p.status)) {
        ok = false;
        error = "invalid command_ack.status";
        return std::monostate{};
      }
      return p;
    }
    case message_type::command_result: {
      if (!reject_unknown_fields(payload, {"command_id", "final_status", "exit_code", "result"}, unknown)) {
        ok = false;
        error = "unknown field in command_result payload: " + unknown;
        return std::monostate{};
      }
      if (!payload.contains("command_id") || !payload["command_id"].is_string() ||
          !payload.contains("final_status") || !payload["final_status"].is_string() ||
          !payload.contains("exit_code") || !payload["exit_code"].is_number_integer()) {
        ok = false;
        error = "command_result.command_id/final_status/exit_code are required";
        return std::monostate{};
      }
      command_result_payload p;
      p.command_id = payload["command_id"].get<std::string>();
      p.exit_code = payload["exit_code"].get<int>();
      p.result = payload.value("result", json::object());
      if (!try_parse_command_status(payload["final_status"].get<std::string>(), p.final_status)) {
        ok = false;
        error = "invalid command_result.final_status";
        return std::monostate{};
      }
      return p;
    }
    case message_type::error: {
      if (!reject_unknown_fields(payload, {"code", "message", "detail"}, unknown)) {
        ok = false;
        error = "unknown field in error payload: " + unknown;
        return std::monostate{};
      }
      error_payload p;
      if (payload.contains("code") && !payload["code"].is_string()) {
        ok = false;
        error = "error.code must be string";
        return std::monostate{};
      }
      if (payload.contains("message") && !payload["message"].is_string()) {
        ok = false;
        error = "error.message must be string";
        return std::monostate{};
      }
      if (payload.contains("detail") && !payload["detail"].is_string()) {
        ok = false;
        error = "error.detail must be string";
        return std::monostate{};
      }
      p.code = payload.value("code", std::string{});
      p.message = payload.value("message", std::string{});
      p.detail = payload.value("detail", std::string{});
      return p;
    }
  }

  ok = false;
  error = "unsupported message type";
  return std::monostate{};
}

} // namespace

std::string encode_envelope_json(const envelope& value) {
  json j = {
      {"type", to_string(value.type)},
      {"meta",
       {
           {"version", value.protocol_version},
           {"ts_ms", value.sent_at_ms},
           {"trace_id", value.trace_id},
           {"agent_id", value.agent_id},
       }},
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

  std::string unknown;
  if (!reject_unknown_fields(j, {"type", "meta", "payload"}, unknown)) {
    error = "unknown field in envelope: " + unknown;
    return false;
  }

  if (!j.contains("type") || !j["type"].is_string()) {
    error = "type is required";
    return false;
  }
  if (!j.contains("meta") || !j["meta"].is_object()) {
    error = "meta is required";
    return false;
  }
  if (!j.contains("payload") || !j["payload"].is_object()) {
    error = "payload is required";
    return false;
  }

  const auto& meta = j["meta"];
  if (!reject_unknown_fields(meta, {"version", "ts_ms", "trace_id", "agent_id"}, unknown)) {
    error = "unknown field in meta: " + unknown;
    return false;
  }
  if (!meta.contains("version") || !meta["version"].is_string()) {
    error = "meta.version is required";
    return false;
  }
  if (!meta.contains("ts_ms") || !meta["ts_ms"].is_number_integer()) {
    error = "meta.ts_ms is required";
    return false;
  }
  if (!meta.contains("trace_id") || !meta["trace_id"].is_string() ||
      meta["trace_id"].get<std::string>().empty()) {
    error = "meta.trace_id is required";
    return false;
  }
  out.protocol_version = meta["version"].get<std::string>();
  out.sent_at_ms = meta["ts_ms"].get<int64_t>();
  out.trace_id = meta["trace_id"].get<std::string>();
  out.agent_id = meta.value("agent_id", std::string{});

  if (!try_parse_message_type(j["type"].get<std::string>(), out.type)) {
    error = "invalid type";
    return false;
  }

  bool payload_ok = true;
  std::string payload_error;
  out.payload = payload_from_json(out.type, j["payload"], payload_ok, payload_error);
  if (!payload_ok) {
    error = payload_error.empty() ? "invalid payload" : payload_error;
    return false;
  }
  return true;
}

} // namespace control
