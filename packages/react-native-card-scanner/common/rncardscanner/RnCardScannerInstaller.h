#pragma once

#include <ReactCommon/CallInvoker.h>
#include <jsi/jsi.h>
#include <memory>

namespace rncardscanner {

using namespace facebook;

/**
 * @class CardScannerInstaller
 * @brief Installs the JSI bindings for the react-native-card-scanner library.
 */
class CardScannerInstaller {
public:
  /**
   * @brief Injects the JSI bindings into the JavaScript runtime.
   *
   * This function creates a single global function in the JS runtime:
   * - multiply(a: number, b: number): number
   *
   * @param jsiRuntime A pointer to the JSI runtime.
   * @param callInvoker A shared pointer to the call invoker for async
   * operations.
   */
  static void
  injectJSIBindings(jsi::Runtime *jsiRuntime,
                    std::shared_ptr<react::CallInvoker> callInvoker);
};

} // namespace rncardscanner
