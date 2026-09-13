#!/usr/bin/env python3
"""Read-only disk health snapshot. Run periodically, outside the network daemon."""
import argparse
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


def run(argv):
    result = subprocess.run(argv, capture_output=True, timeout=15, check=False)
    if len(result.stdout) > 2 * 1024 * 1024:
        raise ValueError("oversized command output")
    return result


def device_name(value):
    return isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,100}", value)


def ancestors(name, graph, seen=None):
    seen = set() if seen is None else seen
    if name in seen or not device_name(name):
        return {'unknown-topology'}
    parents = graph.get(name)
    if parents is None:
        return {name}
    if not parents:
        return {name}
    return set().union(*(ancestors(p, graph, seen | {name}) for p in parents))


def linux_topology(root=Path('/sys/class/block')):
    graph = {}
    for entry in root.iterdir():
        name = entry.name
        if not device_name(name) or name.startswith(('loop', 'ram', 'zram', 'sr')):
            continue
        slaves = [p.name for p in (entry / 'slaves').glob('*')]
        if slaves:
            graph[name] = slaves
        elif (entry / 'partition').exists():
            graph[name] = [entry.resolve().parent.name]
        elif (entry / 'device').exists():
            graph[name] = []
    # /proc/mounts can use /dev/mapper aliases rather than the kernel dm-N name.
    for alias in Path('/dev/mapper').glob('*'):
        target = alias.resolve().name
        if target in graph:
            graph['mapper/' + alias.name] = [target]
    return graph


def mac_topology(listing, raid):
    graph = {}
    def visit(node, parent=None):
        if not isinstance(node, dict):
            return
        name = node.get('DeviceIdentifier') or node.get('SnapshotBSD')
        if device_name(name):
            stores = [s.get('DeviceIdentifier') for s in node.get('APFSPhysicalStores', [])]
            graph[name] = [s for s in stores if device_name(s)] or ([parent] if parent else [])
            parent = name
        for key in ('Partitions', 'APFSVolumes', 'MountedSnapshots'):
            for child in node.get(key, []):
                visit(child, parent)
    for disk in listing.get('AllDisksAndPartitions', []):
        visit(disk)
    for disk in raid.get('AppleRAIDSets', []):
        name = disk.get('BSD Name')
        if device_name(name):
            graph[name] = [m.get('BSD Name') if device_name(m.get('BSD Name')) else 'unknown-raid-member' for m in disk.get('Members', [])]
            if not graph[name] or disk.get('Status', 'Online') != 'Online':
                graph[name].append('unknown-raid-member')
    return graph


def smartctl_health(data, status):
    result = {'state': 'unknown', 'source': 'smartctl'}
    passed = data.get('smart_status', {}).get('passed')
    if status == 99:
        result['state'] = 'standby'
    elif passed is False or status & 8:
        result['state'] = 'failed'
    elif status & 7:
        result['state'] = 'error'
    elif status & 0xF0:
        result['state'] = 'warning'
    elif passed is True:
        result['state'] = 'passed'
    elif data.get('smart_support', {}).get('available') is False:
        result['state'] = 'unsupported'
    result['model'] = str(data.get('model_name') or data.get('product') or '')[:200]
    details = []
    temperature = data.get('temperature', {}).get('current')
    hours = data.get('power_on_time', {}).get('hours')
    if isinstance(temperature, (float, int)):
        details.append(str(temperature) + ' C')
    if isinstance(hours, (float, int)):
        details.append(str(hours) + ' power-on hours')
    result['detail'] = ', '.join(details)
    return result


def collect_device(name, system):
    if name.startswith('unknown-'):
        return {'state': 'unknown', 'detail': 'Incomplete device topology'}
    try:
        if system == 'Darwin':
            result = run(['/usr/sbin/diskutil', 'info', '-plist', '/dev/' + name])
            data = plistlib.loads(result.stdout)
            state = {'Verified': 'passed', 'Failing': 'failed', 'Not Supported': 'unsupported'}.get(data.get('SMARTStatus'), 'unknown')
            return {'state': state, 'source': 'diskutil', 'model': str(data.get('MediaName', ''))[:200], 'detail': ''}
        # Select known native transports to avoid waking disks during autodetection.
        path = str(Path('/sys/class/block', name).resolve())
        if name.startswith('nvme'):
            kind = 'nvme'
        elif '/ata' in path:
            kind = 'ata'
        else:
            return {'state': 'unsupported', 'source': 'smartctl', 'detail': 'Transport is not automatically probed'}
        result = run(['/usr/sbin/smartctl', '-d', kind, '-n', 'standby,99', '-i', '-H', '-A', '-j', '/dev/' + name])
        return smartctl_health(json.loads(result.stdout), result.returncode)
    except FileNotFoundError:
        return {'state': 'unavailable', 'detail': 'Collection tool is not installed'}
    except (subprocess.TimeoutExpired, ValueError, OSError, plistlib.InvalidFileException):
        return {'state': 'error', 'detail': 'Health query failed or timed out'}


def aggregate(devices, records):
    states = [records.get(d, {}).get('state', 'unknown') for d in devices]
    if 'failed' in states:
        return 'failed'
    if 'warning' in states:
        return 'warning'
    if states and all(s == 'passed' for s in states):
        return 'passed'
    return states[0] if states and len(set(states)) == 1 else 'unknown'


def snapshot(graph, records):
    root = ET.Element('health', version='1')
    for name, record in sorted(records.items()):
        ET.SubElement(root, 'device', bsd=name, **record)
    for name in sorted(graph):
        # Mapper paths are aliases, not physical device names.
        devices = sorted(ancestors(name, graph)) if not name.startswith('mapper/') else sorted(ancestors(graph[name][0], graph))
        checked = min((int(records[d]['checked']) for d in devices if d in records), default=0)
        details = '; '.join(d + ': ' + records.get(d, {}).get('state', 'unknown') +
                            (' (' + records[d].get('model', '') + ')' if records.get(d, {}).get('model') else '') +
                            (' - ' + records[d]['detail'] if records.get(d, {}).get('detail') else '') +
                            ' [' + records.get(d, {}).get('source', 'unknown source') + ']' for d in devices)
        ET.SubElement(root, 'volume', bsd=name, devices=','.join(devices), state=aggregate(devices, records), checked=str(checked), detail=details[:4000])
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='/var/run/istatserver-smart/status.xml')
    args = parser.parse_args()
    system = platform.system()
    if system == 'Darwin':
        listing = plistlib.loads(run(['/usr/sbin/diskutil', 'list', '-plist']).stdout)
        raid = plistlib.loads(run(['/usr/sbin/diskutil', 'appleRAID', 'list', '-plist']).stdout)
        graph = mac_topology(listing, raid)
    else:
        graph = linux_topology()
    physical = sorted(set().union(*(ancestors(n, graph) for n in graph)))
    if len(physical) > 64:
        raise ValueError('Too many devices for one health snapshot')
    records = {}
    for name in physical:
        records[name] = collect_device(name, system)
        records[name]['checked'] = str(int(time.time()))
    target = Path(args.output)
    # Installer owns this directory as root, never the network daemon.
    target.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.health-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(snapshot(graph, records))
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), 0o644)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == '__main__':
    main()
