#pragma once

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
   * @param config Scanner configuration
   * @param warmupIterations 
   * @param benchmarkIterations 
   * @param yoloModel Segmentation model (must be initialized)
   * @param embeddingModel Default card embedding model (must be initialized)
   * @param setSymbolYolo Set symbol detector (nullable)
   * @param setSymbolEmbedder Set symbol embedder (nullable)
   * @param fabColorClassifier FAB color classifier (nullable)
   * @param gameEmbedders Per-game embedders (nullable)
   * @return Record count and the JSON payload
   * @throws std::runtime_error if the models are missing or an image fails to
   * decode
   */
  static BenchmarkRunResult
  run(const std::vector<BenchmarkImageInput> &images,
      const ScannerConfig &config, int warmupIterations,
      int benchmarkIterations, cardscanner::DatabaseManager &dbManager,
      cardscanner::YoloSegmentationModel *yoloModel,
      cardscanner::CardEmbeddingModel *embeddingModel,
      cardscanner::SetSymbolYoloModel *setSymbolYolo,
      cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
      cardscanner::FABColorClassifier *fabColorClassifier,
      const cardscanner::GameEmbedders *gameEmbedders);

  /**
   * @brief Serialize records to a JSON array, one record per line.
   */
  static std::string toJson(const std::vector<BenchmarkRecord> &records);
};

} // namespace benchmark
} // namespace cardscanner

