#pragma once

#include <atomic>
#include <string>

namespace cardscanner {
namespace net {

struct HttpResponse {
  long status = 0;
  std::string body;
};

/// Blocking JSON POST. libcurl rather than IXWebSocket's HTTP client, which
/// the product endpoint answers with an HTML 500.
/// Aborts (status 0) once `running` turns false; polled about once a second.
HttpResponse postJson(const std::string &url, const std::string &body,
                      const std::atomic<bool> *running = nullptr);

/// Call once per process; curl_easy_init does it implicitly but not safely.
void globalInit();
void globalCleanup();

} // namespace net
} // namespace cardscanner
