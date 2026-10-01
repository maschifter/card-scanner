#pragma once

#include "../Constants.h"
#include "../inference/InferenceSession.h"
#include "../utils/ImageNetNormalization.h"
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <vector>

namespace cardscanner {

/// Resizes an RGB image to inputSize x inputSize, normalises it with ImageNet
/// statistics, runs it through an embedding model, and returns the first
/// `dimension` outputs, zero-padded when the model returns fewer.
inline std::vector<float> embedImage(inference::InferenceSession &session,
                                     const cv::Mat &image, int inputSize,
                                     size_t dimension) {
  cv::Mat resized;
  cv::resize(image, resized, cv::Size(inputSize, inputSize));
  const std::vector<float> input =
      utils::ImageNetNormalization::normalizeImage(resized, inputSize);

  const auto outputs = session.run(
      input.data(), {1, constants::model::RGB_CHANNELS, inputSize, inputSize});
  const auto &output = outputs.at(0).data;

  std::vector<float> embedding(dimension, 0.0f);
  std::copy_n(output.begin(), std::min(output.size(), dimension),
              embedding.begin());
  return embedding;
}

} // namespace cardscanner
