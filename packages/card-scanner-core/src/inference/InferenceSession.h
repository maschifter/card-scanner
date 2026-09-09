#pragma once

#include <cstdint>
#include <memory>
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
 * Deliberately the smallest surface the five models actually use - no dtypes,
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
};

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
