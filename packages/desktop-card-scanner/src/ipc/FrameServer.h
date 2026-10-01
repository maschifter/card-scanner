#pragma once

#include "FrameProtocol.h"

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
 * clean up after a crash. One client at a time.
 */
class FrameServer {
public:
  /// Receives the ROI, already converted to RGB. Runs on the accept thread.
  using FrameCallback = std::function<void(cv::Mat)>;

  FrameServer(uint16_t port, uint64_t token, FrameCallback callback);
  ~FrameServer();

  FrameServer(const FrameServer &) = delete;
  FrameServer &operator=(const FrameServer &) = delete;

  /// @throws std::runtime_error if the port cannot be bound.
  void start();
  void stop();

  /// Sends a result back to the module. The box arrives in crop fractions and
  /// is mapped onto the full frame here.
  void sendResult(uint64_t sequence, bool detected, bool accepted, float boxX,
                  float boxY, float boxW, float boxH, float confidence, float topScore);

  uint64_t lastSequence() const;
  uint64_t framesReceived() const { return received_.load(); }
  uint64_t framesRejected() const { return rejected_.load(); }

private:
  void acceptLoop();
  void serveClient(int clientFd);

  uint16_t port_;
  uint64_t token_;
  FrameCallback callback_;

  int listenFd_ = -1;
  std::atomic<int> clientFd_{-1};
  std::mutex sendMutex_;
  /// Region of the most recent frame, for mapping boxes back out.
  std::atomic<float> roi_[4]{{0.0f}, {0.0f}, {1.0f}, {1.0f}};
  std::atomic<uint64_t> sequence_{0};
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> received_{0};
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
