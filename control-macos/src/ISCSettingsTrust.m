#import "ISCSettingsProtocol.h"
#import <Security/Security.h>

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
    NSCharacterSet *invalid = [[NSCharacterSet characterSetWithCharactersInString:@"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"] invertedSet];
    if (status != errSecSuccess || ![team isKindOfClass:NSString.class] || team.length != 10 ||
        [team rangeOfCharacterFromSet:invalid].location != NSNotFound ||
        !(flags.unsignedIntValue & kSecCodeSignatureRuntime) || [entitlements[@"com.apple.security.get-task-allow"] boolValue]) return nil;
    if (![identifier isEqual:@"org.istatserver.control"] && ![identifier isEqual:ISCSettingsService]) return nil;
    return [NSString stringWithFormat:@"anchor apple generic and identifier \"%@\" and certificate leaf[subject.OU] = \"%@\" and not entitlement[\"com.apple.security.get-task-allow\"] exists", identifier, team];
}
