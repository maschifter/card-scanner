#include "OfflineBenchmark.h"

#include <DatabaseManager.h>
#include <ScannerRegistry.h>
#include <types/BenchmarkRecord.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cardscanner {
namespace desktop {

std::string benchmarkFileName() {
  const std::time_t now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  std::ostringstream name;
  name << "benchmark_desktop_" << std::put_time(&tm, "%Y-%m-%dT%H-%M-%S")
       << ".json";
  return name.str();
}

namespace {

/// Empty when no manifest was given.
nlohmann::json loadManifest(const std::filesystem::path &path) {
  if (path.empty()) {
    return nlohmann::json::object();
  }
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("benchmark: cannot read manifest '" +
                             path.string() + "'");
  }
  return nlohmann::json::parse(in);
}

std::vector<BenchmarkImageInput> listImages(const std::filesystem::path &dir,
                                            const nlohmann::json &manifest) {
  std::vector<BenchmarkImageInput> images;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    auto ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") {
      continue;
    }
    // Manifest keys include the game directory; file names alone collide.
    const std::string key =
        entry.path().parent_path().filename().string() + "/" +
        entry.path().filename().string();
    const auto truth = manifest.find(key);
    if (truth == manifest.end()) {
      // Still recorded: timings count, outcome reads "no_ground_truth".
      images.push_back({entry.path().string(), "", {}});
    } else {
      images.push_back({entry.path().string(),
                        truth->value("game", std::string{}),
                        truth->value("cardIds", std::vector<std::string>{})});
    }
  }
  std::sort(images.begin(), images.end(), [](const auto &a, const auto &b) {
    return a.imagePath < b.imagePath;
  });
  return images;
}

} // namespace

size_t runOfflineBenchmark(const OfflineBenchmarkOptions &options) {
#if !CARDSCANNER_BENCHMARK
  throw std::runtime_error(
      "benchmark: this build compiled the timers out; reconfigure with "
      "-DCARDSCANNER_BENCHMARK=1 and rebuild");
#endif

  const auto images = listImages(options.imagesDir,
                                 loadManifest(options.manifestPath));
  if (images.empty()) {
    throw std::runtime_error("benchmark: no .jpg/.jpeg/.png images in '" +
                             options.imagesDir.string() + "'");
  }

  const auto runResult = ScannerRegistry::runBenchmark(
      images, options.warmupIterations, options.benchmarkIterations);

  std::ofstream out(options.outPath);
  out << runResult.json;
  if (!out) {
    throw std::runtime_error("benchmark: cannot write '" +
                             options.outPath.string() + "'");
  }
  return runResult.recordCount;
}

} // namespace desktop
} // namespace cardscanner
