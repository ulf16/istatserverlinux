APP := build/iStatServerControl.app
SOURCES := $(wildcard src/*.m)
FLAGS := -fobjc-arc -Wall -Wextra -Wno-unused-parameter -mmacosx-version-min=11.0
SIGN_IDENTITY ?= -
SIGN_TIMESTAMP ?= --timestamp=none
HELPER := $(APP)/Contents/Library/HelperTools/istat-settings-helper
CORE := ../contrib/settings/Settings.cpp

.PHONY: all test
all: $(APP)/Contents/MacOS/iStatServerControl

$(APP)/Contents/MacOS/iStatServerControl: $(SOURCES) $(wildcard src/*.h) Info.plist ../contrib/istat-server-status.py README.md $(HELPER) build/AppIcon.icns
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	cp Info.plist $(APP)/Contents/Info.plist
	cp ../contrib/istat-server-status.py README.md $(APP)/Contents/Resources/
	cp build/AppIcon.icns $(APP)/Contents/Resources/
	clang $(FLAGS) -framework Cocoa -framework Security -framework ServiceManagement $(SOURCES) -o $@
	codesign --force --options runtime $(SIGN_TIMESTAMP) --sign "$(SIGN_IDENTITY)" $(APP)

build/AppIcon.icns: assets/AppIcon.png assets/build-icon.sh
	sh assets/build-icon.sh

$(HELPER): helper/main.mm src/ISCSettingsProtocol.h src/ISCSettingsTrust.m $(CORE) ../contrib/settings/Settings.hpp helper/org.istatserver.control.settings.plist
	mkdir -p $(dir $(HELPER)) $(APP)/Contents/Library/LaunchDaemons
	cp helper/org.istatserver.control.settings.plist $(APP)/Contents/Library/LaunchDaemons/
	clang++ -x objective-c++ -std=c++11 -fobjc-arc -Wall -Wextra -mmacosx-version-min=13.0 -framework Foundation -framework Security -Isrc -I../contrib/settings helper/main.mm src/ISCSettingsTrust.m $(CORE) -o $@
	codesign --force --options runtime $(SIGN_TIMESTAMP) --identifier org.istatserver.control.settings --sign "$(SIGN_IDENTITY)" $@

test:
	python3 ../tests/test-server-status.py
	mkdir -p build
	clang $(FLAGS) -framework Foundation -Isrc tests/ISCReportTests.m src/ISCReport.m -o build/ISCReportTests
	build/ISCReportTests
	c++ -std=c++11 -Wall -Wextra -DISTAT_SETTINGS_TESTING ../tests/settings-tests.cpp $(CORE) -o build/settings-tests
	build/settings-tests
	clang $(FLAGS) -framework Foundation -framework Security -Isrc tests/ISCSettingsTests.m src/ISCSettingsClient.m src/ISCSettingsTrust.m -o build/ISCSettingsTests
	build/ISCSettingsTests
	clang $(FLAGS) -framework Foundation -framework Security -Isrc tests/ISCSettingsTrustTests.m src/ISCSettingsTrust.m -o build/ISCSettingsTrustTests
	build/ISCSettingsTrustTests
	clang $(FLAGS) -framework Foundation -framework ServiceManagement -Isrc tests/ISCHelperRegistrationTests.m src/ISCHelperRegistration.m -o build/ISCHelperRegistrationTests
	build/ISCHelperRegistrationTests
	clang $(FLAGS) -framework Cocoa -framework Security -framework ServiceManagement -Isrc tests/ISCProtectedSettingsTests.m $(filter-out src/main.m,$(SOURCES)) -o build/ISCProtectedSettingsTests
	build/ISCProtectedSettingsTests
	clang++ -x objective-c++ -std=c++11 -fobjc-arc -Wall -Wextra -DISC_HELPER_TESTING -mmacosx-version-min=13.0 -framework Foundation -framework Security -Isrc -I../contrib/settings tests/ISCSettingsHelperTests.mm helper/main.mm src/ISCSettingsTrust.m $(CORE) -o build/ISCSettingsHelperTests
	sh tests/run-helper-tests.sh

.PHONY: settings-cli
.PHONY: test-signed
test-signed:
	mkdir -p build
	clang $(FLAGS) -framework Foundation -framework Security -Isrc tests/ISCSettingsTrustTests.m src/ISCSettingsTrust.m -o build/ISCSettingsTrustTests
	sh tests/run-trust-tests.sh "$(SIGN_IDENTITY)"

settings-cli:
	mkdir -p build
	c++ -std=c++11 -Wall -Wextra ../contrib/settings/main.cpp $(CORE) -o build/istat-settings-read
