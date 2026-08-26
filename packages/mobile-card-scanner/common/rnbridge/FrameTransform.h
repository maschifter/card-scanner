#ifndef FRAME_TRANSFORM_H
#define FRAME_TRANSFORM_H

#include <VisionCamera/CameraOrientation.hpp>
#include <utils/FrameGeometry.h>

namespace cardscanner {
namespace utils {

/**
 * @brief Maps VisionCamera's orientation onto the neutral one core works in.
 *
 * The rotation and box maths themselves live in core (utils/FrameGeometry.h) so
 * a non-VisionCamera capture source can reuse them; all this package owns is
 * the translation of one vendor enum.
 *
 * The app uses only the back camera, so mirroring is never handled.
 */
FrameOrientation
toFrameOrientation(margelo::nitro::camera::CameraOrientation orientation);

} // namespace utils
} // namespace cardscanner

#endif // FRAME_TRANSFORM_H
