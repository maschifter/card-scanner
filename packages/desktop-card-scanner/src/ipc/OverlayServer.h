#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace ix {
class HttpServer;
}

namespace cardscanner {
namespace ipc {

/**
 * @brief Serves the overlay page over HTTP.
 *
 * A URL rather than a file path: macOS treats a .plugin as a package, so no
 * file dialog can reach inside the bundle.
 */
class OverlayServer {
public:
  OverlayServer(uint16_t port, std::filesystem::path file);
  ~OverlayServer();

  OverlayServer(const OverlayServer &) = delete;
  OverlayServer &operator=(const OverlayServer &) = delete;

  /// @return false if the file is missing or the port is taken. The scanner
  ///         still runs; only the overlay is unavailable.
  bool start();
  void stop();

private:
  uint16_t port_;
  std::filesystem::path file_;
  std::unique_ptr<ix::HttpServer> server_;
};

} // namespace ipc
} // namespace cardscanner
