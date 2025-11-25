#include "FrameExtractor.h"
#include <algorithm>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

using namespace facebook;
using namespace cardscanner;

cv::Mat FrameExtractor::extractFrame(jsi::Runtime &runtime,
                                     const jsi::Object &frameObj) {

  // 1. Extract frame dimensions
  int width = 0;
  int height = 0;

  if (frameObj.hasProperty(runtime, "width")) {
    width = static_cast<int>(frameObj.getProperty(runtime, "width").asNumber());
  } else {
    throw jsi::JSError(runtime, "Frame object missing 'width' property");
  }

  if (frameObj.hasProperty(runtime, "height")) {
    height =
        static_cast<int>(frameObj.getProperty(runtime, "height").asNumber());
  } else {
    throw jsi::JSError(runtime, "Frame object missing 'height' property");
  }

  // 2. Get pixel format
  std::string pixelFormat = "unknown";
  if (frameObj.hasProperty(runtime, "pixelFormat")) {
    auto pixelFormatValue = frameObj.getProperty(runtime, "pixelFormat");
    if (pixelFormatValue.isString()) {
      pixelFormat = pixelFormatValue.asString(runtime).utf8(runtime);
    }
  } else {
    throw jsi::JSError(runtime, "Frame object missing 'pixelFormat' property");
  }

  // 3. Extract frame buffer via toArrayBuffer
  if (!frameObj.hasProperty(runtime, "toArrayBuffer")) {
    throw jsi::JSError(runtime, "Frame object missing 'toArrayBuffer' method");
  }

  cv::Mat frameImage;
  try {
    // Call toArrayBuffer() method
    auto toArrayBufferFunc =
        frameObj.getPropertyAsFunction(runtime, "toArrayBuffer");
    auto arrayBuffer =
        toArrayBufferFunc.call(runtime).asObject(runtime).getArrayBuffer(
            runtime);

    // Get buffer data
    uint8_t *data = arrayBuffer.data(runtime);

    if (pixelFormat == "rgb") {
      // Assume 4 bytes per pixel (RGBA or BGRA)
      cv::Mat sourceFrame(height, width, CV_8UC4, data);
#ifdef __ANDROID__
      // Android: Typically RGBA format -> Convert to BGR (required for
      // detection models)
      cv::cvtColor(sourceFrame, frameImage, cv::COLOR_RGBA2RGB);
#else
      // iOS: Typically BGRA format -> Convert to BGR
      cv::cvtColor(sourceFrame, frameImage, cv::COLOR_BGRA2RGB);
#endif
    } else if (pixelFormat == "yuv") {
      // Typically Android YUV420 format (NV21), 1.5 bytes per pixel
      cv::Mat yuvMat(height + height / 2, width, CV_8UC1, data);
      cv::cvtColor(yuvMat, frameImage, cv::COLOR_YUV2RGB_NV21);
    } else {
      throw std::runtime_error("Unsupported pixel format: " + pixelFormat);
    }

    // 4. Rotate the frame 90 degrees clockwise (as per original logic)
    if (!frameImage.empty()) {
      cv::rotate(frameImage, frameImage, cv::ROTATE_90_CLOCKWISE);
    } else {
      throw std::runtime_error("OpenCV Mat conversion resulted in an empty "
                               "image. Check dimensions and format.");
    }

  } catch (const jsi::JSError &e) {
    throw; // Re-throw JSI errors
  } catch (const std::exception &e) {
    throw jsi::JSError(runtime, std::string("Frame data processing failed: ") +
                                    e.what());
  }

  return frameImage;
}