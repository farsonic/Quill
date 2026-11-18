#!/usr/bin/env python3
# ribbonfit.py - (WP26) whether every ribbon tab fits a 640-pixel window, in each interface language.
# A model of amiga/ribbon.c's layout_ribbon() (group widths, squeezing, the styles gallery, groups
# collapsed to one button from the right, the tab strip with the Quick Access Toolbar and the Tell Me
# box), measured with the real QuillSans 11 screen font (fonts-amiga/QuillSans/11), on the tabs as
# ribbon.c and the work packages (amiga/wp_*.c) define them: their sources are run through the C
# preprocessor (gcc -E, system headers left out) so their tables are read as the compiler sees them.
# On an Amiga the same check is SetEnv QUILLFIT 1 before starting Quill (PROGDIR:Quill.fit).
#   python3 tools/ribbonfit.py [ribbon width in pixels: 618, a 640 window's inside] [work dir]
# Keep it in step with layout_ribbon() when that changes. Exit status 1 if a tab overflows.
import os, re, struct, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDTH = int(sys.argv[1]) if len(sys.argv) > 1 else 618
ppdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.environ.get('TMPDIR', '/tmp'), 'quill-ribbonfit')
def preprocess():
    src = os.path.join(ppdir, 'src')
    for d in ('amiga', 'core'): os.makedirs(os.path.join(src, d), exist_ok=True)
    for d in ('amiga', 'core'):
        for fn in os.listdir(os.path.join(root, d)):
            if fn.endswith('.c') and d == 'core' or not (fn.endswith('.c') or fn.endswith('.h')): continue
            t = open(os.path.join(root, d, fn), encoding='latin-1').read()
            open(os.path.join(src, d, fn), 'w', encoding='latin-1').write(re.sub(r'(?m)^\s*#\s*include\s*<.*>', '', t))
    for fn in os.listdir(os.path.join(src, 'amiga')):
        if fn.endswith('.c') and (fn == 'ribbon.c' or fn.startswith('wp_')):
            subprocess.check_call(['gcc', '-E', '-P', '-I' + os.path.join(src, 'amiga'), os.path.join(src, 'amiga', fn),
                                   '-o', os.path.join(ppdir, fn[:-2] + '.i')])
preprocess()
sys.path.insert(0, os.path.join(root, 'tools'))
import mklang

# ---- the font
def load_font(path):
    b = open(path, 'rb').read()
    i = b.index(struct.pack('>I', 0x3E9 | 0x40000000)) if struct.pack('>I', 0x3E9 | 0x40000000) in b else b.index(struct.pack('>I', 0x3E9))
    n = struct.unpack('>I', b[i + 4:i + 8])[0]; d = b[i + 8:i + 8 + n * 4]
    p = 4 + 14 + 2 + 2 + 4 + 32 + 20
    ys, st, fl, xs, bl, bs, acc, lo, hi = struct.unpack('>HBBHHHHBB', d[p:p + 14]); p += 14
    cdata, mod, cloc, cspace, ckern = struct.unpack('>IHIII', d[p:p + 18])
    w = {}
    for c in range(lo, hi + 2):
        k = c - lo
        sp = struct.unpack('>h', d[cspace + 2 * k:cspace + 2 * k + 2])[0] if cspace else xs
        kn = struct.unpack('>h', d[ckern + 2 * k:ckern + 2 * k + 2])[0] if ckern else 0
        w[c] = sp + kn
    return w, hi
FW, FHI = load_font(os.path.join(root, 'fonts-amiga', 'QuillSans', '11'))
def tl(b):  # bytes (Latin-1)
    return sum(FW.get(c, FW.get(FHI + 1, 0)) for c in b)

# ---- translations: English bytes -> five columns of bytes
TR = {}
for sect, words in mklang.all_rows():
    for e in words:
        en = mklang.c_unescape(e[0])
        if en in TR or len(e) != 5: continue
        TR[en] = [en] + [mklang.c_unescape(t) if t is not None else en for t in e[1:]]
LANG = 0
def T(b):
    r = TR.get(b)
    return r[LANG] if r else b

