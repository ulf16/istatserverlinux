# iStat Server

iStat Server is a remote system monitoring daemon for iStat View-compatible
clients. This maintained fork keeps the original protocol usable on modern
Linux systems and adds preliminary macOS Apple Silicon support.

The original project was released by [Bjango](https://github.com/bjango/istatserverlinux)
and is based on [istatd](https://github.com/tiwilliam/istatd) by William
Tisater. This fork is maintained at:

https://github.com/ulf16/istatserverlinux

## 2026 Maintenance Update

Release `v1.2.0` brings the fork in line with the telemetry expected by
iStat View-compatible clients on modern Linux systems and Apple Silicon Macs.

### Highlights

- Modern Autotools build for current GCC, Clang, pkg-config, OpenSSL, SQLite,
  and libxml2 environments.
- Hardened Linux install path with safer config permissions and optional
  systemd service installation.
- Bonjour/Avahi discovery where available.
- Linux CPU, memory, load, uptime, disk, disk I/O, network, process, sensor,
  power, frequency, and GPU telemetry.
- Preliminary macOS Apple Silicon support, tested on Mac mini M1.
- macOS memory pressure, APFS-aware disk space, disk I/O mapping, task lists,
  Apple Silicon sensor names, fan speed, power, frequency, and AGX GPU counters.
- Protocol extensions for server model, OS version, memory pressure, GPU data,
  disk metadata, task lists, and sensor units.
- Install and upgrade paths preserve existing configuration and SQLite history
  databases.

## Supported Architectures

| Platform | Architecture | Example Device | Status |
| --- | --- | --- | --- |
| Linux | x86_64 | Intel/AMD servers, Intel Mac mini | Stable |
| Linux | aarch64 | Odroid N2, Raspberry Pi 4 64-bit | Stable |
| Linux | armv7l | Odroid XU4, older Raspberry Pi systems | Stable |
| macOS | arm64 | Apple Silicon Mac mini | Preliminary |
| BSD/AIX/Solaris/HP-UX | varies | legacy supported targets | Not recently tested |

## Current Telemetry

Linux:

- CPU usage, task list, load, uptime, memory, swap, and process memory
- Disk capacity and disk I/O
- Network interface throughput
- lm_sensors temperature and fan sensors
- CPU frequency through cpufreq
- Intel RAPL CPU package/core/uncore power
- GPU telemetry where exposed through sysfs, DRM, devfreq, or i915 debugfs

Process memory is resident bytes (RSS pages times the runtime page size), not
virtual address space. The procfs parser handles spaces and parentheses in
process names. Kernel pseudo-filesystems are excluded from disk capacity
reports without deleting their previously stored history.

Run `make check` after configuring/building for collector regression tests.
They cover field boundaries, malformed readings and filesystem filtering on
both Linux and macOS without accessing the production database.

macOS Apple Silicon:

- CPU usage, task list, load, uptime, memory, swap, and memory pressure
- Disk capacity using APFS-aware volume accounting
- Disk I/O mapped back to displayed APFS volumes, including multi-disk volumes
- Network interface throughput
- Apple HID temperature sensors with readable Apple Silicon names
- Fan speed through SMC-compatible interfaces
- CPU/GPU/ANE/RAM/PCIe power where available through IOReport/powermetrics
- CPU and GPU frequency through the powermetrics helper
- AGX GPU load, renderer/tiler utilization, and unified GPU memory counters

## Quick Install

### Native Server Control (Preview)

`control-macos` contains a separate native, read-only status app for the
maintained server. It shows installation/service identity, file settings where
readable, observed listening ports, helper status and health-cache freshness.
It detects the classic Mac server separately and never manages it.
The charcoal/cyan dashboard has Modern/Classic views and compact Overview,
Connection and Diagnostics tabs. Classic runtime detection verifies launchd's
executable path rather than assuming an installed app is running. A local
Open Classic Server action opens the original settings app; saved reports
cannot invoke it. Older status reports remain supported.

```sh
make -C control-macos
open control-macos/build/iStatServerControl.app
```

The same Python 3 inventory works on Linux and macOS:

```sh
python3 contrib/istat-server-status.py
python3 contrib/istat-server-status.py --prefix /usr/local
```

No credentials, raw configuration, logs or history are included in its JSON
reports. File > Open Status Report can display a separately obtained report as
a saved snapshot, not a live connection. Protected settings remain explicitly
unavailable without permission; the app never invokes sudo. Settings editing,
credential reveal and service control require a future authorized backend.
See [control-macos/README.md](control-macos/README.md) for build requirements,
commands, report contents and current limitations.

### Optional Disk Health

The new viewer can display real SMART health in its Disks pane. Install the
helper separately after building the server:

```sh
# Linux: Python 3 and smartmontools are required.
sudo apt-get install smartmontools
sudo sh contrib/install-smart-helper.sh /usr/local

# Apple Silicon macOS: Homebrew Python 3 and native diskutil are used.
sudo sh contrib/install-smart-helper.sh /opt/istatserverlinux
```

The root helper runs every five minutes, outside the network daemon, and writes
`/var/run/istatserver-smart/status.xml` atomically. Each command has a 15-second
timeout. The server refreshes that small cache at most every 30 seconds; viewers
request disk details at most once per minute. Observations expire after 15
minutes. No self-tests, SMART settings changes, or raw disk permissions for the
network daemon are involved. No database schema or history is changed.

Linux supports native ATA and NVMe devices and resolves partition, MD and
device-mapper parents. Other transports, including USB bridges, are deliberately
not auto-probed; `smartctl` warns that transport autodetection can wake a drive.
ATA checks use `-n standby,99,98`: standby/sleep returns a skipped result, and an
unknown or unsupported power-mode check is also skipped, without an unguarded
retry. NVMe has no rotating medium and is queried with its explicit transport.

macOS discovers APFS snapshots and AppleRAID members from cached IORegistry
properties, not `diskutil list` or `diskutil appleRAID list`. Only positively
identified internal native SSDs (Apple Fabric/PCI-Express) receive a targeted
`diskutil info` health query. HDDs, external devices and uncertain media are
reported as unavailable with a disk-sleep-policy explanation, even if awake.
No numeric IORegistry power state is guessed to mean standby. This conservative
policy sacrifices HDD health reporting because a reliable no-wake guard for
native macOS health queries has not been established.

A volume passes only when every mapped member reports success. Diskutil exposes
platform-reported status, not a new surface scan or a guarantee against failure.
Skipped observations do not carry forward an old pass with a new timestamp.
Discovery failures leave the previous cache to expire normally. These safeguards
limit this helper's queries; they cannot guarantee that the OS, other software,
or drive firmware will never wake a disk. No self-tests or forced disk-sleep
experiments are performed during installation or testing.

The additive `diskinfo` fields are `health_version="1"`, `health_state`,
`health_devices`, `health_checked` (Unix seconds), and `health_detail`. Existing
identity-only `smart` records and classic-client fields retain their meanings.
Unrecognized classic health encodings are not guessed by the new viewer.

Run `python3 tests/test-smart-helper.py` and `make check` for helper, topology,
cache safety/freshness and collector tests. On Linux, inspect
`systemctl status istat-smart.timer istat-smart.service`; on macOS use
`sudo launchctl print system/com.istat.smart.helper` (a successful periodic
helper is normally not running between observations).

Reference: [smartctl options and exit-status semantics](https://github.com/smartmontools/smartmontools/blob/main/src/smartctl.8.in).

### Build and Install

```sh
curl -fsSL https://raw.githubusercontent.com/ulf16/istatserverlinux/master/get-istatserver.sh -o istatserverlinux.sh
sh istatserverlinux.sh
```

The quick installer is designed for Linux distributions using systemd. It
installs required packages, builds the daemon, installs it, and enables the
`istatserver` service.

On non-systemd systems, the build can still succeed, but service installation is
skipped. Run the daemon manually or add a local init script:

```sh
sudo -u istat /usr/local/bin/istatserver
```

## Requirements

- C and C++ compilers such as gcc, g++, or clang
- Autoconf, automake, libtool, and pkg-config/pkgconf
- OpenSSL/libssl plus development headers
- SQLite3 plus development headers
- libxml2 plus development headers

Optional libraries:

- libavahi plus development headers for Bonjour discovery on Linux
- lm_sensors/libsensors4 plus development headers for Linux sensors

On macOS, Homebrew packages are sufficient for the build dependencies. The
Apple Silicon helper installed by this fork is used for powermetrics-derived
frequency and power values.

## Build And Install

```sh
git clone https://github.com/ulf16/istatserverlinux.git
cd istatserverlinux
./autogen
./configure
make
sudo make install
sudo /usr/local/bin/istatserver -d
```

A 5 digit passcode is generated by the install script. It can be found in:

```text
/usr/local/etc/istatserver/istatserver.conf
```

iStat View-compatible clients ask for this passcode the first time they connect.

## Upgrading

Stop the running service, rebuild, and install normally:

```sh
sudo service istatserver stop
git pull
./autogen
./configure
make
sudo make install
sudo service istatserver start
```

The installer preserves the existing configuration and SQLite history database.
Do not remove `/usr/local/etc/istatserver/istatserver.db` if you want to keep
historical graph data.

## macOS Apple Silicon Helper

Apple's `powermetrics` tool requires elevated privileges. This fork ships a
small LaunchDaemon helper that samples powermetrics and writes a plain key/value
file read by the unprivileged server process.

After installing the server under `/opt/istatserverlinux`, install the helper:

```sh
sudo /opt/istatserverlinux/contrib/install-macos-helper.sh
```

To remove it:

```sh
sudo /opt/istatserverlinux/contrib/uninstall-macos-helper.sh
```

## Starting With systemd

The build can install the systemd unit directly:

```sh
./configure --enable-systemd-unit
make
sudo make install
sudo systemctl enable --now istatserver
```

You can also install the unit manually:

```sh
sudo cp ./resource/systemd/istatserver.service /etc/systemd/system/istatserver.service
sudo systemctl daemon-reload
sudo systemctl enable --now istatserver
```

## Starting With upstart

```sh
sudo cp ./resource/upstart/istatserver.conf /etc/init/istatserver.conf
sudo start istatserver
```

## Starting With rc.d

```sh
sudo cp ./resource/rc.d/istatserver /etc/rc.d/istatserver
sudo /etc/rc.d/istatserver start
```

## Security Notes

- The service runs as a dedicated `istat` user where supported.
- Configuration and database files live in `/usr/local/etc/istatserver/`.
- The systemd unit runs the process in the foreground under systemd supervision.
- Linux RAPL power readings can be exposed to the `istat` user with a narrow
  udev rule instead of running the daemon as root.
- Self-signed certificates use RSA-2048 with SHA-256 through OpenSSL's modern
  EVP API and are stored in:

```text
/usr/local/etc/istatserver/key.pem
/usr/local/etc/istatserver/cert.pem
```

To expose Intel RAPL readings without root, install a udev rule such as:

```text
SUBSYSTEM=="powercap", KERNEL=="intel-rapl:*", TEST=="%S%p/energy_uj", GROUP="istat", MODE="0440"
```

Then reload and apply it:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=powercap
```

## Notes

The native replacement macOS viewer prototype is kept in a separate private
repository. This repository contains the server and server-side helper assets
only.

```text
        :::::::::   :::::::     ::::      ::::    :::   ::::::::    ::::::::
       :+:    :+:      :+:    :+: :+:    :+:+:   :+:  :+:    :+:  :+:    :+:
      +:+    +:+      +:+   +:+   +:+   :+:+:+  +:+  +:+         +:+    +:+
     +#++:++#+       +#+  +#++:++#++:  +#+ +:+ +#+  :#:         +#+    +:+
    +#+    +#+      +#+  +#+     +#+  +#+  +#+#+#  +#+   +#+#  +#+    +#+
   #+#    #+#  #+# #+#  #+#     #+#  #+#   #+#+#  #+#    #+#  #+#    #+#
  #########    #####   ###     ###  ###    ####   ########    ########
```
