#import <Foundation/Foundation.h>

#define ISCSettingsService @"org.istatserver.control.settings"
#define ISCSettingsPlist @"org.istatserver.control.settings.plist"

@protocol ISCSettingsProtocol
- (void)readInstallation:(NSString *)installation authorization:(NSData *)authorization
                  reveal:(BOOL)reveal reply:(void (^)(NSData *))reply;
@end

// Both ends require the same Apple-issued team plus the exact peer identifier.
NSString *ISCSettingsPeerRequirement(NSString *identifier);
