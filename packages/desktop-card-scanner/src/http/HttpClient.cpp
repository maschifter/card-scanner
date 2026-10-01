#include "HttpClient.h"

#include <curl/curl.h>

#include <memory>

namespace cardscanner {
namespace http {

namespace {

size_t appendBody(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

int abortWhenStopped(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<const std::atomic<bool> *>(userdata)->load() ? 0 : 1;
}

struct EasyCleanup {
  void operator()(CURL *curl) const { curl_easy_cleanup(curl); }
};
struct SlistFree {
  void operator()(curl_slist *list) const { curl_slist_free_all(list); }
};

/// Appends a header, keeping the list owned even when curl returns null.
void appendHeader(std::unique_ptr<curl_slist, SlistFree> &list,
                  const char *header) {
  if (curl_slist *grown = curl_slist_append(list.get(), header)) {
    list.release();
    list.reset(grown);
  }
}

} // namespace

void globalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void globalCleanup() { curl_global_cleanup(); }

HttpResponse postJson(const std::string &url, const std::string &body,
                      const std::atomic<bool> *running) {
  HttpResponse result;
  const std::unique_ptr<CURL, EasyCleanup> handle(curl_easy_init());
  if (!handle) {
    return result;
  }
  CURL *curl = handle.get();

  std::unique_ptr<curl_slist, SlistFree> headers;
  appendHeader(headers, "Content-Type: application/json");
  appendHeader(headers, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  // No FOLLOWLOCATION: curl follows a 301/302/303 by re-issuing the POST as
  // a bodyless GET. The API should never redirect; surface the 3xx instead.
  // Runs on a worker thread, so keep curl off signals.
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if (running != nullptr) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortWhenStopped);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,
                     const_cast<std::atomic<bool> *>(running));
  }

  const CURLcode rc = curl_easy_perform(curl);
  if (rc == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
  } else {
    result.error = curl_easy_strerror(rc);
  }
  return result;
}

} // namespace http
} // namespace cardscanner
