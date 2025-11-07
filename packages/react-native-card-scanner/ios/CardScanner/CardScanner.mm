#import "CardScanner.h"

#import "../common/rncardscanner/RnCardScannerInstaller.h"
#import <React/RCTBridge+Private.h>
#import <React/RCTCallInvoker.h>
#import <ReactCommon/RCTTurboModule.h>
#include <stdexcept>

using namespace facebook::react;

@interface RCTBridge (JSIRuntime)
- (void *)runtime;
@end

@implementation CardScannerInstaller

@synthesize callInvoker = _callInvoker;

RCT_EXPORT_MODULE(CardScannerInstaller)

RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install) {
  auto jsiRuntime =
      reinterpret_cast<facebook::jsi::Runtime *>(self.bridge.runtime);
  auto jsCallInvoker = _callInvoker.callInvoker;

  assert(jsiRuntime != nullptr);

  rncardscanner::CardScannerInstaller::injectJSIBindings(jsiRuntime,
                                                         jsCallInvoker);

  NSLog(@"Successfully installed JSI bindings for react-native-card-scanner!");
  return @true;
}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  return std::make_shared<facebook::react::NativeCardScannerSpecJSI>(params);
}

@end
