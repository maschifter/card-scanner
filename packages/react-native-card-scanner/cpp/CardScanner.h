#pragma once

#include <string>
#include <vector>

namespace cardscanner {

struct InferenceResult {
  std::vector<int> outputShape;
  double inferenceTimeMs;
};

class CardScanner {
public:
  // Run inference on the model with dummy input
  // Returns the output shape and inference time
  static InferenceResult runInference(const std::string &modelPath);
};

} // namespace cardscanner
