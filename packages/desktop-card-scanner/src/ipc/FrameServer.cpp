#include "FrameServer.h"

#include <ipc/Socket.h>
#include <Log.h>
#include <utils/PixelConvert.h>

#include <array>
#include <chrono>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>

namespace cardscanner {
namespace ipc {

namespace {

/// The wire format's numbers are its own, so the mapping is explicit. Unknown,
/// and any number no enumerator has, land on utils::PixelFormat::Unknown.
utils::PixelFormat toCoreFormat(PixelFormat format) {
  switch (format) {
  case PixelFormat::BGRA:
    return utils::PixelFormat::BGRA;
  case PixelFormat::RGBA:
    return utils::PixelFormat::RGBA;
  case PixelFormat::NV12:
    return utils::PixelFormat::NV12;
  case PixelFormat::I420:
    return utils::PixelFormat::I420;
  case PixelFormat::UYVY:
    return utils::PixelFormat::UYVY;
  case PixelFormat::YUY2:
    return utils::PixelFormat::YUY2;
  case PixelFormat::Unknown:
    break;
  }
  return utils::PixelFormat::Unknown;
}

} // namespace

void validateHeader(const FrameHeader &h, size_t payloadSize) {
  if (h.width == 0 || h.height == 0) {
    throw std::runtime_error("frame: zero-sized");
  }

  const auto format = toCoreFormat(static_cast<PixelFormat>(h.format));
  const uint32_t expectedPlanes = utils::planesFor(format);
  const uint32_t bpp = utils::lumaBytesPerPixel(format);
  if (expectedPlanes == 0 || bpp == 0) {
    throw std::runtime_error("frame: unsupported pixel format " +
                             std::to_string(h.format));
  }
  if (h.planeCount != expectedPlanes) {
    throw std::runtime_error("frame: format expects " +
                             std::to_string(expectedPlanes) + " planes, header says " +
                             std::to_string(h.planeCount));
  }

  // Writes are bounded by rows[i] and reads by linesize[i], while the
  // destination is sized from width/height, so all three must agree.
  size_t declared = 0;
  for (uint32_t plane = 0; plane < h.planeCount; plane++) {
    const uint32_t expectedRows = (plane == 0) ? h.height : h.height / 2;
    const uint32_t expectedRowBytes =
        (plane == 0) ? h.width * bpp
                     : (format == utils::PixelFormat::NV12 ? h.width : h.width / 2);

    if (h.rows[plane] != expectedRows) {
      throw std::runtime_error("frame: plane " + std::to_string(plane) + " has " +
                               std::to_string(h.rows[plane]) + " rows, expected " +
                               std::to_string(expectedRows));
    }
    if (h.linesize[plane] < expectedRowBytes) {
      throw std::runtime_error("frame: plane " + std::to_string(plane) +
                               " linesize " + std::to_string(h.linesize[plane]) +
                               " is below the " + std::to_string(expectedRowBytes) +
                               " bytes a row needs");
    }
    declared += size_t(h.linesize[plane]) * h.rows[plane];
  }

  if (declared != payloadSize) {
    throw std::runtime_error("frame: planes describe " + std::to_string(declared) +
                             " bytes but the payload is " +
                             std::to_string(payloadSize));
  }
}

namespace {

/// Internal: not declared in the header; the applied region reaches callers
/// through frameToRgb's out-parameter.
cv::Rect resolveRoi(const FrameHeader &header) {
  cv::Rect roi(int(header.roiX * float(header.width)),
               int(header.roiY * float(header.height)),
               int(header.roiWidth * float(header.width)),
               int(header.roiHeight * float(header.height)));
  roi &= cv::Rect(0, 0, int(header.width), int(header.height));

  if (roi.width < 16 || roi.height < 16) {
    roi = cv::Rect(0, 0, int(header.width), int(header.height));
  }
  // An odd origin or extent has no subsampled representation. Snap outward.
  roi.x &= ~1;
  roi.y &= ~1;
  roi.width = (roi.width + 1) & ~1;
  roi.height = (roi.height + 1) & ~1;
  roi &= cv::Rect(0, 0, int(header.width) & ~1, int(header.height) & ~1);
  return roi;
}

} // namespace

cv::Mat frameToRgb(const FrameHeader &header, const uint8_t *payload, cv::Rect *appliedRoi) {
  // Cropping in the source colour space keeps the conversion proportional to
  // the region rather than the frame.
  const cv::Rect roi = resolveRoi(header);
  if (appliedRoi != nullptr) {
    *appliedRoi = roi;
  }

  // One contiguous payload, so plane i starts where the ones before it end.
  std::array<utils::PlaneView, kMaxPlanes> planes{};
  size_t offset = 0;
  for (uint32_t plane = 0; plane < header.planeCount; plane++) {
    const size_t bytes = size_t(header.linesize[plane]) * header.rows[plane];
    planes[plane] = utils::PlaneView{payload + offset, header.linesize[plane]};
    offset += bytes;
  }

  return utils::toRgb(toCoreFormat(static_cast<PixelFormat>(header.format)),
                      {planes.data(), header.planeCount}, int(header.width),
                      int(header.height), roi);
}

FrameServer::FrameServer(uint16_t port, uint64_t token, FrameCallback callback)
    : port_(port), token_(token), callback_(std::move(callback)) {}

FrameServer::~FrameServer() { stop(); }

void FrameServer::start() {
  net::startup();
  listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ == net::kInvalidHandle) {
    throw std::runtime_error("frame server: socket() failed");
  }

