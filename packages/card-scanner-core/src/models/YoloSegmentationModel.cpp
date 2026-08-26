#include "YoloSegmentationModel.h"
#include "../Constants.h"
#include "../utils/BoxGeometry.h"
#include "../utils/YoloPreprocessing.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace cardscanner {

using namespace constants;
using utils::BoxGeometry;

YoloSegmentationModel::YoloSegmentationModel(
    const std::string &modelPath, const GameClassMap &classNames,
    float conf, float iou, int imgsz)
    : conf_(conf), iou_(iou), imgsz_(imgsz), classNames_(classNames) {
  if (classNames_.empty()) {
    throw std::runtime_error(
        "Game class mapping is required. Please provide gameClassMapping in "
        "scanner configuration.");
  }

  session_ = inference::loadSession(modelPath);
}

std::vector<float>
YoloSegmentationModel::preprocess(const cv::Mat &img) const {
  return utils::YoloPreprocessing::preprocess(img, imgsz_);
}

namespace {
void dropGroupBoxes(const std::vector<BBox> &boxes, std::vector<int> &keep) {
  if (keep.size() < 2) {
    return;
  }

  std::vector<bool> dropped(keep.size(), false);

  for (size_t i = 0; i < keep.size(); i++) {
    if (dropped[i]) {
      continue;
    }
    const BBox &container = boxes[keep[i]];

    int containedCount = 0;
    int lastContained = -1;
    for (size_t j = 0; j < keep.size(); j++) {
      if (j == i || dropped[j]) {
        continue;
      }
      if (BoxGeometry::containedFraction(boxes[keep[j]], container) >=
          selection::CONTAINED_MIN_AREA_FRAC) {
        containedCount++;
        lastContained = static_cast<int>(j);
      }
    }

    if (containedCount >= selection::GROUP_BOX_MIN_CONTAINED_CARDS) {
      dropped[i] = true;
    } else if (containedCount == 1) {
      const BBox &inner = boxes[keep[lastContained]];
      if (BoxGeometry::area(container) >=
              selection::GROUP_BOX_MIN_AREA_RATIO * BoxGeometry::area(inner) &&
          BoxGeometry::aspectRatioMismatch(container, inner) >=
              selection::GROUP_BOX_MIN_ASPECT_MISMATCH) {
        dropped[i] = true;
      }
    }
  }

  size_t out = 0;
  for (size_t i = 0; i < keep.size(); i++) {
    if (!dropped[i]) {
      keep[out++] = keep[i];
    }
  }
  keep.resize(out);
}

} // namespace

std::vector<int>
YoloSegmentationModel::nonMaxSuppression(const std::vector<BBox> &boxes) const {

  std::vector<int> indices(boxes.size());
  for (size_t i = 0; i < boxes.size(); i++) {
    indices[i] = i;
  }

  // Sort by confidence descending
  std::sort(indices.begin(), indices.end(), [&boxes](int i1, int i2) {
    return boxes[i1].conf > boxes[i2].conf;
  });

  std::vector<int> keep;
  std::vector<bool> suppressed(boxes.size(), false);

  for (size_t i = 0; i < indices.size(); i++) {
    int idx = indices[i];
    if (suppressed[idx])
      continue;

    keep.push_back(idx);
    const BBox &box1 = boxes[idx];

    for (size_t j = i + 1; j < indices.size(); j++) {
      int idx2 = indices[j];
      if (suppressed[idx2])
        continue;

      const BBox &box2 = boxes[idx2];
      if (BoxGeometry::intersectionOverUnion(box1, box2) > iou_ ||
          BoxGeometry::mutuallyContained(box1, box2,
                                         selection::CONTAINED_MIN_AREA_FRAC)) {
        suppressed[idx2] = true;
      }
    }
  }

  return keep;
}

