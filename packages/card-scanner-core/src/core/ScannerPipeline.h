#pragma once

#include "../types/ScanResults.h"
#include "../types/ScannerConfig.h"
#include "../models/fab/FABColorClassifier.h"
#include "../models/mtg/SetSymbolEmbedder.h"
#include "../models/mtg/SetSymbolYoloModel.h"
#include "SearchStrategy.h"
#include "SetSymbolProcessor.h"
#include "FABColorProcessor.h"
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
   * @param config Scan configuration
   * @param dbManager Database manager
   * @param yoloModel Segmentation model
   * @param embeddingModel Card embedding model
   * @param setSymbolYolo Set symbol detector (nullable)
   * @param setSymbolEmbedder Set symbol embedder (nullable)
   * @param fabColorClassifier FAB color classifier (nullable)
   * @return ScanResult with all processed cards. Empty if no cards detected or benchmark is running.
   */
  static ScanResult
  processFrame(const cv::Mat &frameImage, const ScannerConfig &config,
               cardscanner::DatabaseManager &dbManager,
               cardscanner::YoloSegmentationModel *yoloModel,
               cardscanner::CardEmbeddingModel *embeddingModel,
               cardscanner::SetSymbolYoloModel *setSymbolYolo,
               cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
               cardscanner::FABColorClassifier *fabColorClassifier,
               const cardscanner::GameEmbedders *gameEmbedders);

private:
  /**
   * @brief Stage 1: Run YOLO segmentation and select reportable detections
   * (group/duplicate filtering, scan-mode selection)
   *
   * @param frameImage Input frame
   * @param config Scan configuration
   * @param yoloModel Segmentation model
   * @return Segmentation result (filtered by scan mode)
   */
  static cardscanner::SegmentationResult
  performSegmentation(const cv::Mat &frameImage,
                      const ScannerConfig &config,
                      cardscanner::YoloSegmentationModel *yoloModel);

  /**
   * @brief Stage 2: Save card image to disk (if enabled)
   *
   * @param cardImage Cropped card image
   * @param config Scan configuration
   * @param index Card index
   * @return File path (empty if disabled or failed)
   */
  static std::string saveCardImage(const cv::Mat &cardImage,
                                   const ScannerConfig &config,
                                   size_t index);

  /**
   * @brief Stage 3: Recognize card using embedding and database search
   *
   * @param cardImage Cropped card image
   * @param detection YOLO detection (for game predictions)
   * @param config Scan configuration
   * @param dbManager Database manager
   * @param embeddingModel Card embedding model
   * @return Vector of card matches (empty if no confident match)
   */
  static std::vector<CardMatch> recognizeCard(
      const cv::Mat &cardImage, const cardscanner::Detection &detection,
      const ScannerConfig &config, cardscanner::DatabaseManager &dbManager,
      cardscanner::CardEmbeddingModel *embeddingModel,
      const cardscanner::GameEmbedders *gameEmbedders);

  /**
   * @brief Stage 4: Detect and match MTG set symbol
   *
   * @param cardImage Cropped card image
   * @param cardMatches Recognition results
   * @param config Scan configuration
   * @param setSymbolYolo Set symbol detector
   * @param setSymbolEmbedder Set symbol embedder
   * @return SetSymbolInfo (empty if not MTG or detection failed)
   */
  static SetSymbolInfo
  detectSetSymbol(const cv::Mat &cardImage,
                  const std::vector<CardMatch> &cardMatches,
                  const ScannerConfig &config,
                  cardscanner::SetSymbolYoloModel *setSymbolYolo,
                  cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
                  ObjectBoxDB *setSymbolDb);

  /**
   * @brief Stage 5: Detect FAB color variant
   *
   * @param cardImage Cropped card image
   * @param cardMatches Recognition results
   * @param config Scan configuration
   * @param fabColorClassifier FAB color classifier model
   * @return FABColorInfo (empty if detection failed)
   */
  static FABColorInfo
  detectFABColorVariant(const cv::Mat &cardImage,
                        const std::vector<CardMatch> &cardMatches,
                        const ScannerConfig &config,
                        cardscanner::FABColorClassifier *fabColorClassifier);

  /**
   * @brief Process a single detection through the pipeline
   *
   * @param frameImage Original frame
   * @param detection YOLO detection
   * @param index Card index
   * @param config Scan configuration
   * @param dbManager Database manager
   * @param embeddingModel Card embedding model
   * @param setSymbolYolo Set symbol detector
   * @param setSymbolEmbedder Set symbol embedder
   * @param fabColorClassifier FAB color classifier
   * @return ProcessedCard with all results
   */
  static ProcessedCard
  processDetection(const cv::Mat &frameImage,
                   const cardscanner::Detection &detection, size_t index,
                   const ScannerConfig &config,
                   cardscanner::DatabaseManager &dbManager,
                   cardscanner::CardEmbeddingModel *embeddingModel,
                   cardscanner::SetSymbolYoloModel *setSymbolYolo,
                   cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
                   cardscanner::FABColorClassifier *fabColorClassifier,
    const cardscanner::GameEmbedders *gameEmbedders);
};

} // namespace core
} // namespace cardscanner
