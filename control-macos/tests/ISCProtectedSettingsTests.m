#define ISC_APP_TESTING
#import "../src/main.m"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d\n", __LINE__); abort(); } } while (0)

@interface ISCStubClient : ISCSettingsClient
@property(copy) void (^reply)(NSDictionary *, NSString *);
@property NSInteger reads;
@end
@implementation ISCStubClient
- (void)readInstallation:(NSString *)installation reveal:(BOOL)reveal completion:(void (^)(NSDictionary *, NSString *))completion {
    self.reply = completion; self.reads++;
}
- (void)complete:(NSDictionary *)result {
    void (^reply)(NSDictionary *, NSString *) = self.reply;
    self.reply = nil;
    if (reply) reply(result, nil);
}
- (void)cancel { [self complete:nil]; }
@end

// Headless stand-ins: exercise the real delegate callbacks without displaying
// a window, prompting for authorization, or accessing actual credentials.
@interface ISCStubView : NSObject
@property(getter=isVisible) BOOL visible;
@property NSInteger selectedSegment;
@end
@implementation ISCStubView
@end

@interface ISCFlowApp : ISCApp
@property ISCStubClient *stub;
@property BOOL foreground;
@property NSString *shown;
@property NSInteger reveals;
@end
@implementation ISCFlowApp
- (BOOL)canReadProtected { return self.settingsClient == nil; }
- (ISCSettingsClient *)makeSettingsClient { return self.stub; }
- (BOOL)protectedWindowIsForeground { return self.foreground; }
- (void)refreshHelperApproval {}
- (void)render {}
- (void)failure:(NSString *)message { CHECK(NO); }
- (void)showCredential:(NSString *)credential { self.shown = credential; self.reveals++; }
- (void)hideCredential { self.shown = nil; }
@end

int main(void) { @autoreleasepool {
    NSDictionary *fixture = @{@"port":@5109, @"address":@"::1", @"auth_mode":@"passcode", @"credential_source":@"configured", @"credential":@"12345"};
    ISCProtectedSettingsState *state = [ISCProtectedSettingsState new];
    [state receiveResult:fixture action:0 atTime:100];
    CHECK(!state.metadata[@"credential"] && ![state hasPendingCredentialAtTime:100]);
    [state receiveResult:fixture action:1 atTime:100];
    CHECK([state hasPendingCredentialAtTime:129]);
    CHECK(![state takePendingCredentialAtTime:130]);
    CHECK(state.metadata[@"port"]);
    [state receiveResult:fixture action:2 atTime:200];
    CHECK([[state takePendingCredentialAtTime:201][@"action"] isEqual:@2]);
    CHECK(![state takePendingCredentialAtTime:201]);
    [state receiveResult:fixture action:1 atTime:300];
    [state beginRequest];
    CHECK(![state hasPendingCredentialAtTime:300] && state.metadata);
    [state receiveResult:nil action:1 atTime:301];
    CHECK(state.metadata);
    [state clear]; CHECK(!state.metadata);

    ISCFlowApp *app = [ISCFlowApp new];
    NSNotification *activated = [NSNotification notificationWithName:NSApplicationDidBecomeActiveNotification object:nil];
    app.stub = [ISCStubClient new];
    ISCStubView *window = [ISCStubView new]; window.visible = YES;
    ISCStubView *edition = [ISCStubView new];
    ISCStubView *tabs = [ISCStubView new]; tabs.selectedSegment = 1;
    app.window = (NSWindow *)window;
    app.edition = (NSSegmentedControl *)edition;
    app.tabs = (NSSegmentedControl *)tabs;
    app.foreground = YES;
    app.prefix = @"/opt/istatserverlinux";
    [app requestProtected:0]; [app.stub complete:fixture];
    CHECK(app.protectedState.metadata && !app.shown);

    [app requestProtected:1];
    app.foreground = NO; [app lockProtected:nil];
    [app.stub complete:fixture];
    CHECK(app.protectedState.metadata && !app.protectedState.metadata[@"credential"]);
    CHECK(!app.shown && app.credentialDeliveryTimer && app.stub.reads == 2);
    app.foreground = YES; [app applicationDidBecomeActive:activated];
    CHECK([app.shown isEqual:@"12345"] && app.reveals == 1 && !app.credentialDeliveryTimer);
    [app applicationDidBecomeActive:activated]; CHECK(app.reveals == 1 && app.stub.reads == 2);
    app.foreground = NO; [app lockProtected:nil];
    CHECK(!app.shown && app.protectedState.metadata);
    app.foreground = YES; [app applicationDidBecomeActive:activated]; CHECK(!app.shown);

    [app requestProtected:1]; [app.stub complete:nil];
    CHECK(app.protectedState.metadata && !app.shown);
    [app requestProtected:1]; app.foreground = NO; [app.stub complete:fixture];
    [app clearProtected]; app.foreground = YES; [app applicationDidBecomeActive:activated];
    CHECK(!app.protectedState.metadata && !app.shown && !app.credentialDeliveryTimer);
    [app requestProtected:1];
    void (^lateReply)(NSDictionary *, NSString *) = app.stub.reply;
    [app clearProtected]; lateReply(fixture, nil);
    CHECK(!app.protectedState.metadata && !app.shown);
    [app requestProtected:1]; app.foreground = NO; [app.stub complete:fixture];
    CHECK([app.protectedState hasPendingCredentialAtTime:NSProcessInfo.processInfo.systemUptime]);
    [app.protectedState hasPendingCredentialAtTime:NSProcessInfo.processInfo.systemUptime + 31];
    app.foreground = YES; [app applicationDidBecomeActive:activated];
    CHECK(!app.shown && !app.credentialDeliveryTimer && app.protectedState.metadata);
    [app clearProtected];
    puts("Protected UI: focus handoff, one-shot reveal, expiry, cancellation, navigation and late replies passed");
} }