cv::Mat YoloSegmentationModel::processMask(const std::vector<float> &protos,
                                           int protoH, int protoW,
                                           const std::vector<float> &maskCoeffs,
                                           const BBox &bbox,
                                           const cv::Size &imgSize) const {

  int protoC = maskCoeffs.size();

  // Matrix multiplication: maskCoeffs @ protos using OpenCV for performance
  cv::Mat protosMat(protoC, protoH * protoW, CV_32FC1, (void *)protos.data());
  cv::Mat coeffsMat(1, protoC, CV_32FC1, (void *)maskCoeffs.data());
  cv::Mat resultMat;
  cv::gemm(coeffsMat, protosMat, matrix::GEMM_ALPHA, cv::Mat(),
           matrix::GEMM_BETA, resultMat);

  // Apply sigmoid activation
  float *resultData = resultMat.ptr<float>();
  for (int i = 0; i < protoH * protoW; i++) {
    resultData[i] =
        matrix::SIGMOID_ONE / (matrix::SIGMOID_ONE + std::exp(-resultData[i]));
  }

  cv::Mat maskMat = resultMat.reshape(matrix::RESHAPE_SINGLE_CHANNEL, protoH);

  // Calculate bbox region with padding
  int bx1 = std::max(0, static_cast<int>(bbox.x1) - yolo::BBOX_PADDING);
  int by1 = std::max(0, static_cast<int>(bbox.y1) - yolo::BBOX_PADDING);
  int bx2 =
      std::min(imgSize.width, static_cast<int>(bbox.x2) + yolo::BBOX_PADDING);
  int by2 =
      std::min(imgSize.height, static_cast<int>(bbox.y2) + yolo::BBOX_PADDING);

  cv::Size roiSize(bx2 - bx1, by2 - by1);

  // Calculate letterbox transform
  float gain = std::min(static_cast<float>(protoH) / imgSize.height,
                        static_cast<float>(protoW) / imgSize.width);
  float pad_w = protoW - imgSize.width * gain;
  float pad_h = protoH - imgSize.height * gain;
  pad_w /= letterbox::PADDING_DIVISOR;
  pad_h /= letterbox::PADDING_DIVISOR;

  // Map bbox coordinates to proto space
  float proto_x1 = (bx1 * gain) + pad_w;
  float proto_y1 = (by1 * gain) + pad_h;
  float proto_x2 = (bx2 * gain) + pad_w;
  float proto_y2 = (by2 * gain) + pad_h;

  // Clamp to proto bounds
  int px1 = std::max(0, static_cast<int>(std::floor(proto_x1)));
  int py1 = std::max(0, static_cast<int>(std::floor(proto_y1)));
  int px2 = std::min(protoW, static_cast<int>(std::ceil(proto_x2)));
  int py2 = std::min(protoH, static_cast<int>(std::ceil(proto_y2)));

  if (px2 <= px1 || py2 <= py1) {
    return cv::Mat();
  }

  // Extract and resize only the bbox region from proto mask
  cv::Rect protoRoi(px1, py1, px2 - px1, py2 - py1);
  cv::Mat protoRegion = maskMat(protoRoi);

  cv::Mat croppedMask;
  cv::resize(protoRegion, croppedMask, roiSize, 0, 0, cv::INTER_LINEAR);

  cv::Rect roi(bx1, by1, bx2 - bx1, by2 - by1);

  // Threshold the cropped region
  cv::Mat binaryMask;
  cv::threshold(croppedMask, binaryMask, yolo::MASK_THRESHOLD,
                yolo::BINARY_MASK_VALUE, cv::THRESH_BINARY);
  binaryMask.convertTo(binaryMask, CV_8U);

  // Check if we have multiple components (rare case)
  // Use simple contour count first - much faster than connectedComponents
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binaryMask, contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  cv::Mat fullMask = cv::Mat::zeros(imgSize, CV_8U);

  if (contours.size() == 1) {
    // Fast path: only one contour, just copy the entire binary mask
    binaryMask.copyTo(fullMask(roi));
  } else if (contours.size() > 1) {
    // Multiple contours: find and use only the largest one
    auto largestContour = std::max_element(
        contours.begin(), contours.end(), [](const auto &a, const auto &b) {
          return cv::contourArea(a) < cv::contourArea(b);
        });

    // Draw only the largest contour
    cv::Mat singleContourMask = cv::Mat::zeros(binaryMask.size(), CV_8U);
    cv::drawContours(singleContourMask, contours,
                     std::distance(contours.begin(), largestContour),
                     cv::Scalar(yolo::BINARY_MASK_VALUE), cv::FILLED);
    singleContourMask.copyTo(fullMask(roi));
  } else {
    // No contours found
    return cv::Mat();
  }

  return fullMask;
}

