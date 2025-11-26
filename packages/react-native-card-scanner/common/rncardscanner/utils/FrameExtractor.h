#ifndef FRAME_EXTRACTOR_H
#define FRAME_EXTRACTOR_H

#include <jsi/jsi.h>
#include <string>

// Forward declaration of the OpenCV Mat type to avoid including
// the full OpenCV headers in the interface header.
namespace cv {
class Mat;
}

namespace cardscanner {

/**
 * @brief Static class to encapsulate frame extraction and conversion logic
 * from a JSI HostObject into an OpenCV Mat.
 */
class FrameExtractor {
public:
  /**
   * @brief Extracts pixel data from a JSI Frame object, converts it to a
   * cv::Mat (BGR format), and rotates it 90 degrees clockwise.
   * * @param runtime The JSI Runtime.
   * @param frameObj The JSI Object representing the camera frame (must have
   * 'width', 'height', 'pixelFormat', and 'toArrayBuffer').
   * @return cv::Mat The processed OpenCV matrix (rotated, BGR format).
   * @throws jsi::JSError if mandatory properties or methods are missing, or on
   * conversion failure.
   */
  static cv::Mat extractFrame(facebook::jsi::Runtime &runtime,
                              const facebook::jsi::Object &frameObj);
};

} // namespace cardscanner

#endif // FRAME_EXTRACTOR_H