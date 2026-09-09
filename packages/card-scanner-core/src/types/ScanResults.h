#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {

/**
 * @struct CardMatch
 * @brief Single card match from database search
 */
struct CardMatch {
  std::string cardId;
  std::string gameName;
  float score;
};

/**
 * @struct SetSymbolInfo
 * @brief MTG set symbol detection result
 */
struct SetSymbolInfo {
  std::string setCode; // e.g., "BRO" (Brother's War)
  float similarity;    // Confidence score [0.0, 1.0]

  // Default constructor for empty results
  SetSymbolInfo() : setCode(""), similarity(0.0f) {}

  SetSymbolInfo(const std::string &code, float sim)
      : setCode(code), similarity(sim) {}

  bool isEmpty() const { return setCode.empty(); }
};

/**
 * @struct FABColorInfo
 * @brief Flesh and Blood color variant detection result
 */
struct FABColorInfo {
  std::string color; // "red", "yellow", "blue", or empty
  float similarity;  // Confidence score [0.0, 1.0]

  // Default constructor for empty results
  FABColorInfo() : color(""), similarity(0.0f) {}

  FABColorInfo(const std::string &col, float sim)
      : color(col), similarity(sim) {}

  bool isEmpty() const { return color.empty(); }
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
  // Pending capture in resolved orientation; written and released by
  // ScannerPipeline::saveCardImages() after the scan lease is released.
  cv::Mat imageToSave;
  std::string savedImagePath;
  int imageWidth;     // Width of saved image in pixels
  int imageHeight;    // Height of saved image in pixels
  long imageFileSize; // File size in bytes

  // Recognition results
  std::vector<CardMatch> matches;
  std::string predictedGameName;
  float predictedGameConfidence; // YOLO confidence for predicted game (0.0-1.0)

  // Game-specific metadata
  SetSymbolInfo setSymbol; // MTG: set symbol detection
  FABColorInfo fabColor;   // FAB: color variant detection

  ProcessedCard()
      : detectionConfidence(0.0f), imageWidth(0), imageHeight(0),
        imageFileSize(0), predictedGameConfidence(0.0f) {}

  bool hasMatches() const { return !matches.empty(); }
};

/**
 * @struct ScanResult
 * @brief Complete result of processing a single frame
 *
 * Pure C++ result. Each wrapper package serializes it to its own host
 * representation.
 */
struct ScanResult {
  // Processing results
  std::vector<ProcessedCard> cards;

  // Timing
  double processingTimeMs;

  ScanResult() : processingTimeMs(0.0) {}
};

} // namespace cardscanner
