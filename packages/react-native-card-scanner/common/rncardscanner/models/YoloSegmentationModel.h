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

/**
 * @struct BBox
 * @brief Bounding box with classification results
 *
 * Represents a detected object's location and classification confidence.
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
 * @brief Complete detection result for a single card
 *
 * Contains all information about a detected card including bounding box,
 * segmentation mask, quadrilateral corners, and dewarped card image.
 */
struct Detection {
  BBox box;           ///< Bounding box with classification
  cv::Mat maskBinary; ///< Binary segmentation mask (CV_8U, full resolution)
  std::vector<std::vector<cv::Point>>
      maskContours; ///< Mask contours for visualization
  std::vector<cv::Point2f> quad; ///< 4-point quad: [TL, TR, BR, BL]
  cv::Mat dewarpedCard;          ///< Perspective-corrected card image
  std::string predictedGame;     ///< Top predicted game (e.g., "mtg", "pokemon")
  std::vector<std::pair<std::string, float>>
      topGamePredictions; ///< Top 3 games with confidence scores
};

/**
 * @struct SegmentationResult
 * @brief Result from YOLO segmentation inference
 *
 * Contains all detected cards and optionally a visualization image.
 */
struct SegmentationResult {
  std::vector<Detection> detections; ///< All detected cards
  cv::Mat visualizedImage; ///< Image with detections drawn (if requested)
};

/**
 * @class YoloSegmentationModel
 * @brief YOLO11-seg instance segmentation model for card detection
 *
 * This model performs multi-task learning:
 * 1. Object detection - finds cards in the image
 * 2. Classification - identifies the card game type
 * 3. Instance segmentation - generates pixel-level masks
 *
 * Model Architecture: YOLO11-seg
 * Input: 384x384 RGB image (letterbox resized)
 * Outputs:
 *   - Predictions: [1, 44, 3024] (bbox + class + mask coeffs)
 *   - Prototypes: [1, 32, H, W] (mask prototypes)
 *
 * Pipeline:
 * 1. Letterbox resize to 384x384 (preserves aspect ratio)
 * 2. Normalize to [0, 1]
 * 3. Run inference
 * 4. Apply NMS to remove duplicate detections
 * 5. Generate segmentation masks from prototypes
 * 6. Extract quadrilateral corners from masks
 * 7. Dewarp cards to rectangular images
 *
 * Supported Games: fab, lorcana, mtg, onepiece, pokemon, riftbound, rise,
 * sorcery
 *
 * @note Model runs on CPU via ExecuTorch for cross-platform compatibility
 */
class YoloSegmentationModel {
public:
  /**
   * @brief Construct segmentation model from ExecuTorch .pte file
   *
   * @param modelPath Path to the .pte model file (supports file:// URIs)
   * @param conf Confidence threshold for detection [0, 1] (default: 0.7)
   * @param iou IoU threshold for NMS [0, 1] (default: 0.7)
   * @param imgsz Input image size (square, default: 384)
   * @throws std::runtime_error if model loading fails
   */
  explicit YoloSegmentationModel(const std::string &modelPath,
                                 float conf = 0.7f, float iou = 0.7f,
                                 int imgsz = 384);

  /**
   * @brief Run segmentation on an image file
   *
   * Loads image from disk and performs complete segmentation pipeline.
   *
   * @param imagePath Path to image file
   * @param saveVisualization If true, saves visualization to disk
   * @return SegmentationResult with all detected cards
   */
  SegmentationResult segment(const std::string &imagePath,
                             bool saveVisualization = false);

  /**
   * @brief Run segmentation on a cv::Mat directly
   *
   * Performs complete segmentation pipeline on pre-loaded image.
   * More efficient than file-based version when image is already in memory.
   *
   * @param image Input image (any size, will be letterbox resized)
   * @param saveVisualization If true, saves visualization to disk
   * @return SegmentationResult with all detected cards
   */
  SegmentationResult segment(const cv::Mat &image,
                             bool saveVisualization = false);

private:
  std::unique_ptr<Module> module_; ///< ExecuTorch model instance
  float conf_;                     ///< Confidence threshold
  float iou_;                      ///< IoU threshold for NMS
  int imgsz_;                      ///< Input image size

  /**
   * @brief Letterbox resize preserving aspect ratio
   * @param img Input image
   * @param newSize Target size (square)
   * @return Letterboxed image with gray padding
   */
  cv::Mat letterbox(const cv::Mat &img, int newSize) const;

  /**
   * @brief Preprocess image for model input
   * @param img Input image
   * @param letterboxed Output letterboxed image
   * @return Normalized CHW tensor data
   */
  std::vector<float> preprocess(const cv::Mat &img, cv::Mat &letterboxed) const;

  /**
   * @brief Postprocess model outputs to detections
   * @param originalImg Original input image (for scaling back)
   * @param letterboxed Letterboxed image
   * @param preds Prediction tensor [44, 3024]
   * @param protos Prototype tensor [32, H, W]
   * @param classNames Map of class ID to game name
   * @return Vector of complete detections
   */
  std::vector<Detection>
  postprocess(const cv::Mat &originalImg, const cv::Mat &letterboxed,
              const std::vector<float> &preds, const std::vector<float> &protos,
              const std::map<int, std::string> &classNames);

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
   * @param boxes Detected bounding boxes
   * @param maskCoeffs Mask coefficients (parallel to boxes)
   * @return Indices of boxes to keep
   */
  std::vector<int>
  nonMaxSuppression(const std::vector<BBox> &boxes,
                    const std::vector<std::vector<float>> &maskCoeffs) const;

  /**
   * @brief Draw detections on image
   * @param img Input image
   * @param detections Detected cards
   * @return Image with visualizations
   */
  cv::Mat visualize(const cv::Mat &img,
                    const std::vector<Detection> &detections) const;

  /**
   * @brief Extract 4-point quadrilateral from segmentation mask
   * @param maskU8 Binary mask (CV_8U)
   * @return 4 corner points or empty if extraction fails
   */
  std::vector<cv::Point2f> quadFromMask(const cv::Mat &maskU8) const;

  /**
   * @brief Order quad points as [TL, TR, BR, BL]
   * @param pts Input 4 points (any order)
   * @return Ordered points
   */
  std::vector<cv::Point2f> orderQuad(const std::vector<cv::Point2f> &pts) const;

  /**
   * @brief Rotate quad so top edge is on top
   * @param quad Input quad [TL, TR, BR, BL]
   * @return Oriented quad
   */
  std::vector<cv::Point2f>
  orientQuad(const std::vector<cv::Point2f> &quad) const;

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
