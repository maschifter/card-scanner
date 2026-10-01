#include "ParentWatchdog.h"

#ifndef _WIN32
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>
#endif

namespace cardscanner {
namespace util {

#ifdef _WIN32

void watchParent(std::atomic<bool> &, std::function<void()>) {
  // The module puts this process in a KILL_ON_JOB_CLOSE job object, so the OS
  // reaps it - including when OBS is killed rather than quitting.
}

#else

void watchParent(std::atomic<bool> &running, std::function<void()> onParentGone) {
  const pid_t original = ::getppid();
  if (original <= 1) {
    return; // launched by init, or the parent is already gone
  }

  std::thread([original, &running, onParentGone = std::move(onParentGone)] {
    while (running) {
      // A dead parent leaves us reparented, so the pid changing is the signal.
      if (::getppid() != original) {
        std::cerr << "parent " << original << " exited; shutting down\n";
        if (onParentGone) {
          onParentGone();
        }
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }).detach();
}

#endif

} // namespace util
} // namespace cardscanner
