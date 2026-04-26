#include "log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool is_allowed_stdout_sink(const std::shared_ptr<spdlog::sinks::sink>& sink) {
  if (!sink) {
    return false;
  }
  return std::dynamic_pointer_cast<spdlog::sinks::stdout_color_sink_mt>(sink) != nullptr ||
      std::dynamic_pointer_cast<spdlog::sinks::stdout_color_sink_st>(sink) != nullptr;
}

void test_logger_has_no_file_sink() {
  log::shutdown();
  log::init();
  auto logger = spdlog::get("owt_agent");
  require(logger != nullptr, "owt_agent logger should be registered");
  require(!logger->sinks().empty(), "owt_agent logger should have at least one sink");
  for (const auto& sink : logger->sinks()) {
    require(is_allowed_stdout_sink(sink), "owt_agent logger sinks must be stdout-only");
  }
  log::shutdown();
}

void test_logging_without_explicit_init() {
  log::shutdown();
  log::info("log without explicit init {}", 1);
  log::warn("log without explicit init {}", 2);
}

void test_logging_after_shutdown() {
  log::init();
  log::info("log before shutdown");
  log::shutdown();
  log::error("log after shutdown");
}

void test_concurrent_logging() {
  log::shutdown();
  constexpr int kThreads = 8;
  constexpr int kPerThread = 200;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    workers.emplace_back([i]() {
      for (int j = 0; j < kPerThread; ++j) {
        log::debug("thread={} index={}", i, j);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  log::shutdown();
}

} // namespace

int main() {
  try {
    test_logger_has_no_file_sink();
    test_logging_without_explicit_init();
    test_logging_after_shutdown();
    test_concurrent_logging();
    require(true, "log safety checks");
    std::cout << "owt-agent log safety tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "owt-agent log safety tests failed: " << ex.what() << '\n';
    return 1;
  }
}
