#!/usr/bin/env python3
# langcheck.py - lists the interface strings in Quill's Amiga source that have no row in the language
# table (tools/mklang.py and tools/lang/wp*.py), so tr() would show them in English. Adapted from
# Ledger's tools/langcheck.py.
#
#   python3 tools/langcheck.py                     the work packages' files (amiga/wp_*.c, wphooks.c)
#   python3 tools/langcheck.py amiga/wp_fields.c   just these files
#   python3 tools/langcheck.py --all               every amiga/*.c and *.h (the older files have many
#                                                  strings that aren't words for people: expect noise)
#   ... --rows                                     the missing ones as rows to paste into your
#                                                  tools/lang/wp<n>.py (English in every column)
#
# A string literal counts as interface words unless it is a row's English, in tools/lang/ignore.txt,
# plainly not words (icon drawings, numbers, format strings alone, NAMES_IN_CAPS, file names and
# paths), or an argument of a call that never shows its text (printf, strcmp, MAKE_ID ...). Each row's
# translations must keep the English's printf conversions (%s %d ...) in order and its '|' buttons.
# Exit status 1 if anything is missing or wrong.
import glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklang

ROOT = mklang.ROOT
IGNORE = os.path.join(ROOT, 'tools', 'lang', 'ignore.txt')
QUIET_CALLS = {'printf', 'fprintf', 'sprintf', 'snprintf_log', 'sscanf', 'strcmp', 'strncmp', 'strstr', 'strchr', 'strrchr',
               'Stricmp', 'Strnicmp', 'str_ieq', 'fopen', 'Open', 'Lock', 'OpenLibrary', 'OpenDevice', 'FindPort', 'MAKE_ID',
               'OpenDiskFont', 'mm_log', '__asm__', 'volatile', 'GetVar', 'SetVar', 'DeleteFile', 'Rename', 'CreateDir', 'MatchPattern', 'ParsePattern',
               'ParsePatternNoCase', 'AddPart', 'fputs', 'theme_open_font', 'hyph_load', 'qlogf'}
FMT = re.compile(r'%(?:-?[0-9]*(?:\.[0-9]+)?|\.\*|-?\*)(?:hh|h|ll|l)?[diouxXeEfgGcs%]')

def literals(path):
    """the string literals of a C file: (line, start offset, raw text), adjacent ones joined"""
    src = open(path, encoding='latin-1').read()
    out, i, n, line, cur = [], 0, len(src), 1, None
    while i < n:
        c = src[i]
        if c == '\n': line += 1; i += 1; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j; line += src.count('\n', i, j); i = j + 2; continue
        if src.startswith('//', i): j = src.find('\n', i); i = n if j < 0 else j; continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'": j += 2 if src[j] == '\\' else 1
            i = j + 1
            if cur: out.append(cur); cur = None
            continue
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"': j += 2 if src[j] == '\\' else 1
            if cur: cur = (cur[0], cur[1], cur[2] + src[i + 1:j])
            else: cur = (line, i, src[i + 1:j])
            i = j + 1; continue
        if c in ' \t\r': i += 1; continue
        if cur: out.append(cur); cur = None
        if src.startswith('#include', i): j = src.find('\n', i); i = n if j < 0 else j; continue
        i += 1
    if cur: out.append(cur)
    return src, out

def enclosing_call(src, pos):
    """the name of the call whose parentheses hold position pos ('' if none)"""
    depth, i, lim = 0, pos - 1, max(0, pos - 3000)
    while i > lim:
        c = src[i]
        if c == ')': depth += 1
        elif c == '(':
            if depth == 0:
                j = i - 1
                while j >= 0 and src[j] in ' \t': j -= 1
                k = j
                while k >= 0 and (src[k].isalnum() or src[k] == '_'): k -= 1
                return src[k + 1:j + 1]
            depth -= 1
        elif c in ';{}' and depth == 0: return ''
        i -= 1
    return ''

