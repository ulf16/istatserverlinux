# iStat Server Control

Version 0.2: a separate native, read-only installation/status window for
the maintained iStat server. The classic Bjango daemon is detected separately
and is never managed. Closing this app does not stop any server.

The dashboard uses a charcoal/cyan status header with separate Modern Server
and Classic Server views, plus Overview, Connection and Diagnostics tabs.
Details scroll independently of the header and footer when the window is small.
On macOS, classic runtime detection checks launchd and its exact daemon path;
an installed file alone never means Running. The public settings-app version
is shown separately from the daemon. Open Classic Server opens the original
local settings app, without reading or changing its configuration. This action
is disabled when viewing saved reports.

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
The optional `legacy` section adds classic installation/runtime observations.
Older schema-1 reports without it remain readable and show Not observed.

Use File > Choose Installation to inspect a non-default local prefix. Use
File > Export Status Report to save a report, or File > Open Status Report to
review a report obtained separately from a Linux/Mac server. Imported reports
are explicitly marked as saved snapshots; they are not live remote connections.
Command-R refreshes local status, Command-S exports, Command-O opens a report,
and Command-Q quits without altering services.

## Limits

- No server installation, sudo invocation, service restart, settings writes,
  raw-log access, network probing or database reads/writes are performed.
  An optional, separately approved settings reader can read protected settings
  and reveal a credential on explicit request; see below.
- A matching service label alone is not enough: its executable must match the
  selected prefix before any PID/listener association is shown.
- Port/address from the file are not claimed to be active settings. Command-line
  overrides and changes since service launch can differ. Observed ports come
  from `lsof` for that service PID; permission/tool failures remain Not observed.
- A missing or inaccessible configuration is explicit. Unknown keys and invalid
  values are never echoed, including in errors. No server binary is executed to
  obtain its version. Maintained-daemon version and Bonjour publication are
  not yet observed. The classic version comes only from its public app plist.
- Ordinary inventory still reports protected configuration as Permission denied.
  Protected reads use the separate authorization path, never looser permissions.
- The helper's Idle state between scheduled runs is normal. Last exit and
  cache age are independent of process presence. A skipped check is not healthy.
- Settings editing and server Start/Stop/Restart are not implemented.

## Protected Settings Reader

This is **local access**, not remote administration. Install Server Control on
the same Mac as the maintained server. Imported reports and Classic Server
cannot request privileged reads. A separate root helper, registered only through
an explicit Enable confirmation, exposes one signed XPC method: read a known
installation's settings, optionally including its credential. There is no shell,
arbitrary path, command, service-control, write or database operation.

Requirements and activation:

1. macOS 13 or later for protected access. Basic status still supports macOS 11.
2. Sign both app and helper with the same Apple-issued signing team and hardened
   runtime, without the debug `get-task-allow` entitlement. Ad-hoc builds cannot
   enable protected access. Force a rebuild when changing signing identities:
   `make -B -C control-macos all SIGN_IDENTITY="<signing identity>"`.
3. Use a Developer ID signed, timestamped and notarized app for distribution.
   Set `SIGN_TIMESTAMP=--timestamp` for that build, then notarize/staple through
   your release workflow. The current development build is not notarized;
   development signing alone does not prove deployment eligibility on other Macs.
4. Install the app directly in `/Applications`. Do not enable the helper from a
   mutable build checkout. In Modern Server > Connection, choose Enable Protected
   Access and approve it in System Settings > Login Items & Extensions if asked.
   While approval is pending, the app shows Waiting for approval and observes
   status once per second. Open Approval Settings returns to macOS without
   registering again. There is no approval timeout, automatic settings read or
   repeated authorization prompt. Direct file access remains protected normally.
5. Read Settings shows configured port/address and authentication mode. Reveal
   and Copy each make a separate deliberate request. Authorization Services
   decides whether another prompt is needed; the app stores no authorization.

File > Disable Protected Access unregisters just this settings reader. It never
stops the iStat server or its collection helpers. Before replacing a registered
app/helper, disable protected access, quit, replace the entire signed bundle and
enable again. Avoid rebuilding a registered bundle in place.

The helper validates the peer's exact bundle identifier and matching signing
team; the client validates the helper likewise. Each request requires an OS
`system.privilege.admin` authorization, checked without granting new rights or
displaying UI in the helper. Denial/cancellation does not cause an automatic
retry. Requests use a fresh authorization context, destroyed at completion, and
XPC responses time out after 15 seconds. Command-Q and window close remain usable.

