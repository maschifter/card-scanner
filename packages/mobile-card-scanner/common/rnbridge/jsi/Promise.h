#pragma once

#include <memory>
#include <string>

#include <ReactCommon/CallInvoker.h>
#include <jsi/jsi.h>
#include <react/bridging/CallbackWrapper.h>

namespace cardscanner {

// Aliases, not `using namespace facebook` - a using-directive at namespace
// scope in a header leaks onto every includer. Same bug class as the
// ExecuTorch leak removed from the model headers.
namespace jsi = facebook::jsi;
namespace react = facebook::react;

class Promise;

template <typename T>
concept PromiseRunFn =
    std::invocable<T, std::shared_ptr<Promise>> &&
    std::same_as<std::invoke_result_t<T, std::shared_ptr<Promise>>, void>;

/**
 * Settles a JS Promise from native. Holds the resolver/rejecter only as weak
 * CallbackWrapper handles registered in the runtime's LongLivedObjectCollection,
 * which React Native clears on the JS thread at instance teardown - so worker
 * threads never own jsi state and a settle after teardown is a no-op.
 * resolve()/reject() must be called on the JS thread (via the CallInvoker).
 */
class Promise {
public:
  Promise(jsi::Runtime &runtime,
          std::shared_ptr<react::CallInvoker> callInvoker,
          jsi::Function resolver, jsi::Function rejecter)
      : callInvoker(std::move(callInvoker)),
        _resolver(react::CallbackWrapper::createWeak(std::move(resolver),
                                                     runtime, this->callInvoker)),
        _rejecter(react::CallbackWrapper::createWeak(std::move(rejecter),
                                                     runtime, this->callInvoker)) {}

  Promise(const Promise &) = delete;
  Promise &operator=(const Promise &) = delete;

  void resolve(jsi::Runtime &runtime, jsi::Value &&result);
  void reject(jsi::Runtime &runtime, std::string error);

  std::shared_ptr<react::CallInvoker> getCallInvoker() { return callInvoker; }

  /**
    Creates a new promise and runs the supplied "run" function that takes this
    promise. We use a template for the function type to not use std::function
    and be able to bind a lambda.
  */
  template <PromiseRunFn Fn>
  static jsi::Value
  createPromise(jsi::Runtime &runtime,
                std::shared_ptr<react::CallInvoker> callInvoker, Fn &&run) {
    // Get Promise ctor from global
    auto promiseCtor =
        runtime.global().getPropertyAsFunction(runtime, "Promise");

    auto promiseCallback = jsi::Function::createFromHostFunction(
        runtime, jsi::PropNameID::forUtf8(runtime, "PromiseCallback"), 2,
        [run = std::move(run),
         callInvoker](jsi::Runtime &runtime, const jsi::Value &thisValue,
                      const jsi::Value *arguments, size_t count) -> jsi::Value {
          auto promise = std::make_shared<Promise>(
              runtime, callInvoker,
              arguments[0].asObject(runtime).asFunction(runtime),
              arguments[1].asObject(runtime).asFunction(runtime));
          run(promise);

          return jsi::Value::undefined();
        });

    return promiseCtor.callAsConstructor(runtime, promiseCallback);
  }

private:
  void release();

  std::shared_ptr<react::CallInvoker> callInvoker;
  std::weak_ptr<react::CallbackWrapper> _resolver;
  std::weak_ptr<react::CallbackWrapper> _rejecter;
};

} // namespace cardscanner
