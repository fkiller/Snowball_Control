"""Host-owned MK20 preparation: real DFU, USB and authorized LAN maintenance.

Requests contain only a live inventory token. Paths, commands, and credentials
are never accepted from the renderer. stdout contains bounded public receipts.
"""
import argparse
import hashlib
import importlib.util
import io
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import select
import socket
import sys
import time
import urllib.request
import zipfile
from setup_storage import cards, token, tool, backup_media, backup_with_permission, digest_file
import setup_qmk
import setup_usb
import setup_maintenance

ROOT = Path(__file__).resolve().parents[1]
RELEASE = json.loads((ROOT/'firmware/release.json').read_text(encoding='utf-8'))
ALLOWED = set(setup_usb.FILES)


def emit(event, **data):
    print(json.dumps(dict(event=event, **data), ensure_ascii=True), flush=True)


def progress(phase, **data):
    emit('progress', phase=phase, **data)


def atomic(file, data):
    file = Path(file)
    if file.is_symlink() or any(p.is_symlink() for p in file.parents):
        raise RuntimeError('invalid_storage_path')
    file.parent.mkdir(parents=True, exist_ok=True)
    pending = file.with_name(file.name+'.snowball-new')
    with open(pending, 'xb') as stream:
        os.chmod(pending, 0o600)
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
    os.replace(pending, file)


def wifi(secret=False):
    # Fixed, bundled host helper; never import a module chosen by a plugin worker.
    supplied=os.environ.get('SNOWBALL_SETUP_OS_WIFI','')
    if not supplied or not Path(supplied).is_absolute():
        raise RuntimeError('wifi_import_unavailable')
    source=Path(supplied)
    if not source.is_file() or source.is_symlink() or source.resolve()!=source or digest_file(source)!=os.environ.get('SNOWBALL_SETUP_OS_WIFI_SHA256'):
        raise RuntimeError('wifi_import_unavailable')
    spec = importlib.util.spec_from_file_location('snowball_host_wifi', source)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.current_wifi(secret)


def usb_presence_targets(found, existing):
    """One candidate per physical USB parent, never per HID collection/function.

    A parent instance retains its actual topology/serial identity; two keyboards
    with the same VID/PID must remain separate. Presence confers no write access.
    """
    result = []
    seen = set()
    for item in found:
        identity = item.get('identity', '')
        match = re.fullmatch(r'USB\\VID_(4250&PID_426F|1D6B&PID_0104)\\[^\\]+', identity, re.I)
        if not match or identity.lower() in seen:
            continue
        seen.add(identity.lower())
        linux = match[1].upper() == '1D6B&PID_0104'
        if linux and any(t['usbIdentity'].split('|')[1].lower() in identity.lower()
                         for t in existing if (t.get('native') or {}).get('transport') == 'usb'):
            continue
        result.append(dict(port=token('usb|'+identity), usbIdentity=identity,
                           label='MK20 · USB Linux' if linux else 'MK20 · USB QMK',
                           component='runtime' if linux else 'qmk', native=None))
    return result


def inventory(directory=None):
    result = []
    result.extend(setup_maintenance.devices())
    try:
        result.extend(setup_usb.devices())
    except ImportError:
        pass
    for card in cards():
        result.append(dict(port=card['port'], usbIdentity=card['identity'], label='MK20 · SD', component='runtime', native=card))
    try:
        for item in setup_qmk.devices():
            result.append(dict(port=item['port'], usbIdentity=item['identity'], label='MK20 · QMK DFU', component='qmk', native=item))
    except Exception:
        # SD preparation is still available if a USB driver/library is absent.
        pass
    if sys.platform == 'win32':
        script = r"@(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^USB\\VID_(4250&PID_426F|1D6B&PID_0104)\\' } | ForEach-Object { [pscustomobject]@{identity=$_.InstanceId} }) | ConvertTo-Json -Compress"
        raw = tool(['powershell.exe','-NoProfile','-NonInteractive','-Command',script]).decode('utf-8-sig')
        found = json.loads(raw or '[]'); found = found if isinstance(found,list) else [found]
        result.extend(usb_presence_targets(found, result))
    if directory:
        for file in Path(directory).glob('t-*.pending.json'):
            pending=json.loads(file.read_text())
            port=pending.get('target')
            if not re.fullmatch('t-[a-f0-9]{32}',port or ''):
                continue
            existing=next((t for t in result if t['port']==port),None)
            if existing:
                existing['pending']=True
            else:
                result.append(dict(port=port,usbIdentity='pending',label='MK20 · Boot check',component='runtime',native=None,pending=True,present=False))
    return result


