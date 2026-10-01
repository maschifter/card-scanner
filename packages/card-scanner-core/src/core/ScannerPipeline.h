#pragma once

#include "../models/fab/FABColorClassifier.h"
#include "../models/mtg/SetSymbolEmbedder.h"
#include "../models/mtg/SetSymbolYoloModel.h"
#include "../types/ScanResults.h"
#include "../types/ScannerConfig.h"
#include "FABColorProcessor.h"
#include "MultiScanSession.h"
#include "ScannerContext.h"
#include "SearchStrategy.h"
#include "SetSymbolProcessor.h"
#include <CardEmbeddingModel.h>
#include <DatabaseManager.h>
#include <ObjectBoxDB.h>
#include <PathProvider.h>
#include <YoloSegmentationModel.h>
#include <chrono>
#include <opencv2/opencv.hpp>

namespace cardscanner {
namespace core {

/**
 * @class ScannerPipeline
 * @brief The Director: Orchestrates the complete scanning pipeline
 *
 * Pipeline stages:
 * 1. Segmentation: Detect cards with YOLO
 * 2. Extraction: Crop card images from frame
 * 3. Persistence: Optionally save images to disk
 * 4. Recognition: Compute embeddings and search databases
 * 5. Set Symbol: Detect and match MTG set symbols
 *
 */
class ScannerPipeline {
public:
  /** @brief Steady-clock time when the throttle window next opens (now if
   *  unthrottled) - lets producers hold work until it will not be discarded. */
  static std::chrono::steady_clock::time_point mlWindowOpensAt(int maxFrameRate);

  /**
   * @brief Process a single frame through the complete pipeline
   *
   * @param frameImage Input frame (RGB)
   * @param ctx Config and models; yolo and embedding must be set
   * @param session Live multi-card session, or null for a still image
   * @param forceFreeze The shutter: freeze on any cards, skipping the layout
   *   checks and the stability window. Needs a session.
   * @return ScanResult with all processed cards. Empty if no cards detected or
   * benchmark is running.
   */
  static ScanResult processFrame(const cv::Mat &frameImage,
                                 const ScannerContext &ctx,
                                 MultiScanSession *session = nullptr,
                                 bool forceFreeze = false);

  /// Forgets the resolved 180-degree flip. The registry calls it whenever
  /// the sticky pick is forgotten too.
  static void resetSidewaysFlipCache();

  /**
   * @brief Writes each card's pending capture image to disk (if enabled).
   *
   * Call after processFrame, once the scan lease is released, so disk I/O
   * doesn't extend the scan.
   */
  static void saveCardImages(ScanResult &result, const ScannerConfig &config);

private:
  /// Writes one card's pending capture image and releases it.
  static void saveCardImage(ProcessedCard &card, const std::string &cacheDir,
                            size_t index);

  /**
   * @brief Narrows detections per scan mode: "single" keeps the center-most
   * card; "auto" and "multiple" run the multi trigger and fall back to
   * center-most / everything when it fails. Records the outcome on result.
   *
   * @param session Live session, or null for a still image
   * @return true when the frame takes the freeze path
   */
  static bool applyScanMode(std::vector<cardscanner::Detection> &detections,
                            const cv::Mat &frameImage,
                            const ScannerConfig &config,
                            MultiScanSession *session, bool forceFreeze,
                            ScanResult &result);

  /**
   * @brief Stage 1: Run YOLO segmentation. Scan-mode selection follows in
   * applyScanMode, except the benchmark's fixed highest-confidence pick.
   *
   * @param frameImage Input frame
   * @param ctx Config and the segmentation model
   * @return Segmentation result (filtered by scan mode)
   */
  static cardscanner::SegmentationResult
  performSegmentation(const cv::Mat &frameImage, const ScannerContext &ctx);

  /**
   * @brief Stage 4: Detect and match MTG set symbol
   *
   * @param cardImage Cropped card image
   * @param cardMatches Recognition results
   * @param ctx Config and the set symbol detector and embedder
   * @return SetSymbolInfo (empty if not MTG or detection failed)
   */
  static SetSymbolInfo
  detectSetSymbol(const cv::Mat &cardImage,
                  const std::vector<CardSearchResult> &cardMatches,
                  const ScannerContext &ctx);

  /**
   * @brief Stage 5: Detect FAB color variant
   *
   * @param cardImage Cropped card image
   * @param cardMatches Recognition results
   * @param ctx Config and the FAB color classifier
   * @return FABColorInfo (empty if detection failed)
   */
  static FABColorInfo
  detectFABColorVariant(const cv::Mat &cardImage,
                        const std::vector<CardSearchResult> &cardMatches,
                        const ScannerContext &ctx);

  /**
   * @brief Process a single detection through the pipeline
   *
   * @param frameImage Original frame
   * @param detection YOLO detection
   * @param ctx Config and models
   * @param thorough A miss is final for the user (frozen page, still image):
   *   retry the other orientation and the box crop, and keep the near misses.
   * @param capture Keep the crop for saving, whatever config.captureImage says.
   * @return ProcessedCard with all results
   */
  static ProcessedCard processDetection(const cv::Mat &frameImage,
                                        const cardscanner::Detection &detection,
                                        const ScannerContext &ctx,
                                        bool thorough, bool capture);
};

} // namespace core
} // namespace cardscanner