  int yes = 1;
#ifdef _WIN32
  ::setsockopt(listenFd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&yes), sizeof(yes));
#else
  ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&yes), sizeof(yes));
#endif

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  // Loopback only. This carries raw frames of whatever the user has on camera.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    net::closeHandle(listenFd_);
    listenFd_ = net::kInvalidHandle;
    throw std::runtime_error("frame server: cannot bind port " + std::to_string(port_) +
                             " (already running?)");
  }
  if (::listen(listenFd_, 1) < 0) {
    net::closeHandle(listenFd_);
    listenFd_ = net::kInvalidHandle;
    throw std::runtime_error("frame server: listen() failed");
  }

  running_ = true;
  // The descriptor goes by value: stop() writes listenFd_ from another
  // thread, and the loop must not read the member concurrently.
  thread_ = std::thread([this, listenFd = listenFd_] { acceptLoop(listenFd); });
}

void FrameServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (listenFd_ != net::kInvalidHandle) {
    // Shutting the listener down unblocks accept().
    net::shutdownBoth(listenFd_);
    net::closeHandle(listenFd_);
    listenFd_ = net::kInvalidHandle;
  }
  // Wakes the accept thread out of recv(); under sendMutex_ so the number
  // cannot be closed and recycled between the load and the shutdown.
  {
    std::lock_guard<std::mutex> lock(sendMutex_);
    const net::Handle client = clientFd_;
    if (client != net::kInvalidHandle) {
      net::shutdownBoth(client);
    }
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void FrameServer::sendResult(uint64_t sequence, const cv::Rect2f &roi, bool detected,
                             bool accepted, float boxX, float boxY, float boxW,
                             float boxH, float confidence, float topScore) {
  ResultHeader header{};
  header.magic = kResultMagic;
  header.version = kFrameVersion;
  header.flags = uint16_t((detected ? kResultDetected : 0) |
                          (accepted ? kResultAccepted : 0));
  header.sequence = sequence;

  // Crop-relative box mapped onto the full frame.
  header.boxX = roi.x + boxX * roi.width;
  header.boxY = roi.y + boxY * roi.height;
  header.boxWidth = boxW * roi.width;
  header.boxHeight = boxH * roi.height;
  header.detectionConfidence = confidence;
  header.topScore = topScore;

  // Paired with acceptLoop's locked clear+close, so the send cannot hit a
  // descriptor the OS has already recycled.
  std::lock_guard<std::mutex> lock(sendMutex_);
  const net::Handle socketFd = clientFd_;
  if (socketFd == net::kInvalidHandle) {
    return;
  }
  // A failed send leaves a partial record on the wire; drop the client so
  // both sides resync over a fresh connection.
  if (!net::sendFully(socketFd, std::as_bytes(std::span{&header, 1}))) {
    net::shutdownBoth(socketFd);
  }
}

void FrameServer::acceptLoop(net::Handle listenFd) {
  while (running_) {
    const net::Handle client = ::accept(listenFd, nullptr, nullptr);
    if (client == net::kInvalidHandle) {
      if (!running_) {
        return;
      }
      // A failure that persists, such as running out of descriptors, must
      // not spin this thread.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    // No Nagle delay, and no SIGPIPE on a write to a closed peer.
    net::configureConnected(client);
    // Bounds how long sendResult can hold sendMutex_, which stop() needs to
    // wake this thread out of recv(). Frame sends must stay unbounded.
    net::setSendTimeout(client, 200);
    // Published before the first read, under the lock stop() takes, so stop()
    // can always wake this thread out of recv() with a shutdown.
    {
      std::lock_guard<std::mutex> lock(sendMutex_);
      clientFd_ = client;
    }

    log(LOG_LEVEL::Info, "[Frame]", "client connected");
    serveClient(client);
    {
      std::lock_guard<std::mutex> lock(sendMutex_);
      clientFd_ = net::kInvalidHandle;
    }
    net::closeHandle(client);
    log(LOG_LEVEL::Info, "[Frame]", "client disconnected");
  }
}

void FrameServer::serveClient(net::Handle clientFd) {
  std::vector<uint8_t> payload;

  while (running_) {
    FrameHeader header{};
    if (!net::receiveExactly(clientFd,
                             std::as_writable_bytes(std::span{&header, 1}))) {
      return;
    }

    if (header.magic != kFrameMagic || header.version != kFrameVersion) {
      log(LOG_LEVEL::Error, "[Frame]", "bad magic/version, dropping client");
      return;
    }
    if (header.token != token_) {
      // Loopback is not an authorisation boundary; any local process can
      // connect. Without this, one could inject frames or read nothing useful
      // but still disrupt the scanner.
      log(LOG_LEVEL::Error, "[Frame]", "bad token, dropping client");
      return;
    }
    if (header.planeCount == 0 || header.planeCount > kMaxPlanes) {
      log(LOG_LEVEL::Error, "[Frame]", "bad plane count");
      return;
    }
    // Bound the allocation: a corrupt length must not be a memory bomb.
    if (header.payloadBytes == 0 || header.payloadBytes > 64u * 1024 * 1024) {
      log(LOG_LEVEL::Error, "[Frame]", "implausible payload size",
          header.payloadBytes);
      return;
    }

    payload.resize(header.payloadBytes);
    if (!net::receiveExactly(clientFd,
                             std::as_writable_bytes(std::span{payload}))) {
      return;
    }

    // A header that does not describe its own bytes means the peer cannot be
    // trusted, so the connection goes rather than just this frame.
    try {
      validateHeader(header, payload.size());
    } catch (const std::exception &e) {
      log(LOG_LEVEL::Error, "[Frame]", "rejecting client:", e.what());
      return;
    }

    try {
      cv::Rect applied;
      cv::Mat rgb = frameToRgb(header, payload.data(), &applied);
      scanned_++;
      if (callback_) {
        // The region actually scanned travels with the frame, so its result
        // maps the box through that region and not a later frame's.
        const cv::Rect2f roi(float(applied.x) / float(header.width),
                             float(applied.y) / float(header.height),
                             float(applied.width) / float(header.width),
                             float(applied.height) / float(header.height));
        callback_(std::move(rgb), header.sequence, roi,
                  (header.flags & kFrameReportTimings) != 0);
      }
    } catch (const std::exception &e) {
      // A bad frame behind a valid header; keep the connection.
      log(LOG_LEVEL::Error, "[Frame]", e.what());
      rejected_++;
    }
  }
}

} // namespace ipc
} // namespace cardscanner