std::vector<Detection> YoloSegmentationModel::postprocess(
    const cv::Mat &originalImg, const std::vector<float> &preds,
    const std::vector<float> &protos, int protoH, int protoW,
    const GameClassMap &classNames) {
  // Parse predictions tensor: [1, 4 + numClasses + 32, anchors], channels-first

  std::vector<BBox> boxes;
  std::vector<std::vector<float>> maskCoeffs;

  // Check if we have any predictions
  if (preds.empty()) {
    return {};
  }

  // Derived from the mapping, not a constant: this is the decode stride
  const int numClasses = static_cast<int>(classNames.size());
  const int numMaskCoeffs = yolo::MASK_COEFFS;
  const int boxCoords = yolo::BOX_FEATURES;
  const int numFeatures = boxCoords + numClasses + numMaskCoeffs;
  int numPredictions = preds.size() / numFeatures;

  if (preds.size() % numFeatures != 0) {
    throw std::runtime_error(
        "Segmentation output size " + std::to_string(preds.size()) +
        " is not divisible by " + std::to_string(numFeatures) + " (4 box + " +
        std::to_string(numClasses) + " classes + " +
        std::to_string(numMaskCoeffs) +
        " mask coeffs). The gameClassMapping entry count almost certainly "
        "disagrees with the model's class count.");
  }

  // Data is stored as [feature][prediction] not [prediction][feature]
  // So we need to access it transposed
  for (int i = 0; i < numPredictions; i++) {
    // Find class with max confidence
    float maxConf = -1.0f;
    int classId = -1;
    for (int j = 0; j < numClasses; j++) {
      float conf = preds[(boxCoords + j) * numPredictions + i];
      if (conf >= maxConf) {
        maxConf = conf;
        classId = j;
      }
    }

    if (maxConf < conf_)
      continue;

    std::vector<std::pair<float, int>> class_confs;
    class_confs.reserve(numClasses);
    for (int j = 0; j < numClasses; j++) {
      class_confs.push_back({preds[(boxCoords + j) * numPredictions + i], j});
    }
    std::sort(class_confs.rbegin(), class_confs.rend());

    // Access transposed: feature_idx * numPredictions + prediction_idx
    float x = preds[0 * numPredictions + i];
    float y = preds[1 * numPredictions + i];
    float w = preds[2 * numPredictions + i];
    float h = preds[3 * numPredictions + i];

    // Convert xywh to xyxy
    float x1 = x - w / letterbox::PADDING_DIVISOR;
    float y1 = y - h / letterbox::PADDING_DIVISOR;
    float x2 = x + w / letterbox::PADDING_DIVISOR;
    float y2 = y + h / letterbox::PADDING_DIVISOR;

    // Reverse letterbox transformation
    // Calculate the scale and padding that was applied
    float r = std::min(static_cast<float>(imgsz_) / originalImg.rows,
                       static_cast<float>(imgsz_) / originalImg.cols);

    // Calculate padding that was added
    int newUnpadW = std::round(originalImg.cols * r);
    int newUnpadH = std::round(originalImg.rows * r);
    float dw = (imgsz_ - newUnpadW) / letterbox::PADDING_DIVISOR;
    float dh = (imgsz_ - newUnpadH) / letterbox::PADDING_DIVISOR;

    // Remove padding offset and scale back to original size
    BBox box;
    box.x1 = (x1 - dw) / r;
    box.y1 = (y1 - dh) / r;
    box.x2 = (x2 - dw) / r;
    box.y2 = (y2 - dh) / r;
    box.conf = maxConf;
    box.cls = classId;
    box.class_confs = std::move(class_confs);

    boxes.push_back(std::move(box));

    // Extract mask coefficients
    std::vector<float> coeffs;
    coeffs.reserve(numMaskCoeffs);
    const int maskCoeffStart = boxCoords + numClasses;
    for (int j = 0; j < numMaskCoeffs; j++) {
      coeffs.push_back(preds[(maskCoeffStart + j) * numPredictions + i]);
    }
    maskCoeffs.push_back(std::move(coeffs));
  }

  // Still required: only an end2end=True export is NMS-free, not this raw head
  std::vector<int> keep = nonMaxSuppression(boxes);
  dropGroupBoxes(boxes, keep);

  // Process masks for kept detections
  std::vector<Detection> detections;

  for (int idx : keep) {
    Detection det;
    det.box = boxes[idx];

    auto predictedIt = classNames.find(det.box.cls);
    det.predictedGame = predictedIt != classNames.end()
                            ? predictedIt->second.front() // canonical name
                            : yolo::UNKNOWN_CLASS_NAME;

    det.maskBinary = processMask(protos, protoH, protoW, maskCoeffs[idx],
                                 boxes[idx], originalImg.size());

    // Extract quad from mask and dewarp
    if (!det.maskBinary.empty()) {
      // Downsample mask for faster quad extraction, then scale quad back up
      constexpr float scale = yolo::MASK_DOWNSAMPLE_SCALE;
      cv::Mat smallMask;
      cv::resize(det.maskBinary, smallMask, cv::Size(), scale, scale,
                 cv::INTER_NEAREST);

      auto quadSmall = quadFromMask(smallMask, &det.quadWasSideways);

      // Scale quad coordinates back to full resolution
      if (quadSmall.size() == 4) {
        det.quad.resize(4);
        for (int i = 0; i < 4; i++) {
          det.quad[i].x = quadSmall[i].x / scale;
          det.quad[i].y = quadSmall[i].y / scale;
        }
      }

      // Dewarp if we have a valid quad
      if (det.quad.size() == 4) {
        det.dewarpedCard = warpPerspectiveCard(
            originalImg, det.quad, card::DEWARP_HEIGHT, card::ASPECT_RATIO);
      }
    }

    detections.push_back(det);
  }

  return detections;
}

