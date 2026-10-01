#include "Log.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>

namespace cardscanner {
namespace util {

void logLine(std::string_view tag, std::string_view message) {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count() %
                  1000;

  std::tm local{};
#ifdef _WIN32
  ::localtime_s(&local, &time);
#else
  ::localtime_r(&time, &local);
#endif

  char stamp[16];
  std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d", local.tm_hour,
                local.tm_min, local.tm_sec, int(ms));

  std::string line;
  line.reserve(sizeof(stamp) + tag.size() + 8 + message.size());
  line += stamp;
  line += " [";
  line += tag;
  line += "] ";
  line += message;
  line += '\n';

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  std::cerr << line;
}

} // namespace util
} // namespace cardscanner
