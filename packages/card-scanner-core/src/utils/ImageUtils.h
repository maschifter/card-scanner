#pragma once

#include "../Constants.h"
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
      return saveImageRGB(cardImage, cacheDir + "/" + filename);
    } catch (const std::exception &e) {
      return "";
    }
  }

  /**
   * @brief Writes an RGB image as JPEG to an exact path, overwriting.
   * @return The path if written, empty string otherwise
   */
  static std::string saveImageRGB(const cv::Mat &image,
                                  const std::string &imagePath) {
    if (image.empty()) {
      return "";
    }
    try {
      cv::Mat bgr;
      cv::cvtColor(image, bgr, cv::COLOR_RGB2BGR);
      const std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY,
                                       constants::card::JPEG_QUALITY};
      return cv::imwrite(imagePath, bgr, params) ? imagePath : "";
    } catch (const std::exception &e) {
      return "";
    }
  }

  /**
   * @brief Copies the part of `roi` that lies inside the image
   *
   * @param image Source image
   * @param roi Region of interest, clamped to the image
   * @return Cropped copy (empty when nothing of roi is inside the image)
   */
  static cv::Mat cropRegion(const cv::Mat &image, const cv::Rect &roi) {
    const cv::Rect clamped = roi & cv::Rect(0, 0, image.cols, image.rows);
    return clamped.empty() ? cv::Mat() : image(clamped).clone();
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

    // Downscale by an integer factor so the Laplacian runs on far fewer
    // pixels: 4x fewer at 720p, 9x at 1080p, 36x at 4K, in either
    // orientation. Integer factors keep INTER_AREA on its fast box-filter
    // path; a fractional factor (for example 1080 -> 640) takes a generic
    // path that is slower than scoring the full frame, so frames with a long
    // side under 2 * kBlurScoreLongSide are left alone.
    constexpr int kBlurScoreLongSide = 640;
    const int factor = std::max(gray.cols, gray.rows) / kBlurScoreLongSide;
    if (factor > 1) {
      cv::resize(gray, gray, cv::Size(gray.cols / factor, gray.rows / factor),
                 0, 0, cv::INTER_AREA);
    }

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_32F);

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

    // LUT applies the same table to all 3 channels (R, G, B) automatically
    cv::Mat res;
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
