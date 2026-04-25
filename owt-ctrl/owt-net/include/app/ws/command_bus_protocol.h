#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace app::ws {

struct BusEnvelope {
  std::string version;
  std::string kind;
  std::string name;
  nlohmann::json id = nullptr;
  int64_t ts_ms = 0;
  nlohmann::json payload = nlohmann::json::object();
  std::string target;
};

bool parse_bus_envelope(std::string_view text, BusEnvelope& out, std::string& error);

std::string encode_bus_envelope(const BusEnvelope& envelope);

std::string bus_result(
    std::string_view name,
    const nlohmann::json& id,
    int64_t ts_ms,
    const nlohmann::json& payload,
    std::string_view target = "");

std::string bus_error(
    std::string_view name,
    const nlohmann::json& id,
    int64_t ts_ms,
    std::string_view code,
    std::string message,
    const nlohmann::json& detail = nlohmann::json::object(),
    std::string_view target = "");

std::string bus_event(
    std::string_view name,
    int64_t ts_ms,
    const nlohmann::json& payload,
    std::string_view target = "");

} // namespace app::ws
