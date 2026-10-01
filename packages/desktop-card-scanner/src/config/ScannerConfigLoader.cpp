#include "ScannerConfigLoader.h"
#include <Log.h>
#include <PathProvider.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <stdexcept>

namespace cardscanner {
namespace desktop {

namespace {

using nlohmann::json;

/// Reads an optional scalar, falling back to the C++ default.
template <typename T>
T valueOrDefault(const json &obj, const char *key, T fallback) {
  const auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) {
    return fallback;
  }
  try {
    return it->get<T>();
  } catch (const json::type_error &e) {
    const std::string what = e.what();
    const auto tag = what.find("] ");
    throw std::runtime_error(std::string("config: '") + key + "' " +
                             (tag == std::string::npos ? what : what.substr(tag + 2)));
  }
}

/// Overwrites target only when the key is present; T is deduced from the field.
template <typename T>
void readInto(const json &obj, const char *key, T &target) {
  target = valueOrDefault<T>(obj, key, target);
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
    const std::string fullKey = prefix + key;
    log(LOG_LEVEL::Info, "[Config]", "ignoring unknown key:", fullKey);
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

/// Parses gameClassMapping's JSON shape; ScannerConfig::validate checks the map.
GameClassMap parseGameClassMapping(const json &root) {
  const auto it = root.find("gameClassMapping");
  if (it == root.end()) {
    return {}; // validate() reports the missing mapping
  }
  if (!it->is_object()) {
    throw std::runtime_error("config: 'gameClassMapping' must be an object "
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
    } else if (value.is_array() &&
               std::all_of(value.begin(), value.end(),
                           [](const json &name) { return name.is_string(); })) {
      // Several names when the model merges games it cannot tell apart.
      games = value.get<std::vector<std::string>>();
    } else {
      throw std::runtime_error("config: gameClassMapping['" + key +
                               "'] must be a string or array of strings");
    }

    mapping[classId] = std::move(games);
  }
  return mapping;
}

ScannerConfig::GameConfig parseGameConfig(const json &obj,
                                          const std::filesystem::path &modelsDir,
                                          const std::string &prefix) {
  warnUnknownKeys(obj, prefix,
                  {"embeddingModelPath", "confidenceThreshold", "setSymbolDetection",
                   "colorDetection"});

  ScannerConfig::GameConfig game;

  readInto(obj, "embeddingModelPath", game.embeddingModelPath);
  game.embeddingModelPath = resolveModel(modelsDir, game.embeddingModelPath);

  const auto confidence = obj.find("confidenceThreshold");
  if (confidence != obj.end() && !confidence->is_null()) {
    game.confidenceThreshold = valueOrDefault<float>(obj, "confidenceThreshold", 0.0f);
  }

  const auto setSymbol = obj.find("setSymbolDetection");
  if (setSymbol != obj.end() && setSymbol->is_object()) {
    warnUnknownKeys(*setSymbol, prefix + "setSymbolDetection.",
                    {"detectionModelPath", "embeddingModelPath", "detectionThreshold",
                     "confidenceThreshold", "imageSize"});
    readInto(*setSymbol, "detectionModelPath", game.setSymbolDetectionModelPath);
    readInto(*setSymbol, "embeddingModelPath", game.setSymbolEmbedderModelPath);
    game.setSymbolDetectionModelPath =
        resolveModel(modelsDir, game.setSymbolDetectionModelPath);
    game.setSymbolEmbedderModelPath =
        resolveModel(modelsDir, game.setSymbolEmbedderModelPath);
    readInto(*setSymbol, "detectionThreshold", game.setSymbolDetectionThreshold);
    readInto(*setSymbol, "confidenceThreshold", game.setSymbolConfidenceThreshold);
    // Must match the exported model's input_shape or inference fails.
    readInto(*setSymbol, "imageSize", game.setSymbolImageSize);
  }

  const auto color = obj.find("colorDetection");
  if (color != obj.end() && color->is_object()) {
    warnUnknownKeys(*color, prefix + "colorDetection.",
                    {"modelPath", "dotsRegionRatio", "minDotsRegionSize"});
    readInto(*color, "modelPath", game.colorDetectionModelPath);
    game.colorDetectionModelPath = resolveModel(modelsDir, game.colorDetectionModelPath);
    readInto(*color, "dotsRegionRatio", game.dotsRegionRatio);
    readInto(*color, "minDotsRegionSize", game.minDotsRegionSize);
  }

  return game;
}

/// Resolves a directory key against the config file's own location.
std::filesystem::path resolveDir(const json &root, const char *key,
                                 const std::filesystem::path &base,
                                 const std::string &fallback) {
  const std::filesystem::path value(valueOrDefault<std::string>(root, key, fallback));
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
                   "maxFrameRate", "gameClassMapping", "gameSpecificConfig",
                   "productEndpoint", "productImageBase", "productImageTransform"});

