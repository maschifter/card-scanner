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
 * Output: 3-class probabilities (yellow, red, blue)
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
   * @throws std::runtime_error if model loading fails
   */
  explicit FABColorClassifier(const std::string &modelPath);

  /**
   * @brief Classify color variant from 3-dots region
   *
   * @param dotsRegion Cropped 3-dots region (will be resized to 100x100)
   * @return FABColorInfo with detected color and confidence
   */
  cardscanner::FABColorInfo classifyColor(const cv::Mat &dotsRegion);

private:
  std::unique_ptr<inference::InferenceSession> session_;
};

} // namespace cardscanner