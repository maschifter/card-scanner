#pragma once

#include <string>

namespace cardscanner {

/// Per-scan overrides, applied to a local copy of the config.
struct ScanOptions {
  std::string scanMode;         // empty: the config's mode
  bool ignoreFrameRate = false; // stills bypass the live-feed throttle
  bool recordTimings = false;   // benchmark record around the pipeline
  bool live = false;            // camera frame: a multi-card layout may freeze
  bool forceFreeze = false;     // shutter: live frame freezes on any cards
};

} // namespace cardscanner
