#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

class log {
public:
  log() = delete;
  ~log() = delete;

  log(const log&) = delete;
  log(log&&) = delete;

  void operator=(const log&) = delete;
  void operator=(log&&) = delete;

  static void init();
  static void shutdown();

  template <typename... Args>
  static void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    write(spdlog::level::trace, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    write(spdlog::level::debug, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    write(spdlog::level::info, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    write(spdlog::level::warn, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    write(spdlog::level::err, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    write(spdlog::level::critical, fmt, std::forward<Args>(args)...);
  }

private:
  static std::shared_ptr<spdlog::logger> acquire_logger();

  template <typename... Args>
  static void write(
      spdlog::level::level_enum level,
      spdlog::format_string_t<Args...> fmt,
      Args&&... args) {
    auto logger = acquire_logger();
    if (!logger) {
      return;
    }
    logger->log(level, fmt, std::forward<Args>(args)...);
  }
};
