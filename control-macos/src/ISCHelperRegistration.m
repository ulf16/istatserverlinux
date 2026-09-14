#import "ISCHelperRegistration.h"

ISCHelperRegistrationState ISCRegistrationResult(SMAppServiceStatus status, BOOL registered, NSError *error) {
    if (status == SMAppServiceStatusEnabled) return ISCHelperRegistrationReady;
    if (status == SMAppServiceStatusRequiresApproval) return ISCHelperRegistrationWaiting;
    if (status == SMAppServiceStatusNotFound) return ISCHelperRegistrationFailed;
    BOOL serviceError = [error.domain isEqual:(__bridge NSString *)kSMErrorDomainFramework];
    if (@available(macOS 15.0, *)) serviceError |= [error.domain isEqual:SMAppServiceErrorDomain];
    if (registered || (serviceError && (error.code == kSMErrorLaunchDeniedByUser || error.code == kSMErrorAlreadyRegistered)))
        return ISCHelperRegistrationWaiting;
    return ISCHelperRegistrationFailed;
}

ISCHelperRegistrationState ISCRegistrationObserved(SMAppServiceStatus status, ISCHelperRegistrationState previous) {
    if (status == SMAppServiceStatusEnabled) return ISCHelperRegistrationReady;
    if (status == SMAppServiceStatusRequiresApproval) return ISCHelperRegistrationWaiting;
    if (status == SMAppServiceStatusNotFound) return ISCHelperRegistrationFailed;
    return previous == ISCHelperRegistrationWaiting ? previous : ISCHelperRegistrationIdle;
}
