#pragma once

#include "../dto/ScanResults.h"
#include "../dto/ScannerConfig.h"
#include "../models/mtg/SetSymbolEmbedder.h"
#include "../models/mtg/SetSymbolYoloModel.h"
#include "../utils/ImageUtils.h"
#include <ObjectBoxDB.h>
#include <opencv2/opencv.hpp>

namespace rncardscanner {
namespace core {

/**
 * @class SetSymbolProcessor
 * @brief MTG-specific set symbol detection and matching
 *
 * Responsibilities:
 * - Detect set symbol location with YOLO
 * - Crop set symbol region
 * - Compute embedding and search database
 *
 */
class SetSymbolProcessor {
public:
  /**
   * @brief Detect and match MTG set symbol
   *
   * Only processes if:
   * 1. Card is identified as MTG
   * 2. Set symbol models are available
   * 3. Top 2 matches are ambiguous (within disambiguationThreshold)
   *
   * @param cardImage Cropped card image (RGB)
   * @param cardMatches Recognition results (to check game and ambiguity)
   * @param disambiguationThreshold Min score difference to skip detection
   * @param confidenceThreshold Confidence threshold for matches
   * @param yoloModel Set symbol YOLO detector (nullable)
   * @param embedder Set symbol embedder (nullable)
   * @param database Set symbol database (nullable)
   * @return SetSymbolInfo (empty if not MTG or detection failed)
   */
  static dto::SetSymbolInfo processSetSymbol(
      const cv::Mat &cardImage, const std::vector<dto::CardMatch> &cardMatches,
      const float disambiguationThreshold, const float confidenceThreshold,
      rncardscanner::SetSymbolYoloModel *yoloModel,
      rncardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database);

private:
  /**
   * @brief Check if card is identified as MTG
   *
   * @param cardMatches Recognition results
   * @return True if first match is MTG
   */
  static bool isMTGCard(const std::vector<dto::CardMatch> &cardMatches);

  /**
   * @brief Check if all set symbol models are available
   *
   * @param yoloModel Set symbol YOLO detector
   * @param embedder Set symbol embedder
   * @param database Set symbol database
   * @return True if all models are loaded
   */
  static bool hasSetSymbolModels(rncardscanner::SetSymbolYoloModel *yoloModel,
                                 rncardscanner::SetSymbolEmbedder *embedder,
                                 ObjectBoxDB *database);

  /**
   * @brief Detect set symbol bounding box
   *
   * @param cardImage Cropped card image
   * @param yoloModel Set symbol detector
   * @return Bounding box (empty rect if not detected)
   */
  static cv::Rect
  detectSetSymbolBox(const cv::Mat &cardImage,
                     rncardscanner::SetSymbolYoloModel *yoloModel);

  /**
   * @brief Match set symbol embedding to database
   *
   * @param symbolImage Cropped set symbol image
   * @param confidenceThreshold Confidence threshold for matches
   * @param embedder Set symbol embedder
   * @param database Set symbol database
   * @return SetSymbolInfo (empty if no match or below threshold)
   */
  static dto::SetSymbolInfo
  matchSetSymbol(const cv::Mat &symbolImage, const float confidenceThreshold,
                 rncardscanner::SetSymbolEmbedder *embedder,
                 ObjectBoxDB *database);
};

} // namespace core
} // namespace rncardscanner
