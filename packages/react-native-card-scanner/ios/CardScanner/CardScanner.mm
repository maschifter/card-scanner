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
  NSFileManager *fileManager = [NSFileManager defaultManager];

  // Get the library directory for database (persistent storage)
  NSArray *libraryPaths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,
                                                               NSUserDomainMask, YES);
  NSString *libraryDirectory = [libraryPaths objectAtIndex:0];

  // Create a "database" subdirectory
  NSString *databaseDirectory =
      [libraryDirectory stringByAppendingPathComponent:@"database"];

  // Create the database directory if it doesn't exist
  if (![fileManager fileExistsAtPath:databaseDirectory]) {
    [fileManager createDirectoryAtPath:databaseDirectory
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];
  }
  NSLog(@"Native module database directory: %@", databaseDirectory);

  std::string dbPath = std::string([databaseDirectory UTF8String]);
  pathprovider::set_db_path(dbPath);

  // Get the cache directory for temporary images
  NSArray *cachePaths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory,
                                                             NSUserDomainMask, YES);
  NSString *cacheDirectory = [cachePaths objectAtIndex:0];

  // Create a "card-images" subdirectory in cache
  NSString *imageCacheDirectory =
      [cacheDirectory stringByAppendingPathComponent:@"card-images"];

  // Create the cache directory if it doesn't exist
  if (![fileManager fileExistsAtPath:imageCacheDirectory]) {
    [fileManager createDirectoryAtPath:imageCacheDirectory
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];
  }
  NSLog(@"Native module cache directory: %@", imageCacheDirectory);

  std::string cachePath = std::string([imageCacheDirectory UTF8String]);
  pathprovider::set_cache_path(cachePath);

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
