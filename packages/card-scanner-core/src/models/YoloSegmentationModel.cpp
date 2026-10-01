#include "YoloSegmentationModel.h"
#include "../Constants.h"
#include "../utils/BoxGeometry.h"
#include "../utils/QuadGeometry.h"
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

cv::Mat YoloSegmentationModel::processMask(std::span<const float> protos,
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

  // The mask is only ever read at MASK_DOWNSAMPLE_SCALE (quadFromMask), so
  // it is built at that scale rather than at frame resolution and shrunk
  // afterwards. At 12 Mpx the full-size route was ~55 MB of transient
  // buffers per detection and dominated the YOLO stage.
  constexpr float scale = yolo::MASK_DOWNSAMPLE_SCALE;
  const cv::Size maskSize(std::max(1, static_cast<int>(std::lround(imgSize.width * scale))),
                          std::max(1, static_cast<int>(std::lround(imgSize.height * scale))));
  const int mx1 = std::min(maskSize.width - 1, static_cast<int>(std::lround(bx1 * scale)));
  const int my1 = std::min(maskSize.height - 1, static_cast<int>(std::lround(by1 * scale)));
  const int mx2 = std::min(maskSize.width, static_cast<int>(std::lround(bx2 * scale)));
  const int my2 = std::min(maskSize.height, static_cast<int>(std::lround(by2 * scale)));
  if (mx2 <= mx1 || my2 <= my1) {
    return cv::Mat();
  }
  cv::Size roiSize(mx2 - mx1, my2 - my1);

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

  cv::Rect roi(mx1, my1, roiSize.width, roiSize.height);

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

  cv::Mat fullMask = cv::Mat::zeros(maskSize, CV_8U);

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
    const cv::Mat &originalImg, std::span<const float> preds,
    std::span<const float> protos, int protoH, int protoW,
    const GameClassMap &classNames, float conf, bool bestFitQuads) {
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
  // segment() already matched numFeatures against the tensor's channel dim
  int numPredictions = preds.size() / numFeatures;

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

    if (maxConf < conf)
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
  // Suppresses by IoU and by mutual containment, so near-duplicate boxes of
  // one card are gone before any mask work.
  std::vector<int> keep = BoxGeometry::nonMaxSuppression(
      boxes, [](const BBox &b) { return b.conf; },
      [this](const BBox &a, const BBox &b) {
        return BoxGeometry::intersectionOverUnion(a, b) > iou_ ||
               BoxGeometry::mutuallyContained(
                   a, b, selection::CONTAINED_MIN_AREA_FRAC);
      });
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

    // Extract quad from mask. The mask already is at MASK_DOWNSAMPLE_SCALE
    // (see processMask); the quad scales back up. The pipeline dewarps the
    // cards it keeps.
    if (!det.maskBinary.empty()) {
      constexpr float scale = yolo::MASK_DOWNSAMPLE_SCALE;
      const auto [quadSmall, sideways] =
          utils::quadFromMask(det.maskBinary, bestFitQuads);
      det.quadWasSideways = sideways;

      // Scale quad coordinates back to full resolution
      if (quadSmall.size() == 4) {
        det.quad.resize(4);
        for (int i = 0; i < 4; i++) {
          det.quad[i].x = quadSmall[i].x / scale;
          det.quad[i].y = quadSmall[i].y / scale;
        }
      }
    }

    detections.push_back(det);
  }

  return detections;
}

SegmentationResult YoloSegmentationModel::segment(const cv::Mat &image,
                                                  std::optional<float> conf,
                                                  bool bestFitQuads) {
  if (image.empty()) {
    throw std::runtime_error("Empty image provided to segment()");
  }

  // Preprocess
  std::vector<float> inputData = preprocess(image);

  // Run inference
  auto outputs =
      session_->run(inputData.data(), {1, model::RGB_CHANNELS, imgsz_, imgsz_});

  // Segmentation head emits predictions first and prototypes last.
  if (outputs.size() < 2) {
    throw std::runtime_error("Segmentation model returned " +
                             std::to_string(outputs.size()) +
                             " outputs, expected at least 2");
  }
  // The mapping's entry count is the decode stride, so it has to equal the
  // model's class count exactly. Same count in a different order still passes.
  const auto &predsShape = outputs.front().shape;
  if (predsShape.size() < 2) {
    throw std::runtime_error(
        "Prediction tensor has " + std::to_string(predsShape.size()) +
        " dimensions, expected at least 2 ([.., 4 + classes + 32, anchors])");
  }
  const int64_t channels = predsShape[predsShape.size() - 2];
  const int64_t expectedChannels = yolo::BOX_FEATURES +
                                   static_cast<int64_t>(classNames_.size()) +
                                   yolo::MASK_COEFFS;
  if (channels != expectedChannels) {
    throw std::runtime_error(
        "Segmentation model emits " + std::to_string(channels) +
        " channels per anchor, gameClassMapping expects " +
        std::to_string(expectedChannels) + " (4 box + " +
        std::to_string(classNames_.size()) +
        " classes + 32 mask coeffs). The mapping's entry count disagrees "
        "with the model's class count.");
  }
  const std::span<const float> preds = outputs.front().data;
  const auto &protosOutput = outputs.back();
  const std::span<const float> protos = protosOutput.data;

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
      postprocess(image, preds, protos, protoH, protoW, classNames_,
                  conf.value_or(conf_), bestFitQuads);

  return detections;
}

} // namespace cardscanner
