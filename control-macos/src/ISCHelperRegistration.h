#import <ServiceManagement/ServiceManagement.h>

typedef NS_ENUM(NSInteger, ISCHelperRegistrationState) {
    ISCHelperRegistrationIdle,
    ISCHelperRegistrationWaiting,
    ISCHelperRegistrationReady,
    ISCHelperRegistrationFailed
};

// A launch-denied result can mean the approval UI is still awaiting the user.
ISCHelperRegistrationState ISCRegistrationResult(SMAppServiceStatus status, BOOL registered, NSError *error);
ISCHelperRegistrationState ISCRegistrationObserved(SMAppServiceStatus status, ISCHelperRegistrationState previous);
