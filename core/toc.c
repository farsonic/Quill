/* toc.c - see toc.h */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "toc.h"

/* the outline level of a "heading N" style (either capitalisation), or -1 for any other style */
static int heading_level(const Style *st)
{
  if (!strncmp(st->name, "heading ", 8) || !strncmp(st->name, "Heading ", 8)) return st->name[8] >= '1' && st->name[8] <= '9' ? st->name[8] - '0' : 1;
  return -1;
}
/* 1 if p's style is named "toc N" or "TOC N" (N a digit); an out-of-range style index counts
   as style 0, as elsewhere in the core */
int toc_para(Doc *d, const Para *p)
{
  const Style *st = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0];
  return !strncmp(st->name, "toc ", 4) || !strncmp(st->name, "TOC ", 4) ? (st->name[4] >= '1' && st->name[4] <= '9') : 0;
}
/* the "toc N" style (made if the document has none): Normal, indented by level, a right tab with dots
   at the right margin */
static int toc_style(Doc *d, int level)
{
  char id[8], name[8];
  int k;
  Style s;
  sprintf(id, "TOC%d", level); sprintf(name, "toc %d", level);
  if ((k = doc_style_by_name(d, name)) >= 0 || (k = doc_style(d, id)) >= 0) return k;
  s = d->styles[0];
  strcpy(s.id, id); strcpy(s.name, name);
  s.builtin = 0;
  s.pf.left = (level - 1) * 220; s.pf.first = 0; s.pf.before = 0; s.pf.after = 100;
  memset(s.pf.tabs, 0, sizeof s.pf.tabs); memset(s.pf.tabkind, 0, sizeof s.pf.tabkind); memset(s.pf.tablead, 0, sizeof s.pf.tablead);
  s.pf.tabs[0] = d->page.width - d->page.left - d->page.right - s.pf.left;
  s.pf.tabkind[0] = TAB_RIGHT; s.pf.tablead[0] = LEAD_DOT;
  s.pf.style = (u8)d->nstyles;
  s.next = 0;
  s.pf.keepnext = 0;
  s.quick = 0;
  return doc_add_style_from(d, &s, 0);
}
/* the "TOC Heading" style for the table's title, made as a copy of Heading 1 (or Normal) if
   the document lacks it; its index, or -1 when out of memory */
static int toc_heading_style(Doc *d)
{
  int k = doc_style_by_name(d, "TOC Heading");
  Style s;
  if (k >= 0) return k;
  if ((k = doc_style(d, "Heading1")) < 0 && (k = doc_style_by_name(d, "heading 1")) < 0) k = 0;
  s = d->styles[k];
  strcpy(s.id, "TOCHeading"); strcpy(s.name, "TOC Heading");
  s.builtin = 0; s.pf.style = (u8)d->nstyles; s.next = 0; s.quick = 0;
  return doc_add_style_from(d, &s, k);
}
/* a heading's text for the table: tabs and breaks as spaces, pictures, notes and fields left out */
static void entry_text(Buf *b, const Para *p)
{
  u32 i = 0;
  while (i < p->len) {
    int used;
    u32 cp = utf8_get(p->t + i, p->len - i, &used);
    if (cp == '\t' || cp == '\v') buf_addc(b, ' ');
    else if (!(cp >= 0xE000 && cp <= 0xE0FF) && cp < 0xF0000) buf_add(b, p->t + i, (u32)used);
    i += (u32)used;
  }
}
/* finds the existing table (the run of toc paragraphs, plus a TOC Heading just above it) or,
   for a new one, the start of the paragraph at *at, moved out of any table. Builds the title
   and entries afresh and swaps them in with one doc_replace; if the text is unchanged nothing
   is touched and no undo step is pushed. *at ends up on the paragraph after the table */
