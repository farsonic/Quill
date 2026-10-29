/* test_wp25.c - the native tests of WP25 Views and windows (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp25() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/layout.h"

static Doc *make(int paras)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  int i;
  for (i = 0; i < paras; i++) {
    const char *t = "A paragraph of text long enough to wrap onto a second line in a narrow column of a few inches, as Read Mode shows it.";
    if (i) doc_insert(d, &c, "\n", 1, NULL);
    doc_insert(d, &c, t, (u32)strlen(t), NULL);
  }
  doc_clear_undo(d);
  return d;
}
static u32 total_lines(Layout *L)
{
  u32 i, n = 0;
  for (i = 0; i < L->npl; i++) n += L->pl[i].nlines;
  return n;
}
/* every paragraph's layout the same as a fresh layout's: lines, their breaks and heights */
static int same_layout(Layout *a, Layout *b)
{
  u32 i, k;
  if (a->npl != b->npl) return 0;
  for (i = 0; i < a->npl; i++) {
    if (a->pl[i].p != b->pl[i].p || a->pl[i].nlines != b->pl[i].nlines) return 0;
    for (k = 0; k < a->pl[i].nlines; k++)
      if (a->pl[i].lines[k].start != b->pl[i].lines[k].start || a->pl[i].lines[k].height != b->pl[i].lines[k].height) return 0;
  }
  return a->npages == b->npages;
}

/* one document shown in two windows: each view has its own layout of the same Doc; after the other
   changes it, laying out again (what the view does when it sees the document's version move on)
   gives what a fresh layout gives, whatever was inserted, deleted or undone */
static void shared_doc(void)
{
  Doc *d = make(30);
  Layout *a = lay_new(d, &fs), *b = lay_new(d, &fs), *f;
  Pos c = P(3, 5), e;
  u32 ver;
  lay_update(a); lay_update(b);
  ver = d->ver;
  doc_insert(d, &c, "inserted words, and\na new paragraph ", 35, NULL);
  doc_delete(d, P(10, 0), P(14, 3));
  ok("shared: an edit moves the version on", d->ver != ver);
  lay_update(b);
  f = lay_new(d, &fs); lay_update(f);
  ok("shared: the other layout laid out again matches a fresh one", same_layout(b, f));
  lay_free(f);
  doc_undo(d, &e); doc_undo(d, &e);
  lay_update(a);
  f = lay_new(d, &fs); lay_update(f);
  ok("shared: after undo too", same_layout(a, f) && d->np == 30);
  lay_free(f);
  lay_free(a); lay_free(b);
  doc_free(d);
}

/* Read Mode's options in the layout: a column of a width (webw), wider line spacing (linepct) */
static void read_mode(void)
{
  Doc *d = make(10);
  Layout *L = lay_new(d, &fs);
  u32 narrow, wide, k;
  i32 h1, h2;
  L->webw = 5040;
  lay_update(L);
  narrow = total_lines(L);
  h1 = L->pl[0].lines[0].height;
  L->webw = 9360;
  lay_update(L);
  wide = total_lines(L);
  ok("read: a narrow column has more lines than a wide one", narrow > wide && wide >= 10);
  L->webw = 5040;
  L->linepct = 150;
  for (k = 0; k < L->npl; k++) L->pl[k].laid = 0;
  lay_update(L);
  h2 = L->pl[0].lines[0].height;
  ok("read: wider line spacing", h2 == h1 + h1 / 2 || h2 == h1 + (h1 * 50) / 100);
  ok("read: the lines placed further apart", L->pl[0].nlines > 1 && L->placed[1].y - L->placed[0].y == h2);
  lay_free(L);
  doc_free(d);
}

void test_wp25(void)
{
  shared_doc();
  read_mode();
}
