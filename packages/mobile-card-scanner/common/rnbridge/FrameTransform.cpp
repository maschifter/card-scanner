#include "FrameTransform.h"

namespace cardscanner {
namespace utils {

namespace nitrocamera = margelo::nitro::camera;

FrameOrientation
toFrameOrientation(nitrocamera::CameraOrientation orientation) {
  switch (orientation) {
  case nitrocamera::CameraOrientation::UP:
    return FrameOrientation::Up;
  case nitrocamera::CameraOrientation::LEFT:
    return FrameOrientation::Left;
  case nitrocamera::CameraOrientation::RIGHT:
    return FrameOrientation::Right;
  case nitrocamera::CameraOrientation::DOWN:
    return FrameOrientation::Down;
  }
  return FrameOrientation::Up;
}

} // namespace utils
} // namespace cardscanner
