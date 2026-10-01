#pragma once
#include <concepts>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#ifdef __ANDROID__
#include <android/log.h>
#endif
#ifdef __APPLE__
#include <os/log.h>
#endif
// Set by the desktop build: one timestamped stderr line instead of os_log.
#ifdef CARDSCANNER_LOG_STDERR
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#endif

namespace low_level_log_implementation {

namespace concepts {
template <typename T>
concept Streamable = requires(std::ostream &os, const T &t) {
  { os << t } -> std::convertible_to<std::ostream &>;
};

template <typename T>
concept Iterable = requires(const T &t) {
  { std::begin(t) } -> std::input_or_output_iterator;
  { std::end(t) } -> std::input_or_output_iterator;
};
} // namespace concepts

template <typename T>
  requires concepts::Streamable<T>
void printElement(std::ostream &os, const T &value) {
  os << value;
}

template <std::size_t N>
void printElement(std::ostream &os, const char (&array)[N]) {
  // Treats the input as a string up to length N, drop null termination
  if (N > 1) {
    os << std::string_view(array, N - 1);
  }
}

/// Containers print as "[a, b, c]", recursively.
template <typename T>
  requires concepts::Iterable<T> && (!concepts::Streamable<T>)
void printElement(std::ostream &os, const T &container) {
  os << "[";
  auto it = std::begin(container);
  if (it != std::end(container)) {
    printElement(os, *it++);
    for (; it != std::end(container); ++it) {
      os << ", ";
      printElement(os, *it);
    }
  }
  os << "]";
}

} // namespace low_level_log_implementation

namespace cardscanner {

/**
 * @enum LogLevel
 * @brief Represents various levels of logging severity.
 *
 * This `enum class` is used to specify the severity of a log message. This
 * helps in filtering logs according to their importance and can be crucial for
 * debugging and monitoring applications.
 */
enum class LOG_LEVEL : uint8_t {
  Info,  /**< Informational messages that highlight the progress of the
            application. */
  Error, /**< Error events of considerable importance that will prevent normal
            program execution. */
  Debug  /**< Detailed information, typically of interest only when diagnosing
            problems. */
};

namespace high_level_log_implementation {

#ifdef __ANDROID__
inline android_LogPriority androidLogLevel(LOG_LEVEL logLevel) {
  switch (logLevel) {
  case LOG_LEVEL::Info:
    return ANDROID_LOG_INFO;
  case LOG_LEVEL::Error:
    return ANDROID_LOG_ERROR;
  case LOG_LEVEL::Debug:
    return ANDROID_LOG_DEBUG;
  default:
    return ANDROID_LOG_DEFAULT;
  }
}

inline void handleAndroidLog(LOG_LEVEL logLevel, const char *buffer) {
  __android_log_print(androidLogLevel(logLevel), "CardScanner", "%s", buffer);
}
#endif

#ifdef __APPLE__
inline void handleIosLog(LOG_LEVEL logLevel, const char *buffer) {
  switch (logLevel) {
  case LOG_LEVEL::Info:
    os_log(OS_LOG_DEFAULT, "%{public}s", buffer);
    return;
  case LOG_LEVEL::Error:
    os_log_error(OS_LOG_DEFAULT, "%{public}s", buffer);
    return;
  case LOG_LEVEL::Debug:
    os_log_debug(OS_LOG_DEFAULT, "%{public}s", buffer);
    return;
  }
}
#endif

#ifdef CARDSCANNER_LOG_STDERR
inline std::string_view levelName(LOG_LEVEL level) {
  switch (level) {
  case LOG_LEVEL::Debug:
    return "debug";
  case LOG_LEVEL::Info:
    return "info";
  case LOG_LEVEL::Error:
    return "error";
  }
  return "info";
}

/// One serialised line on stderr: "HH:MM:SS.mmm [info] message".
inline void handleStderrLog(LOG_LEVEL level, std::string_view message) {
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
  line.reserve(sizeof(stamp) + 8 + message.size());
  line += stamp;
  line += " [";
  line += levelName(level);
  line += "] ";
  line += message;
  line += '\n';

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  std::cerr << line;
}
#endif

inline std::string getBuffer(const std::string &logMessage,
                             std::size_t maxLogMessageSize) {
  if (logMessage.size() > maxLogMessageSize) {
    return logMessage.substr(0, maxLogMessageSize) + "...";
  }
  return logMessage;
}

inline std::ostringstream createConfiguredOutputStream() {
  std::ostringstream oss;
  oss << std::boolalpha;
  return oss;
}

} // namespace high_level_log_implementation

/**
 * @brief Logs given data on a console
 * @details
 * The function takes a logging level and any mix of:
 * - Every data type that implements `operator<<` for `std::ostream`
 * - Containers of those, printed as `[a, b, c]`, nested to any depth
 *
 * Any other type is a compile error.
 *
 * You can manipulate size of the log message. By default it is set to 1024
 * characters. To change this, specify the template argument like so:
 * @code{.cpp}
 * log<2048>(LOG_LEVEL::Info, longMsg);
 * @endcode
 * @param logLevel logging level - one of `LOG_LEVEL` enum class value: `Info`,
 * `Error`, and `Debug`.
 * @tparam Args Data to be logged.
 * @tparam MaxLogSize Maximal size of log in characters.
 * @par Returns
 *    Nothing.
 */
template <std::size_t MaxLogSize = 1024, typename... Args>
void log(LOG_LEVEL logLevel, Args &&...args) {
#if defined(CARDSCANNER_LOG_STDERR) && defined(NDEBUG)
  if (logLevel == LOG_LEVEL::Debug) {
    return;
  }
#endif
  auto oss = high_level_log_implementation::createConfiguredOutputStream();
  auto space = [&oss](auto &&arg) {
    low_level_log_implementation::printElement(
        oss, std::forward<decltype(arg)>(arg));
    oss << ' ';
  };

  (..., space(std::forward<Args>(args)));

  // Remove the extra space after the last element
  std::string output = oss.str();
  if (!output.empty()) {
    output.pop_back();
  }

  const auto buffer =
      high_level_log_implementation::getBuffer(output, MaxLogSize);

#ifdef __ANDROID__
  high_level_log_implementation::handleAndroidLog(logLevel, buffer.c_str());
#elif defined(CARDSCANNER_LOG_STDERR)
  high_level_log_implementation::handleStderrLog(logLevel, buffer);
#elif defined(__APPLE__)
  high_level_log_implementation::handleIosLog(logLevel, buffer.c_str());
#else
  // Default log to cout if none of the above platforms
  std::cout << buffer << '\n';
#endif
}

} // namespace cardscanner
