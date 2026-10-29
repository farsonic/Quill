#!/usr/bin/env python3
"""mkbidi.py - writes core/bidi_tab.h, the Unicode tables core/bidi.c needs (WP27):
bidirectional classes (UAX #9), mirrored glyphs and paired brackets, Arabic joining types and
presentation forms, East Asian width (for kinsoku and the character grid).

The data comes from the Unicode Character Database as Perl ships it (lib/unicore/To/*.pl:
Bc, Bmg, Bpb, Bpt, Jt, Ea) and Python's unicodedata (the presentation forms' decompositions).
Usage: python3 tools/mkbidi.py [perl unicore dir] > core/bidi_tab.h
"""
import sys, os, glob, unicodedata

def unicore():
    if len(sys.argv) > 1: return sys.argv[1]
    for d in sorted(glob.glob('/usr/share/perl/*/unicore')): return d
    sys.exit('no Perl unicore directory found')

def table(name):
    """(start, end, value) ranges of To/<name>.pl, and the value of code points not listed"""
    path = os.path.join(unicore(), 'To', name + '.pl')
    out, missing, body = [], None, False
    for line in open(path, encoding='utf-8'):
        if "{'missing'}" in line: missing = line.split("=")[1].split("'")[1]
        if line.startswith("return <<'END'"): body = True; continue
        if not body: continue
        if line.startswith('END'): break
        f = line.rstrip('\n').split('\t')
        if len(f) < 3: continue
        a = int(f[0], 16); b = int(f[1], 16) if f[1] else a
        out.append((a, b, f[2]))
    return out, missing

CLASSES = ['L', 'R', 'AL', 'EN', 'ES', 'ET', 'AN', 'CS', 'NSM', 'BN', 'B', 'S', 'WS', 'ON',
           'LRE', 'LRO', 'RLE', 'RLO', 'PDF', 'LRI', 'RLI', 'FSI', 'PDI']

def ranges_to_starts(rngs, default, limit=0x110000):
    """a list of (start, value) where each value runs to the next start"""
    pts = []
    pos = 0
    for a, b, v in sorted(rngs):
        if a > pos: pts.append((pos, default))
        pts.append((a, v)); pos = b + 1
    if pos < limit: pts.append((pos, default))
    merged = []
    for s, v in pts:
        if merged and merged[-1][1] == v: continue
        merged.append((s, v))
    return merged

