#pragma once
#include <spdlog/spdlog.h>

#include <memory>

class log
{
    static std::shared_ptr<spdlog::logger> logger_;

public:
    log() = delete;
    ~log() = delete;

    log(const log&) = delete;
    log(log&&) = delete;

    void operator=(const log&) = delete;
    void operator=(log&&) = delete;

public:
    static void init();

    template <typename... Args>
    static void trace(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        logger_->log(spdlog::level::trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        logger_->log(spdlog::level::debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        logger_->log(spdlog::level::info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        logger_->log(spdlog::level::warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        logger_->log(spdlog::level::err, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void critical(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        logger_->log(spdlog::level::critical, fmt, std::forward<Args>(args)...);
    }

    static void shutdown()
    {
        spdlog::shutdown();
        logger_.reset();
    }
};
