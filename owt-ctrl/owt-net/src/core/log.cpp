#include "log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr size_t kRotateFileSize = 10 * 1024 * 1024;
constexpr size_t kRotateFileCount = 100;

std::shared_ptr<spdlog::logger> createLogger(std::string& outPath) {
  const std::vector<std::string> candidates = {
      "/var/log/owt-net/owt-net.log",
      "logs/owt-net.log",
  };

  std::string lastError = "no log path candidates";
  for (const auto& logPath : candidates) {
    try {
      const std::filesystem::path path(logPath);
      const auto parent = path.parent_path();
      if (!parent.empty()) {
        std::filesystem::create_directories(parent);
      }
      outPath = path.string();
      return spdlog::rotating_logger_mt("owt_net", outPath, kRotateFileSize, kRotateFileCount);
    } catch (const std::exception& e) {
      lastError = e.what();
    }
  }

  throw std::runtime_error("failed to initialize logger: " + lastError);
}

} // namespace

std::shared_ptr<spdlog::logger> log::logger_{};

void log::init() {
  if (logger_ != nullptr) {
    return;
  }

  try {
    std::string resolvedPath;
    spdlog::flush_every(std::chrono::seconds(5));
    logger_ = createLogger(resolvedPath);
    logger_->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger_->set_level(spdlog::level::info);
    logger_->flush_on(spdlog::level::warn);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    logger_->info("log file: {}", resolvedPath);
  } catch (const std::exception& e) {
    std::cerr << "create logger failure: " << e.what() << '\n';
    std::abort();
  }
}
