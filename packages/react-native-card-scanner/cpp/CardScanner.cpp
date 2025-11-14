#include "CardScanner.h"
#include <chrono>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <vector>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;

InferenceResult CardScanner::runInference(const std::string &modelPath) {

  std::cout << "Object box version is " << obx_version_string() << std::endl;
  // Strip "file://" prefix if present
  std::string cleanPath = modelPath;
  const std::string filePrefix = "file://";
  if (cleanPath.find(filePrefix) == 0) {
    cleanPath = cleanPath.substr(filePrefix.length());
  }

  // Create and load the module
  std::unique_ptr<Module> module = std::make_unique<Module>(
      cleanPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

  Error loadError = module->load();
  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load model from '" + cleanPath +
                             "': Error " +
                             std::to_string(static_cast<int>(loadError)));
  }

  // Get input metadata
  auto method_meta = module->method_meta("forward");
  if (!method_meta.ok()) {
    throw std::runtime_error("Failed to get method metadata");
  }

  // Get first input tensor metadata
  auto input_meta = method_meta->input_tensor_meta(0);
  if (!input_meta.ok()) {
    throw std::runtime_error("Failed to get input metadata");
  }

  // Get input shape (MobileNetV4 typically expects [1, 3, 224, 224])
  auto sizes = input_meta->sizes();
  std::vector<int> inputShape(sizes.begin(), sizes.end());

  // Calculate total number of elements
  size_t numElements = 1;
  for (const auto &dim : inputShape) {
    numElements *= dim;
  }

  // Create dummy input tensor filled with zeros
  std::vector<float> dummyData(numElements, 0.0f);
  auto inputTensor = from_blob(dummyData.data(), inputShape);

  // Run inference and measure time
  auto startTime = std::chrono::high_resolution_clock::now();
  auto result = module->forward(inputTensor);
  auto endTime = std::chrono::high_resolution_clock::now();

  if (!result.ok()) {
    throw std::runtime_error("Forward pass failed: Error " +
                             std::to_string(static_cast<int>(result.error())));
  }

  // Calculate inference time in milliseconds
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime);
  double inferenceTimeMs = duration.count() / 1000.0;

  // Get output tensor shape and data
  auto outputTensor = result->at(0).toTensor();
  auto outputSizes = outputTensor.sizes();
  std::vector<int> outputShape(outputSizes.begin(), outputSizes.end());

  // Extract embedding vector (assuming output is 1D or 2D with shape [1, 256])
  std::vector<float> embedding;
  const float *outputData = outputTensor.const_data_ptr<float>();

  // Calculate total output size
  size_t outputSize = 1;
  for (const auto &dim : outputShape) {
    outputSize *= dim;
  }

  // Extract first 256 values as embedding (or all values if < 256)
  size_t embeddingSize = std::min(outputSize, static_cast<size_t>(256));
  embedding.reserve(256);
  for (size_t i = 0; i < embeddingSize; i++) {
    embedding.push_back(outputData[i]);
  }

  // Pad with zeros if output is smaller than 256
  while (embedding.size() < 256) {
    embedding.push_back(0.0f);
  }

  // L2 normalize the embedding (to match Python preprocessing)
  float norm = 0.0f;
  for (float val : embedding) {
    norm += val * val;
  }
  norm = std::sqrt(norm);

  if (norm > 0.0f) {
    for (float &val : embedding) {
      val /= norm;
    }
  }

  std::cout << "Extracted and L2-normalized " << embeddingSize << "D embedding"
            << std::endl;

  return {outputShape, inferenceTimeMs, embedding};
}

