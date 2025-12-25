#pragma once

#include "../dto/ScanResults.h"
#include "../dto/ScannerConfig.h"
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
#include <opencv2/opencv.hpp>

namespace rncardscanner {
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
   * @return ScanResult with all processed cards
   */
  static dto::ScanResult
  processFrame(const cv::Mat &frameImage, const dto::ScannerConfig &config,
               rncardscanner::DatabaseManager &dbManager,
               rncardscanner::YoloSegmentationModel *yoloModel,
               rncardscanner::CardEmbeddingModel *embeddingModel,
               rncardscanner::SetSymbolYoloModel *setSymbolYolo,
               rncardscanner::SetSymbolEmbedder *setSymbolEmbedder,
               rncardscanner::FABColorClassifier *fabColorClassifier);

private:
  /**
   * @brief Stage 1: Run YOLO segmentation and apply scan mode filter
   *
   * @param frameImage Input frame
   * @param config Scan configuration
   * @param yoloModel Segmentation model
   * @return Segmentation result (filtered by scan mode)
   */
  static rncardscanner::SegmentationResult
  performSegmentation(const cv::Mat &frameImage,
                      const dto::ScannerConfig &config,
                      rncardscanner::YoloSegmentationModel *yoloModel);

  /**
   * @brief Stage 2: Save card image to disk (if enabled)
   *
   * @param cardImage Cropped card image
   * @param config Scan configuration
   * @param index Card index
   * @return File path (empty if disabled or failed)
   */
  static std::string saveCardImage(const cv::Mat &cardImage,
                                   const dto::ScannerConfig &config,
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
  static std::vector<dto::CardMatch> recognizeCard(
      const cv::Mat &cardImage, const rncardscanner::Detection &detection,
      const dto::ScannerConfig &config, rncardscanner::DatabaseManager &dbManager,
      rncardscanner::CardEmbeddingModel *embeddingModel);

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
  static dto::SetSymbolInfo
  detectSetSymbol(const cv::Mat &cardImage,
                  const std::vector<dto::CardMatch> &cardMatches,
                  const dto::ScannerConfig &config,
                  rncardscanner::SetSymbolYoloModel *setSymbolYolo,
                  rncardscanner::SetSymbolEmbedder *setSymbolEmbedder,
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
  static dto::FABColorInfo
  detectFABColorVariant(const cv::Mat &cardImage,
                        const std::vector<dto::CardMatch> &cardMatches,
                        const dto::ScannerConfig &config,
                        rncardscanner::FABColorClassifier *fabColorClassifier);

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
  static dto::ProcessedCard
  processDetection(const cv::Mat &frameImage,
                   const rncardscanner::Detection &detection, size_t index,
                   const dto::ScannerConfig &config,
                   rncardscanner::DatabaseManager &dbManager,
                   rncardscanner::CardEmbeddingModel *embeddingModel,
                   rncardscanner::SetSymbolYoloModel *setSymbolYolo,
                   rncardscanner::SetSymbolEmbedder *setSymbolEmbedder,
                   rncardscanner::FABColorClassifier *fabColorClassifier);
};

} // namespace core
} // namespace rncardscanner
