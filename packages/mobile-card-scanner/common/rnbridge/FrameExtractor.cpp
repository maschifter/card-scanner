#include "FrameExtractor.h"
#include <opencv2/core.hpp>
#include <stdexcept>
#include <utils/PixelConvert.h>

using namespace cardscanner;

namespace nitrocamera = margelo::nitro::camera;

namespace {

/** @brief Non-owning view over the Frame's pixel buffer + conversion metadata.
 *  Valid only while the Frame (and its shared ArrayBuffer) is alive. */
struct BorrowedFrame {
  utils::PlaneView plane; // Points into the buffer without copying.
  std::shared_ptr<margelo::nitro::ArrayBuffer> buffer; // Keeps plane alive.
  utils::PixelFormat format;
  int width;
  int height;
  utils::FrameOrientation orientation;
};

BorrowedFrame
borrowFrame(const std::shared_ptr<nitrocamera::HybridFrameSpec> &frame) {
  if (frame == nullptr) {
    throw std::runtime_error("Frame is null");
  }

  if (!frame->getIsValid()) {
    throw std::runtime_error("Frame is no longer valid");
  }

  const int width = static_cast<int>(frame->getWidth());
  const int height = static_cast<int>(frame->getHeight());
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("Frame has invalid dimensions");
  }

  // The camera output requests pixelFormat 'rgb'; unlike v4, the channel order
  // is stated by the resolved format rather than guessed from the platform.
  const nitrocamera::PixelFormat pixelFormat = frame->getPixelFormat();
  utils::PixelFormat format;
  switch (pixelFormat) {
  case nitrocamera::PixelFormat::RGB_BGRA_8_BIT:
    format = utils::PixelFormat::BGRA;
    break;
  case nitrocamera::PixelFormat::RGB_RGBA_8_BIT:
    format = utils::PixelFormat::RGBA;
    break;
  default:
    throw std::runtime_error(
        "Unsupported pixel format: the frame output must be configured with "
        "pixelFormat 'rgb'");
  }

  if (!frame->getHasPixelBuffer()) {
    throw std::runtime_error("Frame has no CPU-accessible pixel buffer");
  }

  std::shared_ptr<margelo::nitro::ArrayBuffer> pixelBuffer =
      frame->getPixelBuffer();
  if (pixelBuffer == nullptr || pixelBuffer->data() == nullptr) {
    throw std::runtime_error("Frame pixel buffer is not accessible");
  }

  // Camera buffers are commonly row-padded, so the stride is authoritative -
  // deriving it from the width would misread every row after the first.
  const size_t bytesPerRow = static_cast<size_t>(frame->getBytesPerRow());
  if (bytesPerRow < static_cast<size_t>(width) * utils::lumaBytesPerPixel(format)) {
    throw std::runtime_error("Frame bytesPerRow is smaller than one row");
  }
  if (pixelBuffer->size() < bytesPerRow * static_cast<size_t>(height)) {
    throw std::runtime_error("Frame pixel buffer is smaller than expected");
  }

  const uint8_t *const pixels = pixelBuffer->data();

  return {utils::PlaneView{pixels, bytesPerRow},
          std::move(pixelBuffer), format, width, height,
          utils::toFrameOrientation(frame->getOrientation())};
}

} // namespace

ExtractedFrame FrameExtractor::extractFrame(
    const std::shared_ptr<nitrocamera::HybridFrameSpec> &frame) {
  BorrowedFrame borrowed = borrowFrame(frame);

  // The Frame can be disposed any time after this returns, so every path added
  // here (e.g. future YUV ones) must copy out - never return an aliasing Mat.
  const cv::Mat frameImage =
      utils::toRgb(borrowed.format, {&borrowed.plane, 1}, borrowed.width,
                   borrowed.height, cv::Rect(0, 0, borrowed.width, borrowed.height));

  if (frameImage.empty()) {
    throw std::runtime_error("OpenCV Mat conversion resulted in an empty "
                             "image. Check dimensions and format.");
  }

  return {utils::rotateFrameUpright(frameImage, borrowed.orientation),
          borrowed.orientation};
}