InferenceResult CardScanner::runInferenceOnImage(const std::string &modelPath,
                                                 const std::string &imagePath) {
  std::cout << "Loading image from: " << imagePath << std::endl;

  // Strip "file://" prefix from image path if present
  std::string cleanImagePath = imagePath;
  const std::string filePrefix = "file://";
  if (cleanImagePath.find(filePrefix) == 0) {
    cleanImagePath = cleanImagePath.substr(filePrefix.length());
  }

  // Load image with OpenCV
  cv::Mat image = cv::imread(cleanImagePath);
  if (image.empty()) {
    throw std::runtime_error("Failed to load image from: " + cleanImagePath);
  }

  std::cout << "Original image size: " << image.cols << "x" << image.rows
            << std::endl;

  // Resize to 224x224 (MobileNet input size)
  cv::Mat resized;
  cv::resize(image, resized, cv::Size(224, 224));

  // Convert BGR to RGB
  cv::Mat rgb;
  cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

  // Convert to float and normalize to [0, 1]
  cv::Mat normalized;
  rgb.convertTo(normalized, CV_32FC3, 1.0 / 255.0);

  // ImageNet normalization values (matching Python preprocessing)
  const float mean[3] = {0.485f, 0.456f, 0.406f}; // RGB order
  const float std[3] = {0.229f, 0.224f, 0.225f};

  // Prepare input tensor data in NCHW format [1, 3, 224, 224]
  // OpenCV stores data as HWC, we need to convert to CHW and apply ImageNet normalization
  std::vector<float> inputData(1 * 3 * 224 * 224);

  for (int c = 0; c < 3; c++) {
    for (int h = 0; h < 224; h++) {
      for (int w = 0; w < 224; w++) {
        int chw_idx = c * 224 * 224 + h * 224 + w;
        float pixel = normalized.at<cv::Vec3f>(h, w)[c];
        // Apply ImageNet normalization: (pixel - mean) / std
        inputData[chw_idx] = (pixel - mean[c]) / std[c];
      }
    }
  }

  std::cout << "Preprocessed image to 224x224 RGB tensor" << std::endl;

  // Strip "file://" prefix from model path
  std::string cleanModelPath = modelPath;
  if (cleanModelPath.find(filePrefix) == 0) {
    cleanModelPath = cleanModelPath.substr(filePrefix.length());
  }

  // Create and load the module
  std::unique_ptr<Module> module = std::make_unique<Module>(
      cleanModelPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

  Error loadError = module->load();
  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load model from '" + cleanModelPath +
                             "': Error " +
                             std::to_string(static_cast<int>(loadError)));
  }

  // Create input tensor
  std::vector<int> inputShape = {1, 3, 224, 224};
  auto inputTensor = from_blob(inputData.data(), inputShape);

  // Run inference and measure time
  auto startTime = std::chrono::high_resolution_clock::now();
  auto result = module->forward(inputTensor);
  auto endTime = std::chrono::high_resolution_clock::now();

  if (!result.ok()) {
    throw std::runtime_error("Forward pass failed: Error " +
                             std::to_string(static_cast<int>(result.error())));
  }

  // Calculate inference time in milliseconds
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime);
  double inferenceTimeMs = duration.count() / 1000.0;

  // Get output tensor shape and data
  auto outputTensor = result->at(0).toTensor();
  auto outputSizes = outputTensor.sizes();
  std::vector<int> outputShape(outputSizes.begin(), outputSizes.end());

  // Extract embedding vector
  std::vector<float> embedding;
  const float *outputData = outputTensor.const_data_ptr<float>();

  // Calculate total output size
  size_t outputSize = 1;
  for (const auto &dim : outputShape) {
    outputSize *= dim;
  }

  // Extract first 256 values as embedding (or all values if < 256)
  size_t embeddingSize = std::min(outputSize, static_cast<size_t>(256));
  embedding.reserve(256);
  for (size_t i = 0; i < embeddingSize; i++) {
    embedding.push_back(outputData[i]);
  }

  // Pad with zeros if output is smaller than 256
  while (embedding.size() < 256) {
    embedding.push_back(0.0f);
  }

  std::cout << "Extracted " << embeddingSize << "D embedding (already normalized by model)"
            << std::endl;

  return {outputShape, inferenceTimeMs, embedding};
}

InferenceResult CardScanner::runInferenceOnMat(const std::string &modelPath,
                                               const cv::Mat &image) {
  if (image.empty()) {
    throw std::runtime_error("Empty image provided");
  }

  std::cout << "Processing cv::Mat image: " << image.cols << "x" << image.rows
            << std::endl;

  // Resize to 224x224 (MobileNet input size)
  cv::Mat resized;
  cv::resize(image, resized, cv::Size(224, 224));

  // Convert BGR to RGB
  cv::Mat rgb;
  cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

  // Convert to float and normalize to [0, 1]
  cv::Mat normalized;
  rgb.convertTo(normalized, CV_32FC3, 1.0 / 255.0);

  // ImageNet normalization values (matching Python preprocessing)
  const float mean[3] = {0.485f, 0.456f, 0.406f}; // RGB order
  const float std[3] = {0.229f, 0.224f, 0.225f};

  // Prepare input tensor data in NCHW format [1, 3, 224, 224]
  std::vector<float> inputData(1 * 3 * 224 * 224);

  for (int c = 0; c < 3; c++) {
    for (int h = 0; h < 224; h++) {
      for (int w = 0; w < 224; w++) {
        int chw_idx = c * 224 * 224 + h * 224 + w;
        float pixel = normalized.at<cv::Vec3f>(h, w)[c];
        inputData[chw_idx] = (pixel - mean[c]) / std[c];
      }
    }
  }

  std::cout << "Preprocessed image to 224x224 RGB tensor" << std::endl;

  // Strip "file://" prefix from model path
  std::string cleanModelPath = modelPath;
  const std::string filePrefix = "file://";
  if (cleanModelPath.find(filePrefix) == 0) {
    cleanModelPath = cleanModelPath.substr(filePrefix.length());
  }

  // Create and load the module
  std::unique_ptr<Module> module = std::make_unique<Module>(
      cleanModelPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

  Error loadError = module->load();
  if (loadError != Error::Ok) {
    throw std::runtime_error("Failed to load model from '" + cleanModelPath +
                             "': Error " +
                             std::to_string(static_cast<int>(loadError)));
  }

  // Create input tensor
  std::vector<int> inputShape = {1, 3, 224, 224};
  auto inputTensor = from_blob(inputData.data(), inputShape);

  // Run inference and measure time
  auto startTime = std::chrono::high_resolution_clock::now();
  auto result = module->forward(inputTensor);
  auto endTime = std::chrono::high_resolution_clock::now();

  if (!result.ok()) {
    throw std::runtime_error("Forward pass failed");
  }

  double inferenceTimeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
          .count() / 1000.0;

  // Get output tensor and extract embedding
  auto outputTensor = result->at(0).toTensor();
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

  std::cout << "Extracted " << embeddingSize << "D embedding (already normalized by model)"
            << std::endl;

  return {outputShape, inferenceTimeMs, embedding};
}

} // namespace cardscanner
