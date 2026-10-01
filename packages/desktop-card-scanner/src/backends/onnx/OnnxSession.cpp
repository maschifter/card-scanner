// The only translation unit that includes ONNX Runtime; everything above works
// against inference::InferenceSession. Mirrors the ExecuTorch backend in
// mobile-card-scanner: one backend per binary, selected by which one is linked.

#include <inference/InferenceSession.h>
#include <utils/PathUtils.h>

#include <onnxruntime_cxx_api.h>

#include <Log.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cardscanner {
namespace inference {

namespace {

/// One per process; ORT documents Env as safe to share across sessions.
///
/// Telemetry off. The macOS/Linux release dylib bundles Microsoft's 1DS
/// uploader, whose exit-time teardown races an in-flight upload into a
/// SIGABRT; only the env var keeps it from being created (the API call alone
/// leaves it live, and covers the ETW events of the Windows build).
Ort::Env &ortEnv() {
  static Ort::Env env = [] {
#if !defined(_WIN32)
    ::setenv("ORT_DISABLE_TELEMETRY", "1", 1);
#endif
    Ort::Env created(ORT_LOGGING_LEVEL_WARNING, "cardscanner");
    created.DisableTelemetryEvents();
    return created;
  }();
  return env;
}

/**
 * @brief Registers the first available accelerator, falling back to CPU.
 *
 * Overridable with CARD_SCANNER_EP, a comma-separated list. Registration
 * failing is normal, so each attempt is guarded and the winner is returned for
 * logging - a silent CPU fallback otherwise looks like acceleration working.
 */
std::string appendProviders(Ort::SessionOptions &options) {
  std::vector<std::string> ladder;
  if (const char *override = std::getenv("CARD_SCANNER_EP")) {
    std::istringstream spec(override);
    for (std::string name; std::getline(spec, name, ',');) {
      if (!name.empty()) {
        ladder.push_back(std::move(name));
      }
    }
  } else {
#if defined(__APPLE__)
    ladder = {"coreml", "cpu"};
#elif defined(_WIN32)
    ladder = {"dml", "cpu"};
#else
    ladder = {"cpu"};
#endif
  }

  for (const auto &name : ladder) {
    try {
#if defined(__APPLE__)
      if (name == "coreml") {
        // MLProgram format only; ORT still decides per subgraph what
        // CoreML can take.
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
      log(LOG_LEVEL::Error, "[Inference]", name, "unavailable, trying next:",
          e.what());
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

    provider_ = appendProviders(options);

    // Ort::Session takes ORTCHAR_T, which is wchar_t on Windows;
    // path::c_str() yields the native type on every platform.
    const std::filesystem::path modelPathNative(modelPath_);
    try {
      session_ = Ort::Session(ortEnv(), modelPathNative.c_str(), options);
    } catch (const Ort::Exception &e) {
      // DirectML registers fine but rejects ColorBarModel's graph at Initialize;
      // one model the GPU cannot build should run on CPU, not kill the scanner.
      if (provider_ == "cpu") {
        // ORT's exception does not carry the path, and callers need to know
        // which model failed.
        throw std::runtime_error("Failed to load model '" + modelPath_ +
                                 "': " + e.what());
      }
      log(LOG_LEVEL::Error, "[Inference]",
          std::filesystem::path(modelPath_).filename().string(), "build on",
          provider_, "failed, falling back to cpu:", e.what());
      Ort::SessionOptions cpuOptions;
      cpuOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      try {
        session_ = Ort::Session(ortEnv(), modelPathNative.c_str(), cpuOptions);
      } catch (const Ort::Exception &cpuError) {
        throw std::runtime_error("Failed to load model '" + modelPath_ +
                                 "': " + cpuError.what());
      }
      provider_ = "cpu";
    }

    cacheIoNames();

    // A failed registration is otherwise indistinguishable from a successful
    // one. filename() rather than find_last_of('/'): Windows paths use '\\'.
    log(LOG_LEVEL::Info, "[Inference]",
        std::filesystem::path(modelPath_).filename().string(), "on", provider_);
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

    // Run() takes const char* const*, so a borrowed pointer is unavoidable.
    const std::array<const char *, 1> inputNames{inputName_.c_str()};
    std::vector<Ort::Value> outputs;
    try {
      outputs = session_.Run(Ort::RunOptions{nullptr}, inputNames.data(),
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

    inputName_ = session_.GetInputNameAllocated(0, allocator).get();

    const size_t outputCount = session_.GetOutputCount();
    outputNames_.reserve(outputCount);
    for (size_t i = 0; i < outputCount; i++) {
      outputNames_.emplace_back(session_.GetOutputNameAllocated(i, allocator).get());
    }

    // After the vector is fully grown: a later push_back would reallocate
    // the strings and dangle these pointers.
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

  /// Exactly one input by contract; cacheIoNames enforces it.
  std::string inputName_;
  std::vector<std::string> outputNames_;
  std::vector<const char *> outputNamePtrs_;
};

} // namespace

std::unique_ptr<InferenceSession> loadSession(const std::string &modelPath) {
  return std::make_unique<OnnxSession>(
      utils::PathUtils::stripFilePrefix(modelPath));
}

} // namespace inference
} // namespace cardscanner
