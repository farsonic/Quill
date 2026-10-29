#!/usr/bin/env python3
"""Make Amiga bitmap disk fonts for Quill's screen from the TrueType fonts layout measures with.

    mkamigafonts.py OUTDIR

Writes OUTDIR/<Family>.font (a FontContentsHeader) and OUTDIR/<Family>/<ysize> (DiskFontHeader +
TextFont in an AmigaDOS hunk file) for each family and size. The pixel sizes are "em" sizes: the
size a font of N points has at 100% zoom (1 point = 1 pixel). ysize, the Amiga font height, is the
TrueType line box at that size (Windows ascent + descent), so Quill can ask for the size it needs
from the same metrics it lays out with. Glyphs are rendered without anti-aliasing (Amiga bitmap
fonts have one bit per pixel), hinted by FreeType.

The fonts are renamed (QuillSerif... from Liberation Serif..., QuillSansC from Carlito, QuillSerifC from Caladea): a rendering is a modified version
under the SIL Open Font License, which may not carry the original's name.
"""
import os
import struct
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "fonts")
FAMILIES = [  # Amiga family, TrueType file, style bits (FSF_BOLD 2, FSF_ITALIC 4)
    ("QuillSerif", "LiberationSerif-Regular.ttf", 0), ("QuillSerifB", "LiberationSerif-Bold.ttf", 2),
    ("QuillSerifI", "LiberationSerif-Italic.ttf", 4), ("QuillSerifBI", "LiberationSerif-BoldItalic.ttf", 6),
    ("QuillSans", "LiberationSans-Regular.ttf", 0), ("QuillSansB", "LiberationSans-Bold.ttf", 2),
    ("QuillSansI", "LiberationSans-Italic.ttf", 4), ("QuillSansBI", "LiberationSans-BoldItalic.ttf", 6),
    ("QuillMono", "LiberationMono-Regular.ttf", 0), ("QuillMonoB", "LiberationMono-Bold.ttf", 2),
    ("QuillMonoI", "LiberationMono-Italic.ttf", 4), ("QuillMonoBI", "LiberationMono-BoldItalic.ttf", 6),
    # for documents in Calibri and Cambria (measured with Carlito and Caladea)
    ("QuillSansC", "Carlito-Regular.ttf", 0), ("QuillSansCB", "Carlito-Bold.ttf", 2),
    ("QuillSansCI", "Carlito-Italic.ttf", 4), ("QuillSansCBI", "Carlito-BoldItalic.ttf", 6),
    ("QuillSerifC", "Caladea-Regular.ttf", 0), ("QuillSerifCB", "Caladea-Bold.ttf", 2),
    ("QuillSerifCI", "Caladea-Italic.ttf", 4), ("QuillSerifCBI", "Caladea-BoldItalic.ttf", 6),
]
SIZES = list(range(6, 41)) + [44, 48, 56, 64, 72]
LO, HI = 32, 255


