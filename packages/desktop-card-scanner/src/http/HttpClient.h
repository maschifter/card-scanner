#pragma once

#include <atomic>
#include <string>

namespace cardscanner {
namespace http {

struct HttpResponse {
  long status = 0; // stays 0 when the transfer itself failed; see error
  std::string body;
  std::string error;
};

/// Blocking JSON POST. libcurl rather than IXWebSocket's HTTP client, which
/// the product endpoint answers with an HTML 500.
/// Aborts (status 0) once `running` turns false; polled about once a second.
HttpResponse postJson(const std::string &url, const std::string &body,
                      const std::atomic<bool> *running = nullptr);

/// Call once per process; curl_easy_init does it implicitly but not safely.
void globalInit();
void globalCleanup();

} // namespace http
} // namespace cardscanner
