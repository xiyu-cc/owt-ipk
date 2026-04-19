#pragma once

#include "detail/runtime/log.h"
#include "detail/runtime/utils.h"
#include "detail/ws_deal/connection.h"
#include "detail/ws_deal/hub.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <string>

namespace transport {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class websocket_session : public ws_deal::ws_connection,
                          public std::enable_shared_from_this<websocket_session> {
  struct outbound_item {
    std::shared_ptr<std::string const> payload;
    bool text = true;
  };

  static constexpr std::size_t max_read_message_bytes = 10 * 1024 * 1024;
  static constexpr std::size_t max_outbound_queue_size = 256;

  beast::flat_buffer buffer_;
  websocket::stream<beast::tcp_stream> ws_;
  std::deque<outbound_item> queue_;
  std::shared_ptr<ws_deal::ws_hub> hub_;
  std::string route_;
  std::string session_id_;
  std::atomic_bool detached_{false};

  void fail(beast::error_code ec, char const *what);
  void on_accept(beast::error_code ec);
  void on_read(beast::error_code ec, std::size_t bytes_transferred);
  void on_write(beast::error_code ec, std::size_t bytes_transferred);
  void do_write();
  void detach_from_hub();
  void close_with_policy_violation();

public:
  websocket_session(tcp::socket &&socket, std::shared_ptr<ws_deal::ws_hub> hub,
                    std::string route);
  ~websocket_session() override;

  template <class Body, class Allocator>
  void run(http::request<Body, http::basic_fields<Allocator>> req);

  void send(std::shared_ptr<std::string const> payload, bool text) override;
  std::string session_id() const override;

private:
  void on_send(std::shared_ptr<std::string const> payload, bool text);
};

template <class Body, class Allocator>
void websocket_session::run(
    http::request<Body, http::basic_fields<Allocator>> req) {
  ws_.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::server));
  ws_.read_message_max(max_read_message_bytes);

  ws_.set_option(
      websocket::stream_base::decorator([](websocket::response_type &res) {
        res.set(http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) + " websocket-session");
      }));

  ws_.async_accept(req, beast::bind_front_handler(&websocket_session::on_accept,
                                                  shared_from_this()));
}

inline websocket_session::websocket_session(tcp::socket &&socket,
                                            std::shared_ptr<ws_deal::ws_hub> hub,
                                            std::string route)
    : ws_(std::move(socket)), hub_(std::move(hub)),
      route_(std::move(route)),
      session_id_(utils::uuid()) {}

inline websocket_session::~websocket_session() { detach_from_hub(); }

inline void websocket_session::fail(beast::error_code ec, char const *what) {
  if (ec == net::error::operation_aborted || ec == websocket::error::closed) {
    detach_from_hub();
    return;
  }

  log::warn("websocket {} failed, id={}, route={}, reason={}", what,
            session_id_, route_, ec.message());
  detach_from_hub();
}

inline void websocket_session::on_accept(beast::error_code ec) {
  if (ec) {
    return fail(ec, "accept");
  }

  if (hub_) {
    hub_->join(shared_from_this(), route_);
  }

  ws_.async_read(buffer_, beast::bind_front_handler(&websocket_session::on_read,
                                                    shared_from_this()));
}

inline void websocket_session::on_read(beast::error_code ec, std::size_t) {
  if (ec) {
    return fail(ec, "read");
  }

  const bool text_frame = ws_.got_text();
  std::string payload = beast::buffers_to_string(buffer_.cdata());
  buffer_.consume(buffer_.size());

  if (hub_) {
    hub_->on_message(session_id_, text_frame, std::move(payload));
  }

  ws_.async_read(buffer_, beast::bind_front_handler(&websocket_session::on_read,
                                                    shared_from_this()));
}

inline void websocket_session::send(std::shared_ptr<std::string const> payload,
                                    bool text) {
  if (!payload) {
    return;
  }

  net::post(ws_.get_executor(),
            beast::bind_front_handler(&websocket_session::on_send,
                                      shared_from_this(), std::move(payload),
                                      text));
}

inline std::string websocket_session::session_id() const { return session_id_; }

inline void websocket_session::on_send(std::shared_ptr<std::string const> payload,
                                       bool text) {
  if (!ws_.is_open()) {
    return;
  }

  if (queue_.size() >= max_outbound_queue_size) {
    log::warn(
        "websocket outbound queue overflow, id={}, route={}, limit={}",
        session_id_, route_, max_outbound_queue_size);
    close_with_policy_violation();
    return;
  }

  queue_.push_back(outbound_item{std::move(payload), text});

  if (queue_.size() > 1) {
    return;
  }

  do_write();
}

inline void websocket_session::do_write() {
  ws_.text(queue_.front().text);
  ws_.async_write(net::buffer(*queue_.front().payload),
                  beast::bind_front_handler(&websocket_session::on_write,
                                            shared_from_this()));
}

inline void websocket_session::on_write(beast::error_code ec, std::size_t) {
  if (ec) {
    return fail(ec, "write");
  }

  queue_.pop_front();

  if (!queue_.empty()) {
    do_write();
  }
}

inline void websocket_session::detach_from_hub() {
  if (detached_.exchange(true)) {
    return;
  }

  if (hub_) {
    hub_->leave(session_id_);
  }
}

inline void websocket_session::close_with_policy_violation() {
  if (!ws_.is_open()) {
    detach_from_hub();
    return;
  }

  ws_.async_close(
      websocket::close_reason(websocket::close_code::policy_error),
      [self = shared_from_this()](beast::error_code ec) {
        if (ec && ec != websocket::error::closed &&
            ec != net::error::operation_aborted) {
          self->fail(ec, "close");
          return;
        }
        self->detach_from_hub();
      });
}

} // namespace transport
