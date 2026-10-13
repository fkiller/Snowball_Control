"""Fixed migration over an already authorized native ADB connection.

No credential discovery, firewall changes or renderer addresses/commands.
This trusted maintenance transport is separate from USB and device pairing.
"""
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid
from setup_storage import token, digest_file
from setup_usb import FILES

ROOT = Path(__file__).resolve().parent
REMOTE = '/tmp/snowball-migrate.sh'


def executable():
    value = os.environ.get('SNOWBALL_SETUP_ADB', '')
    file = Path(value)
    if not value or not file.is_absolute() or not file.is_file() or file.is_symlink():
        return None
    if digest_file(file) != os.environ.get('SNOWBALL_SETUP_ADB_SHA256'):
        raise RuntimeError('maintenance_tool_changed')
    return str(file)


def adb(args, endpoint=None, timeout=15, destination=None):
    program = executable()
    if not program:
        raise RuntimeError('maintenance_tool_unavailable')
    flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    result = subprocess.run([program, *(['-s', endpoint] if endpoint else []), *args],
                            stdout=destination if destination else subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=timeout, creationflags=flags)
    if result.returncode:
        raise RuntimeError('maintenance_connection_failed')
    return result.stdout


IDENTITY = ('test -e /dev/fb21 && test -r /dev/ttyS1 && test -w /dev/ttyS1 && '
            'test -r /dev/mmcblk0p1 && test -f /mnt/SDCARD/lunch.sh && '
            'test -f /mnt/SDCARD/mk20-hud && test -f /mnt/SDCARD/mk20-audio && '
            'uname -m && cat /sys/class/net/wlan0/address')


def identity(endpoint):
    fields = adb(['shell', IDENTITY], endpoint).decode('ascii').split()
    if len(fields) != 2 or fields[0] != 'armv7l' or not re.fullmatch('[a-f0-9]{2}(:[a-f0-9]{2}){5}', fields[1]):
        raise RuntimeError('maintenance_identity_unconfirmed')
    return 'mk20-'+fields[1].replace(':', '')


