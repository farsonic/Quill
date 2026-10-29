#!/usr/bin/env python3
# mkthes.py - (WP8) makes dict/th_en.dat, Quill's English thesaurus, from LibreOffice's th_en_US_v2.dat (a MyThes
# thesaurus made from Princeton's WordNet; github.com/LibreOffice/dictionaries, en/). Usage:
#   python3 tools/mkthes.py th_en_US_v2.dat dict/british-english dict/th_en.dat
# trims LibreOffice's th_en_US_v2.dat (WordNet-derived MyThes) to single-word headwords that are in
# Quill's British English list; at most 6 meanings, 8 terms each; antonyms dropped, tags stripped
import sys, re
src, words, out = sys.argv[1:4]
known = set(w.strip().lower() for w in open(words, encoding='utf-8'))
lines = open(src, encoding='utf-8').read().split('\n')
res = []
i = 1
while i < len(lines):
    l = lines[i]
    if not l or '|' not in l: i += 1; continue
    head, cnt = l.rsplit('|', 1)
    try: n = int(cnt)
    except: i += 1; continue
    means = lines[i+1:i+1+n]; i += 1 + n
    if not re.fullmatch(r"[a-z][a-z'-]*", head) or head not in known: continue
    keep = []
    for m in means:
        parts = m.split('|')
        pos = parts[0]
        terms = []
        for t in parts[1:]:
            if '(antonym)' in t: continue
            t = re.sub(r'\s*\((generic|similar|related) term\)', '', t).strip()
            if not t or t.lower() == head or t in terms: continue
            if len(t) > 24 or t.count(" ") > 1: continue
            terms.append(t)
        if terms:
            keep.append(pos + '|' + '|'.join(terms[:6]))
        if len(keep) >= 4: break
    if keep:
        res.append(head + '|' + str(len(keep)))
        res.extend(keep)
ents=[]
for r in res:
    if not r.startswith('('): ents.append([r])
    else: ents[-1].append(r)
ents.sort(key=lambda e: e[0].split('|')[0].encode())
res=[l for e in ents for l in e]
open(out, 'w', encoding='utf-8').write('UTF-8\n' + '\n'.join(res) + '\n')
print(len([r for r in res if not r.startswith('(')]), 'headwords')
