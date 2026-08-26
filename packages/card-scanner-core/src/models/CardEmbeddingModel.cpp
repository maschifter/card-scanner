#include "CardEmbeddingModel.h"
#include "../Constants.h"
#include "../utils/ImageNetNormalization.h"
#include "DatabaseManager.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

using namespace constants;

std::vector<float>
CardEmbeddingModel::normalizeImage(const cv::Mat &img) const {
  return utils::ImageNetNormalization::normalizeImage(
      img, model::EMBEDDING_INPUT_SIZE);
}

CardEmbeddingModel::CardEmbeddingModel(const std::string &modelPath) {
  session_ = inference::loadSession(modelPath);
}

CardEmbeddingResult
CardEmbeddingModel::computeEmbedding(const cv::Mat &cardImg) {
  if (cardImg.empty()) {
    throw std::runtime_error("Input image is empty");
  }

  // Resize to model input size (MobileNet)
  cv::Mat resized;
  cv::resize(
      cardImg, resized,
      cv::Size(model::EMBEDDING_INPUT_SIZE, model::EMBEDDING_INPUT_SIZE));

  std::vector<float> inputData = normalizeImage(resized);

  auto outputs =
      session_->run(inputData.data(),
                    {1, model::EMBEDDING_CHANNELS, model::EMBEDDING_INPUT_SIZE,
                     model::EMBEDDING_INPUT_SIZE});
  const auto &output = outputs.at(0);
  const float *outputData = output.data.data();
  const size_t outputSize = output.data.size();

  size_t embeddingSize =
      std::min(outputSize, static_cast<size_t>(model::EMBEDDING_DIMENSION));
  std::vector<float> embedding(model::EMBEDDING_DIMENSION, 0.0f);
  for (size_t i = 0; i < embeddingSize; i++) {
    embedding[i] = outputData[i];
  }

  return {embedding};
}

} // namespace cardscanner
