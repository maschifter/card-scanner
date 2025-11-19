#include "YoloSegmentation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <iostream>
#include <opencv2/imgproc.hpp>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;

// Initialize static members
std::map<std::string, std::shared_ptr<Module>> YoloSegmentation::moduleCache;
std::mutex YoloSegmentation::cacheMutex;

// Get or load a module from cache
std::shared_ptr<Module>
YoloSegmentation::getModule(const std::string &modelPath) {
  // Strip "file://" prefix if present
  std::string cleanPath = modelPath;
  const std::string filePrefix = "file://";
  if (cleanPath.find(filePrefix) == 0) {
    cleanPath = cleanPath.substr(filePrefix.length());
  }

  std::lock_guard<std::mutex> lock(cacheMutex);

  // Check if module is already cached
  auto it = moduleCache.find(cleanPath);
  if (it != moduleCache.end()) {
    std::cout << "📦 Using cached YOLO module: " << cleanPath << std::endl;
    return it->second;
  }

  // Load new module
  std::cout << "🔄 Loading new YOLO module: " << cleanPath << std::endl;
  auto module = std::make_shared<Module>(
      cleanPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

  Error loadError = module->load();
  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load model: " +
                             std::to_string(static_cast<int>(loadError)));
  }

  // Cache the module
  moduleCache[cleanPath] = module;
  std::cout << "✅ YOLO module cached successfully" << std::endl;

  return module;
}

YoloSegmentation::YoloSegmentation(const std::string &modelPath, float conf,
                                   float iou, int imgsz)
    : modelPath_(modelPath), conf_(conf), iou_(iou), imgsz_(imgsz) {}

cv::Mat YoloSegmentation::letterbox(const cv::Mat &img, int newSize) {
  int height = img.rows;
  int width = img.cols;

  // Scale ratio (new / old)
  float r = std::min(static_cast<float>(newSize) / height,
                     static_cast<float>(newSize) / width);

  // Compute new unpadded dimensions
  int newUnpadW = std::round(width * r);
  int newUnpadH = std::round(height * r);

  // Compute padding
  float dw = (newSize - newUnpadW) / 2.0f;
  float dh = (newSize - newUnpadH) / 2.0f;

  // Resize if needed
  cv::Mat resized;
  if (height != newUnpadH || width != newUnpadW) {
    cv::resize(img, resized, cv::Size(newUnpadW, newUnpadH), 0, 0,
               cv::INTER_LINEAR);
  } else {
    resized = img.clone();
  }

  // Add padding
  int top = std::round(dh - 0.1f);
  int bottom = std::round(dh + 0.1f);
  int left = std::round(dw - 0.1f);
  int right = std::round(dw + 0.1f);

  cv::Mat padded;
  cv::copyMakeBorder(resized, padded, top, bottom, left, right,
                     cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

  return padded;
}

std::vector<float> YoloSegmentation::preprocess(const cv::Mat &img,
                                                cv::Mat &letterboxed) {
  // Letterbox resize
  letterboxed = letterbox(img, imgsz_);

  // BGR to RGB
  cv::Mat rgb;
  cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);

  // Convert to float and normalize to [0, 1]
  cv::Mat normalized;
  rgb.convertTo(normalized, CV_32FC3, 1.0 / 255.0);

  // Convert HWC to CHW and flatten to vector
  std::vector<float> inputData(1 * 3 * imgsz_ * imgsz_);

  for (int c = 0; c < 3; c++) {
    for (int h = 0; h < imgsz_; h++) {
      for (int w = 0; w < imgsz_; w++) {
        int chw_idx = c * imgsz_ * imgsz_ + h * imgsz_ + w;
        inputData[chw_idx] = normalized.at<cv::Vec3f>(h, w)[c];
      }
    }
  }

  return inputData;
}

