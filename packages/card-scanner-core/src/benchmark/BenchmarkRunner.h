#pragma once

#include "../core/ScannerContext.h"
#include "../types/BenchmarkRecord.h"
#include "../types/ScannerConfig.h"
#include <DatabaseManager.h>
#include <models/CardEmbeddingModel.h>
#include <models/YoloSegmentationModel.h>
#include <models/fab/FABColorClassifier.h>
#include <models/mtg/SetSymbolEmbedder.h>
#include <models/mtg/SetSymbolYoloModel.h>
#include <string>
#include <vector>

namespace cardscanner {
namespace benchmark {

/**
 * @class BenchmarkRunner
 * @brief Drives ScannerPipeline::processFrame repeatedly over a fixed set of
 * still images to collect per-stage timings on-device.
 */
class BenchmarkRunner {
public:
  /**
   * @brief Run the benchmark, returning every record as JSON text.
   *
   * @param images Images to scan
   * @param ctx Config and models; yolo and embedding must be set
   * @param warmupIterations Untimed runs per image before the measured ones
   * @param benchmarkIterations Measured runs per image
   * @return Record count and the JSON payload
   * @throws std::runtime_error if the models are missing or an image fails to
   * decode
   */
  static BenchmarkRunResult run(const std::vector<BenchmarkImageInput> &images,
                                const core::ScannerContext &ctx,
                                int warmupIterations, int benchmarkIterations);

  /**
   * @brief Serialize records to a JSON array, one record per line.
   */
  static std::string toJson(const std::vector<BenchmarkRecord> &records);
};

} // namespace benchmark
} // namespace cardscanner

