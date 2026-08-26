#pragma once

#include <ReactCommon/CallInvoker.h>
#include <jsi/jsi.h>
#include <memory>

namespace cardscanner {

// Aliases, not `using namespace facebook` - a using-directive at namespace
// scope in a header leaks onto every includer. Same bug class as the
// ExecuTorch leak removed from the model headers.
namespace jsi = facebook::jsi;
namespace react = facebook::react;

/**
 * @class CardScannerInstaller
 * @brief Installs the JSI bindings for the @cardnexus/card-scanner library.
 *
 * Marshalling only. The config, models and lifetime locking live in
 * ScannerRegistry, which has no React Native dependency.
 */
class CardScannerInstaller {
public:
  /**
   * @brief Injects the JSI bindings into the JavaScript runtime.
   *
   * @param jsiRuntime A pointer to the JSI runtime.
   * @param callInvoker A shared pointer to the call invoker for async
   * operations.
   */
  static void
  injectJSIBindings(jsi::Runtime *jsiRuntime,
                    std::shared_ptr<react::CallInvoker> callInvoker);
};

} // namespace cardscanner
