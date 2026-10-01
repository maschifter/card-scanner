#pragma once

#include <types/ScanResults.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cardscanner {
namespace desktop {

enum class Status { Idle, Detecting, CandidateReady, Emitted };

/// Auto commits a confirmed card straight to the stream; Manual holds it at
/// CandidateReady until the dock says otherwise.
enum class Mode { Auto, Manual };

const char *toString(Status status);
const char *toString(Mode mode);
bool parseMode(const std::string &text, Mode &out);

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
  int emittedTimeoutMs = 1500;
};

struct CardInfo {
  std::string cardId;
  std::string gameName;
  float score = 0.0f;
  int detections = 0;
};

/**
 * @brief Turns per-frame scan results into stream-facing state.
 *
 * Server-side rather than in the overlay, so browser clients share one truth
 * that survives a reload.
 */
class ScanSession {
public:
  explicit ScanSession(SessionConfig config = {});

  /// Feeds one frame's results. Returns true when observable state changed.
  bool onScanResult(const ScanResult &result);

  /// Applies the grace period and emitted timeout. Call periodically; returns
  /// true when observable state changed.
  bool tick();

  Status status() const;
  Mode mode() const;
  std::optional<CardInfo> candidate() const;
  std::optional<CardInfo> emitted() const;
  std::vector<CardInfo> history() const;

  void setConfig(SessionConfig config);
  SessionConfig config() const;

  /// @return true when observable state changed, so the caller re-broadcasts.
  bool setMode(Mode mode);
  /// Commits the waiting candidate. Manual mode only; no-op otherwise.
  bool emitCurrent();
  bool clearEmitted();
  /// Puts a card from the history back on stream.
  bool emitFromHistory(const std::string &cardId);

private:
  using Clock = std::chrono::steady_clock;

  bool commitLocked();
  void recordHistoryLocked(const CardInfo &card);

  mutable std::mutex mutex_;
  SessionConfig config_;

  Status status_ = Status::Idle;
  Mode mode_ = Mode::Auto;
  std::optional<CardInfo> candidate_;
  std::optional<CardInfo> emitted_;
  /// Most recent first. Bounded so a long session cannot grow without limit.
  std::vector<CardInfo> history_;
  static constexpr size_t kMaxHistory = 50;

  /// Carried across the streak so the raised bar persists once observed.
  bool ambiguous_ = false;
  Clock::time_point lastSeen_{};
  Clock::time_point emittedAt_{};
};

} // namespace desktop
} // namespace cardscanner
