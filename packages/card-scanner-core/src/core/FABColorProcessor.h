#pragma once

#include "../types/ScanResults.h"
#include "../models/fab/FABColorClassifier.h"
#include "../utils/ImageUtils.h"
#include <opencv2/opencv.hpp>

namespace cardscanner {
namespace core {

/**
 * @class FABColorProcessor
 * @brief FAB-specific color variant detection
 *
 * Responsibilities:
 * - Extract 3-dots region from card
 * - Classify color using FABColorClassifier model
 *
 */
class FABColorProcessor {
public:
  /**
   * @brief Detect and classify FAB color variant
   *
   * Only processes if:
   * 1. Card is identified as FAB
   * 2. FAB color classifier model is available
   * 3. Top 2 matches are ambiguous (within disambiguationThreshold)
   *
   * @param cardImage Cropped card image (RGB)
   * @param cardMatches Recognition results (to check game and ambiguity)
   * @param disambiguationThreshold Min score difference to skip detection
   * @param fabClassifier FAB color classifier model (nullable)
   * @param dotsRegionRatio Ratio of card size to extract for dots region
   * @param minDotsRegionSize Minimum region size in pixels
   * @return FABColorInfo (empty if not FAB or detection failed)
   */
  static FABColorInfo
  processColorVariant(const cv::Mat &cardImage,
                      const std::vector<CardMatch> &cardMatches,
                      float disambiguationThreshold,
                      cardscanner::FABColorClassifier *fabClassifier,
                      double dotsRegionRatio, int minDotsRegionSize);

private:
  /**
   * @brief Check if card is identified as FAB
   *
   * @param cardMatches Recognition results
   * @return True if first match is FAB
   */
  static bool isFABCard(const std::vector<CardMatch> &cardMatches);

  /**
   * @brief Extract the 3-dots indicator region from the top-left corner
   *
   * @param cardImage Dewarped card image
   * @param dotsRegionRatio Ratio of card size to extract for dots region
   * @param minDotsRegionSize Minimum region size in pixels
   * @return Cropped 3-dots region (square aspect ratio)
   */
  static cv::Mat extractDotsRegion(const cv::Mat &cardImage,
                                   double dotsRegionRatio,
                                   int minDotsRegionSize);
};

} // namespace core
} // namespace cardscanner