# ---- C tokens
TOK = re.compile(r'\s+|("(?:\\.|[^"\\])*")|(\'(?:\\.|[^\'\\])*\')|([A-Za-z_]\w*)|(0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*)|(->|<<|>>|&&|\|\||[{}()\[\],;=&|+\-*/<>!~?:.%^])')
def tokens(src):
    out = []
    for m in TOK.finditer(src):
        if m.group(1): out.append(('s', m.group(1)))
        elif m.group(2): out.append(('c', m.group(2)))
        elif m.group(3): out.append(('i', m.group(3)))
        elif m.group(4): out.append(('n', m.group(4)))
        elif m.group(5): out.append(('p', m.group(5)))
    return out
def braced(tk, i):
    """tk[i] is '{': the nested list structure up to its '}' (lists of tokens split at top-level commas), and the index after"""
    assert tk[i] == ('p', '{')
    i += 1; items = []; cur = []
    while True:
        t = tk[i]
        if t == ('p', '{'):
            sub, i = braced(tk, i); cur.append(sub); continue
        if t == ('p', '}'):
            if cur: items.append(cur)
            return items, i + 1
        if t == ('p', ','):
            items.append(cur); cur = []; i += 1; continue
        cur.append(t); i += 1

ENUMS = {}
def enum_values(tk):
    i = 0
    while i < len(tk):
        if tk[i] == ('i', 'enum') and i + 1 < len(tk):
            j = i + 1
            if tk[j][0] == 'i': j += 1
            if tk[j] == ('p', '{'):
                body, j2 = braced(tk, j)
                v = 0
                for ent in body:
                    if not ent or ent[0][0] != 'i': continue
                    if len(ent) > 2 and ent[1] == ('p', '='):
                        try: v = cexpr(ent[2:])
                        except Exception: v = 0
                    ENUMS[ent[0][1]] = v; v += 1
                i = j2; continue
        i += 1
ARRLEN = {}
def cexpr(toks):
    s = ''
    raw = ' '.join(v for k, v in toks)
    m = re.search(r'sizeof\s*\(?\s*(\w+)\s*\)?\s*/\s*sizeof\s*\(?\s*\(?\s*\1\s*\)?\s*\[\s*0\s*\]\s*\)?', raw)
    if m: return ARRLEN[m.group(1)]
    for k, v in toks:
        if k == 'n': s += str(int(v.rstrip('uUlL'), 0))
        elif k == 'i':
            if v in ENUMS: s += str(ENUMS[v])
            elif v in ('NULL',): s += '0'
            elif v in ('int', 'long', 'ULONG', 'LONG', 'UWORD', 'u8'): s += ''
            else: raise ValueError(v)
        elif k == 'c': s += str(ord(mklang.c_unescape(v[1:-1]).decode('latin-1')))
        elif v == '||': s += ' or '
        elif v == '&&': s += ' and '
        elif v == '/': s += '//'
        else: s += v
    s = re.sub(r'\(\s*\)', '', s)
    return int(eval(s))
def cstr(toks):
    if all(k == 's' for k, _ in toks) and toks:
        return b''.join(mklang.c_unescape(v[1:-1]) for _, v in toks)
    return None

class Item:
    def __init__(s, f):
        g = lambda k: f[k] if k < len(f) else []
        s.kind = cexpr(g(0)); s.size = cexpr(g(1))
        s.icon = 0 if not g(2) or g(2) == [('n', '0')] else 1
        s.label = cstr(g(4))
        try: s.flags = cexpr(g(8)) if g(8) else 0
        except ValueError: s.flags = 0
        try: s.fieldw = cexpr(g(9)) if g(9) else 0
        except ValueError: s.fieldw = 0
        s.xicon = 1 if len(f) > 12 and cstr(g(12)) else 0
        s.help = cstr(g(10))
K_BUTTON, K_DROP, K_FIELD, K_GALLERY, K_LABEL = range(5)
S_SMALL, S_LARGE, S_ICON = range(3)
G_COLS, G_ROWS, G_GALLERY = range(3)
F_ROW1, F_NEWCOL = 1, 2

