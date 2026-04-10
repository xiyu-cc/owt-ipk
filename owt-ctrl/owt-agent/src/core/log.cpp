#include "log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::shared_ptr<spdlog::logger> createLogger() {
  return spdlog::stdout_color_mt("owt_agent");
}

} // namespace

std::shared_ptr<spdlog::logger> log::logger_{};

void log::init() {
  if (logger_ != nullptr) {
    return;
  }

  try {
    spdlog::flush_every(std::chrono::seconds(5));
    logger_ = createLogger();
    logger_->set_level(spdlog::level::info);
    logger_->flush_on(spdlog::level::warn);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    logger_->info("log output: stdout only (disk write disabled)");
  } catch (const std::exception& e) {
    std::cerr << "create logger failure: " << e.what() << '\n';
    std::abort();
  }
}
