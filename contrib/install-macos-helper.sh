#!/bin/bash
set -euo pipefail

PREFIX="/opt/istatserverlinux"
HELPER="$PREFIX/libexec/istat-powermetrics-helper"
PLIST_SRC="$PREFIX/contrib/com.istat.powermetrics.helper.plist"
PLIST_DST="/Library/LaunchDaemons/com.istat.powermetrics.helper.plist"
VAR_DIR="$PREFIX/var"
LOG_DIR="$VAR_DIR/log"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
  echo "This helper is only for macOS Apple Silicon (arm64)."
  exit 0
fi

if [[ ! -x "$HELPER" ]]; then
  echo "Missing helper: $HELPER"
  exit 1
fi
if [[ ! -f "$PLIST_SRC" ]]; then
  echo "Missing plist: $PLIST_SRC"
  exit 1
fi

sudo install -d -o istat -g staff -m 0750 "$VAR_DIR"
sudo install -d -o istat -g staff -m 0750 "$LOG_DIR"

sudo install -m 0644 "$PLIST_SRC" "$PLIST_DST"
sudo chown root:wheel "$PLIST_DST"

sudo launchctl bootstrap system "$PLIST_DST" || true
sudo launchctl enable system/com.istat.powermetrics.helper
sudo launchctl kickstart -k system/com.istat.powermetrics.helper

echo "Helper installed. Checking output..."
sleep 2
if [[ -s "$VAR_DIR/powermetrics.kv" ]]; then
  echo "OK: $VAR_DIR/powermetrics.kv"
  tail -n +1 "$VAR_DIR/powermetrics.kv"
else
  echo "Warning: no powermetrics.kv yet. Wait about 10 seconds or check /var/log/istat-pm.err"
fi
