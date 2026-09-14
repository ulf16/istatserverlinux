#import "ISCSettingsClient.h"
#import "ISCSettingsProtocol.h"
#import <Security/Authorization.h>
#include <arpa/inet.h>

NSDictionary *ISCValidateSettings(NSData *data, BOOL reveal) {
    if (data.length > 32768) return nil;
    id value = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if (![value isKindOfClass:NSDictionary.class] || ![value[@"schema"] isEqual:@1] ||
        CFGetTypeID((__bridge CFTypeRef)value[@"schema"]) == CFBooleanGetTypeID() || ![value[@"state"] isEqual:@"readable"]) return nil;
    NSDictionary *r = value;
    if (![r[@"scope"] isEqual:@"configuration-file"] || ![r[@"bonjour"] isEqual:@"not-observed"] ||
        ![@[@"passcode", @"password"] containsObject:r[@"auth_mode"]] ||
        ![@[@"configured", @"server-default"] containsObject:r[@"credential_source"]] ||
        ![r[@"port"] isKindOfClass:NSNumber.class] || [r[@"port"] doubleValue] != [r[@"port"] integerValue] ||
        CFGetTypeID((__bridge CFTypeRef)r[@"port"]) == CFBooleanGetTypeID() ||
        [r[@"port"] integerValue] < 1 || [r[@"port"] integerValue] > 65535 ||
        ![r[@"address"] isKindOfClass:NSString.class] || [r[@"address"] length] > 64) return nil;
    unsigned char address[16];
    const char *literal = [r[@"address"] UTF8String];
    if (inet_pton(AF_INET, literal, address) != 1 && inet_pton(AF_INET6, literal, address) != 1) return nil;
    NSMutableDictionary *clean = [[r dictionaryWithValuesForKeys:@[@"port", @"address", @"auth_mode", @"credential_source"]] mutableCopy];
    if (reveal) {
        if (![r[@"credential"] isKindOfClass:NSString.class] || ![r[@"credential"] length] || [r[@"credential"] length] > 4096) return nil;
        clean[@"credential"] = r[@"credential"];
    }
    return clean;
}

@interface ISCSettingsClient ()
@property NSXPCConnection *connection;
@property AuthorizationRef authorization;
@property(copy) void (^completion)(NSDictionary *, NSString *);
@property BOOL finished;
@end
@implementation ISCSettingsClient
- (void)finish:(NSDictionary *)result error:(NSString *)error {
    if (self.finished) return;
    self.finished = YES;
    [self.connection invalidate]; self.connection = nil;
    if (self.authorization) { AuthorizationFree(self.authorization, kAuthorizationFlagDestroyRights); self.authorization = NULL; }
    void (^completion)(NSDictionary *, NSString *) = self.completion;
    self.completion = nil;
    if (completion) completion(result, error);
}
- (void)cancel { [self finish:nil error:nil]; }
- (void)readInstallation:(NSString *)installation reveal:(BOOL)reveal completion:(void (^)(NSDictionary *, NSString *))completion {
    self.completion = completion;
    if (@available(macOS 13.0, *)) {
        NSString *requirement = ISCSettingsPeerRequirement(ISCSettingsService);
        if (!requirement || ![@[@"opt", @"local"] containsObject:installation]) {
            [self finish:nil error:@"Protected access requires a signed build and a supported local installation."]; return;
        }
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            AuthorizationRef authorization = NULL;
            OSStatus status = AuthorizationCreate(NULL, kAuthorizationEmptyEnvironment, kAuthorizationFlagDefaults, &authorization);
            AuthorizationItem item = {"system.privilege.admin", 0, NULL, 0};
            AuthorizationRights rights = {1, &item};
            const char *prompt = reveal ? "Read the iStat server pairing credential." : "Read protected iStat server settings (no changes).";
            AuthorizationItem environmentItem = {kAuthorizationEnvironmentPrompt, strlen(prompt), (void *)prompt, 0};
            AuthorizationEnvironment environment = {1, &environmentItem};
            if (status == errAuthorizationSuccess)
                status = AuthorizationCopyRights(authorization, &rights, &environment,
                    kAuthorizationFlagInteractionAllowed | kAuthorizationFlagExtendRights, NULL);
            dispatch_async(dispatch_get_main_queue(), ^{
                if (self.finished || status != errAuthorizationSuccess) {
                    if (authorization) AuthorizationFree(authorization, kAuthorizationFlagDestroyRights);
                    [self finish:nil error:status == errAuthorizationCanceled ? nil : @"Authorization was not granted."];
                    return;
                }
                self.authorization = authorization;
                AuthorizationExternalForm form;
                if (AuthorizationMakeExternalForm(authorization, &form) != errAuthorizationSuccess) {
                    [self finish:nil error:@"Authorization could not be passed to the helper."]; return;
                }
                NSData *token = [NSData dataWithBytes:&form length:sizeof(form)];
                memset(&form, 0, sizeof(form));
                self.connection = [[NSXPCConnection alloc] initWithMachServiceName:ISCSettingsService options:NSXPCConnectionPrivileged];
                if (!ISCApplySettingsRequirement(self.connection, requirement)) {
                    [self finish:nil error:@"The settings helper's signature check could not be configured. Access was not attempted."]; return;
                }
                self.connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ISCSettingsProtocol)];
                __weak ISCSettingsClient *weak = self;
                self.connection.interruptionHandler = ^{ dispatch_async(dispatch_get_main_queue(), ^{ [weak finish:nil error:@"The settings helper disconnected. No changes were made."]; }); };
                self.connection.invalidationHandler = ^{ dispatch_async(dispatch_get_main_queue(), ^{ [weak finish:nil error:@"The signed settings helper is unavailable. Check its approval in System Settings."]; }); };
                [self.connection resume];
                id<ISCSettingsProtocol> proxy = [self.connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
                    dispatch_async(dispatch_get_main_queue(), ^{ [weak finish:nil error:@"The settings helper could not be reached or verified."]; });
                }];
                [proxy readInstallation:installation authorization:token reveal:reveal reply:^(NSData *data) {
                    NSDictionary *result = ISCValidateSettings(data, reveal);
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [weak finish:result error:result ? nil : @"Protected settings could not be read. Configuration must belong to root or the istat service account beneath root-owned ancestors, with no symlinks or group/world write access. Missing or invalid settings are not disclosed."];
                    });
                }];
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                    [weak finish:nil error:@"The settings request timed out. No changes were made."];
                });
            });
        });
    } else [self finish:nil error:@"Protected settings access requires macOS 13 or later."];
}
@end
