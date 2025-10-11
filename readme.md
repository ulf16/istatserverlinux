# iStat Server

iStat Server is a system monitoring daemon that is used in conjunction with [iStat View for iOS](https://bjango.com/ios/istat/) and [iStat View for macOS](https://bjango.com/mac/istat/) to remotely monitor computers.

## 2025 Maintenance & Modernization Update

This fork brings iStatServerLinux up to date for modern Linux systems (and beyond).  
All changes are backward-compatible with the original iStat client app.
### ✅ Tested Platforms
| Platform | OS / Distro | Status |
|-----------|--------------|---------|
| Odroid N2 | Armbian 24.x | ✅ CPU/GPU/SoC sensors working |
| Odroid XU4 | Armbian 24.x | ✅ Thermal + frequency sensors verified |
| Intel Mac Mini | Ubuntu 24.04 | ✅ RAPL + CPU frequency working |
| Generic x86 | Debian/Ubuntu | ✅ Build + cert generation verified |
-----

### Quick Install
```
curl -fsSL https://raw.githubusercontent.com/ulf16/istatserverlinux/master/get-istatserver.sh -o istatserverlinux.sh && sh istatserverlinux.sh
```

Quick install will install and update any required packages. If you do not want packages installed or updated automatically then please perform a manual install using the instructions below

-----

## 2025-10-11 Updates
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

## 2025-10-08 Updates
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

### Install and enable:

`sudo systemctl daemon-reload`

`sudo systemctl enable --now istatserver`


⸻

## Notes
- The applesmc kernel message `applesmc: hwmon_device_register() is deprecated` is harmless and does not affect temperature readings.
- Tested on Intel Mac mini (Debian 12, kernel 6.1) using the istat service user.
- No setcap or root privileges required.

----
## Original Readme cont.

### Supported OSs
- Linux
- FreeBSD, DragonFly BSD, OpenBSD, NetBSD and other BSD based OSs
- AIX
- Solaris
- HP-UX (Still in development and not tested)

-----

### Requirements
- C and C++ compilers such as gcc and g++.
- Auto tools (autoconf and automake).
- OpenSSL/libssl + development libraries.
- sqlite3 + development libraries.
- libxml2 + development libraries.

We have a [package guide available](https://github.com/bjango/istatserverlinux/wiki/Package-Guide) to help you install all the required packages for your OS.

-----

### Building and starting iStat Server
- Pull the latest branch (`master`).
- cd /path/to/istatserver
- ./autogen
- ./configure
- make
- sudo make install
- Test with: sudo /usr/local/bin/istatserver
- Install systemd unit file: sudo cp ./resource/systemd/istatserver.service /etc/systemd/system/istatserver.service
- sudo systemctl restart istatserver


A 5 digit passcode is generated by the install script. It can be found in the preference file, which is generally located at **/usr/local/etc/istatserver/istatserver.conf**. iStat View will ask for this passcode the first time you connect to your computer.

-----

### Upgrading iStat Server
Upgrades follow the same process as standard installs. Please stop istatserver if it is running then run the normal build process.

-----

### Starting iStat Server at boot
iStat Server does not install any scripts to start itself at boot. Sample scripts for rc.d, upstart and systemd are included in the resources directory. You may need to customize them depending on your OS.

### Starting with systemd
- sudo cp ./resource/systemd/istatserver.service  /etc/systemd/system/istatserver.service
- sudo service istatserver start

### Starting with upstart
- sudo cp ./resource/upstart/istatserver.conf  /etc/init/istatserver.conf
- sudo start istatserver

### Starting with rc.d
- sudo cp ./resource/rc.d/istatserver  /etc/rc.d/istatserver
- sudo /etc/rc.d/istatserver start

-----

iStat Server is based on [istatd](https://github.com/tiwilliam/istatd) by William Tisäter.

-----

```
        :::::::::   :::::::     ::::      ::::    :::   ::::::::    ::::::::
       :+:    :+:      :+:    :+: :+:    :+:+:   :+:  :+:    :+:  :+:    :+:
      +:+    +:+      +:+   +:+   +:+   :+:+:+  +:+  +:+         +:+    +:+
     +#++:++#+       +#+  +#++:++#++:  +#+ +:+ +#+  :#:         +#+    +:+
    +#+    +#+      +#+  +#+     +#+  +#+  +#+#+#  +#+   +#+#  +#+    +#+
   #+#    #+#  #+# #+#  #+#     #+#  #+#   #+#+#  #+#    #+#  #+#    #+#
  #########    #####   ###     ###  ###    ####   ########    ########
```
