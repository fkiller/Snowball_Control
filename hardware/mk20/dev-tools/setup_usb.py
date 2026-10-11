"""MK20 fixed-operation CDC updater; never opens ADB or an arbitrary serial port."""
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import time
from setup_storage import token, digest_file

FILES = ('mk20-hud', 'mk20-audio', 'lunch.sh', 'fonts/D2Coding.ttf', 'mk20-update')


def devices():
    from serial.tools import list_ports
    result = []
    for port in list_ports.comports():
        if (port.vid, port.pid) != (0x1d6b, 0x0104) or not re.fullmatch('mk20-[a-f0-9]{12}', port.serial_number or ''):
            continue
        identity = 'cdc|' + port.serial_number + '|' + str(port.location or port.device)
        result.append(dict(port=token(identity), usbIdentity=identity, label='MK20 · USB runtime', component='runtime', native=dict(transport='usb', device=port.device, identity=identity, deviceId=port.serial_number)))
    return result


class Link:
    def __init__(self, target):
        import serial
        current = next((d for d in devices() if d['port'] == target['port'] and d['usbIdentity'] == target['usbIdentity']), None)
        if not current:
            raise RuntimeError('usb_device_changed')
        self.target = current
        self.serial = serial.Serial(current['native']['device'], 115200, timeout=2, write_timeout=30)
        self.serial.reset_input_buffer()

    def close(self):
        self.serial.close()

    def send(self, line):
        data = (line + '\n').encode('ascii')
        if self.serial.write(data) != len(data):
            raise RuntimeError('usb_transfer_incomplete')
        self.serial.flush()

    def line(self, timeout=10):
        deadline = time.monotonic() + timeout
        data = bytearray()
        while time.monotonic() < deadline and len(data) < 512:
            value = self.serial.read(1)
            if value == b'\n':
                text = data.decode('ascii')
                if text.startswith('SNMU1 ERROR '):
                    raise RuntimeError('usb_update_failed')
                return text
            data.extend(value)
        raise RuntimeError('usb_update_agent_unavailable')

    def expect(self, text, timeout=10):
        if self.line(timeout) != text:
            raise RuntimeError('invalid_usb_response')

    def info(self):
        nonce = secrets.token_hex(16)
        self.send('SNMU1 INFO ' + nonce)
        fields = self.line().split(' ')
        if len(fields) != 9 or fields[:3] != ['SNMU1', 'INFO', nonce] or fields[3] != self.target['native']['deviceId'] or not re.fullmatch(r'\d+\.\d+\.\d+', fields[4]) or fields[5] not in ('0', '1') or fields[8] not in ('0', '1'):
            raise RuntimeError('invalid_usb_response')
        if any(v != '-' and not re.fullmatch('[a-f0-9]{64}', v) for v in fields[6:8]):
            raise RuntimeError('invalid_usb_response')
        return dict(version=fields[4], wifiConfigured=fields[5] == '1', hud=fields[6], audio=fields[7], running=fields[8] == '1')


def probe(target, release):
    link = Link(target)
    try:
        info = link.info()
        expected = release['components']['runtime']['images']
        return dict(component='runtime', currentVersion=info['version'], nextStep='ready', writable=True, wifiConfigured=info['wifiConfigured'], imageMatch=info['running'] and info['hud'] == expected['mk20-hud'] and info['audio'] == expected['mk20-audio'])
    finally:
        link.close()


