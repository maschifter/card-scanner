#include "FrameGeometry.h"
#include <algorithm>

#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace cardscanner {
namespace utils {

cv::Mat rotateFrameUpright(const cv::Mat &frame, FrameOrientation orientation) {
  cv::Mat rotated;
  switch (orientation) {
  case FrameOrientation::Up:
    return frame;
  case FrameOrientation::Left:
    cv::rotate(frame, rotated, cv::ROTATE_90_CLOCKWISE);
    break;
  case FrameOrientation::Right:
    cv::rotate(frame, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
    break;
  case FrameOrientation::Down:
    cv::rotate(frame, rotated, cv::ROTATE_180);
    break;
  }
  return rotated;
}

cv::Rect inverseRotateBox(const cv::Rect &box, FrameOrientation orientation,
                          const cv::Size &rotatedSize) {
  const cv::Point2f a = inverseRotatePoint(
      cv::Point2f(static_cast<float>(box.x), static_cast<float>(box.y)),
      orientation, rotatedSize);
  const cv::Point2f b =
      inverseRotatePoint(cv::Point2f(static_cast<float>(box.x + box.width),
                                     static_cast<float>(box.y + box.height)),
                         orientation, rotatedSize);
  return cv::Rect(cv::Point(static_cast<int>(std::min(a.x, b.x)),
                            static_cast<int>(std::min(a.y, b.y))),
                  cv::Point(static_cast<int>(std::max(a.x, b.x)),
                            static_cast<int>(std::max(a.y, b.y))));
}

cv::Point2f inverseRotatePoint(const cv::Point2f &p,
                               FrameOrientation orientation,
                               const cv::Size &rotatedSize) {
  const float w = static_cast<float>(rotatedSize.width);
  const float h = static_cast<float>(rotatedSize.height);
  switch (orientation) {
  case FrameOrientation::Up:
    return p;
  case FrameOrientation::Left:
    return {p.y, w - p.x};
  case FrameOrientation::Right:
    return {h - p.y, p.x};
  case FrameOrientation::Down:
    return {w - p.x, h - p.y};
  }
  throw std::runtime_error("Unknown frame orientation");
}

} // namespace utils
} // namespace cardscanner
