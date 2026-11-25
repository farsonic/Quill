#!/usr/bin/env python3
"""mac/mkwpgroups.py - the work packages' ribbon groups for Octavo's experimental ribbon tree.

Each amiga/wp_<name>.c gives the ribbon its own groups (wphooks.h: wpN_package.groups), in tables
written next to that package's Amiga code (its windows, its commands). The Mac compiles ribbon.c
itself, so it needs the same tables, and they are not to be copied by hand. This script reads each
wp_*.c, keeps what the tables are made of (the #defines, enums and typedefs, and the data the
groups reach: their items, lists, label arrays, icon strings) and drops the rest (the functions and
the data only they use), and writes build/.../gen/wpg_<name>.c with the package's own
wpN_package: its tag, its id block, its groups and menus, and no hooks (the commands are mapped on
the Mac by mac/qm_ribbon.c). The Amiga's sources stay the one place the ribbon is described.

  mkwpgroups.py OUTDIR amiga/wp_*.c      writes OUTDIR/wpg_*.c and OUTDIR/wpg_list.txt

Data named in EXPORTS (lists ribbon.c reaches through wp_charpara.h) is kept as well.
GPL-2.0-only, as Quill.
"""
import os
import re
import sys

EXPORTS = {'wp10_bullet_list', 'wp10_number_list', 'wp10_ptools_list', 'wp9_effects_list'}
IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')


def items(src):
    """The file's top-level items: ('pp', text) for preprocessor lines, ('decl', text) for the rest,
    each a declaration or definition ending at depth 0 with ';' or (a function) with '}'."""
    out, i, n, start = [], 0, len(src), 0
    depth, line_start = 0, True
    while i < n:
        c = src[i]
        if line_start and depth == 0 and src[i:].lstrip(' \t').startswith('#'):
            # a preprocessor line, with its continuations
            j = i
            while True:
                k = src.find('\n', j)
                if k < 0:
                    k = n
                if k > 0 and src[k - 1] == '\\':
                    j = k + 1
                    continue
                break
            pre = src[start:i]
            if pre.strip():
                out.append(('decl', pre))
            out.append(('pp', src[i:k]))
            i = start = k + 1
            line_start = True
            continue
        if c == '/' and src.startswith('/*', i):
            i = src.find('*/', i + 2) + 2
            continue
        if c == '/' and src.startswith('//', i):
            i = src.find('\n', i)
            continue
        if c in '"\'':
            j = i + 1
            while src[j] != c:
                j += 2 if src[j] == '\\' else 1
            i = j + 1
            line_start = False
            continue
        if c == '\n':
            line_start = True
            i += 1
            continue
        if not c.isspace():
            line_start = False
        if c in '{(':
            depth += 1
        elif c in '})':
            depth -= 1
            if depth == 0 and c == '}':
                # a function body ends here unless an initializer or a type goes on to ';'
                text = src[start:i + 1]
                head = strip_comments(text[:text.find('{')])
                if '(' in head and '=' not in head and not re.search(r'\b(struct|union|enum)\b[^;(]*$', head.split('(')[0]):
                    out.append(('func', text))
                    start = i + 1
        elif c == ';' and depth == 0:
            out.append(('decl', src[start:i + 1]))
            start = i + 1
        i += 1
    if src[start:].strip():
        out.append(('decl', src[start:]))
    return out


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    t = re.sub(r'"(?:\\.|[^"\\])*"', '""', t)
    return re.sub(r"'(?:\\.|[^'\\])*'", "''", t)


def declared_names(text):
    """The names a data declaration defines (before its '=' or ';'), e.g. 'static const Item a[] = ...'"""
    t = strip_comments(text)
    head = t.split('=')[0] if '=' in t else t.rstrip(';')
    names = []
    for part in head.split(','):
        m = re.findall(r'([A-Za-z_][A-Za-z_0-9]*)\s*(?:\[[^\]]*\]\s*)*$', part.strip())
        if m:
            names.append(m[-1])
    return names


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    made = []
    for path in sys.argv[2:]:
        src = open(path, encoding='latin-1').read()
        its = items(src)
        funcs = set()
        for kind, t in its:
            if kind == 'func':
                m = re.search(r'([A-Za-z_][A-Za-z_0-9]*)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*\{', strip_comments(t))
                if m:
                    funcs.add(m.group(1))
        data = {}            # name -> index of its declaration
        pkg = None
        for k, (kind, t) in enumerate(its):
            if kind != 'decl':
                continue
            s = strip_comments(t).strip()
            if re.match(r'(typedef\b|enum\b|struct\s+\w+\s*\{|union\b)', s) or not s:
                continue
            if re.search(r'\bWpPackage\s+wp\w+_package\s*=', s):
                pkg = k
                continue
            if '(' in s.split('=')[0] and '=' not in s:
                continue                                # a prototype
            for nm in declared_names(t):
                data[nm] = k
        if pkg is None:
            continue
        pkgname = re.search(r'\b(wp\w+_package)\b', its[pkg][1]).group(1)
        init = re.sub(r'/\*.*?\*/', ' ', its[pkg][1], flags=re.S)
        init = init[init.find('{') + 1:init.rfind('}')]
        fields = [f.strip() for f in init.split(',')]
        # positional: tag, first, last, command, state, field, ctxmenu, key, init, cleanup, groups, ngroups, menus, nmenus, drop
        while len(fields) < 15:
            fields.append('0')
        keep_fields = fields[:3] + ['NULL'] * 7 + fields[10:14] + ['NULL']
        roots = set()
        for f in (fields[10], fields[12]):
            roots.update(n for n in IDENT.findall(f) if n in data)
        roots.update(n for n in EXPORTS if n in data)
        # the data the roots reach, through each other
        keep, todo = set(), list(roots)
        while todo:
            nm = todo.pop()
            k = data[nm]
            if k in keep:
                continue
            keep.add(k)
            for ref in IDENT.findall(strip_comments(its[k][1])):
                if ref in data and data[ref] not in keep:
                    todo.append(ref)
        bad = [n for k in keep for n in IDENT.findall(strip_comments(its[k][1])) if n in funcs]
        if bad:
            sys.exit('%s: the ribbon data uses functions: %s' % (path, ', '.join(sorted(set(bad)))))
        base = os.path.splitext(os.path.basename(path))[0]
        out = ['/* Generated by mac/mkwpgroups.py from amiga/%s.c (its ribbon groups and menu rows, with no hooks):'
               ' do not edit. */' % base,
               '#include "qm_wpgen.h"']
        for k, (kind, t) in enumerate(its):
            if kind == 'pp':
                if re.match(r'\s*#\s*include\s*<', t):
                    continue                            # the system's: qm_wpgen.h has them
                out.append(t)
            elif kind == 'decl':
                s = strip_comments(t).strip()
                if k == pkg:
                    continue
                if k in keep or re.match(r'(typedef\b|enum\b|struct\s+\w+\s*\{|union\b)', s):
                    out.append(t.strip('\n'))
        out.append('const WpPackage %s = { %s };' % (pkgname, ', '.join(keep_fields)))
        open(os.path.join(outdir, 'wpg_%s.c' % base[3:]), 'w', encoding='latin-1').write('\n'.join(out) + '\n')
        made.append('wpg_%s' % base[3:])
    open(os.path.join(outdir, 'wpg_list.txt'), 'w').write('\n'.join(made) + '\n')


if __name__ == '__main__':
    main()
