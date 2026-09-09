#pragma once

#include "../../inference/InferenceSession.h"
#include "../../types/SetSymbolDetection.h"
#include <memory>
#include <opencv2/opencv.hpp>
#include <span>
#include <string>
#include <vector>

namespace cardscanner {


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

  // Run detection on a cv::Mat
  SetSymbolDetectionResult detect(const cv::Mat &image);

private:
  std::unique_ptr<inference::InferenceSession> session_;
  float conf_;
  float iou_;
  int imgsz_;

  // Preprocessing: prepare image for model
  std::vector<float> preprocess(const cv::Mat &img) const;

  // Postprocessing: NMS and coordinate scaling
  std::vector<SetSymbolBBox>
  postprocess(const cv::Mat &originalImg, std::span<const float> preds,
              const std::vector<long long> &outputShape);

  // Non-max suppression
  std::vector<int>
  nonMaxSuppression(const std::vector<SetSymbolBBox> &boxes) const;
};

} // namespace cardscanner
