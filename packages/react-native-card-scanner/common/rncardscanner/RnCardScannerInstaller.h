#pragma once

#include <DatabaseManager.h>
#include <ReactCommon/CallInvoker.h>
#include <jsi/jsi.h>
#include <memory>
#include <mutex>

// Forward declarations
namespace cardscanner {
class YoloSegmentationModel;
class CardEmbeddingModel;
}

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

  // Initialize models with optional default game
  static void initializeModels(const std::string &yoloPath,
                               const std::string &embeddingPath,
                               const std::string &defaultGame = "");

  // Release models and free resources
  static void releaseModels();

  // Thread-safe accessors for current game
  static std::string getCurrentGame();
  static void setCurrentGame(const std::string &gameName);

private:
  static std::shared_ptr<cardscanner::YoloSegmentationModel> yoloModel_;
  static std::shared_ptr<cardscanner::CardEmbeddingModel> embeddingModel_;
  static std::mutex modelMutex_;
  static std::string currentGame_;
};

} // namespace rncardscanner
