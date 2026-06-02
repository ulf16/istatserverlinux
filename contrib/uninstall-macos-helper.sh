#!/bin/bash
set -euo pipefail

PREFIX="/opt/istatserverlinux"
PLIST="/Library/LaunchDaemons/com.istat.powermetrics.helper.plist"
VAR_DIR="$PREFIX/var"
PURGE="${1:-}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Not on macOS; nothing to do."
  exit 0
fi

if launchctl print system/com.istat.powermetrics.helper >/dev/null 2>&1; then
  sudo launchctl bootout system/com.istat.powermetrics.helper || true
fi
if [[ -f "$PLIST" ]]; then
  sudo rm -f "$PLIST"
fi

if [[ "$PURGE" == "purge" ]]; then
  sudo rm -f "$VAR_DIR/powermetrics.kv" || true
fi

echo "Uninstalled powermetrics helper. Data dir kept; run with 'purge' to remove KV file."
