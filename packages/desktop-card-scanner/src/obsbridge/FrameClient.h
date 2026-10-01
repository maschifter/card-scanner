#pragma once

#include <ipc/FrameProtocol.h>
#include <ipc/Socket.h>
#include <util/Backoff.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
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
 * Depends only on the protocol and socket headers and the standard library -
 * nothing heavy enters OBS's address space.
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

  uint64_t sent() const { return sent_.load(); }
  uint64_t dropped() const { return dropped_.load(); }

private:
  struct Connection {
    explicit Connection(ipc::net::Handle fd) : fd(fd) {}
    ~Connection() { ipc::net::closeHandle(fd); }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    const ipc::net::Handle fd;
    std::atomic<bool> alive{true};
  };

  void writerLoop();
  void readerLoop();
  /// Null when the server is not answering.
  std::shared_ptr<Connection> connectOnce();
  std::shared_ptr<Connection> connection() const;

  std::string host_;
  uint16_t port_;
  uint64_t token_;

  /// Replaced by the writer alone; everyone else takes a copy under the lock.
  std::shared_ptr<Connection> connection_;
  mutable std::mutex connectionMutex_;
  /// Signalled when the writer connects and on stop(); the reader waits on it.
  std::condition_variable connectionReady_;
  /// The writer's reconnect delay; stop() wakes it.
  util::Backoff reconnect_{std::chrono::milliseconds(200),
                           std::chrono::milliseconds(3000)};
  std::atomic<bool> running_{false};
  /// The writer's view, so submit() can drop without taking a lock.
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