std::vector<int> YoloSegmentation::nonMaxSuppression(
    const std::vector<BBox> &boxes,
    const std::vector<std::vector<float>> &maskCoeffs) {

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

cv::Mat YoloSegmentation::processMask(const std::vector<float> &protos,
                                      int protoH, int protoW,
                                      const std::vector<float> &maskCoeffs,
                                      const BBox &bbox,
                                      const cv::Size &imgSize) {

  int protoC = maskCoeffs.size();

  // Matrix multiplication: maskCoeffs @ protos
  std::vector<float> mask(protoH * protoW, 0.0f);

  for (int h = 0; h < protoH; h++) {
    for (int w = 0; w < protoW; w++) {
      float val = 0.0f;
      for (int c = 0; c < protoC; c++) {
        val += maskCoeffs[c] * protos[c * protoH * protoW + h * protoW + w];
      }
      mask[h * protoW + w] = 1.0f / (1.0f + std::exp(-val)); // sigmoid
    }
  }

  cv::Mat maskMat(protoH, protoW, CV_32F, mask.data());

  // Calculate bbox region with padding
  int bx1 = std::max(0, static_cast<int>(bbox.x1) - 10);
  int by1 = std::max(0, static_cast<int>(bbox.y1) - 10);
  int bx2 = std::min(imgSize.width, static_cast<int>(bbox.x2) + 10);
  int by2 = std::min(imgSize.height, static_cast<int>(bbox.y2) + 10);

  cv::Size roiSize(bx2 - bx1, by2 - by1);

  // Calculate letterbox transform
  float gain = std::min(static_cast<float>(protoH) / imgSize.height,
                        static_cast<float>(protoW) / imgSize.width);
  float pad_w = protoW - imgSize.width * gain;
  float pad_h = protoH - imgSize.height * gain;
  pad_w /= 2.0f;
  pad_h /= 2.0f;

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
  cv::threshold(croppedMask, binaryMask, 0.5f, 255, cv::THRESH_BINARY);
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
                     cv::Scalar(255), cv::FILLED);
    singleContourMask.copyTo(fullMask(roi));
  } else {
    // No contours found
    return cv::Mat();
  }

  return fullMask;
}

std::vector<Detection> YoloSegmentation::postprocess(
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
    std::cout << "No predictions from model" << std::endl;
    return {};
  }

  // YOLO output is [1, 37, 3024] -> flattened to [37 * 3024]
  // 37 = 4 (box) + 1 (conf) + 32 (mask coeffs)
  int numFeatures = 37;
  int numPredictions = 3024;

  std::cout << "Predictions size: " << preds.size()
            << ", Expected: " << (numFeatures * numPredictions)
            << ", numPredictions: " << numPredictions
            << ", numFeatures: " << numFeatures << std::endl;

  // Verify size matches
  if (preds.size() != numFeatures * numPredictions) {
    std::cerr << "Warning: preds size mismatch! Adjusting..." << std::endl;
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
    float x1 = x - w / 2.0f;
    float y1 = y - h / 2.0f;
    float x2 = x + w / 2.0f;
    float y2 = y + h / 2.0f;

    // Reverse letterbox transformation
    // Calculate the scale and padding that was applied
    float r = std::min(static_cast<float>(imgsz_) / originalImg.rows,
                       static_cast<float>(imgsz_) / originalImg.cols);

    // Calculate padding that was added
    int newUnpadW = std::round(originalImg.cols * r);
    int newUnpadH = std::round(originalImg.rows * r);
    float dw = (imgsz_ - newUnpadW) / 2.0f;
    float dh = (imgsz_ - newUnpadH) / 2.0f;

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
    for (int j = 5; j < 37; j++) { // features 5 through 36 (32 coeffs)
      coeffs.push_back(preds[j * numPredictions + i]);
    }
    maskCoeffs.push_back(coeffs);
  }

  auto parseEnd = std::chrono::high_resolution_clock::now();
  double parseMs = std::chrono::duration_cast<std::chrono::microseconds>(
                       parseEnd - postStart)
                       .count() /
                   1000.0;
  std::cout << "  ⏱️ Parse predictions: " << parseMs << "ms" << std::endl;

  // Apply NMS
  auto nmsStart = std::chrono::high_resolution_clock::now();
  std::vector<int> keep = nonMaxSuppression(boxes, maskCoeffs);
  auto nmsEnd = std::chrono::high_resolution_clock::now();
  double nmsMs =
      std::chrono::duration_cast<std::chrono::microseconds>(nmsEnd - nmsStart)
          .count() /
      1000.0;
  std::cout << "  ⏱️ NMS: " << nmsMs << "ms (kept " << keep.size()
            << " detections)" << std::endl;

  // Parse protos dimensions (typically [32, H, W] where H=W=96 or 160)
  int protoC = 32;
  int protoSize = protos.size() / protoC;
  int protoH = std::sqrt(protoSize);
  int protoW = protoH;

  // Process masks for kept detections
  std::vector<Detection> detections;
  double totalMaskMs = 0, totalQuadMs = 0, totalWarpMs = 0;

  for (int idx : keep) {
    Detection det;
    det.box = boxes[idx];

    auto maskStart = std::chrono::high_resolution_clock::now();
    det.maskBinary = processMask(protos, protoH, protoW, maskCoeffs[idx],
                                 boxes[idx], originalImg.size());
    auto maskEnd = std::chrono::high_resolution_clock::now();
    totalMaskMs += std::chrono::duration_cast<std::chrono::microseconds>(
                       maskEnd - maskStart)
                       .count() /
                   1000.0;

    // Extract contours for visualization
    cv::findContours(det.maskBinary.clone(), det.maskContours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Extract quad from mask and dewarp
    if (!det.maskBinary.empty()) {
      // Downsample mask for faster quad extraction, then scale quad back up
      auto quadStart = std::chrono::high_resolution_clock::now();

      float scale = 0.25f; // Downsample to 25% for speed
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

      auto quadEnd = std::chrono::high_resolution_clock::now();
      totalQuadMs += std::chrono::duration_cast<std::chrono::microseconds>(
                         quadEnd - quadStart)
                         .count() /
                     1000.0;

      // Dewarp if we have a valid quad
      if (det.quad.size() == 4) {
        auto warpStart = std::chrono::high_resolution_clock::now();
        det.dewarpedCard =
            warpPerspectiveCard(originalImg, det.quad, 640, 0.63f);
        auto warpEnd = std::chrono::high_resolution_clock::now();
        totalWarpMs += std::chrono::duration_cast<std::chrono::microseconds>(
                           warpEnd - warpStart)
                           .count() /
                       1000.0;
      }
    }

    detections.push_back(det);
  }

  auto postEnd = std::chrono::high_resolution_clock::now();
  double totalPostMs =
      std::chrono::duration_cast<std::chrono::microseconds>(postEnd - postStart)
          .count() /
      1000.0;

  std::cout << "  ⏱️ Postprocessing breakdown:" << std::endl;
  std::cout << "     - Mask generation: " << totalMaskMs << "ms" << std::endl;
  std::cout << "     - Quad extraction: " << totalQuadMs << "ms" << std::endl;
  std::cout << "     - Perspective warp: " << totalWarpMs << "ms" << std::endl;
  std::cout << "     - Total postprocessing: " << totalPostMs << "ms"
            << std::endl;

  return detections;
}

