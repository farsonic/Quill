#!/usr/bin/env python3
# recount the gap document's marks per section of part 1 (the ribbon) and print what differs from its headers and table
import re,sys
p=sys.argv[1]; s=open(p,encoding='utf-8').read()
lines=s.split('\n')
sec=None; counts={}; order=[]
part1=False
for ln in lines:
    if ln.startswith('## 1') or ln.startswith('## Part 1') : part1=True
    m=re.match(r'^### (.+?) \((\d+): ',ln)
    if m: sec=m.group(1); counts[sec]=[0,0,0,0]; order.append(sec); continue
    if ln.startswith('## '): sec=None
    if sec and ln.startswith('- '):
        k='✅🟨⬜➖'.find(ln[2]) if len(ln)>2 else -1
        if k>=0: counts[sec][k]+=1
for sname in order: print(sname, counts[sname])
if len(sys.argv) > 2 and sys.argv[2] == '--write':
    out=[]; T=[0,0,0,0]; seen_all=0
    for sname in order:
        c=counts[sname]; n=sum(c); T=[T[i]+c[i] for i in range(4)]
    for ln in lines:
        m=re.match(r'^### (.+?) \((\d+): ✅ \d+ · 🟨 \d+ · ⬜ \d+ · ➖ \d+\)$',ln)
        if m and m.group(1) in counts:
            c=counts[m.group(1)]; ln='### %s (%d: ✅ %d · 🟨 %d · ⬜ %d · ➖ %d)'%(m.group(1),sum(c),c[0],c[1],c[2],c[3])
        m=re.match(r'^\| (.+?) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \|$',ln)
        if m and m.group(1) in counts:
            c=counts[m.group(1)]; ln='| %s | %d | %d | %d | %d | %d |'%(m.group(1),sum(c),c[0],c[1],c[2],c[3])
        if ln.startswith('| **All** |') and 'Commands' not in ln and seen_all == 0:
            seen_all = 1
            ln='| **All** | **%d** | **%d** | **%d** | **%d** | **%d** |'%(sum(T),T[0],T[1],T[2],T[3])
        if ln.startswith('- **Ribbon commands:**'):
            app=sum(T)-T[3]
            ln=('- **Ribbon commands:** %d in WordCraft (its catalogue, the Zotero and Help tabs plus the Paragraph Tools and Markdown Source of its ribbon), %d of them applicable to Quill. '
                'Quill has **%d** (✅), **%d** partly (🟨) and lacks **%d** (⬜); %d are ➖. So Quill covers %d%% of the applicable commands fully and %d%% at least in part.'
                %(sum(T),app,T[0],T[1],T[2],T[3],round(100*T[0]/app),round(100*(T[0]+T[1])/app)))
        out.append(ln)
    open(p,'w',encoding='utf-8').write('\n'.join(out))
    print('written', T)
