#pragma once

#include <types/ScannerConfig.h>

#include <filesystem>
#include <string>

namespace cardscanner {
namespace desktop {

/// Separate from ScannerConfig: these feed pathprovider, which must be set
/// before DatabaseManager is first constructed.
struct DataPaths {
  std::filesystem::path databases;
  std::filesystem::path cache;
};

/// Where card names and art are looked up. Defaults are CardNexus production.
struct ProductSource {
  std::string endpoint = "https://api.cardnexus.com/orpc/product/getProduct";
  std::string imageBase = "https://ik.imagekit.io/cardnexus/production";
  /// Appended after the image path: ImageKit's URL transform, sized for the
  /// overlay.
  std::string imageTransform = "/tr:w-500,q-80";
};

struct LoadedConfig {
  ScannerConfig scanner;
  DataPaths paths;
  ProductSource products;
};

/**
 * @brief Reads a JSON config into a fully-populated ScannerConfig.
 *
 * Assigns every field, since ScannerConfig declares no initializers for most
 * members. Relative model paths resolve against modelsDir, which resolves
 * against the config file's own directory.
 *
 * @throws std::runtime_error on bad JSON, a missing required key, or a
 *         gameClassMapping that violates its documented invariant.
 */
LoadedConfig loadConfigFile(const std::filesystem::path &configPath);

/// Creates the data directories and points core's pathprovider at them. Must
/// run before anything touches DatabaseManager, which reads the db path once
/// when its singleton is first constructed.
void applyDataPaths(const DataPaths &paths);

} // namespace desktop
} // namespace cardscanner
