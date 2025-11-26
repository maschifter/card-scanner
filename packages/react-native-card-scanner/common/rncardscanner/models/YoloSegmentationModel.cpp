#include "YoloSegmentationModel.h"
#include "../Constants.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <opencv2/imgproc.hpp>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;
using namespace constants;

YoloSegmentationModel::YoloSegmentationModel(const std::string &modelPath,
                                             float conf, float iou, int imgsz)
    : conf_(conf), iou_(iou), imgsz_(imgsz) {
  // Strip file:// prefix if present
  std::string cleanPath = modelPath;
  const std::string filePrefix = "file://";
  if (cleanPath.find(filePrefix) == 0) {
    cleanPath = cleanPath.substr(filePrefix.length());
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
  int height = img.rows;
  int width = img.cols;

  // Scale ratio (new / old)
  float r = std::min(static_cast<float>(newSize) / height,
                     static_cast<float>(newSize) / width);

  // Compute new unpadded dimensions
  int newUnpadW = std::round(width * r);
  int newUnpadH = std::round(height * r);

  // Compute padding
  float dw = (newSize - newUnpadW) / letterbox::PADDING_DIVISOR;
  float dh = (newSize - newUnpadH) / letterbox::PADDING_DIVISOR;

  // Resize if needed
  cv::Mat resized;
  if (height != newUnpadH || width != newUnpadW) {
    cv::resize(img, resized, cv::Size(newUnpadW, newUnpadH), 0, 0,
               cv::INTER_LINEAR);
  } else {
    resized = img;
  }

  // Add padding
  int top = std::round(dh - letterbox::PADDING_ADJUST_MINUS);
  int bottom = std::round(dh + letterbox::PADDING_ADJUST_PLUS);
  int left = std::round(dw - letterbox::PADDING_ADJUST_MINUS);
  int right = std::round(dw + letterbox::PADDING_ADJUST_PLUS);

  cv::Mat padded;
  cv::copyMakeBorder(resized, padded, top, bottom, left, right,
                     cv::BORDER_CONSTANT, yolo::LETTERBOX_PADDING_COLOR);

  return padded;
}

std::vector<float>
YoloSegmentationModel::preprocess(const cv::Mat &img,
                                  cv::Mat &letterboxed) const {
  // Letterbox resize
  letterboxed = letterbox(img, imgsz_);

  // Convert to float and normalize to [0, 1]
  cv::Mat normalized;
  letterboxed.convertTo(normalized, CV_32FC3,
                        matrix::SIGMOID_ONE / imagenet::PIXEL_SCALE);

  // Convert HWC to CHW and flatten to vector
  // Using direct pointer access for performance (3-5x faster than .at<>())
  const int channels = model::EMBEDDING_CHANNELS;
  std::vector<float> inputData(1 * channels * imgsz_ * imgsz_);
  const float *data = normalized.ptr<float>();
  size_t hw = imgsz_ * imgsz_;

  for (int c = 0; c < channels; c++) {
    for (int h = 0; h < imgsz_; h++) {
      const float *row = data + h * imgsz_ * channels;
      for (int w = 0; w < imgsz_; w++) {
        inputData[c * hw + h * imgsz_ + w] = row[w * channels + c];
      }
    }
  }

  return inputData;
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
    const std::vector<float> &preds, const std::vector<float> &protos) {

  auto postStart = std::chrono::high_resolution_clock::now();

  // Parse predictions tensor
  // Actual YOLO11 output shape: [1, 37, 3024] which is [batch, features,
  // predictions] Format: features = [x, y, w, h] (4) + [class_conf] (1) +
  // [mask_coeffs] (32) = 37 predictions = 3024 anchor points

  std::vector<BBox> boxes;
  std::vector<std::vector<float>> maskCoeffs;

  // Check if we have any predictions
  if (preds.empty()) {
    return {};
  }

  // YOLO output is [1, 37, 3024] -> flattened to [37 * 3024]
  // 37 = 4 (box) + 1 (conf) + 32 (mask coeffs)
  int numFeatures = yolo::YOLO11_FEATURES;
  int numPredictions = yolo::YOLO11_PREDICTIONS;

  // Verify size matches
  if (preds.size() != numFeatures * numPredictions) {
    numPredictions = preds.size() / numFeatures;
  }

  // Data is stored as [feature][prediction] not [prediction][feature]
  // So we need to access it transposed
  for (int i = 0; i < numPredictions; i++) {
    // Access transposed: feature_idx * numPredictions + prediction_idx
    float x = preds[0 * numPredictions + i];
    float y = preds[1 * numPredictions + i];
    float w = preds[2 * numPredictions + i];
    float h = preds[3 * numPredictions + i];
    float conf = preds[4 * numPredictions + i];

    if (conf < conf_)
      continue;

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
    box.conf = conf;
    box.cls = 0; // single class

    boxes.push_back(box);

    // Extract mask coefficients (features 5-36, which is 32 coefficients)
    // Remember data is transposed: [feature][prediction]
    std::vector<float> coeffs;
    for (int j = yolo::YOLO11_MASK_COEFF_START; j < yolo::YOLO11_MASK_COEFF_END;
         j++) {
      coeffs.push_back(preds[j * numPredictions + i]);
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
      cv::polylines(result, quadInt, true, cv::Scalar(0, 255, 0), 3);
    }

    // Draw bounding box (YELLOW for debugging)
    cv::rectangle(result, cv::Point(det.box.x1, det.box.y1),
                  cv::Point(det.box.x2, det.box.y2), cv::Scalar(0, 255, 255),
                  1);

    // Draw confidence
    std::string label =
        "card " + std::to_string(static_cast<int>(det.box.conf * 100)) + "%";
    cv::putText(result, label, cv::Point(det.box.x1, det.box.y1 - 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1);
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
      if (std::abs(mids[i1].y - mids[i2].y) < 0.01f) {
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

SegmentationResult YoloSegmentationModel::segment(const cv::Mat &image) {
  auto totalStart = std::chrono::high_resolution_clock::now();

  if (image.empty()) {
    throw std::runtime_error("Empty image provided to segment()");
  }

  // Disable OpenCV threading to prevent interference with ExecutorTorch
  cv::setNumThreads(0);

  // Preprocess
  auto prepStart = std::chrono::high_resolution_clock::now();
  cv::Mat letterboxed;
  std::vector<float> inputData = preprocess(image, letterboxed);
  auto prepEnd = std::chrono::high_resolution_clock::now();
  double prepMs =
      std::chrono::duration_cast<std::chrono::microseconds>(prepEnd - prepStart)
          .count() /
      perf::MICROSECONDS_TO_MILLISECONDS;

  // Run inference
  std::vector<int> inputShape = {1, model::EMBEDDING_CHANNELS, imgsz_, imgsz_};
  auto inputTensor = from_blob(inputData.data(), inputShape);

  auto startTime = std::chrono::high_resolution_clock::now();
  auto result = module_->forward(inputTensor);
  auto endTime = std::chrono::high_resolution_clock::now();

  if (!result.ok()) {
    throw std::runtime_error("Inference failed: " +
                             std::to_string(static_cast<int>(result.error())));
  }

  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime);
  double inferenceTimeMs =
      duration.count() / perf::MICROSECONDS_TO_MILLISECONDS;

  // Extract outputs (preds and protos)
  auto postStart = std::chrono::high_resolution_clock::now();
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
      postprocess(image, letterboxed, preds, protos);
  auto postEnd = std::chrono::high_resolution_clock::now();
  double postMs =
      std::chrono::duration_cast<std::chrono::microseconds>(postEnd - postStart)
          .count() /
      perf::MICROSECONDS_TO_MILLISECONDS;

  // Visualize
  auto vizStart = std::chrono::high_resolution_clock::now();
  cv::Mat visualized = visualize(image, detections);
  auto vizEnd = std::chrono::high_resolution_clock::now();
  double vizMs =
      std::chrono::duration_cast<std::chrono::microseconds>(vizEnd - vizStart)
          .count() /
      perf::MICROSECONDS_TO_MILLISECONDS;

  auto totalEnd = std::chrono::high_resolution_clock::now();
  double totalMs = std::chrono::duration_cast<std::chrono::microseconds>(
                       totalEnd - totalStart)
                       .count() /
                   perf::MICROSECONDS_TO_MILLISECONDS;

  SegmentationResult segResult;
  SegmentationPerformance performance;
  performance.totalTimeMs = totalMs;
  performance.preprocessingTimeMs = prepMs;
  performance.inferenceTimeMs = inferenceTimeMs;
  performance.postprocessingTimeMs = postMs;
  performance.visualizationTimeMs = vizMs;
  segResult.performance = performance;
  segResult.detections = detections;
  segResult.visualizedImage = visualized;

  return segResult;
}

SegmentationResult
YoloSegmentationModel::segment(const std::string &imagePath) {
  // Strip file:// prefix
  std::string cleanImagePath = imagePath;
  const std::string filePrefix = "file://";
  if (cleanImagePath.find(filePrefix) == 0) {
    cleanImagePath = cleanImagePath.substr(filePrefix.length());
  }

  // Load image
  auto loadStart = std::chrono::high_resolution_clock::now();
  cv::Mat img = cv::imread(cleanImagePath);
  auto loadEnd = std::chrono::high_resolution_clock::now();
  double loadMs =
      std::chrono::duration_cast<std::chrono::microseconds>(loadEnd - loadStart)
          .count() /
      perf::MICROSECONDS_TO_MILLISECONDS;

  if (img.empty()) {
    throw std::runtime_error("Failed to load image from: " + cleanImagePath);
  }
  // Call the cv::Mat version
  return segment(img);
}

} // namespace cardscanner
