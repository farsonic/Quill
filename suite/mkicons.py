#!/usr/bin/env python3
# mkicons.py - Bureau's own Workbench icons, for the suite package (suite/package.sh).
#
#   python3 suite/mkicons.py <package-dir>
#
# <package-dir> holds the Bureau drawer as it goes into the archive (Bureau/...), with each program's
# drawer already given its icons by that program's tools/mkicons.py. This draws the Bureau drawer's
# icon (a writing desk's chest of drawers, Bureau.info beside the drawer), the installer, the README,
# LICENSE and the Fonts, TrueType and Dict drawers, with Quill's drawing helpers and .info writer
# (tools/mkicons.py), places the five programs' drawers in a row, and closes up the top row of each
# program's drawer, where the single-app package has its installer (the suite has one installer).
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import mkicons as Q                                   # Quill's tools/mkicons.py
from PIL import Image

APPS = ('Quill', 'Ledger', 'Easel', 'Folio', 'Dispatch')

def pic_bureau():
    """a bureau: a chest of three drawers with a sloping writing flap, a sheet of paper on it"""
    im, d = Q.canvas()
    S, K, INK = Q.S, Q.K, Q.INK
    wood, top, dark = (176, 122, 70), (206, 158, 100), (110, 70, 36)
    d.polygon(S(5, 18, 11, 7, 41, 7, 41, 18), fill=top, outline=INK, width=K)        # the writing flap
    Q.page(d, 16, 2, 32, 14, fold=4, lines=False)
    d.rectangle(S(5, 18, 41, 43), fill=wood, outline=INK, width=K)                   # the body
    colours = ((58, 102, 176), (64, 168, 80), (210, 90, 60))
    for i, y in enumerate((20, 28, 36)):                                              # three drawers
        d.rectangle(S(8, y, 38, y + 6), fill=(196, 144, 88), outline=dark, width=K)
        d.rounded_rectangle(S(19, y + 2, 27, y + 4), radius=K // 2, fill=colours[i], outline=INK, width=K // 2 + 1)
    return im

NOPOS = Q.NOPOS

def setpos(path, x, y):
    """moves an icon: do_CurrentX/Y are at bytes 58 and 62 of a DiskObject"""
    with open(path, 'r+b') as f:
        f.seek(58)
        f.write(struct.pack('>ii', x, y))

def getpos(path):
    with open(path, 'rb') as f:
        f.seek(58)
        return struct.unpack('>ii', f.read(8))

def main(pkg):
    b = os.path.join(pkg, 'Bureau')
    tt = os.path.join(b, 'TrueType', 'Carlito-Bold.ttf')
    if os.path.exists(tt):
        Q.FONT = tt
    prev = os.path.join(pkg, 'icon-previews')
    os.makedirs(prev, exist_ok=True)
    pics = {}
    def make(path, name, pic, kind, **kw):
        normal = Q.finish(pic)
        sel = Q.write_info(path, kind, normal, **kw)
        pics[name] = (normal, sel)
    # the Bureau drawer: the installer and texts across the top, the programs, then the shared drawers
    make(os.path.join(pkg, 'Bureau.info'), 'bureau', pic_bureau(), Q.WBDRAWER, window=(40, 30, 470, 250))
    make(os.path.join(b, 'Install-Bureau.info'), 'install', Q.pic_install(), Q.WBPROJECT, deftool='Installer', pos=(16, 8),
         tooltypes=('SCRIPT=Install-Bureau', 'APPNAME=Bureau', 'MINUSER=NOVICE', 'DEFUSER=AVERAGE'), stack=16384)
    make(os.path.join(b, 'README-Bureau.md.info'), 'readme', Q.pic_readme(), Q.WBPROJECT,
         deftool='SYS:Utilities/MultiView', pos=(104, 8), stack=16384)
    make(os.path.join(b, 'LICENSE.info'), 'licence', Q.pic_readme(), Q.WBPROJECT,
         deftool='SYS:Utilities/MultiView', pos=(192, 8), stack=16384)
    for i, a in enumerate(APPS):
        info = os.path.join(b, a + '.info')
        if os.path.exists(info):
            setpos(info, 16 + i * 88, 84)
    for i, (dname, emblem) in enumerate((('Fonts', 'Aa'), ('TrueType', 'TT'), ('Dict', 'abc'))):
        if os.path.isdir(os.path.join(b, dname)):
            make(os.path.join(b, dname + '.info'), 'drawer-' + dname, Q.pic_subdrawer(emblem), Q.WBDRAWER,
                 pos=(16 + i * 88, 160), window=(80, 60, 460, 260))
    # each program's drawer: its top row (program, converter, README, Licenses.txt) without the gap
    # its single-app installer left
    for a in APPS:
        d = os.path.join(b, a)
        if not os.path.isdir(d):
            continue
        row = []
        for f in os.listdir(d):
            if f.endswith('.info'):
                x, y = getpos(os.path.join(d, f))
                if y == 8 and x != NOPOS:
                    row.append((x, f))
        for i, (x, f) in enumerate(sorted(row)):
            setpos(os.path.join(d, f), 16 + i * 80, 8)
    names = sorted(pics)
    W, H = Q.W, Q.H
    sheet = Image.new('RGB', (len(names) * (W * 2 + 12) * 3, H * 3 + 8), Q.BG)
    for i, n in enumerate(names):
        a, s = pics[n]
        sheet.paste(a.resize((W * 3, H * 3), Image.NEAREST), (i * (W * 2 + 12) * 3, 4), a.resize((W * 3, H * 3), Image.NEAREST))
        sheet.paste(s.resize((W * 3, H * 3), Image.NEAREST), (i * (W * 2 + 12) * 3 + W * 3 + 6, 4), s.resize((W * 3, H * 3), Image.NEAREST))
    sheet.save(os.path.join(prev, 'bureau-icons.png'))
    print(len(pics), "of Bureau's own icons")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: mkicons.py <package-dir>')
    main(sys.argv[1])
