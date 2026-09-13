import importlib.util
from pathlib import Path
import unittest
import sys
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('smart', Path(__file__).resolve().parents[1] / 'contrib/istat-smart-helper.py')
sys.dont_write_bytecode = True
smart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smart)


class SmartTests(unittest.TestCase):
    def test_exit_status(self):
        for data, status, expected in [({}, 0, 'unknown'), ({}, 99, 'standby'),
                ({'smart_status': {'passed': False}}, 8, 'failed'),
                ({}, 8, 'failed'),
                ({'smart_status': {'passed': True}}, 0, 'passed'),
                ({'smart_status': {'passed': True}}, 2, 'error'),
                ({'smart_status': {'passed': True}}, 64, 'warning'),
                ({'smart_support': {'available': False}}, 0, 'unsupported')]:
            self.assertEqual(smart.smartctl_health(data, status)['state'], expected)

    def test_topology(self):
        listing = {'AllDisksAndPartitions': [
            {'DeviceIdentifier': 'disk0', 'Partitions': [{'DeviceIdentifier': 'disk0s2'}]},
            {'DeviceIdentifier': 'disk3', 'APFSPhysicalStores': [{'DeviceIdentifier': 'disk0s2'}],
             'APFSVolumes': [{'DeviceIdentifier': 'disk3s1', 'MountedSnapshots': [{'SnapshotBSD': 'disk3s1s1'}]}]},
            {'DeviceIdentifier': 'disk6', 'Partitions': [{'DeviceIdentifier': 'disk6s2'}]},
            {'DeviceIdentifier': 'disk7', 'Partitions': [{'DeviceIdentifier': 'disk7s2'}]},
            {'DeviceIdentifier': 'disk8'}]}
        raid = {'AppleRAIDSets': [{'BSD Name': 'disk8', 'Members': [{'BSD Name': 'disk6s2'}, {'BSD Name': 'disk7s2'}]}]}
        graph = smart.mac_topology(listing, raid)
        self.assertEqual(smart.ancestors('disk3s1s1', graph), {'disk0'})
        self.assertEqual(smart.ancestors('disk8', graph), {'disk6', 'disk7'})
        self.assertEqual(smart.ancestors('dm-0', {'dm-0': ['md0'], 'md0': ['sda2', 'sdb2'], 'sda2': ['sda'], 'sdb2': ['sdb'], 'sda': [], 'sdb': []}), {'sda', 'sdb'})
        self.assertEqual(smart.ancestors('disk0', {'disk0': ['disk0']}), {'unknown-topology'})

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


if __name__ == '__main__':
    unittest.main()
