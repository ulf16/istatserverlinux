#!/bin/sh
set -eu
PREFIX=${1:-/usr/local}
SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test "$(id -u)" = 0 || { echo 'Run this installer as root.' >&2; exit 1; }
install -d -m 755 "$PREFIX/libexec" /var/run/istatserver-smart
install -m 755 "$SOURCE/istat-smart-helper.py" "$PREFIX/libexec/istat-smart-helper.py"
if [ "$(uname -s)" = Darwin ]; then
    PYTHON=/opt/homebrew/bin/python3
    test -x "$PYTHON"
    /usr/bin/sed -e "s|@PREFIX@|$PREFIX|g" -e "s|@PYTHON@|$PYTHON|g" "$SOURCE/com.istat.smart.helper.plist.in" > /Library/LaunchDaemons/com.istat.smart.helper.plist
    chown root:wheel /Library/LaunchDaemons/com.istat.smart.helper.plist
    chmod 644 /Library/LaunchDaemons/com.istat.smart.helper.plist
    launchctl bootout system/com.istat.smart.helper 2>/dev/null || true
    sleep 2
    launchctl bootstrap system /Library/LaunchDaemons/com.istat.smart.helper.plist
else
    test -x /usr/bin/python3
    test -x /usr/sbin/smartctl || { echo 'Install smartmontools first.' >&2; exit 1; }
    sed "s|@PREFIX@|$PREFIX|g" "$SOURCE/istat-smart.service.in" > /etc/systemd/system/istat-smart.service
    install -m 644 "$SOURCE/istat-smart.timer" /etc/systemd/system/istat-smart.timer
    systemctl daemon-reload
    systemctl enable --now istat-smart.timer
    systemctl start istat-smart.service
fi
