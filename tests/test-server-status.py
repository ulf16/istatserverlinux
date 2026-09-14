import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('status', Path(__file__).resolve().parents[1] / 'contrib/istat-server-status.py')
status = importlib.util.module_from_spec(spec)
spec.loader.exec_module(status)


class StatusTests(unittest.TestCase):
    def test_configuration_matches_first_value_and_redacts(self):
        secret = 'never-print-this-password'
        data = ('server_code ' + secret + '\nnetwork_port 5109\nnetwork_port 6200\n'
                'network_addr 127.0.0.1 # local\nunknown_key ' + secret).encode()
        with patch.object(status, 'read_small', return_value=data):
            record = status.configuration('/not/read')
        self.assertEqual(record, {'state': 'readable', 'port': 5109, 'address': '127.0.0.1', 'pairing': 'configured'})
        self.assertNotIn(secret, json.dumps(record))
        with patch.object(status, 'read_small', return_value=b'network_port secret123\nnetwork_addr secret456'):
            record = status.configuration('/not/read')
        self.assertEqual(record['state'], 'invalid')
        self.assertNotIn('secret', json.dumps(record))

    def test_missing_denied_and_oversized(self):
        for error, expected in [(FileNotFoundError(), 'missing'), (PermissionError(), 'permission-denied'),
                                (ValueError(), 'unavailable'), (UnicodeDecodeError('utf8', b'x', 0, 1, 'bad'), 'unavailable')]:
            with patch.object(status, 'read_small', side_effect=error):
                self.assertEqual(status.configuration('file')['state'], expected)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'large'
            path.write_bytes(b'x' * 262145)
            with self.assertRaises(ValueError): status.read_small(path)
            fifo = Path(directory) / 'pipe'
            os.mkfifo(fifo)
            with self.assertRaises(ValueError): status.read_small(fifo)

    def test_configuration_defaults_and_invalid_ports(self):
        with patch.object(status, 'read_small', return_value=b'# just comments'):
            self.assertEqual(status.configuration('file')['port'], 5109)
        for port in ('0', '65536', '-1', '5x', '"5109"'):
            with patch.object(status, 'read_small', return_value=('network_port ' + port).encode()):
                self.assertIsNone(status.configuration('file')['port'])

    def test_launchd_identity_and_top_level_fields(self):
        run = Mock(return_value='system/com.istat.server = {\n\tprogram = /opt/istatserverlinux/bin/istatserver\n'
                               '\tstate = running\n\tpid = 123\n\t\tpid = 999\n\tlast exit code = 0\n}')
        service = status.service('Darwin', 'com.istat.server', '/opt/istatserverlinux/bin/istatserver', run)
        self.assertEqual((service['state'], service['identity'], service['pid']), ('running', 'matched', 123))
        service = status.service('Darwin', 'com.istat.server', '/other/bin/istatserver', run)
        self.assertEqual(service['state'], 'identity-mismatch')
        self.assertIsNone(service['pid'])

    def test_systemd_identity_and_unavailable(self):
        run = Mock(return_value='LoadState=loaded\nActiveState=active\nMainPID=123\nExecMainStatus=0\n'
                               'ExecStart={ path=/usr/local/bin/istatserver ; argv[]=/usr/local/bin/istatserver ; }')
        service = status.service('Linux', 'istatserver.service', '/usr/local/bin/istatserver', run)
        self.assertEqual((service['state'], service['identity'], service['pid']), ('running', 'matched', 123))
        run.return_value = 'LoadState=not-found\nActiveState=inactive\nMainPID=0'
        self.assertEqual(status.service('Linux', 'istatserver.service', run=run)['state'], 'not-installed')
        run.return_value = None
        self.assertEqual(status.service('Linux', 'istatserver.service', run=run)['state'], 'unavailable')

    def test_listeners_only_from_matching_pid(self):
        run = Mock(return_value='p123\nn*:5109\nn[::1]:5110\np456\nn*:9999\n')
        with patch.object(status.shutil, 'which', return_value='/usr/sbin/lsof'):
            self.assertEqual(status.listeners(123, run)['ports'], [5109, 5110])
            run.return_value = None
            self.assertEqual(status.listeners(123, run)['state'], 'not-observed')

    def test_health_age_and_skip_are_not_passed(self):
        with patch.object(status, 'read_small', return_value=b'<health version="1"><device checked="1000" state="unavailable"/></health>'):
            self.assertEqual(status.health_cache('file', 1100), {'state': 'fresh', 'checked': 1000, 'devices': 1, 'skipped': 1})
            self.assertEqual(status.health_cache('file', 2000)['state'], 'stale')
            self.assertEqual(status.health_cache('file', 100)['state'], 'stale')
        with patch.object(status, 'read_small', return_value=b'<!DOCTYPE health><health version="1"/>'):
            self.assertEqual(status.health_cache('file', 100)['state'], 'unavailable')

    def test_inventory_read_only_no_secret_files(self):
        for system, prefix in [('Darwin', '/opt/istatserverlinux'), ('Linux', '/usr/local')]:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                install = root / prefix.lstrip('/')
                (install / 'bin').mkdir(parents=True)
                (install / 'bin/istatserver').write_text('DO NOT EXECUTE')
                config = install / 'etc/istatserver'
                config.mkdir(parents=True)
                (config / 'istatserver.conf').write_text('server_code never-export-secret\n')
                for name in ('istatserver.db', 'istatserver_generated.conf', 'cert.pem', 'key.pem'):
                    (config / name).write_text('DO NOT READ OR CHANGE')
                before = {p: (p.stat().st_ino, p.stat().st_mtime_ns, p.read_bytes()) for p in config.iterdir()}
                run = Mock(return_value=None)
                with patch.object(status, 'read_small', wraps=status.read_small) as reader:
                    report = status.inventory(system=system, root=root, run=run, now=1000)
                self.assertTrue(report['installation']['installed'])
                self.assertNotIn('never-export-secret', json.dumps(report))
                self.assertTrue(all(Path(call.args[0]).name in ('istatserver.conf', 'status.xml') for call in reader.call_args_list))
                self.assertTrue(all(call.args[0][0] in ('/bin/launchctl', '/usr/bin/systemctl') for call in run.call_args_list))
                self.assertEqual(before, {p: (p.stat().st_ino, p.stat().st_mtime_ns, p.read_bytes()) for p in config.iterdir()})
                self.assertFalse(report['capabilities']['settings_write'])

    def test_command_failure_never_echoes_output(self):
        result = subprocess.CompletedProcess([], 1, b'secret', b'secret')
        with patch.object(status.subprocess, 'run', return_value=result):
            self.assertIsNone(status.command(['fixed-command']))
        with patch.object(status.subprocess, 'run', side_effect=subprocess.TimeoutExpired('fixed-command', 3)):
            self.assertIsNone(status.command(['fixed-command']))


if __name__ == '__main__': unittest.main()
