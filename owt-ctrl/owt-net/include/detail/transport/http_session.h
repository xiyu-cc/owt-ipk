#pragma once
#include "detail/http_deal/router.h"
#include "detail/http_deal/response.h"
#include "detail/runtime/log.h"
#include "websocket_session.h"
#include "detail/ws_deal/handler_dispatcher.h"
#include "detail/ws_deal/hub.h"
#include "detail/ws_deal/router.h"
#include <boost/asio/dispatch.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <queue>
#include <string>

namespace transport {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

template <class Body>
http::message_generator handle_request(http::request<Body> &&req) {
  return http_deal::router::dispatch(req);
}

// Handles an HTTP server connection
class http_session : public std::enable_shared_from_this<http_session> {
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::shared_ptr<ws_deal::ws_hub> ws_hub_;
  std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher_;

  static constexpr std::size_t queue_limit = 8; // max responses
  std::queue<http::message_generator> response_queue_;

  // The parser is stored in an optional container so we can
  // construct it from scratch it at the beginning of each new message.
  boost::optional<http::request_parser<http::vector_body<uint8_t>>> parser_;

public:
  // Take ownership of the socket
  http_session(tcp::socket &&socket, std::shared_ptr<ws_deal::ws_hub> ws_hub,
               std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher)
      : stream_(std::move(socket)), ws_hub_(std::move(ws_hub)),
        ws_dispatcher_(std::move(ws_dispatcher)) {
    buffer_.reserve(64 * 1024);
    static_assert(queue_limit > 0, "queue limit must be positive");
  }

  // Start the session
  void run() {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&http_session::do_read,
                                            this->shared_from_this()));
  }

private:
  void do_read() {
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(10 * 1024 * 1024);

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request using the parser-oriented interface
    http::async_read(stream_, buffer_, *parser_,
                     beast::bind_front_handler(&http_session::on_read,
                                               this->shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
      return do_close();

    if (ec)
      return log::warn("read: {}", ec.message());

    auto request = parser_->release();

    // See if it is a WebSocket Upgrade
    if (websocket::is_upgrade(request)) {
      const std::string route = ws_deal::router::resolve(request.target());
      if (route.empty() || !ws_dispatcher_ || !ws_dispatcher_->exists(route)) {
        log::warn("reject websocket upgrade for unsupported target '{}'",
                  std::string(request.target()));
        queue_write(http_deal::response::message(
            request, http::status::not_found, "unknown websocket endpoint"));

        if (response_queue_.size() < queue_limit)
          do_read();
        return;
      }

      // Create a websocket session, transferring ownership
      // of both the socket and the HTTP request.
      std::make_shared<websocket_session>(stream_.release_socket(), ws_hub_,
                                          route)
          ->run(std::move(request));
      return;
    }

    // Send the response
    queue_write(handle_request(std::move(request)));

    // If we aren't at the queue limit, try to pipeline another request
    if (response_queue_.size() < queue_limit)
      do_read();
  }

  void queue_write(http::message_generator response) {
    // Allocate and store the work
    response_queue_.push(std::move(response));

    // If there was no previous work, start the write loop
    if (response_queue_.size() == 1)
      do_write();
  }

  // Called to start/continue the write-loop. Should not be called when
  // write_loop is already active.
  void do_write() {
    if (!response_queue_.empty()) {
      bool keep_alive = response_queue_.front().keep_alive();

      beast::async_write(stream_, std::move(response_queue_.front()),
                         beast::bind_front_handler(&http_session::on_write,
                                                   this->shared_from_this(),
                                                   keep_alive));
    }
  }

  void on_write(bool keep_alive, beast::error_code ec,
                std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec)
      return log::warn("write: {}", ec.message());

    if (!keep_alive) {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      return do_close();
    }

    // Resume the read if it has been paused
    if (response_queue_.size() == queue_limit)
      do_read();

    response_queue_.pop();

    do_write();
  }

  void do_close() {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

} // namespace transport
