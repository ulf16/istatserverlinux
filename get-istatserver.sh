#!/usr/bin/env sh
set -Eeuo pipefail

# Quick installer for ulf16/istatserverlinux (maintained fork)
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/ulf16/istatserverlinux/master/get-istatserver.sh -o istatserverlinux.sh && sh istatserverlinux.sh

REPO="ulf16/istatserverlinux"
BRANCH="master"

command_exists() { command -v "$1" >/dev/null 2>&1; }

need_root() {
  if [ "$(id -u)" -ne 0 ]; then
    if command_exists sudo; then
      sudo "$@"
    elif command_exists su; then
      su -c "$*"
    else
      echo "This step needs root (sudo or su)." >&2
      exit 1
    fi
  else
    "$@"
  fi
}

detect_pkg_mgr() {
  if command_exists apt-get; then echo apt; return
  elif command_exists dnf; then echo dnf; return
  elif command_exists yum; then echo yum; return
  elif command_exists zypper; then echo zypper; return
  elif command_exists pacman; then echo pacman; return
  elif command_exists apk; then echo apk; return
  fi
  echo unknown
}

install_deps() {
  pm="$(detect_pkg_mgr)"
  echo "Installing build dependencies via ${pm}…"
  case "$pm" in
    apt)
      need_root apt-get update -qq
      # libsensors dev pkg name differs by distro vintage
      # try libsensors-dev first, fall back to libsensors4-dev
      need_root apt-get install -y -qq \
        build-essential autoconf automake libtool pkg-config \
        libxml2-dev libssl-dev libsqlite3-dev libavahi-client-dev curl ca-certificates || true
      if ! dpkg -s libsensors-dev >/dev/null 2>&1; then
        need_root apt-get install -y -qq libsensors4-dev
      fi
      ;;
    dnf)
      need_root dnf -y -q install gcc-c++ make autoconf automake libtool pkgconfig \
        libxml2-devel openssl-devel sqlite-devel lm_sensors lm_sensors-devel avahi-devel curl ca-certificates
      ;;
    yum)
      need_root yum -y -q install gcc-c++ make autoconf automake libtool pkgconfig \
        libxml2-devel openssl-devel sqlite-devel lm_sensors lm_sensors-devel avahi-devel curl ca-certificates
      ;;
    zypper)
      need_root zypper -n install gcc-c++ make autoconf automake libtool pkg-config \
        libxml2-devel openssl-devel sqlite3-devel lm_sensors lm_sensors-devel avahi-devel curl ca-certificates
      ;;
    pacman)
      need_root pacman -Sy --noconfirm base-devel autoconf automake libtool pkgconf \
        libxml2 openssl sqlite lm_sensors avahi curl ca-certificates
      ;;
    apk)
      need_root apk add --no-cache build-base autoconf automake libtool pkgconf \
        libxml2-dev openssl-dev sqlite-dev lm-sensors-dev avahi-dev curl ca-certificates
      ;;
    *)
      echo "Unsupported distro: missing known package manager." >&2
      exit 1
      ;;
  esac
}

make_build_dir() {
  WORKDIR="$(mktemp -d -t istatserver.XXXXXX)"
  trap 'rm -rf "$WORKDIR"' EXIT
  echo "Using temp build dir: $WORKDIR"
}

fetch_source() {
  echo "Downloading ${REPO}@${BRANCH}…"
  need_root true >/dev/null 2>&1 || :
  curl -fsSL "https://github.com/${REPO}/archive/refs/heads/${BRANCH}.tar.gz" -o "$WORKDIR/src.tar.gz"
  mkdir -p "$WORKDIR/src"
  tar -xzf "$WORKDIR/src.tar.gz" -C "$WORKDIR/src" --strip-components=1
}

build_install() {
  cd "$WORKDIR/src"
  echo "Bootstrapping…"
  ./autogen >/dev/null
  echo "Configuring…"
  ./configure >/dev/null
  echo "Building…"
  make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)" >/dev/null
  echo "Installing…"
  need_root make install >/dev/null
}

ensure_user_group() {
  # Run service as unprivileged 'istat' user/group
  if ! id -u istat >/dev/null 2>&1; then
    echo "Creating service user 'istat'…"
    need_root /usr/sbin/groupadd -r istat 2>/dev/null || true
    need_root /usr/sbin/useradd -r -g istat -d /usr/local/etc/istatserver -s /usr/sbin/nologin istat 2>/dev/null || true
  fi
  need_root mkdir -p /usr/local/etc/istatserver
  need_root chown -R istat:istat /usr/local/etc/istatserver
}

install_rapl_udev_rule() {
  # Make RAPL energy counters world-readable so non-root can read power
  RULE='/etc/udev/rules.d/99-rapl-read.rules'
  echo "Installing RAPL udev rule at ${RULE}…"
  need_root sh -c "printf '%s\n' 'SUBSYSTEM==\"powercap\", KERNEL==\"intel-rapl:*\", TEST==\"%S%p/energy_uj\", RUN+=\"/bin/chmod 0444 %S%p/energy_uj\"' > '${RULE}'"
  need_root udevadm control --reload-rules || true
  need_root udevadm trigger --subsystem-match=powercap || true
}

install_systemd_unit() {
  if ! command_exists systemctl; then
    echo "systemd not detected. Skipping unit install." >&2
    return 0
  fi

  UNIT=/etc/systemd/system/istatserver.service
  echo "Installing systemd unit: ${UNIT}"
  need_root tee "$UNIT" >/dev/null <<'UNIT_EOF'
[Unit]
Description=iStat Server for remote monitoring with iStat for iOS or iStat for macOS
Documentation=man:istatserver(1)
After=network-online.target systemd-udevd.service
Wants=network-online.target

[Service]
Type=simple
User=istat
Group=istat
ExecStart=/usr/local/bin/istatserver
Restart=on-failure
RestartSec=3
RuntimeDirectory=istatserver
# If your build writes a pid when -d is used, we run in foreground so no pid is needed.

# Hardening (relaxed enough for reading sensors via sysfs)
NoNewPrivileges=true
LockPersonality=true
PrivateTmp=true
ProtectSystem=full
ProtectHome=true
ProtectControlGroups=true
ProtectKernelModules=true
ProtectKernelTunables=true
ProtectClock=true
RestrictNamespaces=true
RestrictRealtime=true

[Install]
WantedBy=multi-user.target
UNIT_EOF

  need_root systemctl daemon-reload
  need_root systemctl enable --now istatserver
}

print_summary() {
  echo
  echo "✅ iStat Server installed from ${REPO}@${BRANCH}"
  echo "   Binary:        /usr/local/bin/istatserver"
  echo "   Config dir:    /usr/local/etc/istatserver/"
  echo "   Service user:  istat"
  if command_exists systemctl; then
    echo "   Systemd unit:  istatserver.service (enabled)"
    echo
    echo "Status:"
    need_root systemctl --no-pager --full status istatserver || true
  else
    echo
    echo "Run manually:"
    echo "   sudo -u istat /usr/local/bin/istatserver"
  fi
  echo
  echo "Note: RAPL power counters were made world-readable via udev so"
  echo "      the service can read /sys/class/powercap/intel-rapl:*/energy_uj."
}

main() {
  install_deps
  make_build_dir
  fetch_source
  build_install
  ensure_user_group
  install_rapl_udev_rule
  install_systemd_unit
  print_summary
}

main "$@"
