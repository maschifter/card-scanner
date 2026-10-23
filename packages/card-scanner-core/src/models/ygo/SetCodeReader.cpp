#include "SetCodeReader.h"
#include "../../benchmark/BenchmarkCollector.h"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace cardscanner {

namespace {
constexpr int INPUT_HEIGHT = 96;
constexpr int INPUT_WIDTH = 256;
} // namespace

SetCodeReader::SetCodeReader(const std::string &modelPath,
                             const std::vector<std::string> &classNames)
    : session_(inference::loadSession(modelPath)), classNames_(classNames) {
  inference::checkClassCount(*session_, classNames_, "Set code model",
                             "setCodeReading.classNames");
}

CtcRead SetCodeReader::read(const cv::Mat &rgbRegion) {
  using benchmark::BenchmarkCollector;
  using benchmark::Stage;

  std::vector<float> input;
  {
    BenchmarkCollector::ScopedTimer timer(Stage::SetCodePreproc);
    // Aspect is ignored: the model was trained on letters stretched this way.
    cv::Mat resized, gray;
    cv::resize(rgbRegion, resized, {INPUT_WIDTH, INPUT_HEIGHT}, 0, 0,
               cv::INTER_LINEAR);
    cv::cvtColor(resized, gray, cv::COLOR_RGB2GRAY);
    gray.convertTo(gray, CV_32F, 1.0 / 255.0);
    input.assign(gray.begin<float>(), gray.end<float>());
  }

  std::vector<inference::Tensor> outputs;
  {
    BenchmarkCollector::ScopedTimer timer(Stage::SetCodeForward);
    outputs = session_->run(input.data(), {1, 1, INPUT_HEIGHT, INPUT_WIDTH});
  }

  BenchmarkCollector::ScopedTimer timer(Stage::SetCodePost);
  const int classes = static_cast<int>(classNames_.size());
  if (outputs.empty() || outputs[0].shape.size() != 3 ||
      outputs[0].shape[2] != classes) {
    throw std::runtime_error("SetCodeReader: expected [1, T, " +
                             std::to_string(classes) + "] output");
  }

  CtcRead result;
  result.steps = static_cast<int>(outputs[0].shape[1]);
  result.classes = classes;
  result.classNames = classNames_;
  result.probs = std::move(outputs[0].data);
  for (float &v : result.probs) {
    v = std::exp(v);
  }
  // Greedy CTC: the best class per step, repeats collapsed, blanks dropped.
  int prev = 0;
  double sumLog = 0;
  for (int t = 0; t < result.steps; ++t) {
    const float *p = result.probs.data() + t * classes;
    const float *top = std::max_element(p, p + classes);
    const int best = static_cast<int>(top - p);
    sumLog += std::log(std::max(*top, 1e-9f));
    if (best != 0) {
      result.firstStep = result.firstStep < 0 ? t : result.firstStep;
      result.lastStep = t;
      if (best != prev) {
        result.text += classNames_[best];
      }
    }
    prev = best;
  }
  if (result.steps > 0) {
    result.confidence = static_cast<float>(std::exp(sumLog / result.steps));
  }
  return result;
}

} // namespace cardscanner
