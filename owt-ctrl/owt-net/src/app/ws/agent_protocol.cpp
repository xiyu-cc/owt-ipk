#include "app/ws/agent_protocol.h"
#include "json_field_validation.h"
#include "owt/protocol/v4/contract.h"

namespace app::ws {

bool parse_agent_envelope(std::string_view text, AgentEnvelope& out, std::string& error) {
  auto root = nlohmann::json::parse(text, nullptr, false);
  if (!root.is_object()) {
    error = "json root must be object";
    return false;
  }

  std::string unknown;
  if (!detail::reject_unknown_fields(root, {"type", "meta", "data"}, unknown)) {
    error = "unknown field in envelope: " + unknown;
    return false;
  }

  if (!root.contains("type") || !root["type"].is_string() ||
      root["type"].get<std::string>().empty()) {
    error = "type is required";
    return false;
  }
  if (!root.contains("meta") || !root["meta"].is_object()) {
    error = "meta is required";
    return false;
  }
  if (!root.contains("data") || !root["data"].is_object()) {
    error = "data is required";
    return false;
  }

  const auto& meta = root["meta"];
  if (!detail::reject_unknown_fields(meta, {"protocol", "trace_id", "ts_ms", "agent_id"}, unknown)) {
    error = "unknown field in meta: " + unknown;
    return false;
  }
  if (!meta.contains("protocol") || !meta["protocol"].is_string() ||
      meta["protocol"].get<std::string>().empty()) {
    error = "meta.protocol is required";
    return false;
  }
  if (!meta.contains("trace_id") || !meta["trace_id"].is_string() ||
      meta["trace_id"].get<std::string>().empty()) {
    error = "meta.trace_id is required";
    return false;
  }
  if (!meta.contains("ts_ms") || !meta["ts_ms"].is_number_integer()) {
    error = "meta.ts_ms is required";
    return false;
  }

  out.type = root["type"].get<std::string>();
  out.meta.protocol = meta["protocol"].get<std::string>();
  out.meta.trace_id = meta["trace_id"].get<std::string>();
  out.meta.ts_ms = meta["ts_ms"].get<int64_t>();
  out.meta.agent_id = meta.value("agent_id", std::string{});
  out.data = root["data"];
  error.clear();
  return true;
}

std::string encode_agent_envelope(const AgentEnvelope& envelope) {
  return nlohmann::json{
      {"type", envelope.type},
      {"meta",
       {
           {"protocol", envelope.meta.protocol},
           {"trace_id", envelope.meta.trace_id},
           {"ts_ms", envelope.meta.ts_ms},
           {"agent_id", envelope.meta.agent_id},
       }},
      {"data", envelope.data},
  }
      .dump();
}

} // namespace app::ws
