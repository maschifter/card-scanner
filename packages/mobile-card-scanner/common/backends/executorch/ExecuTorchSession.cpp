// The only translation unit in the project that includes ExecuTorch.
// Everything above it works against inference::InferenceSession.

#include <inference/InferenceSession.h>
#include <utils/PathUtils.h>

#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>

#include <stdexcept>
#include <string>

namespace cardscanner {
namespace inference {

namespace {

using ::executorch::extension::from_blob;
using ::executorch::extension::module::Module;
using ::executorch::runtime::Error;

class ExecuTorchSession final : public InferenceSession {
public:
  explicit ExecuTorchSession(const std::string &modelPath) {
    module_ = std::make_unique<Module>(
        modelPath, Module::LoadMode::MmapUseMlockIgnoreErrors);

    const Error loadError = module_->load();
    if (loadError != Error::Ok) {
      throw std::runtime_error("Failed to load model '" + modelPath +
                               "': Error " +
                               std::to_string(static_cast<int>(loadError)));
    }
  }

  std::vector<Tensor> run(const float *input,
                          const std::vector<int64_t> &shape) override {
    // from_blob wants int extents; the caller's int64 dims are model input
    // sizes, so they always fit.
    std::vector<int> extents(shape.begin(), shape.end());

    // from_blob is non-owning - the caller's buffer outlives this call.
    auto inputTensor = from_blob(const_cast<float *>(input), extents);

    auto result = module_->forward(inputTensor);
    if (!result.ok()) {
      throw std::runtime_error(
          "Forward pass failed: Error " +
          std::to_string(static_cast<int>(result.error())));
    }

    std::vector<Tensor> outputs;
    outputs.reserve(result->size());
    for (size_t i = 0; i < result->size(); i++) {
      auto tensor = result->at(i).toTensor();
      auto sizes = tensor.sizes();

      Tensor out;
      out.shape.assign(sizes.begin(), sizes.end());

      size_t count = 1;
      for (const auto dim : out.shape) {
        count *= static_cast<size_t>(dim);
      }

      // Copies out of the method's memory-planned buffers, which the Module
      // owns and reuses on the next forward().
      const float *ptr = tensor.const_data_ptr<float>();
      out.data.assign(ptr, ptr + count);

      outputs.push_back(std::move(out));
    }
    return outputs;
  }

private:
  std::unique_ptr<Module> module_;
};

} // namespace

std::unique_ptr<InferenceSession> loadSession(const std::string &modelPath) {
  return std::make_unique<ExecuTorchSession>(
      utils::PathUtils::stripFilePrefix(modelPath));
}

} // namespace inference
} // namespace cardscanner
