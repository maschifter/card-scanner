#import "CardScanner.h"

#import "../common/rnbridge/CardScannerInstaller.h"
#import "PathProvider.h"
#import <ReactCommon/RCTTurboModule.h>
#import <ReactCommon/RCTTurboModuleWithJSIBindings.h>

using namespace facebook::react;

@interface CardScannerInstaller () <RCTTurboModuleWithJSIBindings>
@end

@implementation CardScannerInstaller

RCT_EXPORT_MODULE(CardScannerInstaller)

// React Native installs the bindings via installJSIBindingsWithRuntime:
// when it creates this TurboModule, before this method can run. install()
// exists only to trigger that creation from JS; acquiring the runtime
// synchronously here would deadlock the JS thread.
RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install) {
  return @true;
}

- (void)installJSIBindingsWithRuntime:(facebook::jsi::Runtime &)runtime
                          callInvoker:(const std::shared_ptr<CallInvoker> &)callInvoker {
  NSFileManager *fileManager = [NSFileManager defaultManager];

  // Library directory: persistent storage for the databases.
  NSArray *libraryPaths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,
                                                              NSUserDomainMask, YES);
  NSString *databaseDirectory =
      [[libraryPaths objectAtIndex:0] stringByAppendingPathComponent:@"database"];
  if (![fileManager fileExistsAtPath:databaseDirectory]) {
    [fileManager createDirectoryAtPath:databaseDirectory
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];
  }
  NSLog(@"Native module database directory: %@", databaseDirectory);
  pathprovider::set_db_path(std::string([databaseDirectory UTF8String]));

  // Caches directory: temporary storage for captured card images.
  NSArray *cachePaths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory,
                                                            NSUserDomainMask, YES);
  NSString *imageCacheDirectory =
      [[cachePaths objectAtIndex:0] stringByAppendingPathComponent:@"card-images"];
  if (![fileManager fileExistsAtPath:imageCacheDirectory]) {
    [fileManager createDirectoryAtPath:imageCacheDirectory
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];
  }
  NSLog(@"Native module cache directory: %@", imageCacheDirectory);
  pathprovider::set_cache_path(std::string([imageCacheDirectory UTF8String]));

  // Paths must be set first: injectJSIBindings constructs the
  // DatabaseManager singleton, which reads the db path once at construction.
  cardscanner::CardScannerInstaller::injectJSIBindings(&runtime, callInvoker);

  NSLog(@"Successfully installed JSI bindings for @cardnexus/card-scanner!");
}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  return std::make_shared<facebook::react::NativeCardScannerSpecJSI>(params);
}

@end
