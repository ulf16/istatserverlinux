import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import plistlib
import subprocess
import tempfile
import sys
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('smart', Path(__file__).resolve().parents[1] / 'contrib/istat-smart-helper.py')
sys.dont_write_bytecode = True
smart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smart)


class SmartTests(unittest.TestCase):
    def test_exit_status(self):
        for data, status, expected in [({}, 0, 'unknown'), ({}, 99, 'standby'), ({}, 98, 'unavailable'),
                ({'smart_status': {'passed': False}}, 98, 'unavailable'),
                ({'smart_status': {'passed': False}}, 8, 'failed'),
                ({}, 8, 'failed'),
                ({'smart_status': {'passed': True}}, 0, 'passed'),
                ({'smart_status': {'passed': True}}, 2, 'error'),
                ({'smart_status': {'passed': True}}, 64, 'warning'),
                ({'smart_support': {'available': False}}, 0, 'unsupported')]:
            self.assertEqual(smart.smartctl_health(data, status)['state'], expected)

    def test_topology(self):
        graph, traits = smart.mac_registry_topology(registry_fixture())
        self.assertEqual(smart.ancestors('disk3s1s1', graph), {'disk0'})
        self.assertEqual(smart.ancestors('disk8', graph), {'disk6', 'disk7'})
        self.assertTrue(traits['disk0']['query_safe'])
        self.assertFalse(traits['disk6']['query_safe'])
        self.assertNotIn('disk8', traits)
        self.assertEqual(smart.ancestors('dm-0', {'dm-0': ['md0'], 'md0': ['sda2', 'sdb2'], 'sda2': ['sda'], 'sdb2': ['sdb'], 'sda': [], 'sdb': []}), {'sda', 'sdb'})
        self.assertEqual(smart.ancestors('disk0', {'disk0': ['disk0']}), {'unknown-topology'})

    def test_missing_raid_member(self):
        roots = registry_fixture()
        graph, _ = smart.mac_registry_topology(roots[:2])
        self.assertIn('unknown-raid-member', smart.ancestors('disk8', graph))
        self.assertNotEqual(smart.aggregate(smart.ancestors('disk8', graph), {'disk6': {'state': 'passed'}}), 'passed')
        roots[1]['IORegistryEntryChildren'][0]['IORegistryEntryChildren'][0]['AppleRAID-SetStatus'] = 'Degraded'
        graph, _ = smart.mac_registry_topology(roots)
        self.assertIn('unknown-topology', smart.ancestors('disk8', graph))

    def test_mac_no_queries_for_hdds_or_uncertain_devices(self):
        graph, traits = smart.mac_registry_topology(registry_fixture())
        with patch.object(smart, 'run') as run:
            for name in ('disk6', 'disk7', 'disk8', 'disk99'):
                record = smart.collect_device(name, 'Darwin', traits)
                self.assertEqual(record['state'], 'unavailable')
                self.assertIn('Skipped', record['detail'])
            run.assert_not_called()
        for medium, location, transport in [('Solid State', 'External', 'PCI-Express'),
                                            ('Rotational', 'Internal', 'Apple Fabric'),
                                            ('Solid State', 'Internal', 'USB'),
                                            ('', 'Internal', 'Apple Fabric')]:
            roots = registry_fixture()[:1]
            roots[0]['Device Characteristics']['Medium Type'] = medium
            roots[0]['Protocol Characteristics'] = {'Physical Interconnect Location': location, 'Physical Interconnect': transport}
            _, traits = smart.mac_registry_topology(roots)
            with patch.object(smart, 'run') as run:
                self.assertEqual(smart.collect_device('disk0', 'Darwin', traits)['state'], 'unavailable')
                run.assert_not_called()

    def test_mac_internal_ssd(self):
        _, traits = smart.mac_registry_topology(registry_fixture())
        output = plistlib.dumps({'SMARTStatus': 'Verified', 'MediaName': 'SSD'})
        with patch.object(smart, 'run', return_value=SimpleNamespace(returncode=0, stdout=output)) as run:
            self.assertEqual(smart.collect_device('disk0', 'Darwin', traits)['state'], 'passed')
            run.assert_called_once_with(['/usr/sbin/diskutil', 'info', '-plist', '/dev/disk0'])
        with patch.object(smart, 'run', return_value=SimpleNamespace(returncode=1, stdout=output)):
            self.assertEqual(smart.collect_device('disk0', 'Darwin', traits)['state'], 'error')

    def test_linux_strict_standby_guard_no_fallback(self):
        with patch.object(Path, 'resolve', return_value=Path('/sys/devices/pci/ata1/block/sda')):
            for status, data, state in [(98, b'', 'unavailable'), (99, b'', 'standby'),
                                        (0, b'{"smart_status":{"passed":true}}', 'passed'),
                                        (2, b'{}', 'error'), (0, b'broken', 'error')]:
                with patch.object(smart, 'run', return_value=SimpleNamespace(returncode=status, stdout=data)) as run:
                    self.assertEqual(smart.collect_device('sda', 'Linux')['state'], state)
                    run.assert_called_once_with(['/usr/sbin/smartctl', '-d', 'ata', '-n', 'standby,99,98', '-i', '-H', '-A', '-j', '/dev/sda'])
            with patch.object(smart, 'run', side_effect=subprocess.TimeoutExpired('smartctl', 15)) as run:
                self.assertEqual(smart.collect_device('sda', 'Linux')['state'], 'error')
                self.assertEqual(run.call_count, 1)

    def test_linux_unknown_transport_not_probed(self):
        with patch.object(Path, 'resolve', return_value=Path('/sys/devices/usb/block/sda')):
            with patch.object(smart, 'run') as run:
                self.assertEqual(smart.collect_device('sda', 'Linux')['state'], 'unsupported')
                run.assert_not_called()

    def test_mac_snapshot_command_boundary(self):
        registry = SimpleNamespace(returncode=0, stdout=plistlib.dumps(registry_fixture()))
        ssd = SimpleNamespace(returncode=0, stdout=plistlib.dumps({'SMARTStatus': 'Verified'}))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'status.xml'
            with patch.object(sys, 'argv', ['helper', '--output', str(output)]), \
                    patch.object(smart.platform, 'system', return_value='Darwin'), \
                    patch.object(smart, 'run', side_effect=[registry, ssd]) as run:
                smart.main()
                self.assertEqual([c.args[0] for c in run.call_args_list], [
                    ['/usr/sbin/ioreg', '-a', '-l', '-r', '-c', 'IOBlockStorageDevice'],
                    ['/usr/sbin/diskutil', 'info', '-plist', '/dev/disk0']])
            xml = ET.fromstring(output.read_bytes())
            self.assertEqual(xml.find("./volume[@bsd='disk8']").get('state'), 'unavailable')
            self.assertEqual(xml.find("./volume[@bsd='disk3s1s1']").get('state'), 'passed')
            self.assertEqual(output.stat().st_mode & 0o777, 0o644)
            previous = output.read_bytes()
            with patch.object(sys, 'argv', ['helper', '--output', str(output)]), \
                    patch.object(smart.platform, 'system', return_value='Darwin'), \
                    patch.object(smart, 'run', return_value=SimpleNamespace(returncode=1, stdout=b'')) as run:
                with self.assertRaises(ValueError):
                    smart.main()
                self.assertEqual(run.call_count, 1)
            # Discovery failure leaves the old timestamp to expire; it never refreshes a pass.
            self.assertEqual(output.read_bytes(), previous)

    def test_aggregate_and_xml(self):
        records = {'sda': {'state': 'passed', 'checked': '100', 'model': 'A & B'}, 'sdb': {'state': 'unknown', 'checked': '200'}}
        self.assertEqual(smart.aggregate(['sda', 'sdb'], records), 'unknown')
        self.assertEqual(smart.aggregate([], records), 'unknown')
        records['sdb']['state'] = 'failed'
        self.assertEqual(smart.aggregate(['sda', 'sdb'], records), 'failed')
        records['sdb']['state'] = 'passed'
        self.assertEqual(smart.aggregate(['sda', 'sdb'], records), 'passed')
        root = ET.fromstring(smart.snapshot({'md0': ['sda', 'sdb'], 'sda': [], 'sdb': []}, records))
        volume = root.find("./volume[@bsd='md0']")
        self.assertEqual(volume.get('checked'), '100')
        self.assertIn('A & B', volume.get('detail'))


def media(name, children=(), whole=False, **extra):
    return {'BSD Name': name, 'Whole': whole, 'IORegistryEntryChildren': list(children), **extra}


def registry_fixture():
    apfs = media('disk3', [media('disk3s1', [media('disk3s1s1')])], whole=True)
    roots = [{
        'Device Characteristics': {'Medium Type': 'Solid State', 'Product Name': 'Test SSD'},
        'Protocol Characteristics': {'Physical Interconnect Location': 'Internal', 'Physical Interconnect': 'Apple Fabric'},
        'IORegistryEntryChildren': [media('disk0', [media('disk0s2', [apfs])], whole=True)],
    }]
    for name in ('disk6', 'disk7'):
        raid = {'AppleRAID-SetStatus': 'Online', 'AppleRAID-Members': ['member-a', 'member-b'],
                'IORegistryEntryChildren': [media('disk8', whole=True)]}
        roots.append({'Device Characteristics': {'Medium Type': 'Rotational', 'Product Name': 'Test HDD'},
                      'Protocol Characteristics': {'Physical Interconnect Location': 'External', 'Physical Interconnect': 'SATA'},
                      'IORegistryEntryChildren': [media(name, [raid], whole=True)]})
    return roots


if __name__ == '__main__':
    unittest.main()
