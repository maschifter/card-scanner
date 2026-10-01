#pragma once

#include <atomic>
#include <functional>

namespace cardscanner {
namespace util {

/**
 * @brief Calls back when the process that launched us goes away.
 *
 * OBS does not call obs_module_unload on quit, so the module's teardown cannot
 * be relied on to stop the server - which would otherwise hold both ports and
 * block the next launch. A no-op on Windows, where the job object covers it.
 */
void watchParent(std::atomic<bool> &running, std::function<void()> onParentGone);

} // namespace util
} // namespace cardscanner
