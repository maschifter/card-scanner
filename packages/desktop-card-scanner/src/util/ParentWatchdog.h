#pragma once

#include "Backoff.h"

#include <functional>
#include <thread>

namespace cardscanner {
namespace util {

/**
 * @brief Calls back when the process that launched us goes away.
 *
 * OBS does not call obs_module_unload on quit, so the module's teardown cannot
 * be relied on to stop the server - which would otherwise hold both ports and
 * block the next launch. A no-op on Windows, where the job object covers it.
 *
 * Owns its polling thread and joins it on destruction, so the callback never
 * outlives the watchdog.
 */
class ParentWatchdog {
public:
  explicit ParentWatchdog(std::function<void()> onParentGone);
  ~ParentWatchdog();

  ParentWatchdog(const ParentWatchdog &) = delete;
  ParentWatchdog &operator=(const ParentWatchdog &) = delete;

private:
  /// Only its interruptible sleep: the poll interval, cut short on destruction.
  Backoff poll_{std::chrono::milliseconds(500), std::chrono::milliseconds(500)};
  std::thread thread_;
};

} // namespace util
} // namespace cardscanner
