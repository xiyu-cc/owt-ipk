#pragma once
#include "listener.h"
#include "detail/runtime/log.h"
#include "detail/ws_deal/handler_dispatcher.h"
#include <algorithm>
#include <boost/asio/signal_set.hpp>
#include <memory>
#include <string>

namespace transport {

class controller {
public:
  controller() = delete;
  ~controller() = delete;

  static bool init(const std::string &host, int port, int threads,
                   std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher) {
    beast::error_code ec;
    auto address = net::ip::make_address(host, ec);
    if (ec) {
      log::warn("invalid server host '{}', using 0.0.0.0", host);
      address = net::ip::make_address("0.0.0.0");
    }
    int safePort = std::clamp(port, 1, 65535);
    auto const portValue = static_cast<unsigned short>(safePort);
    auto const threadCount = std::max<int>(1, threads);

    // The io_context is required for all I/O
    net::io_context ioc{threadCount};

    // Create and launch a listening port
    auto server_listener = std::make_shared<listener>(
        ioc, tcp::endpoint{address, portValue}, std::move(ws_dispatcher));
    if (!server_listener->is_ready()) {
      log::error("listener init failed on {}:{}", address.to_string(), safePort);
      return false;
    }
    server_listener->run();

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](beast::error_code const &, int) {
      // Stop the `io_context`. This will cause `run()`
      // to return immediately, eventually destroying the
      // `io_context` and all of the sockets in it.
      ioc.stop();
    });

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threadCount - 1);
    for (auto i = threadCount - 1; i > 0; --i)
      v.emplace_back([&ioc] { ioc.run(); });
    ioc.run();

    // (If we get here, it means we got a SIGINT or SIGTERM)

    // Block until all the threads exit
    for (auto &t : v) {
      t.join();
    }

    return true;
  }
};

} // namespace transport