Only `opt` (`/opt/istatserverlinux`) and `local` (`/usr/local`) installation IDs
are accepted. The reader opens `etc/istatserver/istatserver.conf` through directory
descriptors without following symlinks. Ancestors must be root-owned; only the
last `istatserver` directory and configuration may instead belong to `istat`, as
in our installers. Group/world-writable paths, hard-linked files, nonregular or
oversized inputs are rejected. It never reads generated identity files, TLS
keys, logs or databases, and never changes ownership/permissions to gain access.

Settings are file values, not verified running options. Bonjour remains Not
observed. The authentication mode matches the current daemon's parsing: five
digits with no leading zero are a passcode; other strings are passwords. An
absent credential reports the daemon's default explicitly, rather than pretending
it is a configured secret. Unknown/malformed input is never echoed in errors.

Protected replies never enter the status report, exports or preferences. Reveal
is hidden after 30 seconds, on deactivation, navigation, window close or quit.
Build 5 preserves already-read non-secret metadata across focus changes, including
the macOS authorization dialog. If an approved Reveal/Copy reply arrives before
the app regains focus, it waits in memory for up to 30 seconds, then is discarded.
Delivery requires the original connection pane and a visible, active key window;
it happens only once, without another read or authorization prompt. Navigation,
refresh, window close and quit clear pending delivery and settings metadata.
The helper status distinguishes Ready, an in-progress request, pending delivery,
and Settings read successfully; helper approval is not a lasting authorization.
Copy marks the clipboard concealed/transient and clears it after 30 seconds or
normal quit, only if the clipboard has not subsequently changed. Clipboard
managers may ignore these markers and force-quit cannot run cleanup: copying is
an explicit disclosure to the system clipboard, not a guarantee against retention.

The same C++ settings parser/reader builds on Linux without AppKit:

```sh
c++ -std=c++11 contrib/settings/main.cpp contrib/settings/Settings.cpp -o /tmp/istat-settings-read
sudo /tmp/istat-settings-read local
```

That command returns metadata only. Add `--reveal` only when deliberately viewing
the credential in a private terminal; never redirect that output into logs or
reports. The CLI requires existing administrator access; do not make it setuid
or add a broad passwordless sudoers exception. It does not install any service.

Apple references: [SMAppService](https://developer.apple.com/documentation/servicemanagement/smappservice)
and [XPC peer signature requirements](https://developer.apple.com/documentation/foundation/nsxpcconnection/setcodesigningrequirement(_:)).

## Verification

Python regression tests run on both macOS and Linux. They cover config parsing,
credential redaction, absent/denied files, executable mismatches, PID-specific
listener matching, helper freshness and a staged installation whose DB/identity
files remain unread and unchanged. Native tests validate imported report types,
size limits and stripping of unknown fields/capability claims before re-export.
Live unprivileged inventory was checked on both minis, plus the local Mac with
only the classic server installed. Report import, resizing/scrolling and native
keyboard shortcuts are checked in the app without changing server installations.

Settings tests cover parsing/defaults, redaction, fixed targets, permissions,
symlink/hard-link/FIFO rejection, unchanged fixture DB metadata, malformed replies,
unsigned peers, forged tokens and valid sessions lacking administrator rights.
Build 4 corrects an invalid signature-requirement expression that caused build 3
to throw after authorization. Both connection ends now reject setup failures
without crashing or continuing with an unrestricted connection. Tests exercise
the actual requirement compiler/setter and approval-state transitions.
For signed, bidirectional XPC tests (including wrong identity/team rejection),
run `make -C control-macos test-signed SIGN_IDENTITY="<signing identity>"`.
These tests use an anonymous local listener and no administrator authorization,
server configuration or credentials. Run outside a restrictive process sandbox.
Headless delegate tests use synthetic credentials and a stub client to cover
authorization focus handoff, one-shot reveal, cancellation, expiry, navigation
and late replies, without touching the clipboard or any real server settings.
The OS authorization test runs from a disposable `/private/tmp` directory because
authd may be unable to inspect test executables inside a Documents checkout.
Credential-free protected reads were verified on both minis via their existing
administrator access. Full SMAppService approval, authenticated XPC success and
interactive Reveal/Copy remain deployment acceptance checks; the helper has not
been enabled on either production system by these tests.
