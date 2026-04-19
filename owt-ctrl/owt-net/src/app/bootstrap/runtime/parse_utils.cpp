#include "internal.h"

#include <limits>

namespace app::bootstrap::runtime_internal {

bool parse_int(const nlohmann::json& value, int& out) {
  if (!value.is_number_integer()) {
    return false;
  }
  const auto raw = value.get<int64_t>();
  if (raw < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
      raw > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  out = static_cast<int>(raw);
  return true;
}

bool parse_int64(const nlohmann::json& value, int64_t& out) {
  if (!value.is_number_integer()) {
    return false;
  }
  out = value.get<int64_t>();
  return true;
}

} // namespace app::bootstrap::runtime_internal
