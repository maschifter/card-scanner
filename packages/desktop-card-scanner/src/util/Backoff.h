#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace cardscanner {
namespace util {

/**
 * @brief Retry delays that double from `initial` up to `max`, slept on a wait
 * that wake() ends at once, so a shutdown never sits out a backoff.
 *
 * Header-only and standard-library-only: the OBS module uses it too.
 */
class Backoff {
public:
  using Duration = std::chrono::milliseconds;

  Backoff(Duration initial, Duration max)
      : initial_(initial), max_(max), next_(initial) {}

  /// Sleeps the current delay, then doubles it up to the max.
  /// @return false when wake() cut the sleep short, or had been called
  bool sleep() {
    const Duration delay = next_;
    next_ = std::min(next_ * 2, max_);
    return sleepFor(delay);
  }

  /// Sleeps `delay` without touching the backoff.
  /// @return false when wake() cut the sleep short, or had been called
  bool sleepFor(Duration delay) {
    std::unique_lock<std::mutex> lock(mutex_);
    return !woken_.wait_for(lock, delay, [this] { return awake_; });
  }

  /// Back to the initial delay, after a success.
  void reset() { next_ = initial_; }

  /// Ends every sleep, current and future, until rearm().
  void wake() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      awake_ = true;
    }
    woken_.notify_all();
  }

  /// Lets sleeps block again, for a restart after wake().
  void rearm() {
    std::lock_guard<std::mutex> lock(mutex_);
    awake_ = false;
    next_ = initial_;
  }

private:
  const Duration initial_;
  const Duration max_;
  Duration next_; ///< The sleeping thread's, or set before it starts.

  std::mutex mutex_;
  std::condition_variable woken_;
  bool awake_ = false;
};

} // namespace util
} // namespace cardscanner
