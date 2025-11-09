#include "CardScanner.h"
#include <chrono>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <iostream>
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

  // Get output tensor shape
  auto outputTensor = result->at(0).toTensor();
  auto outputSizes = outputTensor.sizes();
  std::vector<int> outputShape(outputSizes.begin(), outputSizes.end());

  objectboxtest::ObjectBoxTest::runTest();

  return {outputShape, inferenceTimeMs};
}

} // namespace cardscanner
