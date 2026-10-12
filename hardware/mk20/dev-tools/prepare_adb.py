"""Prepare Google's checksum-verified platform tools for host-owned maintenance."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile


def prepare(directory):
    directory = Path(directory).resolve()
    executable = directory/'platform-tools'/('adb.exe' if sys.platform == 'win32' else 'adb')
    receipt = directory/'platform-tools.sha256'
    if executable.is_file() and receipt.is_file() and hashlib.sha256(executable.read_bytes()).hexdigest() == receipt.read_text().strip():
        return executable
    platform = {'win32': 'windows', 'darwin': 'macosx', 'linux': 'linux'}[sys.platform]
    with urllib.request.urlopen('https://dl.google.com/android/repository/repository2-1.xml', timeout=30) as response:
        document = ET.fromstring(response.read(8*1024**2))
    local = lambda tag: tag.rsplit('}', 1)[-1]
    package = next(e for e in document.iter() if local(e.tag) == 'remotePackage' and e.get('path') == 'platform-tools')
    archive = next(e for e in package.iter() if local(e.tag) == 'archive' and any(local(c.tag) == 'host-os' and c.text == platform for c in e))
    complete = next(e for e in archive if local(e.tag) == 'complete')
    fields = {local(e.tag): e.text for e in complete}
    name = fields['url']
    if '/' in name or not name.startswith('platform-tools') or not name.endswith('.zip'):
        raise RuntimeError('invalid_platform_tools_archive')
    with urllib.request.urlopen('https://dl.google.com/android/repository/'+name, timeout=60) as response:
        data = response.read(32*1024**2+1)
    if len(data) > 32*1024**2 or hashlib.sha1(data).hexdigest() != fields['checksum']:
        raise RuntimeError('platform_tools_checksum_failed')
    directory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for entry in archive.infolist():
            relative = Path(entry.filename)
            if relative.is_absolute() or '..' in relative.parts or not relative.parts or relative.parts[0] != 'platform-tools':
                raise RuntimeError('invalid_platform_tools_archive')
        archive.extractall(directory)
    executable.chmod(0o700)
    receipt.write_text(hashlib.sha256(executable.read_bytes()).hexdigest()+'\n')
    return executable


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--output', required=True)
    executable = prepare(parser.parse_args().output)
    print(json.dumps({'adb': str(executable), 'adbSha256': hashlib.sha256(executable.read_bytes()).hexdigest()}))
