#import "ISCSettingsProtocol.h"
#import <Security/Security.h>

NSString *ISCSettingsRequirementForTeam(NSString *identifier, NSString *team) {
    NSCharacterSet *invalid = [[NSCharacterSet characterSetWithCharactersInString:@"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"] invertedSet];
    if (![team isKindOfClass:NSString.class] || team.length != 10 ||
        [team rangeOfCharacterFromSet:invalid].location != NSNotFound) return nil;
    if (![identifier isEqual:@"org.istatserver.control"] && ![identifier isEqual:ISCSettingsService]) return nil;
    NSString *text = [NSString stringWithFormat:@"anchor apple generic and identifier \"%@\" and certificate leaf[subject.OU] = \"%@\" and !entitlement[\"com.apple.security.get-task-allow\"] exists", identifier, team];
    SecRequirementRef compiled = NULL;
    OSStatus status = SecRequirementCreateWithString((__bridge CFStringRef)text, kSecCSDefaultFlags, &compiled);
    if (compiled) CFRelease(compiled);
    return status == errSecSuccess ? text : nil;
}

BOOL ISCApplySettingsRequirement(NSXPCConnection *connection, NSString *requirement) {
    if (@available(macOS 13.0, *)) {
        if (connection && requirement.length) {
            @try {
                [connection setCodeSigningRequirement:requirement];
                return YES;
            } @catch (NSException *exception) {
                // Never resume a connection whose peer restriction was rejected.
            }
        }
    }
    [connection invalidate];
    return NO;
}

NSString *ISCSettingsPeerRequirement(NSString *identifier) {
    SecCodeRef code = NULL;
    CFDictionaryRef info = NULL;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &code) != errSecSuccess) return nil;
    OSStatus status = SecCodeCopySigningInformation(code, kSecCSSigningInformation, &info);
    CFRelease(code);
    NSDictionary *details = CFBridgingRelease(info);
    NSString *team = details[(__bridge NSString *)kSecCodeInfoTeamIdentifier];
    NSNumber *flags = details[(__bridge NSString *)kSecCodeInfoFlags];
    NSDictionary *entitlements = details[(__bridge NSString *)kSecCodeInfoEntitlementsDict];
    if (status != errSecSuccess ||
        !(flags.unsignedIntValue & kSecCodeSignatureRuntime) || [entitlements[@"com.apple.security.get-task-allow"] boolValue]) return nil;
    return ISCSettingsRequirementForTeam(identifier, team);
}
