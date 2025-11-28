#pragma once

#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;

// Bounding box with confidence and class
struct BBox {
  float x1, y1, x2, y2;
  float conf;
  int cls;
  std::vector<std::pair<float, int>> class_confs; // All class predictions sorted by confidence
};

// Segmentation result for a single detection
struct Detection {
  BBox box;
  cv::Mat maskBinary; // binary mask (CV_8U, full resolution)
  std::vector<std::vector<cv::Point>>
      maskContours;              // mask contours for visualization
  std::vector<cv::Point2f> quad; // 4-point quadrilateral (TL, TR, BR, BL)
  cv::Mat dewarpedCard;          // perspective-corrected card image
  std::string predictedGame;     // Top predicted game from YOLO classification
  std::vector<std::pair<std::string, float>> topGamePredictions; // Top 3 games with confidences
};

struct SegmentationPerformance {
  double totalTimeMs;
  double preprocessingTimeMs;
  double inferenceTimeMs;
  double postprocessingTimeMs;
  double visualizationTimeMs;
};

// Result from YOLO11 segmentation
struct SegmentationResult {
  std::vector<Detection> detections;
  cv::Mat visualizedImage; // image with boxes and masks drawn
  SegmentationPerformance performance;
};

class YoloSegmentationModel {
public:
  explicit YoloSegmentationModel(const std::string &modelPath,
                                 float conf = 0.7f, float iou = 0.7f,
                                 int imgsz = 384);

  // Run segmentation on an image file
  SegmentationResult segment(const std::string &imagePath);

  // Run segmentation on a cv::Mat directly (avoids image loading)
  SegmentationResult segment(const cv::Mat &image);

private:
  std::unique_ptr<Module> module_;
  float conf_;
  float iou_;
  int imgsz_;

  // Preprocessing: letterbox resize
  cv::Mat letterbox(const cv::Mat &img, int newSize) const;

  // Preprocessing: prepare image for model
  std::vector<float> preprocess(const cv::Mat &img, cv::Mat &letterboxed) const;

  // Postprocessing: NMS and mask processing
  std::vector<Detection> postprocess(const cv::Mat &originalImg,
                                     const cv::Mat &letterboxed,
                                     const std::vector<float> &preds,
                                     const std::vector<float> &protos,
                                     const std::map<int, std::string> &classNames);

  // Process masks from proto coefficients - returns binary mask
  cv::Mat processMask(const std::vector<float> &protos, int protoH, int protoW,
                      const std::vector<float> &maskCoeffs, const BBox &bbox,
                      const cv::Size &imgSize) const;

  // Non-max suppression
  std::vector<int>
  nonMaxSuppression(const std::vector<BBox> &boxes,
                    const std::vector<std::vector<float>> &maskCoeffs) const;

  // Visualize results on image
  cv::Mat visualize(const cv::Mat &img,
                    const std::vector<Detection> &detections) const;

  // Extract quadrilateral from mask
  std::vector<cv::Point2f> quadFromMask(const cv::Mat &maskU8) const;

  // Order quad points as [TL, TR, BR, BL]
  std::vector<cv::Point2f> orderQuad(const std::vector<cv::Point2f> &pts) const;

  // Orient quad so topmost edge is on top
  std::vector<cv::Point2f>
  orientQuad(const std::vector<cv::Point2f> &quad) const;

  // Check if quad is valid (non-degenerate)
  bool isValidQuad(const std::vector<cv::Point2f> &quad) const;

  // Dewarp card to rectangular image
  // aspect = width/height (default 0.63 matches Python implementation)
  cv::Mat warpPerspectiveCard(const cv::Mat &img,
                              const std::vector<cv::Point2f> &quad,
                              int targetH = 384, float aspect = 0.63f) const;
};

} // namespace cardscanner
