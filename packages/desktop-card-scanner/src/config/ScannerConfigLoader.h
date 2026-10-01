#pragma once

#include <types/ScannerConfig.h>

#include <filesystem>

namespace cardscanner {
namespace desktop {

/// Separate from ScannerConfig: these feed pathprovider, which must be set
/// before DatabaseManager is first constructed.
struct DataPaths {
  std::filesystem::path databases;
  std::filesystem::path cache;
};

struct LoadedConfig {
  ScannerConfig scanner;
  DataPaths paths;
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

} // namespace desktop
} // namespace cardscanner
