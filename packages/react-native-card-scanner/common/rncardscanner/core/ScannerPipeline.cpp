#include "ScannerPipeline.h"
#include "../RnCardScannerInstaller.h"
#include "../utils/ImageUtils.h"
#include <chrono>
#include <iostream>
#include <log.h>
#include <mutex>

namespace rncardscanner {
namespace core {

// Static variables for frame rate limiting
static std::chrono::steady_clock::time_point lastMLProcessTime;
static std::mutex mlThrottleMutex;

dto::ScanResult ScannerPipeline::processFrame(
    const cv::Mat &frameImage, const dto::ScannerConfig &config,
    rncardscanner::DatabaseManager &dbManager,
    rncardscanner::YoloSegmentationModel *yoloModel,
    rncardscanner::CardEmbeddingModel *embeddingModel,
    rncardscanner::SetSymbolYoloModel *setSymbolYolo,
    rncardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    rncardscanner::FABColorClassifier *fabColorClassifier) {

  auto startTime = std::chrono::high_resolution_clock::now();

  dto::ScanResult result;

  // Check frame quality if blur threshold is enabled
  if (config.blurThreshold > 0.0) {
    double blurScore = utils::ImageUtils::calculateBlurScore(frameImage);

    if (blurScore < config.blurThreshold) {
      auto endTime = std::chrono::high_resolution_clock::now();
      result.processingTimeMs =
          std::chrono::duration<double, std::milli>(endTime - startTime)
              .count();

      log(LOG_LEVEL::Debug,
          "[RNCardScanner] "
          "Frame skipped due to low blur score: "
          "%.2f (threshold: %.2f)",
          blurScore, config.blurThreshold);

      return result; // Skip ML pipeline for blurry frames
    }
  }

  // Frame rate limiting: Use configured max frame rate
  if (config.maxFrameRate > 0) {
    int minIntervalMs =
        1000 / config.maxFrameRate; // Convert FPS to milliseconds

    std::lock_guard<std::mutex> lock(mlThrottleMutex);
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastML =
        std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              lastMLProcessTime)
            .count();

    if (timeSinceLastML < minIntervalMs) {
      // Too soon since last ML processing - skip this frame
      auto endTime = std::chrono::high_resolution_clock::now();
      result.processingTimeMs =
          std::chrono::duration<double, std::milli>(endTime - startTime)
              .count();

      return result;
    }

    lastMLProcessTime = now;
  }

  // Stage 1: Segmentation
  auto segResult = performSegmentation(frameImage, config, yoloModel);

  log(LOG_LEVEL::Debug, "[RNCardScanner] Detected %zu cards in frame",
      segResult.detections.size());
  // Stage 2-5: Process each detection through the pipeline
  for (size_t i = 0; i < segResult.detections.size(); i++) {
    try {
      auto processedCard = processDetection(
          frameImage, segResult.detections[i], i, config, dbManager,
          embeddingModel, setSymbolYolo, setSymbolEmbedder, fabColorClassifier);
      result.cards.push_back(processedCard);
    } catch (const std::exception &e) {
      // Create empty card on error
      dto::ProcessedCard emptyCard;
      emptyCard.boundingBox =
          utils::ImageUtils::boundingBoxToRect(segResult.detections[i].box);
      emptyCard.detectionConfidence = segResult.detections[i].box.conf;
      result.cards.push_back(emptyCard);
    }
  }

  // Calculate total processing time
  auto endTime = std::chrono::high_resolution_clock::now();
  result.processingTimeMs =
      std::chrono::duration<double, std::milli>(endTime - startTime).count();

