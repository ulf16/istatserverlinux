#import <Foundation/Foundation.h>

// Main-thread presentation state. No authorization rights or report data live here.
@interface ISCProtectedSettingsState : NSObject
@property(readonly) NSDictionary *metadata;
- (void)beginRequest;
- (void)receiveResult:(NSDictionary *)result action:(NSInteger)action atTime:(NSTimeInterval)time;
- (BOOL)hasPendingCredentialAtTime:(NSTimeInterval)time;
- (NSDictionary *)takePendingCredentialAtTime:(NSTimeInterval)time;
- (void)clear;
@end
