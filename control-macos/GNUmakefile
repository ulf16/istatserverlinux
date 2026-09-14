APP := build/iStatServerControl.app
SOURCES := $(wildcard src/*.m)
FLAGS := -fobjc-arc -Wall -Wextra -Wno-unused-parameter -mmacosx-version-min=11.0

.PHONY: all test
all: $(APP)/Contents/MacOS/iStatServerControl

$(APP)/Contents/MacOS/iStatServerControl: $(SOURCES) $(wildcard src/*.h) Info.plist ../contrib/istat-server-status.py README.md
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	cp Info.plist $(APP)/Contents/Info.plist
	cp ../contrib/istat-server-status.py README.md $(APP)/Contents/Resources/
	clang $(FLAGS) -framework Cocoa $(SOURCES) -o $@
	codesign --force --sign - $(APP)

test:
	python3 ../tests/test-server-status.py
	mkdir -p build
	clang $(FLAGS) -framework Foundation -Isrc tests/ISCReportTests.m src/ISCReport.m -o build/ISCReportTests
	build/ISCReportTests
