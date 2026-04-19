#pragma once
#include "http_session.h"
#include "detail/ws_deal/handler_dispatcher.h"
#include "detail/ws_deal/hub.h"
#include <boost/asio/strand.hpp>

namespace transport {

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
  net::io_context &ioc_;
  tcp::acceptor acceptor_;
  std::shared_ptr<ws_deal::ws_hub> ws_hub_;
  std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher_;
  bool ready_{false};

public:
  listener(net::io_context &ioc, tcp::endpoint endpoint,
           std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher)
      : ioc_(ioc), acceptor_(net::make_strand(ioc)),
        ws_hub_(std::make_shared<ws_deal::ws_hub>(ioc, ws_dispatcher)),
        ws_dispatcher_(std::move(ws_dispatcher)) {
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
      log::warn("open: {}", ec.message());
      return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      log::warn("set option: {}", ec.message());
      return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
      log::warn("bind: {}", ec.message());
      return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      log::warn("listen: {}", ec.message());
      return;
    }

    ready_ = true;
  }

  // Start accepting incoming connections
  void run() {
    if (!is_ready()) {
      log::warn("acceptor not ready; skip accept");
      return;
    }
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(acceptor_.get_executor(),
                  beast::bind_front_handler(&listener::do_accept,
                                            this->shared_from_this()));
  }

  bool is_ready() const { return ready_ && acceptor_.is_open(); }

private:
  void do_accept() {
    // The new connection gets its own strand
    acceptor_.async_accept(net::make_strand(ioc_),
                           beast::bind_front_handler(&listener::on_accept,
                                                     this->shared_from_this()));
  }

  void on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
      log::warn("accept: {}", ec.message());
      if (ec == net::error::operation_aborted ||
          ec == net::error::bad_descriptor ||
          ec == net::error::invalid_argument) {
        return;
      }
    } else {
      // Create the http session and run it
      std::make_shared<http_session>(std::move(socket), ws_hub_,
                                     ws_dispatcher_)
          ->run();
    }

    // Accept another connection
    do_accept();
  }
};

} // namespace transport