std::vector<cv::Point2f>
YoloSegmentationModel::orderQuad(const std::vector<cv::Point2f> &pts) const {
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

bool YoloSegmentationModel::isValidQuad(
    const std::vector<cv::Point2f> &quad) const {
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

std::vector<cv::Point2f>
YoloSegmentationModel::orientQuad(const std::vector<cv::Point2f> &quad,
                                  bool *wasSideways) const {
  if (wasSideways != nullptr) {
    *wasSideways = false;
  }

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
    if (wasSideways != nullptr) {
      *wasSideways = true;
    }
    return {tr, br, bl, tl};
  }
  return {tl, tr, br, bl};
}

std::vector<cv::Point2f>
YoloSegmentationModel::quadFromMask(const cv::Mat &maskU8,
                                    bool *wasSideways) const {
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

  // Try polygon approximation - start with most likely epsilon values first
  float perimeter = cv::arcLength(hull, true);

  for (float frac : yolo::QUAD_EPSILON_FRACS) {
    std::vector<cv::Point> approx;
    cv::approxPolyDP(hull, approx, frac * perimeter, true);

    if (approx.size() == 4) {
      std::vector<cv::Point2f> quadF;
      quadF.reserve(4);
      for (const auto &p : approx) {
        quadF.push_back(cv::Point2f(p.x, p.y));
      }

      auto ordered = orderQuad(quadF);
      if (isValidQuad(ordered)) {
        return orientQuad(ordered, wasSideways);
      }
    }
  }

  // Fallback to minimum area rectangle
  cv::RotatedRect rect = cv::minAreaRect(hull);
  cv::Point2f vertices[4];
  rect.points(vertices);

  std::vector<cv::Point2f> quadF(vertices, vertices + 4);
  auto ordered = orderQuad(quadF);
  return orientQuad(ordered, wasSideways);
}

cv::Mat
YoloSegmentationModel::warpPerspectiveCard(const cv::Mat &img,
                                           const std::vector<cv::Point2f> &quad,
                                           int targetH, float aspect) const {

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

SegmentationResult YoloSegmentationModel::segment(const cv::Mat &image) {
  if (image.empty()) {
    throw std::runtime_error("Empty image provided to segment()");
  }

  // Preprocess
  std::vector<float> inputData = preprocess(image);

  // Run inference
  auto outputs = session_->run(
      inputData.data(), {1, model::EMBEDDING_CHANNELS, imgsz_, imgsz_});

  // Segmentation head emits predictions first and prototypes last.
  if (outputs.size() < 2) {
    throw std::runtime_error("Segmentation model returned " +
                             std::to_string(outputs.size()) +
                             " outputs, expected at least 2");
  }
  const std::vector<float> &preds = outputs.front().data;
  const auto &protosOutput = outputs.back();
  const std::vector<float> &protos = protosOutput.data;

  // Read the prototype grid from the tensor rather than assuming it is square
  const auto &protosSizes = protosOutput.shape;
  if (protosSizes.size() < 2) {
    throw std::runtime_error("Prototype tensor has " +
                             std::to_string(protosSizes.size()) +
                             " dimensions, expected at least 2 ([.., H, W])");
  }
  int protoH = static_cast<int>(protosSizes[protosSizes.size() - 2]);
  int protoW = static_cast<int>(protosSizes[protosSizes.size() - 1]);

  // Postprocess
  std::vector<Detection> detections =
      postprocess(image, preds, protos, protoH, protoW, classNames_);

  return SegmentationResult{detections};
}

} // namespace cardscanner
