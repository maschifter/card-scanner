#ifndef YOLO_PREPROCESSING_H
#define YOLO_PREPROCESSING_H

#include "../Constants.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace rncardscanner {
namespace utils {

/**
 * @class YoloPreprocessing
 * @brief Shared preprocessing utilities for YOLO-based models
 *
 * Provides standardized preprocessing operations used by all YOLO models
 * in the codebase, including:
 * - Letterbox resizing (aspect-ratio preserving resize with padding)
 * - Image normalization to [0, 1] range
 * - HWC to CHW tensor format conversion
 *
 * Using these shared utilities ensures consistency across models and
 * reduces code duplication.
 *
 * All methods are static and stateless - no instantiation required.
 *
 * @note Performance-optimized using direct pointer access for tensor conversion
 */
class YoloPreprocessing {
public:
  /**
   * @brief Applies letterbox resizing to maintain aspect ratio with padding
   *
   * Resizes the image to fit within newSize x newSize while maintaining
   * aspect ratio, then adds gray padding to make it square.
   *
   * @param img Input image
   * @param newSize Target size for both width and height
   * @return cv::Mat Letterboxed image of size newSize x newSize
   */
  static cv::Mat letterbox(const cv::Mat &img, int newSize) {
    using namespace constants;

    int height = img.rows;
    int width = img.cols;

    // Scale ratio (new / old)
    float r = std::min(static_cast<float>(newSize) / height,
                       static_cast<float>(newSize) / width);

    // Compute new unpadded dimensions
    int newUnpadW = std::round(width * r);
    int newUnpadH = std::round(height * r);

    // Compute padding
    float dw = (newSize - newUnpadW) / letterbox::PADDING_DIVISOR;
    float dh = (newSize - newUnpadH) / letterbox::PADDING_DIVISOR;

    // Resize if needed
    cv::Mat resized;
    if (height != newUnpadH || width != newUnpadW) {
      cv::resize(img, resized, cv::Size(newUnpadW, newUnpadH), 0, 0,
                 cv::INTER_LINEAR);
    } else {
      resized = img;
    }

    // Add padding
    int top = std::round(dh - letterbox::PADDING_ADJUST_MINUS);
    int bottom = std::round(dh + letterbox::PADDING_ADJUST_PLUS);
    int left = std::round(dw - letterbox::PADDING_ADJUST_MINUS);
    int right = std::round(dw + letterbox::PADDING_ADJUST_PLUS);

    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, top, bottom, left, right,
                       cv::BORDER_CONSTANT, yolo::LETTERBOX_PADDING_COLOR);

    return padded;
  }

  /**
   * @brief Preprocesses image for YOLO inference
   *
   * Performs letterbox resize, normalizes to [0, 1], and converts from
   * HWC (Height, Width, Channels) to CHW (Channels, Height, Width) format.
   *
   * @param img Input image
   * @param imgsz Target image size (square)
   * @param letterboxed Output parameter containing the letterboxed image
   * @return std::vector<float> Flattened CHW tensor data ready for inference
   */
  static std::vector<float> preprocess(const cv::Mat &img, int imgsz,
                                       cv::Mat &letterboxed) {
    using namespace constants;

    // Letterbox resize
    letterboxed = letterbox(img, imgsz);

    // Convert to float and normalize to [0, 1]
    cv::Mat normalized;
    letterboxed.convertTo(normalized, CV_32FC3,
                          matrix::SIGMOID_ONE / imagenet::PIXEL_SCALE);

    // Convert HWC to CHW and flatten to vector
    // Using direct pointer access for performance (3-5x faster than .at<>())
    const int channels = model::EMBEDDING_CHANNELS;
    std::vector<float> inputData(1 * channels * imgsz * imgsz);
    const float *data = normalized.ptr<float>();
    size_t hw = imgsz * imgsz;

    for (int c = 0; c < channels; c++) {
      for (int h = 0; h < imgsz; h++) {
        const float *row = data + h * imgsz * channels;
        for (int w = 0; w < imgsz; w++) {
          inputData[c * hw + h * imgsz + w] = row[w * channels + c];
        }
      }
    }

    return inputData;
  }
};

} // namespace utils
} // namespace rncardscanner

#endif // YOLO_PREPROCESSING_H
