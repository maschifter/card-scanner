#pragma once

#include <ReactCommon/CallInvokerHolder.h>
#include <fbjni/fbjni.h>

#include <react/jni/CxxModuleWrapper.h>
#include <react/jni/JMessageQueueThread.h>

#include <memory>
#include <utility>

namespace rncardscanner {

using namespace facebook;
using namespace react;

class CardScannerInstallerModule : public jni::HybridClass<CardScannerInstallerModule> {
public:
  static auto constexpr kJavaDescriptor = "Lcom/cardnexus/cardscanner/CardScannerInstaller;";

  static jni::local_ref<CardScannerInstallerModule::jhybriddata>
  initHybrid(jni::alias_ref<jhybridobject> jThis, jlong jsContext,
             jni::alias_ref<facebook::react::CallInvokerHolder::javaobject>
                 jsCallInvokerHolder);

  static void registerNatives();

  void injectJSIBindings();
  void setDbPath(jstring path);
  void setCachePath(jstring path);

private:
  friend HybridBase;

  jni::global_ref<CardScannerInstallerModule::javaobject> javaPart_;
  jsi::Runtime *jsiRuntime_;
  std::shared_ptr<facebook::react::CallInvoker> jsCallInvoker_;

  explicit CardScannerInstallerModule(
      jni::alias_ref<CardScannerInstallerModule::jhybridobject> &jThis,
      jsi::Runtime *jsiRuntime,
      const std::shared_ptr<facebook::react::CallInvoker> &jsCallInvoker);
};

} // namespace rncardscanner
