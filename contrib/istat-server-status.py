#!/usr/bin/env python3
"""Unprivileged local installation inventory. JSON output never contains secrets."""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import platform
import re
import shutil
import socket
import stat
import subprocess
import time
import xml.etree.ElementTree as ET


def read_small(path, limit=262144):
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError('Not a regular file')
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Oversized input')
    return data


def command(argv):
    try:
        result = subprocess.run(argv, capture_output=True, timeout=3, check=False,
                                env={'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'LC_ALL': 'C'})
        if len(result.stdout) > 262144:
            return None
        return result.stdout.decode('utf-8', errors='replace') if result.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def configuration(path):
    result = {'state': 'unavailable', 'port': None, 'address': None, 'pairing': 'unknown'}
    try:
        lines = read_small(path).decode('utf-8').splitlines()
    except FileNotFoundError:
        result['state'] = 'missing'
        return result
    except PermissionError:
        result['state'] = 'permission-denied'
        return result
    except (OSError, ValueError):
        return result
    # Match Config::get's first occurrence and '#' comment rules, not shell syntax.
    values = {}
    for line in lines:
        parts = line.split('#', 1)[0].strip().split(None, 1)
        if len(parts) != 2:
            continue
        key, value = parts
        if key in ('network_port', 'network_addr'):
            values.setdefault(key, value.strip())
        elif key == 'server_code' and result['pairing'] == 'unknown':
            result['pairing'] = 'configured'  # Never retain or emit the credential.
    result['state'] = 'readable'
    if result['pairing'] == 'unknown':
        result['pairing'] = 'not-configured'
    port = values.get('network_port', '5109')
    if re.fullmatch(r'[0-9]{1,5}', port) and 1 <= int(port) <= 65535:
        result['port'] = int(port)
    else:
        result['state'] = 'invalid'
    try:
        result['address'] = str(ipaddress.ip_address(values.get('network_addr', '0.0.0.0')))
    except ValueError:
        result['state'] = 'invalid'
    return result


def service(system, label, binary=None, run=command):
    result = {'label': label, 'state': 'unavailable', 'pid': None,
              'identity': 'unverified', 'last_exit': None}
    if system == 'Darwin':
        output = run(['/bin/launchctl', 'print', 'system/' + label])
        if output is None:
            return result
        # Top-level fields only: nested environment values cannot impersonate state.
        fields = dict(re.findall(r'^\t([a-z ]+) = ([^\n]+)$', output, re.M))
        state = fields.get('state')
        result['state'] = {'running': 'running', 'not running': 'idle', 'waiting': 'waiting'}.get(state, 'unknown')
        pid = fields.get('pid', '')
        code = fields.get('last exit code', '')
        program = fields.get('program', '')
    else:
        output = run(['/usr/bin/systemctl', 'show', label, '--no-pager',
                      '-p', 'LoadState', '-p', 'ActiveState', '-p', 'MainPID',
                      '-p', 'ExecStart', '-p', 'ExecMainStatus'])
        if output is None:
            return result
        fields = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
        if fields.get('LoadState') == 'not-found':
            result['state'] = 'not-installed'
            return result
        result['state'] = {'active': 'running', 'inactive': 'idle', 'failed': 'failed',
                           'activating': 'starting', 'deactivating': 'stopping'}.get(fields.get('ActiveState'), 'unknown')
        if label.endswith('.timer') and result['state'] == 'running':
            result['state'] = 'scheduled'
        pid = fields.get('MainPID', '')
        code = fields.get('ExecMainStatus', '')
        match = re.search(r'(?:^|\{ )path=([^;]+?)\s*;', fields.get('ExecStart', ''))
        program = match.group(1) if match else ''
    if re.fullmatch(r'[0-9]{1,10}', pid) and int(pid) > 0:
        result['pid'] = int(pid)
    if re.fullmatch(r'-?[0-9]{1,10}', code):
        result['last_exit'] = int(code)
    if binary is not None:
        result['identity'] = 'matched' if program == str(binary) else 'mismatch'
        if result['identity'] != 'matched':
            result['state'] = 'identity-mismatch'
            result['pid'] = None
    return result


def listeners(pid, run=command):
    result = {'state': 'not-observed', 'ports': []}
    tool = shutil.which('lsof', path='/usr/sbin:/usr/bin:/sbin:/bin')
    if not pid or not tool:
        return result
    output = run([tool, '-nP', '-a', '-p', str(pid), '-iTCP', '-sTCP:LISTEN', '-Fpn'])
    if output is None:
        return result
    owner = None
    ports = set()
    for line in output.splitlines():
        if re.fullmatch(r'p[0-9]+', line):
            owner = int(line[1:])
        elif line.startswith('n') and owner == pid:
            match = re.search(r':([0-9]{1,5})$', line)
            if match and 1 <= int(match.group(1)) <= 65535:
                ports.add(int(match.group(1)))
    result['ports'] = sorted(ports)
    result['state'] = 'observed' if ports else 'not-observed'
    return result


def health_cache(path, now):
    result = {'state': 'unavailable', 'checked': None, 'devices': 0, 'skipped': 0}
    try:
        data = read_small(path)
        if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():
            return result
        root = ET.fromstring(data)
        if root.tag != 'health' or root.get('version') != '1':
            return result
        devices = root.findall('device')
        if not devices or len(devices) > 64:
            return result
        times = [int(d.get('checked', '0')) for d in devices]
        result['checked'] = min(times)
        result['devices'] = len(devices)
        result['skipped'] = sum(d.get('state') in ('standby', 'unavailable') for d in devices)
        result['state'] = 'fresh' if all(0 < t <= now + 60 and now - t <= 900 for t in times) else 'stale'
    except (OSError, ValueError, ET.ParseError):
        pass
    return result


def inventory(prefix=None, system=None, run=command, root=Path('/'), now=None):
    system = system or platform.system()
    now = int(time.time()) if now is None else now
    prefix = Path(prefix or ('/opt/istatserverlinux' if system == 'Darwin' else '/usr/local'))
    binary = prefix / 'bin/istatserver'
    config = prefix / 'etc/istatserver/istatserver.conf'
    actual = lambda path: root / str(path).lstrip('/')
    installed = actual(binary).is_file()
    label = 'com.istat.server' if system == 'Darwin' else 'istatserver.service'
    main = service(system, label, binary, run)
    if not installed and main['state'] == 'unavailable':
        main['state'] = 'not-installed'
    helper_labels = (['com.istat.smart.helper', 'com.istat.powermetrics.helper'] if system == 'Darwin'
                     else ['istat-smart.service', 'istat-smart.timer'])
    return {
        'schema': 1, 'mode': 'read-only', 'checked': now,
        'host': socket.gethostname(), 'platform': system,
        'capabilities': {'status': True, 'settings_write': False, 'service_control': False,
                         'credential_read': False, 'logs': False},
        'installation': {'prefix': str(prefix), 'binary': str(binary), 'configuration': str(config),
                         'installed': installed, 'version': None,
                         'classic_present': system == 'Darwin' and
                         actual('/Library/Application Support/iStat Server/iStatServerDaemon').is_file()},
        'service': main,
        'configuration': configuration(actual(config)),
        'listeners': listeners(main['pid'], run) if main['identity'] == 'matched' else {'state': 'not-observed', 'ports': []},
        'bonjour': 'not-observed',
        'helpers': [service(system, label, run=run) for label in helper_labels],
        'health': health_cache(actual('/var/run/istatserver-smart/status.xml'), now),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, help='Installation prefix to inspect (never executed)')
    args = parser.parse_args()
    if args.prefix and (not args.prefix.is_absolute() or '..' in args.prefix.parts):
        parser.error('prefix must be an absolute path without parent traversal')
    print(json.dumps(inventory(args.prefix), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
