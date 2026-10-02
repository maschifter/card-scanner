#include "FABColorProcessor.h"
#include "../benchmark/BenchmarkCollector.h"
#include <Log.h>

namespace cardscanner {
namespace core {

FABColorInfo FABColorProcessor::processColorVariant(
    const cv::Mat &cardImage, const std::vector<CardSearchResult> &cardMatches,
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
  if (!isAmbiguous(cardMatches, disambiguationThreshold)) {
    return FABColorInfo();
  }

  try {
    log(LOG_LEVEL::Debug, "[CardScanner]",
        "detecting FAB color variant with disambiguation threshold:",
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
    const std::vector<CardSearchResult> &cardMatches) {
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
