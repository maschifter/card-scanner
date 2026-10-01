#include "ScannerConfigLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace cardscanner {
namespace desktop {

namespace {

using nlohmann::json;

/// Reads an optional scalar, falling back to the C++ default.
template <typename T>
T optional(const json &obj, const char *key, T fallback) {
  const auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) {
    return fallback;
  }
  return it->get<T>();
}

std::string requiredString(const json &obj, const char *key) {
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_string() || it->get<std::string>().empty()) {
    throw std::runtime_error(std::string("config: missing required string '") +
                             key + "'");
  }
  return it->get<std::string>();
}

/**
 * @brief Warns about keys the loader never reads.
 *
 * A misspelled optional key otherwise keeps the default without a trace. 
 * Handle comments as well.
 */
void warnUnknownKeys(const json &obj, const std::string &prefix,
                     std::initializer_list<const char *> known) {
  if (!obj.is_object()) {
    return;
  }
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    const std::string &key = it.key();
    if (key.compare(0, 2, "//") == 0 ||
        std::find(known.begin(), known.end(), key) != known.end()) {
      continue;
    }
    std::cerr << "config: unknown key '" << prefix << key << "' is ignored\n";
  }
}

/// Resolves a relative model path against modelsDir; absolute paths pass
/// through, so a config can mix bundled and user-supplied models.
std::string resolveModel(const std::filesystem::path &modelsDir,
                         const std::string &value) {
  if (value.empty()) {
    return value;
  }
  const std::filesystem::path p(value);
  return p.is_absolute() ? value : (modelsDir / p).string();
}

/**
 * @brief Parses gameClassMapping and enforces its contiguity invariant.
 *
 * A swapped segmentation model with a different class order otherwise
 * misroutes every database search, and the symptom is bad matches, not an error.
 */
GameClassMap parseGameClassMapping(const json &root) {
  const auto it = root.find("gameClassMapping");
  if (it == root.end() || !it->is_object() || it->empty()) {
    throw std::runtime_error(
        "config: 'gameClassMapping' is required and must be a non-empty object "
        "mapping model class ids to database names");
  }

  GameClassMap mapping;
  for (const auto &[key, value] : it->items()) {
    int classId = 0;
    try {
      classId = std::stoi(key);
    } catch (const std::exception &) {
      throw std::runtime_error("config: gameClassMapping key '" + key +
                               "' is not an integer class id");
    }

    std::vector<std::string> games;
    if (value.is_string()) {
      games.push_back(value.get<std::string>());
    } else if (value.is_array()) {
      // Several names when the model merges games it cannot tell apart.
      games = value.get<std::vector<std::string>>();
    } else {
      throw std::runtime_error("config: gameClassMapping['" + key +
                               "'] must be a string or array of strings");
    }

    if (games.empty()) {
      throw std::runtime_error("config: gameClassMapping['" + key +
                               "'] names no database");
    }
    mapping[classId] = std::move(games);
  }

  // Contiguous from zero, or the entry count no longer decodes the tensor.
  for (int i = 0; i < static_cast<int>(mapping.size()); i++) {
    if (mapping.find(i) == mapping.end()) {
      throw std::runtime_error(
          "config: gameClassMapping must use contiguous keys 0.." +
          std::to_string(mapping.size() - 1) + " (missing " +
          std::to_string(i) +
          "). The entry count decodes the segmentation model's prediction "
          "tensor, so a gap silently misroutes every search.");
    }
  }

  // Checks the map is self-consistent, not that it matches the model. A
  // cross-check against the segmentation output channel dim (4 + numClasses +
  // 32) needs the backend to expose shapes.

  return mapping;
}

ScannerConfig::GameConfig parseGameConfig(const json &obj,
                                          const std::filesystem::path &modelsDir,
                                          const std::string &prefix) {
  warnUnknownKeys(obj, prefix,
                  {"embeddingModelPath", "confidenceThreshold", "setSymbolDetection",
                   "colorDetection"});

  ScannerConfig::GameConfig game;

  game.embeddingModelPath =
      resolveModel(modelsDir, optional<std::string>(obj, "embeddingModelPath", ""));

  const auto confidence = obj.find("confidenceThreshold");
  if (confidence != obj.end() && confidence->is_number()) {
    game.confidenceThreshold = confidence->get<float>();
  }

  const auto setSymbol = obj.find("setSymbolDetection");
  if (setSymbol != obj.end() && setSymbol->is_object()) {
    warnUnknownKeys(*setSymbol, prefix + "setSymbolDetection.",
                    {"detectionModelPath", "embeddingModelPath", "detectionThreshold",
                     "confidenceThreshold", "imageSize"});
    game.setSymbolDetectionModelPath = resolveModel(
        modelsDir, optional<std::string>(*setSymbol, "detectionModelPath", ""));
    game.setSymbolEmbedderModelPath = resolveModel(
        modelsDir, optional<std::string>(*setSymbol, "embeddingModelPath", ""));
    game.setSymbolDetectionThreshold = optional<float>(
        *setSymbol, "detectionThreshold", game.setSymbolDetectionThreshold);
    game.setSymbolConfidenceThreshold = optional<float>(
        *setSymbol, "confidenceThreshold", game.setSymbolConfidenceThreshold);
    // Must match the exported model's input_shape or inference fails.
    game.setSymbolImageSize =
        optional<int>(*setSymbol, "imageSize", game.setSymbolImageSize);
  }

  const auto color = obj.find("colorDetection");
  if (color != obj.end() && color->is_object()) {
    warnUnknownKeys(*color, prefix + "colorDetection.",
                    {"modelPath", "dotsRegionRatio", "minDotsRegionSize"});
    game.colorDetectionModelPath = resolveModel(
        modelsDir, optional<std::string>(*color, "modelPath", ""));
    game.dotsRegionRatio =
        optional<double>(*color, "dotsRegionRatio", game.dotsRegionRatio);
    game.minDotsRegionSize =
        optional<int>(*color, "minDotsRegionSize", game.minDotsRegionSize);
  }

  return game;
}

