#include "SetSymbolEmbedder.h"
#include "../../Constants.h"
#include "../../utils/ImageNetNormalization.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

using namespace constants;

// Set symbol specific constants
namespace set_symbol {
constexpr int INPUT_SIZE = 96;     // 96x96 input
constexpr int EMBEDDING_DIM = 128; // 128-dim output
constexpr int CHANNELS = 3;        // RGB
} // namespace set_symbol

std::vector<float> SetSymbolEmbedder::normalizeImage(const cv::Mat &img) const {
  return utils::ImageNetNormalization::normalizeImage(img,
                                                      set_symbol::INPUT_SIZE);
}

SetSymbolEmbedder::SetSymbolEmbedder(const std::string &modelPath) {
  session_ = inference::loadSession(modelPath);
}

SetSymbolEmbeddingResult
SetSymbolEmbedder::computeEmbedding(const cv::Mat &symbolImg) {
  if (symbolImg.empty()) {
    throw std::runtime_error("Input set symbol image is empty");
  }

  // Resize to model input size (96x96)
  cv::Mat resized;
  cv::resize(symbolImg, resized,
             cv::Size(set_symbol::INPUT_SIZE, set_symbol::INPUT_SIZE));

  std::vector<float> inputData = normalizeImage(resized);

  // Run inference
  auto outputs = session_->run(inputData.data(),
                               {1, set_symbol::CHANNELS, set_symbol::INPUT_SIZE,
                                set_symbol::INPUT_SIZE});
  const auto &output = outputs.at(0);
  const float *outputData = output.data.data();
  const size_t outputSize = output.data.size();

  // Extract embedding (should be 128-dim)
  size_t embeddingSize =
      std::min(outputSize, static_cast<size_t>(set_symbol::EMBEDDING_DIM));
  std::vector<float> embedding(set_symbol::EMBEDDING_DIM, 0.0f);
  for (size_t i = 0; i < embeddingSize; i++) {
    embedding[i] = outputData[i];
  }

  return {embedding};
}

} // namespace cardscanner
