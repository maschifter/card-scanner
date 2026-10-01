#pragma once

#include "core/MultiScanSession.h"
#include "core/ScannerContext.h"
#include "core/ScannerPipeline.h"
#include "types/BenchmarkRecord.h"
#include "types/ScanResults.h"
#include "types/ScannerConfig.h"
#include <DatabaseManager.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace cardscanner {

/**
 * @brief The right to be the only scan running. Claimed
 * before scan() takes the pipeline lease, never after.
 */
std::unique_lock<std::mutex> tryClaimScan(); // Drops rather than queues.

/// Per-scan overrides, applied to a local copy of the config.
struct ScanOptions {
  std::string_view scanMode;    // empty: the config's mode
  bool ignoreFrameRate = false; // stills bypass the live-feed throttle
  bool recordTimings = false;   // benchmark record around the pipeline
  bool live = false;            // camera frame: a multi-card layout may freeze
  bool forceFreeze = false;     // shutter: live frame freezes on any cards
};

/**
 * @class ScannerRegistry
 * @brief Owns the scanner's config, models and lifetime locking.
 *
 * Platform-agnostic - no JSI, no React Native, no camera. Integration layers
 * drive it through setConfig(), initializeModels() and scan().
 *
 * ponytail: process-global statics. The pipeline reads this from the hot path,
 * so an instance would have to be threaded through processFrame ->
 * processDetection -> SearchStrategy::searchCard. Make it an instance
 * only if one process ever needs two independent scanners.
 */
class ScannerRegistry {
public:
  /**
   * @brief Replaces the config the next initializeModels() will build from,
   * and refreshes the lock-free max frame rate cache. Every config enters
   * through here, so this is where ScannerConfig::validate() runs.
   * @throws std::runtime_error when validate() rejects the config.
   */
  static void setConfig(ScannerConfig config);

  // Initialize models with configuration
  static void initializeModels();

  /**
   * @brief Releases models and frees resources. Waits up to one second for
   * in-flight scans; returns false without releasing when the scanner stays
   * busy (for example, a running benchmark).
   */
  static bool releaseModels();

  /// One scan under the lease (the claim is the caller's), image save after it.
  /// Nullopt while a benchmark or reload owns the scanner; throws if no models.
  /// A live scan that freezes pauses the registry before returning, so the
  /// next claim is turned away until resumeScanning().
  static std::optional<ScanResult> scan(const cv::Mat &rgb,
                                        const ScanOptions &options = {});

  /// The multi-card freeze state. Hosts set its listener once to receive the
  /// stream; the pipeline drives it during a live scan().
  static core::MultiScanSession &multiScanSession();

  /// Turns live frames away at tryClaimScan() until resumeScanning(). The
  /// multi-card freeze latches this itself; still-image scans ignore it.
  static void pauseScanning();

  /// Accepts frames again: clears the pause, every session, and the caches
  /// that carry state across frames (sticky pick, sideways flip).
  static void resumeScanning();

  static bool isScanningPaused();

  /// Switches the scan mode in place, no model reload; the next frame picks
  /// it up. Resets the sessions and cross-frame caches like resumeScanning().
  /// @throws std::runtime_error on a mode validate() would reject.
  static void setScanMode(const std::string &scanMode);

  /// scan() over an image file read as RGB, like a camera frame; queues on the
  /// scan claim, throttle off. Throws when busy, unreadable or without models.
  static ScanResult scanImageFile(const std::string &imagePath,
                                  std::string_view scanMode = {});

  /// Swaps a game's database file under the exclusive lock; false on failure.
  static bool swapDatabase(DatabaseManager &dbManager,
                           const std::string &gameName,
                           const std::string &sourcePath);

  /// Deletes a game's database directory under the same exclusive lock.
  static bool deleteDatabase(DatabaseManager &dbManager,
                             const std::string &gameName);

  // Cheap per-frame accessor - lock-free, never blocks on model loading.
  static int getMaxFrameRate();

  /**
   * @brief True while a benchmark run is in progress. Scans check this before
   * the shared lock, so a live camera feed cannot starve a benchmark.
   */
  static bool isBenchmarkRunning();

  /**
   * @brief Runs a benchmark over still images with the scanner to itself:
   * claims the benchmark slot, waits out the scans already in flight, then
   * hands the current models to BenchmarkRunner.
   *
   * Lives here rather than at each integration layer because the order of the
   * claim, the drain and the model check is what keeps a run from measuring a
   * pipeline somebody else is still using. Callers own only their own I/O -
   * where the images come from and what becomes of the JSON.
   *
   * @throws std::runtime_error if a benchmark is already running or the models
   * are not initialized.
   */
  static BenchmarkRunResult
  runBenchmark(const std::vector<BenchmarkImageInput> &images,
               int warmupIterations, int benchmarkIterations);

private:
  class ScanLease;

  /// Snapshot of the config and every model the pipeline needs, under one lock.
  static core::ScannerContext getScannerContext();

  static void resetModelsLocked();

  /// Every session back to idle and every cross-frame cache cleared.
  static void resetFrameState();

  /**
   * @brief Claims the benchmark slot for the calling thread. Take
   * pipelineMutex_ exclusively after it to drain the scans already running -
   * private, because runBenchmark() is the one place that pairing is made in
   * the right order.
   * @throws std::runtime_error if a benchmark is already running.
   */
  static void beginBenchmark();

  /**
   * @brief Releases the benchmark slot. Pair with beginBenchmark().
   */
  static void endBenchmark();

  static std::shared_ptr<cardscanner::YoloSegmentationModel> yoloModel_;
  static std::shared_ptr<cardscanner::CardEmbeddingModel>
      embeddingModel_; // Default/fallback embedder
  static std::map<std::string, std::shared_ptr<cardscanner::CardEmbeddingModel>>
      gameEmbeddingModels_; // Per-game embedders
  static std::shared_ptr<cardscanner::SetSymbolYoloModel> setSymbolYoloModel_;
  static std::shared_ptr<cardscanner::SetSymbolEmbedder> setSymbolEmbedder_;
  static std::shared_ptr<cardscanner::FABColorClassifier> fabColorClassifier_;
  static std::mutex modelMutex_;
  /// Shared by scans, exclusive for model and store swaps; taken before
  /// modelMutex_, never after.
  static std::shared_timed_mutex pipelineMutex_;
  static std::atomic<bool> benchmarkRunning_;
  static ScannerConfig config_;
  static std::atomic<int> maxFrameRate_;
  static std::atomic<bool> paused_;
};

} // namespace cardscanner
