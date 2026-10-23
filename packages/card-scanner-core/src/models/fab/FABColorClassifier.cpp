#include "FABColorClassifier.h"
#include "../../Constants.h"
#include "../../utils/ImageNetNormalization.h"
#include <Log.h>
#include <opencv2/imgproc.hpp>

namespace cardscanner {


// FAB color classifier constants
namespace fab_color {
constexpr int INPUT_SIZE = 100; // Model input size (100x100)
} // namespace fab_color

FABColorClassifier::FABColorClassifier(
    const std::string &modelPath, const std::vector<std::string> &classNames)
    : classNames_(classNames) {
  session_ = inference::loadSession(modelPath);
  inference::checkClassCount(*session_, classNames_, "Color model",
                             "colorDetection.classNames");
}

cardscanner::FABColorInfo
FABColorClassifier::classifyColor(const cv::Mat &dotsRegion) {
  if (dotsRegion.empty()) {
    return cardscanner::FABColorInfo();
  }

  try {
    // Resize to model input size
    cv::Mat resized;
    cv::resize(dotsRegion, resized,
               cv::Size(fab_color::INPUT_SIZE, fab_color::INPUT_SIZE));

    // Preprocess image using shared ImageNet normalization
    std::vector<float> inputData =
        utils::ImageNetNormalization::normalizeImage(resized);

    // Run inference
    // Input shape: [batch=1, channels=3, height=100, width=100]
    auto outputs = session_->run(
        inputData.data(), {1, constants::model::RGB_CHANNELS,
                           fab_color::INPUT_SIZE, fab_color::INPUT_SIZE});
    const auto &output = outputs.at(0).data;
    const int numClasses = static_cast<int>(classNames_.size());
    const float *outputData = output.data();

    // Apply softmax to get probabilities
    std::vector<float> logits(numClasses);
    for (int i = 0; i < numClasses; i++) {
      logits[i] = outputData[i];
    }

    // Softmax: find max for numerical stability
    float maxLogit = *std::max_element(logits.begin(), logits.end());

    std::vector<float> probs(numClasses);
    float sumExp = 0.0f;
    for (int i = 0; i < numClasses; i++) {
      probs[i] = std::exp(logits[i] - maxLogit);
      sumExp += probs[i];
    }
    for (int i = 0; i < numClasses; i++) {
      probs[i] /= sumExp;
    }

    // Find the class with highest probability
    int maxIndex = std::distance(probs.begin(),
                                 std::max_element(probs.begin(), probs.end()));
    float confidence = probs[maxIndex];

    const std::string &detectedColor = classNames_.at(maxIndex);
    // Debug logging (similar to SetSymbolProcessor pattern)
    log(LOG_LEVEL::Debug, "[CardScanner]", "detected color:", detectedColor,
        "confidence:", confidence);

    return cardscanner::FABColorInfo(detectedColor, confidence);

  } catch (const std::exception &e) {
    return cardscanner::FABColorInfo();
  }
}

} // namespace cardscanner
