#pragma once

#include "../ipc/FrameProtocol.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cardscanner {
namespace obsbridge {

/**
 * @brief Ships frames from OBS to the scanner server.
 *
 * submit() never blocks: it copies into a depth-1 mailbox and returns, and a
 * writer thread owns the socket. Reconnects with backoff, so the server can be
 * killed and restarted underneath a running OBS.
 *
 * Depends only on the protocol header and the standard library - nothing heavy
 * enters OBS's address space.
 */
class FrameClient {
public:
  FrameClient(std::string host, uint16_t port, uint64_t token);
  ~FrameClient();

  FrameClient(const FrameClient &) = delete;
  FrameClient &operator=(const FrameClient &) = delete;

  void start();
  void stop();

  /**
   * @brief Offers one frame. Copies and returns immediately.
   * @param linesize bytes per row per plane; authoritative, buffers are padded
   * @return false when dropped - not connected, or a newer frame was waiting.
   */
  bool submit(const ipc::FrameHeader &header, const uint8_t *const *planes,
              const uint32_t *linesize, const uint32_t *rows);

  /// Most recent detection reported by the server, in full-frame fractions.
  /// Returns false when there is nothing fresh to draw.
  bool latestDetection(float &x, float &y, float &w, float &h, bool &accepted) const;

  bool connected() const { return connected_.load(); }
  uint64_t sent() const { return sent_.load(); }
  uint64_t dropped() const { return dropped_.load(); }

private:
  void writerLoop();
  void readerLoop();
  bool connectOnce();
  bool sendAll(const uint8_t *data, size_t bytes);

  std::string host_;
  uint16_t port_;
  uint64_t token_;

  /// Atomic because the writer owns reconnection, the reader polls it, and
  /// stop() closes it - three threads on one descriptor. A torn read here means
  /// recv() on a number the OS has already handed to someone else.
  std::atomic<int> socket_{-1};
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::atomic<uint64_t> sent_{0};
  std::atomic<uint64_t> dropped_{0};
  std::atomic<uint64_t> sequence_{0};

  std::mutex mutex_;
  std::condition_variable pending_;
  std::vector<uint8_t> mailbox_; ///< header + payload, ready to write
  bool hasFrame_ = false;
  std::vector<uint8_t> scratch_; ///< reused so submit() does not allocate

  std::thread writer_;
  std::thread reader_;

  // Guarded rather than atomic: the fields must be read as one consistent set.
  mutable std::mutex resultMutex_;
  ipc::ResultHeader lastResult_{};
  std::chrono::steady_clock::time_point lastResultAt_{};
};

} // namespace obsbridge
} // namespace cardscanner
