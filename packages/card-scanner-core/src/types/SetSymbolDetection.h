#pragma once

#include <vector>

namespace cardscanner {

/**
 * @struct SetSymbolBBox
 * @brief An axis-aligned box around a detected set symbol.
 *
 * Shares the x1/y1/x2/y2 field names with BBox on purpose: utils::BoxGeometry
 * is templated and duck-typed on them, so both work with it without a shared
 * base class.
 */
struct SetSymbolBBox {
  float x1, y1, x2, y2; ///< Absolute pixel coordinates
  float confidence;
};

/**
 * @struct SetSymbolDetectionResult
 * @brief Every set symbol found in one card image.
 */
struct SetSymbolDetectionResult {
  std::vector<SetSymbolBBox> detections;
};

} // namespace cardscanner
