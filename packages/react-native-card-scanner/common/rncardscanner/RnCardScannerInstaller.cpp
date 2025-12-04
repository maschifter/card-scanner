#include "RnCardScannerInstaller.h"
#include "Constants.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "database/SetSymbolDatabase.h"
#include "host_objects/JsiConversions.h"
#include "jsi/Promise.h"
#include "models/CardEmbeddingModel.h"
#include "models/YoloSegmentationModel.h"
#include "models/mtg/SetSymbolEmbedder.h"
#include "models/mtg/SetSymbolYoloModel.h"
#include "utils/FrameExtractor.h"

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>

#include "threads/GlobalThreadPool.h"
#include "threads/utils/ThreadUtils.h"

using namespace cardscanner::constants;
using ScannerConfig = rncardscanner::CardScannerInstaller::ScannerConfig;

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
std::shared_ptr<cardscanner::SetSymbolDatabase>
    CardScannerInstaller::setSymbolDatabase_ = nullptr;
std::mutex CardScannerInstaller::modelMutex_;
CardScannerInstaller::ScannerConfig CardScannerInstaller::config_ = {};

void CardScannerInstaller::initializeModels(const ScannerConfig &config) {
  std::lock_guard<std::mutex> lock(modelMutex_);

  // Store config
  config_ = config;

  if (!config.yoloPath.empty()) {
    yoloModel_ = std::make_shared<cardscanner::YoloSegmentationModel>(
        config.yoloPath, config.segmentationThreshold, config.iouThreshold);
  }

  if (!config.embeddingPath.empty()) {
    embeddingModel_ =
        std::make_shared<cardscanner::CardEmbeddingModel>(config.embeddingPath);
  }

  // Initialize set symbol models if enabled
  if (config.enableSetSymbolDetection) {
    if (!config.setSymbolYoloPath.empty()) {
      // Use 384x384 for set symbol detection (matches trained model)
      float detectionThreshold = config.setSymbolDetectionThreshold > 0.0f
                                     ? config.setSymbolDetectionThreshold
                                     : 0.3f;
      setSymbolYoloModel_ = std::make_shared<cardscanner::SetSymbolYoloModel>(
          config.setSymbolYoloPath, detectionThreshold, 0.7f, 384);
    }

    if (!config.setSymbolEmbedderPath.empty()) {
      setSymbolEmbedder_ = std::make_shared<cardscanner::SetSymbolEmbedder>(
          config.setSymbolEmbedderPath);
    }

    setSymbolDatabase_ = std::make_shared<cardscanner::SetSymbolDatabase>();

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

  if (setSymbolDatabase_) {
    setSymbolDatabase_.reset();
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

std::shared_ptr<cardscanner::SetSymbolDatabase>
CardScannerInstaller::getSetSymbolDatabase() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return setSymbolDatabase_;
}

CardScannerInstaller::ScannerConfig CardScannerInstaller::getConfig() {
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

        ScannerConfig config;
        config.yoloPath =
            configObj.getProperty(runtime, "segmentationModelPath")
                .asString(runtime)
                .utf8(runtime);
        config.embeddingPath =
            configObj.getProperty(runtime, "embeddingModelPath")
                .asString(runtime)
                .utf8(runtime);
        config.scanMode = configObj.getProperty(runtime, "scanMode")
                              .asString(runtime)
                              .utf8(runtime);
        config.segmentationThreshold =
            configObj.getProperty(runtime, "segmentationThreshold").asNumber();
        config.iouThreshold =
            configObj.getProperty(runtime, "iouThreshold").asNumber();
        config.confidenceThreshold =
            configObj.getProperty(runtime, "confidenceThreshold").asNumber();
        config.maxMatches = static_cast<int>(
            configObj.getProperty(runtime, "maxMatches").asNumber());
        config.searchCandidates = static_cast<int>(
            configObj.getProperty(runtime, "searchCandidates").asNumber());

        // Optional: captureImage
        auto captureImageProp = configObj.getProperty(runtime, "captureImage");
        config.captureImage =
            captureImageProp.isBool() ? captureImageProp.asBool() : false;

        // Game-specific detection configs
        auto gameSpecificProp =
            configObj.getProperty(runtime, "gameSpecificConfig");
        if (!gameSpecificProp.isUndefined() && gameSpecificProp.isObject()) {
          jsi::Object gameSpecificObj = gameSpecificProp.asObject(runtime);

          // MTG-specific configs
          auto mtgProp = gameSpecificObj.getProperty(runtime, "mtg");
          if (!mtgProp.isUndefined() && mtgProp.isObject()) {
            jsi::Object mtgObj = mtgProp.asObject(runtime);

            // MTG Set Symbol Detection
            auto setSymbolProp =
                mtgObj.getProperty(runtime, "setSymbolDetection");
            if (!setSymbolProp.isUndefined() && setSymbolProp.isObject()) {
              config.enableSetSymbolDetection = true;
              jsi::Object setSymbolObj = setSymbolProp.asObject(runtime);

              auto detectionModelProp =
                  setSymbolObj.getProperty(runtime, "detectionModelPath");
              if (!detectionModelProp.isUndefined() &&
                  detectionModelProp.isString()) {
                config.setSymbolYoloPath =
                    detectionModelProp.asString(runtime).utf8(runtime);
              }

              auto embeddingModelProp =
                  setSymbolObj.getProperty(runtime, "embeddingModelPath");
              if (!embeddingModelProp.isUndefined() &&
                  embeddingModelProp.isString()) {
                config.setSymbolEmbedderPath =
                    embeddingModelProp.asString(runtime).utf8(runtime);
              }

              auto detectionThresholdProp =
                  setSymbolObj.getProperty(runtime, "detectionThreshold");
              if (!detectionThresholdProp.isUndefined() &&
                  detectionThresholdProp.isNumber()) {
                config.setSymbolDetectionThreshold =
                    detectionThresholdProp.asNumber();
              }

              auto confidenceThresholdProp =
                  setSymbolObj.getProperty(runtime, "confidenceThreshold");
              if (!confidenceThresholdProp.isUndefined() &&
                  confidenceThresholdProp.isNumber()) {
                config.setSymbolConfidenceThreshold =
                    confidenceThresholdProp.asNumber();
              }
            }
          }

          // Future: Pokemon-specific configs
          // auto pokemonProp = gameSpecificObj.getProperty(runtime, "pokemon");
          // ...
        }

        // Return a Promise that runs initialization on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [config, &dbManager](std::shared_ptr<Promise> promise) {
              // Run initialization on background thread
              std::thread([config, &dbManager, promise]() {
                try {
                  // Initialize models with config (this may take time)
                  CardScannerInstaller::initializeModels(config);

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

  auto closeStoreFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "closeGameStore"), 1,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 1 || !args[0].isString()) {
          throw jsi::JSError(
              runtime, "closeGameStore expects one string argument (gameName)");
        }
        std::string gameName = args[0].asString(runtime).utf8(runtime);

        dbManager.closeStore(gameName);
        return jsi::Value(true);
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "closeGameStore",
                                   std::move(closeStoreFunc));

  // Create the 'runSegmentationDebug' host function
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
          auto segResult = yoloModel->segment(image);

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
          result.setProperty(runtime, "totalMs",
                             jsi::Value(segResult.performance.totalTimeMs));
          result.setProperty(
              runtime, "preprocessingMs",
              jsi::Value(segResult.performance.preprocessingTimeMs));
          result.setProperty(runtime, "dewarpedCardPaths", dewarpedPaths);
          result.setProperty(runtime, "inferenceMs",
                             jsi::Value(segResult.performance.inferenceTimeMs));
          result.setProperty(
              runtime, "postprocessingMs",
              jsi::Value(segResult.performance.postprocessingTimeMs));
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
          auto startTotal = std::chrono::high_resolution_clock::now();

          // Get config (thread-safe)
          auto config = CardScannerInstaller::getConfig();
          // Get the Frame HostObject (first argument)
          auto frameObj = args[0].asObject(runtime);

          cv::Mat frameImage;
          frameImage =
              cardscanner::FrameExtractor::extractFrame(runtime, frameObj);

          // Get singleton YOLO model
          auto yoloModel = CardScannerInstaller::getYoloModel();
          if (!yoloModel) {
            throw jsi::JSError(
                runtime,
                "YOLO model not initialized. Call initializeScanner() first.");
          }

          auto segResult = yoloModel->segment(frameImage);

          // Apply scanMode: keep only highest confidence if "single"
          if (config.scanMode == "single" && !segResult.detections.empty()) {
            auto maxConfDet = std::max_element(
                segResult.detections.begin(), segResult.detections.end(),
                [](const auto &a, const auto &b) {
                  return a.box.conf < b.box.conf;
                });
            segResult.detections = {*maxConfDet};
          }

          // If recognition is enabled, run embedding extraction and database
          // search
          std::vector<std::vector<CardSearchResult>> cardMatches;
          std::vector<std::string> croppedImagePaths;

          // Set symbol detection results (parallel to cardMatches)
          struct SetSymbolInfo {
            std::string setCode;
            std::string setName;
            float similarity;
            std::string croppedImagePath;
          };
          std::vector<SetSymbolInfo> setSymbolInfos;

          cardscanner::CardEmbeddingResult embeddingResult;

          if (!segResult.detections.empty()) {
            auto startRecognition = std::chrono::high_resolution_clock::now();

            // Process each detection
            for (const auto &det : segResult.detections) {
              try {
                cv::Mat cardImg;

                // Use dewarped card if available, otherwise crop from bbox
                if (!det.dewarpedCard.empty()) {
                  cardImg = det.dewarpedCard;
                } else {
                  // Crop card from frame using bounding box
                  int ix1 = std::max(0, static_cast<int>(det.box.x1));
                  int iy1 = std::max(0, static_cast<int>(det.box.y1));
                  int ix2 =
                      std::min(frameImage.cols, static_cast<int>(det.box.x2));
                  int iy2 =
                      std::min(frameImage.rows, static_cast<int>(det.box.y2));

                  if (ix2 > ix1 && iy2 > iy1) {
                    cv::Rect roi(ix1, iy1, ix2 - ix1, iy2 - iy1);
                    cardImg = frameImage(roi).clone();
                  }
                }

                if (!cardImg.empty()) {
                  // Save cropped image if captureImage is enabled
                  std::string imagePath = "";
                  if (config.captureImage) {
                    try {
                      // Generate unique filename with timestamp
                      auto now = std::chrono::system_clock::now();
                      auto timestamp =
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();

                      std::string filename =
                          "card_" + std::to_string(timestamp) + "_" +
                          std::to_string(cardMatches.size()) + ".jpg";

                      // Get cache directory path (use db path as base)
                      std::string cacheDir = pathprovider::get_db_path();
                      imagePath = cacheDir + "/" + filename;

                      // Convert RGB to BGR for correct color display
                      cv::Mat cardImgBGR;
                      cv::cvtColor(cardImg, cardImgBGR, cv::COLOR_RGB2BGR);

                      // Save image as JPEG
                      std::vector<int> compression_params;
                      compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
                      compression_params.push_back(90); // Quality 90%

                      bool success = cv::imwrite(imagePath, cardImgBGR,
                                                 compression_params);
                      if (!success) {
                        imagePath = ""; // Failed to save
                      }
                    } catch (const std::exception &e) {
                      // Failed to save image, continue without it
                      imagePath = "";
                    }
                  }
                  croppedImagePaths.push_back(imagePath);

                  // Get singleton embedding model
                  auto embeddingModel =
                      CardScannerInstaller::getEmbeddingModel();
                  if (!embeddingModel) {
                    throw jsi::JSError(runtime,
                                       "Embedding model not initialized. Call "
                                       "initializeScanner() first.");
                  }

                  // Compute embedding once for this card
                  embeddingResult = embeddingModel->computeEmbedding(cardImg);

                  // Initialize set symbol variables (will be populated after
                  // game determination)
                  std::string setCode = "";
                  std::string setName = "";
                  float setSimilarity = 0.0f;
                  std::string setSymbolImagePath = "";

                  // Store card image for potential set symbol detection later
                  cv::Mat savedCardImg = cardImg.clone();

                  // Check if set symbol models are available
                  auto setSymbolYolo =
                      CardScannerInstaller::getSetSymbolYoloModel();
                  auto setSymbolEmbedder =
                      CardScannerInstaller::getSetSymbolEmbedder();
                  auto setSymbolDb =
                      CardScannerInstaller::getSetSymbolDatabase();

                  bool hasSetSymbolModels =
                      (setSymbolYolo && setSymbolEmbedder && setSymbolDb);

                  // Multi-game search: Get top predictions from YOLO with min
                  // confidence filter
                  std::vector<std::string> topGames;
                  const float MIN_YOLO_CONFIDENCE =
                      0.1; // Filter out low-confidence predictions

                  for (const auto &[gameName, conf] : det.topGamePredictions) {
                    if (conf >= MIN_YOLO_CONFIDENCE) {
                      topGames.push_back(gameName);
                      if (topGames.size() >= 3)
                        break;
                    }
                  }

                  // Adaptive search strategy: search top prediction first
                  std::vector<CardSearchResult> allResults;
                  bool shouldSearchMore = false;

                  for (size_t i = 0; i < topGames.size(); i++) {
                    const auto &gameToSearch = topGames[i];

                    try {
                      ObjectBoxDB *gameDb =
                          dbManager.getOrCreateStore(gameToSearch);
                      if (!gameDb)
                        continue;

                      // Search in this game's database
                      auto gameResults = gameDb->search_similar_cards(
                          embeddingResult.embedding, config.searchCandidates);

                      // Tag results with game name
                      for (auto &result : gameResults) {
                        result.gameName = gameToSearch;
                        allResults.push_back(result);
                      }

                      // Optimization: only search more games if first search is
                      // uncertain
                      if (i == 0 && !gameResults.empty()) {
                        float topScore = gameResults[0].score;

                        // Search additional games if:
                        // 1. Top score below confidence threshold OR
                        // 2. Multiple high-confidence games predicted by YOLO
                        if (topScore < config.confidenceThreshold + 0.1 ||
                            topGames.size() > 1) {
                          shouldSearchMore = true;
                        } else {
                          // High confidence match in first game, skip remaining
                          // searches
                          break;
                        }
                      }

                      // After first search, only continue if needed
                      if (i > 0 && !shouldSearchMore) {
                        break;
                      }

                    } catch (const std::exception &e) {
                      // Skip failed game database
                      continue;
                    }
                  }

                  // Sort all results by score (descending)
                  std::sort(
                      allResults.begin(), allResults.end(),
                      [](const CardSearchResult &a, const CardSearchResult &b) {
                        return a.score > b.score;
                      });

                  // Find the best match game (highest similarity score)
                  std::string bestMatchGame = "";
                  if (!allResults.empty() &&
                      allResults[0].score >= config.confidenceThreshold) {
                    bestMatchGame = allResults[0].gameName;
                  }

                  // Filter: keep only cards from best match game
                  std::vector<CardSearchResult> filteredMatches;
                  for (const auto &match : allResults) {
                    // Only include cards from best game that pass threshold
                    if (!bestMatchGame.empty() &&
                        match.gameName == bestMatchGame &&
                        match.score >= config.confidenceThreshold) {
                      filteredMatches.push_back(match);
                      if (filteredMatches.size() >=
                          static_cast<size_t>(config.maxMatches)) {
                        break;
                      }
                    }
                  }

                  cardMatches.push_back(filteredMatches);

                  // Set symbol detection: ONLY run if best match is MTG
                  if (hasSetSymbolModels && !filteredMatches.empty()) {
                    bool isMTG = (filteredMatches[0].gameName == "mtg");

                    if (isMTG) {
                      try {
                        // Detect set symbol location on the card
                        auto symbolDetection =
                            setSymbolYolo->detect(savedCardImg);

                        if (!symbolDetection.detections.empty()) {
                          const auto &symbolBBox =
                              symbolDetection.detections[0];

                          // Crop set symbol from cardImg for embedding
                          int sx1 =
                              std::max(0, static_cast<int>(symbolBBox.x1));
                          int sy1 =
                              std::max(0, static_cast<int>(symbolBBox.y1));
                          int sx2 = std::min(savedCardImg.cols,
                                             static_cast<int>(symbolBBox.x2));
                          int sy2 = std::min(savedCardImg.rows,
                                             static_cast<int>(symbolBBox.y2));

                          if (sx2 > sx1 && sy2 > sy1) {
                            cv::Rect symbolRoi(sx1, sy1, sx2 - sx1, sy2 - sy1);
                            cv::Mat symbolImg = savedCardImg(symbolRoi).clone();

                            // Compute set symbol embedding and search
                            auto symbolEmbedding =
                                setSymbolEmbedder->computeEmbedding(symbolImg);
                            auto symbolMatches = setSymbolDb->search(
                                symbolEmbedding.embedding, 1);

                            if (!symbolMatches.empty()) {
                              float confidenceThreshold =
                                  config.setSymbolConfidenceThreshold > 0.0f
                                      ? config.setSymbolConfidenceThreshold
                                      : 0.6f;

                              if (symbolMatches[0].similarity >=
                                  confidenceThreshold) {
                                setCode = symbolMatches[0].setCode;
                                setName = symbolMatches[0].setName;
                                setSimilarity = symbolMatches[0].similarity;
                              }
                            }
                          }
                        }
                      } catch (const std::exception &e) {
                        // Set symbol detection failed, continue without it
                      }
                    }
                  }

                  // Store set symbol info
                  setSymbolInfos.push_back(
                      {setCode, setName, setSimilarity, setSymbolImagePath});
                } else {
                  cardMatches.push_back({});
                  setSymbolInfos.push_back({"", "", 0.0f, ""});
                }
              } catch (const std::exception &e) {
                cardMatches.push_back({});
                setSymbolInfos.push_back({"", "", 0.0f, ""});
              }
            }
          }

          auto endTotal = std::chrono::high_resolution_clock::now();
          double totalMs =
              std::chrono::duration<double, std::milli>(endTotal - startTotal)
                  .count();

          // Build result matching RawScanResult interface
          using namespace jsiconversion;

          // Count how many detections have matches
          int identifiedCardCount = 0;
          for (const auto &matches : cardMatches) {
            if (!matches.empty()) {
              identifiedCardCount++;
            }
          }

          jsi::Object result(runtime);
          result.setProperty(runtime, "cardCount",
                             jsi::Value(identifiedCardCount));
          result.setProperty(
              runtime, "segmentationCount",
              jsi::Value(static_cast<int>(segResult.detections.size())));
          result.setProperty(runtime, "processingTime", jsi::Value(totalMs));

          // Convert detections array (identified cards only)
          jsi::Array detections(runtime, identifiedCardCount);
          size_t detectionIndex = 0;
          for (size_t i = 0; i < segResult.detections.size(); i++) {
            // Skip detections without matches
            if (i >= cardMatches.size() || cardMatches[i].empty()) {
              continue;
            }

            const auto &det = segResult.detections[i];

            jsi::Object jsDetection(runtime);

            // Bounding box
            jsi::Object box(runtime);
            box.setProperty(runtime, "x1", jsi::Value(det.box.x1));
            box.setProperty(runtime, "y1", jsi::Value(det.box.y1));
            box.setProperty(runtime, "x2", jsi::Value(det.box.x2));
            box.setProperty(runtime, "y2", jsi::Value(det.box.y2));
            box.setProperty(runtime, "conf", jsi::Value(det.box.conf));
            jsDetection.setProperty(runtime, "box", box);

            // Recognition matches (RawMatch format: cardId, name, gameName,
            // score)
            if (!cardMatches[i].empty()) {
              jsi::Array matches(runtime, cardMatches[i].size());
              for (size_t j = 0; j < cardMatches[i].size(); j++) {
                const auto &match = cardMatches[i][j];
                jsi::Object matchObj(runtime);
                matchObj.setProperty(
                    runtime, "cardId",
                    jsi::String::createFromUtf8(runtime, match.card_id));
                matchObj.setProperty(
                    runtime, "name",
                    jsi::String::createFromUtf8(runtime, match.name));
                matchObj.setProperty(
                    runtime, "gameName",
                    jsi::String::createFromUtf8(runtime, match.gameName));
                matchObj.setProperty(runtime, "score", jsi::Value(match.score));
                matches.setValueAtIndex(runtime, j, matchObj);
              }
              jsDetection.setProperty(runtime, "matches", matches);
            }

            // Cropped image path
            if (i < croppedImagePaths.size()) {
              jsDetection.setProperty(
                  runtime, "croppedImagePath",
                  jsi::String::createFromUtf8(runtime, croppedImagePaths[i]));
            }

            // Set symbol info for MTG cards
            if (i < setSymbolInfos.size() &&
                !setSymbolInfos[i].setCode.empty()) {
              jsi::Object setSymbol(runtime);
              setSymbol.setProperty(runtime, "setCode",
                                    jsi::String::createFromUtf8(
                                        runtime, setSymbolInfos[i].setCode));
              setSymbol.setProperty(runtime, "setName",
                                    jsi::String::createFromUtf8(
                                        runtime, setSymbolInfos[i].setName));
              setSymbol.setProperty(runtime, "similarity",
                                    jsi::Value(setSymbolInfos[i].similarity));
              jsDetection.setProperty(runtime, "setSymbol", setSymbol);
            }

            detections.setValueAtIndex(runtime, detectionIndex++, jsDetection);
          }
          result.setProperty(runtime, "detections", detections);

          return result;

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
