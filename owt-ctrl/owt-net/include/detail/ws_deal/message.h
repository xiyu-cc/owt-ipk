#pragma once

#include <string>

namespace ws_deal {

struct inbound_message {
  std::string session_id;
  std::string source_route;
  bool text = true;
  std::string payload;
};

} // namespace ws_deal
