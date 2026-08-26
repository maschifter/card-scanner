#pragma once

#include "HybridCardScannerPluginSpec.hpp"
#include "FrameExtractor.h"

#include <functional>
#include <mutex>
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

  /** @brief Runs the pipeline on the calling thread, emits to the listener if
   *  one is set. Owns the Frame from the call on and disposes it exactly once
   *  on every path. */
  void scanFrame(
      const std::shared_ptr<margelo::nitro::camera::HybridFrameSpec> &frame,
      const std::vector<double> &coordinateSnapshot) override;

  void setDetectionListener(
      const std::function<void(const NitroAsyncScanResult &)> &listener)
      override;

  void clearDetectionListener() override;

private:
  /** @brief Pipeline on an upright frame, serialized with boxes mapped back
   *  to raw buffer coords. @throws on failure. */
  static NitroDetection
  runPipeline(const ::cardscanner::ExtractedFrame &extracted);

  /** @brief Snapshots the listener under listenerMutex_, then invokes it
   *  without the lock. */
  void emitResult(const NitroAsyncScanResult &result);

  std::mutex listenerMutex_;
  /** Guarded by listenerMutex_; invoked without the lock held. */
  std::function<void(const NitroAsyncScanResult &)> detectionListener_;
};

} // namespace margelo::nitro::cardscanner
