#import <Foundation/Foundation.h>
#import <Security/Authorization.h>
#import "ISCSettingsProtocol.h"
#include <cassert>
int main(void) { @autoreleasepool {
    id<ISCSettingsProtocol> session = [NSClassFromString(@"ISCSettingsSession") new];
    assert(session);
    __block unsigned replies = 0;
    void (^check)(NSData *) = ^(NSData *data) {
        NSDictionary *value = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        assert([value[@"state"] isEqual:@"authorization-required"]);
        assert(!value[@"credential"]);
        ++replies;
    };
    [session readInstallation:@"/etc/passwd" authorization:[NSData data] reveal:YES reply:check];
    [session readInstallation:@"opt" authorization:[NSData data] reveal:YES reply:check];
    [session readInstallation:@"local" authorization:[NSMutableData dataWithLength:sizeof(AuthorizationExternalForm)] reveal:YES reply:check];
    AuthorizationRef auth = NULL;
    AuthorizationExternalForm form;
    OSStatus created = AuthorizationCreate(NULL, kAuthorizationEmptyEnvironment, kAuthorizationFlagDefaults, &auth);
    if (created != errAuthorizationSuccess) { fprintf(stderr, "Empty authorization session failed: %d\n", (int)created); return 1; }
    assert(AuthorizationMakeExternalForm(auth, &form) == errAuthorizationSuccess);
    [session readInstallation:@"opt" authorization:[NSData dataWithBytes:&form length:sizeof(form)] reveal:YES reply:check];
    AuthorizationFree(auth, kAuthorizationFlagDestroyRights);
    assert(replies == 4);
    id<NSXPCListenerDelegate> listener = [NSClassFromString(@"ISCSettingsListener") new];
    NSXPCListener *anonymous = [NSXPCListener anonymousListener];
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithListenerEndpoint:anonymous.endpoint];
    assert(![listener listener:anonymous shouldAcceptNewConnection:connection]);
    puts("Unsigned peer and missing/forged authorization rejected without settings access");
} }
