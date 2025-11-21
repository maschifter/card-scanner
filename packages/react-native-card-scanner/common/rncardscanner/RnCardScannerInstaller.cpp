#include "RnCardScannerInstaller.h"
#include "CardRecognitionPipeline.h"
#include "CardScanner.h"
#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "ObjectBoxTest.h"
#include "PathProvider.h"
#include "YoloSegmentation.h"

#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>

#include "threads/GlobalThreadPool.h"
#include "threads/utils/ThreadUtils.h"

namespace rncardscanner {

void CardScannerInstaller::injectJSIBindings(
    jsi::Runtime *jsiRuntime, std::shared_ptr<react::CallInvoker> callInvoker) {

  cardscanner::DatabaseManager &dbManager =
      cardscanner::DatabaseManager::getInstance();
  std::cout << "Injecting JSI bindings for CardScanner" << std::endl;

  // Create the 'runInference' host function
  auto runInferenceFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "runInference"), 1,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 1 || !args[0].isString()) {
          throw jsi::JSError(
              runtime, "runInference expects one string argument (modelPath)");
        }

        std::string modelPath = args[0].asString(runtime).utf8(runtime);
        std::cout << "[RnCardScannerInstaller] runInference - modelPath: "
                  << modelPath << std::endl;
        try {
          auto inferenceResult =
              cardscanner::CardScanner::runInference(modelPath);

          // Create result object with outputShape and inferenceTimeMs
          jsi::Object result(runtime);

          // Add outputShape array
          jsi::Array outputShape(runtime, inferenceResult.outputShape.size());
          for (size_t i = 0; i < inferenceResult.outputShape.size(); i++) {
            outputShape.setValueAtIndex(
                runtime, i, jsi::Value(inferenceResult.outputShape[i]));
          }
          result.setProperty(runtime, "outputShape", outputShape);

          // Add inference time
          result.setProperty(runtime, "inferenceTimeMs",
                             jsi::Value(inferenceResult.inferenceTimeMs));

          // Add embedding array (256 dimensions)
          jsi::Array embedding(runtime, inferenceResult.embedding.size());
          for (size_t i = 0; i < inferenceResult.embedding.size(); i++) {
            embedding.setValueAtIndex(
                runtime, i,
                jsi::Value(static_cast<double>(inferenceResult.embedding[i])));
          }
          result.setProperty(runtime, "embedding", embedding);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Inference failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runInference",
                                   std::move(runInferenceFunc));

  // Create the 'runInferenceOnImage' host function
  auto runInferenceOnImageFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime,
      jsi::PropNameID::forAscii(*jsiRuntime, "runInferenceOnImage"), 2,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(runtime, "runInferenceOnImage expects (modelPath: "
                                      "string, imagePath: string)");
        }

        std::string modelPath = args[0].asString(runtime).utf8(runtime);
        std::string imagePath = args[1].asString(runtime).utf8(runtime);
        std::cout
            << "[RnCardScannerInstaller] runInferenceOnImage - modelPath: "
            << modelPath << ", imagePath: " << imagePath << std::endl;

        try {
          auto inferenceResult = cardscanner::CardScanner::runInferenceOnImage(
              modelPath, imagePath);

          // Create result object
          jsi::Object result(runtime);

          jsi::Array outputShape(runtime, inferenceResult.outputShape.size());
          for (size_t i = 0; i < inferenceResult.outputShape.size(); i++) {
            outputShape.setValueAtIndex(
                runtime, i, jsi::Value(inferenceResult.outputShape[i]));
          }
          result.setProperty(runtime, "outputShape", outputShape);

          result.setProperty(runtime, "inferenceTimeMs",
                             jsi::Value(inferenceResult.inferenceTimeMs));

          jsi::Array embedding(runtime, inferenceResult.embedding.size());
          for (size_t i = 0; i < inferenceResult.embedding.size(); i++) {
            embedding.setValueAtIndex(
                runtime, i,
                jsi::Value(static_cast<double>(inferenceResult.embedding[i])));
          }
          result.setProperty(runtime, "embedding", embedding);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Inference failed: ") + e.what());
        }
      });

  jsiRuntime->global().setProperty(*jsiRuntime, "runInferenceOnImage",
                                   std::move(runInferenceOnImageFunc));

  // Create the 'runTests' host function
  auto runTestsFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "runTests"), 0,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        try {
          std::string result = objectboxtest::ObjectBoxTest::runTest();
          return jsi::String::createFromUtf8(runtime, result);
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Tests failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runTests",
                                   std::move(runTestsFunc));

  // Create the 'searchSimilarCards' host function
  auto searchSimilarFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "searchSimilarCards"),
      3,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 3 || !args[0].isString() || !args[1].isObject() ||
            !args[2].isNumber()) {
          throw jsi::JSError(runtime,
                             "searchSimilarCards expects (gameName: string, "
                             "embedding: number[], limit: number)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);
        jsi::Array embeddingArray = args[1].asObject(runtime).asArray(runtime);
        int limit = static_cast<int>(args[2].asNumber());

        try {
          ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);
          if (!db) {
            throw std::runtime_error("Database store not available.");
          }

          // Convert JSI array to C++ vector
          std::vector<float> queryEmbedding;
          queryEmbedding.reserve(embeddingArray.size(runtime));
          for (size_t i = 0; i < embeddingArray.size(runtime); i++) {
            queryEmbedding.push_back(static_cast<float>(
                embeddingArray.getValueAtIndex(runtime, i).asNumber()));
          }

          // Create ObjectBoxDB instance and search
          auto results = db->search_similar_cards(queryEmbedding, limit);

          // Convert results to JSI array
          jsi::Array resultsArray(runtime, results.size());
          for (size_t i = 0; i < results.size(); i++) {
            jsi::Object resultObj(runtime);
            resultObj.setProperty(
                runtime, "cardId",
                jsi::String::createFromUtf8(runtime, results[i].card_id));
            resultObj.setProperty(
                runtime, "name",
                jsi::String::createFromUtf8(runtime, results[i].name));
            resultObj.setProperty(runtime, "score",
                                  jsi::Value(results[i].score));
            resultsArray.setValueAtIndex(runtime, i, resultObj);
          }

          return resultsArray;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime,
                             std::string("Failed to search similar cards: ") +
                                 e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "searchSimilarCards",
                                   std::move(searchSimilarFunc));

  // Create the 'runYoloSegmentation' host function
  auto yoloSegmentFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime,
      jsi::PropNameID::forAscii(*jsiRuntime, "runYoloSegmentation"), 5,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 5 || !args[0].isString() || !args[1].isString() ||
            !args[2].isNumber() || !args[3].isNumber() || !args[4].isString()) {
          throw jsi::JSError(
              runtime,
              "runYoloSegmentation expects (modelPath: string, imagePath: "
              "string, conf: number, iou: number, outputDir: string)");
        }

        std::string modelPath = args[0].asString(runtime).utf8(runtime);
        std::string imagePath = args[1].asString(runtime).utf8(runtime);
        float conf = static_cast<float>(args[2].asNumber());
        float iou = static_cast<float>(args[3].asNumber());
        std::string outputDir = args[4].asString(runtime).utf8(runtime);

        try {
          // Create YOLO model (imgsz=384 to match the model)
          cardscanner::YoloSegmentation yolo(modelPath, conf, iou, 384);
          auto segResult = yolo.segment(imagePath);

          // Strip file:// prefix from output directory if present (for
          // cv::imwrite)
          std::string tempDir = outputDir;
          const std::string filePrefix = "file://";
          if (tempDir.find(filePrefix) == 0) {
            tempDir = tempDir.substr(filePrefix.length());
          }

          // Ensure output directory ends with /
          if (!tempDir.empty() && tempDir.back() != '/') {
            tempDir += '/';
          }

          std::cout << "Using output directory: " << tempDir << std::endl;

          // Save visualized image to temp location
          std::string outputPath = tempDir + "yolo_result.jpg";
          bool vizSaved = cv::imwrite(outputPath, segResult.visualizedImage);
          std::cout << "Saved visualized image to: " << outputPath << " = "
                    << (vizSaved ? "OK" : "FAILED") << std::endl;

          // Add file:// prefix for React Native Image component
          std::string outputUri = "file://" + outputPath;

          // Save dewarped cards
          jsi::Array dewarpedPaths(runtime, segResult.detections.size());
          for (size_t i = 0; i < segResult.detections.size(); i++) {
            if (!segResult.detections[i].dewarpedCard.empty()) {
              std::string dewarpPath =
                  tempDir + "card_" + std::to_string(i) + ".jpg";
              bool saved =
                  cv::imwrite(dewarpPath, segResult.detections[i].dewarpedCard);
              std::cout << "Card " << i << " dewarped: "
                        << segResult.detections[i].dewarpedCard.cols << "x"
                        << segResult.detections[i].dewarpedCard.rows
                        << ", saved to " << dewarpPath << " = "
                        << (saved ? "OK" : "FAILED") << std::endl;

              // Add file:// prefix for React Native
              std::string dewarpUri = "file://" + dewarpPath;
              dewarpedPaths.setValueAtIndex(
                  runtime, i, jsi::String::createFromUtf8(runtime, dewarpUri));
            } else {
              std::cout << "Card " << i
                        << " has no dewarped card (quad extraction failed)"
                        << std::endl;
              dewarpedPaths.setValueAtIndex(runtime, i, jsi::Value::null());
            }
          }

          // Create result object
          jsi::Object result(runtime);
          result.setProperty(runtime, "inferenceTimeMs",
                             jsi::Value(segResult.inferenceTimeMs));
          result.setProperty(runtime, "visualizedImagePath",
                             jsi::String::createFromUtf8(runtime, outputUri));
          result.setProperty(runtime, "dewarpedCardPaths", dewarpedPaths);

          // Add detections array
          jsi::Array detections(runtime, segResult.detections.size());
          for (size_t i = 0; i < segResult.detections.size(); i++) {
            const auto &det = segResult.detections[i];

            jsi::Object detObj(runtime);

            // Add bounding box
            jsi::Object box(runtime);
            box.setProperty(runtime, "x1", jsi::Value(det.box.x1));
            box.setProperty(runtime, "y1", jsi::Value(det.box.y1));
            box.setProperty(runtime, "x2", jsi::Value(det.box.x2));
            box.setProperty(runtime, "y2", jsi::Value(det.box.y2));
            box.setProperty(runtime, "conf", jsi::Value(det.box.conf));
            box.setProperty(runtime, "cls", jsi::Value(det.box.cls));
            detObj.setProperty(runtime, "box", box);

            // Add mask contours
            jsi::Array contours(runtime, det.maskContours.size());
            for (size_t j = 0; j < det.maskContours.size(); j++) {
              jsi::Array contour(runtime, det.maskContours[j].size());
              for (size_t k = 0; k < det.maskContours[j].size(); k++) {
                jsi::Object point(runtime);
                point.setProperty(runtime, "x",
                                  jsi::Value(det.maskContours[j][k].x));
                point.setProperty(runtime, "y",
                                  jsi::Value(det.maskContours[j][k].y));
                contour.setValueAtIndex(runtime, k, point);
              }
              contours.setValueAtIndex(runtime, j, contour);
            }
            detObj.setProperty(runtime, "mask", contours);

            detections.setValueAtIndex(runtime, i, detObj);
          }
          result.setProperty(runtime, "detections", detections);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(
              runtime, std::string("YOLO segmentation failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runYoloSegmentation",
                                   std::move(yoloSegmentFunc));

  // Create the 'recognizeCards' host function (full pipeline)
  auto recognizeCardsFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "recognizeCards"), 6,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 4 || !args[0].isString() || !args[1].isString() ||
            !args[2].isString() || !args[3].isString()) {
          throw jsi::JSError(runtime,
                             "recognizeCards expects at least (imagePath: "
                             "string, yoloModelPath: string, "
                             "embeddingModelPath: string, gameName: string)");
        }

        std::string imagePath = args[0].asString(runtime).utf8(runtime);
        std::string yoloModelPath = args[1].asString(runtime).utf8(runtime);
        std::string embeddingModelPath =
            args[2].asString(runtime).utf8(runtime);

        std::string gameName = args[3].asString(runtime).utf8(runtime);

        float yoloConf = (count > 4 && args[4].isNumber())
                             ? static_cast<float>(args[4].asNumber())
                             : 0.5f;
        float yoloIou = (count > 5 && args[5].isNumber())
                            ? static_cast<float>(args[5].asNumber())
                            : 0.7f;
        int topK = (count > 6 && args[6].isNumber())
                       ? static_cast<int>(args[6].asNumber())
                       : 10;

        try {
          auto pipelineResult = cardscanner::CardRecognitionPipeline::recognize(
              imagePath, yoloModelPath, embeddingModelPath, gameName, yoloConf,
              yoloIou, topK);

          // Create result object
          jsi::Object result(runtime);
          result.setProperty(runtime, "yoloTimeMs",
                             jsi::Value(pipelineResult.yoloTimeMs));
          result.setProperty(runtime, "totalTimeMs",
                             jsi::Value(pipelineResult.totalTimeMs));

          // Add detailed timing breakdown
          jsi::Object timingBreakdown(runtime);
          timingBreakdown.setProperty(
              runtime, "yoloPreprocessingMs",
              jsi::Value(pipelineResult.timingBreakdown.yoloPreprocessingMs));
          timingBreakdown.setProperty(
              runtime, "yoloInferenceMs",
              jsi::Value(pipelineResult.timingBreakdown.yoloInferenceMs));
          timingBreakdown.setProperty(
              runtime, "yoloPostprocessingMs",
              jsi::Value(pipelineResult.timingBreakdown.yoloPostprocessingMs));
          timingBreakdown.setProperty(
              runtime, "yoloTotalMs",
              jsi::Value(pipelineResult.timingBreakdown.yoloTotalMs));
          timingBreakdown.setProperty(
              runtime, "embeddingPreprocessingMs",
              jsi::Value(
                  pipelineResult.timingBreakdown.embeddingPreprocessingMs));
          timingBreakdown.setProperty(
              runtime, "embeddingInferenceMs",
              jsi::Value(pipelineResult.timingBreakdown.embeddingInferenceMs));
          timingBreakdown.setProperty(
              runtime, "embeddingTotalMs",
              jsi::Value(pipelineResult.timingBreakdown.embeddingTotalMs));
          timingBreakdown.setProperty(
              runtime, "databaseSearchMs",
              jsi::Value(pipelineResult.timingBreakdown.databaseSearchMs));
          timingBreakdown.setProperty(
              runtime, "totalPipelineMs",
              jsi::Value(pipelineResult.timingBreakdown.totalPipelineMs));
          result.setProperty(runtime, "timingBreakdown", timingBreakdown);

          // Add cards array
          jsi::Array cards(runtime, pipelineResult.cards.size());
          for (size_t i = 0; i < pipelineResult.cards.size(); i++) {
            const auto &card = pipelineResult.cards[i];

            jsi::Object cardObj(runtime);

            // Add bounding box
            jsi::Object box(runtime);
            box.setProperty(runtime, "x1", jsi::Value(card.x1));
            box.setProperty(runtime, "y1", jsi::Value(card.y1));
            box.setProperty(runtime, "x2", jsi::Value(card.x2));
            box.setProperty(runtime, "y2", jsi::Value(card.y2));
            cardObj.setProperty(runtime, "box", box);

            cardObj.setProperty(runtime, "conf", jsi::Value(card.conf));
            cardObj.setProperty(runtime, "embeddingTimeMs",
                                jsi::Value(card.embeddingTimeMs));

            // Add matches array
            jsi::Array matches(runtime, card.matches.size());
            for (size_t j = 0; j < card.matches.size(); j++) {
              jsi::Object matchObj(runtime);
              matchObj.setProperty(runtime, "cardId",
                                   jsi::String::createFromUtf8(
                                       runtime, card.matches[j].card_id));
              matchObj.setProperty(
                  runtime, "name",
                  jsi::String::createFromUtf8(runtime, card.matches[j].name));
              matchObj.setProperty(runtime, "score",
                                   jsi::Value(card.matches[j].score));
              matches.setValueAtIndex(runtime, j, matchObj);
            }
            cardObj.setProperty(runtime, "matches", matches);

            cards.setValueAtIndex(runtime, i, cardObj);
          }
          result.setProperty(runtime, "cards", cards);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Card recognition failed: ") +
                                          e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "recognizeCards",
                                   std::move(recognizeCardsFunc));

  // Create the 'swapDatabase' host function
  auto swapDatabaseFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "swapDatabase"), 2,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(runtime, "swapDatabase expects two string "
                                      "arguments (sourcePath, gameName)");
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

  jsiRuntime->global().setProperty(*jsiRuntime, "closeGameStore",
                                   std::move(closeStoreFunc));

  auto loadEmbeddingsFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "loadCardEmbeddings"),
      2,
      [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisValue,
                   const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(runtime, "loadCardEmbeddings expects two string "
                                      "arguments (gameName, jsonPath)");
        }

        std::string gameName = args[0].asString(runtime).utf8(runtime);
        std::string jsonPath = args[1].asString(runtime).utf8(runtime);

        // Clean jsonPath - strip file:// prefix if present
        std::string cleanJsonPath = jsonPath;
        const std::string filePrefix = "file://";
        if (cleanJsonPath.find(filePrefix) == 0) {
          cleanJsonPath = cleanJsonPath.substr(filePrefix.length());
        }

        std::cout << "📦 Loading embeddings from: " << cleanJsonPath
                  << " for game: " << gameName << std::endl;

        try {
          // Get the store via the Manager (opens it if not already open)
          ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);
          if (!db) {
            throw std::runtime_error("Failed to open database store.");
          }

          int count = db->load_embeddings_from_json(cleanJsonPath);

          // Return result object with count
          jsi::Object result(runtime);
          result.setProperty(runtime, "loaded", jsi::Value(count));
          result.setProperty(
              runtime, "totalCards",
              jsi::Value(static_cast<double>(db->get_card_count())));

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(
              runtime, std::string("Failed to load embeddings: ") + e.what());
        }
      }); // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "loadCardEmbeddings",
                                   std::move(loadEmbeddingsFunc));

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

  // Install the function on the global object (must be done outside this
  // snippet) jsiRuntime->global().setProperty(*jsiRuntime, "getCardCount",
  // std::move(getCardCountFunc));  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "getCardCount",
                                   std::move(getCardCountFunc));

  auto myPlugin = [&dbManager](jsi::Runtime &runtime, const jsi::Value &thisArg,
                               const jsi::Value *args,
                               size_t count) -> jsi::Value {
    if (count < 2) {
      throw jsi::JSError(runtime,
                         "myCppPlugin expects at least 2 arguments: (frame, "
                         "yoloModelPath, [embeddingModelPath], [gameName])");
    }

    try {
      auto startTotal = std::chrono::high_resolution_clock::now();

      // Get the Frame HostObject (first argument)
      auto frameObj = args[0].asObject(runtime);

      // Get YOLO model path (second argument)
      if (!args[1].isString()) {
        throw jsi::JSError(runtime, "yoloModelPath must be a string");
      }
      std::string modelPath = args[1].asString(runtime).utf8(runtime);

      // Optional: embedding model path (third argument)
      std::string embeddingModelPath = "";
      if (count > 2 && args[2].isString()) {
        embeddingModelPath = args[2].asString(runtime).utf8(runtime);
      }

      // Optional: game name for database lookup (fourth argument)
      std::string gameName = "";
      if (count > 3 && args[3].isString()) {
        gameName = args[3].asString(runtime).utf8(runtime);
      }

      bool doRecognition = !embeddingModelPath.empty() && !gameName.empty();

      // Disable OpenCV threading to prevent interference with ExecutorTorch
      // cv::setNumThreads(0);

      auto startFrameExtraction = std::chrono::high_resolution_clock::now();

      // Extract frame dimensions
      int width = 0;
      int height = 0;

      if (frameObj.hasProperty(runtime, "width")) {
        width =
            static_cast<int>(frameObj.getProperty(runtime, "width").asNumber());
      }
      if (frameObj.hasProperty(runtime, "height")) {
        height = static_cast<int>(
            frameObj.getProperty(runtime, "height").asNumber());
      }

      // Extract frame buffer
      cv::Mat frameImage;
      std::string frameDebug = "";

      // Get pixel format
      std::string pixelFormat = "unknown";
      if (frameObj.hasProperty(runtime, "pixelFormat")) {
        auto pixelFormatValue = frameObj.getProperty(runtime, "pixelFormat");
        if (pixelFormatValue.isString()) {
          pixelFormat = pixelFormatValue.asString(runtime).utf8(runtime);
        }
      }

      // Try to extract actual frame data using toArrayBuffer
      if (frameObj.hasProperty(runtime, "toArrayBuffer")) {
        try {
          // Call toArrayBuffer() method
          auto toArrayBufferFunc =
              frameObj.getPropertyAsFunction(runtime, "toArrayBuffer");
          auto arrayBuffer =
              toArrayBufferFunc.call(runtime).asObject(runtime).getArrayBuffer(
                  runtime);

          // Get buffer data
          uint8_t *data = arrayBuffer.data(runtime);

          if (pixelFormat == "rgb") {
            // iOS: BGRA format (4 bytes per pixel)
            frameImage = cv::Mat(height, width, CV_8UC4, data).clone();
            cv::cvtColor(frameImage, frameImage, cv::COLOR_BGRA2BGR);
          } else {
            // Android: YUV420 format (1.5 bytes per pixel)
            cv::Mat yuvMat(height + height / 2, width, CV_8UC1, data);
            cv::cvtColor(yuvMat, frameImage, cv::COLOR_YUV2BGR_NV21);
          }
          cv::rotate(frameImage, frameImage, cv::ROTATE_90_CLOCKWISE);

        } catch (const std::exception &e) {
          frameDebug = "Error: " + std::string(e.what());
        }
      } else {
        frameDebug = "toArrayBuffer N/A";
      }

      auto endFrameExtraction = std::chrono::high_resolution_clock::now();
      double frameExtractionMs = std::chrono::duration<double, std::milli>(
                                     endFrameExtraction - startFrameExtraction)
                                     .count();

      // Run YOLO segmentation
      auto startYolo = std::chrono::high_resolution_clock::now();
      cardscanner::YoloSegmentation yolo(modelPath, 0.5f, 0.0f, 384);
      auto segResult = yolo.segment(frameImage);
      auto endYolo = std::chrono::high_resolution_clock::now();

      // If recognition is enabled, run embedding extraction and database search
      std::vector<std::vector<CardSearchResult>> cardMatches;
      std::vector<std::string> croppedImagePaths;
      double recognitionTimeMs = 0.0;
      double embeddingPreprocessMs = 0.0;
      double embeddingInferenceMs = 0.0;
      double dbSearchMs = 0.0;

      if (doRecognition && !segResult.detections.empty()) {
        auto startRecognition = std::chrono::high_resolution_clock::now();

        // Get database handle
        ObjectBoxDB *db = dbManager.getOrCreateStore(gameName);
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
                // Save cropped card image to tmp for debugging
                static int cardSaveCounter = 0;
                std::string tempDir = std::string(getenv("TMPDIR") ?: "/tmp/");
                std::string debugPath = tempDir + "card_crop_" +
                                        std::to_string(cardSaveCounter++) +
                                        ".jpg";
                cv::imwrite(debugPath, cardImg);
                croppedImagePaths.push_back("file://" + debugPath);

                // Extract embedding with timing
                auto startEmbedding = std::chrono::high_resolution_clock::now();
                auto inferenceResult =
                    cardscanner::CardScanner::runInferenceOnMat(
                        embeddingModelPath, cardImg);
                auto endEmbedding = std::chrono::high_resolution_clock::now();

                // Accumulate embedding timing (preprocess + inference only, no postprocess)
                embeddingPreprocessMs += inferenceResult.preprocessingTimeMs;
                embeddingInferenceMs += inferenceResult.inferenceTimeMs;

                // Search database
                auto startDbSearch = std::chrono::high_resolution_clock::now();
                auto matches =
                    db->search_similar_cards(inferenceResult.embedding, 3);
                auto endDbSearch = std::chrono::high_resolution_clock::now();
                dbSearchMs += std::chrono::duration<double, std::milli>(
                                  endDbSearch - startDbSearch)
                                  .count();

                cardMatches.push_back(matches);
              } else {
                cardMatches.push_back({});
              }
            } catch (const std::exception &e) {
              std::cerr << "Card recognition failed: " << e.what() << std::endl;
              cardMatches.push_back({});
            }
          }
        }

        auto endRecognition = std::chrono::high_resolution_clock::now();
        recognitionTimeMs = std::chrono::duration<double, std::milli>(
                                endRecognition - startRecognition)
                                .count();
      }

      auto endTotal = std::chrono::high_resolution_clock::now();
      double totalMs =
          std::chrono::duration<double, std::milli>(endTotal - startTotal)
              .count();

      // Convert result to JSI Object
      jsi::Object result(runtime);
      result.setProperty(
          runtime, "cardCount",
          jsi::Value(static_cast<int>(segResult.detections.size())));
      result.setProperty(runtime, "totalMs", jsi::Value(totalMs));

      // Frame extraction timing
      result.setProperty(runtime, "frameExtractionMs",
                         jsi::Value(frameExtractionMs));

      // YOLO timing breakdown
      result.setProperty(runtime, "yoloPreprocessMs",
                         jsi::Value(segResult.preprocessingTimeMs));
      result.setProperty(runtime, "yoloInferenceMs",
                         jsi::Value(segResult.inferenceTimeMs));
      result.setProperty(runtime, "yoloPostprocessMs",
                         jsi::Value(segResult.postprocessingTimeMs));

      // Embedding/recognition timing breakdown
      result.setProperty(runtime, "embeddingPreprocessMs",
                         jsi::Value(embeddingPreprocessMs));
      result.setProperty(runtime, "embeddingInferenceMs",
                         jsi::Value(embeddingInferenceMs));
      result.setProperty(runtime, "dbSearchMs", jsi::Value(dbSearchMs));
      result.setProperty(runtime, "recognitionTotalMs",
                         jsi::Value(recognitionTimeMs));
      // Return rotated frame dimensions
      result.setProperty(runtime, "frameWidth", jsi::Value(frameImage.cols));
      result.setProperty(runtime, "frameHeight", jsi::Value(frameImage.rows));
      result.setProperty(runtime, "debug",
                         jsi::String::createFromUtf8(runtime, frameDebug));

      // Convert detections array with recognition results
      jsi::Array detections(runtime, segResult.detections.size());
      for (size_t i = 0; i < segResult.detections.size(); i++) {
        const auto &det = segResult.detections[i];

        jsi::Object jsDetection(runtime);

        // Return coordinates from the rotated frame
        jsi::Object box(runtime);
        box.setProperty(runtime, "x1", jsi::Value(det.box.x1));
        box.setProperty(runtime, "y1", jsi::Value(det.box.y1));
        box.setProperty(runtime, "x2", jsi::Value(det.box.x2));
        box.setProperty(runtime, "y2", jsi::Value(det.box.y2));
        box.setProperty(runtime, "conf", jsi::Value(det.box.conf));
        jsDetection.setProperty(runtime, "box", box);

        // Add recognition matches if available
        if (i < cardMatches.size() && !cardMatches[i].empty()) {
          jsi::Array matches(runtime, cardMatches[i].size());
          for (size_t j = 0; j < cardMatches[i].size(); j++) {
            jsi::Object matchObj(runtime);
            matchObj.setProperty(runtime, "cardId",
                                 jsi::String::createFromUtf8(
                                     runtime, cardMatches[i][j].card_id));
            matchObj.setProperty(
                runtime, "name",
                jsi::String::createFromUtf8(runtime, cardMatches[i][j].name));
            matchObj.setProperty(runtime, "score",
                                 jsi::Value(cardMatches[i][j].score));
            matches.setValueAtIndex(runtime, j, matchObj);
          }
          jsDetection.setProperty(runtime, "matches", matches);
        }

        // Add cropped image path if available
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
      throw jsi::JSError(runtime,
                         std::string("Frame processing failed: ") + e.what());
    }
  };
  // 3. Wrap C++ func in jsi::Function
  auto jsiFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forUtf8(*jsiRuntime, "myCppPlugin"), 1,
      myPlugin);
  // 4. Add it to global so it can be called from JS
  jsiRuntime->global().setProperty(*jsiRuntime, "myCppPlugin",
                                   std::move(jsiFunc));

  threads::utils::unsafeSetupThreadPool();
  threads::GlobalThreadPool::initialize();
}

} // namespace rncardscanner