def main():
    o = []
    o.append('/* bidi_tab.h - written by tools/mkbidi.py from the Unicode Character Database %s (do not edit).' % unicodedata.unidata_version)
    o.append('   Each table of starts runs from its start to the next one. */')
    bc, miss = table('Bc')
    st = ranges_to_starts(bc, miss)
    o.append('#define BIDI_NRANGES %d' % len(st))
    o.append('static const u32 bidi_start[BIDI_NRANGES] = {')
    o.append(',\n'.join('  ' + ', '.join('0x%X' % s for s, _ in st[i:i + 10]) for i in range(0, len(st), 10)) + ' };')
    o.append('static const u8 bidi_cls[BIDI_NRANGES] = {')
    o.append(',\n'.join('  ' + ', '.join('%d' % CLASSES.index(v) for _, v in st[i:i + 24]) for i in range(0, len(st), 24)) + ' };')
    # mirrored glyphs (BMP)
    mg, _ = table('Bmg')
    pairs = sorted((a, int(v, 16)) for a, b, v in mg if a == b and a < 0x10000 and int(v, 16) < 0x10000)
    o.append('#define BIDI_NMIRROR %d' % len(pairs))
    o.append('static const u16 bidi_mirtab[BIDI_NMIRROR][2] = {')
    o.append(',\n'.join('  ' + ', '.join('{ 0x%04X, 0x%04X }' % p for p in pairs[i:i + 6]) for i in range(0, len(pairs), 6)) + ' };')
    # paired brackets: opening ones and their closing partner
    bp, _ = table('Bpb'); bt, _ = table('Bpt')
    kind = {a: v for a, b, v in bt for a in range(a, b + 1)}
    opens = sorted((a, int(v, 16)) for a, b, v in bp if a == b and kind.get(a) == 'O' and a < 0x10000)
    o.append('#define BIDI_NBRACKET %d' % len(opens))
    o.append('static const u16 bidi_brtab[BIDI_NBRACKET][2] = {')
    o.append(',\n'.join('  ' + ', '.join('{ 0x%04X, 0x%04X }' % p for p in opens[i:i + 6]) for i in range(0, len(opens), 6)) + ' };')
    # joining types: U (none), R, L, D, C, T
    JT = ['U', 'R', 'L', 'D', 'C', 'T']
    jt, jmiss = table('Jt')
    js = ranges_to_starts([r for r in jt if r[0] < 0x10000], jmiss, 0x10000)
    o.append('#define BIDI_NJOIN %d' % len(js))
    o.append('static const u16 bidi_jstart[BIDI_NJOIN] = {')
    o.append(',\n'.join('  ' + ', '.join('0x%X' % s for s, _ in js[i:i + 12]) for i in range(0, len(js), 12)) + ' };')
    o.append('static const u8 bidi_jt[BIDI_NJOIN] = {')
    o.append(',\n'.join('  ' + ', '.join('%d' % JT.index(v) for _, v in js[i:i + 24]) for i in range(0, len(js), 24)) + ' };')
    # presentation forms: for each letter, its isolated, final, initial and medial forms (0: none)
    forms = {}
    ligs = []
    for cp in list(range(0xFB50, 0xFE00)) + list(range(0xFE70, 0xFF00)):
        d = unicodedata.decomposition(chr(cp))
        if not d.startswith('<'): continue
        tag, rest = d[1:].split('>', 1)
        parts = [int(x, 16) for x in rest.split()]
        idx = {'isolated': 0, 'final': 1, 'initial': 2, 'medial': 3}.get(tag)
        if idx is None: continue
        if len(parts) == 1:
            f = forms.setdefault(parts[0], [0, 0, 0, 0])
            if not f[idx]: f[idx] = cp
        elif len(parts) == 2 and parts[0] == 0x644 and parts[1] in (0x622, 0x623, 0x625, 0x627) and idx in (0, 1):
            ligs.append((parts[1], idx, cp))
    keys = sorted(k for k in forms if 0x600 <= k < 0x800)
    o.append('#define BIDI_NFORMS %d' % len(keys))
    o.append('static const u16 bidi_forms[BIDI_NFORMS][5] = {   /* letter, isolated, final, initial, medial */')
    o.append(',\n'.join('  ' + ', '.join('{ 0x%04X, 0x%04X, 0x%04X, 0x%04X, 0x%04X }' % ((k,) + tuple(forms[k])) for k in keys[i:i + 3]) for i in range(0, len(keys), 3)) + ' };')
    ligs.sort()
    o.append('#define BIDI_NLAMALEF %d' % len(ligs))
    o.append('static const u16 bidi_lamalef[BIDI_NLAMALEF][3] = {   /* the alef, isolated (0) or final (1), the ligature */')
    o.append('  ' + ', '.join('{ 0x%04X, %d, 0x%04X }' % l for l in ligs) + ' };')
    # East Asian width: wide and fullwidth ranges (W, F), for the character grid and line breaking
    ea, _ = table('Ea')
    wide = []
    for a, b, v in sorted(ea):
        if v not in ('W', 'F'): continue
        if wide and wide[-1][1] + 1 == a: wide[-1] = (wide[-1][0], b)
        else: wide.append((a, b))
    o.append('#define BIDI_NWIDE %d' % len(wide))
    o.append('static const u32 bidi_widetab[BIDI_NWIDE][2] = {')
    o.append(',\n'.join('  ' + ', '.join('{ 0x%X, 0x%X }' % p for p in wide[i:i + 6]) for i in range(0, len(wide), 6)) + ' };')
    print('\n'.join(o))

main()