  const std::filesystem::path base =
      std::filesystem::absolute(configPath).parent_path();
  const std::filesystem::path modelsDir = resolveDir(root, "modelsDir", base, "models");

  LoadedConfig loaded;
  loaded.paths.databases = resolveDir(root, "databasesDir", base, "databases");
  loaded.paths.cache = resolveDir(root, "cacheDir", base, "cache");

  readInto(root, "productEndpoint", loaded.products.endpoint);
  readInto(root, "productImageBase", loaded.products.imageBase);
  readInto(root, "productImageTransform", loaded.products.imageTransform);

  ScannerConfig &config = loaded.scanner;

#define SET_CONFIG_OR_DEFAULT(field) readInto(root, #field, config.field)

  SET_CONFIG_OR_DEFAULT(segmentationModelPath);
  SET_CONFIG_OR_DEFAULT(embeddingModelPath);
  config.segmentationModelPath = resolveModel(modelsDir, config.segmentationModelPath);
  config.embeddingModelPath = resolveModel(modelsDir, config.embeddingModelPath);

  SET_CONFIG_OR_DEFAULT(scanMode);

  SET_CONFIG_OR_DEFAULT(segmentationThreshold);
  SET_CONFIG_OR_DEFAULT(iouThreshold);
  SET_CONFIG_OR_DEFAULT(confidenceThreshold);
  SET_CONFIG_OR_DEFAULT(disambiguationThreshold);
  SET_CONFIG_OR_DEFAULT(minGameConfidence);

  SET_CONFIG_OR_DEFAULT(maxMatches);
  SET_CONFIG_OR_DEFAULT(searchCandidates);
  SET_CONFIG_OR_DEFAULT(captureImage);

  SET_CONFIG_OR_DEFAULT(useDetectionSelection);
  SET_CONFIG_OR_DEFAULT(useSidewaysFlipCache);

  SET_CONFIG_OR_DEFAULT(blurThreshold);
  SET_CONFIG_OR_DEFAULT(lowLightThreshold);
  SET_CONFIG_OR_DEFAULT(lowLightGamma);
  SET_CONFIG_OR_DEFAULT(maxFrameRate);

#undef SET_CONFIG_OR_DEFAULT

  config.gameClassMapping = parseGameClassMapping(root);

  const auto gameSpecific = root.find("gameSpecificConfig");
  if (gameSpecific != root.end() && gameSpecific->is_object()) {
    for (const auto &[game, value] : gameSpecific->items()) {
      if (!value.is_object()) {
        throw std::runtime_error("config: gameSpecificConfig['" + game +
                                 "'] must be an object");
      }
      config.gameSpecificConfig[game] =
          parseGameConfig(value, modelsDir, "gameSpecificConfig." + game + ".");
    }
  }

  return loaded;
}

void applyDataPaths(const DataPaths &paths) {
  std::filesystem::create_directories(paths.databases);
  std::filesystem::create_directories(paths.cache);
  pathprovider::set_db_path(paths.databases.string());
  pathprovider::set_cache_path(paths.cache.string());
}

} // namespace desktop
} // namespace cardscanner
