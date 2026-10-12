"""Real file/format guard tests; no simulated USB device or flash success."""
import json
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import setup_maintenance
from setup_usb import canonical_device_id
import setup_bridge as bridge
from setup_qmk import image_with_settings


class PreparationGuards(unittest.TestCase):
    def test_windows_pnp_serial_case_keeps_real_mac_identity(self):
        self.assertEqual(canonical_device_id('MK20-90DE80FACD1D'), 'mk20-90de80facd1d')
        self.assertEqual(canonical_device_id('mk20-90de80facd1d'), 'mk20-90de80facd1d')
        for invalid in ('MK20-90DE80FACD1D-OTHER', 'MK20-90DE80FACD1', 'COM4', None):
            self.assertIsNone(canonical_device_id(invalid))

    def test_maintenance_tool_change_is_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory)/'never-execute-adb'
            executable.write_bytes(b'reviewed bytes')
            approved = hashlib.sha256(executable.read_bytes()).hexdigest()
            with patch.dict(os.environ, {'SNOWBALL_SETUP_ADB':str(executable), 'SNOWBALL_SETUP_ADB_SHA256':approved}):
                self.assertEqual(setup_maintenance.executable(), str(executable))
                executable.write_bytes(b'changed bytes')
                with self.assertRaisesRegex(RuntimeError, 'maintenance_tool_changed'):
                    setup_maintenance.devices()

    def test_maintenance_rejects_wifi_changes_before_opening_any_connection(self):
        for request in ({'network':{'ssid':'not-sent','password':'not-sent'}}, {'migrateWifi':True}):
            with self.assertRaisesRegex(RuntimeError, 'maintenance_preserves_existing_wifi'):
                setup_maintenance.install({}, None, {}, {}, {}, request, None, None)

    def test_usb_inventory_keeps_physical_parents_and_separates_same_model_devices(self):
        parents = [r'USB\VID_4250&PID_426F\7&one&0&2', r'USB\VID_4250&PID_426F\7&two&0&2']
        functions = [r'USB\VID_4250&PID_426F&MI_00\8&one&0&0000',
                     r'HID\VID_4250&PID_426F&MI_00\9&one&0&0000']
        found = [{'identity': value} for value in parents+functions+[parents[0].lower()]]
        targets = bridge.usb_presence_targets(found, [])
        self.assertEqual([t['usbIdentity'] for t in targets], parents)
        self.assertEqual(len({t['port'] for t in targets}), 2)
        self.assertTrue(all(t['native'] is None for t in targets))

    def test_product_cdc_parent_does_not_duplicate_verified_serial_attachment(self):
        identity = r'USB\VID_1D6B&PID_0104\mk20-aabbccddeeff'
        existing = [{'usbIdentity': 'cdc|mk20-aabbccddeeff|COM5', 'native': {'transport': 'usb'}}]
        self.assertEqual(bridge.usb_presence_targets([{'identity': identity}], existing), [])

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

    def test_lan_status_refuses_nonlocal_or_foreign_targets_before_sending(self):
        for address,device in [('127.0.0.1','mk20-aabbccddeeff'),('8.8.8.8','mk20-aabbccddeeff'),('192.168.1.1','foreign')]:
            with self.assertRaisesRegex(RuntimeError,'invalid_setup_target'):
                bridge.runtime_status(address,device)


if __name__=='__main__':
    unittest.main()
