#include "MultiScanSession.h"
#include "../Constants.h"
#include "../utils/ImageUtils.h"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace cardscanner {
namespace core {

namespace {

// Quad when the mask produced one, else the box corners. Always
// [TL, TR, BR, BL].
std::vector<cv::Point2f> polygonOf(const Detection &det) {
  if (det.quad.size() == 4) {
    return det.quad;
  }
  const auto &b = det.box;
  return {{b.x1, b.y1}, {b.x2, b.y1}, {b.x2, b.y2}, {b.x1, b.y2}};
}

// Orientation of the top edge in degrees, folded to [0, 180).
float orientationDeg(const std::vector<cv::Point2f> &poly) {
  const cv::Point2f edge = poly[1] - poly[0];
  return std::fmod(cv::fastAtan2(edge.y, edge.x), 180.0f);
}

// Largest deviation from the mean orientation, on the 180-degree circle.
// Doubling the angles makes the mean well defined across the 0/180 wrap.
float angleSpreadDeg(const std::vector<float> &degs) {
  float sx = 0.0f, sy = 0.0f;
  for (float d : degs) {
    const float r = d * static_cast<float>(CV_PI / 90.0); // doubled, radians
    sx += std::cos(r);
    sy += std::sin(r);
  }
  const float meanDeg = cv::fastAtan2(sy, sx) / 2.0f;
  float spread = 0.0f;
  for (float d : degs) {
    const float diff = std::fabs(d - meanDeg);
    spread = std::max(spread, std::min(diff, 180.0f - diff));
  }
  return spread;
}

float polygonIoU(const std::vector<cv::Point2f> &a,
                 const std::vector<cv::Point2f> &b, float areaA, float areaB) {
  std::vector<cv::Point2f> inter;
  const float interArea =
      std::max(0.0f, cv::intersectConvexConvex(a, b, inter));
  const float unionArea = areaA + areaB - interArea;
  return unionArea > 0.0f ? interArea / unionArea : 0.0f;
}

// Laplacian variance of one card's own full-resolution pixels.
double cardBlurScore(const cv::Mat &frame, const BBox &box) {
  const cv::Rect roi = utils::ImageUtils::boundingBoxToRect(box) &
                       cv::Rect(0, 0, frame.cols, frame.rows);
  if (roi.width < 2 || roi.height < 2) {
    return 0.0;
  }
  return utils::ImageUtils::calculateBlurScore(frame(roi));
}

} // namespace

MultiVerdict
MultiScanSession::evaluateLayout(const std::vector<Detection> &detections,
                                 const cv::Mat &frameImage,
                                 const ScannerConfig &config) {
  if (static_cast<int>(detections.size()) <
      std::max(1, config.minCardsForMulti)) {
    return {false, "count"};
  }

  // One pass over the confident cards. The hull, order-free, serves area and
  // overlap; the ordered polygon serves the angle and the long edge.
  std::vector<BBox> boxes;
  std::vector<std::vector<cv::Point2f>> hulls;
  std::vector<float> areas, degs, longEdges;
  for (const auto &det : detections) {
    if (det.box.conf < config.segmentationThreshold) {
      continue;
    }
    const std::vector<cv::Point2f> poly = polygonOf(det);
    std::vector<cv::Point2f> hull;
    cv::convexHull(poly, hull);
    if (hull.size() < 3) {
      continue;
    }
    boxes.push_back(det.box);
    areas.push_back(static_cast<float>(cv::contourArea(hull)));
    degs.push_back(orientationDeg(poly));
    longEdges.push_back(static_cast<float>(
        std::max(cv::norm(poly[1] - poly[0]), cv::norm(poly[3] - poly[0]))));
    hulls.push_back(std::move(hull));
  }
  // The geometry needs a few confident cards to say anything about the page.
  if (hulls.size() < constants::multi::MIN_CONFIDENT_CARDS) {
    return {false, "count"};
  }

  if (angleSpreadDeg(degs) > constants::multi::MAX_ANGLE_SPREAD_DEG) {
    return {false, "angle"};
  }

  // ponytail: O(n^2) pairwise check; n is a few dozen cards at most.
  for (size_t i = 0; i < hulls.size(); i++) {
    for (size_t j = i + 1; j < hulls.size(); j++) {
      if (polygonIoU(hulls[i], hulls[j], areas[i], areas[j]) >
          constants::multi::MAX_OVERLAP) {
        return {false, "overlap"};
      }
    }
  }

  const float minEdge =
      constants::multi::MIN_CARD_FRAC *
      static_cast<float>(std::max(frameImage.cols, frameImage.rows));
  if (*std::min_element(longEdges.begin(), longEdges.end()) < minEdge) {
    return {false, "small"};
  }

  std::vector<float> sorted = areas;
  std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2,
                   sorted.end());
  const float median = sorted[sorted.size() / 2];
  const float ratio = constants::multi::SIZE_RATIO;
  for (float a : areas) {
    if (a * ratio < median || a > median * ratio) {
      return {false, "size"};
    }
  }

  // Last: the only check that reads pixels.
  for (const BBox &box : boxes) {
    if (cardBlurScore(frameImage, box) <
        constants::multi::CARD_BLUR_THRESHOLD) {
      return {false, "blur"};
    }
  }

  return {true, ""};
}

void MultiScanSession::setListener(Listener listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  listener_ = std::move(listener);
}

MultiVerdict
MultiScanSession::evaluate(const std::vector<Detection> &detections,
                           const cv::Mat &frameImage,
                           const ScannerConfig &config) {
  MultiVerdict verdict = evaluateLayout(detections, frameImage, config);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!verdict.qualifies) {
    consecutiveQualifying_ = 0;
    return verdict;
  }
  if (++consecutiveQualifying_ < config.multiStableFrames) {
    return {false, "unstable"};
  }
  return verdict;
}

void MultiScanSession::begin(const std::string &frameImagePath,
                             const cv::Size &frameSize,
                             const std::vector<Detection> &detections) {
  Event event;
  event.type = Event::Type::Started;
  event.frameImagePath = frameImagePath;
  for (const auto &det : detections) {
    event.slots.push_back({utils::ImageUtils::boundingBoxToRect(det.box),
                           det.box.conf, det.quad, det.predictedGame});
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frameSize_ = frameSize;
    total_ = detections.size();
    consecutiveQualifying_ = 0;
  }
  emit(std::move(event));
}

void MultiScanSession::resolved(size_t index, const ProcessedCard &card) {
  Event event;
  event.type = Event::Type::CardResolved;
  event.index = index;
  event.card = card;
  emit(std::move(event));
}

void MultiScanSession::ended() {
  Event event;
  event.type = Event::Type::Ended;
  emit(std::move(event));
}

void MultiScanSession::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  consecutiveQualifying_ = 0;
  generation_++;
}

int MultiScanSession::generation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation_;
}

void MultiScanSession::emit(Event event) const {
  Listener listener;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    listener = listener_;
    event.frameSize = frameSize_;
    event.total = total_;
  }
  if (listener) {
    listener(event);
  }
}

} // namespace core
} // namespace cardscanner
