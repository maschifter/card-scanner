#include "RnCardScannerInstaller.h"
#include "Constants.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "host_objects/JsiConversions.h"
#include "models/CardEmbeddingModel.h"
#include "models/YoloSegmentationModel.h"
#include "utils/FrameExtractor.h"

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
std::mutex CardScannerInstaller::modelMutex_;
std::string CardScannerInstaller::currentGame_ = "";

void CardScannerInstaller::initializeModels(const std::string &yoloPath,
                                            const std::string &embeddingPath,
                                            const std::string &defaultGame) {
  std::lock_guard<std::mutex> lock(modelMutex_);

  if (!yoloPath.empty()) {
    yoloModel_ = std::make_shared<cardscanner::YoloSegmentationModel>(
        yoloPath, model::DEFAULT_YOLO_CONF_THRESHOLD,
        model::DEFAULT_YOLO_IOU_THRESHOLD, model::DEFAULT_YOLO_IMAGE_SIZE);
  }

  if (!embeddingPath.empty()) {
    embeddingModel_ =
        std::make_shared<cardscanner::CardEmbeddingModel>(embeddingPath);
  }

  // Set default game if provided
  if (!defaultGame.empty()) {
    currentGame_ = defaultGame;
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

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {

  cardscanner::DatabaseManager &dbManager =
      cardscanner::DatabaseManager::getInstance();

  // Create the 'initializeScanner' host function
  auto initializeScannerFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "initializeScanner"),
      3,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(
              runtime, "initializeScanner expects (yoloModelPath: string, "
                       "embeddingModelPath: string, defaultGame?: string)");
        }

        std::string yoloPath = args[0].asString(runtime).utf8(runtime);
        std::string embeddingPath = args[1].asString(runtime).utf8(runtime);
        std::string defaultGame = "";

        if (count >= 3 && args[2].isString()) {
          defaultGame = args[2].asString(runtime).utf8(runtime);
        }

        try {
          CardScannerInstaller::initializeModels(yoloPath, embeddingPath,
                                                 defaultGame);

          // Open default game database if provided
          if (!defaultGame.empty()) {
            ObjectBoxDB *db = dbManager.getOrCreateStore(defaultGame);
          }

          return jsi::Value(true);
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Failed to initialize scanner: ") +
                                 e.what());
        }
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

  // Create the 'swapDatabase' host function
  auto swapDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "swapDatabase"), 2,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(runtime, "swapDatabase expects (sourcePath: "
                                      "string, gameName: string)");
        }

        std::string sourcePath = args[0].asString(runtime).utf8(runtime);
        std::string gameName = args[1].asString(runtime).utf8(runtime);

        try {
          bool success = dbManager.swapDatabaseFile(gameName, sourcePath);

          if (success) {
            dbManager.scanForExistingStores();
          }

          return jsi::Value(success);
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Database swap failed: ") + e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "swapDatabase",
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
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        dbManager.scanForExistingStores();
        std::set<std::string> games = dbManager.getKnownGames();

        jsi::Array result(runtime, games.size());
        size_t i = 0;

        for (const auto &gameName : games) {

          std::string dirPath = dbManager.getStorePath(gameName);

          jsi::Object gameObj(runtime);

          gameObj.setProperty(runtime, "gameName",
                              jsi::String::createFromUtf8(runtime, gameName));
          gameObj.setProperty(runtime, "path",
                              jsi::String::createFromUtf8(runtime, dirPath));

          result.setValueAtIndex(runtime, i++, std::move(gameObj));
        }

        return result;
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
      2,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisArg,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 2) {
          throw jsi::JSError(runtime, "startScanningPlugin expects at least 2 "
                                      "arguments: (frame, gameName)");
        }

        try {
          auto startTotal = std::chrono::high_resolution_clock::now();
          // Get the Frame HostObject (first argument)
          auto frameObj = args[0].asObject(runtime);

          // Optional: game name for database lookup (second argument)
          std::string gameName = "";
          if (count > 1 && args[1].isString()) {
            gameName = args[1].asString(runtime).utf8(runtime);
          }

          // Fall back to current game if not provided (thread-safe)
          if (gameName.empty()) {
            gameName = CardScannerInstaller::getCurrentGame();
          }

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

          // Keep only the detection with highest confidence
          if (!segResult.detections.empty()) {
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
                    // Note: Image display removed for performance
                    // To re-enable: encode to base64 or save to disk here
                    croppedImagePaths.push_back("");

                    // Get singleton embedding model
                    auto embeddingModel =
                        CardScannerInstaller::getEmbeddingModel();
                    if (!embeddingModel) {
                      throw jsi::JSError(
                          runtime, "Embedding model not initialized. Call "
                                   "initializeScanner() first.");
                    }

                    embeddingResult = embeddingModel->computeEmbedding(cardImg);

                    // Search database
                    auto startDbSearch =
                        std::chrono::high_resolution_clock::now();
                    auto matches = db->search_similar_cards(
                        embeddingResult.embedding,
                        database::MAX_SEARCH_RESULTS);
                    auto endDbSearch =
                        std::chrono::high_resolution_clock::now();
                    dbSearchMs += std::chrono::duration<double, std::milli>(
                                      endDbSearch - startDbSearch)
                                      .count();

                    // Filter matches to only include scores above minimum
                    // similarity threshold
                    std::vector<CardSearchResult> filteredMatches;
                    for (const auto &match : matches) {
                      if (match.score > database::MIN_SIMILARITY_SCORE) {
                        filteredMatches.push_back(match);
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

          // Build result using helper functions
          using namespace jsiconversion;

          jsi::Object result(runtime);
          result.setProperty(
              runtime, "cardCount",
              jsi::Value(static_cast<int>(segResult.detections.size())));
          result.setProperty(runtime, "totalMs", jsi::Value(totalMs));
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
          result.setProperty(
              runtime, "recognitionTotalMs",
              jsi::Value(embeddingResult.performance.totalTimeMs));
          result.setProperty(runtime, "frameWidth",
                             jsi::Value(frameImage.cols));
          result.setProperty(runtime, "frameHeight",
                             jsi::Value(frameImage.rows));

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

            // Recognition matches
            if (i < cardMatches.size() && !cardMatches[i].empty()) {
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
