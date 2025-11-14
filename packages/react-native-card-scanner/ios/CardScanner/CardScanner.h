#import <CardScannerSpec/CardScannerSpec.h>
#import <React/RCTCallInvokerModule.h>
#import <React/RCTEventEmitter.h>

@interface CardScannerInstaller
    : RCTEventEmitter <NativeCardScannerSpec, RCTCallInvokerModule>

@end
