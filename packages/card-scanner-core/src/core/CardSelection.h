#pragma once

#include "../types/Detection.h"
#include <opencv2/core.hpp>
#include <vector>

namespace cardscanner {
namespace core {

/**
 * @brief Keep only the detection nearest the frame center; multi-card scenes
 * stabilize the pick against the previous frame, a lone one passes.
 */
void selectCenterMost(std::vector<Detection> &detections,
                      const cv::Size &frameSize);

} // namespace core
} // namespace cardscanner
