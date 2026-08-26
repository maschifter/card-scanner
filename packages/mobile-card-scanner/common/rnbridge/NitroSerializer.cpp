#include "NitroSerializer.h"

namespace cardscanner {
namespace utils {

using namespace margelo::nitro::cardscanner;

namespace {

NitroBoundingBox toNitroBoundingBox(const cv::Rect &box, float confidence) {
  NitroBoundingBox nitroBox;
  nitroBox.x1 = box.x;
  nitroBox.y1 = box.y;
  nitroBox.x2 = box.x + box.width;
  nitroBox.y2 = box.y + box.height;
  nitroBox.conf = confidence;
  return nitroBox;
}

NitroDetectedCard toNitroDetectedCard(const ProcessedCard &card) {
  NitroDetectedCard nitroCard;

  // Primary match fields (flattened from matches[0])
  if (!card.matches.empty()) {
    const auto &primaryMatch = card.matches[0];
    nitroCard.cardId = primaryMatch.cardId;
    nitroCard.gameName = primaryMatch.gameName;
    nitroCard.confidenceScore = primaryMatch.score;
  }

  // Always include predicted game name if available
  if (!card.predictedGameName.empty()) {
    nitroCard.predictedGameName = card.predictedGameName;
    nitroCard.predictedGameConfidence =
        static_cast<double>(card.predictedGameConfidence);
  }

  nitroCard.boundingBox =
      toNitroBoundingBox(card.boundingBox, card.detectionConfidence);

  // Alternative cards (matches[1:])
  if (card.matches.size() > 1) {
    nitroCard.alternativeCards.reserve(card.matches.size() - 1);
    for (size_t i = 1; i < card.matches.size(); i++) {
      NitroAlternativeMatch alternative;
      alternative.cardId = card.matches[i].cardId;
      alternative.confidence = card.matches[i].score;
      nitroCard.alternativeCards.push_back(std::move(alternative));
    }
  }

  // Captured image (optional)
  if (!card.savedImagePath.empty()) {
    NitroCapturedImage capturedImage;
    capturedImage.uri = card.savedImagePath;
    capturedImage.width = card.imageWidth;
    capturedImage.height = card.imageHeight;
    capturedImage.size = static_cast<double>(card.imageFileSize);
    nitroCard.capturedImage = std::move(capturedImage);
  }

  // Set symbol (optional, MTG only)
  if (!card.setSymbol.isEmpty()) {
    NitroSetSymbol setSymbol;
    setSymbol.setCode = card.setSymbol.setCode;
    setSymbol.similarity = card.setSymbol.similarity;
    nitroCard.setSymbol = std::move(setSymbol);
  }

  // FAB color (optional, FAB only)
  if (!card.fabColor.isEmpty()) {
    NitroFabColor fabColor;
    fabColor.color = card.fabColor.color;
    fabColor.similarity = card.fabColor.similarity;
    nitroCard.fabColor = std::move(fabColor);
  }

  return nitroCard;
}

} // namespace

NitroDetection
NitroSerializer::serializeScanResult(const ScanResult &result) {
  NitroDetection detection;
  detection.success = true;
  detection.processingTime = result.processingTimeMs;
  detection.cards.reserve(result.cards.size());
  for (const auto &card : result.cards) {
    detection.cards.push_back(toNitroDetectedCard(card));
  }
  return detection;
}

NitroDetection NitroSerializer::serializeError(const std::string &error) {
  NitroDetection detection;
  detection.success = false;
  detection.processingTime = 0;
  detection.error = error;
  return detection;
}

} // namespace utils
} // namespace cardscanner
