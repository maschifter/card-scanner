#include "ScannerConfigLoader.h"
#include <PathProvider.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

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

/// The document in core's neutral shape. Knows no config keys.
ConfigValue toConfigValue(const json &value) {
  switch (value.type()) {
  case json::value_t::boolean:
    return {value.get<bool>()};
  case json::value_t::number_integer:
  case json::value_t::number_unsigned:
  case json::value_t::number_float:
    return {value.get<double>()};
  case json::value_t::string:
    return {value.get<std::string>()};
  case json::value_t::array: {
    ConfigValue::Array items;
    items.reserve(value.size());
    for (const auto &item : value) {
      items.push_back(toConfigValue(item));
    }
    return {std::move(items)};
  }
  case json::value_t::object: {
    ConfigValue::Object members;
    members.reserve(value.size());
    for (const auto &[key, member] : value.items()) {
      members.emplace_back(key, toConfigValue(member));
    }
    return {std::move(members)};
  }
  default:
    return {};
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

  // Core reads every scanner key; this loader reads only its own.
  auto parsed = parseScannerConfig(toConfigValue(root),
                                   {"modelsDir", "databasesDir", "cacheDir",
                                    "productEndpoint", "productImageBase",
                                    "productImageTransform"});

  const std::filesystem::path base =
      std::filesystem::absolute(configPath).parent_path();
  const std::filesystem::path modelsDir = resolveDir(root, "modelsDir", base, "models");

  LoadedConfig loaded;
  loaded.paths.databases = resolveDir(root, "databasesDir", base, "databases");
  loaded.paths.cache = resolveDir(root, "cacheDir", base, "cache");

  readInto(root, "productEndpoint", loaded.products.endpoint);
  readInto(root, "productImageBase", loaded.products.imageBase);
  readInto(root, "productImageTransform", loaded.products.imageTransform);

  // Model paths in the file are relative to modelsDir.
  ScannerConfig &config = loaded.scanner = std::move(parsed.config);
  const auto resolve = [&modelsDir](std::string &path) {
    path = resolveModel(modelsDir, path);
  };
  resolve(config.segmentationModelPath);
  resolve(config.embeddingModelPath);
  for (auto &[game, gameConfig] : config.gameSpecificConfig) {
    resolve(gameConfig.embeddingModelPath);
    resolve(gameConfig.setSymbolDetectionModelPath);
    resolve(gameConfig.setSymbolEmbedderModelPath);
    resolve(gameConfig.colorDetectionModelPath);
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
