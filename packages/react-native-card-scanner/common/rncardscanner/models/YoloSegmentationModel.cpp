#include "YoloSegmentationModel.h"
#include "../Constants.h"
#include "../utils/PathUtils.h"
#include "../utils/YoloPreprocessing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <opencv2/imgproc.hpp>

namespace rncardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;
using namespace constants;

YoloSegmentationModel::YoloSegmentationModel(
    const std::string &modelPath,
    const std::map<int, std::string> &classNames, float conf, float iou,
    int imgsz)
    : conf_(conf), iou_(iou), imgsz_(imgsz), classNames_(classNames) {
  std::string cleanPath = utils::PathUtils::stripFilePrefix(modelPath);

  if (classNames_.empty()) {
    throw std::runtime_error(
        "Game class mapping is required. Please provide gameClassMapping in "
        "scanner configuration.");
  }

  module_ = std::make_unique<Module>(
      cleanPath, Module::LoadMode::MmapUseMlockIgnoreErrors);
  Error loadError = module_->load();

  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load segmentation model: " +
                             std::to_string(static_cast<int>(loadError)));
  }
}

cv::Mat YoloSegmentationModel::letterbox(const cv::Mat &img,
                                         int newSize) const {
  return utils::YoloPreprocessing::letterbox(img, newSize);
}

std::vector<float>
YoloSegmentationModel::preprocess(const cv::Mat &img,
                                  cv::Mat &letterboxed) const {
  return utils::YoloPreprocessing::preprocess(img, imgsz_, letterboxed);
}

