#pragma once

#include "ObjectBoxTest.h"
#include "objectbox.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
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
};

} // namespace cardscanner
