#ifndef FRAME_EXTRACTOR_H
#define FRAME_EXTRACTOR_H

#include "FrameTransform.h"
#include <VisionCamera/HybridFrameSpec.hpp>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <string>

namespace cardscanner {

/**
 * @brief Upright frame plus the orientation applied to it.
 */
struct ExtractedFrame {
  cv::Mat image;
  utils::FrameOrientation orientation;
};

/**
 * @brief Static class to encapsulate frame extraction and conversion logic
 * from a VisionCamera Frame into an OpenCV Mat.
 */
class FrameExtractor {
public:
  /**
   * @brief Extracts a VisionCamera v5 Frame into an upright RGB cv::Mat.
   * The result never aliases the Frame's buffer - dispose is safe on return.
   * @throws std::runtime_error if the frame is invalid or format unsupported.
   */
  static ExtractedFrame
  extractFrame(const std::shared_ptr<margelo::nitro::camera::HybridFrameSpec>
                   &frame);
};

} // namespace cardscanner

#endif // FRAME_EXTRACTOR_H
