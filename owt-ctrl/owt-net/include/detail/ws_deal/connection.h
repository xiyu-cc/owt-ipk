#pragma once

#include <memory>
#include <string>

namespace ws_deal {

class ws_connection {
public:
  virtual ~ws_connection() = default;

  virtual void send(std::shared_ptr<std::string const> payload, bool text) = 0;

  virtual std::string session_id() const = 0;
};

} // namespace ws_deal
