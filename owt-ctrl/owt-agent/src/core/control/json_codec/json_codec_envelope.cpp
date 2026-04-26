#include "control/json_codec/codec_detail.h"

namespace control::json_codec::detail {

json envelope_to_json(const envelope& value) {
  json out = {
      {"v", value.version},
      {"kind", kind_for_type(value.type)},
      {"name", to_string(value.type)},
      {"id", value.id},
      {"ts_ms", value.ts_ms},
      {"payload", payload_to_json(value.type, value.payload)},
  };
  if (!value.target.empty()) {
    out["target"] = value.target;
  }
  return out;
}

bool envelope_from_json(const json& root, envelope& out, std::string& error) {
  if (!root.is_object()) {
    error = "json root is not object";
    return false;
  }

  std::string unknown;
  if (!reject_unknown_fields(root, {"v", "kind", "name", "id", "ts_ms", "payload", "target"}, unknown)) {
    error = "unknown field in envelope: " + unknown;
    return false;
  }
  if (!root.contains("v") || !root["v"].is_string() || root["v"].get<std::string>().empty()) {
    error = "v is required";
    return false;
  }
  if (!root.contains("kind") || !root["kind"].is_string() || root["kind"].get<std::string>().empty()) {
    error = "kind is required";
    return false;
  }
  if (!root.contains("name") || !root["name"].is_string() || root["name"].get<std::string>().empty()) {
    error = "name is required";
    return false;
  }
  if (!root.contains("ts_ms") || !root["ts_ms"].is_number_integer()) {
    error = "ts_ms is required";
    return false;
  }
  if (!root.contains("payload") || !root["payload"].is_object()) {
    error = "payload is required";
    return false;
  }
  if (root.contains("id") && !is_valid_id(root["id"])) {
    error = "id must be string/integer/null";
    return false;
  }
  if (root.contains("target") && !root["target"].is_string()) {
    error = "target must be string";
    return false;
  }

  message_type parsed_type = message_type::server_error;
  if (!try_parse_message_type(root["name"].get<std::string>(), parsed_type)) {
    error = "invalid name";
    return false;
  }
  const auto expected_kind = kind_for_type(parsed_type);
  if (root["kind"].get<std::string>() != expected_kind) {
    error = "kind does not match name";
    return false;
  }

  out.type = parsed_type;
  out.version = root["v"].get<std::string>();
  out.id = root.value("id", json(nullptr));
  out.ts_ms = root["ts_ms"].get<int64_t>();
  out.target = root.value("target", std::string{});

  bool payload_ok = true;
  std::string payload_error;
  out.payload = payload_from_json(out.type, root["payload"], payload_ok, payload_error);
  if (!payload_ok) {
    error = payload_error.empty() ? "invalid payload" : payload_error;
    return false;
  }

  error.clear();
  return true;
}

} // namespace control::json_codec::detail
