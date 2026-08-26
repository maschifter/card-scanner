#include "CardScannerInstallerModule.h"
#include "CardScannerInstaller.h"
#include "PathProvider.h" // Include PathProvider.h

#include "CardScannerOnLoad.hpp"
#include <jni.h>
#include <jsi/jsi.h>

namespace cardscanner {

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

void CardScannerInstallerModule::setDbPath(jstring path) {
  JNIEnv* env = jni::Environment::current();
  const char *pathChars = env->GetStringUTFChars(path, nullptr);
  std::string dbPath = pathChars;
  env->ReleaseStringUTFChars(path, pathChars);
  pathprovider::set_db_path(dbPath);
}

void CardScannerInstallerModule::setCachePath(jstring path) {
  JNIEnv* env = jni::Environment::current();
  const char *pathChars = env->GetStringUTFChars(path, nullptr);
  std::string cachePath = pathChars;
  env->ReleaseStringUTFChars(path, pathChars);
  pathprovider::set_cache_path(cachePath);
}

void CardScannerInstallerModule::registerNatives() {
  registerHybrid({
      makeNativeMethod("initHybrid", CardScannerInstallerModule::initHybrid),
      makeNativeMethod("injectJSIBindings",
                       CardScannerInstallerModule::injectJSIBindings),
      makeNativeMethod("setDbPath", CardScannerInstallerModule::setDbPath),
      makeNativeMethod("setCachePath", CardScannerInstallerModule::setCachePath),
  });
}

void CardScannerInstallerModule::injectJSIBindings() {
  CardScannerInstaller::injectJSIBindings(jsiRuntime_, jsCallInvoker_);
}

} // namespace cardscanner

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  return facebook::jni::initialize(vm, [] {
    cardscanner::CardScannerInstallerModule::registerNatives();
    margelo::nitro::cardscanner::registerAllNatives();
  });
}
