#include "RnCardScannerInstaller.h"
#include "CardScanner.h"

#include <iostream>
#include <string>

namespace rncardscanner {

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {
  std::cout << "Injecting JSI bindings for CardScanner" << std::endl;

  // Create the 'runInference' host function
  auto runInferenceFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "runInference"), 1,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 1 || !args[0].isString()) {
          throw jsi::JSError(
              runtime,
              "runInference expects one string argument (modelPath)");
        }

        std::string modelPath = args[0].asString(runtime).utf8(runtime);

        try {
          auto inferenceResult =
              cardscanner::CardScanner::runInference(modelPath);

          // Create result object with outputShape and inferenceTimeMs
          jsi::Object result(runtime);

          // Add outputShape array
          jsi::Array outputShape(runtime, inferenceResult.outputShape.size());
          for (size_t i = 0; i < inferenceResult.outputShape.size(); i++) {
            outputShape.setValueAtIndex(runtime, i, jsi::Value(inferenceResult.outputShape[i]));
          }
          result.setProperty(runtime, "outputShape", outputShape);

          // Add inference time
          result.setProperty(runtime, "inferenceTimeMs", jsi::Value(inferenceResult.inferenceTimeMs));

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Inference failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runInference",
                                   std::move(runInferenceFunc));
}

} // namespace rncardscanner
