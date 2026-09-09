#include "BenchmarkRunner.h"
#include "../core/ScannerPipeline.h"
#include "BenchmarkCollector.h"
#include <Log.h>
#include "../utils/ImageUtils.h"
#include <algorithm>
#include <iomanip>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <stdexcept>

// Per-scan progress rows on the platform log, on top of the JSON a run returns.
// Enabled by defining CARDSCANNER_BENCHMARK_VERBOSE=1; the mobile package wires
// that from app.json: "extra": { "cardScanner": { "benchmarkVerbose": true } }

namespace cardscanner {
namespace benchmark {

namespace {

#if CARDSCANNER_BENCHMARK_VERBOSE
bool containsGame(const std::string &predictedGames, const std::string &game) {
  std::istringstream stream(predictedGames);
  std::string token;
  while (stream >> token) {
    if (token == game) {
      return true;
    }
  }
  return false;
}

// Fixed-width so consecutive rows line up as a table in logcat/Console.
std::string progressRow(const BenchmarkRecord &r) {
  std::ostringstream row;
  row << std::fixed << std::setprecision(1);
  row << std::left << std::setw(9) << r.game << std::setw(26)
      << r.cardId.substr(0, 25) << std::right << (r.isWarmup ? "warmup" : "run ")
      << ' ' << std::setw(3) << r.iteration << " | total " << std::setw(7)
      << r.totalMs << "ms  yolo " << std::setw(6) << r.yoloMs << "  preproc "
      << std::setw(6) << r.preprocMs << "  save " << std::setw(6) << r.saveMs
      << "  embed " << std::setw(6) << r.embedMs << "  db " << std::setw(6)
      << r.dbSearchMs;

  // Only when models actually ran, so a "0.0" is never mistaken for a measured cost.
  if (r.setSymbolRan) {
    row << "  set_symbol(yolo " << std::setw(6) << r.setSymbolYoloMs
        << " prep " << std::setw(6) << r.setSymbolPreprocMs << " embed "
        << std::setw(6) << r.setSymbolEmbedMs << " db " << std::setw(6)
        << r.setSymbolDbSearchMs << ")";
  }
  if (r.fabColorRan) {
    row << "  fab_color(prep " << std::setw(6) << r.fabColorPreprocMs
        << " classify " << std::setw(6) << r.fabColorClassifyMs << ")";
  }

  row << " | dets " << r.detectionCount << " (conf " << std::setprecision(3)
      << r.yoloConfidence << ") games " << r.gamesSearched;

  // Flagged when the expected game is absent from YOLO's predictions.
  if (!r.yoloPredictedGames.empty()) {
    row << " yolo_game=" << r.yoloPredictedGames;
    if (!containsGame(r.yoloPredictedGames, r.game)) {
      row << " (MISMATCH, expected " << r.game << ")";
    }
  }

  row << " | " << toString(r.outcome);

  // The reported match is blank on a rejected scan, so fallback to the raw.
  if (!r.topMatchCardId.empty()) {
    row << " -> " << r.topMatchCardId << " @ " << r.topScore;
  } else if (!r.rawTopCardId.empty()) {
    row << " -> (rejected) " << r.rawTopCardId << " @ " << r.rawTopScore;
  }

  return row.str();
}
#endif

// The exact print on the photo; the rest of cardIds are its accepted variants.
const std::string &expectedId(const BenchmarkImageInput &input) {
  static const std::string none;
  return input.cardIds.empty() ? none : input.cardIds.front();
}

// Minimal JSON string escaping.
std::string jsonEscape(const std::string &value) {
  std::ostringstream escaped;
  for (char c : value) {
    switch (c) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<int>(static_cast<unsigned char>(c)) << std::dec
                << std::setfill(' ');
      } else {
        escaped << c;
      }
    }
  }
  return escaped.str();
}

std::string jsonString(const std::string &value) {
  return '"' + jsonEscape(value) + '"';
}

std::string jsonRecord(const BenchmarkRecord &r) {
  std::ostringstream obj;
  obj << "{";

  obj << "\"game\":" << jsonString(r.game) << ','
      << "\"cardId\":" << jsonString(r.cardId) << ','
      << "\"iteration\":" << r.iteration << ','
      << "\"isWarmup\":" << (r.isWarmup ? "true" : "false") << ',';

  obj << "\"yoloMs\":" << r.yoloMs << ','
      << "\"yoloConfidence\":" << r.yoloConfidence << ','
      << "\"detectionCount\":" << r.detectionCount << ','
      << "\"preprocMs\":" << r.preprocMs << ',' << "\"saveMs\":" << r.saveMs
      << ',' << "\"embedMs\":" << r.embedMs << ','
      << "\"dbSearchMs\":" << r.dbSearchMs << ','
      << "\"gamesSearched\":" << r.gamesSearched << ',';

  obj << "\"yoloPredictedGames\":" << jsonString(r.yoloPredictedGames) << ',';

  obj << "\"setSymbolRan\":" << (r.setSymbolRan ? "true" : "false") << ','
      << "\"setSymbolYoloMs\":" << r.setSymbolYoloMs << ','
      << "\"setSymbolPreprocMs\":" << r.setSymbolPreprocMs << ','
      << "\"setSymbolEmbedMs\":" << r.setSymbolEmbedMs << ','
      << "\"setSymbolDbSearchMs\":" << r.setSymbolDbSearchMs << ',';

  obj << "\"fabColorRan\":" << (r.fabColorRan ? "true" : "false") << ','
      << "\"fabColorPreprocMs\":" << r.fabColorPreprocMs << ','
      << "\"fabColorClassifyMs\":" << r.fabColorClassifyMs << ',';

  obj << "\"totalMs\":" << r.totalMs << ','
      << "\"topMatchCardId\":" << jsonString(r.topMatchCardId) << ','
      << "\"topScore\":" << r.topScore << ','
      << "\"matchCorrect\":" << (r.matchCorrect ? "true" : "false") << ','
      << "\"rawTopScore\":" << r.rawTopScore << ','
      << "\"rawTopCardId\":" << jsonString(r.rawTopCardId) << ','
      << "\"outcome\":" << jsonString(toString(r.outcome));

  obj << "}";
  return obj.str();
}

} // namespace

