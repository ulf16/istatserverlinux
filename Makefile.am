SUBDIRS = . src conf resource

check_PROGRAMS = collector-tests settings-tests
TESTS = collector-tests settings-tests
collector_tests_SOURCES = tests/collector-tests.cpp
collector_tests_CPPFLAGS = -I$(srcdir)/src
settings_tests_SOURCES = tests/settings-tests.cpp contrib/settings/Settings.cpp contrib/settings/Settings.hpp
settings_tests_CPPFLAGS = -DISTAT_SETTINGS_TESTING
settings_tests_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11

EXTRA_DIST = README COPYING autogen \
	contrib/istat-server-status.py tests/test-server-status.py \
	control-macos/GNUmakefile control-macos/Info.plist control-macos/README.md \
	control-macos/src/main.m control-macos/src/ISCReport.h control-macos/src/ISCReport.m \
	control-macos/tests/ISCReportTests.m \
	contrib/settings/Settings.hpp contrib/settings/Settings.cpp contrib/settings/main.cpp tests/settings-tests.cpp \
	control-macos/src/ISCSettingsProtocol.h control-macos/src/ISCSettingsTrust.m \
	control-macos/src/ISCSettingsClient.h control-macos/src/ISCSettingsClient.m \
	control-macos/helper/main.mm control-macos/helper/org.istatserver.control.settings.plist \
	control-macos/tests/ISCSettingsTests.m control-macos/tests/ISCSettingsHelperTests.mm \
	control-macos/tests/run-helper-tests.sh \
	control-macos/assets/AppIcon.png control-macos/assets/build-icon.sh control-macos/assets/README.md \
	contrib/istat-smart-helper.py contrib/install-smart-helper.sh \
	contrib/istat-smart.service.in contrib/istat-smart.timer \
	contrib/com.istat.smart.helper.plist.in tests/test-smart-helper.py \
	contrib/com.istat.powermetrics.helper.plist \
	contrib/com.istat.server.plist \
	contrib/install-macos-helper.sh \
	contrib/istat-powermetrics-helper \
	contrib/uninstall-macos-helper.sh
