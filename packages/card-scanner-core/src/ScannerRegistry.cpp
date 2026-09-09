#include "ScannerRegistry.h"
#include "Constants.h"
#include "models/CardEmbeddingModel.h"
#include "models/YoloSegmentationModel.h"
#include "models/fab/FABColorClassifier.h"
#include "models/mtg/SetSymbolEmbedder.h"
#include "models/mtg/SetSymbolYoloModel.h"
#include "utils/ImageUtils.h"
#include <Log.h>
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <stdexcept>

namespace cardscanner {

// Initialize static members
std::shared_ptr<cardscanner::YoloSegmentationModel>
    ScannerRegistry::yoloModel_ = nullptr;
std::shared_ptr<cardscanner::CardEmbeddingModel>
    ScannerRegistry::embeddingModel_ = nullptr;
std::map<std::string, std::shared_ptr<cardscanner::CardEmbeddingModel>>
    ScannerRegistry::gameEmbeddingModels_;
std::shared_ptr<cardscanner::SetSymbolYoloModel>
    ScannerRegistry::setSymbolYoloModel_ = nullptr;
std::shared_ptr<cardscanner::SetSymbolEmbedder>
    ScannerRegistry::setSymbolEmbedder_ = nullptr;
std::shared_ptr<cardscanner::FABColorClassifier>
    ScannerRegistry::fabColorClassifier_ = nullptr;
std::mutex ScannerRegistry::modelMutex_;
std::shared_timed_mutex ScannerRegistry::pipelineMutex_;
std::atomic<bool> ScannerRegistry::benchmarkRunning_{false};
ScannerConfig ScannerRegistry::config_ = {};
std::atomic<int> ScannerRegistry::maxFrameRate_{0};

ScanLease::ScanLease() {
  // Checked before the lock so a live camera feed cannot starve a benchmark
  // waiting for the exclusive acquire.
  if (ScannerRegistry::isBenchmarkRunning()) {
    return;
  }
  lock_ = std::shared_lock<std::shared_timed_mutex>(
      ScannerRegistry::pipelineMutex(), std::try_to_lock);
}

static std::mutex scanMutex;

std::unique_lock<std::mutex> tryClaimScan() {
  return std::unique_lock<std::mutex>(scanMutex, std::try_to_lock);
}

/// Queues rather than drops; internal - scanImageFile() claims it itself.
static std::unique_lock<std::mutex> claimScan() {
  return std::unique_lock<std::mutex>(scanMutex);
}

void ScannerRegistry::setConfig(ScannerConfig config) {
  std::lock_guard<std::mutex> lock(modelMutex_);
  config_ = std::move(config);
  maxFrameRate_.store(config_.maxFrameRate, std::memory_order_relaxed);
}

void ScannerRegistry::resetModelsLocked() {
  yoloModel_.reset();
  embeddingModel_.reset();
  gameEmbeddingModels_.clear();

  setSymbolYoloModel_.reset();
  setSymbolEmbedder_.reset();
  fabColorClassifier_.reset();

  try {
    cardscanner::DatabaseManager::getInstance().closeSetSymbolStore();
  } catch (const std::exception &e) {
    std::cerr << "Warning: Could not close SetSymbol store during reset: "
              << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Warning: Could not close SetSymbol store during reset"
              << std::endl;
  }
}

void ScannerRegistry::initializeModels() {
  if (benchmarkRunning_.load()) {
    throw std::runtime_error("Cannot initialize the scanner while a benchmark "
                             "is running. Wait for it to finish and try again.");
  }

  // Waits for in-flight scans, which hold pointers into what is reset below.
  std::unique_lock<std::shared_timed_mutex> exclusive(pipelineMutex_);
  std::lock_guard<std::mutex> lock(modelMutex_);

  resetModelsLocked();

  try {
    if (!config_.segmentationModelPath.empty()) {
      yoloModel_ = std::make_shared<cardscanner::YoloSegmentationModel>(
          config_.segmentationModelPath, config_.gameClassMapping,
          config_.segmentationThreshold, config_.iouThreshold,
          constants::model::DEFAULT_YOLO_IMAGE_SIZE);
    }

    if (!config_.embeddingModelPath.empty()) {
      embeddingModel_ = std::make_shared<cardscanner::CardEmbeddingModel>(
          config_.embeddingModelPath);
    }

    // Load game-specific configurations
    for (const auto &[gameName, gameConfig] : config_.gameSpecificConfig) {
      // Load game-specific embedding model if provided
      if (gameConfig.hasEmbedding()) {
        gameEmbeddingModels_[gameName] =
            std::make_shared<cardscanner::CardEmbeddingModel>(
                gameConfig.embeddingModelPath);
      }

      // MTG-specific: Set symbol detection
      if (gameName == "mtg" && gameConfig.hasSetSymbolDetection()) {
        if (!gameConfig.setSymbolDetectionModelPath.empty()) {
          setSymbolYoloModel_ = std::make_shared<cardscanner::SetSymbolYoloModel>(
              gameConfig.setSymbolDetectionModelPath,
              gameConfig.setSymbolDetectionThreshold,
              constants::model::DEFAULT_YOLO_IOU_THRESHOLD,
              gameConfig.setSymbolImageSize);
        }

        if (!gameConfig.setSymbolEmbedderModelPath.empty()) {
          setSymbolEmbedder_ = std::make_shared<cardscanner::SetSymbolEmbedder>(
              gameConfig.setSymbolEmbedderModelPath);
        }

        // Initialize set symbol store in DatabaseManager
        try {
          cardscanner::DatabaseManager::getInstance().getSetSymbolStore();
        } catch (const std::exception &e) {
          std::cerr << "Warning: Could not initialize SetSymbol store: "
                    << e.what() << std::endl;
        }
      }

      // FAB-specific: Color detection
      if (gameName == "fab" && gameConfig.hasColorDetection()) {
        fabColorClassifier_ = std::make_shared<cardscanner::FABColorClassifier>(
            gameConfig.colorDetectionModelPath);
      }
    }
  } catch (...) {
    resetModelsLocked();
    throw;
  }
}

bool ScannerRegistry::releaseModels() {
  // A scan finishes in well under a second and is worth waiting for; a
  // benchmark holds the scanner for its whole run and is not. Skip and report
  // it rather than block indefinitely - whoever is still scanning keeps the
  // models they need, and the next initialize clears everything out anyway.
  std::unique_lock<std::shared_timed_mutex> exclusive(pipelineMutex_,
                                                      std::chrono::seconds(1));
  if (!exclusive.owns_lock()) {
    log(LOG_LEVEL::Info, "[CardScanner] Scanner still busy, skipping release.");
    return false;
  }

  std::lock_guard<std::mutex> lock(modelMutex_);
  resetModelsLocked();
  return true;
}

ScannerContext ScannerRegistry::getScannerContext() {
  std::lock_guard<std::mutex> lock(modelMutex_);
  return {config_,          yoloModel_,          embeddingModel_,
          gameEmbeddingModels_, setSymbolYoloModel_, setSymbolEmbedder_,
          fabColorClassifier_};
}

ScanResult ScannerRegistry::scanImageFile(const std::string &imagePath,
                                          DatabaseManager &dbManager,
                                          std::string_view scanMode) {
  cv::Mat imageRGB = utils::ImageUtils::loadImageRGB(imagePath);

  ScanResult result;
  ScannerConfig config;
  {
    // Queues behind a live camera scan if one is running (rare).
    auto scanLock = claimScan();
    ScanLease lease;
    if (!lease) {
      throw std::runtime_error("Scanner busy: benchmark or model reload.");
    }

    // Snapshot under the lease, so a swap cannot slip in between.
    auto ctx = getScannerContext();
    if (!ctx.yoloModel || !ctx.embeddingModel) {
      throw std::runtime_error(
          "Models not initialized. Call initializeScanner() first.");
    }

    if (!scanMode.empty()) {
      ctx.config.scanMode = scanMode;
    }
    ctx.config.maxFrameRate = 0;

    result = core::ScannerPipeline::processFrame(
        imageRGB, ctx.config, dbManager, ctx.yoloModel.get(),
        ctx.embeddingModel.get(), ctx.setSymbolYoloModel.get(),
        ctx.setSymbolEmbedder.get(), ctx.fabColorClassifier.get(),
        &ctx.gameEmbeddingModels);
    config = std::move(ctx.config);
  }

  // Disk I/O off the lease - a swap waiting on the pipeline lock is not
  // blocked by JPEG encoding.
  core::ScannerPipeline::saveCardImages(result, config);
  return result;
}

int ScannerRegistry::getMaxFrameRate() {
  return maxFrameRate_.load(std::memory_order_relaxed);
}

std::shared_timed_mutex &ScannerRegistry::pipelineMutex() {
  return pipelineMutex_;
}

bool ScannerRegistry::isBenchmarkRunning() {
  return benchmarkRunning_.load();
}

void ScannerRegistry::beginBenchmark() {
  bool expected = false;
  if (!benchmarkRunning_.compare_exchange_strong(expected, true)) {
    throw std::runtime_error("A benchmark is already running.");
  }
}

void ScannerRegistry::endBenchmark() { benchmarkRunning_.store(false); }

} // namespace cardscanner
