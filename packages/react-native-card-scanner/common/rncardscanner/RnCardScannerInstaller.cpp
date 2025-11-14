#include "RnCardScannerInstaller.h"
#include "CardScanner.h"
#include "ObjectBoxTest.h"
#include "ObjectBoxDB.h"
#include "YoloSegmentation.h"
#include "CardRecognitionPipeline.h"

#include <iostream>
#include <string>
#include <memory>
#include <opencv2/opencv.hpp>

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

          // Add embedding array (256 dimensions)
          jsi::Array embedding(runtime, inferenceResult.embedding.size());
          for (size_t i = 0; i < inferenceResult.embedding.size(); i++) {
            embedding.setValueAtIndex(runtime, i, jsi::Value(static_cast<double>(inferenceResult.embedding[i])));
          }
          result.setProperty(runtime, "embedding", embedding);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Inference failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runInference",
                                   std::move(runInferenceFunc));

  // Create the 'runInferenceOnImage' host function
  auto runInferenceOnImageFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "runInferenceOnImage"), 2,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count != 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(
              runtime,
              "runInferenceOnImage expects (modelPath: string, imagePath: string)");
        }

        std::string modelPath = args[0].asString(runtime).utf8(runtime);
        std::string imagePath = args[1].asString(runtime).utf8(runtime);

        try {
          auto inferenceResult =
              cardscanner::CardScanner::runInferenceOnImage(modelPath, imagePath);

          // Create result object
          jsi::Object result(runtime);

          jsi::Array outputShape(runtime, inferenceResult.outputShape.size());
          for (size_t i = 0; i < inferenceResult.outputShape.size(); i++) {
            outputShape.setValueAtIndex(runtime, i, jsi::Value(inferenceResult.outputShape[i]));
          }
          result.setProperty(runtime, "outputShape", outputShape);

          result.setProperty(runtime, "inferenceTimeMs", jsi::Value(inferenceResult.inferenceTimeMs));

          jsi::Array embedding(runtime, inferenceResult.embedding.size());
          for (size_t i = 0; i < inferenceResult.embedding.size(); i++) {
            embedding.setValueAtIndex(runtime, i, jsi::Value(static_cast<double>(inferenceResult.embedding[i])));
          }
          result.setProperty(runtime, "embedding", embedding);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Inference failed: ") + e.what());
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
          throw jsi::JSError(runtime,
                             std::string("Tests failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runTests",
                                   std::move(runTestsFunc));

  // Create the 'loadCardEmbeddings' host function
  auto loadEmbeddingsFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "loadCardEmbeddings"), 2,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 2 || !args[0].isString() || !args[1].isString()) {
          throw jsi::JSError(
              runtime,
              "loadCardEmbeddings expects two string arguments (dbPath, jsonPath)");
        }

        std::string dbPath = args[0].asString(runtime).utf8(runtime);
        std::string jsonPath = args[1].asString(runtime).utf8(runtime);

        // Strip "file://" prefix if present
        const std::string filePrefix = "file://";
        if (dbPath.find(filePrefix) == 0) {
          dbPath = dbPath.substr(filePrefix.length());
        }
        if (jsonPath.find(filePrefix) == 0) {
          jsonPath = jsonPath.substr(filePrefix.length());
        }

        try {
          // Create ObjectBoxDB instance
          ObjectBoxDB db(dbPath);
          int count = db.load_embeddings_from_json(jsonPath);

          // Return result object with count
          jsi::Object result(runtime);
          result.setProperty(runtime, "loaded", jsi::Value(count));
          result.setProperty(runtime, "totalCards", jsi::Value(static_cast<double>(db.get_card_count())));

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Failed to load embeddings: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "loadCardEmbeddings",
                                   std::move(loadEmbeddingsFunc));

  // Create the 'searchSimilarCards' host function
  auto searchSimilarFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "searchSimilarCards"), 3,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 3 || !args[0].isString() || !args[1].isObject() || !args[2].isNumber()) {
          throw jsi::JSError(
              runtime,
              "searchSimilarCards expects (dbPath: string, embedding: number[], limit: number)");
        }

        std::string dbPath = args[0].asString(runtime).utf8(runtime);
        jsi::Array embeddingArray = args[1].asObject(runtime).asArray(runtime);
        int limit = static_cast<int>(args[2].asNumber());

        // Strip "file://" prefix if present
        const std::string filePrefix = "file://";
        if (dbPath.find(filePrefix) == 0) {
          dbPath = dbPath.substr(filePrefix.length());
        }

        try {
          // Convert JSI array to C++ vector
          std::vector<float> queryEmbedding;
          queryEmbedding.reserve(embeddingArray.size(runtime));
          for (size_t i = 0; i < embeddingArray.size(runtime); i++) {
            queryEmbedding.push_back(static_cast<float>(embeddingArray.getValueAtIndex(runtime, i).asNumber()));
          }

          // Create ObjectBoxDB instance and search
          ObjectBoxDB db(dbPath);
          auto results = db.search_similar_cards(queryEmbedding, limit);

          // Convert results to JSI array
          jsi::Array resultsArray(runtime, results.size());
          for (size_t i = 0; i < results.size(); i++) {
            jsi::Object resultObj(runtime);
            resultObj.setProperty(runtime, "cardId", jsi::String::createFromUtf8(runtime, results[i].card_id));
            resultObj.setProperty(runtime, "name", jsi::String::createFromUtf8(runtime, results[i].name));
            resultObj.setProperty(runtime, "score", jsi::Value(results[i].score));
            resultsArray.setValueAtIndex(runtime, i, resultObj);
          }

          return resultsArray;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Failed to search similar cards: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "searchSimilarCards",
                                   std::move(searchSimilarFunc));

  // Create the 'runYoloSegmentation' host function
  auto yoloSegmentFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "runYoloSegmentation"), 4,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 4 || !args[0].isString() || !args[1].isString() ||
            !args[2].isNumber() || !args[3].isNumber()) {
          throw jsi::JSError(
              runtime,
              "runYoloSegmentation expects (modelPath: string, imagePath: string, conf: number, iou: number)");
        }

        std::string modelPath = args[0].asString(runtime).utf8(runtime);
        std::string imagePath = args[1].asString(runtime).utf8(runtime);
        float conf = static_cast<float>(args[2].asNumber());
        float iou = static_cast<float>(args[3].asNumber());

        try {
          // Create YOLO model (imgsz=384 to match the model)
          cardscanner::YoloSegmentation yolo(modelPath, conf, iou, 384);
          auto segResult = yolo.segment(imagePath);

          // Get temp directory - use NSTemporaryDirectory on iOS, /data/local/tmp on Android
          std::string tempDir;
          #ifdef __APPLE__
            // On iOS, use NSTemporaryDirectory
            #include <TargetConditionals.h>
            #if TARGET_OS_IPHONE
              tempDir = std::string(getenv("TMPDIR") ?: "/tmp/");
            #else
              tempDir = "/tmp/";
            #endif
          #else
            tempDir = "/data/local/tmp/";
          #endif

          std::cout << "Using temp directory: " << tempDir << std::endl;

          // Save visualized image to temp location
          std::string outputPath = tempDir + "yolo_result.jpg";
          bool vizSaved = cv::imwrite(outputPath, segResult.visualizedImage);
          std::cout << "Saved visualized image to: " << outputPath << " = " << (vizSaved ? "OK" : "FAILED") << std::endl;

          // Save dewarped cards
          jsi::Array dewarpedPaths(runtime, segResult.detections.size());
          for (size_t i = 0; i < segResult.detections.size(); i++) {
            if (!segResult.detections[i].dewarpedCard.empty()) {
              std::string dewarpPath = tempDir + "card_" + std::to_string(i) + ".jpg";
              bool saved = cv::imwrite(dewarpPath, segResult.detections[i].dewarpedCard);
              std::cout << "Card " << i << " dewarped: " << segResult.detections[i].dewarpedCard.cols
                        << "x" << segResult.detections[i].dewarpedCard.rows
                        << ", saved to " << dewarpPath << " = " << (saved ? "OK" : "FAILED") << std::endl;
              dewarpedPaths.setValueAtIndex(runtime, i, jsi::String::createFromUtf8(runtime, dewarpPath));
            } else {
              std::cout << "Card " << i << " has no dewarped card (quad extraction failed)" << std::endl;
              dewarpedPaths.setValueAtIndex(runtime, i, jsi::Value::null());
            }
          }

          // Create result object
          jsi::Object result(runtime);
          result.setProperty(runtime, "inferenceTimeMs", jsi::Value(segResult.inferenceTimeMs));
          result.setProperty(runtime, "visualizedImagePath", jsi::String::createFromUtf8(runtime, outputPath));
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
            jsi::Array contours(runtime, det.mask.size());
            for (size_t j = 0; j < det.mask.size(); j++) {
              jsi::Array contour(runtime, det.mask[j].size());
              for (size_t k = 0; k < det.mask[j].size(); k++) {
                jsi::Object point(runtime);
                point.setProperty(runtime, "x", jsi::Value(det.mask[j][k].x));
                point.setProperty(runtime, "y", jsi::Value(det.mask[j][k].y));
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
          throw jsi::JSError(runtime, std::string("YOLO segmentation failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "runYoloSegmentation",
                                   std::move(yoloSegmentFunc));

  // Create the 'recognizeCards' host function (full pipeline)
  auto recognizeCardsFunc = jsi::Function::createFromHostFunction(
      *jsiRuntime, jsi::PropNameID::forAscii(*jsiRuntime, "recognizeCards"), 6,
      [](jsi::Runtime &runtime, const jsi::Value &thisValue,
         const jsi::Value *args, size_t count) -> jsi::Value {
        if (count < 4 || !args[0].isString() || !args[1].isString() ||
            !args[2].isString() || !args[3].isString()) {
          throw jsi::JSError(
              runtime,
              "recognizeCards expects at least (imagePath: string, yoloModelPath: string, embeddingModelPath: string, dbPath: string)");
        }

        std::string imagePath = args[0].asString(runtime).utf8(runtime);
        std::string yoloModelPath = args[1].asString(runtime).utf8(runtime);
        std::string embeddingModelPath = args[2].asString(runtime).utf8(runtime);
        std::string dbPath = args[3].asString(runtime).utf8(runtime);

        // Optional parameters with defaults
        float yoloConf = (count > 4 && args[4].isNumber()) ? static_cast<float>(args[4].asNumber()) : 0.5f;
        float yoloIou = (count > 5 && args[5].isNumber()) ? static_cast<float>(args[5].asNumber()) : 0.7f;
        int topK = (count > 6 && args[6].isNumber()) ? static_cast<int>(args[6].asNumber()) : 10;

        try {
          auto pipelineResult = cardscanner::CardRecognitionPipeline::recognize(
              imagePath, yoloModelPath, embeddingModelPath, dbPath,
              yoloConf, yoloIou, topK);

          // Create result object
          jsi::Object result(runtime);
          result.setProperty(runtime, "yoloTimeMs", jsi::Value(pipelineResult.yoloTimeMs));
          result.setProperty(runtime, "totalTimeMs", jsi::Value(pipelineResult.totalTimeMs));

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
            cardObj.setProperty(runtime, "embeddingTimeMs", jsi::Value(card.embeddingTimeMs));

            // Add matches array
            jsi::Array matches(runtime, card.matches.size());
            for (size_t j = 0; j < card.matches.size(); j++) {
              jsi::Object matchObj(runtime);
              matchObj.setProperty(runtime, "cardId", jsi::String::createFromUtf8(runtime, card.matches[j].card_id));
              matchObj.setProperty(runtime, "name", jsi::String::createFromUtf8(runtime, card.matches[j].name));
              matchObj.setProperty(runtime, "score", jsi::Value(card.matches[j].score));
              matches.setValueAtIndex(runtime, j, matchObj);
            }
            cardObj.setProperty(runtime, "matches", matches);

            cards.setValueAtIndex(runtime, i, cardObj);
          }
          result.setProperty(runtime, "cards", cards);

          return result;
        } catch (const std::exception &e) {
          throw jsi::JSError(runtime, std::string("Card recognition failed: ") + e.what());
        }
      });

  // Install the function on the global object
  jsiRuntime->global().setProperty(*jsiRuntime, "recognizeCards",
                                   std::move(recognizeCardsFunc));
}

} // namespace rncardscanner
