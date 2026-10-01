#include "FrameServer.h"

#include "Socket.h"

#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <vector>

namespace cardscanner {
namespace ipc {

namespace {

/// Bytes per pixel in plane 0.
uint32_t lumaBytesPerPixel(PixelFormat format) {
  switch (format) {
  case PixelFormat::BGRA:
  case PixelFormat::RGBA:
    return 4;
  case PixelFormat::UYVY:
  case PixelFormat::YUY2:
    return 2;
  case PixelFormat::NV12:
  case PixelFormat::I420:
    return 1;
  default:
    return 0;
  }
}

uint32_t planesFor(PixelFormat format) {
  switch (format) {
  case PixelFormat::NV12:
    return 2;
  case PixelFormat::I420:
    return 3;
  case PixelFormat::BGRA:
  case PixelFormat::RGBA:
  case PixelFormat::UYVY:
  case PixelFormat::YUY2:
    return 1;
  default:
    return 0;
  }
}

} // namespace

void validateHeader(const FrameHeader &h, size_t payloadSize) {
  if (h.width == 0 || h.height == 0) {
    throw std::runtime_error("frame: zero-sized");
  }

  const auto format = static_cast<PixelFormat>(h.format);
  const uint32_t expectedPlanes = planesFor(format);
  const uint32_t bpp = lumaBytesPerPixel(format);
  if (expectedPlanes == 0 || bpp == 0) {
    throw std::runtime_error("frame: unsupported pixel format " +
                             std::to_string(h.format));
  }
  if (h.planeCount != expectedPlanes) {
    throw std::runtime_error("frame: format expects " +
                             std::to_string(expectedPlanes) + " planes, header says " +
                             std::to_string(h.planeCount));
  }

  // OpenCV's YUV converters require exactly height*3/2 rows, which an odd
  // height cannot produce.
  if ((format == PixelFormat::NV12 || format == PixelFormat::I420) &&
      (h.width % 2 != 0 || h.height % 2 != 0)) {
    throw std::runtime_error("frame: " + std::to_string(h.width) + "x" +
                             std::to_string(h.height) +
                             " has odd dimensions, which subsampled formats cannot use");
  }
  if ((format == PixelFormat::UYVY || format == PixelFormat::YUY2) && h.width % 2 != 0) {
    throw std::runtime_error("frame: odd width for a packed YUV format");
  }

  // Writes are bounded by rows[i] and reads by linesize[i], while the
  // destination is sized from width/height, so all three must agree.
  size_t declared = 0;
  for (uint32_t plane = 0; plane < h.planeCount; plane++) {
    const uint32_t expectedRows = (plane == 0) ? h.height : h.height / 2;
    const uint32_t expectedRowBytes =
        (plane == 0) ? h.width * bpp
                     : (format == PixelFormat::NV12 ? h.width : h.width / 2);

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

/// Copies a plane row by row. linesize is authoritative: camera buffers are
/// commonly padded, so width * bytesPerPixel is not.
void packPlane(const uint8_t *src, uint32_t linesize, uint32_t rows,
               uint32_t rowBytes, uint8_t *dst) {
  for (uint32_t y = 0; y < rows; y++) {
    std::memcpy(dst + size_t(y) * rowBytes, src + size_t(y) * linesize, rowBytes);
  }
}

/**
 * @brief Packs the ROI of a planar YUV frame contiguously.
 *
 * @param chromaHalfWidth I420 has two half-width chroma planes; NV12 has one
 *        interleaved plane at full width.
 */
cv::Mat packYuvRoi(const FrameHeader &h, const uint8_t *payload, const cv::Rect &roi,
                   bool chromaHalfWidth) {
  const int lumaRows = roi.height;
  const int chromaRows = roi.height / 2;
  cv::Mat yuv(lumaRows + chromaRows, roi.width, CV_8UC1);

  const uint8_t *src = payload;
  uint8_t *dst = yuv.data;

  packPlane(src + size_t(roi.y) * h.linesize[0] + size_t(roi.x), h.linesize[0],
            uint32_t(lumaRows), uint32_t(roi.width), dst);
  src += size_t(h.linesize[0]) * h.rows[0];
  dst += size_t(roi.width) * lumaRows;

  // Chroma is half resolution in both axes; resolveRoi guarantees even origin
  // and extent so this divides exactly.
  for (uint32_t plane = 1; plane < h.planeCount; plane++) {
    const int rowBytes = chromaHalfWidth ? roi.width / 2 : roi.width;
    const int originX = chromaHalfWidth ? roi.x / 2 : roi.x;
    packPlane(src + size_t(roi.y / 2) * h.linesize[plane] + size_t(originX),
              h.linesize[plane], uint32_t(chromaRows), uint32_t(rowBytes), dst);
    src += size_t(h.linesize[plane]) * h.rows[plane];
    dst += size_t(rowBytes) * chromaRows;
  }
  return yuv;
}

} // namespace

cv::Rect resolveRoi(const FrameHeader &header) {
  cv::Rect roi(int(header.roiX * float(header.width)),
               int(header.roiY * float(header.height)),
               int(header.roiWidth * float(header.width)),
               int(header.roiHeight * float(header.height)));
  roi &= cv::Rect(0, 0, int(header.width), int(header.height));

  if (roi.width < 16 || roi.height < 16) {
    return cv::Rect(0, 0, int(header.width), int(header.height));
  }
  // An odd origin or extent has no subsampled representation. Snap outward.
  roi.x &= ~1;
  roi.y &= ~1;
  roi.width = (roi.width + 1) & ~1;
  roi.height = (roi.height + 1) & ~1;
  roi &= cv::Rect(0, 0, int(header.width) & ~1, int(header.height) & ~1);
  return roi;
}

cv::Mat frameToRgb(const FrameHeader &header, const uint8_t *payload, cv::Rect *appliedRoi) {
  // Cropping in the source colour space keeps the conversion proportional to
  // the region rather than the frame.
  const cv::Rect roi = resolveRoi(header);
  if (appliedRoi != nullptr) {
    *appliedRoi = roi;
  }

  cv::Mat rgb;
  switch (static_cast<PixelFormat>(header.format)) {
  case PixelFormat::BGRA:
  case PixelFormat::RGBA: {
    // Both the wrap and the narrowing are views; neither copies.
    const cv::Mat view(int(header.height), int(header.width), CV_8UC4,
                       const_cast<uint8_t *>(payload), header.linesize[0]);
    cv::cvtColor(view(roi), rgb,
                 static_cast<PixelFormat>(header.format) == PixelFormat::BGRA
                     ? cv::COLOR_BGRA2RGB
                     : cv::COLOR_RGBA2RGB);
    break;
  }
  case PixelFormat::UYVY:
  case PixelFormat::YUY2: {
    const cv::Mat view(int(header.height), int(header.width), CV_8UC2,
                       const_cast<uint8_t *>(payload), header.linesize[0]);
    cv::cvtColor(view(roi), rgb,
                 static_cast<PixelFormat>(header.format) == PixelFormat::UYVY
                     ? cv::COLOR_YUV2RGB_UYVY
                     : cv::COLOR_YUV2RGB_YUY2);
    break;
  }
  case PixelFormat::NV12: {
    const cv::Mat yuv = packYuvRoi(header, payload, roi, /*chromaHalfWidth=*/false);
    cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB_NV12);
    break;
  }
  case PixelFormat::I420: {
    const cv::Mat yuv = packYuvRoi(header, payload, roi, /*chromaHalfWidth=*/true);
    cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB_I420);
    break;
  }
  default:
    throw std::runtime_error("frame: unsupported pixel format " +
                             std::to_string(header.format));
  }
  return rgb;
}

FrameServer::FrameServer(uint16_t port, uint64_t token, FrameCallback callback)
    : port_(port), token_(token), callback_(std::move(callback)) {}

FrameServer::~FrameServer() { stop(); }

void FrameServer::start() {
  net::startup();
  listenFd_ = int(::socket(AF_INET, SOCK_STREAM, 0));
  if (listenFd_ < 0) {
    throw std::runtime_error("frame server: socket() failed");
  }

  int yes = 1;
  ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&yes), sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  // Loopback only. This carries raw frames of whatever the user has on camera.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    net::closeHandle(listenFd_);
    listenFd_ = -1;
    throw std::runtime_error("frame server: cannot bind port " + std::to_string(port_) +
                             " (already running?)");
  }
  if (::listen(listenFd_, 1) < 0) {
    net::closeHandle(listenFd_);
    listenFd_ = -1;
    throw std::runtime_error("frame server: listen() failed");
  }

  running_ = true;
  thread_ = std::thread([this] { acceptLoop(); });
}

void FrameServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (listenFd_ >= 0) {
    // Shutting the listener down unblocks accept().
    net::shutdownBoth(listenFd_);
    net::closeHandle(listenFd_);
    listenFd_ = -1;
  }
  // The client too: running_ is only re-read between frames, so a connected
  // but silent peer would leave the accept thread parked in recv() forever.
  const int client = clientFd_.load();
  if (client >= 0) {
    net::shutdownBoth(client);
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void FrameServer::sendResult(uint64_t sequence, bool detected, bool accepted,
                             float boxX, float boxY, float boxW, float boxH,
                             float confidence, float topScore) {
  const int fd = clientFd_.load();
  if (fd < 0) {
    return;
  }

  ResultHeader header{};
  header.magic = kResultMagic;
  header.version = kFrameVersion;
  header.flags = uint16_t((detected ? kResultDetected : 0) |
                          (accepted ? kResultAccepted : 0));
  header.sequence = sequence;

  // Crop-relative box mapped onto the full frame.
  const float rx = roi_[0].load(), ry = roi_[1].load();
  const float rw = roi_[2].load(), rh = roi_[3].load();
  header.boxX = rx + boxX * rw;
  header.boxY = ry + boxY * rh;
  header.boxWidth = boxW * rw;
  header.boxHeight = boxH * rh;
  header.detectionConfidence = confidence;
  header.topScore = topScore;

  std::lock_guard<std::mutex> lock(sendMutex_);
  // Best effort; a departed module just means no outline this frame.
  // MSG_NOSIGNAL is the Linux half of the SO_NOSIGPIPE set at accept.
  net::sendAll(fd, &header, sizeof(header));
}

uint64_t FrameServer::lastSequence() const { return sequence_.load(); }

void FrameServer::acceptLoop() {
  while (running_) {
    const int client = int(::accept(listenFd_, nullptr, nullptr));
    if (client < 0) {
      if (running_) {
        continue;
      }
      return;
    }
    // No Nagle delay, and no SIGPIPE on a write to a closed peer.
    net::configureConnected(client);

    std::cerr << "frame client connected\n";
    clientFd_ = client;
    serveClient(client);
    clientFd_ = -1;
    net::closeHandle(client);
    std::cerr << "frame client disconnected\n";
  }
}

namespace {

/// TCP is a stream: a single recv() can return a partial header or payload.
bool readExactly(int fd, void *dst, size_t bytes) {
  auto *out = static_cast<uint8_t *>(dst);
  size_t got = 0;
  while (got < bytes) {
    const long n = net::receive(fd, out + got, bytes - got);
    if (n <= 0) {
      return false;
    }
    got += size_t(n);
  }
  return true;
}

} // namespace

void FrameServer::serveClient(int clientFd) {
  std::vector<uint8_t> payload;

  while (running_) {
    FrameHeader header{};
    if (!readExactly(clientFd, &header, sizeof(header))) {
      return;
    }

    if (header.magic != kFrameMagic || header.version != kFrameVersion) {
      std::cerr << "frame: bad magic/version, dropping client\n";
      return;
    }
    if (header.token != token_) {
      // Loopback is not an authorisation boundary; any local process can
      // connect. Without this, one could inject frames or read nothing useful
      // but still disrupt the scanner.
      std::cerr << "frame: bad token, dropping client\n";
      rejected_++;
      return;
    }
    if (header.planeCount == 0 || header.planeCount > kMaxPlanes) {
      std::cerr << "frame: bad plane count\n";
      return;
    }
    // Bound the allocation: a corrupt length must not be a memory bomb.
    if (header.payloadBytes == 0 || header.payloadBytes > 64u * 1024 * 1024) {
      std::cerr << "frame: implausible payload size " << header.payloadBytes << "\n";
      return;
    }

    sequence_ = header.sequence;

    payload.resize(header.payloadBytes);
    if (!readExactly(clientFd, payload.data(), payload.size())) {
      return;
    }

    // A header that does not describe its own bytes means the peer cannot be
    // trusted, so the connection goes rather than just this frame.
    try {
      validateHeader(header, payload.size());
    } catch (const std::exception &e) {
      std::cerr << "frame: rejecting client: " << e.what() << "\n";
      rejected_++;
      return;
    }

    try {
      cv::Rect applied;
      cv::Mat rgb = frameToRgb(header, payload.data(), &applied);
      // The region actually scanned, which sendResult maps boxes through.
      // It differs from the request when degenerate or snapped to even bounds.
      roi_[0] = float(applied.x) / float(header.width);
      roi_[1] = float(applied.y) / float(header.height);
      roi_[2] = float(applied.width) / float(header.width);
      roi_[3] = float(applied.height) / float(header.height);
      received_++;
      if (callback_) {
        callback_(std::move(rgb));
      }
    } catch (const std::exception &e) {
      // A bad frame behind a valid header; keep the connection.
      std::cerr << "frame: " << e.what() << "\n";
      rejected_++;
    }
  }
}

} // namespace ipc
} // namespace cardscanner