int doc_toc(Doc *d, Layout *L, Pos *at, int *again)
{
  u32 i, a = 0, n = 0, nh = 0, k;
  Para **nw;
  int found = 0, hs, changed = 0;
  Pos where;
  if (again) *again = 0;
  /* the existing table: its heading and its run of entries */
  for (i = 0; i < d->np; i++) if (toc_para(d, d->p[i])) { found = 1; break; }
  if (found) {
    a = i;
    while (i < d->np && toc_para(d, d->p[i])) i++;
    n = i - a;
    if (a > 0 && !strcmp(d->styles[d->p[a - 1]->pf.style < d->nstyles ? d->p[a - 1]->pf.style : 0].name, "TOC Heading")) { a--; n++; }
  } else {
    a = at ? at->para : 0;
    if (a > d->np) a = d->np;
    while (a > 0 && a < d->np && d->p[a]->pf.cell.table && d->p[a - 1]->pf.cell.table == d->p[a]->pf.cell.table) a--;   /* not inside a table */
  }
  for (i = 0; i < d->np; i++) {
    int lv = heading_level(&d->styles[d->p[i]->pf.style < d->nstyles ? d->p[i]->pf.style : 0]);
    if (lv >= 1 && lv <= 3 && (i < a || i >= a + n)) nh++;
  }
  if (!nh) return 0;
  if (!(nw = (Para **)calloc(nh + 1, sizeof(Para *)))) return 0;
  /* the title, then an entry for each heading */
  hs = toc_heading_style(d);
  if (hs < 0) hs = 0;
  if ((nw[0] = para_new(&d->styles[hs].pf, &d->styles[hs].cf))) { nw[0]->pf.style = (u8)hs; para_append(nw[0], "Contents", 8, &d->styles[hs].cf); }
  k = 1;
  for (i = 0; i < d->np && k <= nh; i++) {
    Para *p = d->p[i];
    int lv = heading_level(&d->styles[p->pf.style < d->nstyles ? p->pf.style : 0]), st;
    Buf b;
    char num[16];
    u32 page = 0;
    if (lv < 1 || lv > 3 || (i >= a && i < a + n)) continue;
    if ((st = toc_style(d, lv)) < 0) st = 0;
    if (L && i < L->npl) {
      Pos pp;
      i32 x, y, h;
      pp.para = i; pp.off = 0;
      lay_caret(L, pp, &page, &x, &y, &h);
      page++;
    }
    buf_init(&b);
    entry_text(&b, p);
    sprintf(num, "\t%lu", (unsigned long)page);
    buf_adds(&b, num);
    if ((nw[k] = para_new(&d->styles[st].pf, &d->styles[st].cf))) {
      nw[k]->pf.style = (u8)st;
      para_append(nw[k], b.data ? b.data : "", (u32)b.len, &d->styles[st].cf);
    }
    buf_free(&b);
    k++;
  }
  for (i = 0; i < k; i++) if (!nw[i]) { for (i = 0; i < k; i++) if (nw[i]) para_free(nw[i]); free(nw); return 0; }
  /* the same as before? then nothing to do */
  if (found && n == k) {
    for (i = 0; i < k && !strcmp(nw[i]->t, d->p[a + i]->t); i++) ;
    if (i == k) { for (i = 0; i < k; i++) para_free(nw[i]); free(nw); return 1; }
  }
  changed = 1;
  /* page numbers move when the table first goes in or changes length */
  if (again) *again = !found || n != k;
  where.para = a; where.off = 0;
  if (!doc_replace(d, a, n, nw, k, where)) { free(nw); return 0; }
  free(nw);
  if (at) { at->para = a + k < d->np ? a + k : d->np - 1; at->off = 0; }
  return changed;
}
/* see toc.h. The run of "toc N" paragraphs goes, with the "TOC Heading" title just before it; a
   table that is the whole document leaves one empty paragraph behind (a document is never empty) */
int doc_toc_remove(Doc *d, Pos *at)
{
  u32 i, a, n;
  Pos where;
  Para *blank[1];
  for (i = 0; i < d->np && !toc_para(d, d->p[i]); i++) ;
  if (i == d->np) return 0;
  a = i;
  while (i < d->np && toc_para(d, d->p[i])) i++;
  n = i - a;
  if (a > 0 && !strcmp(d->styles[d->p[a - 1]->pf.style < d->nstyles ? d->p[a - 1]->pf.style : 0].name, "TOC Heading")) { a--; n++; }
  where.para = a; where.off = 0;
  if (n == d->np) {
    if (!(blank[0] = para_new(&d->styles[0].pf, &d->styles[0].cf))) return 0;
    if (!doc_replace(d, a, n, blank, 1, where)) return 0;
  } else if (!doc_replace(d, a, n, NULL, 0, where)) return 0;
  if (at) { at->para = a < d->np ? a : d->np - 1; at->off = 0; }
  return 1;
}
