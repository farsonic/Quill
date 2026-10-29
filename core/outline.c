/* outline.c - see outline.h (WP11) */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "outline.h"

int heading_style_level(const char *name)
{
  if (!strncmp(name, "heading ", 8) || !strncmp(name, "Heading ", 8)) return name[8] >= '1' && name[8] <= '9' && !name[9] ? name[8] - '0' : 0;
  return 0;
}
int outline_level(const Doc *d, u32 pi)
{
  int l;
  if (pi >= d->np) return 0;
  l = d->p[pi]->pf.outlvl;
  return l >= 1 && l <= 9 ? l : 0;
}
u32 outline_end(const Doc *d, u32 pi)
{
  int l = outline_level(d, pi);
  u32 i;
  if (pi >= d->np) return d->np;
  if (!l) return pi + 1;
  for (i = pi + 1; i < d->np; i++) { int k = outline_level(d, i); if (k && k <= l) break; }
  return i;
}
int outline_has_kids(const Doc *d, u32 pi)
{
  return outline_level(d, pi) && outline_end(d, pi) > pi + 1;
}
int outline_heading_style(Doc *d, int level)
{
  char id[16], name[16];
  int k, up;
  Style s;
  if (level < 1 || level > 9) return -1;
  sprintf(id, "Heading%d", level); sprintf(name, "heading %d", level);
  if ((k = doc_style_by_name(d, name)) >= 0 && d->styles[k].type == ST_PARA) return k;
  if ((k = doc_style(d, id)) >= 0 && d->styles[k].type == ST_PARA) return k;
  /* like the heading above it, a little smaller */
  up = level > 1 ? outline_heading_style(d, level - 1) : -1;
  s = d->styles[up >= 0 ? up : 0];
  strcpy(s.id, id); strcpy(s.name, name);
  s.builtin = 1; s.quick = 0; s.type = ST_PARA;
  s.cf.flags |= CF_BOLD;
  if (up >= 0 && s.cf.size > 22) s.cf.size = (u16)(s.cf.size - 2);
  if (level >= 4) s.cf.flags |= CF_ITALIC;
  s.pf.keepnext = 1;
  s.pf.list = 0; s.pf.level = 0;
  s.pf.outlvl = (u8)level;
  s.pf.style = (u8)d->nstyles;
  s.next = 0;
  return doc_add_style_from(d, &s, 0);
}
/* paragraph i to level lvl (see outline_set_level); 1 if it changed */
static int set_one(Doc *d, u32 i, int lvl)
{
  Para *p = d->p[i];
  const Style *st = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0];
  int direct = p->pf.outlvl != st->pf.outlvl, k;
  if (outline_level(d, i) == lvl) return 0;
  if (direct && !heading_style_level(st->name)) {
    ParaFmt pf = p->pf;
    pf.outlvl = (u8)lvl;
    return doc_para_format(d, i, i, PM_OUTLINE, &pf);
  }
  k = lvl ? outline_heading_style(d, lvl) : 0;
  if (k < 0) return 0;
  if (!doc_apply_style(d, i, i, k)) return 0;
  if (outline_level(d, i) != lvl) {                 /* (a Normal with an outline level of its own) */
    ParaFmt pf = d->p[i]->pf;
    pf.outlvl = (u8)lvl;
    doc_para_format(d, i, i, PM_OUTLINE, &pf);
  }
  return 1;
}
int outline_set_level(Doc *d, u32 pa, u32 pb, int level)
{
  u32 i;
  int n = 0;
  Pos at;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pa >= d->np || level < 0 || level > 9) return 0;
  if (pb >= d->np) pb = d->np - 1;
  at.para = pa; at.off = 0;
  doc_begin(d, at);
  for (i = pa; i <= pb; i++) n += set_one(d, i, level);
  doc_end(d, at);
  return n > 0;
}
int outline_promote(Doc *d, u32 pa, u32 pb, int dir)
{
  u32 i;
  int n = 0;
  Pos at;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pa >= d->np) return 0;
  if (pb >= d->np) pb = d->np - 1;
  at.para = pa; at.off = 0;
  doc_begin(d, at);
  for (i = pa; i <= pb; i++) {
    int l = outline_level(d, i), nl;
    if (l) nl = l + (dir < 0 ? -1 : 1);
    else if (dir < 0) {
      u32 k = i;
      nl = 1;
      while (k-- > 0) if (outline_level(d, k)) { nl = outline_level(d, k); break; }
    } else continue;
    if (nl < 1) nl = 1;
    if (nl > 9) nl = 9;
    n += set_one(d, i, nl);
  }
  doc_end(d, at);
  return n > 0;
}
int outline_move_to(Doc *d, u32 a, u32 b, u32 dest, u32 *newa)
{
  u32 lo, hi, n, k, o = 0, i;
  Para **nw;
  Pos where;
  u16 lastsect;
  if (a > b || b >= d->np || dest > d->np || (dest > a && dest <= b + 1)) return 0;
  lo = dest < a ? dest : a;
  hi = dest > b ? dest : b + 1;
  n = hi - lo;
  for (i = lo; i < hi; i++) if (in_table(&d->p[i]->pf)) return 0;
  if (!(nw = (Para **)calloc(n, sizeof *nw))) return 0;
  if (dest < a) {
    for (i = a; i <= b; i++) nw[o++] = d->p[i];
    for (i = dest; i < a; i++) nw[o++] = d->p[i];
  } else {
    for (i = b + 1; i < dest; i++) nw[o++] = d->p[i];
    for (i = a; i <= b; i++) nw[o++] = d->p[i];
  }
  /* copies in the new order (doc_replace takes them over; the old ones go into the undo history) */
  for (k = 0; k < n; k++) {
    Para *c = para_copy(nw[k]);
    if (!c) { while (k-- > 0) para_free(nw[k]); free(nw); return 0; }
    nw[k] = c;
  }
  /* the document's last paragraph holds the last section's format: it stays last */
  if (hi == d->np) {
    lastsect = d->p[d->np - 1]->pf.sect;
    for (k = 0; k < n; k++) if (nw[k]->pf.sect == lastsect && lastsect) nw[k]->pf.sect = 0;
    nw[n - 1]->pf.sect = lastsect;
  }
  where.para = lo; where.off = 0;
  if (!doc_replace(d, lo, n, nw, n, where)) { free(nw); return 0; }
  free(nw);
  if (newa) *newa = dest < a ? dest : dest - (b - a + 1);
  return 1;
}
int outline_move(Doc *d, u32 a, u32 b, int dir, const u8 *hidden, u32 *newa)
{
  u32 p;
  if (a > b || b >= d->np) return 0;
  if (dir < 0) {
    if (!a) return 0;
    p = a - 1;
    while (p > 0 && hidden && hidden[p]) p--;
    if (hidden && hidden[p]) return 0;
    return outline_move_to(d, a, b, p, newa);
  }
  p = b + 1;
  while (p < d->np && hidden && hidden[p]) p++;
  if (p >= d->np) return 0;
  p++;
  while (p < d->np && hidden && hidden[p]) p++;
  return outline_move_to(d, a, b, p, newa);
}
void outline_hidden(const Doc *d, Para *const *coll, u32 ncoll, int levels, u8 *out)
{
  u32 i, k;
  int region = 0;
  for (i = 0; i < d->np; i++) {
    int l = outline_level(d, i), eff = l ? l : 10;
    if (region && eff > region) { out[i] = 1; continue; }
    region = 0;
    out[i] = (u8)(levels > 0 && levels < 10 && eff > levels);
    if (l) for (k = 0; k < ncoll; k++) if (coll[k] == d->p[i]) { region = l; break; }
  }
}
