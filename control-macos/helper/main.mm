#import <Foundation/Foundation.h>
#import <Security/Authorization.h>
#import "ISCSettingsProtocol.h"
#include "Settings.hpp"
#include <unistd.h>

@interface ISCSettingsSession : NSObject <ISCSettingsProtocol>
@end
@implementation ISCSettingsSession
- (void)readInstallation:(NSString *)installation authorization:(NSData *)authorization reveal:(BOOL)reveal reply:(void (^)(NSData *))reply {
    std::string output = istat_settings::failure("authorization-required");
    if (([installation isEqual:@"opt"] || [installation isEqual:@"local"]) && authorization.length == sizeof(AuthorizationExternalForm)) {
        AuthorizationExternalForm form;
        [authorization getBytes:&form length:sizeof(form)];
        AuthorizationRef auth = NULL;
        if (AuthorizationCreateFromExternalForm(&form, &auth) == errAuthorizationSuccess) {
            AuthorizationItem item = {"system.privilege.admin", 0, NULL, 0};
            AuthorizationRights rights = {1, &item};
            // Never display UI or grant rights here; the GUI must already have
            // obtained an OS authorization following an explicit user action.
            if (AuthorizationCopyRights(auth, &rights, kAuthorizationEmptyEnvironment, kAuthorizationFlagDefaults, NULL) == errAuthorizationSuccess)
                output = istat_settings::read(installation.UTF8String, reveal);
            AuthorizationFree(auth, kAuthorizationFlagDefaults);
        }
        memset(&form, 0, sizeof(form));
    }
    reply([NSData dataWithBytes:output.data() length:output.size()]);
}
@end

@interface ISCSettingsListener : NSObject <NSXPCListenerDelegate>
@end
@implementation ISCSettingsListener
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    NSString *requirement = ISCSettingsPeerRequirement(@"org.istatserver.control");
    if (geteuid() != 0 || !requirement) return NO;
    if (!ISCApplySettingsRequirement(connection, requirement)) return NO;
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ISCSettingsProtocol)];
    connection.exportedObject = [ISCSettingsSession new];
    [connection resume];
    return YES;
}
@end

#ifndef ISC_HELPER_TESTING
int main(void) {
    @autoreleasepool {
        if (geteuid() != 0 || !ISCSettingsPeerRequirement(@"org.istatserver.control")) return 1;
        ISCSettingsListener *delegate = [ISCSettingsListener new];
        NSXPCListener *listener = [[NSXPCListener alloc] initWithMachServiceName:ISCSettingsService];
        listener.delegate = delegate;
        [listener resume];
        [NSRunLoop.currentRunLoop run];
    }
    return 0;
}
#endif
