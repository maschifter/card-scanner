#pragma once

#include <algorithm>
#include <numeric>
#include <vector>

namespace cardscanner {
namespace utils {

/**
 * @class BoxGeometry
 * @brief Area and overlap math for any box type with x1/y1/x2/y2 corner fields.
 */
class BoxGeometry {
public:
  template <typename Box> static float area(const Box &b) {
    return std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
  }

  template <typename Box>
  static float intersectionArea(const Box &a, const Box &b) {
    const float w = std::min(a.x2, b.x2) - std::max(a.x1, b.x1);
    const float h = std::min(a.y2, b.y2) - std::max(a.y1, b.y1);
    return std::max(0.0f, w) * std::max(0.0f, h);
  }

  template <typename Box>
  static float intersectionOverUnion(const Box &a, const Box &b) {
    const float inter = intersectionArea(a, b);
    return inter / (area(a) + area(b) - inter + AREA_EPSILON);
  }

  /// Fraction of `inner`'s own area covered by `outer`.
  template <typename Box>
  static float containedFraction(const Box &inner, const Box &outer) {
    return intersectionArea(inner, outer) / (area(inner) + AREA_EPSILON);
  }

  /// Each box covers at least `minFraction` of the other.
  template <typename Box>
  static bool mutuallyContained(const Box &a, const Box &b,
                                float minFraction) {
    return containedFraction(a, b) >= minFraction &&
           containedFraction(b, a) >= minFraction;
  }

  /// How many times the two aspect ratios differ, regardless of direction.
  template <typename Box>
  static float aspectRatioMismatch(const Box &a, const Box &b) {
    const float aspectA =
        std::max(1.0f, a.x2 - a.x1) / std::max(1.0f, a.y2 - a.y1);
    const float aspectB =
        std::max(1.0f, b.x2 - b.x1) / std::max(1.0f, b.y2 - b.y1);
    return std::max(aspectA / aspectB, aspectB / aspectA);
  }

  /// Greedy non-maximum suppression: walks the boxes best score first and
  /// drops every later box that `overlaps` one already kept.
  /// @return Indices of the kept boxes, best score first
  template <typename Box, typename Score, typename Overlaps>
  static std::vector<int> nonMaxSuppression(const std::vector<Box> &boxes,
                                            Score score, Overlaps overlaps) {
    std::vector<int> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      return score(boxes[a]) > score(boxes[b]);
    });

    std::vector<int> keep;
    std::vector<bool> suppressed(boxes.size(), false);
    for (size_t i = 0; i < order.size(); i++) {
      if (suppressed[order[i]]) {
        continue;
      }
      keep.push_back(order[i]);
      for (size_t j = i + 1; j < order.size(); j++) {
        if (!suppressed[order[j]] &&
            overlaps(boxes[order[i]], boxes[order[j]])) {
          suppressed[order[j]] = true;
        }
      }
    }
    return keep;
  }

private:
  static constexpr float AREA_EPSILON = 1e-6f;
};

} // namespace utils
} // namespace cardscanner
