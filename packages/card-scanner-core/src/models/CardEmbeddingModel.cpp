#include "CardEmbeddingModel.h"
#include "../Constants.h"
#include "Embedding.h"
#include "DatabaseManager.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

using namespace constants;

CardEmbeddingModel::CardEmbeddingModel(const std::string &modelPath) {
  session_ = inference::loadSession(modelPath);
}

CardEmbeddingResult
CardEmbeddingModel::computeEmbedding(const cv::Mat &cardImg) {
  if (cardImg.empty()) {
    throw std::runtime_error("Input image is empty");
  }

  return {embedImage(*session_, cardImg, model::EMBEDDING_INPUT_SIZE,
                     model::EMBEDDING_DIMENSION)};
}

} // namespace cardscanner
