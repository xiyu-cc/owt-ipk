#pragma once

#include "handler_dispatcher.h"

#include <string>
#include <utility>

namespace ws_deal {

class handler_registry {
  handler_dispatcher &dispatcher_;

public:
  explicit handler_registry(handler_dispatcher &dispatcher)
      : dispatcher_(dispatcher) {}

  template <typename handler_t, typename... args_t>
  void install(std::string route, args_t &&...args) {
    dispatcher_.template install<handler_t>(std::move(route),
                                            std::forward<args_t>(args)...);
  }

  void reset() { dispatcher_.reset(); }

  ~handler_registry() { reset(); }
};

} // namespace ws_deal