def read_wifi_state(root):
    file = root/'dev-access.conf'
    if not file.exists():
        return False
    # Never execute this shell configuration or expose its password.
    data = file.read_text(encoding='utf-8')
    match = re.search(r'^WIFI_SSID=(.*)$', data, re.M)
    return bool(match and match[1].strip() not in ('', "''", '""', '"YOUR_WIFI_SSID"', "'YOUR_WIFI_SSID'"))


def probe(target, directory):
    component = target['component']
    if (target.get('native') or {}).get('transport')=='maintenance':
        return setup_maintenance.probe(target, RELEASE)
    if (target.get('native') or {}).get('transport')=='usb':
        try:
            return setup_usb.probe(target,RELEASE)
        except RuntimeError:
            return dict(component='runtime',currentVersion=None,nextStep='usb-bootstrap',writable=False)
    state = dict(component=component, currentVersion=None, nextStep='enter-dfu' if component=='qmk' else 'usb-connect', writable=target['native'] is not None)
    state['pending']=target.get('pending') is True
    if state['pending']:
        state['nextStep']='return-card'
        state['pendingVersion']=json.loads((Path(directory)/(target['port']+'.pending.json')).read_text())['version']
    if component == 'runtime' and target['native']:
        root = Path(target['native']['root'])
        state['wifiConfigured'] = read_wifi_state(root)
        marker = root/'snowball-runtime.json'
        if marker.is_file():
            try:
                saved = json.loads(marker.read_text(encoding='utf-8'))
                if saved.get('version') == RELEASE['version'] and all(digest_file(root/name)==sha for name,sha in saved['files'].items() if name in ALLOWED) and set(saved['files'])==ALLOWED:
                    state['currentVersion'] = saved['version']
                    state['nextStep'] = 'return-card'
            except (ValueError, KeyError, OSError):
                pass
    elif target['native']:
        try:
            data = setup_qmk.Maple(target['native']).read()
            binary = (ROOT/'qmk/bin/syk_keyboards_mk20_plus_via.bin').read_bytes()
            if data[:len(binary)] == binary:
                state['currentVersion'] = RELEASE['components']['qmk']['version']
            state['nextStep'] = 'ready'
        except Exception:
            state['writable'] = False; state['nextStep'] = 'usb-driver'
    return state


def runtime_bundle(directory):
    release = RELEASE['components']['runtime']
    archive = Path(directory)/'mk20-runtime.zip'
    if not archive.exists() or digest_file(archive) != release['sha256']:
        progress('build')
        with urllib.request.urlopen(release['url'], timeout=30) as response:
            data = response.read(32*1024**2+1)
        if len(data)>32*1024**2 or hashlib.sha256(data).hexdigest()!=release['sha256']:
            raise RuntimeError('invalid_runtime_bundle')
        archive.write_bytes(data)
    with zipfile.ZipFile(archive) as z:
        manifest = json.loads(z.read('manifest.json'))
        if set(manifest.get('deploy',[]))!=ALLOWED or manifest.get('deviceProtocol')!='SNMK1' or manifest.get('audioProtocol')!='SNAU-lease-v1' or manifest.get('usbUpdateProtocol')!='SNMU1':
            raise RuntimeError('invalid_runtime_bundle')
        hashes = {f['path']:f['sha256'] for f in manifest['files'] if f['path'] in ALLOWED}
        files = {name:z.read(name) for name in ALLOWED}
        if set(hashes)!=ALLOWED or any(hashlib.sha256(data).hexdigest()!=hashes[name] for name,data in files.items()):
            raise RuntimeError('invalid_runtime_bundle')
    return files, hashes


