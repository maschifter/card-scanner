#include "ScannerConfig.h"
#include <Log.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

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

bool ScannerConfig::isScanMode(const std::string &mode) {
  return mode == "single" || mode == "multiple" || mode == "auto";
}

std::string ScannerConfig::validate() const {
  if (segmentationModelPath.empty()) {
    return "segmentationModelPath is required";
  }
  if (embeddingModelPath.empty()) {
    return "embeddingModelPath is required";
  }
  if (!isScanMode(scanMode)) {
    return "scanMode must be 'single', 'multiple' or 'auto', got '" + scanMode +
           "'";
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
          "", {{"maxMatches", maxMatches},
               {"searchCandidates", searchCandidates},
               {"minCardsForMulti", minCardsForMulti},
               {"multiStableFrames", multiStableFrames}});
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


namespace {

/// Reads one object's members and remembers which keys it read, so the rest
/// can be reported as unknown.
class ObjectReader {
public:
  ObjectReader(const ConfigValue &object, std::string prefix,
               std::vector<std::string> &unknownKeys)
      : object_(object), prefix_(std::move(prefix)), unknownKeys_(unknownKeys) {
  }

  /// Overwrites target only when the key is present and not null.
  template <typename T> void read(const char *key, T &target) {
    const ConfigValue &value = member(key);
    if (value.isNull()) {
      return;
    }
    if constexpr (std::is_same_v<T, std::string>) {
      target = expect<std::string>(key, value, "string");
    } else if constexpr (std::is_same_v<T, bool>) {
      target = expect<bool>(key, value, "boolean");
    } else {
      target = static_cast<T>(expect<double>(key, value, "number"));
    }
  }

  template <typename T> void read(const char *key, std::optional<T> &target) {
    if (!member(key).isNull()) {
      T value{};
      read(key, value);
      target = value;
    }
  }

  /// The member named `key`, marked as read.
  const ConfigValue &member(std::string_view key) {
    read_.push_back(key);
    return object_[key];
  }

  /// Marks a key as read by someone else.
  void allow(std::string_view key) { read_.push_back(key); }

  /// Adds every member that was never read, except "//" comments.
  void reportUnknown() const {
    const auto *members = std::get_if<ConfigValue::Object>(&object_.value);
    if (members == nullptr) {
      return;
    }
    for (const auto &[name, value] : *members) {
      if (name.compare(0, 2, "//") != 0 &&
          std::find(read_.begin(), read_.end(), name) == read_.end()) {
        unknownKeys_.push_back(prefix_ + name);
      }
    }
  }

private:
  template <typename T>
  const T &expect(const char *key, const ConfigValue &value,
                  const char *typeName) const {
    if (const auto *held = std::get_if<T>(&value.value)) {
      return *held;
    }
    throw std::runtime_error("config: '" + prefix_ + key + "' type must be " +
                             typeName + ", but is " + value.typeName());
  }

  const ConfigValue &object_;
  std::string prefix_;
  std::vector<std::string> &unknownKeys_;
  std::vector<std::string_view> read_;
};

/// Parses gameClassMapping's shape; validate() checks the map itself.
GameClassMap parseGameClassMapping(const ConfigValue &value) {
  if (value.isNull()) {
    return {}; // validate() reports the missing mapping
  }
  const auto *members = std::get_if<ConfigValue::Object>(&value.value);
  if (members == nullptr) {
    throw std::runtime_error("config: 'gameClassMapping' must be an object "
                             "mapping model class ids to database names");
  }

  GameClassMap mapping;
  // Not a structured binding: the Android build's OpenMP mode rejects
  // capturing one in the lambda below.
  for (const auto &member : *members) {
    const std::string &key = member.first;
    const ConfigValue &names = member.second;
    int classId = 0;
    try {
      classId = std::stoi(key);
    } catch (const std::exception &) {
      throw std::runtime_error("config: gameClassMapping key '" + key +
                               "' is not an integer class id");
    }

    const auto invalid = [&key] {
      return std::runtime_error("config: gameClassMapping['" + key +
                                "'] must be a string or array of strings");
    };
    std::vector<std::string> games;
    if (const auto *name = std::get_if<std::string>(&names.value)) {
      games.push_back(*name);
    } else if (const auto *list = std::get_if<ConfigValue::Array>(&names.value)) {
      // Several names when the model merges games it cannot tell apart.
      for (const auto &entry : *list) {
        const auto *game = std::get_if<std::string>(&entry.value);
        if (game == nullptr) {
          throw invalid();
        }
        games.push_back(*game);
      }
    } else {
      throw invalid();
    }
    mapping[classId] = std::move(games);
  }
  return mapping;
}

ScannerConfig::GameConfig parseGameConfig(const ConfigValue &object,
                                          const std::string &prefix,
                                          std::vector<std::string> &unknownKeys) {
  ScannerConfig::GameConfig game;
  ObjectReader reader(object, prefix, unknownKeys);
  reader.read("embeddingModelPath", game.embeddingModelPath);
  reader.read("confidenceThreshold", game.confidenceThreshold);

  if (const ConfigValue &section = reader.member("setSymbolDetection");
      section.isObject()) {
    ObjectReader setSymbol(section, prefix + "setSymbolDetection.",
                           unknownKeys);
    setSymbol.read("detectionModelPath", game.setSymbolDetectionModelPath);
    setSymbol.read("embeddingModelPath", game.setSymbolEmbedderModelPath);
    setSymbol.read("detectionThreshold", game.setSymbolDetectionThreshold);
    setSymbol.read("confidenceThreshold", game.setSymbolConfidenceThreshold);
    // Must match the exported model's input_shape or inference fails.
    setSymbol.read("imageSize", game.setSymbolImageSize);
    setSymbol.reportUnknown();
  }

  if (const ConfigValue &section = reader.member("colorDetection");
      section.isObject()) {
    ObjectReader color(section, prefix + "colorDetection.", unknownKeys);
    color.read("modelPath", game.colorDetectionModelPath);
    color.read("dotsRegionRatio", game.dotsRegionRatio);
    color.read("minDotsRegionSize", game.minDotsRegionSize);
    color.reportUnknown();
  }

  reader.reportUnknown();
  return game;
}

} // namespace

ParsedScannerConfig
parseScannerConfig(const ConfigValue &root,
                   const std::vector<std::string_view> &hostKeys) {
  if (!root.isObject()) {
    throw std::runtime_error(std::string("config: the root must be an object, "
                                         "but is ") +
                             root.typeName());
  }

  ParsedScannerConfig parsed;
  ScannerConfig &config = parsed.config;
  ObjectReader reader(root, "", parsed.unknownKeys);
  for (const auto key : hostKeys) {
    reader.allow(key);
  }

  reader.read("segmentationModelPath", config.segmentationModelPath);
  reader.read("embeddingModelPath", config.embeddingModelPath);
  reader.read("scanMode", config.scanMode);

  reader.read("segmentationThreshold", config.segmentationThreshold);
  reader.read("iouThreshold", config.iouThreshold);
  reader.read("confidenceThreshold", config.confidenceThreshold);
  reader.read("disambiguationThreshold", config.disambiguationThreshold);
  reader.read("minGameConfidence", config.minGameConfidence);

  reader.read("maxMatches", config.maxMatches);
  reader.read("searchCandidates", config.searchCandidates);
  reader.read("captureImage", config.captureImage);

  reader.read("useDetectionSelection", config.useDetectionSelection);
  reader.read("useSidewaysFlipCache", config.useSidewaysFlipCache);

  reader.read("blurThreshold", config.blurThreshold);
  reader.read("lowLightThreshold", config.lowLightThreshold);
  reader.read("lowLightGamma", config.lowLightGamma);
  reader.read("maxFrameRate", config.maxFrameRate);

  reader.read("minCardsForMulti", config.minCardsForMulti);
  reader.read("multiStableFrames", config.multiStableFrames);
  reader.read("freezeOnMulti", config.freezeOnMulti);

  config.gameClassMapping =
      parseGameClassMapping(reader.member("gameClassMapping"));

  if (const auto *games = std::get_if<ConfigValue::Object>(
          &reader.member("gameSpecificConfig").value)) {
    for (const auto &[name, game] : *games) {
      if (!game.isObject()) {
        throw std::runtime_error("config: gameSpecificConfig['" + name +
                                 "'] must be an object");
      }
      config.gameSpecificConfig[name] = parseGameConfig(
          game, "gameSpecificConfig." + name + ".", parsed.unknownKeys);
    }
  }

  reader.reportUnknown();
  for (const auto &key : parsed.unknownKeys) {
    log(LOG_LEVEL::Info, "[Config]", "ignoring unknown key:", key);
  }
  return parsed;
}

} // namespace cardscanner
