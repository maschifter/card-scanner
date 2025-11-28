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
} // namespace cardscanner

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

  // Scanner configuration struct
  struct ScannerConfig {
    std::string yoloPath;
    std::string embeddingPath;
    std::string gameName;
    std::string scanMode; // "single" or "multiple"
    float segmentationThreshold;
    float iouThreshold;
    float confidenceThreshold;
    int maxMatches;
    int searchCandidates;
    bool captureImage;
  };

  // Initialize models with configuration
  static void initializeModels(const ScannerConfig &config);

  // Release models and free resources
  static void releaseModels();

  // Thread-safe accessors for current game and config
  static std::string getCurrentGame();
  static void setCurrentGame(const std::string &gameName);
  static ScannerConfig getConfig();

private:
  static std::shared_ptr<cardscanner::YoloSegmentationModel> yoloModel_;
  static std::shared_ptr<cardscanner::CardEmbeddingModel> embeddingModel_;
  static std::mutex modelMutex_;
  static std::string currentGame_;
  static ScannerConfig config_;
};

} // namespace rncardscanner
