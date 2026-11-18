/*
 * fitpage.c - Fit One Page: see fitpage.h.
 */
#include <stdlib.h>
#include <string.h>
#include "fitpage.h"
#include "layout.h"

/* the steps: text this many half-points smaller, spacing to this many per cent */
static const struct { u8 shrink, pct; } steps[] = {
  { 0, 94 }, { 0, 92 }, { 0, 90 }, { 0, 88 }, { 1, 96 }, { 1, 90 }, { 2, 96 }, { 2, 90 }, { 2, 85 }, { 4, 88 }
};
#define MIN_SIZE 17                /* 8.5 points: text this size or smaller stays as it is */

u32 fit_pages(Doc *d, FontSet *fs)
{
  Layout *L = lay_new(d, fs);
  u32 n;
  if (!L) return 0;
  lay_update(L);
  n = L->npages;
  lay_free(L);
  return n;
}
/* something that starts a new page whatever the text's size */
static int forced_breaks(Doc *d)
{
  u32 i;
  for (i = 0; i < d->np; i++) {
    const ParaFmt *pf = &d->p[i]->pf;
    if ((pf->pagebreak || pf->colbreak) && i > 0) return 1;
    if (pf->sect && i + 1 < d->np && pf->sect <= (u16)d->nsects && d->sects[pf->sect - 1].start != SB_CONT) return 1;
  }
  return 0;
}
/* the copy of p for a step */
static Para *tightened(const Para *p, int shrink, int pct)
{
  Para *q = para_copy(p);
  u32 k;
  int lpct = pct < 94 ? 94 : pct, spct = pct < 85 ? 85 : pct;
  if (!q) return NULL;
  q->pf.before = q->pf.before * spct / 100;
  q->pf.after = q->pf.after * spct / 100;
  if (q->pf.linerule == LS_AUTO) q->pf.line = q->pf.line * lpct / 100;
  else if (q->pf.linerule == LS_ATLEAST) q->pf.line = q->pf.line * lpct / 100;     /* (exact spacing is kept) */
  if (shrink)
    for (k = 0; k < q->nr; k++) {
      CharFmt *f = &q->r[k].f;
      if (f->size > MIN_SIZE) f->size = (u16)(f->size - shrink < MIN_SIZE ? MIN_SIZE : f->size - shrink);
    }
  para_touch(q);
  return q;
}
static void free_paras(Para **v, u32 n) { u32 i; for (i = 0; i < n; i++) para_free(v[i]); free(v); }

int fit_one_page(Doc *d, FontSet *fs, u32 *before, u32 *after)
{
  Layout *L;
  u32 p0, last, i, nlines = 0, n;
  i32 top = 0, bottom = 0, low = 0;
  int s, r = FIT_CANT;
  Para **trial = NULL, **orig;
  if (before) *before = 0;
  if (after) *after = 0;
  if (d->rvtrack) return FIT_TRACKED;
  if (!(L = lay_new(d, fs))) return FIT_OOM;
  lay_update(L);
  p0 = L->npages;
  if (before) *before = p0;
  if (after) *after = p0;
  if (p0 <= 1) { lay_free(L); return FIT_ONEPAGE; }
  if (forced_breaks(d)) { lay_free(L); return FIT_BREAKS; }
  /* how much is on the last page */
  last = p0 - 1;
  {
    const PageSetup *pg = lay_page(L, last);
    top = pg->top; bottom = pg->height - pg->bottom;
  }
  low = top;
  for (i = 0; i < L->nplaced; i++) {
    const Placed *pc = &L->placed[i];
    if (pc->page != last) continue;
    nlines++;
    if (pc->para < L->npl && pc->line < L->pl[pc->para].nlines) {
      i32 y = pc->y + L->pl[pc->para].lines[pc->line].height;
      if (y > low) low = y;
    }
  }
  lay_free(L);
  if (nlines > 12 || (bottom > top && (low - top) * 100 > (bottom - top) * 30)) return FIT_TOOFULL;
  /* each step on copies of the paragraphs, in the document's place while it is laid out */
  n = d->np;
  orig = d->p;
  for (s = 0; s < (int)(sizeof steps / sizeof steps[0]); s++) {
    u32 got;
    if (!(trial = (Para **)calloc(n ? n : 1, sizeof(Para *)))) return FIT_OOM;
    for (i = 0; i < n; i++) if (!(trial[i] = tightened(orig[i], steps[s].shrink, steps[s].pct))) { free_paras(trial, i); return FIT_OOM; }
    d->p = trial;
    got = fit_pages(d, fs);
    d->p = orig;
    if (got && got < p0) {
      Pos where;
      where.para = 0; where.off = 0;
      if (after) *after = got;
      r = doc_replace(d, 0, n, trial, n, where) ? FIT_OK : FIT_OOM;     /* (takes the copies over) */
      free(trial);
      return r;
    }
    free_paras(trial, n);
  }
  return r;
}
