#pragma once

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
  float segmentationThreshold = 0.7f; // YOLO confidence threshold
  float iouThreshold = 0.7f;          // NMS IOU threshold
  float confidenceThreshold = 0.6f;   // Database match threshold
  // Min score difference to skip game-specific detection (0.02 = 2%)
  float disambiguationThreshold = DEFAULT_DISAMBIGUATION_THRESHOLD;
  // Min YOLO class confidence to search a game's DB
  float minGameConfidence = DEFAULT_MIN_GAME_CONFIDENCE;

  // Search parameters
  int maxMatches = 5;         // Max matches to return per detection
  int searchCandidates = 100; // DB fetch size for approximate search

  // Optional features
  bool captureImage = false; // Save cropped card images to disk

  // Pipeline behavior (C++-side defaults; benchmark runs override them)
  bool useDetectionSelection = true; // Center-most pick in "single" scan mode
  bool useSidewaysFlipCache = true;  // Remember resolved 180-deg flip across frames

  // Frame quality
  // Minimum blur score (higher = sharper, 0 = disabled)
  double blurThreshold = DEFAULT_BLUR_THRESHOLD;
  // Minimum brightness for gamma correction (0-255, 0 = disabled)
  double lowLightThreshold = DEFAULT_LOW_LIGHT_THRESHOLD;
  // Gamma correction value for low-light enhancement
  double lowLightGamma = DEFAULT_LOW_LIGHT_GAMMA;
  // Maximum frame rate for the ML pipeline in FPS
  int maxFrameRate = DEFAULT_MAX_FRAME_RATE;

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
    float setSymbolDetectionThreshold = 0.3f;  // Set symbol YOLO threshold
    float setSymbolConfidenceThreshold = 0.6f; // Set symbol match threshold
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
  // Margin above a game's effective confidence threshold at which a match is
  // confident enough to skip the remaining candidate YOLO classes. Databases
  // that share one class are always searched together first.
  static constexpr float EARLY_EXIT_SCORE_MARGIN = 0.2f;
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
