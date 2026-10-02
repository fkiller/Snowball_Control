"""Convert the MK20 TOP framebuffer readback (RGB565 LE, 428x142) to PNG."""
import struct, zlib, sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
w,h=428,142
assert len(raw)==w*h*2
rgb=bytearray()
for (v,) in struct.iter_unpack('<H',raw): rgb.extend(((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
def chunk(k,v): return struct.pack('>I',len(v))+k+v+struct.pack('>I',zlib.crc32(k+v)&0xffffffff)
scan=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
Path(sys.argv[2]).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
