#import <Foundation/Foundation.h>
#import "ISCSettingsClient.h"
#import "ISCSettingsProtocol.h"
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void) { @autoreleasepool {
    NSMutableDictionary *fixture = [@{@"schema":@1, @"state":@"readable", @"scope":@"configuration-file", @"port":@5109,
        @"address":@"::1", @"bonjour":@"not-observed", @"auth_mode":@"password", @"credential_source":@"configured",
        @"credential":@"test-only-secret", @"untrusted":@"not-exported"} mutableCopy];
    NSData *(^encode)(void) = ^{ return [NSJSONSerialization dataWithJSONObject:fixture options:0 error:nil]; };
    CHECK(!ISCValidateSettings(encode(), NO)[@"credential"]);
    CHECK(!ISCValidateSettings(encode(), YES)[@"untrusted"]);
    CHECK([ISCValidateSettings(encode(), YES)[@"credential"] isEqual:@"test-only-secret"]);
    fixture[@"port"] = @65536; CHECK(!ISCValidateSettings(encode(), YES));
    fixture[@"port"] = @1.5; CHECK(!ISCValidateSettings(encode(), YES));
    fixture[@"port"] = @YES; CHECK(!ISCValidateSettings(encode(), YES));
    fixture[@"port"] = @5109; fixture[@"schema"] = @2; CHECK(!ISCValidateSettings(encode(), YES));
    fixture[@"schema"] = @YES; CHECK(!ISCValidateSettings(encode(), YES));
    fixture[@"schema"] = @1; fixture[@"address"] = @"secret-injected-label"; CHECK(!ISCValidateSettings(encode(), YES));
    fixture[@"address"] = @"::1"; fixture[@"credential"] = @[]; CHECK(!ISCValidateSettings(encode(), YES));
    CHECK(!ISCValidateSettings([@"[]" dataUsingEncoding:NSUTF8StringEncoding], YES));
    CHECK(!ISCSettingsPeerRequirement(@"arbitrary.peer"));
    // Test binaries are ad-hoc/unsigned: privileged access must fail closed.
    CHECK(!ISCSettingsPeerRequirement(ISCSettingsService));
    __block NSUInteger completions = 0;
    ISCSettingsClient *client = [ISCSettingsClient new];
    [client readInstallation:@"opt" reveal:YES completion:^(NSDictionary *result, NSString *error) {
        CHECK(!result && error.length); ++completions;
    }];
    [client cancel]; [client cancel];
    CHECK(completions == 1);
    puts("Settings reply validation and unsigned-client rejection passed");
} }
