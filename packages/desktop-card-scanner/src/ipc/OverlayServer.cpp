#include "OverlayServer.h"

#include <ixwebsocket/IXHttpServer.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace cardscanner {
namespace ipc {

OverlayServer::OverlayServer(uint16_t port, std::filesystem::path file)
    : port_(port), file_(std::move(file)) {}

OverlayServer::~OverlayServer() { stop(); }

bool OverlayServer::start() {
  if (!std::filesystem::exists(file_)) {
    std::cerr << "overlay not found at " << file_ << "; the URL will 404\n";
    return false;
  }

  server_ = std::make_unique<ix::HttpServer>(port_, "127.0.0.1");
  const auto file = file_;
  server_->setOnConnectionCallback(
      [file](ix::HttpRequestPtr, std::shared_ptr<ix::ConnectionState>) {
        // Per request rather than cached, so a rebuilt overlay needs no
        // server restart.
        std::ifstream stream(file);
        std::stringstream buffer;
        buffer << stream.rdbuf();

        ix::WebSocketHttpHeaders headers;
        headers["Content-Type"] = "text/html; charset=utf-8";
        headers["Cache-Control"] = "no-store";
        return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok,
                                                  headers, buffer.str());
      });

  const auto result = server_->listen();
  if (!result.first) {
    std::cerr << "overlay server: " << result.second << "\n";
    server_.reset();
    return false;
  }
  server_->start();
  return true;
}

void OverlayServer::stop() {
  if (server_) {
    server_->stop();
    server_.reset();
  }
}

} // namespace ipc
} // namespace cardscanner
