# iStat Server Control

First milestone: a separate native, read-only installation/status window for
the maintained iStat server. The classic Bjango daemon is detected separately
and is never managed. Closing this app does not stop any server.

Build on macOS with Xcode command-line tools and Python 3 installed:

```sh
make -C control-macos
open control-macos/build/iStatServerControl.app
```

The app uses Homebrew Python 3 when available, then `/usr/bin/python3`.
It bundles the same standard-library-only collector used by the Linux CLI:

```sh
python3 contrib/istat-server-status.py
python3 contrib/istat-server-status.py --prefix /usr/local
python3 tests/test-server-status.py
make -C control-macos test
```

Status JSON has `schema: 1` and `mode: read-only`. It contains allowlisted
installation paths, configuration port/address and credential-presence status,
service identity/state, observed process-owned TCP listening ports, helper
states and health-cache freshness. It contains no passcode, password, TLS key,
configuration contents, command output, logs, client lists or history samples.
It does contain hostname and local paths; review before sharing publicly.

Use File > Choose Installation to inspect a non-default local prefix. Use
File > Export Status Report to save a report, or File > Open Status Report to
review a report obtained separately from a Linux/Mac server. Imported reports
are explicitly marked as saved snapshots; they are not live remote connections.
Command-R refreshes local status, Command-S exports, Command-O opens a report,
and Command-Q quits without altering services.

## Limits

- No installation, sudo, service restart, settings writes, credential reveal,
  raw-log access, network probing or database reads/writes are performed.
- A matching service label alone is not enough: its executable must match the
  selected prefix before any PID/listener association is shown.
- Port/address from the file are not claimed to be active settings. Command-line
  overrides and changes since service launch can differ. Observed ports come
  from `lsof` for that service PID; permission/tool failures remain Not observed.
- A missing or inaccessible configuration is explicit. Unknown keys and invalid
  values are never echoed, including in errors. No server binary is executed to
  obtain its version. Version and Bonjour publication are not yet observed.
- A protected configuration is Permission denied, not a reason to loosen file
  permissions. Authorized settings access is a later backend milestone.
- The helper's Idle state between scheduled runs is normal. Last exit and
  cache age are independent of process presence. A skipped check is not healthy.
- Native administration requires a separate authorized backend and signing
  design. There are deliberately no nonfunctional Apply/Start/Reveal buttons.

Builds currently use ad-hoc signing and are local development builds only.

## Verification

Python regression tests run on both macOS and Linux. They cover config parsing,
credential redaction, absent/denied files, executable mismatches, PID-specific
listener matching, helper freshness and a staged installation whose DB/identity
files remain unread and unchanged. Native tests validate imported report types,
size limits and stripping of unknown fields/capability claims before re-export.
Live unprivileged inventory was checked on both minis, plus the local Mac with
only the classic server installed. Report import, resizing/scrolling and native
keyboard shortcuts are checked in the app without changing server installations.
