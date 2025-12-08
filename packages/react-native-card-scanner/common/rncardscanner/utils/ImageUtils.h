#pragma once

#include "../dto/ScannerConfig.h"
#include <YoloSegmentationModel.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace rncardscanner {
namespace utils {

/**
 * @class ImageUtils
 * @brief Pure utility functions for image operations
 */
class ImageUtils {
public:
  /**
   * @brief Extract card image from frame using detection bounding box
   *
   * Prefers dewarped image if available, otherwise crops from bounding box.
   *
   * @param frameImage Original frame (RGB)
   * @param detection YOLO detection with bounding box and optional dewarped
   * image
   * @return Cropped card image (empty if invalid bounds)
   */
  static cv::Mat extractCardImage(const cv::Mat &frameImage,
                                  const cardscanner::Detection &detection) {
    // Prefer dewarped card if available
    if (!detection.dewarpedCard.empty()) {
      return detection.dewarpedCard.clone();
    }

    // Crop from bounding box
    int ix1 = std::max(0, static_cast<int>(detection.box.x1));
    int iy1 = std::max(0, static_cast<int>(detection.box.y1));
    int ix2 = std::min(frameImage.cols, static_cast<int>(detection.box.x2));
    int iy2 = std::min(frameImage.rows, static_cast<int>(detection.box.y2));

    if (ix2 > ix1 && iy2 > iy1) {
      cv::Rect roi(ix1, iy1, ix2 - ix1, iy2 - iy1);
      return frameImage(roi).clone();
    }

    return cv::Mat(); // Empty if invalid
  }

  /**
   * @brief Save card image to disk as JPEG
   *
   * Generates unique filename with timestamp and converts RGB to BGR for
   * correct color encoding.
   *
   * @param cardImage Image to save (RGB format)
   * @param cacheDir Directory to save to
   * @param index Card index for unique naming
   * @return File path if successful, empty string otherwise
   */
  static std::string saveCardImage(const cv::Mat &cardImage,
                                   const std::string &cacheDir, size_t index) {
    if (cardImage.empty()) {
      return "";
    }

    try {
      // Generate unique filename with timestamp
      auto now = std::chrono::system_clock::now();
      auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now.time_since_epoch())
                           .count();

      std::string filename = "card_" + std::to_string(timestamp) + "_" +
                             std::to_string(index) + ".jpg";
      std::string imagePath = cacheDir + "/" + filename;

      // Convert RGB to BGR for correct color display
      cv::Mat cardImgBGR;
      cv::cvtColor(cardImage, cardImgBGR, cv::COLOR_RGB2BGR);

      // Save image as JPEG
      std::vector<int> compression_params;
      compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
      compression_params.push_back(dto::ScannerConfig::JPEG_QUALITY);

      bool success = cv::imwrite(imagePath, cardImgBGR, compression_params);
      return success ? imagePath : "";
    } catch (const std::exception &e) {
      return "";
    }
  }

  /**
   * @brief Crop region from image with bounds checking
   *
   * @param image Source image
   * @param roi Region of interest
   * @return Cropped image (empty if invalid bounds)
   */
  static cv::Mat cropRegion(const cv::Mat &image, const cv::Rect &roi) {
    if (image.empty()) {
      return cv::Mat();
    }

    int x1 = std::max(0, roi.x);
    int y1 = std::max(0, roi.y);
    int x2 = std::min(image.cols, roi.x + roi.width);
    int y2 = std::min(image.rows, roi.y + roi.height);

    if (x2 > x1 && y2 > y1) {
      cv::Rect clampedRoi(x1, y1, x2 - x1, y2 - y1);
      return image(clampedRoi).clone();
    }

    return cv::Mat();
  }

  /**
   * @brief Convert bounding box to cv::Rect
   *
   * @param box YOLO bounding box
   * @return OpenCV rectangle
   */
  static cv::Rect boundingBoxToRect(const cardscanner::BBox &box) {
    int x = static_cast<int>(box.x1);
    int y = static_cast<int>(box.y1);
    int width = static_cast<int>(box.x2 - box.x1);
    int height = static_cast<int>(box.y2 - box.y1);
    return cv::Rect(x, y, width, height);
  }
};

} // namespace utils
} // namespace rncardscanner
