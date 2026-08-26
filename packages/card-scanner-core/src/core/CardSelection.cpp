#include "CardSelection.h"
#include "../Constants.h"
#include "../utils/BoxGeometry.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>

namespace cardscanner {
namespace core {

namespace selection = constants::selection;
using utils::BoxGeometry;

namespace {

// Previously selected card, shared by the sync and async scan paths. A single
// scanner is active at a time; the mutex only guards against their overlap.
struct StickyState {
  BBox box{};
  std::chrono::steady_clock::time_point lastSeen;
  bool valid = false;
};
std::mutex stickyMutex;
StickyState stickyState;

// Quad centroid when the mask produced one, else box center - the quad tracks
// the card itself, so it stays stable for tilted/sideways cards.
cv::Point2f cardCenter(const Detection &det) {
  if (det.quad.size() == 4) {
    cv::Point2f sum(0.0f, 0.0f);
    for (const auto &p : det.quad) {
      sum += p;
    }
    return sum * 0.25f;
  }
  return {(det.box.x1 + det.box.x2) * 0.5f, (det.box.y1 + det.box.y2) * 0.5f};
}

} // namespace

void selectCenterMost(std::vector<Detection> &detections,
                      const cv::Size &frameSize) {
  if (detections.size() <= 1) {
    if (!detections.empty()) {
      std::lock_guard<std::mutex> lock(stickyMutex);
      stickyState.box = detections[0].box;
      stickyState.lastSeen = std::chrono::steady_clock::now();
      stickyState.valid = true;
    }
    return;
  }

  const float centerX = frameSize.width * 0.5f;
  const float centerY = frameSize.height * 0.5f;

  int best = -1;
  float bestDistSq = std::numeric_limits<float>::max();
  std::vector<float> distancesSq(detections.size());
  for (size_t i = 0; i < detections.size(); i++) {
    const cv::Point2f center = cardCenter(detections[i]);
    const float dx = center.x - centerX;
    const float dy = center.y - centerY;
    distancesSq[i] = dx * dx + dy * dy;
    if (distancesSq[i] < bestDistSq) {
      bestDistSq = distancesSq[i];
      best = static_cast<int>(i);
    }
  }

  int winner = best;
  {
    std::lock_guard<std::mutex> lock(stickyMutex);
    const auto now = std::chrono::steady_clock::now();
    if (stickyState.valid &&
        now - stickyState.lastSeen > selection::TRACKING_LOST_AFTER) {
      stickyState.valid = false;
    }

    if (stickyState.valid) {
      int trackedIdx = -1;
      float trackedDistSq = 0.0f;
      float bestIou = selection::SAME_CARD_MIN_IOU;
      for (size_t i = 0; i < detections.size(); i++) {
        const float overlap = BoxGeometry::intersectionOverUnion(
            stickyState.box, detections[i].box);
        if (overlap >= bestIou) {
          bestIou = overlap;
          trackedIdx = static_cast<int>(i);
          trackedDistSq = distancesSq[i];
        }
      }

      if (trackedIdx >= 0) {
        // Keep the tracked card unless a rival is clearly closer to the
        // frame center - deliberate re-aiming switches, jitter does not.
        constexpr float takeoverFracSq = selection::RIVAL_TAKEOVER_DIST_FRAC *
                                         selection::RIVAL_TAKEOVER_DIST_FRAC;
        winner = bestDistSq < takeoverFracSq * trackedDistSq ? best
                                                             : trackedIdx;
      } else {
        // Tracked card missed: rivals wait until tracking is lost
        // (anti-bounce), except a dead-center one - deliberate re-aiming,
        // take it immediately.
        const float takeoverRadius =
            selection::DEAD_CENTER_RADIUS_FRAC *
            std::min(frameSize.width, frameSize.height);
        winner = bestDistSq < takeoverRadius * takeoverRadius ? best : -1;
      }
    }

    if (winner >= 0) {
      stickyState.box = detections[winner].box;
      stickyState.lastSeen = now;
      stickyState.valid = true;
    }
  }

  if (winner < 0) {
    detections.clear();
    return;
  }
  Detection selected = std::move(detections[winner]);
  detections.clear();
  detections.push_back(std::move(selected));
}

} // namespace core
} // namespace cardscanner
