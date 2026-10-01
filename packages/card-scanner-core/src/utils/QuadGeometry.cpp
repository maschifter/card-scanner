#include "QuadGeometry.h"
#include "../Constants.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <opencv2/imgproc.hpp>

namespace cardscanner {
namespace utils {

using namespace constants;

namespace {

// The quad scaled about its centroid by (1 + pad).
std::vector<cv::Point2f> padQuad(const std::vector<cv::Point2f> &quad,
                                 float pad) {
  cv::Point2f centre(0.0f, 0.0f);
  for (const auto &p : quad) {
    centre += p;
  }
  centre *= 1.0f / static_cast<float>(quad.size());
  std::vector<cv::Point2f> out;
  out.reserve(quad.size());
  for (const auto &p : quad) {
    out.push_back(centre + (p - centre) * (1.0f + pad));
  }
  return out;
}

std::vector<cv::Point2f>
orderQuad(const std::vector<cv::Point2f> &pts) {
  // Port of Python's order_quad:
  // Order as [TL, TR, BR, BL] using sum and diff
  // TL has min(x+y), BR has max(x+y)
  // TR has min(y-x), BL has max(y-x)

  if (pts.size() != 4) {
    return pts;
  }

  cv::Point2f tl, tr, br, bl;
  float minSum = FLT_MAX, maxSum = -FLT_MAX;
  float minDiff = FLT_MAX, maxDiff = -FLT_MAX;

  for (const auto &p : pts) {
    float sum = p.x + p.y;
    float diff = p.y - p.x;

    if (sum < minSum) {
      minSum = sum;
      tl = p;
    }
    if (sum > maxSum) {
      maxSum = sum;
      br = p;
    }
    if (diff < minDiff) {
      minDiff = diff;
      tr = p;
    }
    if (diff > maxDiff) {
      maxDiff = diff;
      bl = p;
    }
  }

  return {tl, tr, br, bl};
}

bool isValidQuad(const std::vector<cv::Point2f> &quad) {
  if (quad.size() != 4)
    return false;

  // Check for degenerate points (too close together)
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (cv::norm(quad[i] - quad[j]) < yolo::MIN_POINT_DISTANCE)
        return false;
    }
  }

  // Check area
  std::vector<cv::Point> intQuad;
  for (const auto &p : quad) {
    intQuad.push_back(cv::Point(p.x, p.y));
  }

  double area = cv::contourArea(intQuad);
  return area >= yolo::MIN_QUAD_AREA;
}

MaskQuad orientQuad(const std::vector<cv::Point2f> &quad) {
  // Port of Python's _orient_quad_topmost_upright:
  // Find the edge with the smallest mid-y (topmost edge) and orient from there

  // Calculate midpoints of all 4 edges
  std::vector<cv::Point2f> mids(4);
  for (int i = 0; i < 4; i++) {
    cv::Point2f a = quad[i];
    cv::Point2f b = quad[(i + 1) % 4];
    mids[i] = (a + b) * 0.5f;
  }

  // Find the edge with smallest mid-y
  float minMidY = mids[0].y;
  for (int i = 1; i < 4; i++) {
    if (mids[i].y < minMidY) {
      minMidY = mids[i].y;
    }
  }

  // Find all edges within tolerance of the minimum
  std::vector<int> candidates;
  for (int i = 0; i < 4; i++) {
    if (mids[i].y <= minMidY + yolo::TOPMOST_TIE_TOLERANCE) {
      candidates.push_back(i);
    }
  }

  // If multiple candidates, break tie by smallest y, then leftmost x
  int topIdx = candidates[0];
  if (candidates.size() > 1) {
    std::sort(candidates.begin(), candidates.end(), [&mids](int i1, int i2) {
      if (std::abs(mids[i1].y - mids[i2].y) < yolo::ORIENT_Y_TOLERANCE) {
        return mids[i1].x < mids[i2].x; // leftmost
      }
      return mids[i1].y < mids[i2].y; // topmost
    });
    topIdx = candidates[0];
  }

  // Get points starting from the topmost edge
  cv::Point2f a = quad[topIdx];
  cv::Point2f b = quad[(topIdx + 1) % 4];
  cv::Point2f c = quad[(topIdx + 2) % 4];
  cv::Point2f d = quad[(topIdx + 3) % 4];

  // Ensure top edge goes left to right
  cv::Point2f tl, tr;
  if (a.x <= b.x) {
    tl = a;
    tr = b;
  } else {
    tl = b;
    tr = a;
  }

  // Assign bottom corners based on distance to TR
  cv::Point2f br, bl;
  if (cv::norm(c - tr) <= cv::norm(d - tr)) {
    br = c;
    bl = d;
  } else {
    br = d;
    bl = c;
  }

  // A card's top edge is its short edge - a sideways quad (stale/lagging
  // orientation metadata) would otherwise dewarp rotated 90 degrees.
  if (cv::norm(tr - tl) > cv::norm(br - tr)) {
    return {{tr, br, bl, tl}, true};
  }
  return {{tl, tr, br, bl}, false};
}


// How far the hull strays outside the quad, as a fraction of its shortest side.
float hullDeviation(const std::vector<cv::Point2f> &quad,
                    const std::vector<cv::Point> &hull) {
  float shortestSide = std::numeric_limits<float>::max();
  for (size_t i = 0; i < quad.size(); i++) {
    shortestSide = std::min(
        shortestSide,
        static_cast<float>(cv::norm(quad[(i + 1) % quad.size()] - quad[i])));
  }
  if (!(shortestSide > 0.0f)) {
    return std::numeric_limits<float>::max();
  }

  float worst = 0.0f;
  for (const auto &point : hull) {
    const double signedDistance = cv::pointPolygonTest(
        quad, cv::Point2f(static_cast<float>(point.x),
                          static_cast<float>(point.y)),
        true);
    if (signedDistance < 0.0) {
      worst = std::max(worst, static_cast<float>(-signedDistance));
    }
  }
  return worst / shortestSide;
}

// Direction of the hull's longest edge: a clipped corner only adds short
// edges, so the longest is still a true card side.
cv::Point2f hullDirection(const std::vector<cv::Point> &hull) {
  cv::Point2f best(1.0f, 0.0f);
  double bestLength = 0.0;
  for (size_t i = 0; i < hull.size(); i++) {
    const cv::Point2f edge = hull[(i + 1) % hull.size()] - hull[i];
    const double length = cv::norm(edge);
    if (length > bestLength) {
      bestLength = length;
      best = edge / static_cast<float>(length);
    }
  }
  return best;
}

// The smallest rectangle at `direction` that holds every hull point.
std::vector<cv::Point2f> orientedBounds(const std::vector<cv::Point> &hull,
                                        const cv::Point2f &direction) {
  const cv::Point2f normal(-direction.y, direction.x);
  float minAlong = std::numeric_limits<float>::max();
  float maxAlong = std::numeric_limits<float>::lowest();
  float minAcross = std::numeric_limits<float>::max();
  float maxAcross = std::numeric_limits<float>::lowest();
  for (const auto &point : hull) {
    const cv::Point2f p(static_cast<float>(point.x), static_cast<float>(point.y));
    const float along = p.dot(direction);
    const float across = p.dot(normal);
    minAlong = std::min(minAlong, along);
    maxAlong = std::max(maxAlong, along);
    minAcross = std::min(minAcross, across);
    maxAcross = std::max(maxAcross, across);
  }
  const auto corner = [&](float along, float across) {
    return direction * along + normal * across;
  };
  return {corner(minAlong, minAcross), corner(maxAlong, minAcross),
          corner(maxAlong, maxAcross), corner(minAlong, maxAcross)};
}

} // namespace

