"""Real file/format guard tests; no simulated USB device or flash success."""
import json
from pathlib import Path
import tempfile
import unittest
import setup_bridge as bridge
from setup_qmk import image_with_settings


class PreparationGuards(unittest.TestCase):
    def test_qmk_plan_preserves_every_byte_outside_reviewed_binary(self):
        original=bytes(range(256))*224
        image=image_with_settings(original,b'\x01'*39244)
        self.assertEqual(image[39244:],original[39244:])
        self.assertEqual(len(image),len(original))
        for invalid in [b'', b'x'*len(original)]:
            with self.assertRaisesRegex(RuntimeError,'outside_application'):
                image_with_settings(original,invalid)

    def test_atomic_file_write_and_existing_network_preservation(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            network={'ssid':'test "network"','password':'test-fixture-only'}
            bridge.write_network(root,network)
            saved=(root/'dev-access.conf').read_bytes()
            self.assertTrue(bridge.read_wifi_state(root))
            with self.assertRaisesRegex(RuntimeError,'existing_wifi_preserved'):
                bridge.write_network(root,{'ssid':'replacement','password':''})
            self.assertEqual(saved,(root/'dev-access.conf').read_bytes())

    def test_network_control_characters_never_become_shell_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for ssid in ['bad\nDEV_PC_MAC=anything','bad\x00network']:
                with self.assertRaisesRegex(RuntimeError,'wifi_profile_unavailable'):
                    bridge.write_network(root,{'ssid':ssid,'password':'test-fixture-only'})
            self.assertFalse((root/'dev-access.conf').exists())

    def test_release_is_the_patched_mk20_binary(self):
        self.assertEqual(bridge.digest_file(bridge.ROOT/'qmk/bin/syk_keyboards_mk20_plus_via.bin'),bridge.RELEASE['components']['qmk']['sha256'])
        header=(bridge.ROOT/'firmware/runtime-version.h').read_text()
        self.assertIn('"'+bridge.RELEASE['version']+'"',header)


if __name__=='__main__':
    unittest.main()
