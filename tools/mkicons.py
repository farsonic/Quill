#!/usr/bin/env python3
# mkicons.py - draws Quill's Workbench icons and writes them as .info files.
#
#   python3 tools/mkicons.py <package-dir>
#
# <package-dir> holds the Quill drawer as it goes into the archive (Quill/...). Icons are made for
# the drawer itself (Quill.info, next to it), the program, the installer, the README, the example
# documents and the drawers inside. Each icon has two images: a GlowIcon (OS 3.5 and later, AROS),
# and the old four-colour one for Workbench 2.x/3.0/3.1, made from the same picture.
#
# The pictures are original, drawn here with PIL at four times the size and scaled down: a quill
# pen, pages, drawers and a box. PNG copies go to <package-dir>/../icon-previews for checking.
import os, struct, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W = H = 46                 # icon size (GlowIcons' usual 46 x 46)
K = 4                      # drawn at K times the size
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, '..', 'TrueType', 'Carlito-Bold.ttf')

# ---------------------------------------------------------------- drawing helpers (46-unit coordinates)

def canvas():
    im = Image.new('RGBA', (W * K, H * K), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)

def S(*v):
    return [x * K for x in v]

def font(size):
    for f in (FONT, os.path.join(HERE, '..', '..', 'TrueType', 'Carlito-Bold.ttf'), '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'):
        if os.path.exists(f):
            return ImageFont.truetype(f, int(size * K))
    return ImageFont.load_default()

INK = (30, 34, 48)
PAPER = (252, 252, 248)
LINE = (120, 150, 200)
BLUE = (58, 102, 176)
FEATHER = (246, 236, 206)
FEATHER_DARK = (150, 110, 60)

def page(d, x0, y0, x1, y1, fold=7, lines=True):
    """a sheet of paper with its top right corner folded over, and lines of text"""
    d.polygon(S(x0, y0, x1 - fold, y0, x1, y0 + fold, x1, y1, x0, y1), fill=PAPER, outline=INK, width=K)
    d.polygon(S(x1 - fold, y0, x1 - fold, y0 + fold, x1, y0 + fold), fill=(214, 218, 226), outline=INK, width=K)
    if lines:
        y = y0 + fold + 4
        n = 0
        while y < y1 - 3:
            right = x1 - 4 if n % 3 != 2 else x0 + (x1 - x0) * 3 // 5
            d.line(S(x0 + 4, y, right, y), fill=LINE, width=int(1.6 * K))
            y += 4
            n += 1

