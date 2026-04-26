#include "log.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {

std::mutex& logger_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<spdlog::logger>& logger_storage() {
  static std::shared_ptr<spdlog::logger> logger{};
  return logger;
}

bool is_allowed_stdout_sink(const std::shared_ptr<spdlog::sinks::sink>& sink) {
  if (!sink) {
    return false;
  }
  return std::dynamic_pointer_cast<spdlog::sinks::stdout_color_sink_mt>(sink) != nullptr ||
      std::dynamic_pointer_cast<spdlog::sinks::stdout_color_sink_st>(sink) != nullptr;
}

bool has_disallowed_sink(const std::shared_ptr<spdlog::logger>& logger) {
  if (!logger) {
    return false;
  }
  for (const auto& sink : logger->sinks()) {
    if (!is_allowed_stdout_sink(sink)) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<spdlog::logger> create_stdout_logger() {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("owt_agent", std::move(sink));
  if (has_disallowed_sink(logger)) {
    throw std::runtime_error("logger sinks must be stdout-only");
  }
  spdlog::drop("owt_agent");
  spdlog::register_logger(logger);
  return logger;
}

} // namespace

void log::init() {
  std::scoped_lock lock(logger_mutex());
  if (logger_storage()) {
    return;
  }

  try {
    spdlog::flush_every(std::chrono::seconds(5));
    auto logger = create_stdout_logger();
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::warn);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    logger->info("log output: stdout only (disk write disabled)");
    logger_storage() = std::move(logger);
  } catch (const std::exception& e) {
    std::cerr << "create logger failure: " << e.what() << '\n';
    logger_storage().reset();
  } catch (...) {
    std::cerr << "create logger failure: unknown exception\n";
    logger_storage().reset();
  }
}

void log::shutdown() {
  std::shared_ptr<spdlog::logger> old_logger;
  {
    std::scoped_lock lock(logger_mutex());
    old_logger = std::move(logger_storage());
    logger_storage().reset();
  }
  if (old_logger) {
    old_logger->flush();
  }
  spdlog::shutdown();
}

std::shared_ptr<spdlog::logger> log::acquire_logger() {
  init();
  std::scoped_lock lock(logger_mutex());
  return logger_storage();
}
