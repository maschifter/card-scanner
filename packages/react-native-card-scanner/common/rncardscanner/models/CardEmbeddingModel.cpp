#include "CardEmbeddingModel.h"
#include "DatabaseManager.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;

std::vector<float>
CardEmbeddingModel::normalizeImage(const cv::Mat &img) const {
  // Convert to float (no normalization yet)
  cv::Mat floatImg;
  img.convertTo(floatImg, CV_32FC3);

  // ImageNet normalization values (RGB order)
  const float mean[3] = {0.485f, 0.456f, 0.406f};
  const float std[3] = {0.229f, 0.224f, 0.225f};

  // Prepare input tensor data in NCHW format [1, 3, 224, 224]
  std::vector<float> normalizedImageData(1 * 3 * 224 * 224);

  for (int c = 0; c < 3; c++) {
    for (int h = 0; h < 224; h++) {
      for (int w = 0; w < 224; w++) {
        int chw_idx = c * 224 * 224 + h * 224 + w;
        float pixel = floatImg.at<cv::Vec3f>(h, w)[c];
        // Apply ImageNet normalization directly: (pixel/255 - mean) / std
        normalizedImageData[chw_idx] = (pixel / 255.0f - mean[c]) / std[c];
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
  // Resize to 224x224 (MobileNet input size)
  cv::Mat resized;
  cv::resize(cardImg, resized, cv::Size(224, 224));

  std::vector<float> inputData = normalizeImage(resized);

  auto prepEnd = std::chrono::high_resolution_clock::now();

  double embeddingPreprocessingTimeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(prepEnd - prepStart)
          .count() /
      1000.0;
  try {
    // Extract embedding from card (directly from cv::Mat, no disk I/O!)
    auto embeddingStart = std::chrono::high_resolution_clock::now();
    std::vector<int> inputShape = {1, 3, 224, 224};
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

    size_t embeddingSize = std::min(outputSize, static_cast<size_t>(256));
    std::vector<float> embedding(256, 0.0f);
    for (size_t i = 0; i < embeddingSize; i++) {
      embedding[i] = outputData[i];
    }

    auto embeddingEnd = std::chrono::high_resolution_clock::now();

    double embeddingTimeMs =
        std::chrono::duration_cast<std::chrono::microseconds>(embeddingEnd -
                                                              embeddingStart)
            .count() /
        1000.0;

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
                         1000.0;
    performance.totalTimeMs = totalTimeMs;
    result.performance = performance;

    return result;
  } catch (const std::exception &e) {
    throw; // Re-throw to let caller handle
  }
}

} // namespace cardscanner