cv::Mat YoloSegmentation::visualize(const cv::Mat &img,
                                    const std::vector<Detection> &detections) {
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

    // Optionally draw mask contours (red, thin)
    // cv::drawContours(result, det.maskContours, -1, cv::Scalar(0, 0, 255), 1);
  }

  return result;
}

std::vector<cv::Point2f>
YoloSegmentation::orderQuad(const std::vector<cv::Point2f> &pts) {
  // Port of Python's order_quad:
  // Order as [TL, TR, BR, BL] using sum and diff
  // TL has min(x+y), BR has max(x+y)
  // TR has min(y-x), BL has max(y-x)

  if (pts.size() != 4) {
    std::cerr << "Warning: orderQuad expects exactly 4 points, got "
              << pts.size() << std::endl;
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

bool YoloSegmentation::isValidQuad(const std::vector<cv::Point2f> &quad) {
  if (quad.size() != 4)
    return false;

  // Check for degenerate points (too close together)
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (cv::norm(quad[i] - quad[j]) < 1.0f)
        return false;
    }
  }

  // Check area
  std::vector<cv::Point> intQuad;
  for (const auto &p : quad) {
    intQuad.push_back(cv::Point(p.x, p.y));
  }

  double area = cv::contourArea(intQuad);
  return area >= 5.0;
}

