#include "CardEmbeddingModel.h"
#include "../Constants.h"
#include "../utils/ImageNetNormalization.h"
#include "../utils/PathUtils.h"
#include "DatabaseManager.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace rncardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;
using namespace constants;

std::vector<float>
CardEmbeddingModel::normalizeImage(const cv::Mat &img) const {
  return utils::ImageNetNormalization::normalizeImage(
      img, model::EMBEDDING_INPUT_SIZE);
}

CardEmbeddingModel::CardEmbeddingModel(const std::string &modelPath) {
  std::string cleanPath = utils::PathUtils::stripFilePrefix(modelPath);

  module_ = std::make_unique<Module>(
      cleanPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

  Error loadError = module_->load();
  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load embedding model: " +
                             std::to_string(static_cast<int>(loadError)));
  }
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

  try {
    std::vector<int> inputShape = {1, model::EMBEDDING_CHANNELS,
                                   model::EMBEDDING_INPUT_SIZE,
                                   model::EMBEDDING_INPUT_SIZE};
    auto inputTensor = from_blob(inputData.data(), inputShape);
    auto inferenceResult = module_->forward(inputTensor);
    if (!inferenceResult.ok()) {
      throw std::runtime_error(
          "Embedding forward pass failed: Error " +
          std::to_string(static_cast<int>(inferenceResult.error())));
    }
    auto outputTensor = inferenceResult->at(0).toTensor();
    auto outputSizes = outputTensor.sizes();
    std::vector<int> outputShape(outputSizes.begin(), outputSizes.end());

    const float *outputData = outputTensor.const_data_ptr<float>();
    size_t outputSize = 1;
    for (const auto &dim : outputShape) {
      outputSize *= dim;
    }

    size_t embeddingSize =
        std::min(outputSize, static_cast<size_t>(model::EMBEDDING_DIMENSION));
    std::vector<float> embedding(model::EMBEDDING_DIMENSION, 0.0f);
    for (size_t i = 0; i < embeddingSize; i++) {
      embedding[i] = outputData[i];
    }

    return {embedding};
  } catch (const std::exception &e) {
    throw; // Re-throw to let caller handle
  }
}

} // namespace rncardscanner
