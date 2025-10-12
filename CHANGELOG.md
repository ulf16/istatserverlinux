# iStat Server for Linux – Changelog

## v1.1.0 — 2025-10-12
**“Modern Linux Edition”**

This release rebuilds and hardens iStat Server for modern Linux distributions.  
It focuses on maintainability, portability, and secure installation.

### ✨ Major Changes
- Rewritten `configure.ac` and `Makefile.am` for current autoconf/automake
- Added `--enable-systemd-unit` option to install a systemd service
- Added Avahi/Bonjour LAN discovery (optional, auto-detects if libraries present)
- Added OpenSSL and SQLite detection via `pkg-config`
- Added compiler hardening flags (`-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, RELRO/NOW)
- Improved error messages and dependency checks (libxml2, SQLite, OpenSSL)
- Enforced secure config permissions:
  - `/usr/local/etc/istatserver` → `0750`
  - `/usr/local/etc/istatserver/istatserver.conf` → `0640`
- Service now runs as unprivileged user `istat`
- Updated README and documentation for modern systems

### 🧩 Platform Support
| Platform | Architecture | Status | Notes |
|-----------|---------------|--------|-------|
| **Ubuntu 22.04+ / Debian 12+** | x86_64 | ✅ Stable | Tested on Intel Mac mini (x64) |
| **Ubuntu 22.04+ / Armbian** | arm64 (AArch64) | ✅ Stable | Tested on Odroid N2 |
| **Ubuntu 20.04+ / Armbian** | armv7l (32-bit ARM) | ✅ Stable | Tested on Odroid XU4 |
| **Other Linux distros** | varies | ⚙️ Likely | Requires standard GNU toolchain |
| **macOS** | x86_64 / arm64 | 🚧 Not yet | Build incomplete due to missing frameworks |

### 🔒 Security
- Config directory and files now protected from non-istat users.
- Optional Avahi service broadcasting for LAN discovery.
- TLS via OpenSSL available by default.
- Hardened compiler/linker flags to mitigate memory and binary exploitation risks.

---

## v1.0.0 — 2025-10-11
### Modernized OpenSSL certificate generation (EVP_PKEY API, OpenSSL 3.0+ compatible)
- Migrated OpenSSL certificate generation to the **EVP_PKEY** API (no deprecated RSA calls).
- Certificates now use **SHA-256** signatures and modern cipher defaults.
- Fully compatible with **OpenSSL 3.0+** (Ubuntu 22.04 / 24.04, Armbian, Debian 12, macOS).
- Removed legacy `RSA_generate_key()` and `EC_KEY_free()` usage — clean builds, no warnings.
### Unified sensor detection for Odroid N2, XU4, and Intel Mac Mini
- Unified sensor handling across all systems:
  - **Odroid N2**, **Odroid XU4**, and **Intel Mac Mini** verified.
  - CPU, GPU, SoC, and thermal zones now discovered automatically via **sysfs**.
### Build & Toolchain
- Updated `configure.ac` (Autoconf 2.69+, Automake 1.16)
- Refreshed helper scripts (`compile`, `config.guess`, etc.)
- Works with GCC 11–14 (tested on Ubuntu 24.04 and Armbian 24.x)

## 2025-10-08
### CPU Power and Frequency Monitoring (non-root setup)

- Added modern Linux hardware telemetry support for Intel systems — including live CPU package power and frequency — **without requiring root privileges**.

### New sensors

- **RAPL Power Domains** — reports real-time power (in watts) per CPU domain:
  - `/sys/class/powercap/intel-rapl:*/*/energy_uj`
  - Typically includes `package-0`, `core`, and `uncore`
- **CPU Frequency** — reads per-policy current CPU frequency from:
  - `/sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq`

All sensors are visible remotely in iStat for macOS/iOS through the `istatserver` daemon.

---

### Non-root access to RAPL energy readings

- By default, RAPL energy files (`energy_uj`) are readable only by **root**.  
- To allow the unprivileged `istat` service user to read them safely, add this **udev rule**:

`cat <<'RULE' | sudo tee /etc/udev/rules.d/99-rapl-read.rules
SUBSYSTEM=="powercap", KERNEL=="intel-rapl:*", TEST=="%S%p/energy_uj", RUN+="/bin/chmod 0444 %S%p/energy_uj"
RULE`

`sudo udevadm control --reload-rules`

`sudo udevadm trigger --subsystem-match=powercap`

- You can verify that the permissions were applied correctly:

`ls -l /sys/class/powercap/intel-rapl:*/energy_uj`
- expected: -r--r--r--

`sudo -u istat cat /sys/class/powercap/intel-rapl:0/energy_uj`
- should print a numeric value (microjoules)

This change survives reboots and does not weaken system security — only allows read access to instantaneous CPU energy counters.

⸻

## Systemd unit (modernized)

### This updated service file runs iStat Server as the istat user, with minimal privileges and no PID management needed:

[Unit]
Description=iStat Server for remote monitoring with iStat for iOS/macOS
Documentation=man:istatserver(1)
After=network-online.target systemd-udevd.service
Wants=network-online.target

[Service]
Type=simple
User=istat
Group=istat
ExecStart=/usr/local/bin/istatserver
WorkingDirectory=/usr/local/etc/istatserver
Restart=on-failure
RestartSec=2
AmbientCapabilities=
NoNewPrivileges=true
ProtectSystem=full
ReadWritePaths=/usr/local/etc/istatserver

[Install]
WantedBy=multi-user.target

## 2014
Original open-source release by [Bjango](https://github.com/bjango/istatserverlinux) based on on [istatd](https://github.com/tiwilliam/istatd) by William Tisäter.
