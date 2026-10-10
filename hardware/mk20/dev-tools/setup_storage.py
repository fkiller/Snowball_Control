"""Real OS inventory and full-media backup. No raw-media writes or ADB."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import uuid


def tool(argv, timeout=15):
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
    p = subprocess.run(argv, capture_output=True, timeout=timeout, creationflags=flags)
    if p.returncode:
        raise RuntimeError('storage_inventory_unavailable')
    return p.stdout


def token(value):
    return 't-' + hashlib.sha256(value.encode()).hexdigest()[:32]


def cards():
    found = []
    if sys.platform == 'win32':
        script = r'''
$ErrorActionPreference='Stop'
@(Get-Disk | Where-Object { $_.BusType -in @('USB','SD','MMC') -and -not $_.IsBoot -and -not $_.IsSystem -and $_.Size -gt 0 -and $_.Size -le 274877906944 } | ForEach-Object {
 $disk=$_
 Get-Partition -DiskNumber $disk.Number | Where-Object DriveLetter | ForEach-Object {
  $volume=$_ | Get-Volume
  if($volume.FileSystem -in @('FAT','FAT32','exFAT')) {
   [pscustomobject]@{raw=('\\.\PhysicalDrive'+$disk.Number);root=($_.DriveLetter+':\');size=[long]$disk.Size;identity=($disk.UniqueId+'|'+$disk.SerialNumber+'|'+$disk.Size);label=$volume.FileSystemLabel}
  }
 }
}) | ConvertTo-Json -Compress
'''
        data = json.loads(tool(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script]).decode('utf-8-sig') or '[]')
        found = data if isinstance(data, list) else [data]
    elif sys.platform == 'darwin':
        data = plistlib.loads(tool(['diskutil', 'list', '-plist', 'external', 'physical']))
        for disk in data.get('AllDisksAndPartitions', []):
            for partition in disk.get('Partitions', []):
                if not partition.get('MountPoint'):
                    continue
                info = plistlib.loads(tool(['diskutil', 'info', '-plist', partition['DeviceIdentifier']]))
                if info.get('FilesystemType') not in ('msdos', 'exfat'):
                    continue
                whole = plistlib.loads(tool(['diskutil', 'info', '-plist', disk['DeviceIdentifier']]))
                if whole.get('Internal') or whole.get('SystemImage'):
                    continue
                found.append(dict(raw='/dev/r'+disk['DeviceIdentifier'], root=partition['MountPoint'], size=whole['TotalSize'], identity=whole.get('MediaUUID', disk['DeviceIdentifier'])+'|'+str(whole['TotalSize']), label=info.get('VolumeName', '')))
    result = []
    for card in found:
        root = Path(card['root'])
        # An arbitrary USB disk is never an automatically selected MK20 card.
        known = (root/'lunch.sh').is_file() and ((root/'mk20-plus.bin').is_file() or (root/'mk20-hud').is_file())
        if not known:
            continue
        card['port'] = token(card['identity']+'|'+card['root'])
        result.append(card)
    return result


def digest_file(file):
    digest = hashlib.sha256()
    with open(file, 'rb') as stream:
        while chunk := stream.read(4*1024*1024):
            digest.update(chunk)
    return digest.hexdigest()


def backup_media(card, directory, emit):
    directory = Path(directory).resolve(strict=True)
    target = directory/(str(uuid.uuid4())+'.bin')
    # Recheck the physical identity just before opening it. Inventory never trusts
    # renderer paths, drive letters alone, or a previously attached disk number.
    current = next((c for c in cards() if c['port'] == card['port']), None)
    if not current or any(current[k] != card[k] for k in ('raw','root','size','identity')):
        raise RuntimeError('storage_device_changed')
    size = int(card['size'])
    if size <= 0 or size > 256*1024**3:
        raise RuntimeError('invalid_storage_capacity')
    import shutil
    if shutil.disk_usage(directory).free < size + 64*1024**2:
        raise RuntimeError('backup_space_required')
    emit('backup', backup=str(target), bytes=size)
    try:
        with open(card['raw'], 'rb', buffering=0) as source, open(target, 'xb') as destination:
            os.chmod(target, 0o600)
            remaining = size
            while remaining:
                chunk = source.read(min(4*1024**2, remaining))
                if not chunk:
                    raise RuntimeError('backup_incomplete')
                destination.write(chunk)
                remaining -= len(chunk)
            destination.flush()
            os.fsync(destination.fileno())
    except PermissionError:
        raise RuntimeError('storage_backup_permission_required') from None
    if target.stat().st_size != size:
        raise RuntimeError('backup_incomplete')
    digest = digest_file(target)
    # Mounted-media changes must not produce a claimed consistent recovery image.
    physical = hashlib.sha256()
    with open(card['raw'], 'rb', buffering=0) as source:
        remaining=size
        while remaining:
            chunk=source.read(min(4*1024**2,remaining))
            if not chunk:
                raise RuntimeError('backup_incomplete')
            physical.update(chunk);remaining-=len(chunk)
    if physical.hexdigest()!=digest:
        raise RuntimeError('storage_changed_during_backup')
    receipt = dict(version=1, component='runtime', bytes=size, sha256=digest, target=card['port'], identity=card['identity'])
    sidecar = target.with_suffix('.json')
    with open(sidecar, 'x', encoding='utf-8') as stream:
        os.chmod(sidecar, 0o600)
        json.dump(receipt, stream)
        stream.flush()
        os.fsync(stream.fileno())
    emit('backup-verified', backup=str(target), bytes=size)
    return target


def backup_with_permission(card, directory, emit):
    """Elevate only a read-only backup, never flashing or credential handling."""
    try:
        with open(card['raw'],'rb',buffering=0):
            pass
    except PermissionError:
        if sys.platform!='win32':
            raise RuntimeError('storage_backup_permission_required') from None
        import ctypes
        import time
        status=Path(directory)/(str(uuid.uuid4())+'.backup-result.json')
        executable=Path(sys.executable).with_name('pythonw.exe')
        if not executable.is_file():
            executable=Path(sys.executable)
        bridge=Path(__file__).with_name('setup_bridge.py')
        arguments=subprocess.list2cmdline([str(bridge),'--backup-only',card['port'],'--backup-dir',str(directory),'--result',str(status)])
        launch=ctypes.windll.shell32.ShellExecuteW
        launch.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_int]
        launch.restype=ctypes.c_void_p
        result=launch(None,'runas',str(executable),arguments,str(bridge.parent),0)
        if not result or result<=32:
            raise RuntimeError('storage_backup_permission_required')
        emit('backup')
        deadline=time.monotonic()+1200
        while time.monotonic()<deadline:
            if status.is_file():
                data=json.loads(status.read_text())
                status.unlink()
                if data.get('error'):
                    raise RuntimeError(data['error'])
                backup=Path(data.get('backup',''))
                if backup.parent!=Path(directory) or backup.suffix!='.bin' or not backup.is_file():
                    raise RuntimeError('backup_incomplete')
                receipt=json.loads(backup.with_suffix('.json').read_text())
                if backup.stat().st_size!=card['size'] or receipt.get('target')!=card['port'] or receipt.get('sha256')!=digest_file(backup):
                    raise RuntimeError('backup_incomplete')
                emit('backup-verified',backup=str(backup),bytes=card['size'])
                return backup
            time.sleep(.5)
        raise RuntimeError('storage_backup_timed_out')
    return backup_media(card,directory,emit)
