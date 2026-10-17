import hashlib
import unittest
from setup_maintenance import FILES, storage_plan


class MaintenanceStorage(unittest.TestCase):
    def setUp(self):
        self.files = dict(zip(FILES, [b'H'*480000, b'A'*14000, b'B'*6000, b'F'*4118928, b'U'*21000]))
        self.expected = {name: hashlib.sha256(data).hexdigest() for name,data in self.files.items()}

    def test_small_filesystem_keeps_unchanged_font_and_audio(self):
        payload = ('SPACE 2293\nFILE 0 117872 '+'a'*64+'\nFILE 1 14000 '+self.expected[FILES[1]]+
                   '\nFILE 2 4061 '+'b'*64+'\nFILE 3 4118928 '+self.expected[FILES[3]]+
                   '\nFILE 4 21000 '+'c'*64+'\n').encode()
        self.assertEqual(storage_plan(payload,self.files,self.expected),{1,3})
        self.assertEqual(storage_plan(payload.replace(b'\n',b'\r\r\n'),self.files,self.expected),{1,3})

    def test_changed_font_does_not_bypass_space_check(self):
        payload = ('SPACE 2293\nFILE 3 4118928 '+'0'*64+'\n').encode()
        with self.assertRaisesRegex(RuntimeError,'insufficient_runtime_storage'):
            storage_plan(payload,self.files,self.expected)

    def test_plan_counts_retained_old_bytes_and_safety_headroom(self):
        payload = ('SPACE 1000\nFILE 0 480000 '+'0'*64+'\n').encode()
        with self.assertRaisesRegex(RuntimeError,'insufficient_runtime_storage'):
            storage_plan(payload,self.files,self.expected)

    def test_malformed_or_duplicate_inventory_never_allows_staging(self):
        for payload in [b'',b'SPACE -1',b'SPACE 20000\nFILE 9 0 nope',
                        ('SPACE 20000\nFILE 1 14 '+'a'*64+'\nFILE 1 14 '+'a'*64).encode()]:
            with self.subTest(payload=payload), self.assertRaisesRegex(RuntimeError,'invalid_maintenance_storage'):
                storage_plan(payload,self.files,self.expected)


if __name__=='__main__': unittest.main()
