#include "SetSymbolEmbedder.h"
#include "../../Constants.h"
#include "../../utils/ImageNetNormalization.h"
#include "../../utils/PathUtils.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace rncardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;
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
  std::string cleanPath = utils::PathUtils::stripFilePrefix(modelPath);

  module_ = std::make_unique<Module>(
      cleanPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

  Error loadError = module_->load();
  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load set symbol embedding model: " +
                             std::to_string(static_cast<int>(loadError)));
  }
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

  try {
    // Run inference
    std::vector<int> inputShape = {1, set_symbol::CHANNELS,
                                   set_symbol::INPUT_SIZE,
                                   set_symbol::INPUT_SIZE};
    auto inputTensor = from_blob(inputData.data(), inputShape);
    auto inferenceResult = module_->forward(inputTensor);

    if (!inferenceResult.ok()) {
      throw std::runtime_error(
          "Set symbol embedding inference failed: Error " +
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

    // Extract embedding (should be 128-dim)
    size_t embeddingSize =
        std::min(outputSize, static_cast<size_t>(set_symbol::EMBEDDING_DIM));
    std::vector<float> embedding(set_symbol::EMBEDDING_DIM, 0.0f);
    for (size_t i = 0; i < embeddingSize; i++) {
      embedding[i] = outputData[i];
    }

    return {embedding};
  } catch (const std::exception &e) {
    throw; // Re-throw to let caller handle
  }
}

} // namespace rncardscanner
