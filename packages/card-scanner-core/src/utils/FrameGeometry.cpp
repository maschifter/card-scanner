#include "FrameGeometry.h"

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
  const int x1 = box.x;
  const int y1 = box.y;
  const int x2 = box.x + box.width;
  const int y2 = box.y + box.height;
  const int rotatedWidth = rotatedSize.width;
  const int rotatedHeight = rotatedSize.height;

  switch (orientation) {
  case FrameOrientation::Up:
    return box;
  case FrameOrientation::Left:
    return cv::Rect(cv::Point(y1, rotatedWidth - x2),
                    cv::Point(y2, rotatedWidth - x1));
  case FrameOrientation::Right:
    return cv::Rect(cv::Point(rotatedHeight - y2, x1),
                    cv::Point(rotatedHeight - y1, x2));
  case FrameOrientation::Down:
    return cv::Rect(cv::Point(rotatedWidth - x2, rotatedHeight - y2),
                    cv::Point(rotatedWidth - x1, rotatedHeight - y1));
  }
  throw std::runtime_error("Unknown frame orientation");
}

} // namespace utils
} // namespace cardscanner
