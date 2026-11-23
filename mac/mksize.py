#!/usr/bin/env python3
"""mac/mksize.py APP.bin FLAGS: the SIZE -1 resource's flags in a MacBinary application set to
FLAGS (Inside Macintosh: Processes, "The SIZE Resource"), the MacBinary II header's CRC made
again. Riptide's tools/elf2app.py writes 0x5880 (MultiFinder aware, background, 32-bit clean);
Octavo adds isHighLevelEventAware (0x0040) so the Finder sends it Open Documents events while it
runs (mac/qm_main.c handles them). GPL-2.0-only, as Quill."""
import struct, sys

def crc16(b):
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c

path, flags = sys.argv[1], int(sys.argv[2], 0)
d = bytearray(open(path, 'rb').read())
dlen, rlen = struct.unpack('>II', d[83:91])
r0 = 128 + (dlen + 127) // 128 * 128                    # the resource fork, after the padded data fork
doff, moff = struct.unpack('>II', d[r0:r0 + 8])
m = r0 + moff
tl = m + struct.unpack('>H', d[m + 24:m + 26])[0]
ntypes = struct.unpack('>H', d[tl:tl + 2])[0] + 1
done = 0
for i in range(ntypes):
    t, n, ref = struct.unpack('>4sHH', d[tl + 2 + 8 * i:tl + 10 + 8 * i])
    if t != b'SIZE':
        continue
    for j in range(n + 1):
        e = tl + ref + 12 * j
        rid = struct.unpack('>h', d[e:e + 2])[0]
        if rid != -1:
            continue
        at = r0 + doff + int.from_bytes(d[e + 5:e + 8], 'big') + 4
        old = struct.unpack('>H', d[at:at + 2])[0]
        d[at:at + 2] = struct.pack('>H', flags)
        done = 1
        print('%s: SIZE -1 flags %#06x -> %#06x' % (path, old, flags))
if not done:
    sys.exit('%s: no SIZE -1 resource' % path)
d[124:126] = struct.pack('>H', crc16(bytes(d[:124])))
open(path, 'wb').write(d)
