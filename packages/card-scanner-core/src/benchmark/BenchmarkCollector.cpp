#include "BenchmarkCollector.h"

// Everything below is replaced by inline no-ops in the header when the
// benchmark build flag is off, so this whole file compiles to nothing.
#if CARDSCANNER_BENCHMARK

#include <array>

// g - global, k - runtime constant

namespace cardscanner {
namespace benchmark {
namespace {

constexpr size_t kStageSlots = static_cast<size_t>(Stage::_Count);
constexpr size_t kMetricSlots = static_cast<size_t>(Metric::_Count);
constexpr size_t kLabelSlots = static_cast<size_t>(Label::_Count);

// Per-thread: a live scan runs the same instrumented pipeline and must not
// write into the record a benchmark is building on its own thread.
thread_local bool g_isBenchmarkRecording = false;
thread_local std::array<double, kStageSlots> g_stageTotalMs{};
thread_local std::array<bool, kStageSlots> g_stageDidRun{};
thread_local std::array<double, kMetricSlots> g_metricValues{};
thread_local std::array<std::string, kLabelSlots> g_labelValues{};

} // namespace

void BenchmarkCollector::reset() {
  g_isBenchmarkRecording = false;
  g_stageTotalMs.fill(0.0);
  g_stageDidRun.fill(false);
  g_metricValues.fill(0.0);
  for (auto &label : g_labelValues) {
    label.clear();
  }
}

void BenchmarkCollector::beginBenchmarkRecord() {
  g_isBenchmarkRecording = true;
}

void BenchmarkCollector::endBenchmarkRecord() {
  g_isBenchmarkRecording = false;
}

bool BenchmarkCollector::isBenchmarkRecording() {
  return g_isBenchmarkRecording;
}

void BenchmarkCollector::addMs(Stage stage, double ms) {
  if (!g_isBenchmarkRecording) {
    return;
  }
  const auto index = static_cast<size_t>(stage);
  g_stageTotalMs[index] += ms;
  g_stageDidRun[index] = true;
}

void BenchmarkCollector::set(Metric metric, double value) {
  if (!g_isBenchmarkRecording) {
    return;
  }
  g_metricValues[static_cast<size_t>(metric)] = value;
}

void BenchmarkCollector::add(Metric metric, double value) {
  if (!g_isBenchmarkRecording) {
    return;
  }
  g_metricValues[static_cast<size_t>(metric)] += value;
}

void BenchmarkCollector::set(Label label, std::string value) {
  if (!g_isBenchmarkRecording) {
    return;
  }
  g_labelValues[static_cast<size_t>(label)] = std::move(value);
}

void BenchmarkCollector::appendStringWithSpace(Label label,
                                               const std::string &value) {
  if (!g_isBenchmarkRecording) {
    return;
  }
  auto &target = g_labelValues[static_cast<size_t>(label)];
  if (!target.empty()) {
    target += ' ';
  }
  target += value;
}

double BenchmarkCollector::getBenchmarkedMs(Stage stage) {
  return g_stageTotalMs[static_cast<size_t>(stage)];
}

bool BenchmarkCollector::getBenchmarkedRan(Stage stage) {
  return g_stageDidRun[static_cast<size_t>(stage)];
}

double BenchmarkCollector::getBenchmarkedValue(Metric metric) {
  return g_metricValues[static_cast<size_t>(metric)];
}

const std::string &BenchmarkCollector::getBenchmarkedLabel(Label label) {
  return g_labelValues[static_cast<size_t>(label)];
}

} // namespace benchmark
} // namespace cardscanner

#endif // CARDSCANNER_BENCHMARK