def install_runtime(target, directory, request):
    card = target['native']; root = Path(card['root'])
    files, hashes = runtime_bundle(directory)  # Authenticate before touching media.
    saved = {p.name:digest_file(p) for p in root.iterdir() if p.is_file() and (p.name.startswith('snowball-') or p.name=='dev-access.conf')}
    backup = backup_with_permission(card, directory, progress)
    current = next((c for c in cards() if c['port']==card['port']),None)
    if not current or current['identity']!=card['identity']:
        raise RuntimeError('storage_device_changed')
    progress('flash')
    for name, data in files.items():
        atomic(root/name, data)
    if any(digest_file(root/name)!=sha for name,sha in saved.items()):
        raise RuntimeError('settings_preservation_failed_restore_backup')
    if any(digest_file(root/name)!=sha for name,sha in hashes.items()):
        raise RuntimeError('runtime_write_unconfirmed')
    progress('verify-settings')
    nonce = secrets.token_hex(16)
    pending = dict(version=RELEASE['version'], nonce=nonce, files=hashes, backup=str(backup), target=target['port'])
    atomic(root/'snowball-runtime.json', json.dumps({k:pending[k] for k in ('version','files')}).encode())
    # A small fixed-format receipt is consumed by the native HUD only; no code
    # or network command is executed from it.
    atomic(root/'snowball-setup.receipt', (nonce+'\n').encode())
    atomic(Path(directory)/(target['port']+'.pending.json'), json.dumps(pending).encode())
    if isinstance(request.get('network'),dict) and not read_wifi_state(root):
        write_network(root,request['network'])
    elif request.get('migrateWifi') is True and not read_wifi_state(root):
        migrate_card(root, request.get('ssid'))
    # The card is outside the device. Do not pretend that writing files rebooted it.
    emit('awaiting-device', component='runtime', backup=str(backup), nextStep='return-card')


def migrate_card(root, expected):
    if read_wifi_state(root):
        raise RuntimeError('existing_wifi_preserved')
    network = wifi(False)
    if not expected or network.get('ssid')!=expected or network.get('enterprise'):
        raise RuntimeError('wifi_changed_review_again')
    network = wifi(True)
    if network.get('ssid')!=expected or 'password' not in network:
        raise RuntimeError('wifi_changed_review_again')
    write_network(root,network)


def write_network(root,network):
    if read_wifi_state(root):
        raise RuntimeError('existing_wifi_preserved')
    ssid,password=network.get('ssid'),network.get('password')
    if not isinstance(ssid,str) or not 1<=len(ssid.encode())<=32 or not isinstance(password,str) or (password and not 8<=len(password.encode())<=63):
        raise RuntimeError('wifi_profile_unavailable')
    def quote(s):
        if '\x00' in s or '\n' in s or '\r' in s:
            raise RuntimeError('wifi_profile_unavailable')
        return "'"+s.replace("'", "'\\''")+"'"
    # Retain all unrelated config, including the debug-PC MAC; never shell-source.
    file = root/'dev-access.conf'; data=file.read_text(encoding='utf-8') if file.exists() else ''
    data=re.sub(r'^(WIFI_SSID|WIFI_PSK)=.*(?:\n|$)', '', data, flags=re.M)
    atomic(file, (data+'\nWIFI_SSID='+quote(ssid)+'\nWIFI_PSK='+quote(password)+'\n').encode())


