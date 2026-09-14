#!/bin/sh
set -eu
identity=${1:?Specify an Apple-issued signing identity}
if [ "$identity" = - ]; then
    printf '%s\n' 'Signed XPC tests require an Apple-issued signing identity.' >&2
    exit 1
fi
stage=$(mktemp -d /private/tmp/istat-settings-trust.XXXXXX)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
cp build/ISCSettingsTrustTests "$stage/probe"
for identifier in org.istatserver.control org.istatserver.control.settings; do
    codesign --force --options runtime --timestamp=none --identifier "$identifier" --sign "$identity" "$stage/probe"
    "$stage/probe" "$identifier"
done
