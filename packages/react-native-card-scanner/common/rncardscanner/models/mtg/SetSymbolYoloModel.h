#pragma once

#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;

// Simple bounding box for object detection
struct SetSymbolBBox {
  float x1, y1, x2, y2; // Absolute pixel coordinates
  float confidence;
};

// Result from YOLO object detection
struct SetSymbolDetectionResult {
  std::vector<SetSymbolBBox> detections;
};

/**
 * @class SetSymbolYoloModel
 * @brief Object detection model for MTG set symbols.
 *
 * Unlike YoloSegmentationModel, this is a simpler object detection model
 * that only returns bounding boxes (no segmentation masks).
 */
class SetSymbolYoloModel {
public:
  explicit SetSymbolYoloModel(const std::string &modelPath, float conf = 0.3f,
                              float iou = 0.7f, int imgsz = 384);

  // Run detection on an image file
  SetSymbolDetectionResult detect(const std::string &imagePath);

  // Run detection on a cv::Mat directly
  SetSymbolDetectionResult detect(const cv::Mat &image);

private:
  std::unique_ptr<Module> module_;
  float conf_;
  float iou_;
  int imgsz_;

  // Preprocessing: letterbox resize (same as segmentation model)
  cv::Mat letterbox(const cv::Mat &img, int newSize) const;

  // Preprocessing: prepare image for model
  std::vector<float> preprocess(const cv::Mat &img, cv::Mat &letterboxed) const;

  // Postprocessing: NMS and coordinate scaling
  std::vector<SetSymbolBBox>
  postprocess(const cv::Mat &originalImg, const cv::Mat &letterboxed,
              const std::vector<float> &preds,
              const std::vector<long long> &outputShape);

  // Non-max suppression
  std::vector<int>
  nonMaxSuppression(const std::vector<SetSymbolBBox> &boxes) const;

  // Compute IoU between two boxes
  float computeIoU(const SetSymbolBBox &a, const SetSymbolBBox &b) const;
};

} // namespace cardscanner
