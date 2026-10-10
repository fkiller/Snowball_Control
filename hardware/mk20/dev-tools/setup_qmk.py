"""STM32duino alt-2 maintenance with readback before any download.

Only the application region is affected; the bootloader is never written.
All application/settings bytes are retained in a private recovery backup.
"""
import hashlib
import json
import os
from pathlib import Path
import time
import uuid
from setup_storage import token

VID, PID, ALT, BLOCK = 0x1eaf, 0x0003, 2, 1024


def devices():
    import libusb_package
    import usb.core
    import usb.backend.libusb1
    backend = usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
    if not backend:
        raise RuntimeError('usb_backend_unavailable')
    result = []
    for dev in usb.core.find(find_all=True, idVendor=VID, idProduct=PID, backend=backend):
        identity = f'{dev.bus}:{dev.port_numbers}:{dev.address}'
        result.append(dict(port=token('dfu|'+identity), identity=identity, dev=dev))
    return result


class Maple:
    def __init__(self, target):
        import usb.util
        self.dev = target['dev']
        config = self.dev.get_active_configuration()
        interface = next((i for i in config if i.bAlternateSetting == ALT and i.bInterfaceClass == 0xfe and i.bInterfaceSubClass == 1), None)
        if interface is None:
            raise RuntimeError('mk20_dfu_layout_unrecognized')
        extra = bytes(interface.extra_descriptors)
        functional = next((extra[i:i+9] for i in range(max(0, len(extra)-8)) if extra[i:i+2] == b'\x09\x21'), None)
        if not functional or functional[2]&3 != 3 or int.from_bytes(functional[5:7], 'little') != BLOCK:
            raise RuntimeError('qmk_readback_unavailable')
        self.interface = interface.bInterfaceNumber
        usb.util.claim_interface(self.dev, self.interface)
        self.dev.set_interface_altsetting(interface=self.interface, alternate_setting=ALT)
        self.request(6)

    def request(self, request, value=0, data=None, read=False):
        return self.dev.ctrl_transfer(0xa1 if read else 0x21, request, value, self.interface, data, timeout=5000)

    def idle(self):
        deadline = time.monotonic()+30
        while time.monotonic() < deadline:
            status = bytes(self.request(3, data=6, read=True))
            if len(status) != 6 or status[0]:
                raise RuntimeError('qmk_dfu_failed')
            if status[4] in (2, 5, 7, 8, 9):
                return status[4]
            time.sleep(max(.01, min(1, int.from_bytes(status[1:4], 'little')/1000)))
        raise RuntimeError('qmk_dfu_timeout')

    def read(self):
        self.request(6)
        data = bytearray()
        for block in range(129):
            chunk = bytes(self.request(2, block, BLOCK, read=True))
            data.extend(chunk)
            if len(chunk) < BLOCK:
                break
        self.request(6)
        if len(data) not in (64*1024-8192, 128*1024-8192):
            raise RuntimeError('qmk_backup_capacity_unrecognized')
        return bytes(data)

    def write(self, image):
        self.request(6)
        for block in range((len(image)+BLOCK-1)//BLOCK):
            self.request(1, block, image[block*BLOCK:(block+1)*BLOCK])
            self.idle()
        self.request(6)

    def reboot(self):
        self.request(1, 0, b'')
        try:
            self.idle()
            self.dev.reset()
        except Exception:
            pass  # Re-enumeration, not this exception, determines completion.


def image_with_settings(before,binary):
    binary=bytes(binary)
    if len(before) not in (64*1024-8192,128*1024-8192) or not binary or len(binary)>len(before)-4096:
        raise RuntimeError('qmk_image_outside_application')
    return binary+before[len(binary):]


def install(target, binary, directory, emit):
    from setup_storage import digest_file
    maple = Maple(target)
    before = maple.read()
    backup = Path(directory)/(str(uuid.uuid4())+'.bin')
    emit('backup', backup=str(backup), bytes=len(before))
    with open(backup, 'xb') as stream:
        os.chmod(backup, 0o600)
        stream.write(before); stream.flush(); os.fsync(stream.fileno())
    sha = hashlib.sha256(before).hexdigest()
    if digest_file(backup) != sha:
        raise RuntimeError('backup_incomplete')
    meta = dict(version=1, component='qmk', bytes=len(before), sha256=sha, address='0x08002000', target=target['port'])
    with open(backup.with_suffix('.json'), 'x') as stream:
        os.chmod(backup.with_suffix('.json'), 0o600)
        json.dump(meta, stream); stream.flush(); os.fsync(stream.fileno())
    emit('backup-verified', backup=str(backup), bytes=len(before))
    binary=bytes(binary);image=image_with_settings(before,binary)
    emit('flash'); maple.write(image)
    after = maple.read()
    if after != image or after[len(binary):] != before[len(binary):]:
        raise RuntimeError('qmk_preservation_failed_restore_backup')
    emit('verify-settings'); emit('reboot'); maple.reboot()
    return backup
