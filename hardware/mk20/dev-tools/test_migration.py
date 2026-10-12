"""Execute the actual migration transaction and native recovery on real files.

Linux host test only; no USB/hardware or successful firmware operation is faked.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
FILES = ('mk20-hud', 'mk20-audio', 'lunch.sh', 'fonts/D2Coding.ttf', 'mk20-update')


@unittest.skipUnless(shutil.which('cc') and shutil.which('sh'), 'Requires Linux host C compiler and shell')
class MigrationTransaction(unittest.TestCase):
    def test_first_migration_commit_can_be_recovered_to_exact_legacy_files(self):
        with tempfile.TemporaryDirectory(prefix='snowball-migration-') as scratch:
            root = Path(scratch)
            stage = root/'.snowball-update'; stage.mkdir()
            original = {'mk20-hud': b'old HUD', 'mk20-audio': b'old audio', 'lunch.sh': b'old boot'}
            for name, data in original.items():
                (root/name).write_bytes(data)
            settings = root/'dev-access.conf'; settings.write_bytes(b'private original settings\n')
            for i, name in enumerate(FILES):
                (stage/('new'+str(i))).write_bytes(('new '+name).encode())
            script = root/'migration.sh'
            script.write_text((ROOT/'migrate_runtime.sh').read_text().replace('root=/mnt/SDCARD', 'root='+str(root)))
            subprocess.run(['sh', str(script), 'commit'], check=True)
            self.assertEqual((stage/'journal').read_text().strip(), '7')
            for name in FILES:
                self.assertEqual((root/name).read_bytes(), ('new '+name).encode())
            recovery = root/'recovery'
            subprocess.run(['cc', '-O2', '-DSNMU_TEST', '-DSNMU_ROOT="'+str(root)+'"',
                            str(ROOT.parent/'hud/mk20-update.c'), '-o', str(recovery)], check=True)
            subprocess.run([str(recovery), '--recover'], check=True)
            for name, data in original.items():
                self.assertEqual((root/name).read_bytes(), data)
            self.assertFalse((root/'mk20-update').exists())
            self.assertFalse((root/'fonts/D2Coding.ttf').exists())
            self.assertEqual(settings.read_bytes(), b'private original settings\n')
            self.assertFalse(stage.exists())

    def test_failed_staging_cleanup_never_removes_existing_runtime(self):
        with tempfile.TemporaryDirectory(prefix='snowball-staging-') as scratch:
            root = Path(scratch); stage = root/'.snowball-update'; stage.mkdir()
            (stage/'new0').write_bytes(b'incomplete staging')
            (root/'mk20-hud').write_bytes(b'original HUD')
            script = root/'migration.sh'
            script.write_text((ROOT/'migrate_runtime.sh').read_text().replace('root=/mnt/SDCARD', 'root='+str(root)))
            subprocess.run(['sh', str(script), 'rollback'], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # Remount is unavailable in an unprivileged test; exercise only the
            # exact staging cleanup branch in a copy with that command omitted.
            script.write_text(script.read_text().replace('mount -o remount,rw "$root"', ':'))
            subprocess.run(['sh', str(script), 'rollback'], check=True)
            self.assertEqual((root/'mk20-hud').read_bytes(), b'original HUD')
            self.assertFalse(stage.exists())


if __name__ == '__main__':
    unittest.main()
