#pragma once

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace pandora {

// Console + synchronous daily-rotating file sink (keep 7 days). Safe to call repeatedly.
inline void InitLogging(const std::filesystem::path& log_file = "logs/pandora.log") {
  static bool once = false;
  if (once) return;
  once = true;

  try {
    if (log_file.has_parent_path()) {
      std::filesystem::create_directories(log_file.parent_path());
    }

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("[%Y-%m-%d %H:%M:%S][%^%l%$] %v");

    // Rotate at 00:00; retain last 7 files (pandora_YYYY-MM-DD.log). Sync sink, not async.
    constexpr int kRotateHour = 0;
    constexpr int kRotateMinute = 0;
    constexpr uint16_t kKeepDays = 7;
    auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_file.string(), kRotateHour,
                                                                   kRotateMinute, false, kKeepDays);
    file->set_pattern("[%Y-%m-%d %H:%M:%S][%l] %v");

    std::vector<spdlog::sink_ptr> sinks{console, file};
    auto logger = std::make_shared<spdlog::logger>("pandora", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
    spdlog::info("logging to console and daily file={} (keep {} days)", log_file.string(), kKeepDays);
  } catch (const std::exception& e) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S][%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);
    spdlog::warn("file logging disabled: {}", e.what());
  }
}

inline void EnsureLogger() { InitLogging(); }

}  // namespace pandora

#define PLOG_INFO(msg)                                                          \
  do {                                                                          \
    pandora::EnsureLogger();                                                    \
    std::ostringstream _plog_ss;                                                \
    _plog_ss << msg;                                                            \
    spdlog::info("{}", _plog_ss.str());                                         \
  } while (0)

#define PLOG_WARN(msg)                                                          \
  do {                                                                          \
    pandora::EnsureLogger();                                                    \
    std::ostringstream _plog_ss;                                                \
    _plog_ss << msg;                                                            \
    spdlog::warn("{}", _plog_ss.str());                                         \
  } while (0)

#define PLOG_ERROR(msg)                                                         \
  do {                                                                          \
    pandora::EnsureLogger();                                                    \
    std::ostringstream _plog_ss;                                                \
    _plog_ss << msg;                                                            \
    spdlog::error("{}", _plog_ss.str());                                        \
  } while (0)