def ysize_for(em, asc, desc, upm):
    return max(1, (em * (asc + desc) + upm // 2) // upm), max(1, (em * asc + upm // 2) // upm)


def render(path, em, ysize, base):
    font = ImageFont.truetype(path, em)
    glyphs = []
    for c in list(range(LO, HI + 1)) + [None]:          # the last one: what unknown characters show
        ch = chr(c) if c is not None else "▯"
        if c is not None and 0x7F <= c <= 0x9F:
            glyphs.append((0, 0, (em + 2) // 4, None))
            continue
        adv = int(round(font.getlength(ch))) if c is not None else max(1, em // 2)
        img = Image.new("1", (em * 3 + 8, ysize), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        ox = em
        try:
            d.text((ox, base), ch, font=font, fill=1, anchor="ls")
        except Exception:
            pass
        bb = img.getbbox()
        if not bb:
            glyphs.append((0, 0, adv, None))
            continue
        x0, x1 = bb[0], bb[2]
        glyphs.append((x0 - ox, x1 - x0, adv, img.crop((x0, 0, x1, ysize))))
    return glyphs


def build_font(name, style, glyphs, ysize, base):
    n = len(glyphs)
    total = sum(g[1] for g in glyphs)
    modulo = ((total + 15) // 16) * 2
    data = bytearray(modulo * ysize)
    loc, space, kern = [], [], []
    x = 0
    for kx, w, adv, img in glyphs:
        loc.append((x, w))
        # Text() draws the image at the pen plus the kern, then moves the pen by kern + space
        kern.append(kx)
        space.append(adv - kx)
        if img is not None:
            px = img.load()
            for yy in range(ysize):
                for xx in range(w):
                    if px[xx, yy]:
                        bit = x + xx
                        data[yy * modulo + (bit >> 3)] |= 0x80 >> (bit & 7)
        x += w
    # the code hunk: moveq #-1,d0; rts; DiskFontHeader; TextFont; data; charloc; charspace; charkern
    out = bytearray(b"\x70\xff\x4e\x75")
    dfh = len(out)
    name_off = dfh + 14 + 2 + 2 + 4
    tf = name_off + 32
    after = tf + 52                 # sizeof(struct TextFont)
    chardata = after
    charloc = chardata + len(data)
    charspace = charloc + 4 * n
    charkern = charspace + 2 * n
    end = charkern + 2 * n
    relocs = []
    # struct Node dfh_DF
    out += struct.pack(">IIbbI", 0, 0, 12, 0, name_off); relocs.append(dfh + 10)
    out += struct.pack(">HHi", 0x0F80, 1, 0)
    out += name.encode("latin-1")[:31].ljust(32, b"\0")
    # struct TextFont: tf_Message (Node + ReplyPort + Length)
    out += struct.pack(">IIbbI", 0, 0, 12, 0, name_off); relocs.append(tf + 10)
    out += struct.pack(">IH", 0, 52)         # mn_Length: the TextFont structure
    maxw = max([g[2] for g in glyphs] + [1])
    out += struct.pack(">HBBHHHHBB", ysize, style, 0x02 | 0x20 | 0x40, maxw, base - 1, 1, 0, LO, HI)
    out += struct.pack(">I", chardata); relocs.append(len(out) - 4)
    out += struct.pack(">H", modulo)
    out += struct.pack(">I", charloc); relocs.append(len(out) - 4)
    out += struct.pack(">I", charspace); relocs.append(len(out) - 4)
    out += struct.pack(">I", charkern); relocs.append(len(out) - 4)
    assert len(out) == after
    out += data
    for o, w in loc:
        out += struct.pack(">HH", o, w)
    for s in space:
        out += struct.pack(">h", s)
    for k in kern:
        out += struct.pack(">h", k)
    while len(out) % 4:
        out += b"\0"
    n_longs = len(out) // 4
    # in chip memory (HUNKF_CHIP): the blitter draws the letters from it on native (AGA, ECS) screens
    hunk = struct.pack(">IIIII", 0x3F3, 0, 1, 0, 0) + struct.pack(">I", n_longs | 0x40000000)
    hunk += struct.pack(">II", 0x3E9 | 0x40000000, n_longs) + bytes(out)
    hunk += struct.pack(">III", 0x3EC, len(relocs), 0) + b"".join(struct.pack(">I", r) for r in relocs) + struct.pack(">I", 0)
    hunk += struct.pack(">I", 0x3F2)
    return hunk


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    total = 0
    for fam, file, style in FAMILIES:
        path = os.path.join(SRC, file)
        t = TTFont(path, lazy=True)
        upm = t["head"].unitsPerEm
        asc, desc = t["OS/2"].usWinAscent, t["OS/2"].usWinDescent
        os.makedirs(os.path.join(outdir, fam), exist_ok=True)
        entries = []
        for em in SIZES:
            ysize, base = ysize_for(em, asc, desc, upm)
            glyphs = render(path, em, ysize, base)
            data = build_font(fam, style, glyphs, ysize, base)
            with open(os.path.join(outdir, fam, str(ysize)), "wb") as f:
                f.write(data)
            total += len(data)
            entries.append((ysize, style))
        contents = struct.pack(">HH", 0x0F00, len(entries))
        for ysize, st in entries:
            contents += ("%s/%d" % (fam, ysize)).encode("latin-1").ljust(256, b"\0") + struct.pack(">HBB", ysize, st, 0x02 | 0x20 | 0x40)
        with open(os.path.join(outdir, fam + ".font"), "wb") as f:
            f.write(contents)
    print("%d families, %d sizes each, %d KB" % (len(FAMILIES), len(SIZES), total // 1024))


if __name__ == "__main__":
    main()
