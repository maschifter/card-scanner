#pragma once

#include "ObjectBoxDB.h"
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;

struct CardEmbeddingPerformance {
  double totalTimeMs;
  double preprocessingTimeMs;
  double inferenceTimeMs;
};

struct CardEmbeddingResult {
  std::vector<float> embedding;
  CardEmbeddingPerformance performance;
};

class CardEmbeddingModel {
public:
  explicit CardEmbeddingModel(const std::string &modelPath);
  CardEmbeddingResult computeEmbedding(const cv::Mat &cardImg);

private:
  std::unique_ptr<Module> module_;
  std::vector<float> normalizeImage(const cv::Mat &img) const;
};

} // namespace cardscanner
