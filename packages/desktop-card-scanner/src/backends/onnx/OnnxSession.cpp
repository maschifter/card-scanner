// The only translation unit that includes ONNX Runtime; everything above works
// against inference::InferenceSession. Mirrors the ExecuTorch backend in
// mobile-card-scanner: one backend per binary, selected by which one is linked.

#include <inference/InferenceSession.h>
#include <utils/PathUtils.h>

#include <onnxruntime_cxx_api.h>
#if defined(__APPLE__)
#include <coreml_provider_factory.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cardscanner {
namespace inference {

namespace {

/// One per process; ORT documents Env as safe to share across sessions.
Ort::Env &ortEnv() {
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "cardscanner");
  return env;
}

/**
 * @brief Registers the first available accelerator, falling back to CPU.
 *
 * Overridable with CARD_SCANNER_EP, a comma-separated list. Registration
 * failing is normal, so each attempt is guarded and the winner is returned for
 * logging - a silent CPU fallback otherwise looks like acceleration working.
 */
std::string appendProviders(Ort::SessionOptions &options, const std::string &modelPath) {
  // Models with external weights stay on CPU: an accelerator re-serialising
  // the graph resolves the sidecar as "<model>.onnx/<model>.onnx.data", taking
  // the .onnx for a directory, and the load fails.
  if (std::filesystem::exists(modelPath + ".data")) {
    return "cpu (external weights)";
  }

  std::vector<std::string> ladder;
  if (const char *override = std::getenv("CARD_SCANNER_EP")) {
    std::string spec(override);
    size_t start = 0;
    while (start <= spec.size()) {
      const size_t comma = spec.find(',', start);
      const size_t end = comma == std::string::npos ? spec.size() : comma;
      if (end > start) {
        ladder.push_back(spec.substr(start, end - start));
      }
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
  } else {
    // CPU by default on measurement: CoreML came in at 33.0ms median against
    // CPU's 34.2ms, and 36.9ms with every model on it, while adding ~0.5s per
    // model to startup. These models are small enough that dispatch overhead
    // cancels the gain. The ladder stays because the tradeoff is
    // hardware-specific - a discrete GPU under DirectML may well differ.
    ladder = {"cpu"};
  }

  for (const auto &name : ladder) {
    try {
#if defined(__APPLE__)
      if (name == "coreml") {
        // No flags, so ORT decides per subgraph what CoreML can take.
        options.AppendExecutionProvider("CoreML", {{"ModelFormat", "MLProgram"}});
        return "coreml";
      }
#endif
#if defined(_WIN32)
      if (name == "dml") {
        // Needs the DirectML build of ONNX Runtime; the stock one throws.
        options.AppendExecutionProvider("DML", {});
        return "dml";
      }
#endif
      if (name == "cpu") {
        return "cpu"; // always present, nothing to register
      }
      options.AppendExecutionProvider(name, {});
      return name;
    } catch (const Ort::Exception &e) {
      std::cerr << "inference: " << name << " unavailable (" << e.what()
                << "), trying next\n";
    }
  }
  return "cpu";
}

class OnnxSession final : public InferenceSession {
public:
  explicit OnnxSession(const std::string &modelPath) : modelPath_(modelPath) {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // ORT's default thread counts: this runs in its own process with no UI to
    // starve. Cap intra-op threads here if it competes with the capture path.

    provider_ = appendProviders(options, modelPath_);

    try {
      session_ = Ort::Session(ortEnv(), modelPath_.c_str(), options);
    } catch (const Ort::Exception &e) {
      // ORT's exception does not carry the path, and callers need to know
      // which model failed.
      throw std::runtime_error("Failed to load model '" + modelPath_ +
                               "': " + e.what());
    }

    cacheIoNames();

    // A failed registration is otherwise indistinguishable from a successful
    // one.
    std::cerr << "inference: " << modelPath_.substr(modelPath_.find_last_of('/') + 1)
              << " on " << provider_ << "\n";
  }

  std::vector<Tensor> run(const float *input,
                          const std::vector<int64_t> &shape) override {
    size_t elementCount = 1;
    for (const auto dim : shape) {
      elementCount *= static_cast<size_t>(dim);
    }

    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    // Borrowed, not retained: CreateTensor wraps the caller's buffer.
    auto inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, const_cast<float *>(input), elementCount, shape.data(),
        shape.size());

    std::vector<Ort::Value> outputs;
    try {
      outputs = session_.Run(Ort::RunOptions{nullptr}, inputNamePtrs_.data(),
                             &inputTensor, 1, outputNamePtrs_.data(),
                             outputNamePtrs_.size());
    } catch (const Ort::Exception &e) {
      throw std::runtime_error("Inference failed for '" + modelPath_ +
                               "': " + e.what());
    }

    std::vector<Tensor> result;
    result.reserve(outputs.size());
    for (size_t i = 0; i < outputs.size(); i++) {
      result.push_back(copyOut(outputs[i], i));
    }
    return result;
  }

private:
  /// ORT hands back allocator-owned names; copied once so the char pointers
  /// Run() wants stay valid for the session's lifetime.
  void cacheIoNames() {
    Ort::AllocatorWithDefaultOptions allocator;

    const size_t inputCount = session_.GetInputCount();
    if (inputCount != 1) {
      // run() supplies exactly one buffer; feeding only the first input of a
      // multi-input model would produce plausible garbage.
      throw std::runtime_error("Model '" + modelPath_ + "' expects " +
                               std::to_string(inputCount) +
                               " inputs; this backend supplies exactly 1");
    }

    inputNames_.reserve(inputCount);
    for (size_t i = 0; i < inputCount; i++) {
      inputNames_.emplace_back(session_.GetInputNameAllocated(i, allocator).get());
    }

    const size_t outputCount = session_.GetOutputCount();
    outputNames_.reserve(outputCount);
    for (size_t i = 0; i < outputCount; i++) {
      outputNames_.emplace_back(session_.GetOutputNameAllocated(i, allocator).get());
    }

    // After both vectors are fully grown: a later push_back would reallocate
    // the strings and dangle these pointers.
    for (const auto &name : inputNames_) {
      inputNamePtrs_.push_back(name.c_str());
    }
    for (const auto &name : outputNames_) {
      outputNamePtrs_.push_back(name.c_str());
    }
  }

  /// Copies one output into core's owning Tensor.
  Tensor copyOut(Ort::Value &value, size_t index) const {
    if (!value.IsTensor()) {
      throw std::runtime_error("Output " + std::to_string(index) + " of '" +
                               modelPath_ + "' is not a tensor");
    }

    const auto info = value.GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      // Reading a non-float buffer as float is silent garbage.
      throw std::runtime_error("Output " + std::to_string(index) + " of '" +
                               modelPath_ +
                               "' is not float32; this backend moves floats only");
    }

    Tensor out;
    out.shape = info.GetShape();

    const float *data = value.GetTensorData<float>();
    out.data.assign(data, data + info.GetElementCount());
    return out;
  }

  std::string modelPath_;
  std::string provider_;
  Ort::Session session_{nullptr};

  std::vector<std::string> inputNames_;
  std::vector<std::string> outputNames_;
  std::vector<const char *> inputNamePtrs_;
  std::vector<const char *> outputNamePtrs_;
};

} // namespace

std::unique_ptr<InferenceSession> loadSession(const std::string &modelPath) {
  return std::make_unique<OnnxSession>(
      utils::PathUtils::stripFilePrefix(modelPath));
}

} // namespace inference
} // namespace cardscanner
