#pragma once

#include <sstream>
#include <string>

#include <spdlog/spdlog.h>

namespace pandora {

inline void EnsureLogger() {
  static bool once = [] {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S][%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);
    return true;
  }();
  (void)once;
}

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
