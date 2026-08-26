#pragma once

#include "../Constants.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cardscanner {

/// Model class ID to the database names it should search. More than one name
/// when the model merges games it cannot tell apart (e.g. pokemon variants).
/// Keys must be 0..size()-1: the entry count decodes the prediction tensor.
using GameClassMap = std::map<int, std::vector<std::string>>;


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
  float minGameConfidence; // Min YOLO class confidence to search a game's DB

  // Search parameters
  int maxMatches;       // Max matches to return per detection
  int searchCandidates; // DB fetch size for approximate search

  // Optional features
  bool captureImage; // Save cropped card images to disk

  // Pipeline behavior (C++-side defaults; benchmark runs override them)
  bool useDetectionSelection = true; // Center-most pick in "single" scan mode
  bool useSidewaysFlipCache = true;  // Remember resolved 180-deg flip across frames

  // Frame quality
  double blurThreshold;     // Minimum blur score (higher = sharper, 0 = disabled)
  double lowLightThreshold; // Minimum brightness for gamma correction (0-255, 0 = disabled)
  double lowLightGamma;     // Gamma correction value for low-light enhancement (default: 2.0)
  int maxFrameRate;         // Maximum frame rate for ML pipeline in FPS (default: 5)

  std::string segmentationModelPath;
  std::string embeddingModelPath;

  // Game class mapping (required). A class maps to several databases when the
  // model merges games it cannot distinguish; all of them get searched.
  GameClassMap gameClassMapping;

  // Generic per-game configuration
  struct GameConfig {
    // Optional: Game-specific card embedding model
    std::string embeddingModelPath;

    // Optional: Override the scanner-wide card recognition confidence threshold.
    std::optional<float> confidenceThreshold;

    // MTG-specific: Set symbol detection
    std::string setSymbolDetectionModelPath;
    std::string setSymbolEmbedderModelPath;
    float setSymbolDetectionThreshold =
        constants::mtg::DEFAULT_DETECTION_THRESHOLD;
    float setSymbolConfidenceThreshold =
        constants::mtg::DEFAULT_CONFIDENCE_THRESHOLD;
    // Must match the exported model's input_shape or inference fails
    int setSymbolImageSize = DEFAULT_SET_SYMBOL_IMAGE_SIZE;

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
  static constexpr float DEFAULT_MIN_GAME_CONFIDENCE = 0.1f;
  // Cap on databases searched per detection; a merged class contributes two
  static constexpr int MAX_GAME_DATABASES = 4;
  static constexpr float SEARCH_MORE_THRESHOLD_DELTA = 0.1f;
  static constexpr int JPEG_QUALITY = 90;
  static constexpr double DEFAULT_BLUR_THRESHOLD = 100.0;
  static constexpr double DEFAULT_LOW_LIGHT_THRESHOLD = 65.0;
  static constexpr float DEFAULT_DISAMBIGUATION_THRESHOLD = 0.02f; // 2%
  static constexpr double DEFAULT_LOW_LIGHT_GAMMA = 2.0;
  static constexpr int DEFAULT_MAX_FRAME_RATE = 5; // 5 FPS
  static constexpr double DEFAULT_FAB_DOTS_REGION_RATIO = 0.20;
  static constexpr int DEFAULT_FAB_MIN_DOTS_REGION_SIZE = 50;
  static constexpr int DEFAULT_SET_SYMBOL_IMAGE_SIZE = 384;
};

} // namespace cardscanner
