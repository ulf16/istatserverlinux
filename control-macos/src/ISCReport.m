#import "ISCReport.h"
#include <math.h>

static BOOL ISCString(id value) {
    return [value isKindOfClass:NSString.class] && [value length] <= 4096;
}

static BOOL ISCInteger(id value, double minimum, double maximum, BOOL nullable) {
    if (nullable && (!value || value == NSNull.null)) return YES;
    if (![value isKindOfClass:NSNumber.class]) return NO;
    double number = [value doubleValue];
    return isfinite(number) && floor(number) == number && number >= minimum && number <= maximum;
}

static BOOL ISCProcessNumbers(NSDictionary *record) {
    return ISCInteger(record[@"pid"], 1, 2147483647, YES) &&
           ISCInteger(record[@"last_exit"], -2147483648.0, 2147483647, YES);
}

static NSDictionary *ISCSection(id value, NSArray *strings, NSArray *numbers, NSArray *optionalNumbers, NSArray *optionalStrings) {
    if (![value isKindOfClass:NSDictionary.class]) return nil;
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    for (NSString *key in strings) {
        if (!ISCString(value[key])) return nil;
        out[key] = value[key];
    }
    for (NSString *key in numbers) {
        if (![value[key] isKindOfClass:NSNumber.class]) return nil;
        out[key] = value[key];
    }
    for (NSString *key in optionalNumbers) {
        if (value[key] && value[key] != NSNull.null && ![value[key] isKindOfClass:NSNumber.class]) return nil;
        out[key] = value[key] ?: NSNull.null;
    }
    for (NSString *key in optionalStrings) {
        if (value[key] && value[key] != NSNull.null && !ISCString(value[key])) return nil;
        out[key] = value[key] ?: NSNull.null;
    }
    return out;
}

NSDictionary *ISCReadReport(NSData *data) {
    if (!data || data.length > 65536) return nil;
    id raw = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if (![raw isKindOfClass:NSDictionary.class] || ![raw[@"schema"] isEqual:@1] || ![raw[@"mode"] isEqual:@"read-only"]) return nil;
    NSMutableDictionary *out = [ISCSection(raw, @[@"host", @"platform", @"mode"], @[@"schema", @"checked"], @[], @[]) mutableCopy];
    if (!out) return nil;
    if (!ISCInteger(out[@"checked"], 0, 253402300799.0, NO)) return nil;
    NSDictionary *installation = ISCSection(raw[@"installation"], @[@"prefix", @"binary", @"configuration"], @[@"installed", @"classic_present"], @[], @[@"version"]);
    NSDictionary *config = ISCSection(raw[@"configuration"], @[@"state", @"pairing"], @[], @[@"port"], @[@"address"]);
    NSDictionary *service = ISCSection(raw[@"service"], @[@"state", @"identity", @"label"], @[], @[@"pid", @"last_exit"], @[]);
    NSDictionary *health = ISCSection(raw[@"health"], @[@"state"], @[@"devices", @"skipped"], @[@"checked"], @[]);
    if (!installation || !config || !service || !health) return nil;
    if (!ISCInteger(config[@"port"], 1, 65535, YES) || !ISCProcessNumbers(service) ||
        !ISCInteger(health[@"checked"], 0, 253402300799.0, YES) ||
        !ISCInteger(health[@"devices"], 0, 64, NO) ||
        !ISCInteger(health[@"skipped"], 0, [health[@"devices"] doubleValue], NO)) return nil;
    out[@"installation"] = installation; out[@"configuration"] = config;
    out[@"service"] = service; out[@"health"] = health;
    id listeners = raw[@"listeners"];
    if (![listeners isKindOfClass:NSDictionary.class] || !ISCString(listeners[@"state"]) ||
        ![listeners[@"ports"] isKindOfClass:NSArray.class] || [listeners[@"ports"] count] > 64) return nil;
    for (id port in listeners[@"ports"])
        if (!ISCInteger(port, 1, 65535, NO)) return nil;
    out[@"listeners"] = @{@"state": listeners[@"state"], @"ports": listeners[@"ports"]};
    if (![raw[@"helpers"] isKindOfClass:NSArray.class] || [raw[@"helpers"] count] > 8) return nil;
    NSMutableArray *helpers = [NSMutableArray array];
    for (id value in raw[@"helpers"]) {
        NSDictionary *helper = ISCSection(value, @[@"state", @"identity", @"label"], @[], @[@"pid", @"last_exit"], @[]);
        if (!helper || !ISCProcessNumbers(helper)) return nil;
        [helpers addObject:helper];
    }
    out[@"helpers"] = helpers;
    out[@"bonjour"] = @"not-observed";
    out[@"capabilities"] = @{@"status": @YES, @"credential_read": @NO, @"settings_write": @NO, @"service_control": @NO, @"logs": @NO};
    return out;
}
