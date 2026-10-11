"""Execute native updater against actual private files and a socketpair.

This exercises framing, readback and rollback; it is not a USB/hardware test.
Only the test build replaces mount/reboot and running-process observation.
"""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]/'mk20-update.c'
NAMES = ['mk20-hud', 'mk20-audio', 'lunch.sh', 'fonts/D2Coding.ttf', 'mk20-update']


class Transaction(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix='snowball-native-usb-')
        cls.root = Path(cls.folder.name)
        cls.binary = cls.root/'updater'
        subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-DSNMU_TEST', '-DSNMU_ROOT="'+str(cls.root)+'"', str(SOURCE), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def setUp(self):
        (self.root/'fonts').mkdir(exist_ok=True)
        self.original = {n:('original '+n).encode() for n in NAMES}
        self.replacement = {n:('replacement '+n).encode()*40 for n in NAMES}
        for n, data in self.original.items():
            (self.root/n).write_bytes(data)
        (self.root/'partition.bin').write_bytes(os.urandom(65536))
        (self.root/'dev-access.conf').write_bytes(b"WIFI_SSID='retained'\nWIFI_PSK='fixture-only'\n")
        (self.root/'snowball-machines.json').write_bytes(b'{"preserved":true}')
        self.client, server = socket.socketpair()
        self.client.settimeout(10)
        self.process = subprocess.Popen([str(self.binary)], stdin=server, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        server.close()
        self.reader = self.client.makefile('rb')

    def tearDown(self):
        self.reader.close();self.client.close()
        self.process.wait(timeout=10)
        self.process.stderr.close()
        self.assertFalse((self.root/'.snowball-update').exists())

    def command(self, text):
        self.client.sendall((text+'\n').encode())

    def response(self):
        return self.reader.readline().decode().strip()

    def begin(self):
        self.command('SNMU1 BACKUP');header=self.response().split()
        self.assertEqual(header[:2], ['SNMU1', 'BACKUP'])
        data=self.reader.read(int(header[2]))
        self.assertEqual(data,(self.root/'partition.bin').read_bytes())
        self.assertEqual(hashlib.sha256(data).hexdigest(),header[3])
        self.assertEqual(self.response(),'SNMU1 BACKUP-END')
        self.command('SNMU1 BEGIN '+header[3]+' 0.2.5 '+'a'*32)
        self.assertEqual(self.response(),'SNMU1 READY')

    def put(self, index, expected=None):
        data=self.replacement[NAMES[index]]
        self.command(f'SNMU1 PUT {index} {len(data)} '+(expected or hashlib.sha256(data).hexdigest()))
        self.assertEqual(self.response(),'SNMU1 SEND')
        self.client.sendall(data)
        return self.response()

    def test_verified_partition_backup_then_fixed_files_and_settings_preservation(self):
        self.begin()
        for i in range(5):
            self.assertEqual(self.put(i),'SNMU1 STORED')
        self.command('SNMU1 APPLY');self.assertEqual(self.response(),'SNMU1 APPLIED')
        for n,data in self.replacement.items():self.assertEqual((self.root/n).read_bytes(),data)
        self.assertEqual((self.root/'dev-access.conf').read_bytes(),b"WIFI_SSID='retained'\nWIFI_PSK='fixture-only'\n")
        self.assertEqual((self.root/'snowball-machines.json').read_bytes(),b'{"preserved":true}')

    def test_invalid_checksum_aborts_without_changing_runtime(self):
        self.begin();self.assertEqual(self.put(0,'0'*64),'SNMU1 ERROR usb_update_failed')
        for n,data in self.original.items():self.assertEqual((self.root/n).read_bytes(),data)

    def test_unchanged_large_assets_are_verified_without_duplicate_storage(self):
        self.begin()
        for i,name in enumerate(NAMES):
            self.command(f'SNMU1 KEEP {i} '+hashlib.sha256(self.original[name]).hexdigest())
            self.assertEqual(self.response(),'SNMU1 KEPT')
            self.assertFalse((self.root/'.snowball-update'/('new'+str(i))).exists())
        self.command('SNMU1 APPLY');self.assertEqual(self.response(),'SNMU1 APPLIED')
        for n,data in self.original.items():self.assertEqual((self.root/n).read_bytes(),data)

    def test_empty_wifi_import_retains_unrelated_configuration(self):
        (self.root/'dev-access.conf').write_bytes(b"WIFI_SSID=''\nWIFI_PSK=''\nOTHER='retained'\n")
        self.begin()
        for i in range(5):self.assertEqual(self.put(i),'SNMU1 STORED')
        self.command('SNMU1 APPLY');self.assertEqual(self.response(),'SNMU1 APPLIED')
        self.command('SNMU1 WIFI '+b"test'network".hex()+' '+b'fixture-only'.hex())
        self.assertEqual(self.response(),'SNMU1 WIFI-SAVED')
        config=(self.root/'dev-access.conf').read_text()
        self.assertIn("OTHER='retained'",config)
        self.assertIn("WIFI_SSID='test'\\''network'",config)

    def test_backup_ack_and_file_bounds_required_before_write(self):
        self.command('SNMU1 BEGIN '+'0'*64+' 0.2.5 '+'a'*32)
        self.assertEqual(self.response(),'SNMU1 ERROR usb_update_failed')
        self.begin();self.command('SNMU1 PUT 8 10 '+'0'*64)
        self.assertEqual(self.response(),'SNMU1 ERROR usb_update_failed')
        for n,data in self.original.items():self.assertEqual((self.root/n).read_bytes(),data)

    def test_disconnect_cleans_uncommitted_staging(self):
        self.begin();self.assertEqual(self.put(0),'SNMU1 STORED')
        self.client.shutdown(socket.SHUT_WR)
        self.process.wait(timeout=10)
        for n,data in self.original.items():self.assertEqual((self.root/n).read_bytes(),data)

    def test_boot_recovery_restores_partial_commit(self):
        self.client.shutdown(socket.SHUT_WR);self.process.wait(timeout=10)
        stage=self.root/'.snowball-update';stage.mkdir();(stage/'journal').write_text('31\n')
        for i in range(3):
            (self.root/NAMES[i]).rename(stage/('old'+str(i)))
            (self.root/NAMES[i]).write_bytes(self.replacement[NAMES[i]])
        subprocess.run([str(self.binary),'--recover'],check=True)
        for n,data in self.original.items():self.assertEqual((self.root/n).read_bytes(),data)


if __name__=='__main__':unittest.main()
