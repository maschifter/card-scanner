#include "RnCardScannerInstaller.h"
#include "Constants.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "jsi/Promise.h"
#include "models/CardEmbeddingModel.h"
#include "models/YoloSegmentationModel.h"
#include "models/mtg/SetSymbolEmbedder.h"
#include "models/mtg/SetSymbolYoloModel.h"
#include "utils/FrameExtractor.h"

// New modular architecture

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>

#include "threads/GlobalThreadPool.h"
#include "threads/utils/ThreadUtils.h"

using namespace cardscanner::constants;

#ifdef __ANDROID__
#include <sys/resource.h>
#endif

namespace rncardscanner {

// Initialize static members
std::shared_ptr<cardscanner::YoloSegmentationModel>
    CardScannerInstaller::yoloModel_ = nullptr;
std::shared_ptr<cardscanner::CardEmbeddingModel>
    CardScannerInstaller::embeddingModel_ = nullptr;
std::shared_ptr<cardscanner::SetSymbolYoloModel>
    CardScannerInstaller::setSymbolYoloModel_ = nullptr;
std::shared_ptr<cardscanner::SetSymbolEmbedder>
    CardScannerInstaller::setSymbolEmbedder_ = nullptr;
std::mutex CardScannerInstaller::modelMutex_;
dto::ScannerConfig CardScannerInstaller::config_ = {};

void CardScannerInstaller::initializeModels() {
  std::lock_guard<std::mutex> lock(modelMutex_);

  if (!config_.segmentationModelPath.empty()) {
    yoloModel_ = std::make_shared<cardscanner::YoloSegmentationModel>(
        config_.segmentationModelPath, config_.segmentationThreshold,
        config_.iouThreshold);
  }

  if (!config_.embeddingModelPath.empty()) {
    embeddingModel_ = std::make_shared<cardscanner::CardEmbeddingModel>(
        config_.embeddingModelPath);
  }

  if (config_.mtgConfig.has_value()) {

    if (!config_.mtgConfig->setSymbolDetectionModelPath.empty()) {
      setSymbolYoloModel_ = std::make_shared<cardscanner::SetSymbolYoloModel>(
          config_.mtgConfig->setSymbolDetectionModelPath,
          config_.mtgConfig->detectionThreshold, 0.7f, 384);
    }

    if (!config_.mtgConfig->setSymbolEmbedderModelPath.empty()) {
      setSymbolEmbedder_ = std::make_shared<cardscanner::SetSymbolEmbedder>(
          config_.mtgConfig->setSymbolEmbedderModelPath);
    }

    // Initialize set symbol store in DatabaseManager
    try {
      cardscanner::DatabaseManager::getInstance().getSetSymbolStore();
    } catch (const std::exception &e) {
      std::cerr << "Warning: Could not initialize SetSymbol store: " << e.what()
                << std::endl;
      // We don't throw here, we allow the app to continue; the search will just
      // return empty later.
    }
  }
}

void CardScannerInstaller::releaseModels() {
  std::lock_guard<std::mutex> lock(modelMutex_);

  if (yoloModel_) {
    yoloModel_.reset();
  }

  if (embeddingModel_) {
    embeddingModel_.reset();
  }

  if (setSymbolYoloModel_) {
    setSymbolYoloModel_.reset();
  }

  if (setSymbolEmbedder_) {
    setSymbolEmbedder_.reset();
  }

  cardscanner::DatabaseManager::getInstance().closeSetSymbolStore();
}

std::shared_ptr<cardscanner::YoloSegmentationModel>
CardScannerInstaller::getYoloModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return yoloModel_;
}

std::shared_ptr<cardscanner::CardEmbeddingModel>
CardScannerInstaller::getEmbeddingModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return embeddingModel_;
}

std::shared_ptr<cardscanner::SetSymbolYoloModel>
CardScannerInstaller::getSetSymbolYoloModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return setSymbolYoloModel_;
}

std::shared_ptr<cardscanner::SetSymbolEmbedder>
CardScannerInstaller::getSetSymbolEmbedder() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return setSymbolEmbedder_;
}

