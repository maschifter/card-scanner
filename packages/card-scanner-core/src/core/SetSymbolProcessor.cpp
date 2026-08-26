#include "SetSymbolProcessor.h"
#include "../Constants.h"
#include "../PathProvider.h"
#include "../benchmark/BenchmarkCollector.h"
#include <Log.h>

namespace cardscanner {
namespace core {

using namespace cardscanner::constants;

SetSymbolInfo SetSymbolProcessor::processSetSymbol(
    const cv::Mat &cardImage, const std::vector<CardMatch> &cardMatches,
    float disambiguationThreshold, float confidenceThreshold,
    cardscanner::SetSymbolYoloModel *yoloModel,
    cardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {
  // Early exit: not MTG or models not available
  if (!isMTGCard(cardMatches) ||
      !hasSetSymbolModels(yoloModel, embedder, database)) {
    return SetSymbolInfo();
  }

  if (cardImage.empty()) {
    return SetSymbolInfo();
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
      return SetSymbolInfo(); // Skip set symbol detection
    }
  } else if (cardMatches.size() == 1) {
    // Only one match - no need to disambiguate
    return SetSymbolInfo();
  }

  try {
    log(LOG_LEVEL::Debug,
        "[CardScanner] Detecting set symbol for MTG card with disambiguation "
        "threshold: %.2f",
        disambiguationThreshold);

    // Detect set symbol bounding box
    cv::Rect symbolBox;
    {
      benchmark::BenchmarkCollector::ScopedTimer timer(
          benchmark::Stage::SetSymbolYolo);
      symbolBox = detectSetSymbolBox(cardImage, yoloModel);
    }
    if (symbolBox.width == 0 || symbolBox.height == 0) {
      log(LOG_LEVEL::Debug,
          "[CardScanner] No set symbol detected in card image");
      return SetSymbolInfo(); // Not detected
    }

    // Crop set symbol region
    cv::Mat symbolImage;
    {
      benchmark::BenchmarkCollector::ScopedTimer timer(
          benchmark::Stage::SetSymbolPreproc);
      symbolImage = utils::ImageUtils::cropRegion(cardImage, symbolBox);
    }
    if (symbolImage.empty()) {
      return SetSymbolInfo();
    }

    // Match set symbol to database
    return matchSetSymbol(symbolImage, confidenceThreshold, embedder, database);

  } catch (const std::exception &e) {
    // Set symbol detection failed, return empty
    return SetSymbolInfo();
  }
}

bool SetSymbolProcessor::isMTGCard(
    const std::vector<CardMatch> &cardMatches) {
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

SetSymbolInfo SetSymbolProcessor::matchSetSymbol(
    const cv::Mat &symbolImage, float confidenceThreshold,
    cardscanner::SetSymbolEmbedder *embedder, ObjectBoxDB *database) {

  // Compute set symbol embedding
  SetSymbolEmbeddingResult embeddingResult;
  {
    benchmark::BenchmarkCollector::ScopedTimer timer(
        benchmark::Stage::SetSymbolEmbed);
    embeddingResult = embedder->computeEmbedding(symbolImage);
  }

  // Search database (top 1 result)
  std::vector<SetSymbolMatch> symbolMatches;
  {
    benchmark::BenchmarkCollector::ScopedTimer timer(
        benchmark::Stage::SetSymbolDbSearch);
    symbolMatches =
        database->search_similar_set_symbols(
            embeddingResult.embedding,
            constants::database::SET_SYMBOL_MAX_RESULTS);
  }

  // If no matches found, return empty info
  if (symbolMatches.empty()) {
    log(LOG_LEVEL::Debug,
        "[CardScanner] No matching set symbols found in database");
    return SetSymbolInfo();
  }

  // Check confidence threshold
  float effectiveConfidenceThreshold = confidenceThreshold > 0.0f
                                           ? confidenceThreshold
                                           : database::MIN_SIMILARITY_SCORE;

  // If below threshold, return empty info
  if (symbolMatches[0].similarity < effectiveConfidenceThreshold) {
    log(LOG_LEVEL::Debug,
        "[CardScanner] Set symbol match below confidence threshold: %.4f "
        "(threshold: %.4f)",
        symbolMatches[0].similarity, effectiveConfidenceThreshold);
    return SetSymbolInfo();
  }

  log(LOG_LEVEL::Debug,
      "[CardScanner] Matched set symbol: %s (similarity: %.4f)",
      symbolMatches[0].setCode.c_str(), symbolMatches[0].similarity);
  // Return matched set symbol info
  return SetSymbolInfo(symbolMatches[0].setCode,
                            symbolMatches[0].similarity);
}

} // namespace core
} // namespace cardscanner
