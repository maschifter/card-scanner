#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

namespace executorch {
namespace extension {
namespace module {
class Module;
}
}
}

namespace cardscanner {

// Bounding box with confidence and class
struct BBox {
  float x1, y1, x2, y2; // coordinates
  float conf;            // confidence score
  int cls;               // class id
};

// Segmentation result for a single detection
struct Detection {
  BBox box;
  cv::Mat maskBinary; // binary mask (CV_8U, full resolution)
  std::vector<std::vector<cv::Point>> maskContours; // mask contours for visualization
  std::vector<cv::Point2f> quad; // 4-point quadrilateral (TL, TR, BR, BL)
  cv::Mat dewarpedCard; // perspective-corrected card image
};

// Result from YOLO11 segmentation
struct SegmentationResult {
  std::vector<Detection> detections;
  double inferenceTimeMs;
  cv::Mat visualizedImage; // image with boxes and masks drawn
};

class YoloSegmentation {
public:
  YoloSegmentation(const std::string &modelPath, float conf = 0.25f,
                   float iou = 0.7f, int imgsz = 640);

  // Run segmentation on an image file
  SegmentationResult segment(const std::string &imagePath);

  // Run segmentation on a cv::Mat directly (avoids image loading)
  SegmentationResult segment(const cv::Mat &image);

private:
  std::string modelPath_;
  float conf_;
  float iou_;
  int imgsz_;

  // Module cache to avoid reloading models (shared across instances)
  static std::map<std::string, std::shared_ptr<executorch::extension::module::Module>> moduleCache;
  static std::mutex cacheMutex;

  // Get or load a module from cache
  static std::shared_ptr<executorch::extension::module::Module> getModule(const std::string &modelPath);

  // Preprocessing: letterbox resize
  cv::Mat letterbox(const cv::Mat &img, int newSize);

  // Preprocessing: prepare image for model
  std::vector<float> preprocess(const cv::Mat &img, cv::Mat &letterboxed);

  // Postprocessing: NMS and mask processing
  std::vector<Detection> postprocess(const cv::Mat &originalImg,
                                      const cv::Mat &letterboxed,
                                      const std::vector<float> &preds,
                                      const std::vector<float> &protos);

  // Process masks from proto coefficients - returns binary mask
  cv::Mat processMask(const std::vector<float> &protos, int protoH, int protoW,
                      const std::vector<float> &maskCoeffs, const BBox &bbox,
                      const cv::Size &imgSize);

  // Non-max suppression
  std::vector<int> nonMaxSuppression(const std::vector<BBox> &boxes,
                                      const std::vector<std::vector<float>> &maskCoeffs);

  // Visualize results on image
  cv::Mat visualize(const cv::Mat &img,
                    const std::vector<Detection> &detections);

  // Extract quadrilateral from mask
  std::vector<cv::Point2f> quadFromMask(const cv::Mat &maskU8);

  // Order quad points as [TL, TR, BR, BL]
  std::vector<cv::Point2f> orderQuad(const std::vector<cv::Point2f> &pts);

  // Orient quad so topmost edge is on top
  std::vector<cv::Point2f> orientQuad(const std::vector<cv::Point2f> &quad);

  // Check if quad is valid (non-degenerate)
  bool isValidQuad(const std::vector<cv::Point2f> &quad);

  // Dewarp card to rectangular image
  // aspect = width/height (default 0.63 matches Python implementation)
  cv::Mat warpPerspectiveCard(const cv::Mat &img,
                              const std::vector<cv::Point2f> &quad,
                              int targetH = 640, float aspect = 0.63f);
};

} // namespace cardscanner
