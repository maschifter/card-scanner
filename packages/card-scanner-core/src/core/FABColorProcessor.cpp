#include "FABColorProcessor.h"
#include "../benchmark/BenchmarkCollector.h"
#include <Log.h>

namespace cardscanner {
namespace core {

using cardscanner::LOG_LEVEL;

FABColorInfo FABColorProcessor::processColorVariant(
    const cv::Mat &cardImage, const std::vector<CardMatch> &cardMatches,
    float disambiguationThreshold,
    cardscanner::FABColorClassifier *fabClassifier, double dotsRegionRatio,
    int minDotsRegionSize) {

  // Early exit: not FAB or model not available
  if (!isFABCard(cardMatches) || fabClassifier == nullptr) {
    return FABColorInfo();
  }

  if (cardImage.empty()) {
    return FABColorInfo();
  }

  // Optimization: Only search for color variant if top 2 matches are close
  // If top match is clearly the best (score difference > threshold), skip
  // detection
  if (cardMatches.size() >= 2) {
    float top1Score = cardMatches[0].score;
    float top2Score = cardMatches[1].score;
    float scoreDifference = top1Score - top2Score;

    // If difference exceeds threshold, top match is clearly best
    if (scoreDifference > disambiguationThreshold) {
      return FABColorInfo(); // Skip color detection
    }
  } else if (cardMatches.size() == 1) {
    // Only one match - no need to disambiguate
    return FABColorInfo();
  }

  try {
    log(LOG_LEVEL::Debug,
        "[CardScanner] Detecting FAB color variant with disambiguation "
        "threshold: %.2f",
        disambiguationThreshold);

    // Extract 3-dots region from top-left corner
    cv::Mat dotsRegion;
    {
      benchmark::BenchmarkCollector::ScopedTimer preprocTimer(
          benchmark::Stage::FabColorPreproc);
      dotsRegion =
          extractDotsRegion(cardImage, dotsRegionRatio, minDotsRegionSize);
    }
    if (dotsRegion.empty()) {
      return FABColorInfo();
    }

    // Classify color using model
    benchmark::BenchmarkCollector::ScopedTimer classifyTimer(
        benchmark::Stage::FabColorClassify);
    return fabClassifier->classifyColor(dotsRegion);

  } catch (const std::exception &e) {
    return FABColorInfo();
  }
}

bool FABColorProcessor::isFABCard(
    const std::vector<CardMatch> &cardMatches) {
  return !cardMatches.empty() && cardMatches[0].gameName == "fab";
}

cv::Mat FABColorProcessor::extractDotsRegion(const cv::Mat &cardImage,
                                             double dotsRegionRatio,
                                             int minDotsRegionSize) {
  // Extract top-left square region containing the 3 colored dots
  // Use configurable ratio to reliably capture the color indicators

  int regionSize = static_cast<int>(std::min(cardImage.cols, cardImage.rows) *
                                    dotsRegionRatio);

  // Safety check: ensure minimum size
  if (regionSize < minDotsRegionSize) {
    regionSize = minDotsRegionSize;
  }

  // Safety check: don't exceed image bounds
  if (regionSize > cardImage.cols || regionSize > cardImage.rows) {
    regionSize = std::min(cardImage.cols, cardImage.rows);
  }

  // Extract square region from top-left
  cv::Rect dotsRect(0, 0, regionSize, regionSize);
  return cardImage(dotsRect).clone();
}

} // namespace core
} // namespace cardscanner