ICON = re.compile(r'^(?:[A-Za-z]{1,2}|-?\d+(?:\.\d+)?)(?: (?:[A-Za-z]{1,2}|-?\d+(?:\.\d+)?))*$')
def not_words(t):
    s = FMT.sub(' ', t)
    if not re.search(r'[A-Za-z\300-\377]{2}', s): return True                 # numbers, signs, conversions
    if ICON.match(t) and re.search(r'\d', t): return True                      # an icon's drawing (theme.c's language)
    if re.fullmatch(r'[A-Z][A-Z0-9_.]*[A-Z0-9]?', t) and len(t) >= 3: return True   # NAMES, IDs
    if ' ' not in t and re.fullmatch(r'[A-Za-z0-9_]+:\S*', t): return True     # PROGDIR:file
    if ' ' not in t and re.search(r'\.[a-z]{2,9}$', t) and not t.endswith('...'): return True   # file names
    if ' ' not in t and re.fullmatch(r'[a-z0-9_/.\-#?|()]+', t): return True   # paths, ids, patterns
    if t.startswith('WP') and re.fullmatch(r'WP\d+[a-d]?', t): return True    # a package's tag
    return False

def read_ignore():
    ign = set()
    if os.path.exists(IGNORE):
        for line in open(IGNORE, encoding='utf-8'):
            line = line.rstrip('\n')
            if line and not line.startswith('#'): ign.add(line)
    return ign

def table():
    """English -> (where, [the five columns]) for every row of the table"""
    rows = {}
    for sect, words in mklang.all_rows():
        for e in words:
            try: cols = [mklang.c_unescape(c).decode('latin-1') if c is not None else None for c in e]
            except Exception: continue
            rows.setdefault(cols[0], ('tools/lang/wp%s.py' % sect if sect else 'tools/mklang.py', cols))
    return rows

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if '--all' in sys.argv: files = sorted(glob.glob(os.path.join(ROOT, 'amiga', '*.c')) + glob.glob(os.path.join(ROOT, 'amiga', '*.h')))
    elif args: files = [os.path.join(ROOT, a) if not os.path.isabs(a) else a for a in args]
    else: files = sorted(glob.glob(os.path.join(ROOT, 'amiga', 'wp_*.c'))) + [os.path.join(ROOT, 'amiga', 'wphooks.c')]
    files = [f for f in files if not f.endswith('lang_table.h')]
    rows, ign = table(), read_ignore()
    missing = {}
    for path in files:
        rel = os.path.relpath(path, ROOT)
        src, lits = literals(path)
        for line, pos, raw in lits:
            try: t = mklang.c_unescape(raw).decode('latin-1')
            except Exception: continue
            if not t or t in rows or t in ign or not_words(t): continue
            if enclosing_call(src, pos) in QUIET_CALLS: continue
            if re.search(r'MUIA_(?:FixWidthTxt|FixHeightTxt|String_Accept|String_Reject|List_Format)\s*,\s*(?:\(ULONG\)\s*)?$', src[max(0, pos - 60):pos]): continue
            missing.setdefault(t, []).append('%s:%d' % (rel, line))
    probs = []
    for en, (where, v) in rows.items():
        fe = FMT.findall(en)
        for k, t in enumerate(v[1:], 1):
            if t is None: continue
            lang = 'de fr es it'.split()[k - 1]
            if FMT.findall(t) != fe: probs.append('%s: "%s" %s: conversions %s, not %s' % (where, en, lang, FMT.findall(t), fe))
            if en.count('|') != t.count('|'): probs.append('%s: "%s" %s: %d \'|\', not %d' % (where, en, lang, t.count('|'), en.count('|')))
    if '--rows' in sys.argv:
        for t in missing:
            e = t.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
            print('("%s", "%s", "%s", "%s", "%s"),' % ((e,) * 5))
        return 1 if missing else 0
    for t, locs in sorted(missing.items(), key=lambda x: x[1][0]):
        print('%s: not in the table: "%s"' % (locs[0], t.replace('\n', '\\n')))
    for p in probs: print(p)
    print('langcheck: %d files, %d rows, %d strings missing, %d row problems' % (len(files), len(rows), len(missing), len(probs)))
    return 1 if missing or probs else 0

if __name__ == '__main__':
    sys.exit(main())