def feather(d, nib, tip, width=7.0):
    """a quill pen from its nib to the tip of the feather: a curved vane, the shaft and the nib"""
    import math
    (nx, ny), (tx, ty) = nib, tip
    dx, dy = tx - nx, ty - ny
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length            # along the shaft
    px, py = -uy, ux                             # across it
    left, right = [], []
    steps = 24
    for i in range(steps + 1):
        t = i / steps
        if t < 0.28:
            continue                             # the bare shaft near the nib
        u = (t - 0.28) / 0.72
        w = width * math.sin(math.pi * min(1.0, u * 1.05)) ** 0.8 * (1 - 0.15 * u)
        bow = 1.6 * math.sin(math.pi * u)        # the vane curves a little
        cx, cy = nx + dx * t + px * bow, ny + dy * t + py * bow
        left.append((cx + px * w * 0.55, cy + py * w * 0.55))
        right.append((cx - px * w * 0.45, cy - py * w * 0.45))
    outline = left + right[::-1]
    d.polygon([v * K for p in outline for v in p], fill=FEATHER, outline=FEATHER_DARK, width=K)
    # barbs: short strokes from the shaft out to the edge
    for i in range(4, 20, 3):
        t = 0.28 + 0.72 * i / 24
        cx, cy = nx + dx * t, ny + dy * t
        d.line(S(cx, cy, cx + px * 2.6 + ux * 1.6, cy + py * 2.6 + uy * 1.6), fill=FEATHER_DARK, width=K // 2 + 1)
    d.line(S(nx + dx * 0.2, ny + dy * 0.2, tx - ux * 1.5, ty - uy * 1.5), fill=FEATHER_DARK, width=int(K * 1.2))
    # the nib, dark, and a drop of ink
    d.polygon(S(nx, ny, nx + dx * 0.2 + px * 1.6, ny + dy * 0.2 + py * 1.6, nx + dx * 0.2 - px * 1.6, ny + dy * 0.2 - py * 1.6),
              fill=INK)
    d.ellipse(S(nx - 1.8, ny - 0.5, nx + 1.2, ny + 2.5), fill=BLUE)

def drawer(d, emblem=None, quill=False):
    """a Workbench drawer: a cabinet front with a handle, seen a little from above"""
    body, top, dark = (122, 150, 196), (164, 186, 222), (64, 88, 132)
    d.polygon(S(4, 16, 9, 9, 42, 9, 42, 16), fill=top, outline=INK, width=K)      # the top
    d.rectangle(S(4, 16, 42, 41), fill=body, outline=INK, width=K)                 # the front
    d.rectangle(S(8, 20, 38, 37), outline=dark, width=K)                           # the panel
    d.rounded_rectangle(S(17, 22, 29, 25), radius=K, fill=(230, 232, 238), outline=INK, width=K // 2 + 1)
    if quill:
        page(d, 12, 2, 28, 15, fold=4, lines=False)
        feather(d, (14, 30), (40, 1), 6.5)
    if emblem:
        f = font(11 if len(emblem) <= 2 else 9)
        bx = d.textbbox((0, 0), emblem, font=f)
        tw, th = bx[2] - bx[0], bx[3] - bx[1]
        d.text(((23 * K - tw / 2) - bx[0], (31.5 * K - th / 2) - bx[1]), emblem, font=f, fill=(255, 255, 255))

def badge_feather(d):
    d.ellipse(S(25, 25, 43, 43), fill=(236, 242, 252), outline=BLUE, width=K)
    feather(d, (29, 40), (41, 27), 4.5)

# ---------------------------------------------------------------- the icons

def pic_tool():
    im, d = canvas()
    page(d, 5, 6, 31, 42)
    feather(d, (16, 41), (43, 3), 8)
    return im

def pic_drawer():
    im, d = canvas()
    drawer(d, quill=True)
    return im

def pic_subdrawer(emblem):
    im, d = canvas()
    drawer(d, emblem=emblem)
    return im

def pic_document():
    im, d = canvas()
    page(d, 8, 3, 36, 43)
    d.rectangle(S(9, 11, 23, 15), fill=BLUE)                       # a heading
    badge_feather(d)
    return im

def pic_readme():
    im, d = canvas()
    page(d, 8, 3, 36, 43)
    d.ellipse(S(24, 25, 42, 43), fill=BLUE, outline=INK, width=K)
    f = font(13)
    bx = d.textbbox((0, 0), 'i', font=f)
    d.text((33 * K - (bx[2] - bx[0]) / 2 - bx[0], 34 * K - (bx[3] - bx[1]) / 2 - bx[1]), 'i', font=f, fill=(255, 255, 255))
    return im

def pic_install():
    im, d = canvas()
    # a box, open at the top, with a green arrow going into it
    side, front, flap = (176, 128, 74), (204, 154, 96), (226, 186, 128)
    d.polygon(S(5, 24, 23, 30, 23, 43, 5, 37), fill=front, outline=INK, width=K)
    d.polygon(S(23, 30, 41, 24, 41, 37, 23, 43), fill=side, outline=INK, width=K)
    d.polygon(S(5, 24, 23, 18, 41, 24, 23, 30), fill=(120, 84, 46), outline=INK, width=K)
    d.polygon(S(5, 24, 1, 19, 19, 13, 23, 18), fill=flap, outline=INK, width=K)
    d.polygon(S(41, 24, 45, 19, 27, 13, 23, 18), fill=flap, outline=INK, width=K)
    green = (64, 168, 80)
    d.polygon(S(19, 2, 27, 2, 27, 12, 32, 12, 23, 22, 14, 12, 19, 12), fill=green, outline=INK, width=K)
    feather(d, (8, 40), (20, 31), 3.5)
    return im

# ---------------------------------------------------------------- to icon images

BG = (170, 170, 170)        # what soft edges are blended with (Workbench grey)

def finish(im):
    """scaled down; edges blended with the background, then each pixel opaque or transparent"""
    small = im.resize((W, H), Image.LANCZOS)
    out = Image.new('RGBA', (W, H))
    px, po = small.load(), out.load()
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a < 100:
                po[x, y] = (0, 0, 0, 0)
            else:
                f = a / 255
                po[x, y] = (int(r * f + BG[0] * (1 - f)), int(g * f + BG[1] * (1 - f)), int(b * f + BG[2] * (1 - f)), 255)
    return out

def selected(im):
    """the selected image: the picture with a warm glow round it"""
    mask = im.split()[3]
    glow = mask.filter(ImageFilter.MaxFilter(5))
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    gp, mp, op, ip = glow.load(), mask.load(), out.load(), im.load()
    near = mask.filter(ImageFilter.MaxFilter(3)).load()
    for y in range(H):
        for x in range(W):
            if mp[x, y]:
                r, g, b, a = ip[x, y]
                op[x, y] = (min(255, r + 18), min(255, g + 18), min(255, b + 10), 255)
            elif near[x, y]:
                op[x, y] = (255, 214, 90, 255)
            elif gp[x, y]:
                op[x, y] = (255, 178, 64, 255)
    return out

def quantize(images, n=200):
    """one shared palette (at most n colours) for the images; returns the palette and index lists"""
    w = sum(i.size[0] for i in images)
    strip = Image.new('RGB', (w, H), BG)
    x = 0
    for i in images:
        strip.paste(i.convert('RGB'), (x, 0))
        x += i.size[0]
    q = strip.quantize(colors=n, method=Image.MEDIANCUT)
    pal = q.getpalette()[:3 * n]
    palette = [(0, 0, 0)] + [tuple(pal[i * 3:i * 3 + 3]) for i in range(n)]
    idx, x = [], 0
    qp = q.load()
    for i in images:
        a = i.split()[3].load()
        idx.append([0 if a[xx, yy] < 128 else 1 + qp[x + xx, yy] for yy in range(H) for xx in range(W)])
        x += i.size[0]
    used = sorted(set(v for l in idx for v in l) | {0})
    remap = {v: k for k, v in enumerate(used)}
    return [palette[v] for v in used], [[remap[v] for v in l] for l in idx]

# ---------------------------------------------------------------- .info writing

WB_COLOURS = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]

def nearest(c):
    return min(range(4), key=lambda i: sum((a - b) ** 2 for a, b in zip(c, WB_COLOURS[i])))

def planar(im):
    """the old-style image: two bitplanes in the Workbench colours, transparent as colour 0"""
    words = (W + 15) // 16
    planes = [bytearray(words * 2 * H) for _ in range(2)]
    p = im.load()
    for y in range(H):
        for x in range(W):
            r, g, b, a = p[x, y]
            c = nearest((r, g, b)) if a >= 128 else 0
            for k in range(2):
                if c >> k & 1:
                    planes[k][y * words * 2 + x // 8] |= 0x80 >> (x & 7)
    return struct.pack('>hhhhhIBBI', 0, 0, W, H, 2, 1, 3, 0, 0) + bytes(planes[0]) + bytes(planes[1])

class Bits:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0
    def put(self, v, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc); self.acc, self.n = 0, 0
    def data(self):
        return bytes(self.out) + (bytes([self.acc << (8 - self.n)]) if self.n else b'')

def rle(values, depth):
    """GlowIcon run-length coding: PackBits over depth-bit values"""
    bw, i, n = Bits(), 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            bw.put(257 - run, 8); bw.put(values[i], depth); i += run
            continue
        j = i
        while j < n and j - i < 128 and not (j + 2 < n and values[j] == values[j + 1] == values[j + 2]):
            j += 1
        bw.put(j - i - 1, 8)
        for v in values[i:j]:
            bw.put(v, depth)
        i = j
    return bw.data()

def chunk(cid, body):
    return cid + struct.pack('>I', len(body)) + body + (b'\0' if len(body) & 1 else b'')

def glowicon(images):
    palette, idxs = quantize(images)
    depth = max(1, (len(palette) - 1).bit_length())
    form = b'ICON' + chunk(b'FACE', struct.pack('>BBBBH', W - 1, H - 1, 0, 0x11, len(palette) * 3 - 1))
    pal = bytes(c for rgb in palette for c in rgb)
    for idx in idxs:
        img = rle(idx, depth)
        form += chunk(b'IMAG', struct.pack('>BBBBBBHH', 0, len(palette) - 1, 3, 1, 0, depth, len(img) - 1, len(pal) - 1) + img + pal)
    return b'FORM' + struct.pack('>I', len(form)) + form

WBDISK, WBDRAWER, WBTOOL, WBPROJECT, WBGARBAGE = 1, 2, 3, 4, 5
NOPOS = -2147483648

def write_info(path, kind, normal, deftool=None, tooltypes=(), stack=0, pos=None, window=None):
    """a DiskObject: the gadget, the drawer's window (drawers), both images, the default tool, the
       tooltypes, the OS 2 drawer flags, then the GlowIcon"""
    sel = selected(normal)
    gadget = struct.pack('>IhhhhHHHIIIIIHI', 0, 0, 0, W, H, 0x0006, 0x0003, 0x0001, 1, 1, 0, 0, 0, 0, 1)
    x, y = pos if pos else (NOPOS, NOPOS)
    isdrawer = kind in (WBDISK, WBDRAWER, WBGARBAGE)
    body = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack('>BBIIiiIII', kind, 0, 1 if deftool else 0, 1 if tooltypes else 0,
                                                                x, y, 1 if isdrawer else 0, 0, stack)
    if isdrawer:
        wl, wt, ww, wh = window or (60, 40, 420, 220)
        newwin = struct.pack('>hhhhBBIIIIIIIhhHHH', wl, wt, ww, wh, 255, 255, 0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
        body += newwin + struct.pack('>ii', 0, 0)
    body += planar(normal) + planar(sel)
    if deftool:
        s = deftool.encode('latin-1') + b'\0'
        body += struct.pack('>I', len(s)) + s
    if tooltypes:
        body += struct.pack('>I', (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            s = t.encode('latin-1') + b'\0'
            body += struct.pack('>I', len(s)) + s
    if isdrawer:
        body += struct.pack('>IH', 1, 1)          # show icons only, view by icon
    body += glowicon([normal, sel])
    with open(path, 'wb') as f:
        f.write(body)
    return sel

# ---------------------------------------------------------------- the package

def main(pkg):
    global FONT
    q = os.path.join(pkg, 'Quill')
    if os.path.exists(os.path.join(q, 'TrueType', 'Carlito-Bold.ttf')):
        FONT = os.path.join(q, 'TrueType', 'Carlito-Bold.ttf')
    prev = os.path.join(pkg, '..', 'icon-previews')
    os.makedirs(prev, exist_ok=True)
    pics = {}
    def make(path, name, pic, kind, **kw):
        normal = finish(pic)
        sel = write_info(path, kind, normal, **kw)
        pics[name] = (normal, sel)
    # in the drawer: the program and installer across the top, the documents below, the drawers last
    make(os.path.join(pkg, 'Quill.info'), 'drawer', pic_drawer(), WBDRAWER, window=(40, 30, 560, 330))
    make(os.path.join(q, 'Quill.info'), 'tool', pic_tool(), WBTOOL, stack=65536, pos=(16, 8))
    make(os.path.join(q, 'Install-Quill.info'), 'install', pic_install(), WBPROJECT, deftool='Installer', pos=(96, 8),
         tooltypes=('SCRIPT=Install-Quill', 'APPNAME=Quill', 'MINUSER=NOVICE', 'DEFUSER=AVERAGE'), stack=16384)
    make(os.path.join(q, 'README.md.info'), 'readme', pic_readme(), WBPROJECT, deftool='SYS:Utilities/MultiView', pos=(176, 8), stack=16384)
    docs = sorted(f for f in os.listdir(q) if f.endswith('.docx'))
    for i, f in enumerate(docs):
        make(os.path.join(q, f + '.info'), 'document', pic_document(), WBPROJECT, deftool='Quill', stack=65536,
             pos=(16 + (i % 6) * 88, 84 + (i // 6) * 76))
    row = 84 + ((len(docs) + 5) // 6) * 76
    for i, (d, emblem) in enumerate((('Fonts', 'Aa'), ('TrueType', 'TT'), ('Dict', 'abc'), ('Source', '{ }'))):
        if os.path.isdir(os.path.join(q, d)):
            make(os.path.join(q, d + '.info'), 'drawer-' + d, pic_subdrawer(emblem), WBDRAWER, pos=(16 + i * 88, row), window=(80, 60, 460, 260))
    # previews: each icon and its selected image, on Workbench grey
    names = sorted(pics)
    sheet = Image.new('RGB', (len(names) * (W * 2 + 12) * 3, H * 3 + 8), BG)
    for i, n in enumerate(names):
        a, b = pics[n]
        sheet.paste(a.resize((W * 3, H * 3), Image.NEAREST), (i * (W * 2 + 12) * 3, 4), a.resize((W * 3, H * 3), Image.NEAREST))
        sheet.paste(b.resize((W * 3, H * 3), Image.NEAREST), (i * (W * 2 + 12) * 3 + W * 3 + 6, 4), b.resize((W * 3, H * 3), Image.NEAREST))
    sheet.save(os.path.join(prev, 'icons.png'))
    print(len(pics), 'kinds of icon,', 4 + len(docs) + 4, 'files')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: mkicons.py <package-dir>')
    main(sys.argv[1])
