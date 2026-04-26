#pragma once

#include <nlohmann/json.hpp>

#include <exception>

namespace control::command_handlers {

template <typename T>
T payload_value_or(const nlohmann::json& payload, const char* key, const T& fallback) {
  if (!payload.is_object()) {
    return fallback;
  }
  const auto it = payload.find(key);
  if (it == payload.end() || it->is_null()) {
    return fallback;
  }
  try {
    return it->get<T>();
  } catch (const std::exception&) {
    return fallback;
  }
}

} // namespace control::command_handlers
