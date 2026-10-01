#include "ScannerService.h"

#include <DatabaseManager.h>
#include <ScannerRegistry.h>
#include <core/ScannerPipeline.h>

#include <chrono>
#include <iostream>

namespace cardscanner {
namespace desktop {

ScannerService::ScannerService(const ScannerConfig &config,
                              SessionConfig sessionConfig)
    : session_(sessionConfig) {
  // Keeps OpenCV's pool out of the inference runtime's way.
  cv::setNumThreads(0);

  ScannerRegistry::setConfig(config);
  ScannerRegistry::initializeModels();

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

Diagnostics ScannerService::diagnostics() const {
  std::lock_guard<std::mutex> lock(diagnosticsMutex_);
  return diagnostics_;
}

void ScannerService::setListener(Listener listener) {
  std::lock_guard<std::mutex> lock(listenerMutex_);
  listener_ = std::move(listener);
}

bool ScannerService::submitFrame(cv::Mat frame) {
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
  while (true) {
    cv::Mat job;
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
    if (!job.empty()) {
      try {
        auto &dbManager = DatabaseManager::getInstance();

        // Skip rather than block while a model swap owns the scanner.
        ScanLease lease;
        if (lease) {
          auto ctx = ScannerRegistry::getScannerContext();

          // Core throttles inside processFrame; repeating the rule here
          // would be a second copy to keep in sync.
          const auto result = core::ScannerPipeline::processFrame(
              job, ctx.config, dbManager, ctx.yoloModel.get(),
              ctx.embeddingModel.get(), ctx.setSymbolYoloModel.get(),
              ctx.setSymbolEmbedder.get(), ctx.fabColorClassifier.get(),
              &ctx.gameEmbeddingModels);

          // Recorded whether or not anything matched.
          Diagnostics diag;
          diag.hasFrame = true;
          diag.detections = int(result.cards.size());
          diag.processingMs = result.processingTimeMs;
          // Same rule ScanSession uses, so the outlined card and the named
          // card are the same one: best match score, falling back to detection
          // confidence when nothing matched.
          const cardscanner::ProcessedCard *best = nullptr;
          for (const auto &card : result.cards) {
            if (card.matches.empty()) {
              continue;
            }
            if (!best || card.matches[0].score > best->matches[0].score) {
              best = &card;
            }
          }
          if (best == nullptr) {
            for (const auto &card : result.cards) {
              if (!best || card.detectionConfidence > best->detectionConfidence) {
                best = &card;
              }
            }
          }
          if (best && !job.empty()) {
            diag.boxX = float(best->boundingBox.x) / float(job.cols);
            diag.boxY = float(best->boundingBox.y) / float(job.rows);
            diag.boxW = float(best->boundingBox.width) / float(job.cols);
            diag.boxH = float(best->boundingBox.height) / float(job.rows);
          }
          if (best) {
            diag.detectionConfidence = best->detectionConfidence;
            diag.predictedGame = best->predictedGameName;
            diag.predictedGameConfidence = best->predictedGameConfidence;
            if (!best->matches.empty()) {
              diag.topScore = best->matches[0].score;
              diag.topCardId = best->matches[0].cardId;
            }
          }
          {
            std::lock_guard<std::mutex> lock(diagnosticsMutex_);
            diag.accepted = session_.onScanResult(result);
            diagnostics_ = diag;
          }
          changed = true;
        }
      } catch (const std::exception &e) {
        // One bad frame must not take the server down.
        std::cerr << "scan failed: " << e.what() << "\n";
      }
    }

    changed = session_.tick() || changed;

    if (changed) {
      Listener listener;
      {
        std::lock_guard<std::mutex> lock(listenerMutex_);
        listener = listener_;
      }
      if (listener) {
        try {
          listener();
        } catch (const std::exception &e) {
          std::cerr << "listener threw: " << e.what() << "\n";
        }
      }
    }
  }
}

} // namespace desktop
} // namespace cardscanner
