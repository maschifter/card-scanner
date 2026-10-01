#include "ScannerConfig.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>

namespace cardscanner {

namespace {

using NamedDouble = std::pair<const char *, double>;
using NamedInt = std::pair<const char *, int>;

// Empty when every field lies in [0, 1]; NaN fails the comparison on purpose.
std::string checkUnitRange(const std::string &prefix,
                           std::initializer_list<NamedDouble> fields) {
  for (const auto &[name, value] : fields) {
    if (!(value >= 0.0 && value <= 1.0)) {
      return prefix + name + " must be within [0, 1], got " +
             std::to_string(value);
    }
  }
  return {};
}

std::string checkAtLeastOne(const std::string &prefix,
                            std::initializer_list<NamedInt> fields) {
  for (const auto &[name, value] : fields) {
    if (value < 1) {
      return prefix + name + " must be at least 1, got " +
             std::to_string(value);
    }
  }
  return {};
}

} // namespace

std::string ScannerConfig::validate() const {
  if (segmentationModelPath.empty()) {
    return "segmentationModelPath is required";
  }
  if (embeddingModelPath.empty()) {
    return "embeddingModelPath is required";
  }
  if (scanMode != "single" && scanMode != "multiple") {
    return "scanMode must be 'single' or 'multiple', got '" + scanMode + "'";
  }

  if (const auto error = checkUnitRange(
          "", {{"segmentationThreshold", segmentationThreshold},
               {"iouThreshold", iouThreshold},
               {"confidenceThreshold", confidenceThreshold},
               {"disambiguationThreshold", disambiguationThreshold},
               {"minGameConfidence", minGameConfidence}});
      !error.empty()) {
    return error;
  }
  if (const auto error = checkAtLeastOne(
          "", {{"maxMatches", maxMatches}, {"searchCandidates", searchCandidates}});
      !error.empty()) {
    return error;
  }

  if (maxFrameRate < 0) {
    return "maxFrameRate must be 0 (unthrottled) or positive, got " +
           std::to_string(maxFrameRate);
  }
  if (!(blurThreshold >= 0.0)) {
    return "blurThreshold must be 0 (disabled) or positive, got " +
           std::to_string(blurThreshold);
  }
  if (!(lowLightThreshold >= 0.0 && lowLightThreshold <= 255.0)) {
    return "lowLightThreshold must be within [0, 255], got " +
           std::to_string(lowLightThreshold);
  }
  if (!(lowLightGamma > 0.0)) {
    return "lowLightGamma must be positive, got " +
           std::to_string(lowLightGamma);
  }

  if (gameClassMapping.empty()) {
    return "gameClassMapping is required and must map class ids to database "
           "names";
  }
  // Contiguous from zero, or the entry count no longer decodes the tensor.
  for (int i = 0; i < static_cast<int>(gameClassMapping.size()); i++) {
    const auto entry = gameClassMapping.find(i);
    if (entry == gameClassMapping.end()) {
      return "gameClassMapping must use contiguous keys 0.." +
             std::to_string(gameClassMapping.size() - 1) + " (missing " +
             std::to_string(i) +
             "). The entry count decodes the segmentation model's prediction "
             "tensor, so a gap silently misroutes every search.";
    }
    if (entry->second.empty()) {
      return "gameClassMapping[" + std::to_string(i) + "] names no database";
    }
  }

  // Searches key gameSpecificConfig by mapping names; other entries never apply.
  for (const auto &entry : gameSpecificConfig) {
    const std::string &game = entry.first;
    const bool routed = std::any_of(
        gameClassMapping.begin(), gameClassMapping.end(),
        [&game](const auto &mapped) {
          return std::find(mapped.second.begin(), mapped.second.end(), game) !=
                 mapped.second.end();
        });
    if (!routed) {
      return "gameSpecificConfig['" + game +
             "'] names a game that no gameClassMapping entry routes to";
    }

    const GameConfig &gameConfig = entry.second;
    const std::string prefix = "gameSpecificConfig['" + game + "'].";
    if (gameConfig.confidenceThreshold) {
      if (const auto error = checkUnitRange(
              prefix, {{"confidenceThreshold", *gameConfig.confidenceThreshold}});
          !error.empty()) {
        return error;
      }
    }
    if (const auto error = checkUnitRange(
            prefix,
            {{"setSymbolDetection.detectionThreshold",
              gameConfig.setSymbolDetectionThreshold},
             {"setSymbolDetection.confidenceThreshold",
              gameConfig.setSymbolConfidenceThreshold},
             {"colorDetection.dotsRegionRatio", gameConfig.dotsRegionRatio}});
        !error.empty()) {
      return error;
    }
    if (const auto error = checkAtLeastOne(
            prefix,
            {{"setSymbolDetection.imageSize", gameConfig.setSymbolImageSize},
             {"colorDetection.minDotsRegionSize",
              gameConfig.minDotsRegionSize}});
        !error.empty()) {
      return error;
    }
  }

  return {};
}

} // namespace cardscanner
