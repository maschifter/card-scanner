#include "FrameClient.h"

#include <ipc/Socket.h>

#include <cstring>
#include <span>

namespace cardscanner {
namespace obsbridge {

FrameClient::FrameClient(std::string host, uint16_t port, uint64_t token)
    : host_(std::move(host)), port_(port), token_(token) {}

FrameClient::~FrameClient() { stop(); }

void FrameClient::start() {
  if (running_.exchange(true)) {
    return;
  }
  reconnect_.rearm();
  writer_ = std::thread([this] { writerLoop(); });
  try {
    reader_ = std::thread([this] { readerLoop(); });
  } catch (...) {
    // Joins the writer: destroying a joinable std::thread calls terminate.
    stop();
    throw;
  }
}

std::shared_ptr<FrameClient::Connection> FrameClient::connection() const {
  std::lock_guard<std::mutex> lock(connectionMutex_);
  return connection_;
}

void FrameClient::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  pending_.notify_all();
  reconnect_.wake();
  {
    // Taken so the reader is either waiting or sees running_ false.
    std::lock_guard<std::mutex> lock(connectionMutex_);
  }
  connectionReady_.notify_all();

  // Unblocks reader/writer before the joins - this runs on an OBS UI thread.
  if (const auto conn = connection()) {
    ipc::net::shutdownBoth(conn->fd);
  }
  if (writer_.joinable()) {
    writer_.join();
  }
  // The writer may have dialled anew after the shutdown above; it is joined
  // now, so no new descriptor can appear and this shutdown is final.
  if (const auto conn = connection()) {
    ipc::net::shutdownBoth(conn->fd);
  }
  if (reader_.joinable()) {
    reader_.join();
  }
  // Both threads are gone, so this is the last reference and the close.
  std::lock_guard<std::mutex> lock(connectionMutex_);
  connection_.reset();
  connected_ = false;
}

bool FrameClient::submit(const ipc::FrameHeader &header, const uint8_t *const *planes,
                         const uint32_t *linesize, const uint32_t *rows) {
  if (!running_ || !connected_) {
    dropped_++;
    return false;
  }

  size_t payloadBytes = 0;
  for (uint32_t i = 0; i < header.planeCount; i++) {
    payloadBytes += size_t(linesize[i]) * rows[i];
  }

  // Assembled outside the lock so a stalled socket cannot hold up OBS.
  scratch_.resize(sizeof(ipc::FrameHeader) + payloadBytes);

  ipc::FrameHeader out = header;
  out.magic = ipc::kFrameMagic;
  out.version = ipc::kFrameVersion;
  out.token = token_;
  out.sequence = sequence_++;
  out.planeCount = header.planeCount;
  out.payloadBytes = uint32_t(payloadBytes);
  for (uint32_t i = 0; i < header.planeCount; i++) {
    out.linesize[i] = linesize[i];
    out.rows[i] = rows[i];
  }
  std::memcpy(scratch_.data(), &out, sizeof(out));

  uint8_t *dst = scratch_.data() + sizeof(out);
  for (uint32_t i = 0; i < header.planeCount; i++) {
    const size_t bytes = size_t(linesize[i]) * rows[i];
    std::memcpy(dst, planes[i], bytes);
    dst += bytes;
  }

  bool evicted;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    evicted = hasFrame_;
    // Keep-latest, depth one: a newer frame replaces an unsent older one.
    mailbox_.swap(scratch_);
    hasFrame_ = true;
  }
  pending_.notify_one();

  if (evicted) {
    dropped_++;
    return false;
  }
  return true;
}

bool FrameClient::latestDetection(float &x, float &y, float &w, float &h,
                                  bool &accepted) const {
  std::lock_guard<std::mutex> lock(resultMutex_);
  if (lastResultAt_.time_since_epoch().count() == 0) {
    return false;
  }
  // Stale results must not linger as a box over nothing.
  const auto age = std::chrono::steady_clock::now() - lastResultAt_;
  if (age > std::chrono::milliseconds(600)) {
    return false;
  }
  if ((lastResult_.flags & ipc::kResultDetected) == 0) {
    return false;
  }
  x = lastResult_.boxX;
  y = lastResult_.boxY;
  w = lastResult_.boxWidth;
  h = lastResult_.boxHeight;
  accepted = (lastResult_.flags & ipc::kResultAccepted) != 0;
  return true;
}

void FrameClient::readerLoop() {
  while (running_) {
    std::shared_ptr<Connection> conn;
    {
      // Sleeps until the writer has a live connection, or until stop().
      std::unique_lock<std::mutex> lock(connectionMutex_);
      connectionReady_.wait(lock, [this] {
        return !running_ || (connection_ && connection_->alive);
      });
      if (!running_) {
        break;
      }
      // A copy keeps the descriptor open for the whole recv(), whatever the
      // writer does to connection_ meanwhile.
      conn = connection_;
    }

    ipc::ResultHeader header{};
    if (!ipc::net::receiveExactly(conn->fd,
                                  std::as_writable_bytes(std::span{&header, 1}))) {
      conn->alive = false;
      continue;
    }
    if (header.magic != ipc::kResultMagic ||
        header.version != ipc::kFrameVersion) {
      // Desynced or foreign peer. Flag only: the writer closes and redials.
      conn->alive = false;
      continue;
    }

    std::lock_guard<std::mutex> lock(resultMutex_);
    lastResult_ = header;
    lastResultAt_ = std::chrono::steady_clock::now();
  }
}

std::shared_ptr<FrameClient::Connection> FrameClient::connectOnce() {
  ipc::net::startup();
  const ipc::net::Handle socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd == ipc::net::kInvalidHandle) {
    return nullptr;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

  if (::connect(socketFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ipc::net::closeHandle(socketFd);
    return nullptr;
  }

  // Includes suppressing SIGPIPE, which would otherwise take OBS down.
  ipc::net::configureConnected(socketFd);

  auto conn = std::make_shared<Connection>(socketFd);
  {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connection_ = conn;
  }
  connectionReady_.notify_all();
  connected_ = true;
  return conn;
}

void FrameClient::writerLoop() {
  // Outside the loop so the two buffers cycle between the threads; declared
  // inside, every submit() would reallocate on OBS's capture thread.
  std::vector<uint8_t> frame;

  while (running_) {
    auto conn = connection();
    if (!conn || !conn->alive) {
      connected_ = false;
      if (conn) {
        {
          std::lock_guard<std::mutex> lock(connectionMutex_);
          connection_.reset();
        }
        ipc::net::shutdownBoth(conn->fd);
        conn.reset();
      }
      conn = connectOnce();
      if (!conn) {
        // The server may not be up yet, or may have been restarted.
        reconnect_.sleep();
        continue;
      }
      reconnect_.reset();
    }

    frame.clear();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      pending_.wait_for(lock, std::chrono::milliseconds(200),
                        [this] { return !running_ || hasFrame_; });
      if (!running_) {
        break;
      }
      if (!hasFrame_) {
        continue;
      }
      frame.swap(mailbox_);
      hasFrame_ = false;
    }

    if (!ipc::net::sendFully(conn->fd, std::as_bytes(std::span{frame}))) {
      conn->alive = false;
      continue;
    }
    sent_++;
  }
}

} // namespace obsbridge
} // namespace cardscanner
