#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cardscanner {
namespace inference {

/**
 * @brief One model output: an owned copy of the backend's buffer plus
 * dimensions. Owning, so it stays valid across later run() calls.
 */
struct Tensor {
  std::vector<float> data;
  std::vector<int64_t> shape;
};

/**
 * @class InferenceSession
 * @brief A loaded model that maps one float NCHW input to N float outputs.
 *
 * Deliberately the smallest surface the models actually use - no dtypes,
 * no named inputs, no metadata queries. Preprocessing and postprocessing stay
 * in the model classes as plain OpenCV/STL, so a backend only has to move
 * floats.
 */
class InferenceSession {
public:
  virtual ~InferenceSession() = default;

  /**
   * @brief Runs the model.
   * @param input Contiguous NCHW float buffer; borrowed, not retained.
   * @param shape Dimensions of @p input.
   * @throws std::runtime_error on inference failure.
   */
  virtual std::vector<Tensor> run(const float *input,
                                  const std::vector<int64_t> &shape) = 0;

  /// Static dims of output @p index, known after load.
  virtual std::vector<int64_t> outputShape(size_t index) const = 0;
};

/**
 * @brief Checks at load that output 0 scores one class per configured name,
 * so a mismatched config fails once instead of mislabelling every scan.
 * @param model Model name for the message, e.g. "Color model".
 * @param namesKey Config key the names come from.
 * @throws std::runtime_error if the counts differ.
 */
inline void checkClassCount(const InferenceSession &session,
                            const std::vector<std::string> &names,
                            const std::string &model,
                            const std::string &namesKey) {
  const auto shape = session.outputShape(0);
  const int64_t classes = shape.empty() ? 0 : shape.back();
  if (classes != static_cast<int64_t>(names.size())) {
    throw std::runtime_error(model + " scores " + std::to_string(classes) +
                             " classes, " + namesKey + " lists " +
                             std::to_string(names.size()));
  }
}

/**
 * @brief Loads a model file. Declared here, defined by whichever backend is
 * linked into the binary - one undefined symbol resolved at link time, which
 * is the entire runtime-selection mechanism. Handles any `file://` prefix.
 *
 * ponytail: one backend per binary. If a single process ever needs both
 * ExecuTorch and ONNX, swap this for a settable factory.
 *
 * @throws std::runtime_error if the model cannot be loaded.
 */
std::unique_ptr<InferenceSession> loadSession(const std::string &modelPath);

} // namespace inference
} // namespace cardscanner
