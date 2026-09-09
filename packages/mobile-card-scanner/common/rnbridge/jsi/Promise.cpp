#include "Promise.h"

namespace cardscanner {

void Promise::resolve(jsi::Runtime &runtime, jsi::Value &&result) {
  auto resolver = _resolver.lock();
  if (!resolver) {
    return; // Runtime torn down, or already settled.
  }
  resolver->callback().call(runtime, result);
  release();
}

void Promise::reject(jsi::Runtime &runtime, std::string message) {
  auto rejecter = _rejecter.lock();
  if (!rejecter) {
    return;
  }
  jsi::JSError error(runtime, std::move(message));
  rejecter->callback().call(runtime, error.value());
  release();
}

void Promise::release() {
  if (auto resolver = _resolver.lock()) {
    resolver->destroy();
  }
  if (auto rejecter = _rejecter.lock()) {
    rejecter->destroy();
  }
}

} // namespace cardscanner
