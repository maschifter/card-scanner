#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <utility>
#include <vector>

namespace cardscanner {

/**
 * @struct BBox
 * @brief An axis-aligned detection box with its class scores.
 */
struct BBox {
  float x1, y1, x2, y2; ///< Bounding box coordinates (xyxy format)
  float conf;           ///< Detection confidence score [0, 1]
  int cls;              ///< Predicted class ID
  std::vector<std::pair<float, int>>
      class_confs; ///< All class predictions (confidence, class_id) sorted desc
};

/**
 * @struct Detection
 * @brief One detected card: box, mask, quad and the class the model picked.
 *
 * Lives in types/ rather than alongside the segmentation model so that lower
 * layers (utils/) can name it without depending upward on models/.
 */
struct Detection {
  BBox box;           ///< Bounding box with classification
  cv::Mat maskBinary; ///< Binary segmentation mask (CV_8U, full resolution)
  std::vector<cv::Point2f> quad; ///< 4-point quad: [TL, TR, BR, BL]
  cv::Mat dewarpedCard;          ///< Perspective-corrected card image
  bool quadWasSideways = false;  ///< Dewarp direction ambiguous by 180 deg
  std::string predictedGame;     ///< Canonical name of the top predicted class
};

/**
 * @struct SegmentationResult
 * @brief Everything one segmentation pass found in a frame.
 */
struct SegmentationResult {
  std::vector<Detection> detections; ///< All detected cards
};

} // namespace cardscanner
