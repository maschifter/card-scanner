#include "SetSymbolEmbedder.h"
#include "../../Constants.h"
#include "../Embedding.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

using namespace constants;

// Set symbol specific constants
namespace set_symbol {
constexpr int INPUT_SIZE = 96;     // 96x96 input
constexpr int EMBEDDING_DIM = 128; // 128-dim output
} // namespace set_symbol

SetSymbolEmbedder::SetSymbolEmbedder(const std::string &modelPath) {
  session_ = inference::loadSession(modelPath);
}

SetSymbolEmbeddingResult
SetSymbolEmbedder::computeEmbedding(const cv::Mat &symbolImg) {
  if (symbolImg.empty()) {
    throw std::runtime_error("Input set symbol image is empty");
  }

  return {embedImage(*session_, symbolImg, set_symbol::INPUT_SIZE,
                     set_symbol::EMBEDDING_DIM)};
}

} // namespace cardscanner
