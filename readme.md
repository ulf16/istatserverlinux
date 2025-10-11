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

Quick install will install and update any required packages. 
The installer is designed for **Linux** distributions using **systemd**.
It automatically builds, installs, and enables the istatserver service.

On **non-systemd** systems (e.g., Alpine with OpenRC, Devuan, or BSDs),
the build will still succeed, but service installation will be skipped.
You can then run the daemon manually:
```
sudo -u istat /usr/local/bin/istatserver
```
or create a small init script if you prefer to start it automatically.
If you do not want packages installed or updated automatically then please perform a manual install using the instructions below

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

### Supported OSs
- Linux (updated)
  
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

### Security Notes

- **Least privilege:**
    
    The service runs as a dedicated user istat (not root).
    
    Configuration and database files live in /usr/local/etc/istatserver/
    
    and are owned by istat:istat.
    
- **Foreground service:**
    
    The included systemd unit uses Type=simple (no -d), meaning the process stays
    
    in the foreground under systemd’s direct supervision.
    
    This ensures clean restarts, proper logging (journalctl), and avoids PID-file issues.
    
- **No elevated privileges required:**
    
    Power (RAPL) readings are made accessible to the istat user through a udev rule
    
    that adjusts permissions on the relevant sysfs files.
    
    By default, the installer applies a world-readable rule:
    

```
SUBSYSTEM=="powercap", KERNEL=="intel-rapl:*", TEST=="%S%p/energy_uj", RUN+="/bin/chmod 0444 %S%p/energy_uj"
```

- For tighter control, you can restrict access to the istat group instead:
    

```
SUBSYSTEM=="powercap", KERNEL=="intel-rapl:*", TEST=="%S%p/energy_uj", GROUP="istat", MODE="0440"
```

- Then reload and apply the rule:
    

```
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=powercap
```

-   
    
- **Process hardening:**
    
    The systemd unit enforces several security directives:
    
    NoNewPrivileges, PrivateTmp, and multiple Protect*= options to reduce
    
    the attack surface. You can tighten them further (for example
    
    ProtectSystem=strict, ReadWritePaths=/usr/local/etc/istatserver)
    
    depending on your setup.
    
- **Certificates and keys:**
    
    Self-signed certificates use RSA-2048 with SHA-256 and are generated through
    
    OpenSSL’s modern EVP API. They are stored in:
    

```
/usr/local/etc/istatserver/key.pem
/usr/local/etc/istatserver/cert.pem
```

- Make sure these files are **not world-writable** and owned by the istat user.
    
    You can safely replace them with your own certs using the same file paths.
    
- **Network exposure:**
    
    istatserver listens on all interfaces by default.
    
    To restrict access, configure network_addr and network_port
    
    in /usr/local/etc/istatserver/istatserver.conf,
    
    or use a local firewall rule to limit visibility.
    
- **Database and logging:**
    
    Historical data is stored in SQLite under /usr/local/etc/istatserver/.
    
    On devices with flash storage (like SBCs), consider placing this directory
    
    on a more durable drive, tmpfs, or using periodic syncs to reduce write wear.
    

---


### Starting iStat Server at boot
iStat Server does not install any scripts to start itself at boot. Sample scripts for rc.d, upstart and systemd are included in the resources directory. You may need to customize them depending on your OS.

### Starting with systemd
- sudo cp ./resource/systemd/istatserver.service  /etc/systemd/system/istatserver.service
- sudo service istatserver start

### Starting with upstart (outdated)
- sudo cp ./resource/upstart/istatserver.conf  /etc/init/istatserver.conf
- sudo start istatserver

### Starting with rc.d (outdated)
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
