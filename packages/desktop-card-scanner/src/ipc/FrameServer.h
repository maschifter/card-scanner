#pragma once

#include "FrameProtocol.h"
#include "Socket.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <opencv2/core.hpp>
#include <mutex>
#include <thread>

namespace cardscanner {
namespace ipc {

/**
 * @brief Accepts frames from the OBS module over loopback TCP.
 *
 * TCP rather than shared memory: identical on both platforms, and nothing to
 * clean up after a crash.
 *
 * One client at a time: a second connection waits in the listen backlog,
 * unserved, until the first disconnects.
 */
class FrameServer {
public:
  /// Receives the ROI converted to RGB, the frame's sequence, the region scanned as
  /// full-frame fractions, and its "report timings" switch. Runs on the accept thread.
  using FrameCallback = std::function<void(cv::Mat rgb, uint64_t sequence, cv::Rect2f roi,
                                           bool reportTimings)>;

  FrameServer(uint16_t port, uint64_t token, FrameCallback callback);
  ~FrameServer();

  FrameServer(const FrameServer &) = delete;
  FrameServer &operator=(const FrameServer &) = delete;

  /// @throws std::runtime_error if the port cannot be bound.
  void start();
  void stop();

  /// Sends a result back to the module. The box arrives as fractions of roi, the
  /// region its frame's callback delivered, and is mapped onto the full frame here.
  void sendResult(uint64_t sequence, const cv::Rect2f &roi, bool detected, bool accepted,
                  float boxX, float boxY, float boxW, float boxH, float confidence,
                  float topScore);

  uint64_t framesScanned() const { return scanned_.load(); }

  /// Frames that arrived complete but could not be converted for scanning.
  /// Protocol-level rejections are excluded; the log names those.
  uint64_t framesRejected() const { return rejected_.load(); }

private:
  void acceptLoop(net::Handle listenFd);
  void serveClient(net::Handle clientFd);

  uint16_t port_;
  uint64_t token_;
  FrameCallback callback_;

  net::Handle listenFd_ = net::kInvalidHandle;
  net::Handle clientFd_ = net::kInvalidHandle; // guarded by sendMutex_
  std::mutex sendMutex_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> scanned_{0};
  std::atomic<uint64_t> rejected_{0};
  std::thread thread_;
};

/**
 * @brief Checks a header describes a frame that fits its own payload.
 * @throws std::runtime_error if the fields disagree.
 */
void validateHeader(const FrameHeader &header, size_t payloadSize);

/**
 * @brief Converts a received frame to RGB, cropped to the ROI.
 *
 * Call validateHeader first; this trusts the header completely.
 *
 * @param appliedRoi receives the region actually used, which differs from the
 *        request when it was degenerate or snapped to even bounds.
 * @throws std::runtime_error on an unsupported format.
 */
cv::Mat frameToRgb(const FrameHeader &header, const uint8_t *payload,
                   cv::Rect *appliedRoi = nullptr);

} // namespace ipc
} // namespace cardscanner
