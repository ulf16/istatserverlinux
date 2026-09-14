#import "ISCProtectedSettingsState.h"

@interface ISCProtectedSettingsState ()
@property(readwrite) NSDictionary *metadata;
@property NSDictionary *pending;
@property NSTimeInterval deadline;
@end

@implementation ISCProtectedSettingsState
- (void)beginRequest { self.pending = nil; }
- (void)receiveResult:(NSDictionary *)result action:(NSInteger)action atTime:(NSTimeInterval)time {
    self.pending = nil;
    if (!result) return;
    self.metadata = [result dictionaryWithValuesForKeys:@[@"port", @"address", @"auth_mode", @"credential_source"]];
    NSString *credential = result[@"credential"];
    if ((action == 1 || action == 2) && [credential isKindOfClass:NSString.class] && credential.length) {
        self.pending = @{@"credential":credential, @"action":@(action)};
        self.deadline = time + 30;
    }
}
- (BOOL)hasPendingCredentialAtTime:(NSTimeInterval)time {
    if (time >= self.deadline) self.pending = nil;
    return self.pending != nil;
}
- (NSDictionary *)takePendingCredentialAtTime:(NSTimeInterval)time {
    if (![self hasPendingCredentialAtTime:time]) return nil;
    NSDictionary *result = self.pending;
    self.pending = nil;
    return result;
}
- (void)clear { self.pending = nil; self.metadata = nil; }
@end
