#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace cardscanner {
namespace desktop {

/// benchmark_desktop_<timestamp>.json, mirroring mobile's exports.
std::string benchmarkFileName();

struct OfflineBenchmarkOptions {
  std::filesystem::path imagesDir;
  std::filesystem::path outPath;
  /// Optional "<game>/<file>" -> {game, cardIds} map. Images missing from it
  /// report outcome "no_ground_truth" and only timings.
  std::filesystem::path manifestPath;
  // Mobile's defaults, so the numbers compare.
  int warmupIterations = 2;
  int benchmarkIterations = 10;
};

/**
 * @brief Runs core's BenchmarkRunner over every image in a directory.
 *
 * Timings are always real; accuracy needs options.manifestPath. The runner
 * forces comparable conditions - maxFrameRate 0, detection selection and the
 * flip cache off, warmup separated from the measured iterations.
 *
 * Call with the registry configured and models initialized; takes the pipeline
 * exclusively for the duration.
 *
 * @return Number of records written to options.outPath.
 * @throws std::runtime_error on no images, uninitialized models, or a failed
 *         write.
 */
size_t runOfflineBenchmark(const OfflineBenchmarkOptions &options);

} // namespace desktop
} // namespace cardscanner
