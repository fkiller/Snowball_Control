"""Test isolated MK20 PCM bridge over ADB TCP forwarding, retaining only metrics."""
import argparse
import json
import os
import secrets
import socket
import struct
import subprocess
import time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--report', type=Path, required=True)
p.add_argument('--reject-token', action='store_true')
p.add_argument('--disconnect', action='store_true')
a = p.parse_args()
adb = str(Path(os.environ['LOCALAPPDATA']) / 'Temp/Codex-MK20-ADB/platform-tools/adb.exe')
device = os.environ.get('SNOWBALL_DEVICE_ADB', '192.168.1.248:5555')
base = [adb, '-s', device]
port = 19000 + secrets.randbelow(10000)
token = secrets.token_hex(16)
local = subprocess.check_output(base + ['forward', 'tcp:0', f'tcp:{port}'], timeout=5).decode().strip()
helper = subprocess.Popen(base + ['shell', f'/mnt/SDCARD/snowball-pcm-poc {port} {token}'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
sock = None
report = {'scope': 'live PCM transport only; not dictation', 'rejectToken': a.reject_token, 'disconnect': a.disconnect}
try:
    time.sleep(.35)
    sock = socket.create_connection(('127.0.0.1', int(local)), timeout=5)
    started = time.monotonic()
    sock.sendall(((secrets.token_hex(16) if a.reject_token else token) + '\n').encode())
    def read(n):
        data = bytearray()
        while len(data) < n:
            part = sock.recv(n - len(data))
            if not part:
                raise EOFError('bridge closed')
            data.extend(part)
        return bytes(data)
    if a.reject_token:
        report['pass'] = sock.recv(8) == b''
    else:
        assert read(8) == b'SBP1' + struct.pack('<I', 16000)
        count, peak, frames, first, stop = 0, 0, 0, None, False
        while True:
            n, = struct.unpack('<I', read(4))
            if n == 0:
                report['captureExitCode'], = struct.unpack('<I', read(4))
                break
            assert n <= 4096 and n % 2 == 0
            chunk = read(n)
            first = first or time.monotonic()
            count += n
            frames += 1
            peak = max(peak, max(map(abs, struct.unpack('<' + 'h' * (n//2), chunk))))
            if a.disconnect:
                sock.close()
                sock = None
                helper.communicate(timeout=4)
                report.update(passOnDisconnect=helper.returncode == 0, receivedBytes=count,
                              helperExitCode=helper.returncode, durationSeconds=round(time.monotonic()-started, 3))
                break
            if time.monotonic() - first >= 2 and not stop:
                sock.sendall(b'STOP\n')
                stop = True
        report.update(receivedBytes=count, frames=frames, peak=peak,
                      firstFrameMs=round((first-started)*1000, 1) if first else None,
                      stopRequested=stop, passedFraming=True,
                      durationSeconds=round(time.monotonic()-started, 3))
        report['sampleClockRatio'] = round((count/32000) / report['durationSeconds'], 3)
        report['pass'] = report.get('passOnDisconnect', False) if a.disconnect else (
            count > 32000 and stop and report['captureExitCode'] == 0 and .7 < report['sampleClockRatio'] < 1.3)
    helper.communicate(timeout=5)
except Exception as e:
    report.update({'pass': False, 'error': str(e)})
finally:
    if sock:
        sock.close()
    try:
        helper.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        helper.kill()
        helper.communicate()
    subprocess.run(base + ['forward', '--remove', f'tcp:{local}'], timeout=5, capture_output=True)
    a.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
raise SystemExit(0 if report['pass'] else 1)
