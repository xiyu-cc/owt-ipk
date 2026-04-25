#pragma once

#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace {

inline std::mutex &server_core_log_mutex() {
  static std::mutex mutex;
  return mutex;
}

} // namespace

inline std::shared_ptr<spdlog::logger> &log::logger_storage() {
  static std::shared_ptr<spdlog::logger> logger{};
  return logger;
}

inline void log::ensure_initialized() {
  if (logger_storage()) {
    return;
  }
  init();
}

inline void log::init() {
  std::scoped_lock lock(server_core_log_mutex());
  if (logger_storage()) {
    return;
  }

  try {
    std::filesystem::create_directories("logs");
    spdlog::flush_every(std::chrono::seconds(5));
    auto logger = spdlog::rotating_logger_mt<spdlog::async_factory>(
        "server_core", "logs/server_core.log", 10 * 1024 * 1024, 100);
    logger->sinks().push_back(
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger->flush_on(spdlog::level::warn);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    logger_storage() = std::move(logger);
  } catch (const std::exception &e) {
    std::cerr << "create logger failure: " << e.what() << '\n';
    std::abort();
  }
}

inline void log::shutdown() {
  std::scoped_lock lock(server_core_log_mutex());
  spdlog::shutdown();
  logger_storage().reset();
}
