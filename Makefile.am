SUBDIRS = . src conf resource

check_PROGRAMS = collector-tests
TESTS = collector-tests
collector_tests_SOURCES = tests/collector-tests.cpp
collector_tests_CPPFLAGS = -I$(srcdir)/src

EXTRA_DIST = README COPYING autogen \
	contrib/com.istat.powermetrics.helper.plist \
	contrib/com.istat.server.plist \
	contrib/install-macos-helper.sh \
	contrib/istat-powermetrics-helper \
	contrib/uninstall-macos-helper.sh
