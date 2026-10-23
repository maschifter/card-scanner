#pragma once

#include "../../types/ScanResults.h"
#include "../../inference/InferenceSession.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {


/**
 * @class FABColorClassifier
 * @brief CNN model for detecting FAB card color variants.
 *
 * Architecture: MobileNetV3-Small classifier
 * Input: 100x100 RGB image (3-dots region)
 * Output: 3-class probabilities (blue, red, yellow)
 *
 * @note This class is NOT thread-safe. The singleton instance should be
 * accessed through mutex-protected getters in ScannerRegistry.
 */
class FABColorClassifier {
public:
  /**
   * @brief Construct FAB color classifier from ExecuTorch .pte file
   *
   * @param modelPath Path to the .pte model file (supports file:// URIs)
   * @param classNames Color names in class order
   * @throws std::runtime_error if model loading fails or its class count
   * differs from classNames
   */
  explicit FABColorClassifier(const std::string &modelPath,
                              const std::vector<std::string> &classNames);

  /**
   * @brief Classify color variant from 3-dots region
   *
   * @param dotsRegion Cropped 3-dots region (will be resized to 100x100)
   * @return FABColorInfo with detected color and confidence
   */
  cardscanner::FABColorInfo classifyColor(const cv::Mat &dotsRegion);

private:
  std::unique_ptr<inference::InferenceSession> session_;
  std::vector<std::string> classNames_;
};

} // namespace cardscanner