def devices():
    if not executable():
        return []
    found = []
    for line in adb(['devices']).decode('ascii', errors='replace').splitlines():
        fields = line.split()
        if len(fields) != 2 or fields[1] != 'device' or not re.fullmatch(r'[0-9.]+:5555', fields[0]):
            continue
        endpoint = fields[0]
        try:
            address = ipaddress.ip_address(endpoint.split(':')[0])
        except ValueError:
            continue
        if not any(address in ipaddress.ip_network(n) for n in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')):
            continue
        try:
            device_id = identity(endpoint)
        except (RuntimeError, subprocess.TimeoutExpired):
            continue
        key = 'maintenance|'+device_id+'|'+endpoint
        found.append(dict(port=token(key), usbIdentity=key, label='MK20 · LAN maintenance', component='runtime',
                          transport='maintenance', deviceId=device_id, address=endpoint.rsplit(':', 1)[0],
                          native=dict(transport='maintenance', endpoint=endpoint, deviceId=device_id)))
    return found


def hashes(endpoint, names):
    if any(name not in FILES for name in names):
        raise RuntimeError('invalid_maintenance_file')
    lines = adb(['shell', 'sha256sum '+' '.join('/mnt/SDCARD/'+n for n in names)], endpoint).decode('ascii').splitlines()
    result = {}
    for line in lines:
        fields = line.split()
        if len(fields) == 2 and re.fullmatch('[a-f0-9]{64}', fields[0]):
            result[fields[1].removeprefix('/mnt/SDCARD/')] = fields[0]
    return result


QMK_OWNER = r'''p=$(pidof mk20-hud)
case "$p" in ''|*[!0-9]*) exit 1 ;; esac
uart=
for f in /proc/$p/fd/*; do
if test "$(readlink "$f")" = /dev/ttyS1; then uart=1; break; fi
done
test "$uart" = 1 || exit 1
'''


def probe(target, release):
    current = hashes(target['native']['endpoint'], ('mk20-hud', 'mk20-audio'))
    expected = release['components']['runtime']['images']
    matches = all(current.get(name) == expected[name] for name in expected)
    try:
        supported = adb(['shell', QMK_OWNER+'printf ready'], target['native']['endpoint']).strip() == b'ready'
    except (RuntimeError, subprocess.TimeoutExpired):
        supported = False
    return dict(component='runtime', currentVersion=release['version'] if matches else None,
                nextStep='maintenance', writable=True, imageMatch=matches, prepareSupported=supported)


def prepare_qmk(target):
    """Explicitly request the manufacturer's UART bootloader command, no flash.

    The native HUD owns ttyS1: stop it only while sending the fixed frame and
    resume it with a shell trap, including failures. LAN identity is rechecked;
    neither a renderer command nor an inferred USB/LAN identity is accepted.
    """
    import setup_qmk
    endpoint = target['native']['endpoint']
    if identity(endpoint) != target['native']['deviceId']:
        raise RuntimeError('maintenance_identity_unconfirmed')
    before = {d['port'] for d in setup_qmk.devices()}
    # mk20_plus.c processByte(): AA55, sum, len, inverse-len, data, F55F.
    # id_custom_request_bootloader_jump = 0x19; no EEPROM/keymap operation.
    # The running HUD already configured its open UART as 115200 8N1.
    # Tina Linux has no stty utility; require that real owner instead.
    script = QMK_OWNER+r'''trap 'kill -CONT $p' EXIT
kill -STOP $p || exit 1
printf '\252\125\031\001\376\031\365\137' > /dev/ttyS1'''
    adb(['shell', script], endpoint)
    deadline = time.monotonic()+20
    while time.monotonic() < deadline:
        arrived = [d for d in setup_qmk.devices() if d['port'] not in before]
        if len(arrived) == 1:
            return
        time.sleep(.25)
    raise RuntimeError('qmk_dfu_unconfirmed')


SETTINGS = r'''for f in /mnt/SDCARD/dev-access.conf /mnt/SDCARD/snowball-*; do
test -f "$f" || continue
case "$f" in /mnt/SDCARD/snowball-runtime.json|/mnt/SDCARD/snowball-setup.receipt) continue ;; esac
sha256sum "$f" || exit 1
done'''


def install(target, directory, files, expected, release, request, progress, emit, *, backup_only=False):
    if request.get('migrateWifi') or request.get('network') is not None:
        raise RuntimeError('maintenance_preserves_existing_wifi')
    endpoint = target['native']['endpoint']
    if identity(endpoint) != target['native']['deviceId']:
        raise RuntimeError('maintenance_identity_unconfirmed')
    directory = Path(directory)
    backup = directory/('mk20-runtime-'+uuid.uuid4().hex+'.bin')
    partial = backup.with_suffix('.partial')
    frozen = finalized = False
    adb(['push', str(ROOT/'migrate_runtime.sh'), REMOTE], endpoint)
    def action(name):
        return adb(['shell', '/bin/sh '+REMOTE+' '+name], endpoint, timeout=60)
    # RAM-only recovery is available before starting a transaction. This also
    # repairs an interrupted first migration which never installed its boot hook.
    if not backup_only:
        helper = directory/('migration-'+uuid.uuid4().hex)
        try:
            with open(helper, 'xb') as stream:
                os.chmod(helper, 0o600); stream.write(files['mk20-update'])
            adb(['push', str(helper), '/tmp/snowball-migration-recover'], endpoint)
            actual = adb(['shell', 'sha256sum /tmp/snowball-migration-recover'], endpoint).decode().split()[0]
            if actual != expected['mk20-update']:
                raise RuntimeError('maintenance_transfer_failed')
            adb(['shell', 'chmod 700 /tmp/snowball-migration-recover'], endpoint)
        finally:
            helper.unlink(missing_ok=True)
        pending = adb(['shell', 'if test -e /mnt/SDCARD/.snowball-update; then echo pending; fi'], endpoint).strip()
        if pending:
            idle = adb(['shell', 'if pidof mk20-update >/dev/null; then echo busy; fi'], endpoint).strip()
            if idle:
                raise RuntimeError('maintenance_update_busy')
            action('rollback')
    try:
        frozen = True
        action('freeze')
        settings_before = adb(['shell', SETTINGS], endpoint)
        progress('backup')
        size = int(adb(['shell', 'cat /sys/class/block/mmcblk0p1/size'], endpoint).strip())*512
        if not 0 < size <= 256*1024**2:
            raise RuntimeError('invalid_storage_capacity')
        original = adb(['shell', 'sha256sum /dev/mmcblk0p1'], endpoint, timeout=60).decode().split()[0]
        with open(partial, 'xb') as stream:
            os.chmod(partial, 0o600)
        # Legacy Tina adbd has sync readback but no exec-out service. Pull the
        # fixed block device without a pseudo terminal or CR/LF conversion.
        adb(['pull', '/dev/mmcblk0p1', str(partial)], endpoint, timeout=900)
        with open(partial, 'r+b') as stream:
            stream.flush(); os.fsync(stream.fileno())
        if partial.stat().st_size != size or digest_file(partial) != original:
            raise RuntimeError('backup_incomplete')
        partial.rename(backup)
        meta = dict(component='runtime', target=target['port'], bytes=size, sha256=original,
                    scope='complete-runtime-partition', source='/dev/mmcblk0p1', deviceId=target['native']['deviceId'])
        with open(backup.with_suffix('.json'), 'x') as stream:
            os.chmod(stream.name, 0o600); json.dump(meta, stream); stream.flush(); os.fsync(stream.fileno())
        progress('backup-verified', backup=str(backup), bytes=size)
        if backup_only:
            action('resume'); frozen = False
            return meta | {'backup': str(backup)}
        action('begin')
        for i, name in enumerate(FILES):
            local = directory/('migration-'+uuid.uuid4().hex)
            try:
                with open(local, 'xb') as stream:
                    os.chmod(local, 0o600); stream.write(files[name])
                adb(['push', str(local), '/mnt/SDCARD/.snowball-update/new'+str(i)], endpoint, timeout=90)
                actual = adb(['shell', 'sha256sum /mnt/SDCARD/.snowball-update/new'+str(i)], endpoint).decode().split()[0]
                if actual != expected[name]:
                    raise RuntimeError('maintenance_transfer_failed')
            finally:
                local.unlink(missing_ok=True)
        progress('flash')
        action('commit')
        if hashes(endpoint, FILES) != expected or adb(['shell', SETTINGS], endpoint) != settings_before:
            raise RuntimeError('settings_verification_failed')
        progress('verify-settings')
        action('finalize')
        finalized = True
        progress('reboot')
        adb(['shell', 'sync; /sbin/reboot'], endpoint)
        deadline = time.monotonic()+120
        while time.monotonic() < deadline:
            time.sleep(2)
            try:
                adb(['connect', endpoint], timeout=5)
                if identity(endpoint) != target['native']['deviceId']:
                    continue
                command = 'for n in mk20-hud mk20-audio; do p=$(pidof "$n"); test -n "$p" && sha256sum /proc/$p/exe || exit 1; done'
                running = adb(['shell', command], endpoint).decode().splitlines()
                actual = [line.split()[0] for line in running if line.split()]
                if actual == [expected['mk20-hud'], expected['mk20-audio']] and hashes(endpoint, FILES) == expected and adb(['shell', SETTINGS], endpoint) == settings_before:
                    frozen = False
                    emit('verified', component='runtime', version=release['version'], backup=str(backup)); return
            except (RuntimeError, subprocess.TimeoutExpired):
                continue
        raise RuntimeError('runtime_reboot_unconfirmed')
    except Exception:
        if frozen:
            try:
                if not finalized:
                    action('rollback')
                action('resume')
            except Exception:
                raise RuntimeError('maintenance_recovery_unconfirmed') from None
        raise