def verify_runtime(port, directory, bindings):
    file = Path(directory)/(port+'.pending.json')
    if not file.is_file():
        raise RuntimeError('boot_verification_not_pending')
    pending = json.loads(file.read_text())
    nonce = secrets.token_hex(16)
    request = ('SNMP1\tPROBE\t'+nonce).encode()
    replies = set();peers=[]
    try:
        for binding in bindings[:16]:
            interface=ipaddress.ip_interface((binding['address'],binding['netmask']))
            address=interface.ip
            if not (address in ipaddress.ip_network('10.0.0.0/8') or address in ipaddress.ip_network('172.16.0.0/12') or address in ipaddress.ip_network('192.168.0.0/16')):
                continue
            peer=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
            try:
                peer.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1);peer.bind((str(address),0));peer.sendto(request,(str(interface.network.broadcast_address),7701));peers.append(peer)
            except OSError:
                peer.close()
        deadline = time.monotonic()+3
        while peers and time.monotonic()<deadline:
            readable,_,_=select.select(peers,[],[],.2)
            if not readable:
                continue
            peer=readable[0];data,remote=peer.recvfrom(1024)
            fields=data.decode('ascii',errors='replace').split('\t')
            address=ipaddress.ip_address(remote[0])
            if not (address in ipaddress.ip_network('10.0.0.0/8') or address in ipaddress.ip_network('172.16.0.0/12') or address in ipaddress.ip_network('192.168.0.0/16')):
                continue
            if len(fields)==8 and fields[:3]==['SNMP1','INFO',nonce] and fields[3]==pending['nonce'] and fields[4]==pending['version'] and fields[6]==pending['files']['mk20-hud'] and fields[7]==pending['files']['mk20-audio'] and re.fullmatch('mk20-[a-f0-9]{12}',fields[5]):
                replies.add(tuple(fields))
    finally:
        for peer in peers:peer.close()
    if len(replies)!=1:
        emit('awaiting-device',component='runtime',backup=pending['backup'],nextStep='return-card'); return
    progress('reboot')
    emit('verified',component='runtime',version=pending['version'],backup=pending['backup'])
    file.unlink()


