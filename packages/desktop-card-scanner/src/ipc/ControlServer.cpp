#include "ControlServer.h"

#include <Log.h>

#include <ixwebsocket/IXWebSocketServer.h>

#include <mutex>
#include <set>
#include <stdexcept>

namespace cardscanner {
namespace ipc {

namespace {

/// A page on a loopback host: the overlay or the dock served by the dev server.
bool loopbackOrigin(const std::string &origin) {
  const auto scheme = origin.find("://");
  if (scheme == std::string::npos) {
    return false; // "null", or not an origin at all
  }
  const size_t hostStart = scheme + 3;
  const size_t hostEnd = origin.find(':', hostStart);
  const std::string host = origin.substr(
      hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
  return host == "127.0.0.1" || host == "localhost";
}

/// Local process (no Origin), loopback page, or OBS's own browser: Origin "null" with its UA.
bool browserAllowed(const ix::WebSocketHttpHeaders &headers) {
  const auto origin = headers.find("Origin");
  if (origin == headers.end()) {
    return true;
  }
  if (loopbackOrigin(origin->second)) {
    return true;
  }
  const auto agent = headers.find("User-Agent");
  return origin->second == "null" && agent != headers.end() &&
         agent->second.find("OBS/") != std::string::npos;
}

} // namespace

struct ControlServer::Impl {
  explicit Impl(uint16_t port)
      // Loopback only: this carries what is on the user's camera table.
      : server(port, "127.0.0.1") {}

  ix::WebSocketServer server;
  mutable std::mutex mutex;
  CommandHandler handler;
  std::string lastSnapshot;
};

ControlServer::ControlServer(uint16_t port) : impl_(std::make_unique<Impl>(port)) {}

ControlServer::~ControlServer() { stop(); }

void ControlServer::setCommandHandler(CommandHandler handler) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->handler = std::move(handler);
}

void ControlServer::start() {
  impl_->server.setOnClientMessageCallback(
      [this](std::shared_ptr<ix::ConnectionState>, ix::WebSocket &socket,
             const ix::WebSocketMessagePtr &msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
          const auto &headers = msg->openInfo.headers;
          const auto origin = headers.find("Origin");
          if (!browserAllowed(headers)) {
            // Only a page can be turned away, so an Origin is present here.
            log(LOG_LEVEL::Error, "[Control]", "closed a client from",
                origin->second);
            // 1008, policy violation. The handshake is done by now, so this
            // is the earliest a client can be turned away.
            socket.close(1008, "origin not allowed");
            return;
          }
          log(LOG_LEVEL::Info, "[Control]", "client connected from",
              origin == headers.end() ? std::string("a local process")
                                      : origin->second);
          std::string snapshot;
          {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            snapshot = impl_->lastSnapshot;
          }
          // Sent on connect so a reloading client is not blank until the
          // next state change.
          if (!snapshot.empty()) {
            socket.send(snapshot);
          }
          return;
        }

        if (msg->type == ix::WebSocketMessageType::Message) {
          // A turned-away client's close is still in flight.
          if (socket.getReadyState() != ix::ReadyState::Open) {
            return;
          }
          CommandHandler handler;
          {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            handler = impl_->handler;
          }
          if (handler) {
            try {
              handler(msg->str);
            } catch (const std::exception &e) {
              log(LOG_LEVEL::Error, "[Control]", "command failed:", e.what());
            }
          }
        }
      });

  const auto result = impl_->server.listen();
  if (!result.first) {
    throw std::runtime_error("control server: " + result.second);
  }
  impl_->server.start();
}

void ControlServer::stop() {
  if (impl_) {
    impl_->server.stop();
  }
}

void ControlServer::broadcast(const std::string &message) {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->lastSnapshot = message;
  }
  // The server tracks live connections; no second registry to drift.
  for (const auto &client : impl_->server.getClients()) {
    client->send(message);
  }
}

} // namespace ipc
} // namespace cardscanner