files = {}
for fn in sorted(os.listdir(ppdir)):
    if fn.endswith('.i'): files[fn[:-2]] = tokens(open(os.path.join(ppdir, fn), encoding='latin-1').read())
for tk in files.values(): enum_values(tk)

def U(e):
    return e[0] if len(e) == 1 and isinstance(e[0], list) else e
def decls(tk, typ):
    """static/const <typ> NAME[...] = { ... }: name -> parsed body"""
    out = {}
    for i in range(len(tk) - 3):
        if tk[i] == ('i', typ) and tk[i + 1][0] == 'i' and (tk[i - 1] == ('i', 'const') or tk[i - 1] == ('i', 'static')):
            j = i + 2; name = tk[i + 1][1]
            while j < len(tk) and tk[j] not in (('p', '='), ('p', ';')): j += 1
            if j < len(tk) and tk[j] == ('p', '=') and tk[j + 1] == ('p', '{'):
                body, _ = braced(tk, j + 1); out[name] = [U(e) for e in body]; ARRLEN[name] = len(body)
    return out

def group_of(f, itemsarr):
    name = cstr(f[0]); mode = cexpr(f[1])
    ref = f[2]; off = 0
    if ref and ref[0] == ('p', '&'):    # &x[k]
        nm = ref[1][1]; off = cexpr(ref[3:-1]) if len(ref) > 3 else 0
    else:
        nm = ref[0][1] if ref and ref[0][0] == 'i' else None
        if len(ref) == 3 and ref[1] == ('p', '+'): off = cexpr([ref[2]])
    arr = itemsarr.get(nm, [])
    try: n = cexpr(f[3])
    except Exception: n = len(arr) - off
    return (name, mode, arr[off:off + n])

def file_items(tk):
    ARRLEN.clear()
    for ty in ('Item', 'Group', 'WpGroup', 'WpMenu', 'Tab'): decls(tk, ty)
    return {k: [Item(e) for e in v] for k, v in decls(tk, 'Item').items()}

# ribbon.c's tabs
rtk = files['ribbon']
ritems = file_items(rtk)
rgroups = {k: [group_of(e, ritems) for e in v] for k, v in decls(rtk, 'Group').items()}
basetabs = []
for e in decls(rtk, 'Tab')['basetabs']:
    nm = cstr(e[0]); g = e[1][0][1] if e[1] and e[1][0][0] == 'i' and e[1][0][1] != 'NULL' else None
    basetabs.append([nm, list(rgroups.get(g, [])) if g else [], cexpr(e[3]) if e[3] else 0])
NT = len(basetabs)
# the packages' groups, in packages[] order
order = re.findall(r'&(wp\w+)_package', open(os.path.join(root, 'amiga', 'wphooks.c')).read())
pkgfile = {}
for fn, tk in files.items():
    for i in range(len(tk) - 3):
        if tk[i] == ('i', 'WpPackage') and tk[i + 1][0] == 'i' and tk[i + 1][1].endswith('_package') and tk[i + 2] == ('p', '='):
            body, _ = braced(tk, i + 3)
            body = [U(e) for e in body]
            pkgfile[tk[i + 1][1][:-8]] = (fn, body)
extra = {t: [] for t in range(NT)}     # tab -> list of (before, group)
for p in order:
    if p not in pkgfile: continue
    fn, body = pkgfile[p]
    if len(body) < 12 or not body[10] or body[10][0][0] != 'i' or body[10][0][1] == 'NULL': continue
    tk = files[fn]; its = file_items(tk); wg = decls(tk, 'WpGroup').get(body[10][0][1], [])
    ng = cexpr(body[11])
    for e in wg[:ng]:
        t = cexpr(e[0]); before = cexpr(e[1])
        extra[t].append((before, group_of(U(e[2]), its), p))
def put_group(gl, grp):
    # (ribbon.c's put_group) a group with the same caption as the one before joins it
    if gl and gl[-1][0] == grp[0] and gl[-1][1] == G_COLS and grp[1] == G_COLS:
        gl[-1] = (grp[0], G_COLS, list(gl[-1][2]) + list(grp[2]))
    else: gl.append(grp)
