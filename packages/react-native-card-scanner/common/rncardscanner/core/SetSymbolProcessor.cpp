#include "SetSymbolProcessor.h"
#include "../Constants.h"
#include "../PathProvider.h"
#include <log.h>

namespace rncardscanner {
namespace core {

using namespace rncardscanner::constants;

dto::SetSymbolInfo SetSymbolProcessor::processSetSymbol(
    const cv::Mat &cardImage, const std::vector<dto::CardMatch> &cardMatches,
    float disambiguationThreshold, float confidenceThreshold,
    rncardscanner::SetSymbolYoloModel *yoloModel,
    rncardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {
  // Early exit: not MTG or models not available
  if (!isMTGCard(cardMatches) ||
      !hasSetSymbolModels(yoloModel, embedder, database)) {
    return dto::SetSymbolInfo();
  }

  if (cardImage.empty()) {
    return dto::SetSymbolInfo();
  }

  // Optimization: Only search for set symbol if top 2 matches are close
  // If top match is clearly the best (score difference > threshold), skip
  // detection
  if (cardMatches.size() >= 2) {
    float top1Score = cardMatches[0].score;
    float top2Score = cardMatches[1].score;
    float scoreDifference = top1Score - top2Score;

    // If difference exceeds threshold, top match is clearly best
    if (scoreDifference > disambiguationThreshold) {
      return dto::SetSymbolInfo(); // Skip set symbol detection
    }
  } else if (cardMatches.size() == 1) {
    // Only one match - no need to disambiguate
    return dto::SetSymbolInfo();
  }

  try {
    log(LOG_LEVEL::Debug,
        "[RNCardScanner] Detecting set symbol for MTG card with disambiguation "
        "threshold: %.2f",
        disambiguationThreshold);

    // Detect set symbol bounding box
    cv::Rect symbolBox = detectSetSymbolBox(cardImage, yoloModel);
    if (symbolBox.width == 0 || symbolBox.height == 0) {
      log(LOG_LEVEL::Debug,
          "[RNCardScanner] No set symbol detected in card image");
      return dto::SetSymbolInfo(); // Not detected
    }

    // Crop set symbol region
    cv::Mat symbolImage = utils::ImageUtils::cropRegion(cardImage, symbolBox);
    if (symbolImage.empty()) {
      return dto::SetSymbolInfo();
    }

    // Match set symbol to database
    return matchSetSymbol(symbolImage, confidenceThreshold, embedder, database);

  } catch (const std::exception &e) {
    // Set symbol detection failed, return empty
    return dto::SetSymbolInfo();
  }
}

bool SetSymbolProcessor::isMTGCard(
    const std::vector<dto::CardMatch> &cardMatches) {
  return !cardMatches.empty() && cardMatches[0].gameName == "mtg";
}

bool SetSymbolProcessor::hasSetSymbolModels(
    rncardscanner::SetSymbolYoloModel *yoloModel,
    rncardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {
  return yoloModel != nullptr && embedder != nullptr && database != nullptr;
}

cv::Rect SetSymbolProcessor::detectSetSymbolBox(
    const cv::Mat &cardImage, rncardscanner::SetSymbolYoloModel *yoloModel) {
  auto symbolDetection = yoloModel->detect(cardImage);

  if (symbolDetection.detections.empty()) {
    return cv::Rect(); // Empty rect
  }

  // Convert SetSymbolBBox to cv::Rect
  const auto &bbox = symbolDetection.detections[0];
  int x = static_cast<int>(bbox.x1);
  int y = static_cast<int>(bbox.y1);
  int width = static_cast<int>(bbox.x2 - bbox.x1);
  int height = static_cast<int>(bbox.y2 - bbox.y1);
  return cv::Rect(x, y, width, height);
}

dto::SetSymbolInfo SetSymbolProcessor::matchSetSymbol(
    const cv::Mat &symbolImage, float confidenceThreshold,
    rncardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {

  // Compute set symbol embedding
  auto embeddingResult = embedder->computeEmbedding(symbolImage);

  // Search database (top 1 result)
  auto symbolMatches =
      database->search_similar_set_symbols(embeddingResult.embedding, 1);

  // If no matches found, return empty info
  if (symbolMatches.empty()) {
    log(LOG_LEVEL::Debug,
        "[RNCardScanner] No matching set symbols found in database");
    return dto::SetSymbolInfo();
  }

  // Check confidence threshold
  float effectiveConfidenceThreshold = confidenceThreshold > 0.0f
                                           ? confidenceThreshold
                                           : database::MIN_SIMILARITY_SCORE;

  // If below threshold, return empty info
  if (symbolMatches[0].similarity < effectiveConfidenceThreshold) {
    log(LOG_LEVEL::Debug,
        "[RNCardScanner] Set symbol match below confidence threshold: %.4f "
        "(threshold: %.4f)",
        symbolMatches[0].similarity, effectiveConfidenceThreshold);
    return dto::SetSymbolInfo();
  }

  log(LOG_LEVEL::Debug,
      "[RNCardScanner] Matched set symbol: %s (similarity: %.4f)",
      symbolMatches[0].setCode.c_str(), symbolMatches[0].similarity);
  // Return matched set symbol info
  return dto::SetSymbolInfo(symbolMatches[0].setCode,
                            symbolMatches[0].similarity);
}

} // namespace core
} // namespace rncardscanner
