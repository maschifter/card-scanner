#ifndef IMAGENET_NORMALIZATION_H
#define IMAGENET_NORMALIZATION_H

#include "../Constants.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace cardscanner {
namespace utils {

/**
 * @class ImageNetNormalization
 * @brief Shared normalization utilities for embedding models
 *
 * Provides standardized ImageNet normalization operations used by all
 * embedding models in the codebase. Applies the standard ImageNet
 * preprocessing:
 *   normalized = (pixel / 255.0 - mean) / std
 *
 * Where mean and std are the ImageNet dataset statistics:
 *   mean = [0.485, 0.456, 0.406] (RGB)
 *   std  = [0.229, 0.224, 0.225] (RGB)
 *
 * Also handles HWC to CHW tensor format conversion required by most
 * deep learning frameworks.
 *
 * Using these shared utilities ensures consistency across models and
 * reduces code duplication.
 *
 * All methods are static and stateless - no instantiation required.
 */
class ImageNetNormalization {
public:
  /**
   * @brief Normalizes image using ImageNet statistics and converts to CHW
   * format
   *
   * Applies ImageNet normalization: (pixel/255 - mean) / std
   * and converts from HWC to CHW tensor format.
   *
   * @param img Input image (assumed to be already resized to inputSize x
   * inputSize)
   * @param inputSize Size of the square input image (e.g., 224, 96)
   * @return std::vector<float> Normalized CHW tensor data ready for inference
   *
   * @note The input image must already be resized to inputSize x inputSize
   */
  static std::vector<float> normalizeImage(const cv::Mat &img, int inputSize) {
    using namespace constants;

    // Convert to float
    cv::Mat floatImg;
    img.convertTo(floatImg, CV_32FC3);

    // Prepare input tensor data in NCHW format (N=1)
    const int channels = 3;
    std::vector<float> normalizedImageData(1 * channels * inputSize *
                                           inputSize);

    // Apply ImageNet normalization and convert HWC to CHW
    for (int c = 0; c < channels; c++) {
      for (int h = 0; h < inputSize; h++) {
        for (int w = 0; w < inputSize; w++) {
          int chw_idx = c * inputSize * inputSize + h * inputSize + w;
          float pixel = floatImg.at<cv::Vec3f>(h, w)[c];
          // Apply ImageNet normalization: (pixel/scale - mean) / std
          normalizedImageData[chw_idx] =
              (pixel / imagenet::PIXEL_SCALE - imagenet::MEAN[c]) /
              imagenet::STD[c];
        }
      }
    }

    return normalizedImageData;
  }
};

} // namespace utils
} // namespace cardscanner

#endif // IMAGENET_NORMALIZATION_H
