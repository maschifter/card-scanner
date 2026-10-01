#include "HybridCardScannerPlugin.hpp"
#include "FrameExtractor.h"
#include "FrameTransform.h"
#include "NitroSerializer.h"
#include "ScannerRegistry.h"
#include "core/ScannerPipeline.h"
#include <DatabaseManager.h>
#include <atomic>
#include <chrono>
#include <memory>
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
constexpr int kScanWaitMarginMs = 20;

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

// Which plugin instance owns the process-global session listener. Only
// compared, never dereferenced.
std::atomic<const HybridCardScannerPlugin *> sessionListenerOwner{nullptr};

// A `multi*` result: upright-frame coords, no coordinate snapshot.
NitroAsyncScanResult multiResult(std::string type, NitroDetection detection,
                                 const cv::Size &frameSize,
                                 std::optional<std::string> frameUri,
                                 std::optional<double> cardIndex,
                                 double total) {
  NitroAsyncScanResult result;
  result.type = std::move(type);
  result.detection = std::move(detection);
  result.frameWidth = frameSize.width;
  result.frameHeight = frameSize.height;
  result.frameUri = std::move(frameUri);
  result.cardIndex = cardIndex;
  result.total = total;
  return result;
}
} // namespace

HybridCardScannerPlugin::~HybridCardScannerPlugin() {
  clearDetectionListener();
}

std::optional<HybridCardScannerPlugin::FrameOutcome>
HybridCardScannerPlugin::runPipeline(
    const ::cardscanner::ExtractedFrame &extracted, bool shutter) {
  const cv::Size rotatedSize = extracted.image.size();

  ::cardscanner::ScanOptions options;
  options.live = true;
  if (shutter) {
    // One frame scanned as a page, frozen on whatever cards it holds.
    options.scanMode = "multiple";
    options.forceFreeze = true;
  }
  auto scanned = ::cardscanner::ScannerRegistry::scan(extracted.image, options);
  if (!scanned) {
    // Busy: skip rather than block.
    return FrameOutcome{::cardscanner::utils::NitroSerializer::serializeScanResult(
                            ::cardscanner::ScanResult{}),
                        std::nullopt};
  }
  if (scanned->frozen) {
    return std::nullopt;
  }

  // Contract with JS: boxes go back in raw frame-buffer coordinates.
  for (auto &card : scanned->cards) {
    card.boundingBox = ::cardscanner::utils::inverseRotateBox(
        card.boundingBox, extracted.orientation, rotatedSize);
    for (auto &p : card.quad) {
      p = ::cardscanner::utils::inverseRotatePoint(p, extracted.orientation,
                                                   rotatedSize);
    }
  }

  return FrameOutcome{
      ::cardscanner::utils::NitroSerializer::serializeScanResult(*scanned),
      scanned->multi && !scanned->multi->qualifies
          ? std::optional<std::string>(scanned->multi->reason)
          : std::nullopt,
      scanned->multi && scanned->multi->qualifies};
}

void HybridCardScannerPlugin::onMultiEvent(
    const ::cardscanner::core::MultiScanSession::Event &event) {
  using Type = ::cardscanner::core::MultiScanSession::Event::Type;
  using ::cardscanner::utils::NitroSerializer;
  const double total = static_cast<double>(event.total);
  switch (event.type) {
  case Type::Started:
    emitResult(multiResult(
        "multiStart", NitroSerializer::serializePlaceholders(event.slots),
        event.frameSize, event.frameImagePath, std::nullopt, total));
    break;
  case Type::CardResolved:
    emitResult(multiResult(
        "multiCard", NitroSerializer::serializeCard(event.card),
        event.frameSize, std::nullopt, static_cast<double>(event.index), total));
    break;
  case Type::Ended:
    emitResult(multiResult("multiEnd",
                           NitroSerializer::serializeScanResult(
                               ::cardscanner::ScanResult{}),
                           event.frameSize, std::nullopt, std::nullopt, total));
    break;
  }
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

  // Taken only by a frame that reaches the pipeline, so a dropped frame does
  // not use up the user's shutter press.
  const bool shutter = shutterRequested_.exchange(false);
  std::optional<FrameOutcome> outcome = [&ownedFrame, windowOpensAt,
                                         shutter]() {
    try {
      ::cardscanner::ExtractedFrame extracted =
          ::cardscanner::FrameExtractor::extractFrame(ownedFrame.get());

      ownedFrame.disposeFrame();
      // No-op when the window is already open; processFrame consumes it after.
      std::this_thread::sleep_until(windowOpensAt);
      return runPipeline(extracted, shutter);
    } catch (const std::exception &e) {
      return std::optional<FrameOutcome>(FrameOutcome{
          ::cardscanner::utils::NitroSerializer::serializeError(
              std::string("Frame processing failed: ") + e.what()),
          std::nullopt});
    } catch (...) {
      return std::optional<FrameOutcome>(FrameOutcome{
          ::cardscanner::utils::NitroSerializer::serializeError(
              "Frame processing failed: unknown error"),
          std::nullopt});
    }
  }();
  // Hand the buffer back before unlocking and calling into JS - the Frame is
  // still held here only when extractFrame threw.
  ownedFrame.disposeFrame();

  // Emitting doesn't touch the pipeline; free the scan first. A frozen frame
  // already went out as the multi stream.
  scanLock.unlock();
  if (!outcome) {
    return;
  }
  NitroAsyncScanResult result;
  result.detection = std::move(outcome->detection);
  result.frameWidth = frameSize->width;
  result.frameHeight = frameSize->height;
  result.coordinateSnapshot = coordinateSnapshot;
  result.multiRejectReason = std::move(outcome->multiRejectReason);
  if (outcome->multi) {
    result.multi = true;
  }
  emitResult(result);
}

void HybridCardScannerPlugin::setDetectionListener(
    const std::function<void(const NitroAsyncScanResult &)> &listener) {
  {
    std::lock_guard<std::mutex> lock(listenerMutex_);
    detectionListener_ = listener;
  }
  // Weak token: the global session can outlive this object.
  std::weak_ptr<HybridObject> alive = shared_from_this();
  sessionListenerOwner.store(this);
  ::cardscanner::ScannerRegistry::multiScanSession().setListener(
      [alive, this](const ::cardscanner::core::MultiScanSession::Event &event) {
        if (auto lock = alive.lock()) {
          onMultiEvent(event);
        }
      });
}

void HybridCardScannerPlugin::clearDetectionListener() {
  // Only the instance that registered the session listener may clear it, or
  // a dying instance would silence the one JS just created.
  const HybridCardScannerPlugin *self = this;
  if (sessionListenerOwner.compare_exchange_strong(self, nullptr)) {
    ::cardscanner::ScannerRegistry::multiScanSession().setListener(nullptr);
  }
  std::lock_guard<std::mutex> lock(listenerMutex_);
  detectionListener_ = nullptr;
}

void HybridCardScannerPlugin::requestShutter(bool requested) {
  shutterRequested_.store(requested);
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
