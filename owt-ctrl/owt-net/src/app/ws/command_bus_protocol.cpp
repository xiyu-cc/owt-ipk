#include "app/ws/command_bus_protocol.h"

#include "json_field_validation.h"
#include "owt/protocol/v5/contract.h"

#include <algorithm>

namespace app::ws {

namespace {

bool is_valid_id(const nlohmann::json& id) {
  return id.is_null() || id.is_string() || id.is_number_integer() || id.is_number_unsigned();
}

bool is_valid_kind(std::string_view kind) {
  return kind == owt::protocol::v5::kind::kAction ||
      kind == owt::protocol::v5::kind::kResult ||
      kind == owt::protocol::v5::kind::kEvent ||
      kind == owt::protocol::v5::kind::kError;
}

} // namespace

bool parse_bus_envelope(std::string_view text, BusEnvelope& out, std::string& error) {
  error.clear();

  auto root = nlohmann::json::parse(text, nullptr, false);
  if (!root.is_object()) {
    error = "invalid json";
    return false;
  }

  std::string unknown;
  if (!detail::reject_unknown_fields(
          root,
          {"v", "kind", "name", "id", "ts_ms", "payload", "target"},
          unknown)) {
    error = "unknown field: " + unknown;
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

  const auto& kind = root["kind"];
  if (!is_valid_kind(kind.get<std::string>())) {
    error = "invalid kind";
    return false;
  }

  if (root.contains("id") && !is_valid_id(root["id"])) {
    error = "id must be string/integer/null";
    return false;
  }

  if (root.contains("payload") && !root["payload"].is_object()) {
    error = "payload must be object";
    return false;
  }

  if (root.contains("target") && !root["target"].is_string()) {
    error = "target must be string";
    return false;
  }

  out.version = root["v"].get<std::string>();
  out.kind = root["kind"].get<std::string>();
  out.name = root["name"].get<std::string>();
  out.id = root.value("id", nlohmann::json(nullptr));
  out.ts_ms = root["ts_ms"].get<int64_t>();
  out.payload = root.value("payload", nlohmann::json::object());
  out.target = root.value("target", std::string{});
  return true;
}

std::string encode_bus_envelope(const BusEnvelope& envelope) {
  nlohmann::json out = {
      {"v", envelope.version},
      {"kind", envelope.kind},
      {"name", envelope.name},
      {"id", envelope.id},
      {"ts_ms", envelope.ts_ms},
      {"payload", envelope.payload.is_object() ? envelope.payload : nlohmann::json::object()},
  };
  if (!envelope.target.empty()) {
    out["target"] = envelope.target;
  }
  return out.dump();
}

std::string bus_result(
    std::string_view name,
    const nlohmann::json& id,
    int64_t ts_ms,
    const nlohmann::json& payload,
    std::string_view target) {
  BusEnvelope envelope;
  envelope.version = std::string(owt::protocol::v5::kProtocol);
  envelope.kind = std::string(owt::protocol::v5::kind::kResult);
  envelope.name = std::string(name);
  envelope.id = id;
  envelope.ts_ms = ts_ms;
  envelope.payload = payload.is_object() ? payload : nlohmann::json::object();
  envelope.target = std::string(target);
  return encode_bus_envelope(envelope);
}

std::string bus_error(
    std::string_view name,
    const nlohmann::json& id,
    int64_t ts_ms,
    std::string_view code,
    std::string message,
    const nlohmann::json& detail,
    std::string_view target) {
  nlohmann::json payload = {
      {"code", std::string(code)},
      {"message", std::move(message)},
      {"detail", detail.is_object() ? detail : nlohmann::json::object()},
  };
  BusEnvelope envelope;
  envelope.version = std::string(owt::protocol::v5::kProtocol);
  envelope.kind = std::string(owt::protocol::v5::kind::kError);
  envelope.name = std::string(name);
  envelope.id = id;
  envelope.ts_ms = ts_ms;
  envelope.payload = std::move(payload);
  envelope.target = std::string(target);
  return encode_bus_envelope(envelope);
}

std::string bus_event(
    std::string_view name,
    int64_t ts_ms,
    const nlohmann::json& payload,
    std::string_view target) {
  BusEnvelope envelope;
  envelope.version = std::string(owt::protocol::v5::kProtocol);
  envelope.kind = std::string(owt::protocol::v5::kind::kEvent);
  envelope.name = std::string(name);
  envelope.id = nullptr;
  envelope.ts_ms = ts_ms;
  envelope.payload = payload.is_object() ? payload : nlohmann::json::object();
  envelope.target = std::string(target);
  return encode_bus_envelope(envelope);
}

} // namespace app::ws
