/*
 * paratools.c - Home > Paragraph > Paragraph Tools: see paratools.h. Every tool works on the
 * paragraphs last first, so an edit only moves the paragraphs (and offsets) after it, and runs as
 * one undo step (doc_begin/doc_end round the edits).
 */
#include <stdlib.h>
#include <string.h>
#include "paratools.h"

/* the character at off is shown (not marked deleted) */
static int live(Para *p, u32 off) { return para_fmt_at(p, off)->rev != REV_DEL; }
/* a space's width in p (a quarter of an em of its first character's font) */
static i32 space_w(Para *p)
{
  i32 w = (i32)para_fmt_at(p, 0)->size * 5 / 2;      /* half-points * 10 twips / 4 */
  return w > 0 ? w : 60;
}
/* a..b of paragraph pi deleted (marked deleted while tracking); 1 if anything changed */
static int del(Doc *d, u32 pi, u32 a, u32 b, int rid)
{
  Pos x, y;
  if (a >= b) return 0;
  x.para = y.para = pi; x.off = a; y.off = b;
  return rid >= 0 ? doc_track_delete(d, x, y, rid) : doc_delete(d, x, y);
}
/* the ranges [r[2k], r[2k+1]) of paragraph pi deleted, last first; how many changed */
static int del_ranges(Doc *d, u32 pi, const u32 *r, int n, int rid)
{
  int k, any = 0;
  for (k = n - 1; k >= 0; k--) any += del(d, pi, r[2 * k], r[2 * k + 1], rid);
  return any;
}
/* a list of ranges (grown as needed): adds [a, b), joining it to the last when they touch */
typedef struct { u32 *r; int n, cap; int oom; } Ranges;
static void add_range(Ranges *R, u32 a, u32 b)
{
  if (R->n && R->r[2 * R->n - 1] == a) { R->r[2 * R->n - 1] = b; return; }
  if (R->n == R->cap) {
    int nc = R->cap ? R->cap * 2 : 16;
    u32 *nr = (u32 *)realloc(R->r, (size_t)nc * 2 * sizeof(u32));
    if (!nr) { R->oom = 1; return; }
    R->r = nr; R->cap = nc;
  }
  R->r[2 * R->n] = a; R->r[2 * R->n + 1] = b; R->n++;
}
/* the live line breaks of p */
static void breaks(Para *p, Ranges *R)
{
  u32 i;
  for (i = 0; i < p->len; i++) if (p->t[i] == '\v' && live(p, i)) add_range(R, i, i + 1);
}
/* the live spaces at p's start (deleted text among them passed over) */
static void leading(Para *p, Ranges *R)
{
  u32 i;
  for (i = 0; i < p->len; i = (u32)utf8_next(p->t, p->len, i)) {
    if (!live(p, i)) continue;
    if (p->t[i] != ' ') break;
    add_range(R, i, i + 1);
  }
}
static int count_spaces(const Ranges *R)
{
  int k, n = 0;
  for (k = 0; k < R->n; k++) n += (int)(R->r[2 * k + 1] - R->r[2 * k]);
  return n;
}
/* the spaces that go when every run of live spaces after the first other character becomes one: a
   run inside the text keeps its last space, one that reaches the end its first */
static void extra_spaces(Para *p, Ranges *R)
{
  u32 i, *run = NULL;
  int n = 0, cap = 0, started = 0, k;
  for (i = 0; i < p->len; i = (u32)utf8_next(p->t, p->len, i)) {
    if (!live(p, i)) continue;
    if (p->t[i] == ' ') {
      if (!started) continue;
      if (n == cap) {
        u32 *nr = (u32 *)realloc(run, (size_t)(cap = cap ? cap * 2 : 16) * sizeof(u32));
        if (!nr) { R->oom = 1; free(run); return; }
        run = nr;
      }
      run[n++] = i;
      continue;
    }
    for (k = 0; k + 1 < n; k++) add_range(R, run[k], run[k] + 1);
    n = 0; started = 1;
  }
  for (k = 1; k < n; k++) add_range(R, run[k], run[k] + 1);
  free(run);
}
/* an empty paragraph made after paragraph i (like it, or like the next one when i ends a section;
   no list, breaks or drop cap), through doc_replace; 0 if out of memory */
static int blank_after(Doc *d, u32 i)
{
  const Para *like = d->p[i]->pf.sect && i + 1 < d->np ? d->p[i + 1] : d->p[i];
  ParaFmt pf = like->pf;
  CharFmt cf = d->p[i]->r[d->p[i]->nr - 1].f;
  Para *np;
  Pos where;
  pf.sect = 0; pf.pagebreak = 0; pf.colbreak = 0; pf.dropcap = 0; pf.dropmargin = 0;
  pf.list = 0; pf.level = 0; pf.prev = 0;
  cf.rev = REV_NONE; cf.rid = 0; cf.frev = 0; cf.move = 0; cf.link = 0; cf.bmark = 0;
  if (!(np = para_new(&pf, &cf))) return 0;
  where.para = i; where.off = d->p[i]->len;
  return doc_replace(d, i + 1, 0, &np, 1, where);
}
/* paragraph i may take an empty paragraph after it: outside tables, or with the next in its cell */
static int may_add_after(Doc *d, u32 i)
{
  if (!in_table(&d->p[i]->pf)) return 1;
  return i + 1 < d->np && same_cell(&d->p[i]->pf, &d->p[i + 1]->pf);
}

