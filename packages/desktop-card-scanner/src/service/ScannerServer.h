#pragma once

#include "ProductClient.h"
#include "ScanSession.h"
#include "ScannerService.h"

#include <ipc/ControlServer.h>
#include <ipc/FrameServer.h>
#include <ipc/OverlayServer.h>
#include <types/ScannerConfig.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace cardscanner {
namespace desktop {

struct ServerOptions {
  uint64_t token = 0;
  uint16_t framePort = 27846;
  uint16_t controlPort = 27845;
  uint16_t overlayPort = 27847;
  std::filesystem::path overlayFile;
  std::string productEndpoint = "https://api.cardnexus.com/orpc/product/getProduct";
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
  ScannerServer(const ScannerConfig &config, SessionConfig sessionConfig,
                ServerOptions options);
  ~ScannerServer();

  ScannerServer(const ScannerServer &) = delete;
  ScannerServer &operator=(const ScannerServer &) = delete;

  /// @throws std::runtime_error if a port cannot be bound.
  void start();
  void stop();

  /// Frames from somewhere other than the socket; used by the replay harness.
  void submitFrame(cv::Mat frame);

  ScannerService &service() { return service_; }
  uint64_t framesReceived() const;

private:
  void broadcastState();
  void handleCommand(const std::string &raw);

  ServerOptions options_;

  // Declaration order reversed is destruction order: service_ last means it is
  // destroyed first, so the worker is joined before anything it calls into.
  std::unique_ptr<ipc::OverlayServer> overlay_;
  ipc::ControlServer control_;
  ProductClient products_;
  std::unique_ptr<ipc::FrameServer> frames_;
  ScannerService service_;
};

} // namespace desktop
} // namespace cardscanner
