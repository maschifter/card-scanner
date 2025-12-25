#include "RnCardScannerInstaller.h"
#include "Constants.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "jsi/Promise.h"
#include "models/CardEmbeddingModel.h"
#include "models/YoloSegmentationModel.h"
#include "models/fab/FABColorClassifier.h"
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

using namespace rncardscanner::constants;

#ifdef __ANDROID__
#include <sys/resource.h>
#endif

namespace rncardscanner {

// Initialize static members
std::shared_ptr<rncardscanner::YoloSegmentationModel>
    CardScannerInstaller::yoloModel_ = nullptr;
std::shared_ptr<rncardscanner::CardEmbeddingModel>
    CardScannerInstaller::embeddingModel_ = nullptr;
std::map<std::string, std::shared_ptr<rncardscanner::CardEmbeddingModel>>
    CardScannerInstaller::gameEmbeddingModels_;
std::shared_ptr<rncardscanner::SetSymbolYoloModel>
    CardScannerInstaller::setSymbolYoloModel_ = nullptr;
std::shared_ptr<rncardscanner::SetSymbolEmbedder>
    CardScannerInstaller::setSymbolEmbedder_ = nullptr;
std::shared_ptr<rncardscanner::FABColorClassifier>
    CardScannerInstaller::fabColorClassifier_ = nullptr;
std::mutex CardScannerInstaller::modelMutex_;
dto::ScannerConfig CardScannerInstaller::config_ = {};

void CardScannerInstaller::initializeModels() {
  std::lock_guard<std::mutex> lock(modelMutex_);

  if (!config_.segmentationModelPath.empty()) {
    yoloModel_ = std::make_shared<rncardscanner::YoloSegmentationModel>(
        config_.segmentationModelPath, config_.gameClassMapping,
        config_.segmentationThreshold, config_.iouThreshold,
        constants::model::DEFAULT_YOLO_IMAGE_SIZE);
  }

  if (!config_.embeddingModelPath.empty()) {
    embeddingModel_ = std::make_shared<rncardscanner::CardEmbeddingModel>(
        config_.embeddingModelPath);
  }

  // Load game-specific configurations
  for (const auto &[gameName, gameConfig] : config_.gameSpecificConfig) {
    // Load game-specific embedding model if provided
    if (gameConfig.hasEmbedding()) {
      gameEmbeddingModels_[gameName] =
          std::make_shared<rncardscanner::CardEmbeddingModel>(
              gameConfig.embeddingModelPath);
    }

    // MTG-specific: Set symbol detection
    if (gameName == "mtg" && gameConfig.hasSetSymbolDetection()) {
      if (!gameConfig.setSymbolDetectionModelPath.empty()) {
        setSymbolYoloModel_ = std::make_shared<rncardscanner::SetSymbolYoloModel>(
            gameConfig.setSymbolDetectionModelPath,
            gameConfig.setSymbolDetectionThreshold,
            constants::model::DEFAULT_YOLO_IOU_THRESHOLD,
            constants::model::DEFAULT_YOLO_IMAGE_SIZE);
      }

      if (!gameConfig.setSymbolEmbedderModelPath.empty()) {
        setSymbolEmbedder_ = std::make_shared<rncardscanner::SetSymbolEmbedder>(
            gameConfig.setSymbolEmbedderModelPath);
      }

      // Initialize set symbol store in DatabaseManager
      try {
        rncardscanner::DatabaseManager::getInstance().getSetSymbolStore();
      } catch (const std::exception &e) {
        std::cerr << "Warning: Could not initialize SetSymbol store: "
                  << e.what() << std::endl;
      }
    }

    // FAB-specific: Color detection
    if (gameName == "fab" && gameConfig.hasColorDetection()) {
      fabColorClassifier_ = std::make_shared<rncardscanner::FABColorClassifier>(
          gameConfig.colorDetectionModelPath);
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

  gameEmbeddingModels_.clear();

  if (setSymbolYoloModel_) {
    setSymbolYoloModel_.reset();
  }

  if (setSymbolEmbedder_) {
    setSymbolEmbedder_.reset();
  }

  if (fabColorClassifier_) {
    fabColorClassifier_.reset();
  }

  rncardscanner::DatabaseManager::getInstance().closeSetSymbolStore();
}

std::shared_ptr<rncardscanner::YoloSegmentationModel>
CardScannerInstaller::getYoloModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return yoloModel_;
}

std::shared_ptr<rncardscanner::CardEmbeddingModel>
CardScannerInstaller::getEmbeddingModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return embeddingModel_;
}

std::shared_ptr<rncardscanner::CardEmbeddingModel>
CardScannerInstaller::getEmbeddingModelForGame(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(modelMutex_);

  // Check if game has a specific embedding model
  auto it = gameEmbeddingModels_.find(gameName);
  if (it != gameEmbeddingModels_.end()) {
    return it->second;
  }

  // Fall back to default embedding model
  return embeddingModel_;
}

std::shared_ptr<rncardscanner::SetSymbolYoloModel>
CardScannerInstaller::getSetSymbolYoloModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return setSymbolYoloModel_;
}