int para_tool(Doc *d, int tool, u32 pa, u32 pb, int rid, Pos *caret)
{
  Pos start;
  u32 i;
  int any = 0, k;
  if (!d || !d->np || tool < 0 || tool >= PT_N) return 0;
  if (pb >= d->np) pb = d->np - 1;
  if (pa > pb) return 0;
  if (pb - pa + 1 > PT_MAXPARAS) return 0;
  start.para = pa; start.off = 0;
  doc_begin(d, caret ? *caret : start);
  for (i = pb + 1; i-- > pa; ) {
    Para *p = d->p[i];
    Ranges R;
    memset(&R, 0, sizeof R);
    switch (tool) {
    case PT_SPLITLINES:
      breaks(p, &R);
      for (k = R.n - 1; k >= 0 && !R.oom; k--) {
        u32 at, len;
        Pos x;
        /* one break at a time (a run of them: each), the last first */
        for (at = R.r[2 * k + 1]; at-- > R.r[2 * k]; ) {
          len = d->p[i]->len;
          if (!del(d, i, at, at + 1, rid)) continue;
          x.para = i; x.off = d->p[i]->len == len ? at + 1 : at;   /* marked deleted (still there) or gone */
          if (doc_insert(d, &x, "\n", 1, NULL)) any++;
        }
      }
      break;
    case PT_BLANKBETWEEN: case PT_SEPARATE:
      if ((i < pb || (tool == PT_SEPARATE)) && may_add_after(d, i) && blank_after(d, i)) any++;
      break;
    case PT_REMOVEBLANK:
      if (!p->len && !p->pf.sect && !p->pf.pagebreak && !p->pf.colbreak && i + 1 < d->np && same_cell(&p->pf, &d->p[i + 1]->pf)) {
        Pos where;
        where.para = i; where.off = 0;
        if (doc_replace(d, i, 1, NULL, 0, where)) any++;
      }
      break;
    case PT_TRIMLEADING: leading(p, &R); any += del_ranges(d, i, R.r, R.n, rid); break;
    case PT_EXTRASPACES: extra_spaces(p, &R); any += del_ranges(d, i, R.r, R.n, rid); break;
    case PT_JOINLINES: breaks(p, &R); any += del_ranges(d, i, R.r, R.n, rid); break;
    case PT_SPACESTOINDENT: {
      int n;
      leading(p, &R);
      if ((n = count_spaces(&R)) > 0) {
        ParaFmt pf = p->pf;
        i32 w = space_w(p), room = 31680 - pf.first;      /* (at most 22 inches in) */
        if (room < 0) room = 0;
        if ((i32)n * w > room) {
          /* only as many spaces as the indent has room for go (the last of them) */
          int keep = n - (int)(room / w);
          while (keep > 0 && R.n) {
            u32 len = R.r[1] - R.r[0];
            if ((int)len <= keep) { keep -= (int)len; memmove(R.r, R.r + 2, (size_t)(R.n - 1) * 2 * sizeof(u32)); R.n--; }
            else { R.r[0] += (u32)keep; keep = 0; }
          }
          n = count_spaces(&R);
        }
        if (n > 0) {
          pf.first += (i32)n * w;
          doc_para_format(d, i, i, PM_INDENT, &pf);
          any += del_ranges(d, i, R.r, R.n, rid);
        }
      }
      break; }
    case PT_INDENTTOSPACES:
      if (!p->pf.list && (p->pf.left || p->pf.first)) {
        ParaFmt pf = p->pf;
        i32 w = space_w(p), tw = pf.left + pf.first;
        int n = tw > 0 ? (int)((tw + w / 2) / w) : 0;
        if (n > 1000) n = 1000;
        pf.left = 0; pf.first = 0;
        doc_para_format(d, i, i, PM_INDENT, &pf);
        any++;
        if (n > 0) {
          char *sp = (char *)malloc((size_t)n);
          CharFmt f = *para_fmt_at(d->p[i], 0);
          Pos x;
          if (sp) {
            memset(sp, ' ', (size_t)n);
            if (rid >= 0) { f.rev = REV_INS; f.rid = (u16)rid; } else { f.rev = REV_NONE; f.rid = 0; }
            f.frev = 0; f.move = 0;
            x.para = i; x.off = 0;
            doc_insert(d, &x, sp, (u32)n, &f);
            free(sp);
          }
        }
      }
      break;
    }
    free(R.r);
  }
  if (caret) *caret = pos_clamp(d, *caret);
  doc_end(d, caret ? *caret : start);
  return any;
}
