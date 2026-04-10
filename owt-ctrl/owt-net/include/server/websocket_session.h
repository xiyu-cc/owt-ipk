#pragma once

#include "log.h"
#include "server/websocket_session_observer.h"

#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <memory>
#include <string>
#include <utility>

namespace server {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class websocket_session : public std::enable_shared_from_this<websocket_session> {
  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;
  std::deque<std::string> write_queue_;
  std::shared_ptr<websocket_session_observer> observer_;
  bool closing_ = false;
  bool closed_notified_ = false;
  std::string close_reason_;

public:
  explicit websocket_session(
      tcp::socket&& socket,
      std::shared_ptr<websocket_session_observer> observer = nullptr)
      : ws_(std::move(socket)), observer_(std::move(observer)) {}

  void set_observer(std::shared_ptr<websocket_session_observer> observer) {
    observer_ = std::move(observer);
  }

  template <class Body, class Allocator>
  void do_accept(http::request<Body, http::basic_fields<Allocator>> req) {
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
      res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " owt-net");
    }));

    ws_.async_accept(
        req, beast::bind_front_handler(&websocket_session::on_accept, shared_from_this()));
  }

  void send_text(std::string text) {
    auto self = shared_from_this();
    net::post(ws_.get_executor(), [self, text = std::move(text)]() mutable {
      const bool idle = self->write_queue_.empty();
      self->write_queue_.push_back(std::move(text));
      if (idle) {
        self->do_write();
      }
    });
  }

  void close_with_reason(std::string reason) {
    auto self = shared_from_this();
    net::post(ws_.get_executor(), [self, reason = sanitize_close_reason(std::move(reason))]() mutable {
      if (self->closing_) {
        return;
      }
      self->closing_ = true;
      self->close_reason_ = std::move(reason);
      if (self->write_queue_.empty()) {
        self->start_close();
      }
    });
  }

private:
  void on_accept(beast::error_code ec) {
    if (ec) {
      log::warn("websocket accept failed: {}", ec.message());
      return;
    }
    notify_session_open();
    do_read();
  }

  void do_read() {
    ws_.async_read(
        buffer_, beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == websocket::error::closed) {
      cleanup_on_close();
      return;
    }
    if (ec) {
      log::warn("websocket read failed: {}", ec.message());
      cleanup_on_close();
      return;
    }

    const auto text = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    notify_text_message(text);
    if (!closing_) {
      do_read();
    }
  }

  void do_write() {
    if (write_queue_.empty()) {
      return;
    }
    ws_.text(true);
    ws_.async_write(
        net::buffer(write_queue_.front()),
        beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
  }

  void on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec) {
      log::warn("websocket write failed: {}", ec.message());
      cleanup_on_close();
      return;
    }

    if (!write_queue_.empty()) {
      write_queue_.pop_front();
    }
    if (!write_queue_.empty()) {
      do_write();
    } else if (closing_) {
      start_close();
    }
  }

  void start_close() {
    if (!closing_) {
      return;
    }
    ws_.async_close(
        websocket::close_reason{websocket::close_code::policy_error, close_reason_},
        beast::bind_front_handler(&websocket_session::on_close, shared_from_this()));
  }

  void on_close(beast::error_code ec) {
    if (ec && ec != websocket::error::closed) {
      log::warn("websocket close failed: {}", ec.message());
    }
    cleanup_on_close();
  }

  void cleanup_on_close() {
    if (closed_notified_) {
      return;
    }
    closed_notified_ = true;
    notify_session_closed();
  }

  void notify_session_open() {
    if (!observer_) {
      return;
    }
    observer_->on_session_open(shared_from_this());
  }

  void notify_text_message(const std::string& text) {
    if (!observer_) {
      return;
    }
    observer_->on_text_message(shared_from_this(), text);
  }

  void notify_session_closed() {
    if (!observer_) {
      return;
    }
    observer_->on_session_closed(shared_from_this());
  }

  static std::string sanitize_close_reason(std::string text) {
    constexpr std::size_t kMaxReasonSize = 120;
    if (text.size() > kMaxReasonSize) {
      text.resize(kMaxReasonSize);
    }
    return text;
  }
};

} // namespace server