tabs = []
for t, (nm, own, ctx) in enumerate(basetabs):
    gl = []
    for g in range(len(own) + 1):
        for before, grp, p in extra[t]:
            at = len(own) if before < 0 or before > len(own) else before
            if at == g: put_group(gl, grp)
        if g < len(own): put_group(gl, own[g])
    tabs.append((nm, gl, ctx))

# ---- the layout (ribbon.c)
squeeze = 0
def bare(it): return squeeze and it.size == S_SMALL and (it.icon or it.xicon) and it.kind in (K_BUTTON, K_DROP)
def split_at(s):
    n = len(s); bw = tl(s); best = 0
    for i in range(1, n - 1):
        if s[i] == 32:
            a, b = tl(s[:i]), tl(s[i + 1:])
            if max(a, b) < bw: bw = max(a, b); best = i
    return best, bw
def item_w(it):
    lw = tl(T(it.label)) if it.label else 0
    if bare(it): return 22 + (9 if it.kind == K_DROP else 0)
    if squeeze and it.size == S_LARGE and it.label: lw = split_at(T(it.label))[1]
    if it.kind == K_LABEL: return lw + 6
    if it.kind == K_FIELD: return it.fieldw + (lw + 4 if it.label else 0)
    if it.size == S_LARGE: return max(lw, 30) + 10
    if it.size == S_ICON: return (20 if squeeze else 22) + (9 if it.kind == K_DROP else 0)
    return 16 + 6 + lw + 8 + (9 if it.kind == K_DROP else 0)
MAXG = 24
def group_widths(gs):
    gw = []
    for name, mode, items in gs[:MAXG]:
        cap = tl(T(name)) + 12; w = 0
        if mode == G_ROWS:
            r0 = sum(item_w(i) + 1 for i in items if not i.flags & F_ROW1); r1 = sum(item_w(i) + 1 for i in items if i.flags & F_ROW1)
            w = max(r0, r1) + 8
        elif mode == G_COLS:
            colw = 0; row = 0
            for it in items:
                if it.size == S_LARGE:
                    if colw: w += colw; colw = 0; row = 0
                    w += item_w(it) + 2; continue
                if row == 3 or it.flags & F_NEWCOL: w += colw + 2; colw = 0; row = 0
                colw = max(colw, item_w(it)); row += 1
            w += colw + 8
        if w < cap and not squeeze: w = cap
        gw.append(w)
    return gw
def group_button_w(gr):
    w = split_at(T(gr[0]))[1] if squeeze else tl(T(gr[0]))
    return max(w + 12, 40)
def layout(gs, W):
    global squeeze
    squeeze = 0
    gw = group_widths(gs); col = [0] * len(gw)
    gal = lambda: sum(56 - gw[g] for g in range(len(gw)) if gs[g][1] == G_GALLERY and not col[g])
    natural = sum(gw)
    if sum(gw) + gal() > W - 4: squeeze = 1; gw = group_widths(gs)
    squeezed = sum(gw)
    need = sum(gw) + gal()
    for g in range(len(gw) - 1, -1, -1):
        if need <= W - 4: break
        cw = group_button_w(gs[g]) + 8
        if gs[g][1] == G_GALLERY: need += cw - 56; gw[g] = cw; col[g] = 1; continue
        if cw >= gw[g]: continue
        need += cw - gw[g]; gw[g] = cw; col[g] = 1
    allbtn = 0
    if need > W - 4:
        for g in range(len(gw)):
            if gs[g][1] == G_GALLERY and not col[g]: gw[g] = group_button_w(gs[g]) + 8; col[g] = 1
        lo, hi = 30, 400
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if sum(min(w, mid) for w in gw) <= W - 4: lo = mid
            else: hi = mid - 1
        for g in range(len(gw)):
            if gw[g] > lo: gw[g] = lo; col[g] = 1
        allbtn = lo
    # squeezed with room left: groups narrower than their caption widened from the left
    if squeeze:
        slack = W - 4 - sum(gw) - gal()
        for g in range(len(gw)):
            if slack <= 0: break
            if col[g] or gs[g][1] == G_GALLERY: continue
            cap = tl(T(gs[g][0])) + 12
            if gw[g] < cap: add = min(cap - gw[g], slack); gw[g] += add; slack -= add
    cut = [gs[g][0].decode('latin-1') for g in range(len(gw)) if not col[g] and gs[g][1] != G_GALLERY and gw[g] < tl(T(gs[g][0])) + 4]
    # placing: the gallery takes the room left (or is its 56-pixel button)
    total = sum(gw); extra = W - total; x = 2
    for g in range(len(gw)):
        w = gw[g]
        if gs[g][1] == G_GALLERY and not col[g]:
            w = min(gw[g] + extra - 6, 7 * 66 + 30)
            if w < 2 * 66 + 30: w = 56
            extra -= w - gw[g]
        x += w
    nitems = sum(1 if col[g] else len(gs[g][2]) for g in range(len(gw)))
    return dict(natural=natural, squeezed=squeezed if squeeze else None, right=x - 1, ncol=sum(col), ngroups=len(gs), allbtn=allbtn,
                nitems=nitems, cut=cut, colnames=[gs[g][0].decode('latin-1') for g in range(len(gw)) if col[g]])

