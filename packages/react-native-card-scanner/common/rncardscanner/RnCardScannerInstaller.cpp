#include "RnCardScannerInstaller.h"
#include "CardScanner.h"

#include <iostream>
#include <string>

namespace rncardscanner {

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {
  std::cout << "Injecting JSI bindings for CardScanner" << std::endl;

  // Create the 'multiply' host function
  auto multiplyFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "multiply"), 1,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "multiply expects one string argument (modelSource)");
        }

        std::string modelSource = args[0].asString(runtime).utf8(runtime);

        double result = cardscanner::CardScanner::multiply(modelSource);

        return jsi::Value(result);
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "multiply",
                                   std::move(multiplyFunc));
}

} // namespace rncardscanner
