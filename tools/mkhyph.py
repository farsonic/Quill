#!/usr/bin/env python3
# mkhyph.py - (WP31) makes Hyphen .dic pattern files (what core/hyph.c reads) from the hyph-utf8 TeX
# pattern files (github.com/hyphenation/tex-hyphen, hyph-utf8/tex/generic/hyph-utf8/patterns/tex/).
# Usage: python3 tools/mkhyph.py hyph-de-1996.tex dict/hyph_de_DE.dic
# The output starts "UTF-8", then LEFTHYPHENMIN/RIGHTHYPHENMIN from the file's hyphenmins, then the
# patterns one a line; the \hyphenation exceptions become patterns of their own (9 at each hyphen,
# 8 between the other letters, so they win over everything). The file's header comment (title,
# copyright, licence) is copied to the top of the output as Hyphen's "%" comment lines are not
# allowed, so it goes to <output>.txt instead.
import re, sys

src, out = sys.argv[1], sys.argv[2]
text = open(src, encoding='utf-8').read()
header = []
for line in text.split('\n'):
    if line.startswith('%'): header.append(line[1:].rstrip())
    elif line.strip(): break
left = right = 2
m = re.search(r'hyphenmins:\s*\n%\s*typesetting:\s*\n%\s*left:\s*(\d+)\s*\n%\s*right:\s*(\d+)', text)
if m: left, right = int(m.group(1)), int(m.group(2))
def block(name):
    m = re.search(r'\\' + name + r'\s*\{(.*?)\}', text, re.S)
    if not m: return []
    body = re.sub(r'%[^\n]*', '', m.group(1))
    return body.split()
pats = block('patterns')
exc = []
for w in block('hyphenation'):
    w = w.lower()
    letters = w.replace('-', '')
    p = '.'
    for i, ch in enumerate(w):
        if ch == '-': continue
        p += ch
        nxt = w[i + 1] if i + 1 < len(w) else ''
        if nxt == '-': p += '9'
        elif nxt: p += '8'
    exc.append(p + '.')
with open(out, 'w', encoding='utf-8') as f:
    f.write('UTF-8\nLEFTHYPHENMIN %d\nRIGHTHYPHENMIN %d\n' % (left, right))
    for p in pats + exc: f.write(p + '\n')
with open(out[:-4] + '.txt', 'w', encoding='utf-8') as f:
    f.write('%s - made by Quill\'s tools/mkhyph.py from %s (hyph-utf8, github.com/hyphenation/tex-hyphen).\n' % (out.split('/')[-1], src.split('/')[-1]))
    f.write('Its header, with the copyright and licence:\n\n')
    f.write('\n'.join(header) + '\n')
print(out, len(pats), 'patterns,', len(exc), 'exceptions, left', left, 'right', right)
