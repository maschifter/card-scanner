#include "CardEmbeddingModel.h"
#include "../Constants.h"
#include "DatabaseManager.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;
using namespace constants;

std::vector<float>
CardEmbeddingModel::normalizeImage(const cv::Mat &img) const {
  // Convert to float (no normalization yet)
  cv::Mat floatImg;
  img.convertTo(floatImg, CV_32FC3);

  // Prepare input tensor data in NCHW format
  const int inputSize = model::EMBEDDING_INPUT_SIZE;
  const int channels = model::EMBEDDING_CHANNELS;
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

CardEmbeddingModel::CardEmbeddingModel(const std::string &modelPath) {
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
    throw std::runtime_error("Failed to load embedding model: " +
                             std::to_string(static_cast<int>(loadError)));
  }
}

CardEmbeddingResult
CardEmbeddingModel::computeEmbedding(const cv::Mat &cardImg) {
  auto totalStart = std::chrono::high_resolution_clock::now();
  if (cardImg.empty()) {
    throw std::runtime_error("Input image is empty");
  }
  auto prepStart = std::chrono::high_resolution_clock::now();
  // Resize to model input size (MobileNet)
  cv::Mat resized;
  cv::resize(
      cardImg, resized,
      cv::Size(model::EMBEDDING_INPUT_SIZE, model::EMBEDDING_INPUT_SIZE));

  std::vector<float> inputData = normalizeImage(resized);

  auto prepEnd = std::chrono::high_resolution_clock::now();

  double embeddingPreprocessingTimeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(prepEnd - prepStart)
          .count() /
      perf::MICROSECONDS_TO_MILLISECONDS;
  try {
    // Extract embedding from card (directly from cv::Mat, no disk I/O!)
    auto embeddingStart = std::chrono::high_resolution_clock::now();
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

    auto embeddingEnd = std::chrono::high_resolution_clock::now();

    double embeddingTimeMs =
        std::chrono::duration_cast<std::chrono::microseconds>(embeddingEnd -
                                                              embeddingStart)
            .count() /
        perf::MICROSECONDS_TO_MILLISECONDS;

    CardEmbeddingResult result;
    result.embedding = embedding;
    CardEmbeddingPerformance performance;
    performance.totalTimeMs = embeddingTimeMs + embeddingPreprocessingTimeMs;
    performance.inferenceTimeMs = embeddingTimeMs;
    performance.preprocessingTimeMs = embeddingPreprocessingTimeMs;

    auto totalEnd = std::chrono::high_resolution_clock::now();
    double totalTimeMs = std::chrono::duration_cast<std::chrono::microseconds>(
                             totalEnd - totalStart)
                             .count() /
                         perf::MICROSECONDS_TO_MILLISECONDS;
    performance.totalTimeMs = totalTimeMs;
    result.performance = performance;

    return result;
  } catch (const std::exception &e) {
    throw; // Re-throw to let caller handle
  }
}

} // namespace cardscanner
