#pragma once

#include <cstddef>
#include <cstdint>
#include <opencv2/core.hpp>
#include <span>

namespace cardscanner {
namespace utils {

/// A frame's pixel layout, named by the layout rather than by the API that
/// reported it, so every wrapper maps its own type onto this one.
enum class PixelFormat {
  Unknown, ///< Not a layout this converts; toRgb rejects it.
  BGRA,    ///< 1 plane, 4 bytes per pixel
  RGBA,    ///< 1 plane, 4 bytes per pixel
  NV12,    ///< 2 planes: Y, interleaved UV at half resolution
  I420,    ///< 3 planes: Y, U, V at half resolution
  UYVY,    ///< 1 plane, 2 bytes per pixel
  YUY2,    ///< 1 plane, 2 bytes per pixel
};

/// Bytes per pixel in plane 0; 0 for Unknown.
uint32_t lumaBytesPerPixel(PixelFormat format);

/// Planes the layout carries; 0 for Unknown.
uint32_t planesFor(PixelFormat format);

struct PlaneView {
  const uint8_t *data;
  size_t linesize;
};

/// Converts the region roi of a frame to a packed RGB cv::Mat, cropping in the
/// source colour space. Checks the plane count and that roi lies inside
/// width x height; trusts the rest: an even origin and extent for subsampled
/// formats, and every plane laid out as its enumerator says, holding every row
/// roi reaches.
/// @throws std::runtime_error on a format this does not convert, a plane count
/// that does not match it, or a roi outside the frame.
cv::Mat toRgb(PixelFormat format, std::span<const PlaneView> planes, int width,
              int height, const cv::Rect &roi);

} // namespace utils
} // namespace cardscanner