dto::ScannerConfig CardScannerInstaller::getConfig() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return config_;
}

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {

  cardscanner::DatabaseManager &dbManager =
      cardscanner::DatabaseManager::getInstance();

  // Create the 'initializeScannerNative' host function (returns Promise)
  auto initializeScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime,
      jsi::PropNameID::forAscii(*jsiRuntime, "initializeScannerNative"), 1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isObject()) {
          throw jsi::JSError(runtime,
                             "initializeScannerNative expects a config object");
        }

        // Parse config object on JS thread
        jsi::Object configObj = args[0].asObject(runtime);

        // Disable OpenCV threading to prevent interference with ExecutorTorch
        cv::setNumThreads(0);

        // Thread-safe config update
        {
          std::lock_guard<std::mutex> lock(modelMutex_);
          config_ = utils::JSISerializer::parseScannerConfig(runtime, configObj);
        }

        // Return a Promise that runs initialization on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager](std::shared_ptr<Promise> promise) {
              // Run initialization on background thread
              std::thread([&dbManager, promise]() {
                try {
                  CardScannerInstaller::initializeModels();

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync([promise]() {
                    jsi::Object result(promise->getRuntime());
                    result.setProperty(promise->getRuntime(), "success",
                                       jsi::Value(true));
                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  // Reject promise on JS thread
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        jsi::Object result(promise->getRuntime());
                        result.setProperty(promise->getRuntime(), "success",
                                           jsi::Value(false));
                        result.setProperty(
                            promise->getRuntime(), "error",
                            jsi::String::createFromUtf8(promise->getRuntime(),
                                                        errorMsg));
                        promise->resolve(std::move(result));
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "initializeScannerNative",
                                   std::move(initializeScannerFunc));

  // Create the 'releaseScanner' host function
  auto releaseScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "releaseScanner"), 0,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        CardScannerInstaller::releaseModels();
        return jsi::Value(true);
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "releaseScanner",
                                   std::move(releaseScannerFunc));

  // Create the 'swapDatabaseNative' host function (returns Promise)
  auto swapDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "swapDatabaseNative"),
      2,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(
              runtime,
              "swapDatabase expects (sourcePath: string, gameName: string)");
        }

        std::string sourcePath = args[0].asString(runtime).utf8(runtime);
        std::string gameName = args[1].asString(runtime).utf8(runtime);

        // Return a Promise that runs database swap on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [sourcePath, gameName,
             &dbManager](std::shared_ptr<Promise> promise) {
              // Run database swap on background thread
              std::thread([sourcePath, gameName, &dbManager, promise]() {
                try {
                  bool success =
                      dbManager.swapDatabaseFile(gameName, sourcePath);

                  if (success) {
                    dbManager.scanForExistingStores();
                  }

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync([promise, success]() {
                    jsi::Object result(promise->getRuntime());
                    result.setProperty(promise->getRuntime(), "success",
                                       jsi::Value(success));
                    if (!success) {
                      result.setProperty(promise->getRuntime(), "error",
                                         jsi::String::createFromUtf8(
                                             promise->getRuntime(),
                                             "Failed to swap database file"));
                    }
                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  // Reject promise on JS thread
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        jsi::Object result(promise->getRuntime());
                        result.setProperty(promise->getRuntime(), "success",
                                           jsi::Value(false));
                        result.setProperty(
                            promise->getRuntime(), "error",
                            jsi::String::createFromUtf8(promise->getRuntime(),
                                                        errorMsg));
                        promise->resolve(std::move(result));
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "swapDatabaseNative",
                                   std::move(swapDatabaseFunc));

  // Create the 'runSegmentationDebug' host function
  // This function is intended for debugging purposes and will be removed later
  auto runSegmentationDebugFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime,
      jsi::PropNameID::forAscii(*jsiRuntime, "runSegmentationDebug"), 2,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(runtime, "runSegmentationDebug expects "
                                      "(imagePath: string, outputDir: string)");
        }

        std::string imagePath = args[0].asString(runtime).utf8(runtime);
        std::string outputDir = args[1].asString(runtime).utf8(runtime);

        try {
          // Strip file:// prefix if present
          const std::string filePrefix = "file://";
          if (imagePath.find(filePrefix) == 0) {
            imagePath = imagePath.substr(filePrefix.length());
          }

          // Get singleton YOLO model
          auto yoloModel = CardScannerInstaller::getYoloModel();
          if (!yoloModel) {
            throw jsi::JSError(
                runtime,
                "YOLO model not initialized. Call initializeScanner() first.");
          }

          // Load image
          cv::Mat image = cv::imread(imagePath);
          if (image.empty()) {
            throw jsi::JSError(runtime,
                               "Failed to load image from: " + imagePath);
          }

          // Run segmentation
          auto segResult = yoloModel->segment(image, true);

          std::string tempDir = outputDir;
          if (tempDir.find(filePrefix) == 0) {
            tempDir = tempDir.substr(filePrefix.length());
          }

          // Ensure output directory ends with /
          if (!tempDir.empty() && tempDir.back() != '/') {
            tempDir += '/';
          }

          std::string outputPath = tempDir + "yolo_result.jpg";
          bool vizSaved = cv::imwrite(outputPath, segResult.visualizedImage);

          // Add file:// prefix for React Native Image component
          std::string outputUri = "file://" + outputPath;

          jsi::Array dewarpedPaths(runtime, segResult.detections.size());
          for (size_t i = 0; i < segResult.detections.size(); i++) {
            if (!segResult.detections[i].dewarpedCard.empty()) {
              std::string dewarpPath =
                  tempDir + "card_" + std::to_string(i) + ".jpg";
              bool saved =
                  cv::imwrite(dewarpPath, segResult.detections[i].dewarpedCard);

              // Add file:// prefix for React Native
              std::string dewarpUri = "file://" + dewarpPath;
              dewarpedPaths.setValueAtIndex(
                  runtime, i, jsi::String::createFromUtf8(runtime, dewarpUri));
            } else {
              dewarpedPaths.setValueAtIndex(runtime, i, jsi::Value::null());
            }
          }

          // Build result object
          jsi::Object result(runtime);
          result.setProperty(
              runtime, "cardCount",
              jsi::Value(static_cast<int>(segResult.detections.size())));
          result.setProperty(runtime, "dewarpedCardPaths", dewarpedPaths);
          result.setProperty(runtime, "visualizedImagePath",
                             jsi::String::createFromUtf8(runtime, outputUri));

          // Convert detections
          jsi::Array detections(runtime, segResult.detections.size());
          for (size_t i = 0; i < segResult.detections.size(); i++) {
            const auto &det = segResult.detections[i];
            jsi::Object jsDetection(runtime);

            jsi::Object box(runtime);
            box.setProperty(runtime, "x1", jsi::Value(det.box.x1));
            box.setProperty(runtime, "y1", jsi::Value(det.box.y1));
            box.setProperty(runtime, "x2", jsi::Value(det.box.x2));
            box.setProperty(runtime, "y2", jsi::Value(det.box.y2));
            box.setProperty(runtime, "conf", jsi::Value(det.box.conf));
            jsDetection.setProperty(runtime, "box", box);

            detections.setValueAtIndex(runtime, i, jsDetection);
          }
          result.setProperty(runtime, "detections", detections);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Segmentation failed: ") + e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "runSegmentationDebug",
                                   std::move(runSegmentationDebugFunc));

  auto listGamesFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "listAvailableGames"),
      0,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        // Return a Promise that scans for games on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager](std::shared_ptr<Promise> promise) {
              std::thread([&dbManager, promise]() {
                try {
                  dbManager.scanForExistingStores();
                  std::set<std::string> games = dbManager.getKnownGames();

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync([promise, games,
                                                          &dbManager]() {
                    jsi::Array result(promise->getRuntime(), games.size());
                    size_t i = 0;

                    for (const auto &gameName : games) {
                      std::string dirPath = dbManager.getStorePath(gameName);

                      jsi::Object gameObj(promise->getRuntime());
                      gameObj.setProperty(promise->getRuntime(), "gameName",
                                          jsi::String::createFromUtf8(
                                              promise->getRuntime(), gameName));
                      gameObj.setProperty(promise->getRuntime(), "path",
                                          jsi::String::createFromUtf8(
                                              promise->getRuntime(), dirPath));

                      result.setValueAtIndex(promise->getRuntime(), i++,
                                             std::move(gameObj));
                    }

                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        promise->reject(errorMsg);
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "listAvailableGames",
                                   std::move(listGamesFunc));

  auto getCardCountFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "getCardCount"), 1,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "getCardCount expects one string argument "
                             "(gameName)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);

        try {
          ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);

          if (!db) {
            return jsi::Value(0.0);
          }

          uint64_t cardCount = db->get_card_count();

          return jsi::Value(static_cast<double>(cardCount));

        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Failed to get card count for ") +
                                 gameName + ": " + e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "getCardCount",
                                   std::move(getCardCountFunc));

  auto startScanningFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forUtf8(*jsiRuntime, "scanFramePlugin"), 1,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisArg,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1) {
          throw jsi::JSError(runtime, "scanFramePlugin expects at least 1 "
                                      "argument: (frame)");
        }

        try {
          // 1. Extract frame from JSI
          auto frameObj = args[0].asObject(runtime);
          cv::Mat frameImage =
              cardscanner::FrameExtractor::extractFrame(runtime, frameObj);

          // 2. Get config
          auto scannerConfig = CardScannerInstaller::getConfig();

          // 3. Get all models
          auto yoloModel = CardScannerInstaller::getYoloModel();
          auto embeddingModel = CardScannerInstaller::getEmbeddingModel();
          auto setSymbolYolo = CardScannerInstaller::getSetSymbolYoloModel();
          auto setSymbolEmbedder = CardScannerInstaller::getSetSymbolEmbedder();

          if (!yoloModel || !embeddingModel) {
            throw jsi::JSError(
                runtime,
                "Models not initialized. Call initializeScanner() first.");
          }

          // 4. Run pipeline
          auto result = core::ScannerPipeline::processFrame(
              frameImage, scannerConfig, dbManager, yoloModel.get(),
              embeddingModel.get(), setSymbolYolo.get(),
              setSymbolEmbedder.get());

          // 5. Serialize to JSI
          return utils::JSISerializer::serializeScanResult(runtime, result);

        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Frame processing failed: ") +
                                          e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "scanFramePlugin",
                                   std::move(startScanningFunc));

  threads::utils::unsafeSetupThreadPool();
  threads::GlobalThreadPool::initialize();
}

} // namespace rncardscanner
