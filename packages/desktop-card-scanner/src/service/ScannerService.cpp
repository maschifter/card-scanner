#include "ScannerService.h"

#include <DatabaseManager.h>
#include <ScannerRegistry.h>
#include <benchmark/BenchmarkCollector.h>

#include <util/Log.h>
#include <util/OpenCvThreads.h>

#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace cardscanner {
namespace desktop {

ScannerService::ScannerService(const ScannerConfig &config,
                               SessionConfig sessionConfig, Listener listener)
    : session_(sessionConfig), listener_(std::move(listener)) {
  util::configureOpenCvThreads();

  ScannerRegistry::setConfig(config);
  ScannerRegistry::initializeModels();
}

void ScannerService::start() {
  worker_ = std::thread([this] { workerLoop(); });
}

ScannerService::~ScannerService() {
  stop();
  ScannerRegistry::releaseModels();
}

void ScannerService::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
  }
  pending_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void ScannerService::setReportTimings(bool enabled) {
  reportTimings_.store(enabled, std::memory_order_relaxed);
#if !CARDSCANNER_BENCHMARK
  if (enabled) {
    static std::once_flag warned;
    std::call_once(warned, [] {
      util::logLine("perf", "timings are switched on, but this build compiled "
                            "the timers out (CARDSCANNER_BENCHMARK=0)");
    });
  }
#endif
}

Diagnostics ScannerService::diagnostics() const {
  std::lock_guard<std::mutex> lock(diagnosticsMutex_);
  return diagnostics_;
}

bool ScannerService::submitFrame(Frame frame) {
  bool evicted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
      return false;
    }
    evicted = mailbox_.has_value();
    // cv::Mat is refcounted, so ownership transfers without a pixel copy.
    mailbox_ = std::move(frame);
  }
  pending_.notify_one();
  return !evicted;
}

void ScannerService::workerLoop() {
  // Per-second averages for the [perf] log line. Worker-locals, so no lock.
  auto perfWindowStart = std::chrono::steady_clock::now();
  int perfFrames = 0;
  double perfYolo = 0, perfPreproc = 0, perfEmbed = 0, perfDbSearch = 0,
         perfTotal = 0;
  auto lastFrameAt = std::chrono::steady_clock::now();

  while (true) {
    Frame job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Also wakes periodically so the session's timers still fire when the
      // feed goes quiet.
      pending_.wait_for(lock, std::chrono::milliseconds(100),
                        [this] { return shutdown_ || mailbox_.has_value(); });
      if (shutdown_) {
        return;
      }
      if (mailbox_) {
        job = std::move(*mailbox_);
        mailbox_.reset();
      }
    }

    bool changed = false;
    if (!job.image.empty()) {
      lastFrameAt = std::chrono::steady_clock::now();
      try {
        auto &dbManager = DatabaseManager::getInstance();

        using benchmark::BenchmarkCollector;
        using benchmark::Stage;

        // Off, the frame runs unmeasured: with no record open the
        // collector ignores every write the pipeline makes.
        const bool timings = reportTimings_.load(std::memory_order_relaxed);
        ScanOptions options;
        options.recordTimings = timings;
        const auto scanned = ScannerRegistry::scan(job.image, dbManager, options);
        if (scanned) {
          const ScanResult &result = *scanned;

          // Recorded whether or not anything matched.
          Diagnostics diag;
          diag.hasFrame = true;
          diag.sequence = job.sequence;
          diag.roi = job.roi;
          diag.detections = int(result.cards.size());
          diag.processingMs = result.processingTimeMs;
          diag.timingsEnabled = timings;
          if (timings) {
            diag.measured =
                BenchmarkCollector::getBenchmarkedRan(Stage::YoloSegmentation);
          }
          if (diag.measured) {
            diag.yoloMs =
                BenchmarkCollector::getBenchmarkedMs(Stage::YoloSegmentation);
            diag.preprocMs =
                BenchmarkCollector::getBenchmarkedMs(Stage::Preproc);
            diag.embedMs = BenchmarkCollector::getBenchmarkedMs(Stage::Embed);
            diag.dbSearchMs =
                BenchmarkCollector::getBenchmarkedMs(Stage::DbSearch);
          }
          if (const ProcessedCard *best = bestVisibleCard(result)) {
            diag.boxX = float(best->boundingBox.x) / float(job.image.cols);
            diag.boxY = float(best->boundingBox.y) / float(job.image.rows);
            diag.boxW = float(best->boundingBox.width) / float(job.image.cols);
            diag.boxH = float(best->boundingBox.height) / float(job.image.rows);
            diag.detectionConfidence = best->detectionConfidence;
            diag.predictedGame = best->predictedGameName;
            diag.predictedGameConfidence = best->predictedGameConfidence;
            if (best->hasMatches()) {
              diag.topScore = best->matches[0].score;
              diag.topCardId = best->matches[0].cardId;
            }
          }
          // One session call under one lock: the accepted answer and the
          // state it describes cannot come from two different moments.
          diag.accepted = session_.onScanResult(result);
          {
            std::lock_guard<std::mutex> lock(diagnosticsMutex_);
            diagnostics_ = diag;
          }
          changed = true;

          if (diag.measured) {
            perfFrames++;
            perfYolo += diag.yoloMs;
            perfPreproc += diag.preprocMs;
            perfEmbed += diag.embedMs;
            perfDbSearch += diag.dbSearchMs;
            perfTotal += diag.processingMs;
          }
        }
      } catch (const std::exception &e) {
        // One bad frame must not take the server down.
        util::logLine("scan", e.what());
      }
    }

    // The feed went quiet: the last readout must not stand as if current.
    if (std::chrono::steady_clock::now() - lastFrameAt > kFeedTimeout) {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      if (diagnostics_.hasFrame) {
        diagnostics_ = Diagnostics{};
        changed = true;
      }
    }

    // Outside the frame block: the loop wakes every 100ms regardless, so the
    // last window still lands once the feed goes quiet.
    if (perfFrames > 0) {
      const auto now = std::chrono::steady_clock::now();
      if (now - perfWindowStart >= std::chrono::seconds(1)) {
        std::ostringstream line;
        line << perfFrames << " frame(s), avg ms  yolo " << std::fixed
             << std::setprecision(1) << perfYolo / perfFrames << "  preproc "
             << perfPreproc / perfFrames << "  embed "
             << perfEmbed / perfFrames << "  dbSearch "
             << perfDbSearch / perfFrames << "  total "
             << perfTotal / perfFrames;
        util::logLine("perf", line.str());
        perfWindowStart = now;
        perfFrames = 0;
        perfYolo = perfPreproc = perfEmbed = perfDbSearch = perfTotal = 0;
      }
    }

    changed = session_.tick() || changed;

    if (changed) {
      try {
        listener_();
      } catch (const std::exception &e) {
        util::logLine("scan", std::string("listener threw: ") + e.what());
      }
    }
  }
}

} // namespace desktop
} // namespace cardscanner
