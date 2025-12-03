#include "SetSymbolEmbedder.h"
#include "../../Constants.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

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
  // Convert to float
  cv::Mat floatImg;
  img.convertTo(floatImg, CV_32FC3);

  // Prepare input tensor data in NCHW format
  const int inputSize = set_symbol::INPUT_SIZE;
  const int channels = set_symbol::CHANNELS;
  std::vector<float> normalizedImageData(1 * channels * inputSize * inputSize);

  for (int c = 0; c < channels; c++) {
    for (int h = 0; h < inputSize; h++) {
      for (int w = 0; w < inputSize; w++) {
        int chw_idx = c * inputSize * inputSize + h * inputSize + w;
        float pixel = floatImg.at<cv::Vec3f>(h, w)[c];
        // Apply ImageNet normalization: (pixel/scale - mean) / std
        normalizedImageData[chw_idx] =
            (pixel / imagenet::PIXEL_SCALE - imagenet::MEAN[c]) /
            imagenet::STD[c];
      }
    }
  }

  return normalizedImageData;
}

SetSymbolEmbedder::SetSymbolEmbedder(const std::string &modelPath) {
  // Strip file:// prefix if present
  std::string cleanPath = modelPath;
  const std::string filePrefix = "file://";
  if (cleanPath.find(filePrefix) == 0) {
    cleanPath = cleanPath.substr(filePrefix.length());
  }

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
  auto totalStart = std::chrono::high_resolution_clock::now();

  if (symbolImg.empty()) {
    throw std::runtime_error("Input set symbol image is empty");
  }

  auto prepStart = std::chrono::high_resolution_clock::now();

  // Resize to model input size (96x96)
  cv::Mat resized;
  cv::resize(symbolImg, resized,
             cv::Size(set_symbol::INPUT_SIZE, set_symbol::INPUT_SIZE));

  std::vector<float> inputData = normalizeImage(resized);

  auto prepEnd = std::chrono::high_resolution_clock::now();

  double preprocessingTimeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(prepEnd - prepStart)
          .count() /
      perf::MICROSECONDS_TO_MILLISECONDS;

  try {
    // Run inference
    auto inferenceStart = std::chrono::high_resolution_clock::now();

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

    auto inferenceEnd = std::chrono::high_resolution_clock::now();

    double inferenceTimeMs =
        std::chrono::duration_cast<std::chrono::microseconds>(inferenceEnd -
                                                              inferenceStart)
            .count() /
        perf::MICROSECONDS_TO_MILLISECONDS;

    auto totalEnd = std::chrono::high_resolution_clock::now();
    double totalTimeMs = std::chrono::duration_cast<std::chrono::microseconds>(
                             totalEnd - totalStart)
                             .count() /
                         perf::MICROSECONDS_TO_MILLISECONDS;

    SetSymbolEmbeddingResult result;
    result.embedding = embedding;
    result.performance.totalTimeMs = totalTimeMs;
    result.performance.inferenceTimeMs = inferenceTimeMs;
    result.performance.preprocessingTimeMs = preprocessingTimeMs;

    return result;
  } catch (const std::exception &e) {
    throw; // Re-throw to let caller handle
  }
}

} // namespace cardscanner
