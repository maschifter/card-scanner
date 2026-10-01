#include "ControlServer.h"

#include <ixwebsocket/IXWebSocketServer.h>

#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>

namespace cardscanner {
namespace ipc {

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
          CommandHandler handler;
          {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            handler = impl_->handler;
          }
          if (handler) {
            try {
              handler(msg->str);
            } catch (const std::exception &e) {
              std::cerr << "command failed: " << e.what() << "\n";
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

size_t ControlServer::clientCount() const { return impl_->server.getClients().size(); }

} // namespace ipc
} // namespace cardscanner
