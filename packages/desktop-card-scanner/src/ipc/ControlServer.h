#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cardscanner {
namespace ipc {

/**
 * @brief WebSocket endpoint the overlay connects to.
 *
 * One server for the whole process, not one per source, so a second filter can
 * attach. Payloads are JSON strings; this class moves them, it does not parse
 * them.
 */
class ControlServer {
public:
  using CommandHandler = std::function<void(const std::string &)>;

  explicit ControlServer(uint16_t port);
  ~ControlServer();

  ControlServer(const ControlServer &) = delete;
  ControlServer &operator=(const ControlServer &) = delete;

  /// @throws std::runtime_error if the port cannot be bound.
  void start();
  void stop();

  /// Sends to every connected client. Safe to call from any thread.
  void broadcast(const std::string &message);

  void setCommandHandler(CommandHandler handler);

  size_t clientCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ipc
} // namespace cardscanner
