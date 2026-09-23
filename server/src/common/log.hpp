#pragma once

#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace pandora {

inline std::mutex& LogMutex() {
  static std::mutex m;
  return m;
}

inline std::string NowTs() {
  using namespace std::chrono;
  const auto t = system_clock::to_time_t(system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%F %T", &tm);
  return buf;
}

#define PLOG_INFO(msg)                                                        \
  do {                                                                        \
    std::lock_guard<std::mutex> _lk(pandora::LogMutex());                     \
    std::cout << "[" << pandora::NowTs() << "][INFO] " << msg << std::endl; \
  } while (0)

#define PLOG_WARN(msg)                                                        \
  do {                                                                        \
    std::lock_guard<std::mutex> _lk(pandora::LogMutex());                     \
    std::cout << "[" << pandora::NowTs() << "][WARN] " << msg << std::endl; \
  } while (0)

#define PLOG_ERROR(msg)                                                        \
  do {                                                                         \
    std::lock_guard<std::mutex> _lk(pandora::LogMutex());                      \
    std::cerr << "[" << pandora::NowTs() << "][ERROR] " << msg << std::endl; \
  } while (0)

}  // namespace pandora
