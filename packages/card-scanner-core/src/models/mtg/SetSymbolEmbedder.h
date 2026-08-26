#pragma once

#include "../../inference/InferenceSession.h"
#include "../../types/Embeddings.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {


/**
 * @class SetSymbolEmbedder
 * @brief Embedding model for MTG set symbols.
 *
 * Architecture: MobileNetV3-Small-075 + embedding layer
 * Input: 96x96 RGB image
 * Output: 128-dim L2-normalized embedding
 */
class SetSymbolEmbedder {
public:
  explicit SetSymbolEmbedder(const std::string &modelPath);

  // Compute embedding from cropped set symbol image
  SetSymbolEmbeddingResult computeEmbedding(const cv::Mat &symbolImg);

private:
  std::unique_ptr<inference::InferenceSession> session_;

  // Preprocess image: resize to 96x96, normalize with ImageNet stats
  std::vector<float> normalizeImage(const cv::Mat &img) const;
};

} // namespace cardscanner
