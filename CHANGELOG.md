# iStat Server for Linux – Changelog

## Unreleased

- Add optional read-only SMART collection: native macOS diskutil health and
  Linux smartctl JSON for native ATA/NVMe devices. A separate five-minute helper
  writes an atomic cache; the network daemon never launches disk queries.
- Map APFS snapshots/physical stores, AppleRAID members, and Linux
  partition/device-mapper/MD dependencies. Incomplete mappings cannot pass.
- Add versioned diskinfo health attributes with source/member details and the
  oldest observation timestamp. Missing, stale, unsupported, sleeping, warning
  and failed results remain distinct. No database changes or new SMART tables.
- Fix Linux process RSS, CPU ticks and thread counts when `/proc/PID/stat`
  contains whitespace or parentheses in the process name. Reject truncated,
  malformed and overflowing readings instead of publishing shifted fields.
- Preserve spaces in Linux process names and bound the display-name copy.
- Exclude newer pseudo-filesystems (`cgroup2`, `bpf`, `tracefs`, `efivarfs`,
  `nsfs`) from disk capacity reports. Existing database history is not deleted.
- Add `make check` regression coverage for procfs parsing and filesystem filters.
- Add `software="istatserverlinux"` handshake metadata so new viewers can
  distinguish the maintained implementation without relying on its platform.
- Mark macOS memory pressure as `pressure_unit="level"` in handshake/stat
  metadata. The existing sysctl value is a kernel pressure level, not a percent.
  These optional XML attributes do not change stored history or the DB schema.

## v1.2.0 — 2026-06-02
**“Apple Silicon and Full Telemetry Edition”**

This release brings the maintained fork in line with the telemetry expected by
iStat View-compatible clients on modern Linux systems and Apple Silicon Macs.

### Major Changes
- Added preliminary macOS Apple Silicon support, tested on Mac mini M1.
- Added macOS CPU, memory, uptime, load, task list, network, disk, disk I/O,
  sensors, fan, frequency, power, and GPU telemetry.
- Added macOS memory pressure and additional memory fields used by the classic
  iStat View macOS layout.
- Added APFS-aware disk capacity reporting and disk I/O mapping from BSD disk
  names back to displayed volumes, including multi-disk volumes.
- Added Apple Silicon sensor support from HID/SMC/IOReport/powermetrics/AGX
  sources, including readable labels for Apple temperature channels.
- Added macOS powermetrics helper support for CPU/GPU frequency and power values.
- Added AGX GPU load, renderer/tiler utilization, and unified GPU memory values
  where the system exposes them.
- Added Linux GPU telemetry from sysfs/devfreq/DRM/i915 debugfs when available.
- Added Linux and macOS process/task details for CPU and memory panes.
- Added protocol fields for server model, OS version, memory pressure, GPU data,
  disk metadata, and GPU sensor units.
- Improved persistent TLS polling behavior for modern and classic clients.
- Preserved existing configuration and SQLite history database behavior during
  install/upgrade.

### Version
- Runtime server version bumped to `3.04`.
- Runtime build bumped to `106`.
- Autoconf package version bumped to `3.04`.

### Notes
- GPU memory sensors are now explicitly marked as byte values for newer clients.
  Legacy clients may still interpret unknown sensor type `7` as lux if they do
  not read the new unit hint.
- The native replacement macOS viewer prototype is kept as a separate private
  project and is not part of this server repository.

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
