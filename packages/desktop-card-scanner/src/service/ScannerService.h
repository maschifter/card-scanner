#pragma once

#include <core/ScanSession.h>
#include <types/ScannerConfig.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <opencv2/core.hpp>
#include <optional>
#include <string>
#include <thread>

namespace cardscanner {
namespace desktop {

/// One frame for the worker, with what its result has to carry back out.
struct Frame {
  cv::Mat image;
  uint64_t sequence = 0;
  /// The region the image was cut from, as fractions of the full frame.
  cv::Rect2f roi{0.0f, 0.0f, 1.0f, 1.0f};
};

/// Quiet time before Diagnostics::hasFrame drops: twice the filter's slowest
/// scan interval (1 fps), so that setting cannot flap it.
constexpr std::chrono::milliseconds kFeedTimeout{2000};

/// What the scanner saw last frame, matched or not: "nothing on the overlay"
/// is otherwise ambiguous between no detection, no match and a rejected match.
struct Diagnostics {
  /// False until the first frame, and again once the feed has been quiet for
  /// kFeedTimeout.
  bool hasFrame = false;
  /// Which frame this describes and the region it was scanned at, so the box
  /// maps out through that region and not a later frame's.
  uint64_t sequence = 0;
  cv::Rect2f roi{0.0f, 0.0f, 1.0f, 1.0f};
  int detections = 0;
  float detectionConfidence = 0.0f;
  std::string predictedGame;
  float predictedGameConfidence = 0.0f;
  /// Top score even when below the accept threshold.
  float topScore = 0.0f;
  std::string topCardId;
  bool accepted = false;
  /// Box within roi, as fractions of it.
  float boxX = 0.0f, boxY = 0.0f, boxW = 0.0f, boxH = 0.0f;
  double processingMs = 0.0;
  /// The OBS filter's "show timings" box, so a client can drop the readout
  /// when it is unticked rather than leave the last values on screen.
  bool timingsEnabled = false;
  /// Valid only when true; the zeros below are absence, not speed. They cover
  /// the first detection only, so they need not sum to processingMs.
  bool measured = false;
  double yoloMs = 0.0;
  double preprocMs = 0.0;
  double embedMs = 0.0;
  double dbSearchMs = 0.0;
};

/**
 * @brief Owns the worker thread and the scan session, and brings up the models.
 *
 * The models are ScannerRegistry's statics: the constructor initialises that
 * process-wide registry and the destructor releases it. One service per process.
 *
 * Depth-1 keep-latest mailbox, never a queue: a newer frame evicts the older
 * one and the producer never waits. Knows nothing about OBS or sockets.
 */
class ScannerService {
public:
  /// Fires on the worker thread after every processed frame. Keep it short;
  /// the worker blocks for its duration.
  using Listener = std::function<void()>;

  /// The listener is fixed for the service's lifetime; it may capture objects
  /// freely as long as they outlive this service - stop() joins the worker, so
  /// nothing fires after it returns.
  ScannerService(const ScannerConfig &config, core::SessionConfig sessionConfig,
                 Listener listener);
  ~ScannerService();

  ScannerService(const ScannerService &) = delete;
  ScannerService &operator=(const ScannerService &) = delete;

  /// Spawns the worker; call it exactly once. Separate from the constructor
  /// so the owner can finish wiring - and start its endpoints - before the
  /// listener can first fire.
  void start();

  /// Offers a frame. Never blocks. False when dropped for a newer one.
  bool submitFrame(Frame frame);

  /// Stops the worker and joins it. Idempotent. Callers owning objects the
  /// listener captures must call this before those objects unwind.
  void stop();

  core::ScanSession &session() { return session_; }

  /// Turns the stage timers on or off - the OBS filter's "show timings" box.
  /// Off, nothing is measured and Diagnostics::measured stays false.
  void setReportTimings(bool enabled);

  /// Snapshot of what the last frame contained, matched or not.
  Diagnostics diagnostics() const;

private:
  void workerLoop();

  core::ScanSession session_;
  Diagnostics diagnostics_;
  /// Written per frame by the frame thread, read per frame by the worker.
  std::atomic<bool> reportTimings_{false};
  mutable std::mutex diagnosticsMutex_;
  /// Immutable after construction, so the worker reads it without a lock.
  const Listener listener_;

  std::mutex mutex_;
  std::condition_variable pending_;
  std::optional<Frame> mailbox_;
  std::atomic<bool> shutdown_{false};
  std::thread worker_;
};

} // namespace desktop
} // namespace cardscanner