std::shared_ptr<rncardscanner::SetSymbolEmbedder>
CardScannerInstaller::getSetSymbolEmbedder() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return setSymbolEmbedder_;
}

std::shared_ptr<rncardscanner::FABColorClassifier>
CardScannerInstaller::getFABColorClassifier() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return fabColorClassifier_;
}

dto::ScannerConfig CardScannerInstaller::getConfig() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return config_;
}

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {

  rncardscanner::DatabaseManager &dbManager =
      rncardscanner::DatabaseManager::getInstance();

  // Create the 'initializeScannerNative' host function (returns Promise)
  auto initializeScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "initializeScanner"),
      1,
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
          config_ =
              utils::JSISerializer::parseScannerConfig(runtime, configObj);
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

  jsiRuntime->global().setProperty(*jsiRuntime, "initializeScanner",
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
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "swapDatabase"), 2,
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
                  std::chrono::steady_clock::time_point startTime =
                      std::chrono::steady_clock::now();
                  bool success =
                      dbManager.swapDatabaseFile(gameName, sourcePath);
                  std::chrono::steady_clock::time_point endTime =
                      std::chrono::steady_clock::now();
                  auto duration =
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          endTime - startTime)
                          .count();
                  std::cout << "[RNCardScanner] Swapped database for game '"
                            << gameName << "' in " << duration << " ms."
                            << std::endl;

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

  jsiRuntime->global().setProperty(*jsiRuntime, "swapDatabase",
                                   std::move(swapDatabaseFunc));

  // Create the 'scanImage' host function
  auto scanImageFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "scanImage"), 1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "scanImage expects (imagePath: string)");
        }

        std::string imagePath = args[0].asString(runtime).utf8(runtime);

        // Return a Promise that processes the image on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [imagePath, &dbManager](std::shared_ptr<Promise> promise) {
              try {
                // Strip file:// prefix if present
                std::string cleanedPath = imagePath;
                const std::string filePrefix = "file://";
                if (cleanedPath.find(filePrefix) == 0) {
                  cleanedPath = cleanedPath.substr(filePrefix.length());
                }

                // Get config and models
                auto scannerConfig = CardScannerInstaller::getConfig();
                auto yoloModel = CardScannerInstaller::getYoloModel();
                auto embeddingModel = CardScannerInstaller::getEmbeddingModel();
                auto setSymbolYolo =
                    CardScannerInstaller::getSetSymbolYoloModel();
                auto setSymbolEmbedder =
                    CardScannerInstaller::getSetSymbolEmbedder();
                auto fabColorClassifier =
                    CardScannerInstaller::getFABColorClassifier();

                if (!yoloModel || !embeddingModel) {
                  promise->reject("Models not initialized. Call "
                                  "initializeScanner() first.");
                  return;
                }

                // Load image
                cv::Mat image = cv::imread(cleanedPath);
                if (image.empty()) {
                  promise->reject("Failed to load image from: " + cleanedPath);
                  return;
                }

                // Convert BGR to RGB for consistency with camera frames
                cv::Mat imageRGB;
                cv::cvtColor(image, imageRGB, cv::COLOR_BGR2RGB);

                // Run full pipeline (segmentation + recognition)
                auto scanResult = core::ScannerPipeline::processFrame(
                    imageRGB, scannerConfig, dbManager, yoloModel.get(),
                    embeddingModel.get(), setSymbolYolo.get(),
                    setSymbolEmbedder.get(), fabColorClassifier.get());

                // Serialize to JSI using the same serializer as scanFramePlugin
                auto &runtime = promise->getRuntime();
                auto result = utils::JSISerializer::serializeScanResult(
                    runtime, scanResult);
                promise->resolve(std::move(result));
              } catch (const std::exception &e) {
                promise->reject(std::string("Image scan failed: ") + e.what());
              }
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "scanImage",
                                   std::move(scanImageFunc));

  auto listDatabasesFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "listDatabases"), 0,
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

                      // Get metadata
                      uint64_t cardCount = 0;
                      std::string creationTimestamp = "";
                      long fileSize = 0;

                      try {
                        ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);
                        if (db) {
                          cardCount = db->get_card_count();
                          creationTimestamp =
                              db->get_metadata_value("creation_timestamp");
                        }

                        // Get file size
                        struct stat stat_buf;
                        if (stat(dirPath.c_str(), &stat_buf) == 0) {
                          fileSize = stat_buf.st_size;
                        }
                      } catch (...) {
                        // Ignore errors for individual games
                      }

                      // Use JSISerializer for consistent serialization
                      jsi::Object gameObj =
                          utils::JSISerializer::serializeDatabaseInfo(
                              promise->getRuntime(), gameName, dirPath,
                              cardCount, creationTimestamp, fileSize);

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

  jsiRuntime->global().setProperty(*jsiRuntime, "listDatabases",
                                   std::move(listDatabasesFunc));

  auto getDatabaseInfoFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "getDatabaseInfo"), 1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "getDatabaseInfo expects one string argument "
                             "(gameName)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);

        // Return a Promise that retrieves database info on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName](std::shared_ptr<Promise> promise) {
              std::thread([&dbManager, gameName, promise]() {
                try {
                  std::string dirPath = dbManager.getStorePath(gameName);

                  // Check if database exists
                  if (!std::filesystem::exists(dirPath)) {
                    promise->getCallInvoker()->invokeAsync(
                        [promise, gameName]() {
                          promise->reject(
                              std::string("Database not found for game: ") +
                              gameName);
                        });
                    return;
                  }

                  // Get metadata
                  uint64_t cardCount = 0;
                  std::string creationTimestamp = "";
                  long fileSize = 0;

                  ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);
                  if (db) {
                    cardCount = db->get_card_count();
                    creationTimestamp =
                        db->get_metadata_value("creation_timestamp");
                  }

                  // Get file size
                  struct stat stat_buf;
                  if (stat(dirPath.c_str(), &stat_buf) == 0) {
                    fileSize = stat_buf.st_size;
                  }

                  // Resolve promise on JS thread
                  promise->getCallInvoker()->invokeAsync(
                      [promise, gameName, dirPath, cardCount, creationTimestamp,
                       fileSize]() {
                        // Use JSISerializer for consistent serialization
                        jsi::Object gameObj =
                            utils::JSISerializer::serializeDatabaseInfo(
                                promise->getRuntime(), gameName, dirPath,
                                cardCount, creationTimestamp, fileSize);

                        promise->resolve(std::move(gameObj));
                      });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, gameName, errorMsg = std::string(e.what())]() {
                        promise->reject(
                            std::string("Failed to get database info for ") +
                            gameName + ": " + errorMsg);
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "getDatabaseInfo",
                                   std::move(getDatabaseInfoFunc));

  auto deleteDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "deleteDatabase"), 1,
      [&dbManager,
       callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                    const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 1 || !args[0].isString()) {
          throw jsi::JSError(runtime,
                             "deleteDatabase expects one string argument "
                             "(gameName)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);

        // Return a Promise that deletes database on background thread
        return Promise::createPromise(
            runtime, callInvoker,
            [&dbManager, gameName](std::shared_ptr<Promise> promise) {
              std::thread([&dbManager, gameName, promise]() {
                try {
                  bool success = dbManager.deleteDatabaseDirectory(gameName);

                  promise->getCallInvoker()->invokeAsync([promise, success,
                                                          gameName]() {
                    // Use JSISerializer for consistent serialization
                    std::string error =
                        success
                            ? ""
                            : std::string("Failed to delete database for ") +
                                  gameName;
                    jsi::Object result =
                        utils::JSISerializer::serializeOperationResult(
                            promise->getRuntime(), success, error);

                    promise->resolve(std::move(result));
                  });
                } catch (const std::exception &e) {
                  promise->getCallInvoker()->invokeAsync(
                      [promise, errorMsg = std::string(e.what())]() {
                        // Use JSISerializer for consistent serialization
                        jsi::Object result =
                            utils::JSISerializer::serializeOperationResult(
                                promise->getRuntime(), false, errorMsg);
                        promise->resolve(std::move(result));
                      });
                }
              }).detach();
            });
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "deleteDatabase",
                                   std::move(deleteDatabaseFunc));

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
              rncardscanner::FrameExtractor::extractFrame(runtime, frameObj);

          // 2. Get config
          auto scannerConfig = CardScannerInstaller::getConfig();

          // 3. Get all models
          auto yoloModel = CardScannerInstaller::getYoloModel();
          auto embeddingModel = CardScannerInstaller::getEmbeddingModel();
          auto setSymbolYolo = CardScannerInstaller::getSetSymbolYoloModel();
          auto setSymbolEmbedder = CardScannerInstaller::getSetSymbolEmbedder();
          auto fabColorClassifier =
              CardScannerInstaller::getFABColorClassifier();

          if (!yoloModel || !embeddingModel) {
            throw jsi::JSError(
                runtime,
                "Models not initialized. Call initializeScanner() first.");
          }

          // 4. Run pipeline
          auto result = core::ScannerPipeline::processFrame(
              frameImage, scannerConfig, dbManager, yoloModel.get(),
              embeddingModel.get(), setSymbolYolo.get(),
              setSymbolEmbedder.get(), fabColorClassifier.get());

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
