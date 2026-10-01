#include "PixelConvert.h"

#include <cstring>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>

namespace cardscanner {
namespace utils {

namespace {

/// Copies a plane row by row. linesize is authoritative: camera buffers are
/// commonly padded, so width * bytesPerPixel is not.
void packPlane(const uint8_t *src, size_t linesize, uint32_t rows,
               uint32_t rowBytes, uint8_t *dst) {
  for (uint32_t y = 0; y < rows; y++) {
    std::memcpy(dst + size_t(y) * rowBytes, src + size_t(y) * linesize, rowBytes);
  }
}

/// Packs the ROI of a planar YUV frame contiguously: I420 has two half-width
/// chroma planes, NV12 one interleaved plane at full width.
cv::Mat packYuvRoi(PixelFormat format, std::span<const PlaneView> planes,
                   const cv::Rect &roi) {
  if (format != PixelFormat::NV12 && format != PixelFormat::I420) {
    throw std::runtime_error("not a planar YUV format");
  }
  const bool chromaHalfWidth = format == PixelFormat::I420;
  const int lumaRows = roi.height;
  const int chromaRows = roi.height / 2;
  cv::Mat yuv(lumaRows + chromaRows, roi.width, CV_8UC1);

  uint8_t *dst = yuv.data;

  const PlaneView &luma = planes[0];
  packPlane(luma.data + size_t(roi.y) * luma.linesize + size_t(roi.x),
            luma.linesize, uint32_t(lumaRows), uint32_t(roi.width), dst);
  dst += size_t(roi.width) * lumaRows;

  // Chroma is half resolution in both axes; the even origin and extent the
  // caller promises is what makes this divide exactly.
  for (size_t plane = 1; plane < planes.size(); plane++) {
    const PlaneView &chroma = planes[plane];
    const int rowBytes = chromaHalfWidth ? roi.width / 2 : roi.width;
    const int originX = chromaHalfWidth ? roi.x / 2 : roi.x;
    packPlane(chroma.data + size_t(roi.y / 2) * chroma.linesize + size_t(originX),
              chroma.linesize, uint32_t(chromaRows), uint32_t(rowBytes), dst);
    dst += size_t(rowBytes) * chromaRows;
  }
  return yuv;
}

} // namespace

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

cv::Mat toRgb(PixelFormat format, std::span<const PlaneView> planes, int width,
              int height, const cv::Rect &roi) {
  if (planes.size() != planesFor(format)) {
    throw std::runtime_error("format needs " + std::to_string(planesFor(format)) +
                             " planes, got " + std::to_string(planes.size()));
  }
  if ((roi & cv::Rect(0, 0, width, height)) != roi) {
    throw std::runtime_error("roi is not inside the frame");
  }

  cv::Mat rgb;
  switch (format) {
  case PixelFormat::BGRA:
  case PixelFormat::RGBA: {
    // Both the wrap and the narrowing are views; neither copies.
    const cv::Mat view(height, width, CV_8UC4,
                       const_cast<uint8_t *>(planes[0].data), planes[0].linesize);
    cv::cvtColor(view(roi), rgb,
                 format == PixelFormat::BGRA ? cv::COLOR_BGRA2RGB : cv::COLOR_RGBA2RGB);
    break;
  }
  case PixelFormat::UYVY:
  case PixelFormat::YUY2: {
    const cv::Mat view(height, width, CV_8UC2,
                       const_cast<uint8_t *>(planes[0].data), planes[0].linesize);
    cv::cvtColor(view(roi), rgb,
                 format == PixelFormat::UYVY ? cv::COLOR_YUV2RGB_UYVY
                                             : cv::COLOR_YUV2RGB_YUY2);
    break;
  }
  case PixelFormat::NV12:
  case PixelFormat::I420: {
    const cv::Mat yuv = packYuvRoi(format, planes, roi);
    cv::cvtColor(yuv, rgb,
                 format == PixelFormat::NV12 ? cv::COLOR_YUV2RGB_NV12
                                             : cv::COLOR_YUV2RGB_I420);
    break;
  }
  default:
    throw std::runtime_error("unsupported pixel format");
  }
  return rgb;
}

} // namespace utils
} // namespace cardscanner
