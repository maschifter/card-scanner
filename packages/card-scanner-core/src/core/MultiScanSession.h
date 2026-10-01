#pragma once

#include "../types/Detection.h"
#include "../types/ScanResults.h"
#include "../types/ScannerConfig.h"
#include <functional>
#include <mutex>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace cardscanner {
namespace core {

/// The multi-card freeze: the layout check, its temporal window, and the
/// event stream a host listens to.
class MultiScanSession {
public:
  /// One card on the frozen frame, as known at begin().
  struct Slot {
    cv::Rect box;
    float detectionConfidence = 0.0f;
    std::vector<cv::Point2f> quad;
    std::string predictedGame;
  };

  struct Event {
    enum class Type { Started, CardResolved, Ended };
    Type type = Type::Started;
    /// Started: the frozen frame and every card as a placeholder. frameSize
    /// and total ride on every event, so a host needs no state.
    std::string frameImagePath;
    cv::Size frameSize;
    std::vector<Slot> slots;
    size_t total = 0;
    /// CardResolved: which slot, and the card.
    size_t index = 0;
    ProcessedCard card;
  };
  using Listener = std::function<void(const Event &)>;

  /// One frame's layout: enough cards, aligned, not overlapping, similar in
  /// size, large enough, and each sharp enough. Only cards at or above
  /// config.segmentationThreshold judge the page. A still image, with no
  /// window to hold, calls this directly.
  static MultiVerdict evaluateLayout(const std::vector<Detection> &detections,
                                     const cv::Mat &frameImage,
                                     const ScannerConfig &config);

  /// Replaces the listener; nullptr clears it.
  void setListener(Listener listener);

  /// evaluateLayout plus the temporal window: the layout must hold for
  /// config.multiStableFrames consecutive calls.
  MultiVerdict evaluate(const std::vector<Detection> &detections,
                        const cv::Mat &frameImage, const ScannerConfig &config);

  void begin(const std::string &frameImagePath, const cv::Size &frameSize,
             const std::vector<Detection> &detections);
  void resolved(size_t index, const ProcessedCard &card);
  void ended();

  /// Restarts the window. Keeps the listener.
  void reset();
  /// Bumps on every reset(), so a caller can tell whether a resume landed
  /// while it was still streaming.
  int generation() const;

private:
  void emit(Event event) const;

  mutable std::mutex mutex_;
  Listener listener_;
  int consecutiveQualifying_ = 0;
  int generation_ = 0;
  cv::Size frameSize_;
  size_t total_ = 0;
};

} // namespace core
} // namespace cardscanner
