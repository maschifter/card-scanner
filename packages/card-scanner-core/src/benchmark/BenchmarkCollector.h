#pragma once

#include <chrono>
#include <cstdint>
#include <string>

// Benchmark instrumentation is opt-in per app build. Left undefined or 0, every
// entry point below is an empty inline that the optimizer deletes outright, so
// a production scan pays nothing - not even a clock read.
#ifndef CARDSCANNER_BENCHMARK
#define CARDSCANNER_BENCHMARK 0
#endif

namespace cardscanner {
namespace benchmark {

/**
 * @enum Stage
 * @brief Contains stages of pipeline steps measured in time and FAB/MTG-specific stages.
 */
enum class Stage : uint8_t {
  YoloSegmentation,
  Preproc,
  Save,
  Embed,
  DbSearch,

  SetSymbolYolo,
  SetSymbolPreproc,
  SetSymbolEmbed,
  SetSymbolDbSearch,

  FabColorPreproc,
  FabColorClassify,

  _Count,
};

/**
 * @enum Metric
 * @brief Numeric benchmark values, such as counts and confidences.
 */
enum class Metric : uint8_t {
  YoloConfidence,
  DetectionCount,
  GamesSearched,
  RawTopScore,
  _Count,
};

/**
 * @enum Label
 * @brief Textual benchmark values, such as predicted games and card IDs.
 */
enum class Label : uint8_t {
  YoloPredictedGames,
  RawTopCardId,
  _Count,
};

/**
 * @class BenchmarkCollector
 * @brief Writes outside an open record are ignored - base for production NOOP.
 * Scoped instances measure their enclosing scope and add to a stage total.
 * State is per-thread, so concurrent scans record independently.
 */
class BenchmarkCollector {
public:
  BenchmarkCollector() = delete;
  BenchmarkCollector(const BenchmarkCollector &) = delete;
  BenchmarkCollector &operator=(const BenchmarkCollector &) = delete;

#if CARDSCANNER_BENCHMARK

  static void reset();
  static void beginBenchmarkRecord();
  static void endBenchmarkRecord();
  static bool isBenchmarkRecording();

  static void addMs(Stage stage, double ms);
  static void set(Metric metric, double value);
  static void add(Metric metric, double value);
  static void set(Label label, std::string value);

  /// Builds a list with spaces for benchmark purposes.
  static void appendStringWithSpace(Label label, const std::string &value);

  static double getBenchmarkedMs(Stage stage);
  static bool getBenchmarkedRan(Stage stage);

  static double getBenchmarkedValue(Metric metric);
  static const std::string &getBenchmarkedLabel(Label label);

  /**
   * @class ScopedTimer
   * @brief Records its enclosing scope into one stage using its destructor.
   */
  class ScopedTimer {
  public:
    explicit ScopedTimer(Stage stage)
        : stage_(stage), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
      const auto end = std::chrono::steady_clock::now();
      addMs(stage_,
            std::chrono::duration<double, std::milli>(end - start_).count());
    }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

  private:
    Stage stage_;
    std::chrono::steady_clock::time_point start_;
  };

#else

  static void reset() {}
  static void beginBenchmarkRecord() {}
  static void endBenchmarkRecord() {}
  static bool isBenchmarkRecording() { return false; }

  static void addMs(Stage, double) {}
  static void set(Metric, double) {}
  static void add(Metric, double) {}
  static void set(Label, std::string) {}

  static void appendStringWithSpace(Label, const std::string &) {}

  static double getBenchmarkedMs(Stage) { return 0.0; }
  static bool getBenchmarkedRan(Stage) { return false; }

  static double getBenchmarkedValue(Metric) { return 0.0; }
  static const std::string &getBenchmarkedLabel(Label) {
    static const std::string empty;
    return empty;
  }

  /// No clock is read, so an enclosing scope costs nothing to "measure".
  class ScopedTimer {
  public:
    explicit ScopedTimer(Stage) {}

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;
  };

#endif
};

} // namespace benchmark
} // namespace cardscanner
