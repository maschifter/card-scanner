#pragma once

#include "ProductClient.h"
#include "ScannerService.h"

#include <core/ScanSession.h>
#include <ipc/ControlServer.h>
#include <ipc/FrameProtocol.h>
#include <ipc/FrameServer.h>
#include <types/ScannerConfig.h>

#include <cstdint>
#include <string>

namespace cardscanner {
namespace desktop {

struct ServerOptions {
  uint64_t token = 0;
  uint16_t framePort = ipc::kDefaultFramePort;
  uint16_t controlPort = ipc::kDefaultControlPort;
  ProductSource products;
};

/**
 * @brief Everything the scanner process is, minus argument parsing.
 *
 * Owning the members here fixes their destruction order once: they unwind
 * bottom-up, so the service - which owns the worker thread that calls into
 * everything else - is destroyed first.
 */
class ScannerServer {
public:
  ScannerServer(const ScannerConfig &config, core::SessionConfig sessionConfig,
                ServerOptions options);
  ~ScannerServer();

  ScannerServer(const ScannerServer &) = delete;
  ScannerServer &operator=(const ScannerServer &) = delete;

  /// @throws std::runtime_error if a port cannot be bound.
  void start();
  void stop();

  /// Feeds one frame to the scanner. The frame socket and the replay
  /// harness both land here.
  void submitFrame(Frame frame);

  ScannerService &service() { return service_; }
  uint64_t framesScanned() const;
  uint64_t framesRejected() const;

private:
  void broadcastState();
  /// Fires on the worker thread after every frame or tick that changed state.
  void onScanUpdate();
  void handleCommand(const std::string &raw);

  ServerOptions options_;

  // Declaration order reversed is destruction order: service_ last means it is
  // destroyed first, so the worker is joined before anything its listener -
  // fixed at construction - calls into.
  ipc::ControlServer control_;
  ProductClient products_;
  ipc::FrameServer frames_;
  ScannerService service_;
};

} // namespace desktop
} // namespace cardscanner
