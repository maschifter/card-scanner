#pragma once

#include <string>

namespace cardscanner {
namespace core {

/// Counts consecutive detections rather than milliseconds, so a dropped frame
/// does not advance a timer.
struct SessionConfig {
  /// The pipeline's threshold is set lower so near-misses still come back and
  /// can be reported.
  float acceptScore = 0.6f;
  int stableDetections = 2;
  /// A near-tie means the embedding cannot separate the two.
  int ambiguousDetections = 4;
  float ambiguityDelta = 0.01f;
  /// Survives frames with no detection, so one blurred frame does not reset
  /// the streak.
  int gracePeriodMs = 400;
  /// Applied only once nothing has been accepted for the grace period: any card
  /// still on the table, even a different one, keeps the emitted one on stream.
  int emittedTimeoutMs = 1500;
};

} // namespace core
} // namespace cardscanner
