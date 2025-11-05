#include "CardScannerInstallerModule.h"
#include "rncardscanner/RnCardScannerInstaller.h"

#include <jni.h>
#include <jsi/jsi.h>

namespace rncardscanner {

using namespace facebook::jni;

CardScannerInstallerModule::CardScannerInstallerModule(
    jni::alias_ref<CardScannerInstallerModule::jhybridobject> &jThis,
    jsi::Runtime *jsiRuntime,
    const std::shared_ptr<facebook::react::CallInvoker> &jsCallInvoker)
    : javaPart_(make_global(jThis)), jsiRuntime_(jsiRuntime),
      jsCallInvoker_(jsCallInvoker) {}

jni::local_ref<CardScannerInstallerModule::jhybriddata>
CardScannerInstallerModule::initHybrid(
    jni::alias_ref<jhybridobject> jThis, jlong jsContext,
    jni::alias_ref<facebook::react::CallInvokerHolder::javaobject>
        jsCallInvokerHolder) {
  auto jsCallInvoker = jsCallInvokerHolder->cthis()->getCallInvoker();
  auto rnRuntime = reinterpret_cast<jsi::Runtime *>(jsContext);
  return makeCxxInstance(jThis, rnRuntime, jsCallInvoker);
}

void CardScannerInstallerModule::registerNatives() {
  registerHybrid({
      makeNativeMethod("initHybrid", CardScannerInstallerModule::initHybrid),
      makeNativeMethod("injectJSIBindings",
                       CardScannerInstallerModule::injectJSIBindings),
  });
}

void CardScannerInstallerModule::injectJSIBindings() {
  CardScannerInstaller::injectJSIBindings(jsiRuntime_, jsCallInvoker_);
}

} // namespace rncardscanner

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  return facebook::jni::initialize(
      vm, [] { rncardscanner::CardScannerInstallerModule::registerNatives(); });
}
