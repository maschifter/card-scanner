#include "CardLanguageModel.h"
#include "../Constants.h"
#include "../benchmark/BenchmarkCollector.h"
#include "../utils/ImageNetNormalization.h"
#include <algorithm>
#include <array>

namespace cardscanner {

namespace language_model {
constexpr int INPUT_WIDTH = 256;
constexpr int INPUT_HEIGHT = 512;
constexpr float MIN_CONFIDENCE = 0.44f;
constexpr float MIN_TRADITIONAL = 0.5f;
// The model's class_names, in output order
constexpr std::array<const char *, 10> CODES = {"de", "en", "es", "fr", "it",
                                                "ja", "ko", "pt", "ru", "zh"};
} // namespace language_model

CardLanguageModel::CardLanguageModel(const std::string &modelPath) {
  session_ = inference::loadSession(modelPath);
}

CardLanguageInfo CardLanguageModel::classify(const cv::Mat &card) {
  std::vector<float> input;
  {
    benchmark::BenchmarkCollector::ScopedTimer preprocTimer(
        benchmark::Stage::LanguagePreproc);
    // Not the validated INTER_AREA: far slower at this ratio, ~same accuracy
    cv::Mat resized;
    cv::resize(
        card, resized,
        cv::Size(language_model::INPUT_WIDTH, language_model::INPUT_HEIGHT));
    input = utils::ImageNetNormalization::normalizeImage(
        resized, utils::PreprocessMode::MinusOneToOne);
  }

  benchmark::BenchmarkCollector::ScopedTimer classifyTimer(
      benchmark::Stage::LanguageClassify);
  const auto outputs =
      session_->run(input.data(), {1, constants::model::RGB_CHANNELS,
                                   language_model::INPUT_HEIGHT,
                                   language_model::INPUT_WIDTH});
  const auto &probs = outputs.at(0).data;
  const auto top = std::max_element(probs.begin(), probs.end());
  if (top == probs.end() || *top < language_model::MIN_CONFIDENCE) {
    return {};
  }
  std::string code = language_model::CODES.at(top - probs.begin());
  if (code == "zh") {
    const float traditional = outputs.at(1).data.at(0);
    code = traditional >= language_model::MIN_TRADITIONAL ? "zht" : "zhs";
  }
  return {code, *top};
}

} // namespace cardscanner
