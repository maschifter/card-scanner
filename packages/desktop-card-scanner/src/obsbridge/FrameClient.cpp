#include "FrameClient.h"

#include "../ipc/Socket.h"

#include <cstring>

namespace cardscanner {
namespace obsbridge {

FrameClient::FrameClient(std::string host, uint16_t port, uint64_t token)
    : host_(std::move(host)), port_(port), token_(token) {}

FrameClient::~FrameClient() { stop(); }

void FrameClient::start() {
  if (running_.exchange(true)) {
    return;
  }
  writer_ = std::thread([this] { writerLoop(); });
  reader_ = std::thread([this] { readerLoop(); });
}

void FrameClient::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  pending_.notify_all();

  // Before the joins, not after: the reader parks in recv() until the server
  // sends something, and this runs on an OBS UI thread.
  const int fd = socket_.load();
  if (fd >= 0) {
    ipc::net::shutdownBoth(fd);
  }

  if (writer_.joinable()) {
    writer_.join();
  }
  if (reader_.joinable()) {
    reader_.join();
  }
  const int last = socket_.exchange(-1);
  if (last >= 0) {
    ipc::net::closeHandle(last);
  }
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
    const int fd = socket_.load();
    if (fd < 0 || !connected_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    // A loop rather than MSG_WAITALL, which Winsock does not support here.
    ipc::ResultHeader header{};
    size_t got = 0;
    bool ok = true;
    while (got < sizeof(header) && running_) {
      const long n = ipc::net::receive(fd, reinterpret_cast<uint8_t *>(&header) + got,
                                       sizeof(header) - got);
      if (n <= 0) {
        ok = false;
        break;
      }
      got += size_t(n);
    }
    if (!ok || got != sizeof(header)) {
      // The writer owns reconnection.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    if (header.magic != ipc::kResultMagic) {
      continue;
    }

    std::lock_guard<std::mutex> lock(resultMutex_);
    lastResult_ = header;
    lastResultAt_ = std::chrono::steady_clock::now();
  }
}

bool FrameClient::connectOnce() {
  ipc::net::startup();
  const int fd = int(::socket(AF_INET, SOCK_STREAM, 0));
  if (fd < 0) {
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ipc::net::closeHandle(fd);
    return false;
  }

  // Includes suppressing SIGPIPE, which would otherwise take OBS down.
  ipc::net::configureConnected(fd);

  socket_ = fd;
  connected_ = true;
  return true;
}

bool FrameClient::sendAll(const uint8_t *data, size_t bytes) {
  size_t sent = 0;
  while (sent < bytes) {
    const int fd = socket_.load();
    if (fd < 0) {
      return false;
    }
    const long n = ipc::net::sendAll(fd, data + sent, bytes - sent);
    if (n <= 0) {
      return false;
    }
    sent += size_t(n);
  }
  return true;
}

void FrameClient::writerLoop() {
  int backoffMs = 200;
  // Outside the loop so the two buffers cycle between the threads. Declared
  // inside, it freed the real allocation each iteration and left scratch_ with
  // zero capacity, making every submit() reallocate on OBS's capture thread.
  std::vector<uint8_t> frame;

  while (running_) {
    if (!connected_) {
      if (connectOnce()) {
        backoffMs = 200;
      } else {
        // The server may not be up yet, or may have been restarted.
        for (int slept = 0; slept < backoffMs && running_; slept += 50) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        backoffMs = backoffMs < 3000 ? backoffMs * 2 : 3000;
        continue;
      }
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

    if (!sendAll(frame.data(), frame.size())) {
      connected_ = false;
      const int dead = socket_.exchange(-1);
      if (dead >= 0) {
        ipc::net::closeHandle(dead);
      }
      continue;
    }
    sent_++;
  }
}

} // namespace obsbridge
} // namespace cardscanner
