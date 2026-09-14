#import "ISCReport.h"

static void check(BOOL ok, NSString *message) {
    if (!ok) { NSLog(@"FAIL: %@", message); exit(1); }
}
static NSData *encode(id value) {
    return [NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
}
static NSMutableDictionary *fixture(void) {
    NSDictionary *service = @{@"label": @"com.istat.server", @"state": @"running", @"identity": @"matched", @"pid": @123, @"last_exit": NSNull.null};
    return [@{@"schema": @1, @"mode": @"read-only", @"host": @"test-host", @"platform": @"Darwin", @"checked": @1000,
              @"installation": @{@"prefix": @"/opt/istatserverlinux", @"binary": @"/opt/istatserverlinux/bin/istatserver",
                                   @"configuration": @"/opt/istatserverlinux/etc/istatserver/istatserver.conf", @"installed": @YES, @"classic_present": @NO, @"version": NSNull.null},
              @"configuration": @{@"state": @"permission-denied", @"pairing": @"unknown", @"port": NSNull.null, @"address": NSNull.null},
              @"service": service, @"helpers": @[service],
              @"listeners": @{@"state": @"observed", @"ports": @[@5109]},
              @"health": @{@"state": @"fresh", @"devices": @3, @"skipped": @2, @"checked": @990}} mutableCopy];
}
int main(void) {
    @autoreleasepool {
        NSMutableDictionary *raw = fixture();
        check(ISCReadReport(encode(raw)) != nil, @"Accept version-one report");
        raw[@"private_password"] = @"never-reexport";
        NSMutableDictionary *config = [raw[@"configuration"] mutableCopy];
        config[@"server_code"] = @"never-reexport"; raw[@"configuration"] = config;
        raw[@"capabilities"] = @{@"service_control": @YES};
        NSDictionary *clean = ISCReadReport(encode(raw));
        check(clean != nil, @"Unknown extension fields tolerated");
        NSString *json = [[NSString alloc] initWithData:encode(clean) encoding:NSUTF8StringEncoding];
        check(![json containsString:@"never-reexport"], @"Drop unknown credential fields at every level");
        check([clean[@"capabilities"][@"service_control"] isEqual:@NO], @"A report cannot grant administration capabilities");
        for (NSString *key in @[@"installation", @"configuration", @"service", @"health", @"listeners", @"helpers", @"host", @"checked"]) {
            raw = fixture(); raw[key] = @{};
            check(ISCReadReport(encode(raw)) == nil, [@"Reject malformed " stringByAppendingString:key]);
        }
        raw = fixture(); raw[@"listeners"] = @{@"state": @"observed", @"ports": @[@65536]};
        check(ISCReadReport(encode(raw)) == nil, @"Reject invalid listener port");
        raw = fixture(); raw[@"schema"] = @2;
        check(ISCReadReport(encode(raw)) == nil, @"Reject unknown schema");
        raw = fixture(); raw[@"checked"] = @1e100;
        check(ISCReadReport(encode(raw)) == nil, @"Reject out-of-range dates before rendering");
        raw = fixture(); raw[@"listeners"] = @{@"state": @"observed", @"ports": @[@5109.5]};
        check(ISCReadReport(encode(raw)) == nil, @"Reject fractional ports");
        check(ISCReadReport([NSMutableData dataWithLength:65537]) == nil, @"Bound file size");
        check(ISCReadReport([@"not JSON" dataUsingEncoding:NSUTF8StringEncoding]) == nil, @"Reject bad JSON");
        puts("Status report validation tests passed");
    }
    return 0;
}
