#pragma once

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

class log {
public:
  log() = delete;
  ~log() = delete;

  log(const log &) = delete;
  log(log &&) = delete;

  void operator=(const log &) = delete;
  void operator=(log &&) = delete;

  static void init();
  static void shutdown();

  template <typename... Args>
  static void trace(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    write(spdlog::level::trace, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    write(spdlog::level::debug, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void info(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    write(spdlog::level::info, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    write(spdlog::level::warn, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    write(spdlog::level::err, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void critical(spdlog::format_string_t<Args...> fmt, Args &&...args) {
    write(spdlog::level::critical, fmt, std::forward<Args>(args)...);
  }

private:
  static std::shared_ptr<spdlog::logger> &logger_storage();
  static void ensure_initialized();

  template <typename... Args>
  static void write(spdlog::level::level_enum level,
                    spdlog::format_string_t<Args...> fmt, Args &&...args) {
    ensure_initialized();
    auto &logger = logger_storage();
    if (!logger) {
      return;
    }
    logger->log(level, fmt, std::forward<Args>(args)...);
  }
};

#include "detail/runtime/log_impl.h"
