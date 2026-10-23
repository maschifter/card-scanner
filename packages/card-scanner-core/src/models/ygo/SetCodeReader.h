#pragma once

#include "../../inference/InferenceSession.h"
#include "../../types/SearchResults.h"
#include <opencv2/core.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cardscanner {

/**
 * @class SetCodeReader
 * @brief Conv-only CTC reader of a printed Yu-Gi-Oh! set code (YGLD-ENA22).
 *
 * Input: [1, 1, 96, 256] grayscale in 0-1, the search box resized ignoring
 * aspect; the model normalizes each crop itself. Output: [1, T, classes]
 * log-probabilities in setCodeReading.classNames order: the CTC blank first,
 * then one character each.
 *
 * @note This class is NOT thread-safe, like the other models.
 */
class SetCodeReader {
public:
  /// @throws std::runtime_error if model loading fails or the model scores
  ///   a different number of classes than @p classNames lists
  SetCodeReader(const std::string &modelPath,
                const std::vector<std::string> &classNames);

  /**
   * @brief Reads the code in a search box.
   *
   * @param rgbRegion The search box, cut from a card scaled to
   *   card::DEWARP_HEIGHT
   * @throws std::runtime_error if the model returns an unexpected shape
   */
  CtcRead read(const cv::Mat &rgbRegion);

private:
  std::unique_ptr<inference::InferenceSession> session_;
  std::vector<std::string> classNames_;
};

} // namespace cardscanner
