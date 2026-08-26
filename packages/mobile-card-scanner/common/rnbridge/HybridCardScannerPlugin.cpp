#include "HybridCardScannerPlugin.hpp"
#include "ScannerRegistry.h"
#include "core/ScannerPipeline.h"
#include "FrameExtractor.h"
#include "FrameTransform.h"
#include "NitroSerializer.h"
#include <DatabaseManager.h>
#include <chrono>
#include <mutex>
#include <opencv2/core.hpp>
#include <optional>
#include <thread>
#include <utility>

namespace margelo::nitro::cardscanner {

namespace {
// Frames due this close to the throttle window are kept and the scan sleeps
// the gap - the next camera frame would arrive later than the window opens.
// Margin is adjusted for assumed 30 fps camera.
constexpr int kScanWaitMarginMs = 35;

// Pre-rotation buffer dims, captured while the frame is alive.
struct FrameSize {
  double width = 0;
  double height = 0;
};

// Frame accessors go through JSI/JNI and can throw once the Frame is gone.
std::optional<FrameSize> tryReadFrameSize(
    const std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> &frame) noexcept {
  try {
    if (frame == nullptr || !frame->getIsValid()) {
      return std::nullopt;
    }
    return FrameSize{frame->getWidth(), frame->getHeight()};
  } catch (...) {
    return std::nullopt;
  }
}

// Sole disposer of the frame (the caller and JS keep their own references).
// Disposes exactly once on every path, even if the pipeline throws.
class OwnedFrame {
public:
  explicit OwnedFrame(
      std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> frame) noexcept
      : frame_(std::move(frame)) {}
  ~OwnedFrame() { disposeFrame(); }
  OwnedFrame(const OwnedFrame &) = delete;
  OwnedFrame &operator=(const OwnedFrame &) = delete;

  const std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> &
  get() const noexcept {
    return frame_;
  }

  // Hands the buffer back to the camera.
  void disposeFrame() noexcept {
    if (frame_ == nullptr) {
      return;
    }
    try {
      frame_->dispose();
    } catch (...) {
    }
    frame_.reset();
  }

private:
  std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> frame_;
};
} // namespace

NitroDetection HybridCardScannerPlugin::runPipeline(
    const ::cardscanner::ExtractedFrame &extracted) {
  const cv::Mat &frameImage = extracted.image;
  const cv::Size rotatedSize = frameImage.size();

  auto ctx = ::cardscanner::ScannerRegistry::getScannerContext();

  if (!ctx.yoloModel || !ctx.embeddingModel) {
    throw std::runtime_error(
        "Models not initialized. Call initializeScanner() first.");
  }

  // Meyer's singleton; the store accessors the pipeline uses lock internally.
  auto &dbManager = ::cardscanner::DatabaseManager::getInstance();

  // Skip rather than block while a benchmark or a model swap owns the scanner.
  ::cardscanner::ScanLease lease;
  if (!lease) {
    return ::cardscanner::utils::NitroSerializer::serializeScanResult(
        ::cardscanner::ScanResult{});
  }

  auto result = ::cardscanner::core::ScannerPipeline::processFrame(
      frameImage, ctx.config, dbManager, ctx.yoloModel.get(),
      ctx.embeddingModel.get(), ctx.setSymbolYoloModel.get(),
      ctx.setSymbolEmbedder.get(), ctx.fabColorClassifier.get(),
      &ctx.gameEmbeddingModels);

  // Contract with JS: boxes go back in raw frame-buffer coordinates.
  for (auto &card : result.cards) {
    card.boundingBox = ::cardscanner::utils::inverseRotateBox(
        card.boundingBox, extracted.orientation, rotatedSize);
  }

  return ::cardscanner::utils::NitroSerializer::serializeScanResult(result);
}

void HybridCardScannerPlugin::scanFrame(
    const std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> &frame,
    const std::vector<double> &coordinateSnapshot) {
  // Every early return below disposes the Frame through this guard.
  OwnedFrame ownedFrame(frame);

  auto scanLock = ::cardscanner::tryClaimScan();
  if (!scanLock) {
    return;
  }

  const auto windowOpensAt = ::cardscanner::core::ScannerPipeline::mlWindowOpensAt(
      ::cardscanner::ScannerRegistry::getMaxFrameRate());
  if (std::chrono::steady_clock::now() +
          std::chrono::milliseconds(kScanWaitMarginMs) < windowOpensAt) {
    return;
  }

  const std::optional<FrameSize> frameSize = tryReadFrameSize(ownedFrame.get());
  if (!frameSize) {
    return;
  }

  NitroDetection detection = [&ownedFrame, windowOpensAt]() {
    try {
      ::cardscanner::ExtractedFrame extracted =
          ::cardscanner::FrameExtractor::extractFrame(ownedFrame.get());

      ownedFrame.disposeFrame();
      // No-op when the window is already open; processFrame consumes it after.
      std::this_thread::sleep_until(windowOpensAt);
      return runPipeline(extracted);
    } catch (const std::exception &e) {
      return ::cardscanner::utils::NitroSerializer::serializeError(
          std::string("Frame processing failed: ") + e.what());
    } catch (...) {
      return ::cardscanner::utils::NitroSerializer::serializeError(
          "Frame processing failed: unknown error");
    }
  }();
  // Hand the buffer back before unlocking and calling into JS - the Frame is
  // still held here only when extractFrame threw.
  ownedFrame.disposeFrame();

  // Emitting doesn't touch the pipeline; free the scan first
  scanLock.unlock();
  emitResult(NitroAsyncScanResult(std::move(detection), frameSize->width,
                                  frameSize->height, coordinateSnapshot));
}

void HybridCardScannerPlugin::setDetectionListener(
    const std::function<void(const NitroAsyncScanResult &)> &listener) {
  std::lock_guard<std::mutex> lock(listenerMutex_);
  detectionListener_ = listener;
}

void HybridCardScannerPlugin::clearDetectionListener() {
  std::lock_guard<std::mutex> lock(listenerMutex_);
  detectionListener_ = nullptr;
}

void HybridCardScannerPlugin::emitResult(const NitroAsyncScanResult &result) {
  std::function<void(const NitroAsyncScanResult &)> listenerSnapshot;
  {
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listenerSnapshot = detectionListener_;
  }
  if (listenerSnapshot) {
    try {
      listenerSnapshot(result);
    } catch (...) {
    }
  }
}

} // namespace margelo::nitro::cardscanner
