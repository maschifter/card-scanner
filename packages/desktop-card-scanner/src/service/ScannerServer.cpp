#include "ScannerServer.h"

#include "../protocol/StateMessage.h"

#include <nlohmann/json.hpp>

#include <iostream>

namespace cardscanner {
namespace desktop {

ScannerServer::ScannerServer(const ScannerConfig &config, SessionConfig sessionConfig,
                             ServerOptions options)
    : options_(std::move(options)), control_(options_.controlPort),
      products_(options_.productEndpoint), service_(config, sessionConfig) {}

ScannerServer::~ScannerServer() { stop(); }

void ScannerServer::broadcastState() {
  control_.broadcast(stateMessage(service_, products_));
}

void ScannerServer::start() {
  control_.setCommandHandler([this](const std::string &raw) { handleCommand(raw); });
  control_.start();

  // A late name resolution is a second broadcast, not a blocked scan.
  products_.start([this](const std::string &) { broadcastState(); });

  if (!options_.overlayFile.empty()) {
    overlay_ = std::make_unique<ipc::OverlayServer>(options_.overlayPort,
                                                    options_.overlayFile);
    overlay_->start();
  }

  frames_ = std::make_unique<ipc::FrameServer>(
      options_.framePort, options_.token,
      [this](cv::Mat frame) { service_.submitFrame(std::move(frame)); });
  frames_->start();

  service_.setListener([this] {
    broadcastState();
    // The box lets the module outline what was found, not just the region.
    const auto d = service_.diagnostics();
    frames_->sendResult(frames_->lastSequence(), d.detections > 0, d.accepted, d.boxX,
                        d.boxY, d.boxW, d.boxH, d.detectionConfidence, d.topScore);
  });
}

void ScannerServer::handleCommand(const std::string &raw) {
  nlohmann::json message;
  try {
    message = nlohmann::json::parse(raw);
  } catch (const std::exception &) {
    return; // the socket carries whatever a browser sends
  }

  const std::string command = message.value("command", "");
  auto &session = service_.session();
  bool changed = false;

  if (command == "set_mode") {
    Mode mode = Mode::Auto;
    if (parseMode(message.value("mode", ""), mode)) {
      changed = session.setMode(mode);
    }
  } else if (command == "emit_current") {
    changed = session.emitCurrent();
  } else if (command == "clear_emitted") {
    changed = session.clearEmitted();
  } else if (command == "emit_from_history") {
    changed = session.emitFromHistory(message.value("cardId", ""));
  } else if (command == "set_settings") {
    auto config = session.config();
    config.acceptScore = message.value("acceptScore", config.acceptScore);
    config.stableDetections = message.value("stableDetections", config.stableDetections);
    config.gracePeriodMs = message.value("gracePeriodMs", config.gracePeriodMs);
    config.emittedTimeoutMs = message.value("emittedTimeoutMs", config.emittedTimeoutMs);
    session.setConfig(config);
    changed = true;
  }

  // Answer even when nothing changed, so the dock confirms from the reply
  // rather than assuming.
  (void)changed;
  broadcastState();
}

void ScannerServer::stop() {
  // Detach and join before anything the listener reaches for is torn down.
  service_.setListener(nullptr);
  service_.stop();

  if (frames_) {
    frames_->stop();
  }
  products_.stop();
  if (overlay_) {
    overlay_->stop();
  }
  control_.stop();
}

void ScannerServer::submitFrame(cv::Mat frame) {
  service_.submitFrame(std::move(frame));
}

uint64_t ScannerServer::framesReceived() const {
  return frames_ ? frames_->framesReceived() : 0;
}

} // namespace desktop
} // namespace cardscanner