std::string BenchmarkRunner::toJson(
    const std::vector<BenchmarkRecord> &records) {
  std::ostringstream json;
  json << "[\n";
  for (size_t i = 0; i < records.size(); i++) {
    json << "  " << jsonRecord(records[i]);
    if (i + 1 < records.size()) {
      json << ',';
    }
    json << '\n';
  }
  json << "]\n";
  return json.str();
}

// Raw pointers are the same as in the scanner pipeline, that's why used
BenchmarkRunResult BenchmarkRunner::run(
    const std::vector<BenchmarkImageInput> &images,
    const ScannerConfig &config, int warmupIterations,
    int benchmarkIterations, cardscanner::DatabaseManager &dbManager,
    cardscanner::YoloSegmentationModel *yoloModel,
    cardscanner::CardEmbeddingModel *embeddingModel,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    cardscanner::FABColorClassifier *fabColorClassifier,
    const cardscanner::GameEmbedders *gameEmbedders) {

  if (!yoloModel || !embeddingModel) {
    throw std::runtime_error(
        "Benchmark requires initialized YOLO and embedding models");
  }

  // Disable frame-rate throttling so every iteration runs the full pipeline.
  ScannerConfig benchmarkConfig = config;
  benchmarkConfig.maxFrameRate = 0;
  // Images are unrelated prepared single cards - cross-frame state (sticky
  // tracking, flip cache) would make results depend on run order.
  benchmarkConfig.useDetectionSelection = false;
  benchmarkConfig.useSidewaysFlipCache = false;

  std::vector<BenchmarkRecord> records;
  records.reserve(images.size() *
                  static_cast<size_t>(warmupIterations + benchmarkIterations));

  const int totalIterations = warmupIterations + benchmarkIterations;

#if CARDSCANNER_BENCHMARK_VERBOSE
  log(LOG_LEVEL::Info, "[CardScanner][Benchmark] === run start:",
      images.size(), "images x", totalIterations, "iterations (",
      warmupIterations, "warmup +", benchmarkIterations, "measured ) ===");
  log(LOG_LEVEL::Info, "[CardScanner][Benchmark] conf_threshold:",
      benchmarkConfig.confidenceThreshold,
      "seg_threshold:", benchmarkConfig.segmentationThreshold,
      "disambig_threshold:", benchmarkConfig.disambiguationThreshold);
  size_t imageIndex = 0;
  size_t correctCount = 0;
#endif

  for (const auto &input : images) {
    cv::Mat imageRGB = utils::ImageUtils::loadImageRGB(input.imagePath);

#if CARDSCANNER_BENCHMARK_VERBOSE
    ++imageIndex;
    log(LOG_LEVEL::Info, "[CardScanner][Benchmark] ---", imageIndex, "/",
        images.size(), "--", input.game, "/", expectedId(input), "(",
        imageRGB.cols, "x", imageRGB.rows, ")");
#endif

    for (int iter = 1; iter <= totalIterations; iter++) {
      bool isWarmup = iter <= warmupIterations;

      // Cleared per iteration.
      BenchmarkCollector::reset();
      BenchmarkCollector::beginBenchmarkRecord();

      auto scanResult =
          core::ScannerPipeline::processFrame(
              imageRGB, benchmarkConfig, dbManager, yoloModel, embeddingModel,
              setSymbolYolo, setSymbolEmbedder, fabColorClassifier,
                gameEmbedders);

      // processFrame closes the record after the first detection; reopen it
      // so the deferred save stage still lands in this record's saveMs.
      BenchmarkCollector::beginBenchmarkRecord();
      core::ScannerPipeline::saveCardImages(scanResult, benchmarkConfig);
      BenchmarkCollector::endBenchmarkRecord();

      BenchmarkRecord record;

      // Main information
      record.game = input.game;
      record.cardId = expectedId(input);
      record.iteration = isWarmup ? iter : iter - warmupIterations;
      record.isWarmup = isWarmup;

      // Times and details
      record.yoloMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::YoloSegmentation);
      record.yoloConfidence = static_cast<float>(
          BenchmarkCollector::getBenchmarkedValue(Metric::YoloConfidence));
      record.detectionCount = static_cast<int>(
          BenchmarkCollector::getBenchmarkedValue(Metric::DetectionCount));
      record.preprocMs = BenchmarkCollector::getBenchmarkedMs(Stage::Preproc);
      record.saveMs = BenchmarkCollector::getBenchmarkedMs(Stage::Save);
      record.embedMs = BenchmarkCollector::getBenchmarkedMs(Stage::Embed);
      record.dbSearchMs = BenchmarkCollector::getBenchmarkedMs(Stage::DbSearch);
      record.gamesSearched = static_cast<int>(
          BenchmarkCollector::getBenchmarkedValue(Metric::GamesSearched));
      record.yoloPredictedGames =
          BenchmarkCollector::getBenchmarkedLabel(Label::YoloPredictedGames);

      // Kept even on a rejected match, so a near-miss stays legible.
      record.rawTopScore = static_cast<float>(
          BenchmarkCollector::getBenchmarkedValue(Metric::RawTopScore));
      record.rawTopCardId =
          BenchmarkCollector::getBenchmarkedLabel(Label::RawTopCardId);

      // MTG-connected
      record.setSymbolRan =
          BenchmarkCollector::getBenchmarkedRan(Stage::SetSymbolYolo);
      record.setSymbolYoloMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::SetSymbolYolo);
      record.setSymbolPreprocMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::SetSymbolPreproc);
      record.setSymbolEmbedMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::SetSymbolEmbed);
      record.setSymbolDbSearchMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::SetSymbolDbSearch);

      // FAB-connected. Flag means the classifier produced a result.
      record.fabColorRan =
          BenchmarkCollector::getBenchmarkedRan(Stage::FabColorClassify);
      record.fabColorPreprocMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::FabColorPreproc);
      record.fabColorClassifyMs =
          BenchmarkCollector::getBenchmarkedMs(Stage::FabColorClassify);

      record.totalMs = scanResult.processingTimeMs;

      if (!scanResult.cards.empty() && scanResult.cards[0].hasMatches()) {
        const auto &topMatch = scanResult.cards[0].matches[0];
        record.topMatchCardId = topMatch.cardId;
        record.topScore = topMatch.score;

        if (input.cardIds.empty()) {
          record.outcome = ScanOutcome::NoGroundTruth;
        } else {
          record.matchCorrect =
              std::find(input.cardIds.begin(), input.cardIds.end(),
                        topMatch.cardId) != input.cardIds.end();
          record.outcome = record.matchCorrect ? ScanOutcome::Correct
                                               : ScanOutcome::WrongMatch;
        }
      } else if (record.detectionCount == 0) {
        record.outcome = ScanOutcome::NoDetection;
      } else {
        record.outcome = ScanOutcome::BelowThreshold;
      }

#if CARDSCANNER_BENCHMARK_VERBOSE
      log(LOG_LEVEL::Info, "[CardScanner][Benchmark]", progressRow(record));
      if (!record.isWarmup && record.matchCorrect) {
        ++correctCount;
      }
#endif

      records.push_back(std::move(record));
    }
  }

#if CARDSCANNER_BENCHMARK_VERBOSE
  const size_t measuredCount =
      imageIndex * static_cast<size_t>(benchmarkIterations);
  log(LOG_LEVEL::Info,
      "[CardScanner][Benchmark] === run complete:", records.size(),
      "records,", correctCount, "/", measuredCount,
      "measured scans correct ===");
#endif

  BenchmarkRunResult result;
  result.recordCount = records.size();
  result.json = toJson(records);
  return result;
}

} // namespace benchmark
} // namespace cardscanner

