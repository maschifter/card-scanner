// Placeholder entry point. Proves card-scanner-core compiles and links outside
// React Native. The real wrapper (ONNX backend, capture loop, path setup) is a
// follow-up PR - see README.md.

#include <Constants.h>
#include <PathProvider.h>
#include <types/ScannerConfig.h>

#include <filesystem>

#include <iostream>

int main() {
  // Contract from PathProvider.h: set these before anything touches the
  // database. The real wrapper will use platform dirs (~/Library/Application
  // Support on macOS, %LOCALAPPDATA% on Windows); a temp dir is enough here.
  const auto base = std::filesystem::temp_directory_path() / "card-scanner";
  pathprovider::set_db_path((base / "db").string());
  pathprovider::set_cache_path((base / "cache").string());

  cardscanner::ScannerConfig config;
  config.scanMode = "single";
  std::cout << "card-scanner-core linked. scanMode=" << config.scanMode
            << ", embedding dim="
            << cardscanner::constants::model::EMBEDDING_DIMENSION << "\n";
  return 0;
}
