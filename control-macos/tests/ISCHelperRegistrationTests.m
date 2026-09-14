#import "ISCHelperRegistration.h"
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void) { @autoreleasepool {
    NSError *denied = [NSError errorWithDomain:(__bridge NSString *)kSMErrorDomainFramework code:kSMErrorLaunchDeniedByUser userInfo:nil];
    NSError *invalid = [NSError errorWithDomain:(__bridge NSString *)kSMErrorDomainFramework code:kSMErrorInvalidSignature userInfo:nil];
    CHECK(ISCRegistrationResult(SMAppServiceStatusRequiresApproval, NO, denied) == ISCHelperRegistrationWaiting);
    CHECK(ISCRegistrationResult(SMAppServiceStatusNotRegistered, NO, denied) == ISCHelperRegistrationWaiting);
    CHECK(ISCRegistrationResult(SMAppServiceStatusNotRegistered, YES, nil) == ISCHelperRegistrationWaiting);
    CHECK(ISCRegistrationResult(SMAppServiceStatusEnabled, NO, denied) == ISCHelperRegistrationReady);
    CHECK(ISCRegistrationResult(SMAppServiceStatusNotRegistered, NO, invalid) == ISCHelperRegistrationFailed);
    CHECK(ISCRegistrationResult(SMAppServiceStatusNotFound, NO, denied) == ISCHelperRegistrationFailed);
    CHECK(ISCRegistrationObserved(SMAppServiceStatusNotRegistered, ISCHelperRegistrationWaiting) == ISCHelperRegistrationWaiting);
    CHECK(ISCRegistrationObserved(SMAppServiceStatusRequiresApproval, ISCHelperRegistrationReady) == ISCHelperRegistrationWaiting);
    CHECK(ISCRegistrationObserved(SMAppServiceStatusEnabled, ISCHelperRegistrationWaiting) == ISCHelperRegistrationReady);
    CHECK(ISCRegistrationObserved(SMAppServiceStatusNotRegistered, ISCHelperRegistrationReady) == ISCHelperRegistrationIdle);
    puts("Helper pending approval, completion, revocation and failure tests passed");
} }
