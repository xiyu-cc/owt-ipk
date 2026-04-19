#pragma once

#include "handler.h"
#include "detail/runtime/log.h"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace ws_deal {

class handler_dispatcher {
  using handler_ptr = std::shared_ptr<handler>;

  std::unordered_map<std::string, handler_ptr> handlers_;
  mutable std::shared_mutex handlers_mutex_;

public:
  template <typename handler_t, typename... args_t>
  void install(std::string route, args_t &&...args) {
    static_assert(std::is_base_of_v<handler, handler_t>,
                  "handler_t must derive from ws_deal::handler");
    if (route.empty()) {
      return;
    }

    auto instance =
        std::make_shared<handler_t>(std::forward<args_t>(args)...);
    std::unique_lock lock(handlers_mutex_);
    handlers_.insert_or_assign(std::move(route), std::move(instance));
  }

  void uninstall(std::string route) {
    if (route.empty()) {
      return;
    }
    std::unique_lock lock(handlers_mutex_);
    handlers_.erase(route);
  }

  void reset() {
    std::unique_lock lock(handlers_mutex_);
    handlers_.clear();
  }

  bool exists(std::string_view route) const {
    return static_cast<bool>(find(route));
  }

  void on_join(std::string_view route, ws_hub_api &hub,
               std::string_view session_id) const {
    auto handler_ptr = find(route);
    if (!handler_ptr) {
      return;
    }
    handler_ptr->on_join_route(hub, session_id, route);
  }

  void on_leave(std::string_view route, ws_hub_api &hub,
                std::string_view session_id) const {
    auto handler_ptr = find(route);
    if (!handler_ptr) {
      return;
    }
    handler_ptr->on_leave_route(hub, session_id, route);
  }

  void on_message(std::string_view route, ws_hub_api &hub,
                  inbound_message message) const {
    auto handler_ptr = find(route);
    if (!handler_ptr) {
      log::warn("ws handler missing for route={}", route);
      return;
    }
    handler_ptr->on_message(hub, std::move(message));
  }

private:
  handler_ptr find(std::string_view route) const {
    std::shared_lock lock(handlers_mutex_);
    const auto exact_it = handlers_.find(std::string(route));
    if (exact_it != handlers_.end()) {
      return exact_it->second;
    }

    handler_ptr prefix_match;
    size_t prefix_size = 0;
    for (const auto &[pattern, instance] : handlers_) {
      if (pattern.empty() || pattern.back() != '/') {
        continue;
      }
      if (route.size() < pattern.size()) {
        continue;
      }
      if (route.substr(0, pattern.size()) != pattern) {
        continue;
      }
      if (pattern.size() > prefix_size) {
        prefix_size = pattern.size();
        prefix_match = instance;
      }
    }
    return prefix_match;
  }
};

} // namespace ws_deal