def strip(extra_ctx):
    # File and the main tabs (and contextual ones given), in what the Quick Access Toolbar (4) and the
    # Tell Me box leave: the padding round the names, and the cut (0: none)
    names = [tabs[0][0]] + [nm for nm, gl, ctx in tabs[1:] if gl and not ctx] + extra_ctx
    ws = [tl(T(n)) for n in names]; nv = len(ws)
    tell, qa = 1, 4
    while True:
        avail = WIDTH - 4 - (2 + 13 + 24 * qa + (24 if tell else 0))
        if sum(ws) + 6 * nv <= avail: break
        if tell: tell = 0; continue
        if sum(min(w, 36) + 6 for w in ws) <= avail: break
        if qa: qa -= 1
        else: break
    pad = 16; cap = 0
    if sum(ws) + nv * 16 > avail:
        pad = (avail - sum(ws)) // nv
        if pad < 6:
            pad = 6; lo, hi = 8, 400
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if sum(min(w, mid) + 6 for w in ws) <= avail: lo = mid
                else: hi = mid - 1
            cap = lo
    return sum(ws) + 16 * nv, avail, pad, cap, tell, qa
def strip_width():
    a = strip([]); b = strip([b'Table Design', b'Table Layout'])
    return 'natural %d, padding %d cut %d, Tell Me %d QA %d; in a table: natural %d, padding %d cut %d, Tell Me %d QA %d' % (a[0], a[2], a[3], a[4], a[5], b[0], b[2], b[3], b[4], b[5])

codes = ['en', 'de', 'fr', 'es', 'it']
over = 0
for LANG in range(5):
    print('--- %s (ribbon %d px)' % (codes[LANG], WIDTH))
    for t, (nm, gl, ctx) in enumerate(tabs):
        if t == 0 or not gl: continue
        r = layout(gl, WIDTH)
        ok = r['right'] <= WIDTH - 1
        if not ok: over += 1
        print('%-16s groups %2d natural %4d squeezed %4s -> %4d %s collapsed %d%s%s' % (
            nm.decode('latin-1'), r['ngroups'], r['natural'], r['squeezed'] if r['squeezed'] is not None else '-', r['right'] + 1,
            'fits' if ok else 'OVERFLOW', r['ncol'], (' (cut to %d)' % r['allbtn']) if r['allbtn'] else '',
            (' [' + ', '.join(r['colnames']) + ']') if r['colnames'] else ''))
        if r['cut']: print('   captions cut short:', ', '.join(r['cut']))
        caps = [T(g[0]) for g in gl]
        dup = sorted(set(c.decode('latin-1') for c in caps if caps.count(c) > 1))
        if dup: print('   two groups with the same caption:', ', '.join(dup)); over += 1
        if r['nitems'] > 128: print('   MAXI exceeded', r['nitems'])
        if r['ngroups'] > MAXG: print('   MAXG exceeded')
    print('tab strip:', strip_width())
print(over, 'overflowing')
sys.exit(1 if over else 0)
