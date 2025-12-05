#include "SetSymbolProcessor.h"
#include "../Constants.h"

namespace rncardscanner {
namespace core {

using namespace cardscanner::constants;

dto::SetSymbolInfo SetSymbolProcessor::processSetSymbol(
    const cv::Mat &cardImage, const std::vector<dto::CardMatch> &cardMatches,
    float confidenceThreshold, cardscanner::SetSymbolYoloModel *yoloModel,
    cardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {
  // Early exit: not MTG or models not available
  if (!isMTGCard(cardMatches) ||
      !hasSetSymbolModels(yoloModel, embedder, database)) {
    return dto::SetSymbolInfo();
  }

  if (cardImage.empty()) {
    return dto::SetSymbolInfo();
  }

  try {
    // Detect set symbol bounding box
    cv::Rect symbolBox = detectSetSymbolBox(cardImage, yoloModel);
    if (symbolBox.width == 0 || symbolBox.height == 0) {
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
    cardscanner::SetSymbolYoloModel *yoloModel,
    cardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {
  return yoloModel != nullptr && embedder != nullptr && database != nullptr;
}

cv::Rect SetSymbolProcessor::detectSetSymbolBox(
    const cv::Mat &cardImage, cardscanner::SetSymbolYoloModel *yoloModel) {
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
    cardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {

  // Compute set symbol embedding
  auto embeddingResult = embedder->computeEmbedding(symbolImage);

  // Search database (top 1 result)
  auto symbolMatches =
      database->search_similar_set_symbols(embeddingResult.embedding, 1);

  if (symbolMatches.empty()) {
    return dto::SetSymbolInfo();
  }

  // Check confidence threshold
  float effectiveConfidenceThreshold = confidenceThreshold > 0.0f
                                           ? confidenceThreshold
                                           : database::MIN_SIMILARITY_SCORE;

  if (symbolMatches[0].similarity < effectiveConfidenceThreshold) {
    return dto::SetSymbolInfo();
  }

  // Return matched set symbol info
  return dto::SetSymbolInfo(symbolMatches[0].setCode, symbolMatches[0].setName,
                            symbolMatches[0].variant,
                            symbolMatches[0].similarity, "");
}

} // namespace core
} // namespace rncardscanner
