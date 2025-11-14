#include "CardRecognitionPipeline.h"
#include "CardScanner.h"
#include "YoloSegmentation.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

cv::Mat CardRecognitionPipeline::cropCard(const cv::Mat &img, float x1,
                                           float y1, float x2, float y2) {
  // Clamp coordinates to image bounds
  int ix1 = std::max(0, static_cast<int>(x1));
  int iy1 = std::max(0, static_cast<int>(y1));
  int ix2 = std::min(img.cols, static_cast<int>(x2));
  int iy2 = std::min(img.rows, static_cast<int>(y2));

  // Ensure valid crop region
  if (ix2 <= ix1 || iy2 <= iy1) {
    throw std::runtime_error("Invalid crop region");
  }

  cv::Rect roi(ix1, iy1, ix2 - ix1, iy2 - iy1);
  return img(roi).clone();
}

PipelineResult
CardRecognitionPipeline::recognize(const std::string &imagePath,
                                   const std::string &yoloModelPath,
                                   const std::string &embeddingModelPath,
                                   const std::string &dbPath, float yoloConf,
                                   float yoloIou, int topK) {

  auto pipelineStart = std::chrono::high_resolution_clock::now();

  std::cout << "========================================" << std::endl;
  std::cout << "Starting card recognition pipeline..." << std::endl;
  std::cout << "========================================" << std::endl;

  // Load image once for the entire pipeline
  std::string cleanImagePath = imagePath;
  const std::string filePrefix = "file://";
  if (cleanImagePath.find(filePrefix) == 0) {
    cleanImagePath = cleanImagePath.substr(filePrefix.length());
  }

  auto imgLoadStart = std::chrono::high_resolution_clock::now();
  cv::Mat img = cv::imread(cleanImagePath);
  auto imgLoadEnd = std::chrono::high_resolution_clock::now();
  double imgLoadMs = std::chrono::duration_cast<std::chrono::microseconds>(imgLoadEnd - imgLoadStart).count() / 1000.0;

  if (img.empty()) {
    throw std::runtime_error("Failed to load image: " + cleanImagePath);
  }

  std::cout << "📷 Loaded image: " << img.cols << "x" << img.rows << " in " << imgLoadMs << "ms" << std::endl;

  // Step 1: Run YOLO segmentation to detect cards (using the loaded image)
  auto yoloStart = std::chrono::high_resolution_clock::now();
  YoloSegmentation yolo(yoloModelPath, yoloConf, yoloIou, 384);
  auto yoloResult = yolo.segment(img); // Pass cv::Mat instead of path!
  auto yoloEnd = std::chrono::high_resolution_clock::now();
  double yoloTotalMs = std::chrono::duration_cast<std::chrono::microseconds>(yoloEnd - yoloStart).count() / 1000.0;

  std::cout << "✅ YOLO detected " << yoloResult.detections.size() << " cards" << std::endl;
  std::cout << "   - YOLO inference only: " << yoloResult.inferenceTimeMs << "ms" << std::endl;
  std::cout << "   - YOLO total (incl. postprocessing): " << yoloTotalMs << "ms" << std::endl;

  // Step 2: For each detected card, extract embedding and search database
  std::vector<CardRecognitionResult> results;

  // Get temp directory
  std::string tempDir;
  #ifdef __APPLE__
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

  // Open database once for all searches
  auto dbStart = std::chrono::high_resolution_clock::now();
  std::string cleanDbPath = dbPath;
  if (cleanDbPath.find(filePrefix) == 0) {
    cleanDbPath = cleanDbPath.substr(filePrefix.length());
  }
  ObjectBoxDB db(cleanDbPath);
  auto dbEnd = std::chrono::high_resolution_clock::now();
  double dbOpenMs = std::chrono::duration_cast<std::chrono::microseconds>(dbEnd - dbStart).count() / 1000.0;
  std::cout << "📦 Database opened in " << dbOpenMs << "ms" << std::endl;
  std::cout << std::endl;

  for (size_t i = 0; i < yoloResult.detections.size(); i++) {
    const auto &detection = yoloResult.detections[i];
    auto cardStart = std::chrono::high_resolution_clock::now();

    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "📇 Card " << (i + 1) << "/" << yoloResult.detections.size() << std::endl;

    try {
      cv::Mat cardImg;

      // Use dewarped card if available, otherwise fall back to bbox crop
      auto prepStart = std::chrono::high_resolution_clock::now();
      if (!detection.dewarpedCard.empty()) {
        cardImg = detection.dewarpedCard;
        std::cout << "   ✓ Using dewarped card (" << cardImg.cols << "x"
                  << cardImg.rows << ")" << std::endl;
      } else {
        cardImg = cropCard(img, detection.box.x1, detection.box.y1,
                          detection.box.x2, detection.box.y2);
        std::cout << "   ⚠ Using bbox crop (dewarping failed)" << std::endl;
      }

      auto prepEnd = std::chrono::high_resolution_clock::now();
      double prepMs = std::chrono::duration_cast<std::chrono::microseconds>(prepEnd - prepStart).count() / 1000.0;

      // Extract embedding from card (directly from cv::Mat, no disk I/O!)
      auto embeddingStart = std::chrono::high_resolution_clock::now();
      auto inferenceResult =
          CardScanner::runInferenceOnMat(embeddingModelPath, cardImg);
      auto embeddingEnd = std::chrono::high_resolution_clock::now();

      double embeddingTimeMs =
          std::chrono::duration_cast<std::chrono::microseconds>(embeddingEnd -
                                                                 embeddingStart)
              .count() /
          1000.0;

      // Search for similar cards in database
      auto searchStart = std::chrono::high_resolution_clock::now();
      auto matches = db.search_similar_cards(inferenceResult.embedding, topK);
      auto searchEnd = std::chrono::high_resolution_clock::now();
      double searchMs = std::chrono::duration_cast<std::chrono::microseconds>(searchEnd - searchStart).count() / 1000.0;

      auto cardEnd = std::chrono::high_resolution_clock::now();
      double cardTotalMs = std::chrono::duration_cast<std::chrono::microseconds>(cardEnd - cardStart).count() / 1000.0;

      std::cout << "   ⏱️  Timings:" << std::endl;
      std::cout << "      - Image prep: " << prepMs << "ms" << std::endl;
      std::cout << "      - Embedding extraction: " << embeddingTimeMs << "ms" << std::endl;
      std::cout << "      - Database search: " << searchMs << "ms" << std::endl;
      std::cout << "      - Card total: " << cardTotalMs << "ms" << std::endl;

      std::cout << "   🎯 Found " << matches.size() << " matches";
      if (!matches.empty()) {
        std::cout << " - Top: " << matches[0].name
                  << " (" << (matches[0].score * 100.0) << "%)" << std::endl;
      } else {
        std::cout << std::endl;
      }

      // Create result for this card
      CardRecognitionResult cardResult;
      cardResult.x1 = detection.box.x1;
      cardResult.y1 = detection.box.y1;
      cardResult.x2 = detection.box.x2;
      cardResult.y2 = detection.box.y2;
      cardResult.conf = detection.box.conf;
      cardResult.matches = matches;
      cardResult.embeddingTimeMs = embeddingTimeMs;

      results.push_back(cardResult);

    } catch (const std::exception &e) {
      std::cerr << "  Failed to process card " << (i + 1) << ": " << e.what()
                << std::endl;
      // Continue with next card
    }
  }

  auto pipelineEnd = std::chrono::high_resolution_clock::now();
  double totalTimeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(pipelineEnd -
                                                             pipelineStart)
          .count() /
      1000.0;

  std::cout << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "📊 Pipeline Summary" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "   Cards detected: " << results.size() << std::endl;
  std::cout << "   Image load: " << imgLoadMs << "ms" << std::endl;
  std::cout << "   YOLO time: " << yoloTotalMs << "ms" << std::endl;
  std::cout << "   Database open: " << dbOpenMs << "ms" << std::endl;
  std::cout << "   Total time: " << totalTimeMs << "ms" << std::endl;
  std::cout << "   💡 Optimization: Loaded image once, saved ~" << imgLoadMs << "ms!" << std::endl;
  std::cout << "========================================" << std::endl;

  return {results, yoloResult.inferenceTimeMs, totalTimeMs};
}

} // namespace cardscanner
