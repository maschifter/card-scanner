#pragma once

#include "ObjectBoxTest.h"
#include "objectbox.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

namespace executorch {
namespace extension {
namespace module {
class Module;
}
}
}

namespace cardscanner {

struct InferenceResult {
  std::vector<int> outputShape;
  double inferenceTimeMs;
  std::vector<float> embedding; // 256D embedding vector extracted from model output
};

class CardScanner {
public:
  // Run inference on the model with dummy input
  // Returns the output shape, inference time, and embedding vector
  static InferenceResult runInference(const std::string &modelPath);

  // Run inference on the model with an image file
  // imagePath: path to the image file
  static InferenceResult runInferenceOnImage(const std::string &modelPath,
                                              const std::string &imagePath);

  // Run inference on the model with a cv::Mat directly (avoids disk I/O)
  // image: OpenCV Mat (BGR format)
  static InferenceResult runInferenceOnMat(const std::string &modelPath,
                                           const cv::Mat &image);

private:
  // Module cache to avoid reloading models
  static std::map<std::string, std::shared_ptr<executorch::extension::module::Module>> moduleCache;
  static std::mutex cacheMutex;

  // Get or load a module from cache
  static std::shared_ptr<executorch::extension::module::Module> getModule(const std::string &modelPath);
};

} // namespace cardscanner
