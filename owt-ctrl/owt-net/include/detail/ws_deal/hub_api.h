#pragma once

#include <string>

namespace ws_deal {

class ws_hub_api {
public:
  virtual ~ws_hub_api() = default;

  virtual void publish_to_topic(std::string topic, bool text,
                                std::string payload) = 0;
  virtual void publish_to_route(std::string route, bool text,
                                std::string payload) = 0;
  virtual void publish_to_session(std::string session_id, bool text,
                                  std::string payload) = 0;

  virtual void subscribe(std::string session_id, std::string topic) = 0;

  virtual void unsubscribe(std::string session_id, std::string topic) = 0;

  virtual void unsubscribe_all(std::string session_id) = 0;
};

} // namespace ws_deal
