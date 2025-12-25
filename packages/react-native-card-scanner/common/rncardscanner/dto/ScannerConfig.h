#pragma once

#include <map>
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
  float segmentationThreshold;    // YOLO confidence threshold (e.g., 0.7)
  float iouThreshold;             // NMS IOU threshold (e.g., 0.7)
  float confidenceThreshold;      // Database match threshold (e.g., 0.6)
  float disambiguationThreshold;  // Min score difference to skip game-specific detection (e.g., 0.02 = 2%)

  // Search parameters
  int maxMatches;       // Max matches to return per detection
  int searchCandidates; // DB fetch size for approximate search

  // Optional features
  bool captureImage; // Save cropped card images to disk

  // Frame quality
  double blurThreshold;     // Minimum blur score (higher = sharper, 0 = disabled)
  double lowLightThreshold; // Minimum brightness for gamma correction (0-255, 0 = disabled)
  double lowLightGamma;     // Gamma correction value for low-light enhancement (default: 2.0)
  int maxFrameRate;         // Maximum frame rate for ML pipeline in FPS (default: 5)

  std::string segmentationModelPath;
  std::string embeddingModelPath;

  // Game class mapping (required)
  std::map<int, std::string> gameClassMapping;

  // Generic per-game configuration
  struct GameConfig {
    // Optional: Game-specific card embedding model
    std::string embeddingModelPath;

    // MTG-specific: Set symbol detection
    std::string setSymbolDetectionModelPath;
    std::string setSymbolEmbedderModelPath;
    float setSymbolDetectionThreshold = 0.3f;
    float setSymbolConfidenceThreshold = 0.6f;

    // FAB-specific: Color variant detection
    std::string colorDetectionModelPath;
    double dotsRegionRatio = DEFAULT_FAB_DOTS_REGION_RATIO;
    int minDotsRegionSize = DEFAULT_FAB_MIN_DOTS_REGION_SIZE;

    // Check if any fields are configured
    bool hasEmbedding() const { return !embeddingModelPath.empty(); }
    bool hasSetSymbolDetection() const {
      return !setSymbolDetectionModelPath.empty() || !setSymbolEmbedderModelPath.empty();
    }
    bool hasColorDetection() const { return !colorDetectionModelPath.empty(); }
  };

  // Map of game name to game-specific configuration
  std::map<std::string, GameConfig> gameSpecificConfig;

  // Constants
  static constexpr float MIN_YOLO_GAME_CONFIDENCE = 0.1f;
  static constexpr int MAX_GAME_PREDICTIONS = 3;
  static constexpr float SEARCH_MORE_THRESHOLD_DELTA = 0.1f;
  static constexpr int JPEG_QUALITY = 90;
  static constexpr double DEFAULT_BLUR_THRESHOLD = 100.0;
  static constexpr double DEFAULT_LOW_LIGHT_THRESHOLD = 65.0;
  static constexpr float DEFAULT_DISAMBIGUATION_THRESHOLD = 0.02f; // 2%
  static constexpr double DEFAULT_LOW_LIGHT_GAMMA = 2.0;
  static constexpr int DEFAULT_MAX_FRAME_RATE = 5; // 5 FPS
  static constexpr double DEFAULT_FAB_DOTS_REGION_RATIO = 0.20;
  static constexpr int DEFAULT_FAB_MIN_DOTS_REGION_SIZE = 50;
};

} // namespace dto
} // namespace rncardscanner
