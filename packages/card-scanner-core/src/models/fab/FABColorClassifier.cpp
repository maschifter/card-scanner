#include "FABColorClassifier.h"
#include "../../utils/ImageNetNormalization.h"
#include <Log.h>
#include <opencv2/imgproc.hpp>

namespace cardscanner {


// FAB color classifier constants
namespace fab_color {
constexpr int INPUT_SIZE = 100; // Model input size (100x100)
constexpr int NUM_CLASSES = 3;  // Yellow, Red, Blue
constexpr int CHANNELS = 3;     // RGB

// Class label mapping: index -> color name
// Model output: [yellow_logit, red_logit, blue_logit]
constexpr const char *CLASS_LABELS[NUM_CLASSES] = {"yellow", "red", "blue"};
} // namespace fab_color

FABColorClassifier::FABColorClassifier(const std::string &modelPath) {
  session_ = inference::loadSession(modelPath);
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
    std::vector<float> inputData = utils::ImageNetNormalization::normalizeImage(
        resized, fab_color::INPUT_SIZE);

    // Run inference
    // Input shape: [batch=1, channels=3, height=100, width=100]
    auto outputs = session_->run(inputData.data(),
                                 {1, fab_color::CHANNELS,
                                  fab_color::INPUT_SIZE,
                                  fab_color::INPUT_SIZE});
    const float *outputData = outputs.at(0).data.data();

    // Apply softmax to get probabilities
    std::vector<float> logits(fab_color::NUM_CLASSES);
    for (int i = 0; i < fab_color::NUM_CLASSES; i++) {
      logits[i] = outputData[i];
    }

    // Softmax: find max for numerical stability
    float maxLogit = *std::max_element(logits.begin(), logits.end());

    std::vector<float> probs(fab_color::NUM_CLASSES);
    float sumExp = 0.0f;
    for (int i = 0; i < fab_color::NUM_CLASSES; i++) {
      probs[i] = std::exp(logits[i] - maxLogit);
      sumExp += probs[i];
    }
    for (int i = 0; i < fab_color::NUM_CLASSES; i++) {
      probs[i] /= sumExp;
    }

    // Find the class with highest probability
    int maxIndex = std::distance(probs.begin(),
                                 std::max_element(probs.begin(), probs.end()));
    float confidence = probs[maxIndex];

    // Map index to color with bounds checking
    std::string detectedColor = "unknown";
    if (maxIndex >= 0 && maxIndex < fab_color::NUM_CLASSES) {
      detectedColor = fab_color::CLASS_LABELS[maxIndex];

      // Debug logging (similar to SetSymbolProcessor pattern)
      log(LOG_LEVEL::Debug,
          "[FABColorClassifier] Detected color:", detectedColor,
          "(confidence:", confidence, ")");
    }

    return cardscanner::FABColorInfo(detectedColor, confidence);

  } catch (const std::exception &e) {
    return cardscanner::FABColorInfo();
  }
}

} // namespace cardscanner
