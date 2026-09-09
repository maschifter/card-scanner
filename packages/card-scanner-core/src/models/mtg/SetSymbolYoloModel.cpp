#include "SetSymbolYoloModel.h"
#include "../../Constants.h"
#include "../../utils/BoxGeometry.h"
#include "../../utils/YoloPreprocessing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <opencv2/imgproc.hpp>

namespace cardscanner {

using namespace constants;
using utils::BoxGeometry;

SetSymbolYoloModel::SetSymbolYoloModel(const std::string &modelPath, float conf,
                                       float iou, int imgsz)
    : conf_(conf), iou_(iou), imgsz_(imgsz) {
  session_ = inference::loadSession(modelPath);
}

std::vector<float> SetSymbolYoloModel::preprocess(const cv::Mat &img) const {
  return utils::YoloPreprocessing::preprocess(img, imgsz_);
}

std::vector<int> SetSymbolYoloModel::nonMaxSuppression(
    const std::vector<SetSymbolBBox> &boxes) const {

  std::vector<int> indices(boxes.size());
  for (size_t i = 0; i < boxes.size(); i++) {
    indices[i] = i;
  }

  // Sort by confidence descending
  std::sort(indices.begin(), indices.end(), [&boxes](int i1, int i2) {
    return boxes[i1].confidence > boxes[i2].confidence;
  });

  std::vector<int> keep;
  std::vector<bool> suppressed(boxes.size(), false);

  for (size_t i = 0; i < indices.size(); i++) {
    int idx = indices[i];
    if (suppressed[idx])
      continue;

    keep.push_back(idx);
    const SetSymbolBBox &box1 = boxes[idx];

    for (size_t j = i + 1; j < indices.size(); j++) {
      int idx2 = indices[j];
      if (suppressed[idx2])
        continue;

      const SetSymbolBBox &box2 = boxes[idx2];
      if (BoxGeometry::intersectionOverUnion(box1, box2) > iou_) {
        suppressed[idx2] = true;
      }
    }
  }

  return keep;
}

std::vector<SetSymbolBBox>
SetSymbolYoloModel::postprocess(const cv::Mat &originalImg,
                                std::span<const float> preds,
                                const std::vector<long long> &outputShape) {

  // YOLO output format for object detection can be:
  // Option 1: [batch, num_predictions, 5] = [1, 3024, 5] where each prediction
  // is [x, y, w, h, conf] Option 2: [batch, 5, num_predictions] = [1, 5, 3024]
  // (transposed)

  // Determine format from output shape
  bool isTransposed = false;
  int numPredictions = 0;
  const int predSize = yolo::SET_SYMBOL_PRED_SIZE; // x, y, w, h, conf

  if (outputShape.size() == 3) {
    // [batch, dim1, dim2]
    int dim1 = outputShape[1];
    int dim2 = outputShape[2];

    if (dim1 == 5) {
      // Format: [1, 5, num_predictions] - transposed
      isTransposed = true;
      numPredictions = dim2;
    } else if (dim2 == 5) {
      // Format: [1, num_predictions, 5] - standard
      isTransposed = false;
      numPredictions = dim1;
    } else {
      // Unknown format, fallback
      numPredictions = preds.size() / 5;
    }
  } else {
    // Fallback
    numPredictions = preds.size() / 5;
  }

  // Compute letterbox padding to reverse the transformation
  // The letterbox adds padding to maintain aspect ratio
  int origHeight = originalImg.rows;
  int origWidth = originalImg.cols;
  float r = std::min(static_cast<float>(imgsz_) / origHeight,
                     static_cast<float>(imgsz_) / origWidth);

  int newUnpadW = std::round(origWidth * r);
  int newUnpadH = std::round(origHeight * r);

  float dw = (imgsz_ - newUnpadW) / 2.0f;
  float dh = (imgsz_ - newUnpadH) / 2.0f;

  // Extract valid detections
  std::vector<SetSymbolBBox> boxes;

  for (int i = 0; i < numPredictions; i++) {
    float cx, cy, w, h, conf;

    if (isTransposed) {
      // Format: [1, 5, num_predictions]
      // Access as preds[channel * numPredictions + i]
      cx = preds[0 * numPredictions + i];
      cy = preds[1 * numPredictions + i];
      w = preds[2 * numPredictions + i];
      h = preds[3 * numPredictions + i];
      conf = preds[4 * numPredictions + i];
    } else {
      // Format: [1, num_predictions, 5]
      // Access as preds[i * 5 + channel]
      int offset = i * predSize;

      if (offset + 4 >= static_cast<int>(preds.size())) {
        std::cerr << "Warning: offset out of bounds at i=" << i << std::endl;
        break;
      }

      cx = preds[offset + 0];
      cy = preds[offset + 1];
      w = preds[offset + 2];
      h = preds[offset + 3];
      conf = preds[offset + 4];
    }

    if (conf < conf_)
      continue;

    // Remove padding offset and scale back to original image
    // YOLO outputs are in letterboxed image coordinates
    float x1_letterbox = cx - w / 2.0f;
    float y1_letterbox = cy - h / 2.0f;
    float x2_letterbox = cx + w / 2.0f;
    float y2_letterbox = cy + h / 2.0f;

    // Remove padding
    float x1_unpadded = x1_letterbox - dw;
    float y1_unpadded = y1_letterbox - dh;
    float x2_unpadded = x2_letterbox - dw;
    float y2_unpadded = y2_letterbox - dh;

    // Scale to original image size
    SetSymbolBBox bbox;
    bbox.x1 = x1_unpadded / r;
    bbox.y1 = y1_unpadded / r;
    bbox.x2 = x2_unpadded / r;
    bbox.y2 = y2_unpadded / r;
    bbox.confidence = conf;

    // Clamp to image bounds
    bbox.x1 =
        std::max(0.0f, std::min(bbox.x1, static_cast<float>(originalImg.cols)));
    bbox.y1 =
        std::max(0.0f, std::min(bbox.y1, static_cast<float>(originalImg.rows)));
    bbox.x2 =
        std::max(0.0f, std::min(bbox.x2, static_cast<float>(originalImg.cols)));
    bbox.y2 =
        std::max(0.0f, std::min(bbox.y2, static_cast<float>(originalImg.rows)));

    boxes.push_back(bbox);
  }

  // Apply NMS
  std::vector<int> keepIndices = nonMaxSuppression(boxes);

  std::vector<SetSymbolBBox> finalBoxes;
  for (int idx : keepIndices) {
    finalBoxes.push_back(boxes[idx]);
  }

  return finalBoxes;
}

SetSymbolDetectionResult SetSymbolYoloModel::detect(const cv::Mat &image) {
  SetSymbolDetectionResult result;

  // Preprocessing
  std::vector<float> inputData = preprocess(image);

  // Inference
  auto outputs = session_->run(inputData.data(), {1, 3, imgsz_, imgsz_});
  const auto &output = outputs.at(0);

  // postprocess sniffs [1,5,N] vs [1,N,5] from these dims
  const std::vector<long long> outputShape(output.shape.begin(),
                                           output.shape.end());
  const std::span<const float> preds = output.data;

  // Postprocessing
  result.detections = postprocess(image, preds, outputShape);
  return result;
}

} // namespace cardscanner
