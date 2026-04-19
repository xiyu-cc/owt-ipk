#pragma once

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <set>
#include <string>
#include <string_view>

namespace app::ws::detail {

inline bool reject_unknown_fields(
    const nlohmann::json& obj,
    std::initializer_list<std::string_view> allowed,
    std::string& unknown_field) {
  if (!obj.is_object()) {
    unknown_field.clear();
    return false;
  }

  const std::set<std::string_view> allowed_set(allowed.begin(), allowed.end());
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (allowed_set.find(it.key()) == allowed_set.end()) {
      unknown_field = it.key();
      return false;
    }
  }
  unknown_field.clear();
  return true;
}

} // namespace app::ws::detail
