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


def mac_registry_topology(roots):
    """Read cached registry properties, never use diskutil for disk discovery."""
    graph = {}
    traits = {}
    raid_counts = {}
    def visit(node, characteristics, parent=None, physical=None, uncertain=False, raid_count=None):
        if not isinstance(node, dict):
            return
        if 'AppleRAID-SetStatus' in node:
            uncertain |= node['AppleRAID-SetStatus'] != 'Online'
            members = node.get('AppleRAID-Members')
            raid_count = len(members) if isinstance(members, list) and members else None
            uncertain |= raid_count is None
        name = node.get('BSD Name')
        if device_name(name):
            if physical is None:
                physical = name
                candidate = dict(characteristics)
                candidate['query_safe'] &= node.get('Whole') is True and not uncertain
                previous = traits.get(name)
                if previous is not None and previous != candidate:
                    candidate['query_safe'] = False
                traits[name] = candidate
            parents = graph.setdefault(name, set())
            if parent and parent != name:
                parents.add(parent)
            if uncertain:
                parents.add('unknown-topology')
            if raid_count is not None:
                raid_counts[name] = raid_count
            parent = name
        for child in node.get('IORegistryEntryChildren', []):
            visit(child, characteristics, parent, physical, uncertain, raid_count)
    for root in roots:
        device = root.get('Device Characteristics', {})
        protocol = root.get('Protocol Characteristics', {})
        visit(root, {
            'model': str(device.get('Product Name', '')).strip()[:200],
            'query_safe': device.get('Medium Type') == 'Solid State' and
                          protocol.get('Physical Interconnect Location') == 'Internal' and
                          protocol.get('Physical Interconnect') in ('Apple Fabric', 'PCI-Express'),
        })
    # A degraded or incompletely discovered RAID must never inherit one member's pass.
    for name, count in raid_counts.items():
        if len(ancestors(name, graph)) != count:
            graph[name].add('unknown-raid-member')
    return graph, traits


def smartctl_health(data, status):
    result = {'state': 'unknown', 'source': 'smartctl'}
    passed = data.get('smart_status', {}).get('passed')
    if status == 99:
        result['state'] = 'standby'
        result['detail'] = 'Skipped: drive is in standby or sleep'
        return result
    elif status == 98:
        result['state'] = 'unavailable'
        result['detail'] = 'Skipped: drive power state cannot be established'
        return result
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


def collect_device(name, system, traits=None):
    if not device_name(name) or name.startswith('unknown-'):
        return {'state': 'unknown', 'detail': 'Incomplete device topology'}
    try:
        if system == 'Darwin':
            identity = (traits or {}).get(name, {})
            if not identity.get('query_safe'):
                return {'state': 'unavailable', 'source': 'IORegistry',
                        'model': identity.get('model', ''),
                        'detail': 'Skipped by disk-sleep policy: only identified internal native SSDs are queried'}
            result = run(['/usr/sbin/diskutil', 'info', '-plist', '/dev/' + name])
            if result.returncode != 0:
                raise ValueError('diskutil failed')
            data = plistlib.loads(result.stdout)
            state = {'Verified': 'passed', 'Failing': 'failed', 'Not Supported': 'unsupported'}.get(data.get('SMARTStatus'), 'unknown')
            return {'state': state, 'source': 'diskutil', 'model': str(data.get('MediaName', ''))[:200], 'detail': ''}
        # Select known native transports to avoid waking disks during autodetection.
        path = str(Path('/sys/class/block', name).resolve())
        if name.startswith('nvme'):
            kind = 'nvme'
        elif re.search(r'/ata[0-9]+/', path):
            kind = 'ata'
        else:
            return {'state': 'unsupported', 'source': 'smartctl', 'detail': 'Transport is not automatically probed'}
        guard = ['-n', 'standby,99,98'] if kind == 'ata' else []
        result = run(['/usr/sbin/smartctl', '-d', kind] + guard + ['-i', '-H', '-A', '-j', '/dev/' + name])
        if kind == 'ata' and result.returncode in (98, 99):
            return smartctl_health({}, result.returncode)
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
    traits = {}
    if system == 'Darwin':
        registry = run(['/usr/sbin/ioreg', '-a', '-l', '-r', '-c', 'IOBlockStorageDevice'])
        if registry.returncode != 0:
            raise ValueError('Registry discovery failed; no disks queried')
        graph, traits = mac_registry_topology(plistlib.loads(registry.stdout))
    else:
        graph = linux_topology()
    physical = sorted(set().union(*(ancestors(n, graph) for n in graph)))
    if len(physical) > 64:
        raise ValueError('Too many devices for one health snapshot')
    records = {}
    for name in physical:
        records[name] = collect_device(name, system, traits)
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