def runtime_status(address, device_id):
    """Query an actually observed LAN peer; never infer firmware from USB IDs."""
    ip=ipaddress.ip_address(address)
    if not (ip in ipaddress.ip_network('10.0.0.0/8') or ip in ipaddress.ip_network('172.16.0.0/12') or ip in ipaddress.ip_network('192.168.0.0/16')) or not re.fullmatch('mk20-[a-f0-9]{12}',device_id or ''):
        raise RuntimeError('invalid_setup_target')
    state=dict(component='runtime',currentVersion=None,nextStep='usb-connect',writable=False)
    nonce=secrets.token_hex(16)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
        peer.settimeout(.8);peer.sendto(('SNMP1\tSTATUS\t'+nonce).encode(),(address,7701))
        deadline=time.monotonic()+.8
        while time.monotonic()<deadline:
            try:data,remote=peer.recvfrom(1024)
            except socket.timeout:break
            fields=data.decode('ascii',errors='replace').split('\t')
            if remote!=(address,7701) or len(fields)!=7 or fields[:3]!=['SNMP1','STATUS',nonce] or fields[4]!=device_id:
                continue
            if not re.fullmatch(r'\d+\.\d+\.\d+',fields[3]) or any(not re.fullmatch('[a-f0-9]{64}',v) for v in fields[5:]):
                continue
            state['currentVersion']=fields[3]
            if fields[3]==RELEASE['version']:
                expected=RELEASE['components']['runtime'].get('images',{})
                state['imageMatch']=fields[5]==expected.get('mk20-hud') and fields[6]==expected.get('mk20-audio')
            break
    return state


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--command',required=True,choices=['list','probe','install','wifi','verify','status']); parser.add_argument('--port'); parser.add_argument('--backup-dir',required=True)
    args=parser.parse_args(); request=json.loads(sys.stdin.readline(65537) or '{}')
    directory=Path(args.backup_dir).resolve(strict=True)
    if args.command=='status':
        emit('target',target=runtime_status(request.get('address'),request.get('deviceId')));return
    if args.command=='wifi':
        network=wifi(False); emit('wifi',network={k:network[k] for k in ('ssid','enterprise') if k in network}); return
    if args.command=='verify':
        if not re.fullmatch('t-[a-f0-9]{32}',args.port or ''):
            raise RuntimeError('invalid_setup_target')
        verify_runtime(args.port,directory,request.get('lan',[])); return
    targets=inventory(directory)
    if args.command=='list':
        emit('ports',ports=[{k:t[k] for k in ('port','usbIdentity','label','component','present','transport') if k in t} for t in targets]); return
    target=next((t for t in targets if t['port']==args.port),None)
    if not target:
        raise RuntimeError('setup_target_disconnected')
    if args.command=='probe':
        emit('target',target=probe(target,directory)); return
    if request.get('confirmBoard') is not True or request.get('version')!=RELEASE['version'] or request.get('usbIdentity')!=target['usbIdentity']:
        raise RuntimeError('firmware_review_required')
    if not target['native']:
        raise RuntimeError('qmk_enter_dfu_required')
    progress('identify')
    if target['component']=='runtime':
        if target['native'].get('transport')=='maintenance':
            files,hashes=runtime_bundle(directory)
            setup_maintenance.install(target,directory,files,hashes,RELEASE,request,progress,emit);return
        if target['native'].get('transport')=='usb':
            files,hashes=runtime_bundle(directory)
            setup_usb.install(target,directory,files,hashes,RELEASE,request,progress,emit,wifi);return
        install_runtime(target,directory,request); return
    image=(ROOT/'qmk/bin/syk_keyboards_mk20_plus_via.bin').read_bytes()
    if hashlib.sha256(image).hexdigest()!=RELEASE['components']['qmk']['sha256']:
        raise RuntimeError('invalid_qmk_image')
    backup=setup_qmk.install(target['native'],image,directory,progress)
    # Actual keyboard arrival is required; no timer converts reset into success.
    deadline=time.monotonic()+25
    while time.monotonic()<deadline:
        import libusb_package,usb.core,usb.backend.libusb1
        backend=usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
        original=target['native']['dev']
        arrived=any(d.bus==original.bus and d.port_numbers and tuple(d.port_numbers)==tuple(original.port_numbers or ()) for d in usb.core.find(find_all=True,idVendor=0x4250,idProduct=0x426f,backend=backend))
        if arrived:
            emit('verified',component='qmk',version=RELEASE['components']['qmk']['version'],backup=str(backup)); return
        time.sleep(.5)
    raise RuntimeError('qmk_reboot_unconfirmed')


if __name__=='__main__':
    if '--backup-only' in sys.argv:
        # This is the only elevated operation. It reads a live selected card and
        # writes a new recovery image. No firmware/network request is accepted.
        parser=argparse.ArgumentParser();parser.add_argument('--backup-only',required=True);parser.add_argument('--backup-dir',required=True);parser.add_argument('--result',required=True)
        args=parser.parse_args();directory=Path(args.backup_dir).resolve(strict=True);result=Path(args.result)
        if result.parent.resolve()!=directory or not re.fullmatch(r'[a-f0-9-]{36}\.backup-result\.json',result.name):
            sys.exit(1)
        try:
            card=next((c for c in cards() if c['port']==args.backup_only),None)
            if not card:raise RuntimeError('storage_device_changed')
            backup=backup_media(card,directory,lambda *a,**k:None)
            answer={'backup':str(backup)}
        except Exception as error:
            answer={'error':str(error) if re.fullmatch('[a-z0-9_]{1,80}',str(error)) else 'backup_incomplete'}
        atomic(result,json.dumps(answer).encode());sys.exit(0)
    try:
        main()
    except Exception as error:
        code=str(error) if re.fullmatch('[a-z0-9_]{1,80}',str(error)) else 'mk20_setup_failed'
        emit('error',code=code);sys.exit(1)
