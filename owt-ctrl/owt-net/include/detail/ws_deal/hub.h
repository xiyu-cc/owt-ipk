#pragma once

#include "connection.h"
#include "handler_dispatcher.h"
#include "hub_api.h"
#include "detail/runtime/log.h"
#include "message.h"

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ws_deal {

namespace net = boost::asio;

class ws_hub : public ws_hub_api, public std::enable_shared_from_this<ws_hub> {
  struct member {
    std::string route;
    std::weak_ptr<ws_connection> connection;
  };

  net::strand<net::io_context::executor_type> strand_;
  std::shared_ptr<handler_dispatcher> dispatcher_;
  std::unordered_map<std::string, member> members_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      route_members_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      topic_subscribers_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      session_subscriptions_;

public:
  ws_hub(net::io_context &ioc, std::shared_ptr<handler_dispatcher> dispatcher)
      : strand_(net::make_strand(ioc)), dispatcher_(std::move(dispatcher)) {}

  void join(std::shared_ptr<ws_connection> connection, std::string route) {
    if (!connection || route.empty()) {
      return;
    }

    net::post(strand_,
              [self = shared_from_this(), connection = std::move(connection),
               route = std::move(route)]() mutable {
                self->do_join(std::move(connection), std::move(route));
              });
  }

  void leave(std::string session_id) {
    if (session_id.empty()) {
      return;
    }

    net::post(strand_, [self = shared_from_this(),
                        session_id = std::move(session_id)]() mutable {
      self->do_leave(session_id);
    });
  }

  void on_message(std::string session_id, bool text, std::string payload) {
    if (session_id.empty()) {
      return;
    }

    net::post(strand_,
              [self = shared_from_this(), session_id = std::move(session_id),
               text, payload = std::move(payload)]() mutable {
                self->do_on_message(std::move(session_id), text,
                                    std::move(payload));
              });
  }

  void publish_to_topic(std::string topic, bool text,
                        std::string payload) override {
    if (topic.empty()) {
      return;
    }

    if (strand_.running_in_this_thread()) {
      do_publish_to_topic(std::move(topic), text, std::move(payload));
      return;
    }

    net::post(strand_, [self = shared_from_this(), topic = std::move(topic),
                        text, payload = std::move(payload)]() mutable {
      self->do_publish_to_topic(std::move(topic), text, std::move(payload));
    });
  }

  void publish_to_route(std::string route, bool text,
                        std::string payload) override {
    if (route.empty()) {
      return;
    }

    if (strand_.running_in_this_thread()) {
      do_publish_to_route(std::move(route), text, std::move(payload));
      return;
    }

    net::post(strand_, [self = shared_from_this(), route = std::move(route),
                        text, payload = std::move(payload)]() mutable {
      self->do_publish_to_route(std::move(route), text, std::move(payload));
    });
  }

  void publish_to_session(std::string session_id, bool text,
                          std::string payload) override {
    if (session_id.empty()) {
      return;
    }

    if (strand_.running_in_this_thread()) {
      do_publish_to_session(std::move(session_id), text, std::move(payload));
      return;
    }

    net::post(strand_, [self = shared_from_this(),
                        session_id = std::move(session_id), text,
                        payload = std::move(payload)]() mutable {
      self->do_publish_to_session(std::move(session_id), text,
                                  std::move(payload));
    });
  }

  void subscribe(std::string session_id, std::string topic) override {
    if (session_id.empty() || topic.empty()) {
      return;
    }

    if (strand_.running_in_this_thread()) {
      do_subscribe(std::move(session_id), std::move(topic));
      return;
    }

    net::post(strand_, [self = shared_from_this(),
                        session_id = std::move(session_id),
                        topic = std::move(topic)]() mutable {
      self->do_subscribe(std::move(session_id), std::move(topic));
    });
  }

  void unsubscribe(std::string session_id, std::string topic) override {
    if (session_id.empty() || topic.empty()) {
      return;
    }

    if (strand_.running_in_this_thread()) {
      do_unsubscribe(std::move(session_id), std::move(topic));
      return;
    }

    net::post(strand_, [self = shared_from_this(),
                        session_id = std::move(session_id),
                        topic = std::move(topic)]() mutable {
      self->do_unsubscribe(std::move(session_id), std::move(topic));
    });
  }

  void unsubscribe_all(std::string session_id) override {
    if (session_id.empty()) {
      return;
    }

    if (strand_.running_in_this_thread()) {
      do_unsubscribe_all(session_id);
      return;
    }

    net::post(strand_, [self = shared_from_this(),
                        session_id = std::move(session_id)]() mutable {
      self->do_unsubscribe_all(session_id);
    });
  }

private:
  void do_join(std::shared_ptr<ws_connection> connection, std::string route) {
    if (!connection) {
      return;
    }

    const std::string id = connection->session_id();
    if (id.empty()) {
      return;
    }

    const auto it = members_.find(id);
    if (it != members_.end()) {
      erase_route_member(it->second.route, id);
    }

    route_members_[route].insert(id);
    members_.insert_or_assign(id, member{
                                      .route = route,
                                      .connection = std::move(connection),
                                  });

    log::info("ws_hub join id={} route={}", id, route);
    if (dispatcher_) {
      dispatcher_->on_join(route, *this, id);
    }
  }

  void do_leave(const std::string &session_id) {
    if (const auto it = members_.find(session_id); it != members_.end()) {
      const std::string route = it->second.route;
      members_.erase(it);
      erase_route_member(route, session_id);
      do_unsubscribe_all(session_id);
      log::info("ws_hub leave id={}", session_id);
      if (dispatcher_) {
        dispatcher_->on_leave(route, *this, session_id);
      }
      return;
    }

    do_unsubscribe_all(session_id);
  }

  void do_on_message(std::string session_id, bool text, std::string payload) {
    const auto it = members_.find(session_id);
    if (it == members_.end()) {
      return;
    }

    if (dispatcher_) {
      dispatcher_->on_message(it->second.route, *this,
                              inbound_message{
                                  .session_id = std::move(session_id),
                                  .source_route = it->second.route,
                                  .text = text,
                                  .payload = std::move(payload),
                              });
    }
  }

  void do_publish_to_topic(std::string topic, bool text, std::string payload) {
    const auto it = topic_subscribers_.find(topic);
    if (it == topic_subscribers_.end()) {
      return;
    }

    fanout(it->second, text, std::move(payload));
  }

  void do_publish_to_route(std::string route, bool text, std::string payload) {
    const auto it = route_members_.find(route);
    if (it == route_members_.end()) {
      return;
    }

    fanout(it->second, text, std::move(payload));
  }

  void do_publish_to_session(std::string session_id, bool text,
                             std::string payload) {
    const auto it = members_.find(session_id);
    if (it == members_.end()) {
      return;
    }

    auto message = std::make_shared<std::string const>(std::move(payload));
    if (!send_to_member(it->second, message, text)) {
      do_leave(session_id);
    }
  }

  void do_subscribe(std::string session_id, std::string topic) {
    if (members_.find(session_id) == members_.end()) {
      return;
    }

    topic_subscribers_[topic].insert(session_id);
    session_subscriptions_[session_id].insert(topic);
    log::debug("ws_hub subscribe id={} topic={}", session_id, topic);
  }

  void do_unsubscribe(std::string session_id, std::string topic) {
    auto topic_it = topic_subscribers_.find(topic);
    if (topic_it != topic_subscribers_.end()) {
      topic_it->second.erase(session_id);
      if (topic_it->second.empty()) {
        topic_subscribers_.erase(topic_it);
      }
    }

    auto session_it = session_subscriptions_.find(session_id);
    if (session_it != session_subscriptions_.end()) {
      session_it->second.erase(topic);
      if (session_it->second.empty()) {
        session_subscriptions_.erase(session_it);
      }
    }
    log::debug("ws_hub unsubscribe id={} topic={}", session_id, topic);
  }

  void do_unsubscribe_all(const std::string &session_id) {
    const auto session_it = session_subscriptions_.find(session_id);
    if (session_it == session_subscriptions_.end()) {
      return;
    }

    for (const auto &topic : session_it->second) {
      auto topic_it = topic_subscribers_.find(topic);
      if (topic_it == topic_subscribers_.end()) {
        continue;
      }
      topic_it->second.erase(session_id);
      if (topic_it->second.empty()) {
        topic_subscribers_.erase(topic_it);
      }
    }
    session_subscriptions_.erase(session_it);
  }

  void erase_route_member(const std::string &route, const std::string &session) {
    auto route_it = route_members_.find(route);
    if (route_it == route_members_.end()) {
      return;
    }
    route_it->second.erase(session);
    if (route_it->second.empty()) {
      route_members_.erase(route_it);
    }
  }

  bool send_to_member(const member &target,
                      const std::shared_ptr<std::string const> &message,
                      bool text) {
    const auto connection = target.connection.lock();
    if (!connection) {
      return false;
    }
    connection->send(message, text);
    return true;
  }

  void fanout(const std::unordered_set<std::string> &targets, bool text,
              std::string payload) {
    if (targets.empty()) {
      return;
    }

    auto message = std::make_shared<std::string const>(std::move(payload));
    std::vector<std::string> stale_ids;
    stale_ids.reserve(targets.size());

    for (const auto &target_id : targets) {
      const auto it = members_.find(target_id);
      if (it == members_.end()) {
        stale_ids.push_back(target_id);
        continue;
      }

      if (!send_to_member(it->second, message, text)) {
        stale_ids.push_back(target_id);
      }
    }

    for (const auto &stale_id : stale_ids) {
      do_leave(stale_id);
    }
  }
};

} // namespace ws_deal
