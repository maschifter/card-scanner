#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace rncardscanner {
namespace dto {

/**
 * @struct CardMatch
 * @brief Single card match from database search
 */
struct CardMatch {
  std::string cardId;
  std::string name;
  std::string gameName;
  float score;
};

/**
 * @struct SetSymbolInfo
 * @brief MTG set symbol detection result
 */
struct SetSymbolInfo {
  std::string setCode;
  std::string setName;
  std::string variant;
  float similarity;
  std::string croppedImagePath;

  // Default constructor for empty results
  SetSymbolInfo()
      : setCode(""), setName(""), variant(""), similarity(0.0f),
        croppedImagePath("") {}

  SetSymbolInfo(const std::string &code, const std::string &name,
                const std::string &var, float sim, const std::string &path)
      : setCode(code), setName(name), variant(var), similarity(sim),
        croppedImagePath(path) {}

  bool isEmpty() const { return setCode.empty(); }
};

/**
 * @struct ProcessedCard
 * @brief All information about a single detected card
 *
 * This is the core data structure passed through the pipeline:
 * Segmentation -> Extraction -> Recognition -> Set Symbol Detection
 */
struct ProcessedCard {
  // Original detection info
  cv::Rect boundingBox;
  float detectionConfidence;

  // Extracted images
  cv::Mat croppedImage;
  std::string savedImagePath;

  // Recognition results
  std::vector<CardMatch> matches;
  std::vector<float> embedding;

  // MTG-specific
  SetSymbolInfo setSymbol;

  ProcessedCard() : detectionConfidence(0.0f) {}

  bool hasMatches() const { return !matches.empty(); }
  bool isIdentified() const { return hasMatches() && matches[0].score >= 0.0f; }
};

/**
 * @struct ScanResult
 * @brief Complete result of processing a single frame
 *
 * Pure C++ result that will be serialized to JSI by the installer layer.
 */
struct ScanResult {
  // Processing results
  std::vector<ProcessedCard> cards;

  // Timing
  double processingTimeMs;

  // Convenience methods
  int getIdentifiedCount() const {
    int count = 0;
    for (const auto &card : cards) {
      if (card.isIdentified()) {
        count++;
      }
    }
    return count;
  }

  int getTotalDetectionCount() const { return static_cast<int>(cards.size()); }
};

} // namespace dto
} // namespace rncardscanner
