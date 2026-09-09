#pragma once

#include "core/ScannerPipeline.h"
#include "types/ScanResults.h"
#include "types/ScannerConfig.h"
#include <DatabaseManager.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace cardscanner {

/**
 * @brief Config and model set the scanner pipeline runs against, captured
 * atomically so a concurrent model swap cannot mix generations.
 */
struct ScannerContext {
  ScannerConfig config;
  std::shared_ptr<cardscanner::YoloSegmentationModel> yoloModel;
  std::shared_ptr<cardscanner::CardEmbeddingModel> embeddingModel;
  /// Per-game embedders; games absent here use embeddingModel.
  GameEmbedders gameEmbeddingModels;
  std::shared_ptr<cardscanner::SetSymbolYoloModel> setSymbolYoloModel;
  std::shared_ptr<cardscanner::SetSymbolEmbedder> setSymbolEmbedder;
  std::shared_ptr<cardscanner::FABColorClassifier> fabColorClassifier;
};

/**
 * @class ScanLease
 * @brief Permission to run one scan against the current models.
 *
 * Contextually false when a benchmark, an initialize or a release owns the
 * scanner - the caller should skip the frame rather than block, which is what
 * a live camera wants. Holds the shared pipeline lock for its lifetime, so the
 * models and stores a scan reaches for cannot be swapped underneath it.
 *
 * Lives here rather than inside ScannerPipeline so that core/ takes its inputs
 * as parameters and never calls back into the registry that drives it.
 */
class ScanLease {
public:
  ScanLease();
  explicit operator bool() const { return lock_.owns_lock(); }

private:
  std::shared_lock<std::shared_timed_mutex> lock_;
};

/**
 * @brief The right to be the only scan running. Claimed
 * before ScanLease, never after.
 */
std::unique_lock<std::mutex> tryClaimScan(); // Drops rather than queues.

/**
 * @class ScannerRegistry
 * @brief Owns the scanner's config, models and lifetime locking.
 *
 * Platform-agnostic - no JSI, no React Native, no camera. Integration layers
 * drive it through setConfig() + initializeModels() and read it through
 * getScannerContext().
 *
 * ponytail: process-global statics. The pipeline reads this from the hot path,
 * so an instance would have to be threaded through processFrame ->
 * processDetection -> recognizeCard -> searchAcrossGames. Make it an instance
 * only if one process ever needs two independent scanners.
 */
class ScannerRegistry {
public:
  /**
   * @brief Replaces the config the next initializeModels() will build from,
   * and refreshes the lock-free max frame rate cache.
   */
  static void setConfig(ScannerConfig config);

  // Snapshot of the config and every model the pipeline needs, taken under one
  // lock.
  static ScannerContext getScannerContext();

  // Initialize models with configuration
  static void initializeModels();

  /**
   * @brief Releases models and frees resources. Waits up to one second for
   * in-flight scans; returns false without releasing when the scanner stays
   * busy (for example, a running benchmark).
   */
  static bool releaseModels();

  /**
   * @brief Runs the full pipeline over an image file. The file is read as BGR
   * and converted to RGB so it matches what the camera path feeds in. Calls
   * claim scan, ignores maxFrameRate.
   * @param scanMode Optional override for the scan mode in the config.
   * If empty, uses the config's scanMode.
   *
   * @throws std::runtime_error if the models are not initialized, the image
   * cannot be read, or the pipeline is busy.
   */
  static ScanResult scanImageFile(const std::string &imagePath,
                                  DatabaseManager &dbManager,
                                  std::string_view scanMode = {});

  // Cheap per-frame accessor - lock-free, never blocks on model loading.
  static int getMaxFrameRate();

  /**
   * @brief Guards a pipeline call against the models and stores it uses being
   * swapped underneath it. Shared by concurrent scans, taken exclusively by a
   * benchmark run, initializeModels() and releaseModels(), which each wait for
   * in-flight scans to return. Always taken before modelMutex_, never after.
   */
  static std::shared_timed_mutex &pipelineMutex();

  /**
   * @brief True while a benchmark run is in progress. Scans check this before
   * the shared lock, so a live camera feed cannot starve a benchmark.
   */
  static bool isBenchmarkRunning();

  /**
   * @brief Claims the benchmark slot for the calling thread. Take
   * pipelineMutex_ exclusively after it to drain the scans already running.
   * @throws std::runtime_error if a benchmark is already running.
   */
  static void beginBenchmark();

  /**
   * @brief Releases the benchmark slot. Pair with beginBenchmark().
   */
  static void endBenchmark();

private:
  static void resetModelsLocked();

  static std::shared_ptr<cardscanner::YoloSegmentationModel> yoloModel_;
  static std::shared_ptr<cardscanner::CardEmbeddingModel>
      embeddingModel_; // Default/fallback embedder
  static std::map<std::string, std::shared_ptr<cardscanner::CardEmbeddingModel>>
      gameEmbeddingModels_; // Per-game embedders
  static std::shared_ptr<cardscanner::SetSymbolYoloModel> setSymbolYoloModel_;
  static std::shared_ptr<cardscanner::SetSymbolEmbedder> setSymbolEmbedder_;
  static std::shared_ptr<cardscanner::FABColorClassifier> fabColorClassifier_;
  static std::mutex modelMutex_;
  static std::shared_timed_mutex pipelineMutex_;
  static std::atomic<bool> benchmarkRunning_;
  static ScannerConfig config_;
  static std::atomic<int> maxFrameRate_;
};

} // namespace cardscanner
