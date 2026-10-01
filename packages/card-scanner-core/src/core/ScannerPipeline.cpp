#include "ScannerPipeline.h"
#include "../Constants.h"
#include "../benchmark/BenchmarkCollector.h"
#include "../utils/ImageUtils.h"
#include "../utils/QuadGeometry.h"
#include "CardSelection.h"
#include <Log.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
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

void ScannerPipeline::resetSidewaysFlipCache() {
  std::lock_guard<std::mutex> lock(flipCacheMutex);
  flipCache = FlipCache{};
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

// The cheap gates ahead of ML: the frame-rate window, then the live blur
// gate, then claiming the window. The window check runs first so a dropped
// frame never pays for blur.
static bool passesFrameGates(const cv::Mat &frameImage,
                             const ScannerConfig &config) {
  // Read the window before the clock: unthrottled, it opens at "now", and a
  // clock read first would always land just before it.
  const auto windowOpensAt =
      ScannerPipeline::mlWindowOpensAt(config.maxFrameRate);
  if (std::chrono::steady_clock::now() < windowOpensAt) {
    return false;
  }
  if (config.blurThreshold > 0.0) {
    const double blurScore = utils::ImageUtils::calculateBlurScore(frameImage);
    if (blurScore < config.blurThreshold) {
      log(LOG_LEVEL::Debug, "[CardScanner]",
          "frame skipped due to low blur score:", blurScore,
          "threshold:", config.blurThreshold);
      return false;
    }
  }
  return tryConsumeMLWindow(config.maxFrameRate);
}

ScanResult ScannerPipeline::processFrame(const cv::Mat &frameImage,
                                         const ScannerContext &ctx,
                                         MultiScanSession *session,
                                         bool forceFreeze) {
  const ScannerConfig &config = ctx.config;

  ScanResult result;
  const auto startTime = std::chrono::steady_clock::now();
  const auto elapsedMs = [&startTime] {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - startTime)
        .count();
  };

  if (!passesFrameGates(frameImage, config)) {
    result.processingTimeMs = elapsedMs();
    return result;
  }

  // Stage 1: Segmentation
  cardscanner::SegmentationResult detections;
  {
    benchmark::BenchmarkCollector::ScopedTimer timer(
        benchmark::Stage::YoloSegmentation);
    detections = performSegmentation(frameImage, ctx);
  }
  // Recorded even when zero, so an all-zero row is attributable to YOLO
  // finding nothing rather than to stages measuring 0ms.
  benchmark::BenchmarkCollector::set(
      benchmark::Metric::DetectionCount,
      static_cast<double>(detections.size()));
  if (!detections.empty()) {
    benchmark::BenchmarkCollector::set(benchmark::Metric::YoloConfidence,
                                       detections[0].box.conf);
  }

  log(LOG_LEVEL::Debug, "[CardScanner]", "detected",
      detections.size(), "cards in frame");

  const bool freeze =
      applyScanMode(detections, frameImage, config, session, forceFreeze,
                    result);

  // A frozen page or a still image gets one shot per card, so a miss there
  // buys the retries; a live feed gets the next frame instead.
  const bool thorough =
      freeze || (result.multi && result.multi->qualifies && session == nullptr);

  // One warp per kept card, after the mode has picked them; a thorough scan
  // pads each quad so a coarse mask edge cannot clip the card. Timed as YOLO,
  // where it ran on main, so benchmark numbers stay comparable.
  {
    benchmark::BenchmarkCollector::ScopedTimer timer(
        benchmark::Stage::YoloSegmentation);
    utils::dewarpDetections(frameImage, detections,
                            thorough ? constants::multi::DEWARP_PADDING : 0.0f);
  }

  // The freeze path always captures crops (the UI is built from them) and
  // writes each one before reporting it, so the path is valid on arrival.
  const bool capture = freeze || config.captureImage;
  std::string cacheDir;
  if (freeze) {
    cacheDir = pathprovider::get_cache_path();
    // A fresh name per freeze, or the host's URI-keyed image cache shows
    // the previous page behind the new cards.
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const std::string framePath = utils::ImageUtils::saveImageRGB(
        frameImage,
        cacheDir + "/multi_frame_" + std::to_string(stamp) + ".jpg");
    // Only the latest page is ever on screen; freezes run under the scan
    // claim, one at a time.
    static std::string lastFramePath;
    std::error_code ignored;
    std::filesystem::remove(lastFramePath, ignored);
    lastFramePath = framePath;
    result.frozen = true;
    session->begin(framePath, frameImage.size(), detections);
  }

  const size_t total = detections.size();
  // Stage 2-5: Process each detection through the pipeline
  for (size_t i = 0; i < total; i++) {
    try {
      auto processedCard =
          processDetection(frameImage, detections[i], ctx, thorough, capture);
      result.cards.push_back(std::move(processedCard));
    } catch (const std::exception &e) {
      // Create empty card on error
      ProcessedCard emptyCard;
      emptyCard.boundingBox =
          utils::ImageUtils::boundingBoxToRect(detections[i].box);
      emptyCard.detectionConfidence = detections[i].box.conf;
      emptyCard.quad = detections[i].quad;
      result.cards.push_back(emptyCard);
    }
    if (freeze) {
      saveCardImage(result.cards.back(), cacheDir, i);
      session->resolved(i, result.cards.back());
    }
    if (i == 0) {
      benchmark::BenchmarkCollector::endBenchmarkRecord();
    }
  }
  if (freeze) {
    session->ended();
  }

  result.processingTimeMs = elapsedMs();

  return result;
}

