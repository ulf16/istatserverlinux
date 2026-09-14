#!/bin/sh
set -eu

# authd may not inspect executables in privacy-protected user folders such as
# Documents. Keep this test disposable and outside the source checkout.
directory=$(mktemp -d /private/tmp/istat-settings-auth.XXXXXX)
trap 'rm -rf "$directory"' EXIT HUP INT TERM
cp build/ISCSettingsHelperTests "$directory/ISCSettingsHelperTests"
codesign --force --sign - --options runtime --identifier org.istatserver.control.helper-tests "$directory/ISCSettingsHelperTests"
"$directory/ISCSettingsHelperTests"
