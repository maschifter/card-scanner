#pragma once

#include "../types/ScannerConfig.h"
#include "../types/Detection.h"
#include "PathUtils.h"
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

namespace cardscanner {
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
   * @brief Load an image from disk as RGB (handles a file:// prefix)
   *
   * @param imagePath Path to the image
   * @return Loaded image in RGB order
   * @throws std::runtime_error if the file cannot be read
   */
  static cv::Mat loadImageRGB(const std::string &imagePath) {
    const std::string cleanedPath = PathUtils::stripFilePrefix(imagePath);
    cv::Mat image = cv::imread(cleanedPath);
    if (image.empty()) {
      throw std::runtime_error("Failed to load image from: " + cleanedPath);
    }

    cv::Mat imageRGB;
    cv::cvtColor(image, imageRGB, cv::COLOR_BGR2RGB);
    return imageRGB;
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
      compression_params.push_back(ScannerConfig::JPEG_QUALITY);

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

  /**
   * @brief Calculate blur score of an image using Laplacian variance
   *
   * Uses the variance of the Laplacian operator to measure image sharpness.
   * Higher values indicate sharper images. Typical thresholds:
   * - < 100: Very blurry
   * - 100-200: Moderately blurry
   * - > 200: Sharp
   *
   * @param image Input image (grayscale or color)
   * @return Blur score (higher = sharper)
   */
  static double calculateBlurScore(const cv::Mat &image) {
    if (image.empty()) {
      return 0.0;
    }

    cv::Mat gray;
    if (image.channels() == 3) {
      cv::cvtColor(image, gray, cv::COLOR_RGB2GRAY);
    } else {
      gray = image;
    }

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);

    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);

    // Variance = stddev^2
    double variance = stddev[0] * stddev[0];
    return variance;
  }

  // 1. Detect if the RGB image is too dark
  static bool isLowLight(const cv::Mat &image, double threshold = 65.0) {
    if (image.empty())
      return false;

    cv::Mat gray;
    // IMPORTANT: Use COLOR_RGB2GRAY since your input is RGB
    if (image.channels() == 3) {
      cv::cvtColor(image, gray, cv::COLOR_RGB2GRAY);
    } else {
      gray = image;
    }

    // Calculate average intensity (Luminance)
    cv::Scalar meanIntensity = cv::mean(gray);

    // If average luminance is below threshold, it is considered dark
    return meanIntensity[0] < threshold;
  }

  // 2. Brighten image using Gamma Correction (Works for RGB or BGR equally)
  static cv::Mat adjustGamma(const cv::Mat &image, double gamma) {
    cv::Mat lookUpTable(1, 256, CV_8U);
    uchar *p = lookUpTable.ptr();

    for (int i = 0; i < 256; ++i) {
      // Formula: ((i / 255.0) ^ (1.0 / gamma)) * 255.0
      p[i] = cv::saturate_cast<uchar>(pow(i / 255.0, 1.0 / gamma) * 255.0);
    }

    cv::Mat res = image.clone();

    // LUT applies the same table to all 3 channels (R, G, B) automatically
    cv::LUT(image, lookUpTable, res);
    return res;
  }

  /**
   * @brief Get file size in bytes
   *
   * @param filePath Path to file
   * @return File size in bytes, or -1 if file doesn't exist
   */
  static long getFileSize(const std::string &filePath) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(filePath, ec);
    return ec ? -1 : static_cast<long>(size);
  }
};

} // namespace utils
} // namespace cardscanner
