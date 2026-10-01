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
  /// Applied only once nothing has been accepted for the grace period: any card
  /// still on the table, even a different one, keeps the emitted one on stream.
  int emittedTimeoutMs = 1500;
};

struct CardInfo {
  std::string cardId;
  std::string gameName;
  float score = 0.0f;
  int detections = 0;
};

/// Best identified card in a frame (highest top-match score); null when
/// nothing matched. One shared rule, so outline and name never diverge.
const ProcessedCard *bestMatchedCard(const ScanResult &result);

/// bestMatchedCard, falling back to the most confident bare detection when
/// nothing matched. The fallback card has no matches; check before indexing.
const ProcessedCard *bestVisibleCard(const ScanResult &result);

/**
 * @brief Turns per-frame scan results into stream-facing state.
 *
 * Server-side rather than in the overlay, so browser clients share one truth
 * that survives a reload.
 */
class ScanSession {
public:
  /// Everything a broadcast needs, read under one lock so the fields cannot
  /// contradict each other mid-commit.
  struct Snapshot {
    Status status = Status::Idle;
    Mode mode = Mode::Auto;
    std::optional<CardInfo> candidate;
    std::optional<CardInfo> emitted;
    std::vector<CardInfo> history;
    SessionConfig config;
  };

  explicit ScanSession(SessionConfig config = {});

  /// Feeds one frame's results. Returns true when the identified card is
  /// the one now on stream - the wire's "accepted" bit.
  bool onScanResult(const ScanResult &result);

  /// Applies the grace period and emitted timeout. Call periodically; returns
  /// true when observable state changed.
  bool tick();

  Snapshot snapshot() const;

  void setConfig(SessionConfig config);
  /// Kept alongside snapshot(): set_settings wants the config alone for its
  /// read-modify-write, without paying for a history copy.
  SessionConfig config() const;

  /// The dock's mutators. No change reports: the control server re-broadcasts
  /// after every command, whether or not anything changed.
  void setMode(Mode mode);
  /// Commits the waiting candidate; no-op without one.
  void emitCurrent();
  void clearEmitted();
  /// Puts a card from the history back on stream.
  void emitFromHistory(const std::string &cardId);

private:
  using Clock = std::chrono::steady_clock;

  void commitLocked();
  void recordHistoryLocked(const CardInfo &card);
  /// Detections the candidate needs before it can be emitted.
  int requiredDetectionsLocked() const;

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
