#import <Foundation/Foundation.h>

// A short-lived request, not a cached authorization/session or credential store.
@interface ISCSettingsClient : NSObject
- (void)readInstallation:(NSString *)installation reveal:(BOOL)reveal completion:(void (^)(NSDictionary *result, NSString *error))completion;
- (void)cancel;
@end
NSDictionary *ISCValidateSettings(NSData *data, BOOL reveal);
