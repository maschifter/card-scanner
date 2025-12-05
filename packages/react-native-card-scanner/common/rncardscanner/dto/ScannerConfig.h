#pragma once

#include <optional>
#include <string>

namespace rncardscanner {
namespace dto {

/**
 * @struct ScannerConfig
 * @brief Configuration for the scanning pipeline (no JSI dependencies)
 *
 * Pure data structure that can be passed through the entire pipeline
 * without coupling to React Native or JSI.
 */
struct ScannerConfig {
  // Scan behavior
  std::string scanMode; // "single" or "multiple"

  // Thresholds
  float segmentationThreshold; // YOLO confidence threshold (e.g., 0.7)
  float iouThreshold;          // NMS IOU threshold (e.g., 0.7)
  float confidenceThreshold;   // Database match threshold (e.g., 0.6)

  // Search parameters
  int maxMatches;       // Max matches to return per detection
  int searchCandidates; // DB fetch size for approximate search

  // Optional features
  bool captureImage; // Save cropped card images to disk

  std::string segmentationModelPath;
  std::string embeddingModelPath;

  struct MTGConfig {
    std::string setSymbolDetectionModelPath;
    std::string setSymbolEmbedderModelPath;
    float detectionThreshold;
    float confidenceThreshold;
  };

  std::optional<MTGConfig> mtgConfig;

  // Constants
  static constexpr float MIN_YOLO_GAME_CONFIDENCE = 0.1f;
  static constexpr int MAX_GAME_PREDICTIONS = 3;
  static constexpr float SEARCH_MORE_THRESHOLD_DELTA = 0.1f;
  static constexpr int JPEG_QUALITY = 90;
};

} // namespace dto
} // namespace rncardscanner
