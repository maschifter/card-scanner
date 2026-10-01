#include "HttpClient.h"

#include <curl/curl.h>

namespace cardscanner {
namespace net {

namespace {

size_t appendBody(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

int abortWhenStopped(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<const std::atomic<bool> *>(userdata)->load() ? 0 : 1;
}

} // namespace

void globalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void globalCleanup() { curl_global_cleanup(); }

HttpResponse postJson(const std::string &url, const std::string &body,
                      const std::atomic<bool> *running) {
  HttpResponse result;
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return result;
  }

  curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  // Runs on a worker thread, so keep curl off signals.
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if (running != nullptr) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortWhenStopped);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,
                     const_cast<std::atomic<bool> *>(running));
  }

  if (curl_easy_perform(curl) == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return result;
}

} // namespace net
} // namespace cardscanner
