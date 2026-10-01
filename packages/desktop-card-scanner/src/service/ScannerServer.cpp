#include "ScannerServer.h"

#include <protocol/StateMessage.h>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace cardscanner {
namespace desktop {

ScannerServer::ScannerServer(const ScannerConfig &config,
                             core::SessionConfig sessionConfig,
                             ServerOptions options)
    : options_(std::move(options)),
      overlay_(options_.overlayPort, options_.overlayFile),
      control_(options_.controlPort), products_(options_.products),
      frames_(options_.framePort, options_.token,
              [this](cv::Mat rgb, uint64_t sequence, cv::Rect2f roi, bool reportTimings) {
                service_.setReportTimings(reportTimings);
                submitFrame({std::move(rgb), sequence, roi});
              }),
      // The listener fires only once start() spins up the worker; declaration
      // order keeps everything it reaches alive longer than service_.
      service_(config, sessionConfig, [this] { onScanUpdate(); }) {}

ScannerServer::~ScannerServer() { stop(); }

void ScannerServer::broadcastState() {
  control_.broadcast(stateMessage(service_, products_));
}

void ScannerServer::onScanUpdate() {
  // The box first: the 40-byte outline for the module is latency-sensitive
  // and should not queue behind the JSON build and websocket sends.
  const auto d = service_.diagnostics();
  frames_.sendResult(d.sequence, d.roi, d.detections > 0, d.accepted, d.boxX, d.boxY,
                     d.boxW, d.boxH, d.detectionConfidence, d.topScore);
  broadcastState();
}

void ScannerServer::start() {
  control_.setCommandHandler([this](const std::string &raw) { handleCommand(raw); });
  control_.start();

  // A late name resolution is a second broadcast, not a blocked scan.
  products_.start([this](const std::string &) { broadcastState(); });

  if (!options_.overlayFile.empty()) {
    overlay_.start();
  }

  frames_.start();

  // Last: only now can the listener fire, so everything it reaches is not
  // just constructed but started.
  service_.start();
}

namespace {

void applyCommand(ScannerService &service, const nlohmann::json &message) {
  const std::string command = message.value("command", "");
  auto &session = service.session();

  if (command == "set_mode") {
    core::ScanSession::Mode mode = core::ScanSession::Mode::Auto;
    if (parseMode(message.value("mode", ""), mode)) {
      session.setMode(mode);
    }
  } else if (command == "emit_current") {
    session.emitCurrent();
  } else if (command == "clear_emitted") {
    session.clearEmitted();
  } else if (command == "emit_from_history") {
    session.emitFromHistory(message.value("cardId", ""));
  } else if (command == "set_settings") {
    auto config = session.config();
    config.acceptScore = message.value("acceptScore", config.acceptScore);
    config.stableDetections = message.value("stableDetections", config.stableDetections);
    config.gracePeriodMs = message.value("gracePeriodMs", config.gracePeriodMs);
    config.emittedTimeoutMs = message.value("emittedTimeoutMs", config.emittedTimeoutMs);
    // Loopback is not an authorisation boundary; clamp what the socket sends.
    config.acceptScore = std::clamp(config.acceptScore, 0.0f, 1.0f);
    config.stableDetections = std::max(1, config.stableDetections);
    config.gracePeriodMs = std::max(0, config.gracePeriodMs);
    config.emittedTimeoutMs = std::max(0, config.emittedTimeoutMs);
    session.setConfig(config);
  }
}

} // namespace

void ScannerServer::handleCommand(const std::string &raw) {
  try {
    applyCommand(service_, nlohmann::json::parse(raw));
  } catch (const std::exception &) {
    return; // the socket carries whatever a browser sends
  }
  // Answer even when nothing changed, so the dock confirms from the reply.
  broadcastState();
}

void ScannerServer::stop() {
  // Join the worker first so the listener cannot fire during teardown; late
  // frames until frames_.stop() are dropped on submitFrame's shutdown_ guard.
  service_.stop();

  frames_.stop();
  products_.stop();
  overlay_.stop();
  control_.stop();
}

void ScannerServer::submitFrame(Frame frame) { service_.submitFrame(std::move(frame)); }

uint64_t ScannerServer::framesScanned() const { return frames_.framesScanned(); }

uint64_t ScannerServer::framesRejected() const { return frames_.framesRejected(); }

} // namespace desktop
} // namespace cardscanner
