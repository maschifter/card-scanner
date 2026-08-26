#include "ScannerPipeline.h"
#include "../Constants.h"
#include "../benchmark/BenchmarkCollector.h"
#include "../utils/ImageUtils.h"
#include "CardSelection.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <Log.h>
#include <mutex>
#include <shared_mutex>

namespace cardscanner {
namespace core {

// Static variables for frame rate limiting
static std::chrono::steady_clock::time_point lastMLProcessTime;
static std::mutex mlThrottleMutex;

// Last resolved 180-degree flip for sideways dewarps (stale orientation over
// a flat surface); remembering it skips the second embedding pass next frames.
struct FlipCache {
  bool flipped = false;
  std::chrono::steady_clock::time_point resolvedAt;
};
static std::mutex flipCacheMutex;
static FlipCache flipCache;

static bool cachedSidewaysFlip() {
  std::lock_guard<std::mutex> lock(flipCacheMutex);
  return flipCache.flipped &&
         std::chrono::steady_clock::now() - flipCache.resolvedAt <=
             constants::card::SIDEWAYS_FLIP_CACHE_TTL;
}

static void setCachedSidewaysFlip(bool flipped) {
  std::lock_guard<std::mutex> lock(flipCacheMutex);
  flipCache.flipped = flipped;
  flipCache.resolvedAt = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point
ScannerPipeline::mlWindowOpensAt(int maxFrameRate) {
  if (maxFrameRate <= 0) {
    return std::chrono::steady_clock::now();
  }
  std::lock_guard<std::mutex> lock(mlThrottleMutex);
  return lastMLProcessTime + std::chrono::milliseconds(1000 / maxFrameRate);
}

// Re-checks and claims the window in one lock - call only for a frame that
// reached ML.
static bool tryConsumeMLWindow(int maxFrameRate) {
  if (maxFrameRate <= 0) {
    return true;
  }
  std::lock_guard<std::mutex> lock(mlThrottleMutex);
  const auto now = std::chrono::steady_clock::now();
  if (now <
      lastMLProcessTime + std::chrono::milliseconds(1000 / maxFrameRate)) {
    return false;
  }
  lastMLProcessTime = now;
  return true;
}

ScanResult ScannerPipeline::processFrame(
    const cv::Mat &frameImage, const ScannerConfig &config,
    cardscanner::DatabaseManager &dbManager,
    cardscanner::YoloSegmentationModel *yoloModel,
    cardscanner::CardEmbeddingModel *embeddingModel,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    cardscanner::FABColorClassifier *fabColorClassifier,
    const cardscanner::GameEmbedders *gameEmbedders) {

  ScanResult result;
  const auto startTime = std::chrono::steady_clock::now();
  const auto elapsedMs = [&startTime] {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - startTime)
        .count();
  };

  // Ahead of the blur gate; no need to pay blur cost for drops
  const auto mlWindowAt = mlWindowOpensAt(config.maxFrameRate);
  if (std::chrono::steady_clock::now() < mlWindowAt) {
    result.processingTimeMs = elapsedMs();
    return result;
  }

  // Check frame quality if blur threshold is enabled
  if (config.blurThreshold > 0.0) {
    const double blurScore = utils::ImageUtils::calculateBlurScore(frameImage);

    if (blurScore < config.blurThreshold) {
      result.processingTimeMs = elapsedMs();

      log(LOG_LEVEL::Debug,
          "[CardScanner] "
          "Frame skipped due to low blur score: "
          "%.2f (threshold: %.2f)",
          blurScore, config.blurThreshold);

      return result; // Skip ML pipeline for blurry frames
    }
  }

  // Double check and claim the ML window after blur check.
  if (!tryConsumeMLWindow(config.maxFrameRate)) {
    result.processingTimeMs = elapsedMs();
    return result;
  }

  // Stage 1: Segmentation
  cardscanner::SegmentationResult segResult;
  {
    benchmark::BenchmarkCollector::ScopedTimer timer(
        benchmark::Stage::YoloSegmentation);
    segResult = performSegmentation(frameImage, config, yoloModel);
  }
  // Recorded even when zero, so an all-zero row is attributable to YOLO
  // finding nothing rather than to stages measuring 0ms.
  benchmark::BenchmarkCollector::set(
      benchmark::Metric::DetectionCount,
      static_cast<double>(segResult.detections.size()));
  if (!segResult.detections.empty()) {
    benchmark::BenchmarkCollector::set(benchmark::Metric::YoloConfidence,
                                       segResult.detections[0].box.conf);
  }

  log(LOG_LEVEL::Debug, "[CardScanner] Detected %zu cards in frame",
      segResult.detections.size());
  // Stage 2-5: Process each detection through the pipeline
  for (size_t i = 0; i < segResult.detections.size(); i++) {
    try {
      auto processedCard = processDetection(
          frameImage, segResult.detections[i], i, config, dbManager,
          embeddingModel, setSymbolYolo, setSymbolEmbedder, fabColorClassifier, gameEmbedders);
      result.cards.push_back(processedCard);
    } catch (const std::exception &e) {
      // Create empty card on error
      ProcessedCard emptyCard;
      emptyCard.boundingBox =
          utils::ImageUtils::boundingBoxToRect(segResult.detections[i].box);
      emptyCard.detectionConfidence = segResult.detections[i].box.conf;
      result.cards.push_back(emptyCard);
    }
    if (i == 0) {
      benchmark::BenchmarkCollector::endBenchmarkRecord();
    }
  }

  result.processingTimeMs = elapsedMs();

  return result;
}

cardscanner::SegmentationResult ScannerPipeline::performSegmentation(
    const cv::Mat &frameImage, const ScannerConfig &config,
    cardscanner::YoloSegmentationModel *yoloModel) {

  if (!yoloModel) {
    throw std::runtime_error("YOLO model not initialized");
  }

  // Run YOLO segmentation
  auto segResult = yoloModel->segment(frameImage);

  // Selection disabled (benchmark): keep the historical highest-confidence
  // pick so results stay comparable across runs.
  if (!config.useDetectionSelection) {
    if (config.scanMode == "single" && !segResult.detections.empty()) {
      auto maxConfDet = std::max_element(
          segResult.detections.begin(), segResult.detections.end(),
          [](const auto &a, const auto &b) { return a.box.conf < b.box.conf; });
      segResult.detections = {*maxConfDet};
    }
    return segResult;
  }

  // "single" keeps the card nearest the frame center.
  if (config.scanMode == "single") {
    selectCenterMost(segResult.detections, frameImage.size());
  }

  return segResult;
}

std::string ScannerPipeline::saveCardImage(const cv::Mat &cardImage,
                                           const ScannerConfig &config,
                                           size_t index) {
  if (!config.captureImage || cardImage.empty()) {
    return "";
  }

  // Use cache directory for temporary images, not database directory
  std::string cacheDir = pathprovider::get_cache_path();
  return utils::ImageUtils::saveCardImage(cardImage, cacheDir, index);
}

std::vector<CardMatch> ScannerPipeline::recognizeCard(
    const cv::Mat &cardImage, const cardscanner::Detection &detection,
    const ScannerConfig &config, cardscanner::DatabaseManager &dbManager,
    cardscanner::CardEmbeddingModel *defaultEmbeddingModel,
    const cardscanner::GameEmbedders *gameEmbedders) {

  if (cardImage.empty() || !defaultEmbeddingModel) {
    return {};
  }

  // Search databases using SearchStrategy with per-game embeddings
  // SearchStrategy will compute game-specific embeddings for each probable game
  return SearchStrategy::searchCard(cardImage, detection, config, dbManager,
                                    *defaultEmbeddingModel, gameEmbedders);
}

SetSymbolInfo ScannerPipeline::detectSetSymbol(
    const cv::Mat &cardImage, const std::vector<CardMatch> &cardMatches,
    const ScannerConfig &config,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    ObjectBoxDB *setSymbolDb) {

  // Check if MTG config exists in gameSpecificConfig
  auto mtgConfigIt = config.gameSpecificConfig.find("mtg");
  if (mtgConfigIt == config.gameSpecificConfig.end() ||
      !mtgConfigIt->second.hasSetSymbolDetection()) {
    return SetSymbolInfo(); // Return empty if MTG config is not present
  }

  const auto &mtgConfig = mtgConfigIt->second;
  return SetSymbolProcessor::processSetSymbol(
      cardImage, cardMatches, config.disambiguationThreshold,
      mtgConfig.setSymbolConfidenceThreshold, setSymbolYolo, setSymbolEmbedder,
      setSymbolDb);
}

FABColorInfo ScannerPipeline::detectFABColorVariant(
    const cv::Mat &cardImage, const std::vector<CardMatch> &cardMatches,
    const ScannerConfig &config,
    cardscanner::FABColorClassifier *fabColorClassifier) {

  // Check if FAB config exists in gameSpecificConfig
  auto fabConfigIt = config.gameSpecificConfig.find("fab");
  if (fabConfigIt == config.gameSpecificConfig.end() ||
      !fabConfigIt->second.hasColorDetection()) {
    return FABColorInfo(); // Return empty if no FAB config
  }

  const auto &fabConfig = fabConfigIt->second;
  return FABColorProcessor::processColorVariant(
      cardImage, cardMatches, config.disambiguationThreshold, fabColorClassifier,
      fabConfig.dotsRegionRatio, fabConfig.minDotsRegionSize);
}

ProcessedCard ScannerPipeline::processDetection(
    const cv::Mat &frameImage, const cardscanner::Detection &detection,
    size_t index, const ScannerConfig &config,
    cardscanner::DatabaseManager &dbManager,
    cardscanner::CardEmbeddingModel *embeddingModel,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    cardscanner::FABColorClassifier *fabColorClassifier,
    const cardscanner::GameEmbedders *gameEmbedders) {

  ProcessedCard card;
  GameStorePtr setSymbolDb = dbManager.getSetSymbolStore();
  // Store detection info
  card.boundingBox = utils::ImageUtils::boundingBoxToRect(detection.box);
  card.detectionConfidence = detection.box.conf;

  // Declared outside the timed block below, since it is still needed after it.
  cv::Mat processingImage;

  // Stages 2-3: Extract card image (+ optional low-light correction). Scoped
  // so preproc_ms does not swallow the save, embedding and DB search below.
  {
    benchmark::BenchmarkCollector::ScopedTimer preprocTimer(
        benchmark::Stage::Preproc);

    card.croppedImage =
        utils::ImageUtils::extractCardImage(frameImage, detection);
    if (card.croppedImage.empty()) {
      return card; // Early exit if extraction failed
    }

    processingImage = card.croppedImage;

    if (config.lowLightThreshold > 0.0 &&
        utils::ImageUtils::isLowLight(processingImage,
                                      config.lowLightThreshold)) {
      log(LOG_LEVEL::Debug,
          "[CardScanner] Low-light enhancement applied (threshold: %.2f)",
          config.lowLightThreshold);
      processingImage =
          utils::ImageUtils::adjustGamma(processingImage, config.lowLightGamma);
      cv::normalize(processingImage, processingImage, 0, 255, cv::NORM_MINMAX);
    }
  }

  // Store image dimensions before saving
  card.imageWidth = processingImage.cols;
  card.imageHeight = processingImage.rows;

  // Stage 4: Recognize card. A sideways quad dewarps 180 degrees off half the
  // time - try the cached flip first, then the opposite one on a miss.
  const bool useFlipCache = config.useSidewaysFlipCache;
  bool flipped =
      useFlipCache && detection.quadWasSideways && cachedSidewaysFlip();
  if (flipped) {
    cv::Mat rotated;
    cv::rotate(processingImage, rotated, cv::ROTATE_180);
    processingImage = std::move(rotated);
  }
  card.matches = recognizeCard(processingImage, detection, config, dbManager,
                               embeddingModel, gameEmbedders);
  if (detection.quadWasSideways) {
    if (card.matches.empty()) {
      cv::Mat other;
      cv::rotate(processingImage, other, cv::ROTATE_180);
      auto otherMatches =
          recognizeCard(other, detection, config, dbManager, embeddingModel, gameEmbedders);
      if (!otherMatches.empty()) {
        processingImage = other;
        card.matches = std::move(otherMatches);
        flipped = !flipped;
      }
    }
    if (useFlipCache && !card.matches.empty()) {
      setCachedSidewaysFlip(flipped);
    }
    // Keep the raw crop consistent with the resolved orientation - the set
    // symbol and FAB color stages read it.
    if (flipped) {
      cv::rotate(card.croppedImage, card.croppedImage, cv::ROTATE_180);
    }
  }

  // Disk I/O, measured because the live-camera path pays it too; runs after
  // recognition so the image saves in its resolved orientation.
  {
    benchmark::BenchmarkCollector::ScopedTimer saveTimer(benchmark::Stage::Save);

    card.savedImagePath = saveCardImage(processingImage, config, index);

    // Get file size after saving
    if (!card.savedImagePath.empty()) {
      card.imageFileSize = utils::ImageUtils::getFileSize(card.savedImagePath);
    }
  }

  // If no matches, but we have a predicted game, store it
  if (card.matches.empty()) {
    card.predictedGameName = detection.predictedGame;
    // box.conf is the winning class's confidence, i.e. the predicted game's
    card.predictedGameConfidence = detection.box.conf;
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
                        setSymbolEmbedder, setSymbolDb.get());
    // FAB: Detect color variant (with disambiguation threshold)
    card.fabColor = detectFABColorVariant(card.croppedImage, card.matches,
                                          config, fabColorClassifier);
  }

  return card;
}

} // namespace core
} // namespace cardscanner
