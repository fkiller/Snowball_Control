"""Validate developer MAC parsing with the actual shell script, no network stubs."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('snowball-dev-adb.sh')

@unittest.skipUnless(shutil.which('sh'), 'POSIX shell required')
class DevelopmentAdbConfig(unittest.TestCase):
    def check(self, value):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory)/'dev-access.conf'
            config.write_text(value, encoding='utf-8')
            return subprocess.run(['sh', str(SCRIPT), '--check-config', str(config)], capture_output=True, text=True)

    def test_quotes_and_crlf(self):
        for value in ['DEV_PC_MAC="12:34:56:78:9a:bc"\r\n', "DEV_PC_MAC='12:34:56:78:9a:bc'\n"]:
            result = self.check(value)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout.strip(), '12:34:56:78:9a:bc')

    def test_missing_invalid_and_ambiguous_mac_are_rejected(self):
        for value in ['', 'DEV_PC_MAC="example"\n', 'DEV_PC_MAC="12:34:56:78:9a:bc"\n'*2, 'DEV_PC_MAC="12:34:56:78:9a:bc"; exit 0\n']:
            self.assertNotEqual(self.check(value).returncode, 0)

    def test_trusted_subnet_rejects_public_broad_and_invalid_routes(self):
        for subnet in ['192.168.1.0/24', '10.0.0.0/8', '172.16.1.0/24']:
            self.assertEqual(subprocess.run(['sh', str(SCRIPT), '--check-subnet', subnet]).returncode, 0)
        for subnet in ['0.0.0.0/0', '8.8.8.0/24', '172.32.0.0/16', '192.168.999.0/24', '10..0.0/24', '10.0.0.0/0', '10.0.0.0/31', '10.0.0.0/24;echo bad']:
            self.assertNotEqual(subprocess.run(['sh', str(SCRIPT), '--check-subnet', subnet]).returncode, 0)

    def test_config_is_never_executed(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory)/'executed'
            result = self.check('DEV_PC_MAC="12:34:56:78:9a:bc"\ntouch '+str(marker)+'\n')
            self.assertEqual(result.returncode, 0)
            self.assertFalse(marker.exists())

if __name__ == '__main__':
    unittest.main()