/// Resolves a directory key against the config file's own location.
std::filesystem::path resolveDir(const json &root, const char *key,
                                 const std::filesystem::path &base,
                                 const std::string &fallback) {
  const std::filesystem::path value(optional<std::string>(root, key, fallback));
  return value.is_absolute() ? value : (base / value);
}

} // namespace

LoadedConfig loadConfigFile(const std::filesystem::path &configPath) {
  std::ifstream stream(configPath);
  if (!stream) {
    throw std::runtime_error("config: cannot open '" + configPath.string() + "'");
  }

  json root;
  try {
    root = json::parse(stream, nullptr, true, /*ignore_comments=*/true);
  } catch (const json::parse_error &e) {
    throw std::runtime_error("config: invalid JSON in '" + configPath.string() +
                             "': " + e.what());
  }

  warnUnknownKeys(root, "",
                  {"modelsDir", "databasesDir", "cacheDir", "segmentationModelPath",
                   "embeddingModelPath", "scanMode", "segmentationThreshold",
                   "iouThreshold", "confidenceThreshold", "disambiguationThreshold",
                   "minGameConfidence", "maxMatches", "searchCandidates",
                   "captureImage", "useDetectionSelection", "useSidewaysFlipCache",
                   "blurThreshold", "lowLightThreshold", "lowLightGamma",
                   "maxFrameRate", "gameClassMapping", "gameSpecificConfig"});

  const std::filesystem::path base =
      std::filesystem::absolute(configPath).parent_path();
  const std::filesystem::path modelsDir = resolveDir(root, "modelsDir", base, "models");

  LoadedConfig loaded;
  loaded.paths.databases = resolveDir(root, "databasesDir", base, "databases");
  loaded.paths.cache = resolveDir(root, "cacheDir", base, "cache");

  ScannerConfig &config = loaded.scanner;

  config.segmentationModelPath =
      resolveModel(modelsDir, requiredString(root, "segmentationModelPath"));
  config.embeddingModelPath =
      resolveModel(modelsDir, requiredString(root, "embeddingModelPath"));

  config.scanMode = optional<std::string>(root, "scanMode", "single");
  if (config.scanMode != "single" && config.scanMode != "multiple") {
    throw std::runtime_error("config: scanMode must be 'single' or 'multiple', got '" +
                             config.scanMode + "'");
  }

  // Matching mobile's defaults, so both paths score the same images alike.
  config.segmentationThreshold = optional<float>(root, "segmentationThreshold", 0.6f);
  config.iouThreshold = optional<float>(root, "iouThreshold", 0.7f);
  config.confidenceThreshold = optional<float>(root, "confidenceThreshold", 0.6f);
  config.disambiguationThreshold = optional<float>(
      root, "disambiguationThreshold", ScannerConfig::DEFAULT_DISAMBIGUATION_THRESHOLD);
  config.minGameConfidence = optional<float>(root, "minGameConfidence", 1e-5f);

  config.maxMatches = optional<int>(root, "maxMatches", 5);
  config.searchCandidates = optional<int>(root, "searchCandidates", 100);
  config.captureImage = optional<bool>(root, "captureImage", false);

  config.useDetectionSelection = optional<bool>(root, "useDetectionSelection", true);
  config.useSidewaysFlipCache = optional<bool>(root, "useSidewaysFlipCache", true);

  config.blurThreshold = optional<double>(root, "blurThreshold", 0.0);
  config.lowLightThreshold = optional<double>(
      root, "lowLightThreshold", ScannerConfig::DEFAULT_LOW_LIGHT_THRESHOLD);
  config.lowLightGamma = optional<double>(root, "lowLightGamma",
                                          ScannerConfig::DEFAULT_LOW_LIGHT_GAMMA);
  config.maxFrameRate =
      optional<int>(root, "maxFrameRate", ScannerConfig::DEFAULT_MAX_FRAME_RATE);

  config.gameClassMapping = parseGameClassMapping(root);

  const auto gameSpecific = root.find("gameSpecificConfig");
  if (gameSpecific != root.end() && gameSpecific->is_object()) {
    for (const auto &[game, value] : gameSpecific->items()) {
      if (!value.is_object()) {
        throw std::runtime_error("config: gameSpecificConfig['" + game +
                                 "'] must be an object");
      }
      // Searches key this map by gameClassMapping names; any other entry never applies.
      const bool mapped =
          std::any_of(config.gameClassMapping.begin(), config.gameClassMapping.end(),
                      [&game](const auto &entry) {
                        return std::find(entry.second.begin(), entry.second.end(),
                                         game) != entry.second.end();
                      });
      if (!mapped) {
        throw std::runtime_error("config: gameSpecificConfig['" + game +
                                 "'] names a game that no gameClassMapping "
                                 "entry routes to");
      }
      config.gameSpecificConfig[game] =
          parseGameConfig(value, modelsDir, "gameSpecificConfig." + game + ".");
    }
  }

  return loaded;
}

} // namespace desktop
} // namespace cardscanner