std::vector<cv::Point2f>
YoloSegmentation::orientQuad(const std::vector<cv::Point2f> &quad) {
  // Port of Python's _orient_quad_topmost_upright:
  // Find the edge with the smallest mid-y (topmost edge) and orient from there

  const float TOPMOST_TIE_TOL =
      2.0f; // tolerance for comparing edge mid-y values

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
    if (mids[i].y <= minMidY + TOPMOST_TIE_TOL) {
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

std::vector<cv::Point2f> YoloSegmentation::quadFromMask(const cv::Mat &maskU8) {
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
  std::vector<float> epsilonFracs = {0.02f, 0.03f, 0.015f, 0.04f, 0.01f, 0.05f,
                                     0.06f, 0.08f, 0.10f,  0.12f, 0.15f};

  for (float frac : epsilonFracs) {
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
YoloSegmentation::warpPerspectiveCard(const cv::Mat &img,
                                      const std::vector<cv::Point2f> &quad,
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

SegmentationResult YoloSegmentation::segment(const cv::Mat &image) {
  auto totalStart = std::chrono::high_resolution_clock::now();

  if (image.empty()) {
    throw std::runtime_error("Empty image provided to segment()");
  }

  cv::Mat img = image; // Use the provided image
  std::cout << "Processing image: " << img.cols << "x" << img.rows << std::endl;

  // Disable OpenCV threading to prevent interference with ExecutorTorch
  cv::setNumThreads(0);

  // Preprocess
  auto prepStart = std::chrono::high_resolution_clock::now();
  cv::Mat letterboxed;
  std::vector<float> inputData = preprocess(img, letterboxed);
  auto prepEnd = std::chrono::high_resolution_clock::now();
  double prepMs =
      std::chrono::duration_cast<std::chrono::microseconds>(prepEnd - prepStart)
          .count() /
      1000.0;
  std::cout << "⏱️ Preprocessing: " << prepMs << "ms" << std::endl;

  // Get cached module or load new one
  auto modelLoadStart = std::chrono::high_resolution_clock::now();
  auto module = getModule(modelPath_);
  auto modelLoadEnd = std::chrono::high_resolution_clock::now();
  double modelLoadMs = std::chrono::duration_cast<std::chrono::microseconds>(
                           modelLoadEnd - modelLoadStart)
                           .count() /
                       1000.0;
  std::cout << "⏱️ Model loading/cache lookup: " << modelLoadMs << "ms"
            << std::endl;

  // Run inference
  std::vector<int> inputShape = {1, 3, imgsz_, imgsz_};
  auto inputTensor = from_blob(inputData.data(), inputShape);

  auto startTime = std::chrono::high_resolution_clock::now();
  auto result = module->forward(inputTensor);
  auto endTime = std::chrono::high_resolution_clock::now();

  if (!result.ok()) {
    throw std::runtime_error("Inference failed: " +
                             std::to_string(static_cast<int>(result.error())));
  }

  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime);
  double inferenceTimeMs = duration.count() / 1000.0;

  // Extract outputs (preds and protos)
  // result is a Result type, use -> to access
  size_t numOutputs = result->size();
  std::cout << "Model has " << numOutputs << " outputs" << std::endl;

  auto predsTensor = result->at(0).toTensor();
  auto protosTensor = result->at(numOutputs - 1).toTensor();

  // Log tensor shapes
  auto predsSizes = predsTensor.sizes();
  std::cout << "Preds tensor shape: [";
  for (size_t i = 0; i < predsSizes.size(); i++) {
    std::cout << predsSizes[i];
    if (i < predsSizes.size() - 1)
      std::cout << ", ";
  }
  std::cout << "]" << std::endl;

  auto protosSizes = protosTensor.sizes();
  std::cout << "Protos tensor shape: [";
  for (size_t i = 0; i < protosSizes.size(); i++) {
    std::cout << protosSizes[i];
    if (i < protosSizes.size() - 1)
      std::cout << ", ";
  }
  std::cout << "]" << std::endl;

  // Convert to vectors
  auto tensorStart = std::chrono::high_resolution_clock::now();
  const float *predsData = predsTensor.const_data_ptr<float>();
  const float *protosData = protosTensor.const_data_ptr<float>();

  size_t predsSize = 1;
  for (auto s : predsSizes) {
    predsSize *= s;
  }

  size_t protosSize = 1;
  for (auto s : protosSizes) {
    protosSize *= s;
  }

  std::vector<float> preds(predsData, predsData + predsSize);
  std::vector<float> protos(protosData, protosData + protosSize);
  auto tensorEnd = std::chrono::high_resolution_clock::now();
  double tensorMs = std::chrono::duration_cast<std::chrono::microseconds>(
                        tensorEnd - tensorStart)
                        .count() /
                    1000.0;
  std::cout << "⏱️ Tensor extraction: " << tensorMs << "ms" << std::endl;

  // Postprocess
  auto postStart = std::chrono::high_resolution_clock::now();
  std::vector<Detection> detections =
      postprocess(img, letterboxed, preds, protos);
  auto postEnd = std::chrono::high_resolution_clock::now();
  double postMs =
      std::chrono::duration_cast<std::chrono::microseconds>(postEnd - postStart)
          .count() /
      1000.0;

  std::cout << "Found " << detections.size() << " detections" << std::endl;

  // Visualize
  auto vizStart = std::chrono::high_resolution_clock::now();
  cv::Mat visualized = visualize(img, detections);
  auto vizEnd = std::chrono::high_resolution_clock::now();
  double vizMs =
      std::chrono::duration_cast<std::chrono::microseconds>(vizEnd - vizStart)
          .count() /
      1000.0;
  std::cout << "⏱️ Visualization: " << vizMs << "ms" << std::endl;

  auto totalEnd = std::chrono::high_resolution_clock::now();
  double totalMs = std::chrono::duration_cast<std::chrono::microseconds>(
                       totalEnd - totalStart)
                       .count() /
                   1000.0;
  std::cout << "⏱️ TOTAL segment() time: " << totalMs << "ms" << std::endl;

  return {detections, inferenceTimeMs, prepMs, postMs, visualized};
}

SegmentationResult YoloSegmentation::segment(const std::string &imagePath) {
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
      1000.0;

  if (img.empty()) {
    throw std::runtime_error("Failed to load image from: " + cleanImagePath);
  }

  std::cout << "Loaded image: " << img.cols << "x" << img.rows << " in "
            << loadMs << "ms" << std::endl;

  // Call the cv::Mat version
  return segment(img);
}

} // namespace cardscanner
