#pragma once

#include "core/ScannerPipeline.h"
#include "dto/ScanResults.h"
#include "dto/ScannerConfig.h"
#include "utils/JSISerializer.h"
#include <DatabaseManager.h>
#include <ReactCommon/CallInvoker.h>
#include <jsi/jsi.h>
#include <memory>
#include <mutex>

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
   * @param jsiRuntime A pointer to the JSI runtime.
   * @param callInvoker A shared pointer to the call invoker for async
   * operations.
   */
  static void
  injectJSIBindings(jsi::Runtime *jsiRuntime,
                    std::shared_ptr<react::CallInvoker> callInvoker);

  // Singleton model instances
  static std::shared_ptr<cardscanner::YoloSegmentationModel> getYoloModel();
  static std::shared_ptr<cardscanner::CardEmbeddingModel> getEmbeddingModel();
  static std::shared_ptr<cardscanner::SetSymbolYoloModel>
  getSetSymbolYoloModel();
  static std::shared_ptr<cardscanner::SetSymbolEmbedder> getSetSymbolEmbedder();

  // Initialize models with configuration
  static void initializeModels();

  // Release models and free resources
  static void releaseModels();

  // Thread-safe accessors for config
  static dto::ScannerConfig getConfig();

private:
  static std::shared_ptr<cardscanner::YoloSegmentationModel> yoloModel_;
  static std::shared_ptr<cardscanner::CardEmbeddingModel> embeddingModel_;
  static std::shared_ptr<cardscanner::SetSymbolYoloModel> setSymbolYoloModel_;
  static std::shared_ptr<cardscanner::SetSymbolEmbedder> setSymbolEmbedder_;
  static std::mutex modelMutex_;
  static dto::ScannerConfig config_;
};

} // namespace rncardscanner
