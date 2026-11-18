/* toc.c - see toc.h */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "toc.h"
#include "refsi.h"

/* 1 if p's style is named "toc N" or "TOC N" (N a digit); an out-of-range style index counts
   as style 0, as elsewhere in the core */
int toc_para(Doc *d, const Para *p)
{
  const Style *st = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0];
  return !strncmp(st->name, "toc ", 4) || !strncmp(st->name, "TOC ", 4) ? (st->name[4] >= '1' && st->name[4] <= '9') : 0;
}
/* (WP2) the table of contents is a TOC field whose result is its entries (core/refs.h): updating
   builds them afresh. Quill's older table, a run of "toc N" paragraphs with no field round it (as
   documents made before WP2 hold in memory), is taken over: the run becomes the field's result. */
static const char *const def_code = " TOC \\o \"1-3\" \\h \\z \\u ";
/* the old kind of table: a run of toc paragraphs outside any block field; its first and last */
static int old_table(Doc *d, u32 *a, u32 *b)
{
  u32 i;
  u8 *mask = ri_block_mask(d);
  for (i = 0; i < d->np; i++) if (toc_para(d, d->p[i]) && !(mask && mask[i])) break;
  free(mask);
  if (i == d->np) return 0;
  *a = *b = i;
  while (*b + 1 < d->np && toc_para(d, d->p[*b + 1])) (*b)++;
  return 1;
}
int doc_toc(Doc *d, Layout *L, Pos *at, int *again)
{
  Pos st, en, w;
  u32 a, b;
  int r;
  if (again) *again = 0;
  if (refs_block_find(d, BK_TOC, 0, &st, &en) >= 0) {
    r = refs_block_update(d, st, &en, L, -1);
    if (at && r) { at->para = en.para + 1 < d->np ? en.para + 1 : en.para; at->off = 0; }
    return r;
  }
  if (old_table(d, &a, &b)) {
    /* the old table's paragraphs give way to the field (its code as read, else Word's usual one) */
    const char *code = d->finfo && d->finfo->toccode ? d->finfo->toccode : def_code;
    int k = field_new(d, code, 0);
    Para *p;
    char m[8];
    if (k < 0 || !(p = para_new(&d->p[a]->pf, &d->p[a]->r[0].f))) return 0;
    fmark_utf8(m, k, 0); para_append(p, m, 4, &d->p[a]->r[0].f);
    fmark_utf8(m, k, 1); para_append(p, m, 4, &d->p[a]->r[0].f);
    p->pf.sect = d->p[b]->pf.sect;
    w.para = a; w.off = 0;
    doc_begin(d, w);
    if (!doc_replace(d, a, b - a + 1, &p, 1, w)) { doc_end(d, w); return 0; }
    st.para = a; st.off = 0; en = st; en.off = 4;
    r = refs_block_update(d, st, &en, L, -1);
    doc_end(d, w);
    if (at && r) { at->para = en.para + 1 < d->np ? en.para + 1 : en.para; at->off = 0; }
    return r;
  }
  w = at ? *at : doc_start(d);
  r = refs_block_insert(d, &w, d->finfo && d->finfo->toccode ? d->finfo->toccode : def_code, refs_word(refs_lang, RW_CONTENTS), L, -1) >= 0;
  if (at && r) { at->para = w.para + 1 < d->np ? w.para + 1 : w.para; at->off = 0; }
  return r;
}
/* see toc.h: the TOC field with its paragraphs and title, or the old run of "toc N" paragraphs */
int doc_toc_remove(Doc *d, Pos *at)
{
  u32 a, b, n;
  Pos st, en, where;
  Para *blank[1];
  if (refs_block_find(d, BK_TOC, 0, &st, &en) >= 0) {
    u32 p0 = st.para;
    if (st.off == 0 && p0 > 0 && !strcmp(d->styles[d->p[p0 - 1]->pf.style < d->nstyles ? d->p[p0 - 1]->pf.style : 0].name, "TOC Heading")) p0--;   /* (its title goes too) */
    if (!refs_block_remove(d, st)) return 0;
    if (at) { at->para = p0 < d->np ? p0 : d->np - 1; at->off = 0; }
    return 1;
  }
  if (!old_table(d, &a, &b)) return 0;
  n = b - a + 1;
  if (a > 0 && !strcmp(d->styles[d->p[a - 1]->pf.style < d->nstyles ? d->p[a - 1]->pf.style : 0].name, "TOC Heading")) { a--; n++; }
  where.para = a; where.off = 0;
  if (n == d->np) {
    if (!(blank[0] = para_new(&d->styles[0].pf, &d->styles[0].cf))) return 0;
    if (!doc_replace(d, a, n, blank, 1, where)) return 0;
  } else if (!doc_replace(d, a, n, NULL, 0, where)) return 0;
  if (at) { at->para = a < d->np ? a : d->np - 1; at->off = 0; }
  return 1;
}
