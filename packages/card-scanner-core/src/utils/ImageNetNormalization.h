#ifndef IMAGENET_NORMALIZATION_H
#define IMAGENET_NORMALIZATION_H

#include "../Constants.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace cardscanner {
namespace utils {

enum class PreprocessMode { ImageNet, MinusOneToOne };

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
   * @param img Input image, already resized to the model's input size
   * @param mode MinusOneToOne applies pixel/255 * 2 - 1 instead
   * @return std::vector<float> Normalized CHW tensor data ready for inference
   */
  static std::vector<float>
  normalizeImage(const cv::Mat &img,
                 PreprocessMode mode = PreprocessMode::ImageNet) {
    using namespace constants;

    std::vector<cv::Mat> planes;
    cv::split(img, planes);

    // One CHW plane per channel, written in place by a vectorised convertTo:
    // (pixel / scale - mean) / std is a single scale and shift.
    const size_t area = img.total();
    std::vector<float> normalizedImageData(planes.size() * area);
    for (size_t c = 0; c < planes.size(); c++) {
      cv::Mat plane(img.size(), CV_32F, normalizedImageData.data() + c * area);
      if (mode == PreprocessMode::MinusOneToOne) {
        planes[c].convertTo(plane, CV_32F, 2.0 / imagenet::PIXEL_SCALE, -1.0);
      } else {
        planes[c].convertTo(plane, CV_32F,
                            1.0 / (imagenet::PIXEL_SCALE * imagenet::STD[c]),
                            -imagenet::MEAN[c] / imagenet::STD[c]);
      }
    }
    return normalizedImageData;
  }
};

} // namespace utils
} // namespace cardscanner

#endif // IMAGENET_NORMALIZATION_H
