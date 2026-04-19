#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace app::ws {

struct AgentMeta {
  std::string version;
  std::string trace_id;
  int64_t ts_ms = 0;
  std::string agent_id;
};

struct AgentEnvelope {
  std::string type;
  AgentMeta meta;
  nlohmann::json payload = nlohmann::json::object();
};

bool parse_agent_envelope(std::string_view text, AgentEnvelope& out, std::string& error);
std::string encode_agent_envelope(const AgentEnvelope& envelope);

} // namespace app::ws
