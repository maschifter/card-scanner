#pragma once

#include "Constants.h"
#include "types/Detection.h"
#include "types/ScannerConfig.h"
#include "../inference/InferenceSession.h"
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {


/**
 * @class YoloSegmentationModel
 * @brief YOLO-seg instance segmentation model for card detection
 *
 * This model performs multi-task learning:
 * 1. Object detection - finds cards in the image
 * 2. Classification - identifies the card game type
 * 3. Instance segmentation - generates pixel-level masks
 *
 * Input: 384x384 RGB image (letterbox resized)
 * Outputs:
 *   - Predictions: [1, 4 + numClasses + 32, anchors], channels-first
 *   - Prototypes: [1, 32, H, W] (mask prototypes)
 *
 * Pipeline:
 * 1. Letterbox resize to 384x384 (preserves aspect ratio)
 * 2. Normalize to [0, 1]
 * 3. Run inference
 * 4. Apply NMS and drop group boxes to remove duplicate detections
 * 5. Generate segmentation masks from prototypes
 * 6. Extract quadrilateral corners from masks
 * 7. Dewarp cards to rectangular images
 *
 * Supported games come from the caller's class mapping, in the model's order.
 *
 * @note Model runs on CPU via ExecuTorch for cross-platform compatibility
 */
class YoloSegmentationModel {
public:
  /**
   * @brief Construct segmentation model from ExecuTorch .pte file
   *
   * @param modelPath Path to the .pte model file (supports file:// URIs)
   * @param classNames Class ID to game name(s), keys contiguous from 0
   * @param conf Confidence threshold for detection [0, 1] (default: 0.7)
   * @param iou IoU threshold for NMS [0, 1] (default: 0.7)
   * @param imgsz Input image size (square, default: 384)
   * @throws std::runtime_error if loading fails or classNames is invalid
   */
  explicit YoloSegmentationModel(
      const std::string &modelPath, const GameClassMap &classNames,
      float conf = constants::model::DEFAULT_YOLO_CONF_THRESHOLD,
      float iou = constants::model::DEFAULT_YOLO_IOU_THRESHOLD,
      int imgsz = constants::model::DEFAULT_YOLO_IMAGE_SIZE);

  /**
   * @brief Run segmentation on an image
   *
   * @param image Input image (any size, will be letterbox resized)
   * @return SegmentationResult with all detected cards
   */
  SegmentationResult segment(const cv::Mat &image);

private:
  std::unique_ptr<inference::InferenceSession> session_;
  float conf_;                     ///< Confidence threshold
  float iou_;                      ///< IoU threshold for NMS
  int imgsz_;                      ///< Input image size
  GameClassMap classNames_; ///< Class ID to game name(s)

  /**
   * @brief Preprocess image for model input
   * @param img Input image
   * @return Normalized CHW tensor data
   */
  std::vector<float> preprocess(const cv::Mat &img) const;

  /**
   * @brief Postprocess model outputs to detections
   * @param originalImg Original input image (for scaling back)
   * @param preds Prediction tensor [4 + numClasses + 32, anchors]
   * @param protos Prototype tensor [32, protoH, protoW]
   * @param protoH Prototype grid height, read from the tensor
   * @param protoW Prototype grid width, read from the tensor
   * @param classNames Class ID to game name(s)
   * @return Vector of complete detections
   */
  std::vector<Detection>
  postprocess(const cv::Mat &originalImg, const std::vector<float> &preds,
              const std::vector<float> &protos, int protoH, int protoW,
              const GameClassMap &classNames);

  /**
   * @brief Generate binary mask from prototypes and coefficients
   * @param protos Prototype tensor
   * @param protoH Prototype height
   * @param protoW Prototype width
   * @param maskCoeffs Mask coefficients for this detection
   * @param bbox Bounding box for cropping
   * @param imgSize Original image size
   * @return Binary mask (CV_8U)
   */
  cv::Mat processMask(const std::vector<float> &protos, int protoH, int protoW,
                      const std::vector<float> &maskCoeffs, const BBox &bbox,
                      const cv::Size &imgSize) const;

  /**
   * @brief Non-maximum suppression
   *
   * Suppresses by IoU and by mutual containment, so near-duplicate boxes of
   * one card are gone before any mask work.
   *
   * @param boxes Detected bounding boxes
   * @return Indices of boxes to keep, highest confidence first
   */
  std::vector<int> nonMaxSuppression(const std::vector<BBox> &boxes) const;

  /**
   * @brief Extract 4-point quadrilateral from segmentation mask
   * @param maskU8 Binary mask (CV_8U)
   * @param wasSideways Whether the quad needed the sideways 90-deg fixup
   * @return 4 corner points or empty if extraction fails
   */
  std::vector<cv::Point2f> quadFromMask(const cv::Mat &maskU8,
                                        bool *wasSideways = nullptr) const;

  /**
   * @brief Order quad points as [TL, TR, BR, BL]
   * @param pts Input 4 points (any order)
   * @return Ordered points
   */
  std::vector<cv::Point2f> orderQuad(const std::vector<cv::Point2f> &pts) const;

  /**
   * @brief Rotate quad so top edge is on top
   * @param quad Input quad [TL, TR, BR, BL]
   * @param wasSideways Whether the short-edge-as-top fixup fired
   * @return Oriented quad
   */
  std::vector<cv::Point2f>
  orientQuad(const std::vector<cv::Point2f> &quad,
             bool *wasSideways = nullptr) const;

  /**
   * @brief Check if quad is valid (not degenerate)
   * @param quad Quadrilateral points
   * @return true if valid, false otherwise
   */
  bool isValidQuad(const std::vector<cv::Point2f> &quad) const;

  /**
   * @brief Apply perspective transform to dewarp card
   * @param img Input image
   * @param quad 4 corner points [TL, TR, BR, BL]
   * @param targetH Target height in pixels (default: 384)
   * @param aspect Width/height ratio (default: 0.63 for trading cards)
   * @return Dewarped rectangular card image
   */
  cv::Mat warpPerspectiveCard(const cv::Mat &img,
                              const std::vector<cv::Point2f> &quad,
                              int targetH = 384, float aspect = 0.63f) const;
};

} // namespace cardscanner
