#pragma once

#include "ScanSession.h"

#include <types/ScannerConfig.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <opencv2/core.hpp>
#include <optional>
#include <thread>

namespace cardscanner {
namespace desktop {

/**
 * @brief Owns the models, the worker thread and the scan session.
 *
 * Depth-1 keep-latest mailbox, never a queue: a newer frame evicts the older
 * one and the producer never waits. Knows nothing about OBS or sockets.
 */
/// What the scanner saw last frame, matched or not: "nothing on the overlay"
/// is otherwise ambiguous between no detection, no match and a rejected match.
struct Diagnostics {
  bool hasFrame = false;
  int detections = 0;
  float detectionConfidence = 0.0f;
  std::string predictedGame;
  float predictedGameConfidence = 0.0f;
  /// Top score even when below the accept threshold.
  float topScore = 0.0f;
  std::string topCardId;
  bool accepted = false;
  /// Box within the cropped region, as fractions of it.
  float boxX = 0.0f, boxY = 0.0f, boxW = 0.0f, boxH = 0.0f;
  double processingMs = 0.0;
};

class ScannerService {
public:
  /// Fires on the worker thread after every processed frame. Keep it short;
  /// the worker blocks for its duration.
  using Listener = std::function<void()>;

  ScannerService(const ScannerConfig &config, SessionConfig sessionConfig);
  ~ScannerService();

  ScannerService(const ScannerService &) = delete;
  ScannerService &operator=(const ScannerService &) = delete;

  /// Offers a frame. Never blocks. False when dropped for a newer one.
  bool submitFrame(cv::Mat frame);

  /// Stops the worker and joins it. Idempotent. Callers owning objects the
  /// listener captures must call this before those objects unwind.
  void stop();

  void setListener(Listener listener);
  ScanSession &session() { return session_; }

  /// Snapshot of what the last frame contained, matched or not.
  Diagnostics diagnostics() const;

private:
  void workerLoop();

  ScanSession session_;
  Diagnostics diagnostics_;
  mutable std::mutex diagnosticsMutex_;
  Listener listener_;
  std::mutex listenerMutex_;

  std::mutex mutex_;
  std::condition_variable pending_;
  std::optional<cv::Mat> mailbox_;
  std::atomic<bool> shutdown_{false};
  std::thread worker_;
};

} // namespace desktop
} // namespace cardscanner
