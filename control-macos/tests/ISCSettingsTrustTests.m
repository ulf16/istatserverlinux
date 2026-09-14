#import "ISCSettingsProtocol.h"
#import <Security/Security.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d\n", __LINE__); abort(); } } while (0)

@protocol ISCTrustProbe
- (void)ping:(void (^)(NSString *))reply;
@end
@interface ISCTrustListener : NSObject <NSXPCListenerDelegate, ISCTrustProbe>
@property NSString *requirement;
@property NSMutableArray<NSXPCConnection *> *connections;
@end
@implementation ISCTrustListener
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    if (!ISCApplySettingsRequirement(connection, self.requirement)) return NO;
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ISCTrustProbe)];
    connection.exportedObject = self;
    @synchronized (self) { [self.connections addObject:connection]; }
    [connection resume];
    return YES;
}
- (void)ping:(void (^)(NSString *))reply { reply(@"verified"); }
@end

static void exchange(NSString *outgoing, NSString *incoming, BOOL expected) {
    ISCTrustListener *delegate = [ISCTrustListener new];
    delegate.requirement = incoming;
    delegate.connections = [NSMutableArray new];
    NSXPCListener *listener = NSXPCListener.anonymousListener;
    listener.delegate = delegate;
    [listener resume];
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithListenerEndpoint:listener.endpoint];
    CHECK(ISCApplySettingsRequirement(connection, outgoing));
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ISCTrustProbe)];
    [connection resume];
    __block BOOL done = NO, success = NO;
    id<ISCTrustProbe> proxy = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{ done = YES; });
    }];
    [proxy ping:^(NSString *value) { dispatch_async(dispatch_get_main_queue(), ^{ success = [value isEqual:@"verified"]; done = YES; }); }];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (!done && deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    CHECK(done && success == expected);
    [connection invalidate]; [listener invalidate];
    @synchronized (delegate) { for (NSXPCConnection *peer in delegate.connections) [peer invalidate]; }
}

int main(int argc, const char **argv) { @autoreleasepool {
    for (NSString *identifier in @[@"org.istatserver.control", ISCSettingsService]) {
        NSString *r = ISCSettingsRequirementForTeam(identifier, @"ABCDEFGHIJ");
        CHECK(r.length);
        NSXPCConnection *c = [[NSXPCConnection alloc] initWithMachServiceName:ISCSettingsService options:NSXPCConnectionPrivileged];
        CHECK(ISCApplySettingsRequirement(c, r));
        [c invalidate];
    }
    CHECK(!ISCSettingsRequirementForTeam(@"unknown", @"ABCDEFGHIJ"));
    CHECK(!ISCSettingsRequirementForTeam(ISCSettingsService, @"invalid\"team"));
    NSXPCConnection *bad = [[NSXPCConnection alloc] initWithMachServiceName:ISCSettingsService options:0];
    CHECK(!ISCApplySettingsRequirement(bad, @"not a valid requirement"));
    if (argc > 1) {
        NSString *identifier = [NSString stringWithUTF8String:argv[1]];
        NSString *own = ISCSettingsPeerRequirement(identifier);
        CHECK(own.length);
        exchange(own, own, YES);
        NSString *other = ISCSettingsPeerRequirement([identifier isEqual:ISCSettingsService] ? @"org.istatserver.control" : ISCSettingsService);
        exchange(other, own, NO);
        exchange(own, other, NO);
        exchange(ISCSettingsRequirementForTeam(identifier, @"ABCDEFGHIJ"), own, NO);
        puts("Signed XPC: bidirectional exchange and wrong peer/team rejection passed");
    }
    puts("Settings requirement compilation, connection setup and fail-closed checks passed");
} }
