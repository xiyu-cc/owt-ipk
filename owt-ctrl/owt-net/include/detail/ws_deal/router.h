#pragma once

#include "detail/runtime/utils.h"

#include <string>
#include <string_view>

namespace ws_deal {

class router {
public:
  static std::string resolve(std::string_view target) {
    const std::string path = utils::url_path(std::string(target));
    if (path.empty() || path.front() != '/') {
      return {};
    }
    return path;
  }
};

} // namespace ws_deal
