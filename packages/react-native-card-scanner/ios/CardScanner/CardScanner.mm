#import "CardScanner.h"

#import "../common/rncardscanner/RnCardScannerInstaller.h"
#import "PathProvider.h"
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
  // Get the library directory
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,
                                                       NSUserDomainMask, YES);
  NSString *libraryDirectory = [paths objectAtIndex:0];

  // Create a "database" subdirectory
  NSString *databaseDirectory =
      [libraryDirectory stringByAppendingPathComponent:@"database"];

  // Create the directory if it doesn't exist
  NSFileManager *fileManager = [NSFileManager defaultManager];
  if (![fileManager fileExistsAtPath:databaseDirectory]) {
    [fileManager createDirectoryAtPath:databaseDirectory
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];
  }
  NSLog(@"Native module database directory: %@",
        databaseDirectory); // Added print statement

  std::string documentsPath = std::string([databaseDirectory UTF8String]);
  pathprovider::set_db_path(documentsPath);

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
