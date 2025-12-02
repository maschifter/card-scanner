#include "RnCardScannerInstaller.h"
#include "Constants.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "host_objects/JsiConversions.h"
#include "jsi/Promise.h"
#include "models/CardEmbeddingModel.h"
#include "models/YoloSegmentationModel.h"
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
std::mutex CardScannerInstaller::modelMutex_;
std::string CardScannerInstaller::currentGame_ = "";
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

  // Set current game
  if (!config.gameName.empty()) {
    currentGame_ = config.gameName;
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

std::string CardScannerInstaller::getCurrentGame() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return currentGame_;
}

void CardScannerInstaller::setCurrentGame(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(modelMutex_);
  currentGame_ = gameName;
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

        ScannerConfig config;
        config.yoloPath =
            configObj.getProperty(runtime, "segmentationModelPath")
                .asString(runtime)
                .utf8(runtime);
        config.embeddingPath =
            configObj.getProperty(runtime, "embeddingModelPath")
                .asString(runtime)
                .utf8(runtime);
        config.gameName = configObj.getProperty(runtime, "gameName")
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

        // Return a Promise that runs initialization on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [config, &dbManager](std::shared_ptr<Promise> promise) {
              // Run initialization on background thread
              std::thread([config, &dbManager, promise]() {
                try {
                  // Initialize models with config (this may take time)
                  CardScannerInstaller::initializeModels(config);

                  // Open game database
                  if (!config.gameName.empty()) {
                    ObjectBoxDB *db =
                        dbManager.getOrCreateStore(config.gameName);
                  }

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

  // Create the 'switchGame' host function
  auto switchGameFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "switchGame"), 1,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 1 || !args[0].isString()) {
          throw jsi::JSError(runtime, "switchGame expects (gameName: string)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);

        try {
          // Update current game (thread-safe)
          CardScannerInstaller::setCurrentGame(gameName);

          // Open/get database for this game
          ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);
          if (!db) {
            throw jsi::JSError(runtime,
                               "Failed to open database for game: " + gameName);
          }

          return jsi::Value::undefined();
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Failed to switch game: ") + e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "switchGame",
                                   std::move(switchGameFunc));

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
      *jsiRuntime, jsi::PropNameID::forUtf8(*jsiRuntime, "startScanningPlugin"),
      1,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisArg,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1) {
          throw jsi::JSError(runtime, "startScanningPlugin expects at least 1 "
                                      "argument: (frame)");
        }

        try {
          auto startTotal = std::chrono::high_resolution_clock::now();

          // Get config (thread-safe)
          auto config = CardScannerInstaller::getConfig();
          std::string gameName = CardScannerInstaller::getCurrentGame();

          // Get the Frame HostObject (first argument)
          auto frameObj = args[0].asObject(runtime);

          // Disable OpenCV threading to prevent interference with ExecutorTorch
          cv::setNumThreads(0);

          auto startFrameExtraction = std::chrono::high_resolution_clock::now();

          cv::Mat frameImage;
          frameImage =
              cardscanner::FrameExtractor::extractFrame(runtime, frameObj);
          auto endFrameExtraction = std::chrono::high_resolution_clock::now();
          double frameExtractionMs =
              std::chrono::duration<double, std::milli>(endFrameExtraction -
                                                        startFrameExtraction)
                  .count();

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
          double dbSearchMs = 0.0;
          cardscanner::CardEmbeddingResult embeddingResult;

          if (!segResult.detections.empty()) {
            auto startRecognition = std::chrono::high_resolution_clock::now();

            // Get database handle (only if game name is set)
            ObjectBoxDB *db = nullptr;
            if (!gameName.empty()) {
              db = dbManager.getOrCreateStore(gameName);
            }
            if (db) {
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
                        auto timestamp = std::chrono::duration_cast<
                                             std::chrono::milliseconds>(
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
                        cv::cvtColor(cardImg, cardImgBGR, cv::COLOR_BGR2RGB);

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
                      throw jsi::JSError(
                          runtime, "Embedding model not initialized. Call "
                                   "initializeScanner() first.");
                    }

                    embeddingResult = embeddingModel->computeEmbedding(cardImg);

                    // Search database using config.searchCandidates
                    auto startDbSearch =
                        std::chrono::high_resolution_clock::now();
                    auto matches = db->search_similar_cards(
                        embeddingResult.embedding, config.searchCandidates);
                    auto endDbSearch =
                        std::chrono::high_resolution_clock::now();
                    dbSearchMs += std::chrono::duration<double, std::milli>(
                                      endDbSearch - startDbSearch)
                                      .count();

                    // Filter matches above confidence threshold
                    // and limit to maxMatches
                    std::vector<CardSearchResult> filteredMatches;
                    for (const auto &match : matches) {
                      if (match.score >= config.confidenceThreshold) {
                        filteredMatches.push_back(match);
                        if (filteredMatches.size() >=
                            static_cast<size_t>(config.maxMatches)) {
                          break;
                        }
                      }
                    }

                    cardMatches.push_back(filteredMatches);
                  } else {
                    cardMatches.push_back({});
                  }
                } catch (const std::exception &e) {
                  cardMatches.push_back({});
                }
              }
            }

            // If no matches found, clear detections to hide bounding box
            if (!cardMatches.empty() && cardMatches[0].empty()) {
              segResult.detections.clear();
            }
          }

          auto endTotal = std::chrono::high_resolution_clock::now();
          double totalMs =
              std::chrono::duration<double, std::milli>(endTotal - startTotal)
                  .count();

          // Build result matching RawScanResult interface
          using namespace jsiconversion;

          jsi::Object result(runtime);
          result.setProperty(
              runtime, "cardCount",
              jsi::Value(static_cast<int>(segResult.detections.size())));
          result.setProperty(runtime, "frameWidth",
                             jsi::Value(frameImage.cols));
          result.setProperty(runtime, "frameHeight",
                             jsi::Value(frameImage.rows));
          result.setProperty(runtime, "processingTime", jsi::Value(totalMs));

          // Timing breakdown
          result.setProperty(runtime, "frameExtractionMs",
                             jsi::Value(frameExtractionMs));
          result.setProperty(
              runtime, "yoloPreprocessMs",
              jsi::Value(segResult.performance.preprocessingTimeMs));
          result.setProperty(runtime, "yoloInferenceMs",
                             jsi::Value(segResult.performance.inferenceTimeMs));
          result.setProperty(
              runtime, "yoloPostprocessMs",
              jsi::Value(segResult.performance.postprocessingTimeMs));
          result.setProperty(
              runtime, "embeddingPreprocessMs",
              jsi::Value(embeddingResult.performance.preprocessingTimeMs));
          result.setProperty(
              runtime, "embeddingInferenceMs",
              jsi::Value(embeddingResult.performance.inferenceTimeMs));
          result.setProperty(runtime, "dbSearchMs", jsi::Value(dbSearchMs));

          // Convert detections array
          jsi::Array detections(runtime, segResult.detections.size());
          for (size_t i = 0; i < segResult.detections.size(); i++) {
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

            // Recognition matches (RawMatch format: cardId, gameName, score)
            if (i < cardMatches.size() && !cardMatches[i].empty()) {
              jsi::Array matches(runtime, cardMatches[i].size());
              for (size_t j = 0; j < cardMatches[i].size(); j++) {
                const auto &match = cardMatches[i][j];
                jsi::Object matchObj(runtime);
                matchObj.setProperty(
                    runtime, "cardId",
                    jsi::String::createFromUtf8(runtime, match.name));
                matchObj.setProperty(
                    runtime, "gameName",
                    jsi::String::createFromUtf8(runtime, gameName));
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

            detections.setValueAtIndex(runtime, i, jsDetection);
          }
          result.setProperty(runtime, "detections", detections);

          return result;

        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Frame processing failed: ") +
                                          e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "startScanningPlugin",
                                   std::move(startScanningFunc));

  threads::utils::unsafeSetupThreadPool();
  threads::GlobalThreadPool::initialize();
}

} // namespace rncardscanner