  return result;
}

rncardscanner::SegmentationResult ScannerPipeline::performSegmentation(
    const cv::Mat &frameImage, const dto::ScannerConfig &config,
    rncardscanner::YoloSegmentationModel *yoloModel) {

  if (!yoloModel) {
    throw std::runtime_error("YOLO model not initialized");
  }

  // Run YOLO segmentation
  auto segResult = yoloModel->segment(frameImage);

  // Apply scanMode: keep only highest confidence if "single"
  if (config.scanMode == "single" && !segResult.detections.empty()) {
    auto maxConfDet = std::max_element(
        segResult.detections.begin(), segResult.detections.end(),
        [](const auto &a, const auto &b) { return a.box.conf < b.box.conf; });
    segResult.detections = {*maxConfDet};
  }

  return segResult;
}

std::string ScannerPipeline::saveCardImage(const cv::Mat &cardImage,
                                           const dto::ScannerConfig &config,
                                           size_t index) {
  if (!config.captureImage || cardImage.empty()) {
    return "";
  }

  // Use cache directory for temporary images, not database directory
  std::string cacheDir = pathprovider::get_cache_path();
  return utils::ImageUtils::saveCardImage(cardImage, cacheDir, index);
}

std::vector<dto::CardMatch> ScannerPipeline::recognizeCard(
    const cv::Mat &cardImage, const rncardscanner::Detection &detection,
    const dto::ScannerConfig &config, rncardscanner::DatabaseManager &dbManager,
    rncardscanner::CardEmbeddingModel *defaultEmbeddingModel) {

  if (cardImage.empty() || !defaultEmbeddingModel) {
    return {};
  }

  // Search databases using SearchStrategy with per-game embeddings
  // SearchStrategy will compute game-specific embeddings for each probable game
  return SearchStrategy::searchCard(cardImage, detection, config, dbManager,
                                    defaultEmbeddingModel);
}

dto::SetSymbolInfo ScannerPipeline::detectSetSymbol(
    const cv::Mat &cardImage, const std::vector<dto::CardMatch> &cardMatches,
    const dto::ScannerConfig &config,
    rncardscanner::SetSymbolYoloModel *setSymbolYolo,
    rncardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    ObjectBoxDB *setSymbolDb) {

  // Check if MTG config exists in gameSpecificConfig
  auto mtgConfigIt = config.gameSpecificConfig.find("mtg");
  if (mtgConfigIt == config.gameSpecificConfig.end() ||
      !mtgConfigIt->second.hasSetSymbolDetection()) {
    return dto::SetSymbolInfo(); // Return empty if MTG config is not present
  }

  const auto &mtgConfig = mtgConfigIt->second;
  return SetSymbolProcessor::processSetSymbol(
      cardImage, cardMatches, config.disambiguationThreshold,
      mtgConfig.setSymbolConfidenceThreshold, setSymbolYolo, setSymbolEmbedder,
      setSymbolDb);
}

dto::FABColorInfo ScannerPipeline::detectFABColorVariant(
    const cv::Mat &cardImage, const std::vector<dto::CardMatch> &cardMatches,
    const dto::ScannerConfig &config,
    rncardscanner::FABColorClassifier *fabColorClassifier) {

  // Check if FAB config exists in gameSpecificConfig
  auto fabConfigIt = config.gameSpecificConfig.find("fab");
  if (fabConfigIt == config.gameSpecificConfig.end() ||
      !fabConfigIt->second.hasColorDetection()) {
    return dto::FABColorInfo(); // Return empty if no FAB config
  }

  const auto &fabConfig = fabConfigIt->second;
  return FABColorProcessor::processColorVariant(
      cardImage, cardMatches, config.disambiguationThreshold, fabColorClassifier,
      fabConfig.dotsRegionRatio, fabConfig.minDotsRegionSize);
}

dto::ProcessedCard ScannerPipeline::processDetection(
    const cv::Mat &frameImage, const rncardscanner::Detection &detection,
    size_t index, const dto::ScannerConfig &config,
    rncardscanner::DatabaseManager &dbManager,
    rncardscanner::CardEmbeddingModel *embeddingModel,
    rncardscanner::SetSymbolYoloModel *setSymbolYolo,
    rncardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    rncardscanner::FABColorClassifier *fabColorClassifier) {

  dto::ProcessedCard card;
  ObjectBoxDB *setSymbolDb = dbManager.getSetSymbolStore();
  // Store detection info
  card.boundingBox = utils::ImageUtils::boundingBoxToRect(detection.box);
  card.detectionConfidence = detection.box.conf;

  // Stage 2: Extract card image
  card.croppedImage =
      utils::ImageUtils::extractCardImage(frameImage, detection);
  if (card.croppedImage.empty()) {
    return card; // Early exit if extraction failed
  }

  // Stage 3: Apply low-light enhancement if enabled and needed
  cv::Mat processingImage = card.croppedImage;

  if (config.lowLightThreshold > 0.0 &&
      utils::ImageUtils::isLowLight(processingImage,
                                    config.lowLightThreshold)) {
    log(LOG_LEVEL::Debug,
        "[RNCardScanner] Low-light enhancement applied (threshold: %.2f)",
        config.lowLightThreshold);
    processingImage =
        utils::ImageUtils::adjustGamma(processingImage, config.lowLightGamma);
    cv::normalize(processingImage, processingImage, 0, 255, cv::NORM_MINMAX);
  }

  // Store image dimensions before saving
  card.imageWidth = processingImage.cols;
  card.imageHeight = processingImage.rows;

  card.savedImagePath = saveCardImage(processingImage, config, index);

  // Get file size after saving
  if (!card.savedImagePath.empty()) {
    card.imageFileSize = utils::ImageUtils::getFileSize(card.savedImagePath);
  }

  // Stage 4: Recognize card
  card.matches = recognizeCard(processingImage, detection, config, dbManager,
                               embeddingModel);

  // If no matches, but we have a predicted game, store it
  if (card.matches.empty()) {
    card.predictedGameName = detection.predictedGame;
    // Store YOLO confidence for the predicted game
    card.predictedGameConfidence = !detection.topGamePredictions.empty()
        ? detection.topGamePredictions[0].second
        : 0.0f;
  } else {
    // If a game was detected and recognized, use its name as the predicted game
    card.predictedGameName = card.matches[0].gameName;
    // Use the match score as confidence
    card.predictedGameConfidence = card.matches[0].score;
  }

  // Stage 5: Game-specific metadata detection
  if (card.hasMatches()) {
    // MTG: Detect set symbol (with disambiguation threshold)
    card.setSymbol =
        detectSetSymbol(card.croppedImage, card.matches, config, setSymbolYolo,
                        setSymbolEmbedder, setSymbolDb);
    // FAB: Detect color variant (with disambiguation threshold)
    card.fabColor = detectFABColorVariant(card.croppedImage, card.matches,
                                          config, fabColorClassifier);
  }

  return card;
}

} // namespace core
} // namespace rncardscanner