std::vector<int> YoloSegmentationModel::nonMaxSuppression(
    const std::vector<BBox> &boxes,
    const std::vector<std::vector<float>> &maskCoeffs) const {

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

      // Calculate IoU
      float x1 = std::max(box1.x1, box2.x1);
      float y1 = std::max(box1.y1, box2.y1);
      float x2 = std::min(box1.x2, box2.x2);
      float y2 = std::min(box1.y2, box2.y2);

      float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
      float area1 = (box1.x2 - box1.x1) * (box1.y2 - box1.y1);
      float area2 = (box2.x2 - box2.x1) * (box2.y2 - box2.y1);
      float iou = inter / (area1 + area2 - inter);

      if (iou > iou_) {
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
  cv::findContours(binaryMask.clone(), contours, cv::RETR_EXTERNAL,
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
    const cv::Mat &originalImg, const cv::Mat &letterboxed,
    const std::vector<float> &preds, const std::vector<float> &protos,
    const std::map<int, std::string> &classNames) {
  // Parse predictions tensor
  // New YOLO output shape: [1, 44, 3024] which is [batch, features,
  // predictions] Format: features = [x, y, w, h] (4) + [class_conf] (8) +
  // [mask_coeffs] (32) = 44 predictions = 3024 anchor points

  std::vector<BBox> boxes;
  std::vector<std::vector<float>> maskCoeffs;

  // Check if we have any predictions
  if (preds.empty()) {
    return {};
  }

  // YOLO output is [1, 44, 3024] -> flattened to [44 * 3024]
  // 44 = 4 (box) + 8 (classes) + 32 (mask coeffs)
  const int numClasses = yolo::NUM_CLASSES;
  const int numMaskCoeffs = yolo::YOLO11_MASK_COEFFS;
  const int boxCoords = yolo::YOLO11_BOX_FEATURES;
  const int numFeatures = boxCoords + numClasses + numMaskCoeffs;
  int numPredictions = preds.size() / numFeatures;

  // Data is stored as [feature][prediction] not [prediction][feature]
  // So we need to access it transposed
  for (int i = 0; i < numPredictions; i++) {
    // Find class with max confidence
    std::vector<std::pair<float, int>> class_confs;
    for (int j = 0; j < numClasses; j++) {
      class_confs.push_back({preds[(boxCoords + j) * numPredictions + i], j});
    }
    std::sort(class_confs.rbegin(), class_confs.rend());

    float maxConf = class_confs[0].first;
    int classId = class_confs[0].second;

    if (maxConf < conf_)
      continue;

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
    box.class_confs = class_confs;

    boxes.push_back(box);

    // Extract mask coefficients
    std::vector<float> coeffs;
    coeffs.reserve(numMaskCoeffs);
    const int maskCoeffStart = boxCoords + numClasses;
    for (int j = 0; j < numMaskCoeffs; j++) {
      coeffs.push_back(preds[(maskCoeffStart + j) * numPredictions + i]);
    }
    maskCoeffs.push_back(coeffs);
  }

  // Apply NMS
  std::vector<int> keep = nonMaxSuppression(boxes, maskCoeffs);

  // Parse protos dimensions (typically [32, H, W] where H=W=96 or 160)
  int protoC = yolo::YOLO11_MASK_COEFFS;
  int protoSize = protos.size() / protoC;
  int protoH = std::sqrt(protoSize);
  int protoW = protoH;

  // Process masks for kept detections
  std::vector<Detection> detections;

  for (int idx : keep) {
    Detection det;
    det.box = boxes[idx];

    // Populate topGamePredictions
    for (int i = 0;
         i < std::min((int)det.box.class_confs.size(), yolo::MAX_TOP_PREDICTIONS);
         ++i) {
      float conf = det.box.class_confs[i].first;
      int class_id = det.box.class_confs[i].second;
      if (classNames.count(class_id)) {
        det.topGamePredictions.push_back({classNames.at(class_id), conf});
      }
    }

    if (classNames.count(det.box.cls)) {
      det.predictedGame = classNames.at(det.box.cls);
    } else {
      det.predictedGame = yolo::UNKNOWN_CLASS_NAME;
    }

    det.maskBinary = processMask(protos, protoH, protoW, maskCoeffs[idx],
                                 boxes[idx], originalImg.size());

    // Extract contours for visualization
    cv::findContours(det.maskBinary.clone(), det.maskContours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Extract quad from mask and dewarp
    if (!det.maskBinary.empty()) {
      // Downsample mask for faster quad extraction, then scale quad back up
      constexpr float scale = yolo::MASK_DOWNSAMPLE_SCALE;
      cv::Mat smallMask;
      cv::resize(det.maskBinary, smallMask, cv::Size(), scale, scale,
                 cv::INTER_NEAREST);

      auto quadSmall = quadFromMask(smallMask);

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

cv::Mat YoloSegmentationModel::visualize(
    const cv::Mat &img, const std::vector<Detection> &detections) const {
  cv::Mat result = img.clone();

  for (const auto &det : detections) {
    // Draw quadrilateral if available (GREEN - matching Python)
    if (det.quad.size() == 4) {
      std::vector<cv::Point> quadInt;
      for (const auto &p : det.quad) {
        quadInt.push_back(
            cv::Point(static_cast<int>(p.x), static_cast<int>(p.y)));
      }
      cv::polylines(result, quadInt, true, viz::QUAD_COLOR_GREEN,
                    viz::QUAD_LINE_THICKNESS);
    }

    // Draw bounding box (YELLOW for debugging)
    cv::rectangle(result, cv::Point(det.box.x1, det.box.y1),
                  cv::Point(det.box.x2, det.box.y2), viz::BBOX_COLOR_YELLOW,
                  viz::BBOX_LINE_THICKNESS);

    // Draw confidence
    std::string label =
        det.predictedGame + " " +
        std::to_string(static_cast<int>(det.box.conf * viz::PERCENT_MULTIPLIER)) +
        "%";
    cv::putText(result, label,
                cv::Point(det.box.x1, det.box.y1 + viz::LABEL_OFFSET_Y),
                cv::FONT_HERSHEY_SIMPLEX, viz::LABEL_FONT_SCALE,
                viz::BBOX_COLOR_YELLOW, viz::BBOX_LINE_THICKNESS);
  }

  return result;
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
YoloSegmentationModel::orientQuad(const std::vector<cv::Point2f> &quad) const {
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

  return {tl, tr, br, bl};
}

std::vector<cv::Point2f>
YoloSegmentationModel::quadFromMask(const cv::Mat &maskU8) const {
  // Use CHAIN_APPROX_SIMPLE for faster contour detection
  std::vector<std::vector<cv::Point>> contours;
  cv::Mat maskCopy = maskU8.clone();
  cv::findContours(maskCopy, contours, cv::RETR_EXTERNAL,
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
        return orientQuad(ordered);
      }
    }
  }

  // Fallback to minimum area rectangle
  cv::RotatedRect rect = cv::minAreaRect(hull);
  cv::Point2f vertices[4];
  rect.points(vertices);

  std::vector<cv::Point2f> quadF(vertices, vertices + 4);
  auto ordered = orderQuad(quadF);
  return orientQuad(ordered);
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

SegmentationResult YoloSegmentationModel::segment(const cv::Mat &image,
                                                  bool saveVisualization) {
  if (image.empty()) {
    throw std::runtime_error("Empty image provided to segment()");
  }

  // Preprocess
  cv::Mat letterboxed;
  std::vector<float> inputData = preprocess(image, letterboxed);

  // Run inference
  std::vector<int> inputShape = {1, model::EMBEDDING_CHANNELS, imgsz_, imgsz_};
  auto inputTensor = from_blob(inputData.data(), inputShape);

  auto result = module_->forward(inputTensor);

  if (!result.ok()) {
    throw std::runtime_error("Inference failed: " +
                             std::to_string(static_cast<int>(result.error())));
  }

  // Extract outputs (preds and protos)
  size_t numOutputs = result->size();

  auto predsTensor = result->at(0).toTensor();
  auto protosTensor = result->at(numOutputs - 1).toTensor();

  // Convert to vectors
  const float *predsData = predsTensor.const_data_ptr<float>();
  const float *protosData = protosTensor.const_data_ptr<float>();

  auto predsSizes = predsTensor.sizes();
  size_t predsSize = 1;
  for (auto s : predsSizes) {
    predsSize *= s;
  }

  auto protosSizes = protosTensor.sizes();
  size_t protosSize = 1;
  for (auto s : protosSizes) {
    protosSize *= s;
  }

  std::vector<float> preds(predsData, predsData + predsSize);
  std::vector<float> protos(protosData, protosData + protosSize);

  // Postprocess
  std::vector<Detection> detections =
      postprocess(image, letterboxed, preds, protos, classNames_);

  // Visualize
  if (!saveVisualization) {
    cv::Mat empty;
    return SegmentationResult{detections, empty};
  }

  cv::Mat visualized = visualize(image, detections);

  SegmentationResult segResult;
  segResult.detections = detections;
  segResult.visualizedImage = visualized;

  return segResult;
}

SegmentationResult YoloSegmentationModel::segment(const std::string &imagePath,
                                                  bool saveVisualization) {
  // Strip file:// prefix
  std::string cleanImagePath = imagePath;
  const std::string filePrefix = "file://";
  if (cleanImagePath.find(filePrefix) == 0) {
    cleanImagePath = cleanImagePath.substr(filePrefix.length());
  }

  // Load image
  cv::Mat img = cv::imread(cleanImagePath);

  if (img.empty()) {
    throw std::runtime_error("Failed to load image from: " + cleanImagePath);
  }
  // Call the cv::Mat version
  return segment(img, saveVisualization);
}

} // namespace rncardscanner
