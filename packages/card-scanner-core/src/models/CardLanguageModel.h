#pragma once

#include "../inference/InferenceSession.h"
#include "../types/SearchResults.h"
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>

namespace cardscanner {

class CardLanguageModel {
public:
  explicit CardLanguageModel(const std::string &modelPath);

  CardLanguageInfo classify(const cv::Mat &card);

private:
  std::unique_ptr<inference::InferenceSession> session_;
};

} // namespace cardscanner
