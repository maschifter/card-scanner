#pragma once

#include "../types/Detection.h"
#include <opencv2/core.hpp>
#include <vector>

namespace cardscanner {
namespace utils {

/// A quad [TL, TR, BR, BL] fitted to a mask.
struct MaskQuad {
  std::vector<cv::Point2f> points;
  /// The quad needed the sideways 90-degree fixup.
  bool sideways = false;
};

/**
 * @brief Fits a 4-point quad [TL, TR, BR, BL] to the largest blob in a
 * binary mask, in the mask's own pixel space.
 * @param bestFit Keep the fit that loses least of the hull and fall back to
 *   oriented bounds (multi modes); otherwise the first valid fit, else
 *   minAreaRect (single mode, unchanged for benchmarks).
 * @return The quad (empty points when the mask holds nothing usable)
 */
MaskQuad quadFromMask(const cv::Mat &maskU8, bool bestFit);

/**
 * @brief Perspective-warps the quad [TL, TR, BR, BL] to an upright card
 * targetH pixels tall with the given width/height aspect.
 */
cv::Mat warpPerspectiveCard(const cv::Mat &img,
                            const std::vector<cv::Point2f> &quad, int targetH,
                            float aspect);

/**
 * @brief Dewarps every detection that has a quad into its dewarpedCard, with
 * the quad grown about its centre by `padding` first. The quad stays tight.
 */
void dewarpDetections(const cv::Mat &frame, std::vector<Detection> &detections,
                      float padding);

} // namespace utils
} // namespace cardscanner