// The lowered YOLO confidence multi-capable modes look with, so a spread's
// faint cards are seen; nullopt when the mode keeps the normal bar.
static std::optional<float> multiThreshold(const ScannerConfig &config) {
  if (config.useDetectionSelection && config.scanMode != "single" &&
      constants::multi::SEGMENTATION_THRESHOLD < config.segmentationThreshold) {
    return constants::multi::SEGMENTATION_THRESHOLD;
  }
  return std::nullopt;
}

cardscanner::SegmentationResult
ScannerPipeline::performSegmentation(const cv::Mat &frameImage,
                                     const ScannerContext &ctx) {
  const ScannerConfig &config = ctx.config;
  if (!ctx.yolo) {
    throw std::runtime_error("YOLO model not initialized");
  }

  // applyScanMode restores the normal bar when the frame does not qualify.
  auto segResult = ctx.yolo->segment(frameImage, multiThreshold(config),
                                      config.scanMode != "single");

  // Selection disabled (benchmark): keep the historical highest-confidence
  // pick so results stay comparable across runs.
  if (!config.useDetectionSelection) {
    if (config.scanMode == "single" && !segResult.empty()) {
      auto maxConfDet = std::max_element(
          segResult.begin(), segResult.end(),
          [](const auto &a, const auto &b) { return a.box.conf < b.box.conf; });
      segResult = {*maxConfDet};
    }
  }

  return segResult;
}

bool ScannerPipeline::applyScanMode(
    std::vector<cardscanner::Detection> &detections, const cv::Mat &frameImage,
    const ScannerConfig &config, MultiScanSession *session, bool forceFreeze,
    ScanResult &result) {
  // Benchmark: performSegmentation already made its fixed pick.
  if (!config.useDetectionSelection) {
    return false;
  }

  // "single" keeps the card nearest the frame center.
  if (config.scanMode == "single") {
    selectCenterMost(detections, frameImage.size());
    return false;
  }

  // The shutter: freeze on whatever the frame holds, skipping the layout
  // checks and the stability window. One confident card is enough; the
  // fainter ones ride along, as on a qualifying page.
  if (forceFreeze && session != nullptr) {
    const bool anyConfident =
        std::any_of(detections.begin(), detections.end(),
                    [&](const cardscanner::Detection &d) {
                      return d.box.conf >= config.segmentationThreshold;
                    });
    result.multi = anyConfident ? MultiVerdict{true, ""}
                                : MultiVerdict{false, "count"};
    if (anyConfident) {
      return true;
    }
    detections.clear();
    return false;
  }

  // "auto" / "multiple": freeze on a stable multi-card layout.
  // A still image is one frame; only a live feed has a window to hold.
  result.multi = session ? session->evaluate(detections, frameImage, config)
                         : MultiScanSession::evaluateLayout(detections,
                                                            frameImage, config);
  if (result.multi->qualifies) {
    return config.freezeOnMulti && session != nullptr;
  }

  // Back to the live bar: the lowered multi threshold must not leak into
  // single-card tracking or live multiple results.
  if (multiThreshold(config)) {
    detections.erase(std::remove_if(detections.begin(), detections.end(),
                                    [&](const cardscanner::Detection &d) {
                                      return d.box.conf <
                                             config.segmentationThreshold;
                                    }),
                     detections.end());
  }
  if (config.scanMode == "auto") {
    selectCenterMost(detections, frameImage.size());
  }
  return false;
}

