#pragma once

#include <opencv2/core.hpp>

namespace cardscanner {
namespace utils {

/**
 * @enum FrameOrientation
 * @brief How far a captured frame is rotated from upright.
 *
 * Names the rotation, not the camera API that reported it, so every wrapper can
 * map its own type onto this one - VisionCamera's CameraOrientation on mobile,
 * whatever a desktop capture library reports.
 */
enum class FrameOrientation {
  Up,    ///< Already upright; no rotation needed.
  Left,  ///< Rotated left; correct with 90 degrees clockwise.
  Right, ///< Rotated right; correct with 90 degrees counter-clockwise.
  Down,  ///< Upside down; correct with 180 degrees.
};

/**
 * @brief Counter-rotates a frame upright: left -> 90 CW, right -> 90 CCW,
 * down -> 180, up -> returned unchanged.
 */
cv::Mat rotateFrameUpright(const cv::Mat &frame, FrameOrientation orientation);

/**
 * @brief Inverse of rotateFrameUpright for a box: maps it from upright space
 * back to raw buffer coordinates.
 *
 * @param rotatedSize Size of the upright frame the box was measured against.
 */
cv::Rect inverseRotateBox(const cv::Rect &box, FrameOrientation orientation,
                          const cv::Size &rotatedSize);

} // namespace utils
} // namespace cardscanner
