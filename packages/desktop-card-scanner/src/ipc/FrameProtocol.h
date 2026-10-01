#pragma once

// The frame wire format, shared by the OBS module and the server. Depends on
// <cstdint> alone, so the module links libobs and nothing else.
//
// Frames go over in whatever format OBS handed out, with the ROI as fractions;
// converting and cropping happen server-side where OpenCV lives.

#include <cstdint>

namespace cardscanner {
namespace ipc {

constexpr uint32_t kFrameMagic = 0x43534631; // "CSF1"
constexpr uint16_t kFrameVersion = 2;
constexpr uint16_t kDefaultFramePort = 27846;
constexpr uint16_t kDefaultControlPort = 27845;
constexpr uint16_t kDefaultOverlayPort = 27847;
constexpr int kMaxPlanes = 3;

/// Our own values, not OBS's, so this header stays free of libobs.
enum class PixelFormat : uint32_t {
  Unknown = 0,
  BGRA = 1, ///< 1 plane, 4 bytes per pixel
  RGBA = 2, ///< 1 plane, 4 bytes per pixel
  NV12 = 3, ///< 2 planes: Y, interleaved UV at half resolution
  I420 = 4, ///< 3 planes: Y, U, V at half resolution
  UYVY = 5, ///< 1 plane, 2 bytes per pixel
  YUY2 = 6, ///< 1 plane, 2 bytes per pixel
};

/// Both ends are little-endian; the magic catches it if that stops being true.
#pragma pack(push, 1)
struct FrameHeader {
  uint32_t magic; ///< kFrameMagic; a mismatch means drop the client.
  uint16_t version;
  uint16_t format;
  uint32_t width;
  uint32_t height;
  uint64_t sequence;

  /// Fractions of the full frame, so resolution never has to be agreed on.
  float roiX, roiY, roiWidth, roiHeight;

  uint32_t planeCount;
  uint32_t linesize[kMaxPlanes]; ///< authoritative; buffers are row-padded
  uint32_t rows[kMaxPlanes];
  uint32_t payloadBytes;         ///< total of linesize[i] * rows[i]

  /// Loopback alone does not stop another local process injecting frames.
  uint64_t token;

  /// Filter switches the scanner has to know about, per frame like the ROI.
  uint32_t flags;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) ==
                  4 + 2 + 2 + 4 + 4 + 8 + 16 + 4 + 12 + 12 + 4 + 8 + 4,
              "FrameHeader must stay packed; both sides memcpy it verbatim");

/// Measure the pipeline and report stage timings - the filter's "show
/// timings" box. Off, no clock is read at all.
constexpr uint32_t kFrameReportTimings = 1 << 0;

constexpr uint32_t kResultMagic = 0x43535231; // "CSR1"

/// Sent back on the same socket so the module can outline where the card was
/// found, not just the region it asked to be scanned.
#pragma pack(push, 1)
struct ResultHeader {
  uint32_t magic; ///< kResultMagic; a mismatch means drop the socket.
  uint16_t version;
  uint16_t flags; ///< bit 0: a card was detected. bit 1: the match was accepted.
  uint64_t sequence;
  /// Fractions of the FULL frame, already mapped out of the cropped region.
  float boxX, boxY, boxWidth, boxHeight;
  float detectionConfidence;
  float topScore;
};
#pragma pack(pop)

static_assert(sizeof(ResultHeader) == 4 + 2 + 2 + 8 + 16 + 4 + 4,
              "wire layout is a contract between two separately built "
              "binaries; the pack pragma must hold");

constexpr uint16_t kResultDetected = 1 << 0;
constexpr uint16_t kResultAccepted = 1 << 1;

} // namespace ipc
} // namespace cardscanner
