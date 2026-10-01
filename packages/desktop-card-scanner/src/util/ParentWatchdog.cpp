#include "ParentWatchdog.h"

#ifndef _WIN32
#include <Log.h>

#include <unistd.h>
#endif

namespace cardscanner {
namespace util {

ParentWatchdog::ParentWatchdog(std::function<void()> onParentGone) {
#ifdef _WIN32
  // The module puts this process in a KILL_ON_JOB_CLOSE job object, so the OS
  // reaps it - including when OBS is killed rather than quitting.
  (void)onParentGone;
#else
  const pid_t original = ::getppid();
  if (original <= 1) {
    return; // launched by init, or the parent is already gone
  }

  thread_ = std::thread([this, original,
                         onParentGone = std::move(onParentGone)] {
    // A dead parent leaves us reparented, so the pid changing is the signal.
    while (poll_.sleep()) {
      if (::getppid() != original) {
        log(LOG_LEVEL::Info, "[Watchdog]", "parent", original,
            "exited; shutting down");
        if (onParentGone) {
          onParentGone();
        }
        return;
      }
    }
  });
#endif
}

ParentWatchdog::~ParentWatchdog() {
  poll_.wake();
  if (thread_.joinable()) {
    thread_.join();
  }
}

} // namespace util
} // namespace cardscanner
