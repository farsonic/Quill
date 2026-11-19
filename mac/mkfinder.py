#!/usr/bin/env python3
# mac/mkfinder.py - the resources that introduce Octavo (Quill for the classic Mac OS) to the Finder,
# written into mac/rsrc/.
#
#   python3 mac/mkfinder.py          (needs PIL; run again only when the pictures change)
#
# The Finder learns from an application's bundle (Inside Macintosh: Macintosh Toolbox Essentials,
# "Finder Interface") which files it opens and which icons they get: the signature resource
# ('OCTV' 0, Octavo's creator), BNDL 128 tying local icon and file-reference numbers to resources,
# FREF 128-130 (the application itself, Word documents 'DOCX', text 'TEXT') and their icons in
# every size and depth the System 7 Finder draws: ICN# (32x32, 1 bit, with its mask), icl8
# (32x32, the System's 256 colours), icl4 (16 colours), ics# / ics8 / ics4 (16x16). The pictures
# are the Amiga's Workbench icons (tools/mkicons.py: the program's page and quill, a document with
# a quill badge), drawn at 32 and 16 pixels and mapped to the Mac's standard palettes. Makefile.mac
# passes the files to riptide's elf2app.py (--rsrc). Also 'vers' 1 for the Finder's Get Info.
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import mkicons
from PIL import Image

OUT = os.path.join(HERE, 'rsrc')
VERSION = (0x00, 0x15, 0x80, 0)          # 0.1.5 final (BCD major, minor.bug, stage final, release)
SHORT, LONG = '0.15', '0.15, Octavo (Quill for the classic Mac OS)'

def mac8_palette():
    """the System's 8-bit colour table ('clut' 8): a 6x6x6 cube from white (0) down (215 is black,
    kept as 255), then ten steps each of red, green, blue and grey, black last"""
    lv = [0xFF, 0xCC, 0x99, 0x66, 0x33, 0x00]
    pal = [(lv[i // 36], lv[i // 6 % 6], lv[i % 6]) for i in range(215)]
    steps = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    pal += [(s, 0, 0) for s in steps] + [(0, s, 0) for s in steps] + [(0, 0, s) for s in steps] + [(s, s, s) for s in steps]
    return pal + [(0, 0, 0)]

def mac4_palette():
    """the System's 16 colours ('clut' 4)"""
    return [(0xFF, 0xFF, 0xFF), (0xFC, 0xF3, 0x05), (0xFF, 0x64, 0x02), (0xDD, 0x08, 0x06), (0xF2, 0x08, 0x84),
            (0x46, 0x00, 0xA5), (0x00, 0x00, 0xD4), (0x02, 0xAB, 0xEA), (0x1F, 0xB7, 0x14), (0x00, 0x64, 0x11),
            (0x56, 0x2C, 0x05), (0x90, 0x71, 0x3A), (0xC0, 0xC0, 0xC0), (0x80, 0x80, 0x80), (0x40, 0x40, 0x40),
            (0x00, 0x00, 0x00)]

def nearest(pal, c):
    return min(range(len(pal)), key=lambda i: sum((a - b) ** 2 for a, b in zip(pal[i], c)))

def render(pic, size):
    """the 46-unit picture at size x size, RGBA, edges on white (the Finder's background)"""
    small = pic.resize((size, size), Image.LANCZOS)
    out = []
    for y in range(size):
        for x in range(size):
            r, g, b, a = small.getpixel((x, y))
            f = a / 255
            out.append(((int(r * f + 255 * (1 - f)), int(g * f + 255 * (1 - f)), int(b * f + 255 * (1 - f))), a >= 96))
    return out

def bits(flags, size):
    """rows of 1-bit pixels, most significant bit first"""
    out = b''
    for y in range(size):
        v = 0
        for x in range(size):
            v = v << 1 | (1 if flags[y * size + x] else 0)
        out += v.to_bytes(size // 8, 'big')
    return out

def one_bit(px, size):
    """the 1-bit picture: a plain threshold (dithering makes a 32-pixel icon look dirty), dark
    lines and outlines black, paper and feather white"""
    lum = [0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2] if m else 255 for c, m in px]
    black = [lum[i] < 170 and px[i][1] for i in range(size * size)]
    # the edge of the mask always black, as the Finder's icons have it
    mask = [m for _, m in px]
    for y in range(size):
        for x in range(size):
            i = y * size + x
            if mask[i] and any(not (0 <= x + dx < size and 0 <= y + dy < size and mask[(y + dy) * size + x + dx])
                               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                black[i] = True
    return black, mask

def icon_family(pic):
    out = {}
    for size, bw, c8, c4 in ((32, 'ICN#', 'icl8', 'icl4'), (16, 'ics#', 'ics8', 'ics4')):
        px = render(pic, size)
        black, mask = one_bit(px, size)
        out[bw] = bits(black, size) + bits(mask, size)
        p8, p4 = mac8_palette(), mac4_palette()
        out[c8] = bytes(nearest(p8, c) if m else 0 for c, m in px)
        b4 = [nearest(p4, c) if m else 0 for c, m in px]
        out[c4] = bytes(b4[i] << 4 | b4[i + 1] for i in range(0, len(b4), 2))
    return out

def pstr(s):
    b = s.encode('mac_roman')
    return bytes([len(b)]) + b

def main():
    os.makedirs(OUT, exist_ok=True)
    files = {}
    # 128 the application, 129 a Word document, 130 a text file
    for rid, pic in ((128, mkicons.pic_tool()), (129, mkicons.pic_document()), (130, mkicons.pic_readme())):
        for typ, data in icon_family(pic).items():
            files[(typ, rid)] = data
    for rid, ftype, local in ((128, b'APPL', 0), (129, b'DOCX', 1), (130, b'TEXT', 2)):
        files[('FREF', rid)] = ftype + struct.pack('>H', local) + b'\0'
    # BNDL: signature, its resource id, then two types (ICN#, FREF) each mapping local ids 0-2
    bndl = b'OCTV' + struct.pack('>HH', 0, 1)
    for t in (b'ICN#', b'FREF'):
        bndl += t + struct.pack('>H', 2) + b''.join(struct.pack('>HH', i, 128 + i) for i in range(3))
    files[('BNDL', 128)] = bndl
    files[('OCTV', 0)] = pstr('Octavo ' + SHORT)
    # vers 1: version, region 0 (US), the short and long version strings
    files[('vers', 1)] = bytes(VERSION) + struct.pack('>H', 0) + pstr(SHORT) + pstr(LONG)
    with open(os.path.join(OUT, 'list.txt'), 'w') as f:
        for (t, i), data in sorted(files.items()):
            name = '%s-%d.bin' % (t.replace('#', '_'), i)
            open(os.path.join(OUT, name), 'wb').write(data)
            f.write('%s:%d:%s\n' % (t, i, name))
    print(len(files), 'resources in', OUT)

if __name__ == '__main__':
    main()