void ScannerPipeline::saveCardImage(ProcessedCard &card,
                                    const std::string &cacheDir,
                                    size_t index) {
  if (card.imageToSave.empty()) {
    return;
  }
  card.savedImagePath =
      utils::ImageUtils::saveCardImage(card.imageToSave, cacheDir, index);
  if (!card.savedImagePath.empty()) {
    card.imageFileSize = utils::ImageUtils::getFileSize(card.savedImagePath);
  }
  card.imageToSave.release();
}

void ScannerPipeline::saveCardImages(ScanResult &result,
                                     const ScannerConfig &config) {
  if (!config.captureImage) {
    return;
  }
  benchmark::BenchmarkCollector::ScopedTimer saveTimer(benchmark::Stage::Save);
  // Use cache directory for temporary images, not database directory
  const std::string cacheDir = pathprovider::get_cache_path();
  for (size_t i = 0; i < result.cards.size(); i++) {
    saveCardImage(result.cards[i], cacheDir, i);
  }
}

SetSymbolInfo
ScannerPipeline::detectSetSymbol(const cv::Mat &cardImage,
                                 const std::vector<CardSearchResult> &cardMatches,
                                 const ScannerContext &ctx) {
  const ScannerConfig &config = ctx.config;

  // Check if MTG config exists in gameSpecificConfig
  auto mtgConfigIt = config.gameSpecificConfig.find("mtg");
  if (mtgConfigIt == config.gameSpecificConfig.end() ||
      !mtgConfigIt->second.hasSetSymbolDetection()) {
    return SetSymbolInfo(); // Return empty if MTG config is not present
  }

  GameStorePtr setSymbolDb =
      cardscanner::DatabaseManager::getInstance().getSetSymbolStore();

  const auto &mtgConfig = mtgConfigIt->second;
  return SetSymbolProcessor::processSetSymbol(
      cardImage, cardMatches, config.disambiguationThreshold,
      mtgConfig.setSymbolConfidenceThreshold, ctx.setSymbolYolo.get(),
      ctx.setSymbolEmbedder.get(), setSymbolDb.get());
}

FABColorInfo ScannerPipeline::detectFABColorVariant(
    const cv::Mat &cardImage, const std::vector<CardSearchResult> &cardMatches,
    const ScannerContext &ctx) {
  const ScannerConfig &config = ctx.config;

  // Check if FAB config exists in gameSpecificConfig
  auto fabConfigIt = config.gameSpecificConfig.find("fab");
  if (fabConfigIt == config.gameSpecificConfig.end() ||
      !fabConfigIt->second.hasColorDetection()) {
    return FABColorInfo(); // Return empty if no FAB config
  }

  const auto &fabConfig = fabConfigIt->second;
  return FABColorProcessor::processColorVariant(
      cardImage, cardMatches, config.disambiguationThreshold,
      ctx.fabColor.get(), fabConfig.dotsRegionRatio,
      fabConfig.minDotsRegionSize);
}