MaskQuad quadFromMask(const cv::Mat &maskU8, bool bestFit) {
  // Use CHAIN_APPROX_SIMPLE for faster contour detection
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(maskU8, contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  if (contours.empty()) {
    return {};
  }

  // Get largest contour
  auto largestContour = *std::max_element(
      contours.begin(), contours.end(), [](const auto &a, const auto &b) {
        return cv::contourArea(a) < cv::contourArea(b);
      });

  // Get convex hull
  std::vector<cv::Point> hull;
  cv::convexHull(largestContour, hull);

  // Try polygon approximation and keep the fit that loses least of the hull.
  // Taking the first four-point result instead lets a coarse epsilon chop a
  // corner, and the dewarp then trades that corner for background.
  const float perimeter = cv::arcLength(hull, true);
  std::vector<cv::Point2f> best;
  float bestDeviation = std::numeric_limits<float>::max();

  for (float frac : yolo::QUAD_EPSILON_FRACS) {
    std::vector<cv::Point> approx;
    cv::approxPolyDP(hull, approx, frac * perimeter, true);
    if (approx.size() != 4) {
      continue;
    }

    std::vector<cv::Point2f> quadF;
    quadF.reserve(4);
    for (const auto &p : approx) {
      quadF.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }

    auto ordered = orderQuad(quadF);
    if (!isValidQuad(ordered)) {
      continue;
    }
    if (!bestFit) {
      return orientQuad(ordered);
    }
    const float deviation = hullDeviation(ordered, hull);
    if (deviation < bestDeviation) {
      bestDeviation = deviation;
      best = std::move(ordered);
    }
  }

  if (!best.empty() && bestDeviation <= yolo::MAX_HULL_DEVIATION_FRAC) {
    return orientQuad(best);
  }
  if (!bestFit) {
    cv::Point2f vertices[4];
    cv::minAreaRect(hull).points(vertices);
    return orientQuad(orderQuad({vertices, vertices + 4}));
  }

  // Nothing fit the hull closely enough, so bound it instead. That pays a
  // little background at the corners, which the embedder survives far better
  // than a missing corner. Squared to the hull's longest edge, so a clipped
  // mask does not come back tilted.
  auto ordered = orderQuad(orientedBounds(hull, hullDirection(hull)));
  return orientQuad(ordered);
}

cv::Mat
warpPerspectiveCard(const cv::Mat &img, const std::vector<cv::Point2f> &quad,
                    int targetH, float aspect) {

  int W = static_cast<int>(std::round(targetH * aspect));
  int H = targetH;

  std::vector<cv::Point2f> dst = {cv::Point2f(0, 0), cv::Point2f(W - 1, 0),
                                  cv::Point2f(W - 1, H - 1),
                                  cv::Point2f(0, H - 1)};

  cv::Mat M = cv::getPerspectiveTransform(quad, dst);
  cv::Mat warped;
  cv::warpPerspective(img, warped, M, cv::Size(W, H), cv::INTER_LINEAR);

  return warped;
}

void dewarpDetections(const cv::Mat &frame, std::vector<Detection> &detections,
                      float padding) {
  for (auto &det : detections) {
    if (det.quad.size() == 4) {
      det.dewarpedCard = warpPerspectiveCard(
          frame, padding > 0.0f ? padQuad(det.quad, padding) : det.quad,
          card::DEWARP_HEIGHT, card::ASPECT_RATIO);
    }
  }
}

} // namespace utils
} // namespace cardscanner
