#pragma once

#include "ConfigValue.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
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
  std::string scanMode = "single"; // "single", "multiple" or "auto"

  // Multi-card freeze ("auto" and "multiple"). What counts as a page and how
  // long it must hold; the geometry thresholds are pipeline constants in
  // Constants.h (constants::multi).
  int minCardsForMulti = 6;
  // Consecutive qualifying frames before the freeze; 1 = decide per frame
  int multiStableFrames = 2;
  // Freeze and stream on a qualifying frame; false = process all, no freeze
  bool freezeOnMulti = true;

  // Thresholds
  float segmentationThreshold = 0.7f; // YOLO confidence threshold
  float iouThreshold = 0.7f;          // NMS IOU threshold
  float confidenceThreshold = 0.6f;   // Database match threshold
  // Min score difference to skip game-specific detection (0.02 = 2%)
  float disambiguationThreshold = 0.02f;
  // Min YOLO class confidence to search a game's DB
  float minGameConfidence = 0.1f;

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
  double blurThreshold = 100.0;
  // Minimum brightness for gamma correction (0-255, 0 = disabled)
  double lowLightThreshold = 65.0;
  // Gamma correction value for low-light enhancement
  double lowLightGamma = 2.0;
  // Maximum frame rate for the ML pipeline in FPS
  int maxFrameRate = 5;

  std::string segmentationModelPath;
  std::string embeddingModelPath;
  std::string languageModelPath;
  // In the model's class order
  std::vector<std::string> languageClassNames = {"de", "en", "es", "fr", "it",
                                                 "ja", "ko", "pt", "ru", "zh"};

  // Game class mapping (required). A class maps to several databases when the
  // model merges games it cannot distinguish; all of them get searched.
  GameClassMap gameClassMapping;

  // Generic per-game configuration
  struct GameConfig {
    // Optional: Game-specific card embedding model
    std::string embeddingModelPath;

    // Optional: Override the scanner-wide card recognition confidence threshold.
    std::optional<float> confidenceThreshold;
    // Optional: Override the scanner-wide disambiguationThreshold, the score
    // gap under which the top two matches count as one card in two printings.
    std::optional<float> disambiguationThreshold;

    // MTG-specific: Set symbol detection
    std::string setSymbolDetectionModelPath;
    std::string setSymbolEmbedderModelPath;
    float setSymbolDetectionThreshold = 0.3f;  // Set symbol YOLO threshold
    float setSymbolConfidenceThreshold = 0.6f; // Set symbol match threshold
    // Must match the exported model's input_shape or inference fails
    int setSymbolImageSize = 384;

    // FAB-specific: Color variant detection
    std::string colorDetectionModelPath;
    // In the model's class order
    std::vector<std::string> colorDetectionClassNames = {"blue", "red",
                                                         "yellow"};
    double dotsRegionRatio = 0.20;
    int minDotsRegionSize = 50;

    // Printed set-code reading (Yu-Gi-Oh!: YGLD-ENA22)
    std::string setCodeModelPath;
    // In the model's class order: the CTC blank, then one character each
    std::vector<std::string> setCodeClassNames = {
        "blank", "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A",
        "B",     "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
        "O",     "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};

    // Check if any fields are configured
    bool hasEmbedding() const { return !embeddingModelPath.empty(); }
    bool hasSetSymbolDetection() const {
      return !setSymbolDetectionModelPath.empty() || !setSymbolEmbedderModelPath.empty();
    }
    bool hasColorDetection() const { return !colorDetectionModelPath.empty(); }
    bool hasSetCodeReading() const { return !setCodeModelPath.empty(); }
  };

  // Map of game name to game-specific configuration
  std::map<std::string, GameConfig> gameSpecificConfig;

  /// The game's confidenceThreshold override, else the scanner-wide one.
  float confidenceThresholdFor(const std::string &game) const {
    return forGame(game, &GameConfig::confidenceThreshold, confidenceThreshold);
  }
  /// The game's disambiguationThreshold override, else the scanner-wide one.
  float disambiguationThresholdFor(const std::string &game) const {
    return forGame(game, &GameConfig::disambiguationThreshold,
                   disambiguationThreshold);
  }

  /// Empty means valid, else the first violation. Callers add their own prefix.
  std::string validate() const;

  /// True for "single", "multiple" and "auto".
  static bool isScanMode(const std::string &mode);

private:
  /// A game's override of a scanner-wide value, else the value.
  float forGame(const std::string &game,
                std::optional<float> GameConfig::*override,
                float fallback) const {
    const auto it = gameSpecificConfig.find(game);
    return it != gameSpecificConfig.end() && it->second.*override
               ? *(it->second.*override)
               : fallback;
  }
};


/// A parsed config and the keys it held that nothing reads.
struct ParsedScannerConfig {
  ScannerConfig config;
  /// Full dotted paths, e.g. "gameSpecificConfig.mtg.typo", in document order.
  std::vector<std::string> unknownKeys;
};

/**
 * @brief Reads a ScannerConfig from a host's config document.
 *
 * The only code that knows the config's keys. A missing or null key keeps the
 * ScannerConfig default; a key of the wrong type throws std::runtime_error.
 * Keys starting with "//" are comments. Unknown keys are logged and returned.
 * Call validate() on the result.
 *
 * @param hostKeys Top-level keys the host reads itself, so they are not
 *   reported as unknown.
 */
ParsedScannerConfig
parseScannerConfig(const ConfigValue &root,
                   const std::vector<std::string_view> &hostKeys = {});

} // namespace cardscanner