def install(target, directory, files, hashes, release, request, progress, emit, wifi):
    link = Link(target)
    backup = Path(directory)/('mk20-runtime-'+secrets.token_hex(12)+'.bin')
    try:
        before = link.info()
        network = request.get('network')
        if request.get('migrateWifi') is True:
            public = wifi(False)
            if public.get('ssid') != request.get('ssid') or public.get('enterprise'):
                raise RuntimeError('wifi_changed_review_again')
            network = wifi(True)
            if network.get('ssid') != request.get('ssid') or 'password' not in network:
                raise RuntimeError('wifi_changed_review_again')
        if network is not None:
            ssid, password = network.get('ssid'), network.get('password')
            if before['wifiConfigured'] or not isinstance(ssid, str) or not 1 <= len(ssid.encode()) <= 32 or not isinstance(password, str) or (password and not 8 <= len(password.encode()) <= 63) or any(c in ssid+password for c in '\x00\r\n'):
                raise RuntimeError('wifi_review_required')
        progress('backup')
        link.send('SNMU1 BACKUP')
        fields = link.line(90).split(' ')
        if len(fields) != 4 or fields[:2] != ['SNMU1', 'BACKUP'] or not fields[2].isdigit() or not 0 < int(fields[2]) <= 256*1024**2 or not re.fullmatch('[a-f0-9]{64}', fields[3]):
            raise RuntimeError('backup_incomplete')
        size, expected = int(fields[2]), fields[3]
        digest = hashlib.sha256()
        with open(backup, 'xb') as stream:
            os.chmod(backup, 0o600)
            left = size
            while left:
                chunk = link.serial.read(min(left, 32768))
                if not chunk:
                    raise RuntimeError('backup_incomplete')
                stream.write(chunk);digest.update(chunk);left -= len(chunk)
            stream.flush();os.fsync(stream.fileno())
        link.expect('SNMU1 BACKUP-END')
        if digest.hexdigest() != expected or digest_file(backup) != expected or backup.stat().st_size != size:
            raise RuntimeError('backup_incomplete')
        meta = dict(component='runtime', target=target['port'], bytes=size, sha256=expected, scope='complete-runtime-partition', source='/dev/mmcblk0p1', deviceId=target['native']['deviceId'])
        with open(backup.with_suffix('.json'), 'x', encoding='utf-8') as stream:
            os.chmod(stream.name, 0o600);json.dump(meta, stream);stream.flush();os.fsync(stream.fileno())
        progress('backup-verified', backup=str(backup), bytes=size)
        link.send('SNMU1 BEGIN '+expected+' '+release['version']+' '+secrets.token_hex(16));link.expect('SNMU1 READY')
        progress('flash')
        for index, name in enumerate(FILES):
            link.send(f'SNMU1 KEEP {index} {hashes[name]}')
            answer = link.line()
            if answer == 'SNMU1 KEPT':
                continue
            if answer != 'SNMU1 NEED':
                raise RuntimeError('invalid_usb_response')
            link.send(f'SNMU1 PUT {index} {len(files[name])} {hashes[name]}');link.expect('SNMU1 SEND')
            view = memoryview(files[name])
            while view:
                written = link.serial.write(view[:32768])
                if not written:
                    raise RuntimeError('usb_transfer_incomplete')
                view = view[written:]
            link.serial.flush();link.expect('SNMU1 STORED', 60)
        link.send('SNMU1 APPLY');link.expect('SNMU1 APPLIED', 90)
        progress('verify-settings')
        if network is not None:
            link.send('SNMU1 WIFI '+network['ssid'].encode().hex()+' '+(network['password'].encode().hex() or '-'));link.expect('SNMU1 WIFI-SAVED')
        progress('reboot');link.send('SNMU1 RESTART');link.expect('SNMU1 RESTARTED');link.close()
        deadline = time.monotonic()+120
        while time.monotonic() < deadline:
            time.sleep(2)
            candidate = next((d for d in devices() if d['native']['deviceId'] == target['native']['deviceId']), None)
            if not candidate:
                continue
            try:
                check = Link(candidate)
                try:
                    actual = check.info()
                finally:
                    check.close()
                if actual['version'] == release['version'] and actual['running'] and actual['hud'] == hashes['mk20-hud'] and actual['audio'] == hashes['mk20-audio'] and (not (before['wifiConfigured'] or network is not None) or actual['wifiConfigured']):
                    emit('verified', component='runtime', version=release['version'], backup=str(backup));return
            except (OSError, RuntimeError):
                pass
        raise RuntimeError('runtime_reboot_unconfirmed')
    except Exception:
        try:
            link.send('SNMU1 ABORT');link.expect('SNMU1 ABORTED')
        except Exception:
            pass
        raise
    finally:
        link.close()
