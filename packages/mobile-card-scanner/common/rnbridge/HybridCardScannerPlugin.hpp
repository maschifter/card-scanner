#pragma once

#include "FrameExtractor.h"
#include "HybridCardScannerPluginSpec.hpp"
#include "core/MultiScanSession.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Nitrogen puts this plugin in margelo::nitro::cardscanner, and the core
// library is plain ::cardscanner. The leaf names are identical, so an
// unqualified `cardscanner::` inside here resolves to THIS namespace, not the
// core one. Every reference to core below is therefore rooted with a leading
// `::` - do not remove it.
namespace margelo::nitro::cardscanner {

/** @brief VisionCamera v5 frame plugin (replaces v4's `global.scanFramePlugin`):
 *  scanFrame runs on the caller's AsyncRunner task, results go to the listener. */
class HybridCardScannerPlugin : public HybridCardScannerPluginSpec {
public:
  HybridCardScannerPlugin() : HybridObject(TAG) {}
  ~HybridCardScannerPlugin() override;

  /** @brief Runs the pipeline on the calling thread, emits to the listener if
   *  one is set. Owns the Frame from the call on and disposes it exactly once
   *  on every path. */
  void scanFrame(
      const std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> &frame,
      const std::vector<double> &coordinateSnapshot) override;

  /** @brief Claims both listener slots: this one for JS, and the multi-scan
   *  session's for the freeze stream. Replaces any previous listener. */
  void setDetectionListener(
      const std::function<void(const NitroAsyncScanResult &)> &listener)
      override;

  /** @brief Releases both. Pending results are dropped. */
  void clearDetectionListener() override;

  /** @brief Arms or disarms the one-shot shutter for the next scanned frame. */
  void requestShutter(bool requested) override;

private:
  struct FrameOutcome {
    NitroDetection detection;
    std::optional<std::string> multiRejectReason;
    bool multi = false;
  };

  /** @brief One live scan of an upright frame, boxes and quads mapped back to
   *  raw buffer coords. Nullopt when the frame froze: its cards already went
   *  out as the multi stream, in upright coords. @throws on failure. */
  static std::optional<FrameOutcome>
  runPipeline(const ::cardscanner::ExtractedFrame &extracted, bool shutter);

  /** @brief Session event -> `multi*` result on the JS listener. Runs on the
   *  scanning thread, outside the session lock. */
  void onMultiEvent(const ::cardscanner::core::MultiScanSession::Event &event);

  /** @brief Snapshots the listener under listenerMutex_, then invokes it
   *  without the lock. */
  void emitResult(const NitroAsyncScanResult &result);

  /** Armed by requestShutter; taken by the first frame to reach the pipeline. */
  std::atomic<bool> shutterRequested_{false};

  std::mutex listenerMutex_;
  /** Guarded by listenerMutex_; invoked without the lock held. */
  std::function<void(const NitroAsyncScanResult &)> detectionListener_;
};

} // namespace margelo::nitro::cardscanner