// Stage 4: search the card, and on a miss search it upside down. A sideways
// quad dewarps 180 degrees off half the time, so it tries the cached flip
// first. Leaves processingImage and card.croppedImage in the orientation that
// matched and, on a miss, the best near misses on the card.
static void
recognizeInEitherOrientation(ProcessedCard &card, cv::Mat &processingImage,
                             const cardscanner::Detection &detection,
                             const ScannerContext &ctx, bool thorough) {
  const ScannerConfig &config = ctx.config;
  const bool useFlipCache = config.useSidewaysFlipCache;
  bool flipped =
      useFlipCache && detection.quadWasSideways && cachedSidewaysFlip();
  if (flipped) {
    // Into a new buffer: processingImage may still share card.croppedImage's
    // pixels, which are flipped once more below.
    cv::Mat rotated;
    cv::rotate(processingImage, rotated, cv::ROTATE_180);
    processingImage = std::move(rotated);
  }
  auto recognize = [&](const cv::Mat &image) {
    if (image.empty() || ctx.embedding == nullptr) {
      return std::vector<CardSearchResult>{};
    }
    auto outcome = SearchStrategy::searchCard(image, detection, config,
                                              *ctx.embedding, ctx.gameEmbedders);
    auto &misses = outcome.nearMisses;
    if (thorough && !misses.empty() &&
        (card.nearMisses.empty() ||
         misses[0].score > card.nearMisses[0].score)) {
      card.nearMisses = std::move(misses);
    }
    return std::move(outcome.matches);
  };
  card.matches = recognize(processingImage);
  // Thorough also covers an upright card slipped into the page upside down.
  if (card.matches.empty() && (detection.quadWasSideways || thorough)) {
    cv::Mat other;
    cv::rotate(processingImage, other, cv::ROTATE_180);
    auto otherMatches = recognize(other);
    if (!otherMatches.empty()) {
      processingImage = other;
      card.matches = std::move(otherMatches);
      flipped = !flipped;
    }
  }
  if (detection.quadWasSideways && useFlipCache && !card.matches.empty()) {
    setCachedSidewaysFlip(flipped);
  }
  // Keep the raw crop consistent with the resolved orientation - the set
  // symbol and FAB color stages read it.
  if (flipped) {
    cv::rotate(card.croppedImage, card.croppedImage, cv::ROTATE_180);
  }
  if (!card.matches.empty()) {
    card.nearMisses.clear();
  }
}

ProcessedCard
ScannerPipeline::processDetection(const cv::Mat &frameImage,
                                  const cardscanner::Detection &detection,
                                  const ScannerContext &ctx, bool thorough,
                                  bool capture) {
  const ScannerConfig &config = ctx.config;

  ProcessedCard card;
  // Store detection info
  card.boundingBox = utils::ImageUtils::boundingBoxToRect(detection.box);
  card.detectionConfidence = detection.box.conf;
  card.quad = detection.quad;

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
      log(LOG_LEVEL::Debug, "[CardScanner]",
          "low-light enhancement applied, threshold:",
          config.lowLightThreshold);
      processingImage =
          utils::ImageUtils::adjustGamma(processingImage, config.lowLightGamma);
      cv::normalize(processingImage, processingImage, 0, 255, cv::NORM_MINMAX);
    }
  }

  // Store image dimensions before saving
  card.imageWidth = processingImage.cols;
  card.imageHeight = processingImage.rows;

  // Stage 4: Recognize card, in whichever orientation matches.
  recognizeInEitherOrientation(card, processingImage, detection, ctx, thorough);

  // A quad that snapped to a sleeve edge or a neighbour dewarps garbage; on a
  // thorough scan the plain box crop is the last resort.
  if (thorough && card.matches.empty() && ctx.embedding != nullptr &&
      !detection.dewarpedCard.empty()) {
    cardscanner::Detection boxOnly = detection;
    boxOnly.dewarpedCard = cv::Mat();
    cv::Mat boxCrop = utils::ImageUtils::extractCardImage(frameImage, boxOnly);
    if (!boxCrop.empty()) {
      card.matches = SearchStrategy::searchCard(boxCrop, detection, config,
                                                *ctx.embedding,
                                                ctx.gameEmbedders)
                         .matches;
      if (!card.matches.empty()) {
        card.nearMisses.clear();
        processingImage = boxCrop;
        card.croppedImage = boxCrop;
        card.imageWidth = boxCrop.cols;
        card.imageHeight = boxCrop.rows;
      }
    }
  }

  // Disk I/O happens in saveCardImages(), after the scan lease is released;
  // kept here in resolved orientation until then.
  if (capture) {
    card.imageToSave = processingImage;
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
    card.setSymbol = detectSetSymbol(card.croppedImage, card.matches, ctx);
    // FAB: Detect color variant (with disambiguation threshold)
    card.fabColor = detectFABColorVariant(card.croppedImage, card.matches, ctx);
  }

  return card;
}

} // namespace core
} // namespace cardscanner
