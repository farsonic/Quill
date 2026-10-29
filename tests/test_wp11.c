/* test_wp11.c - the native tests of WP11 Outline view and outline levels (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp11() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/outline.h"
#include "../core/layout.h"
#include "../core/toc.h"

/* a document of paragraphs, each "L:text" with L its heading level (0 body text) */
static Doc *outline_doc(const char *const *paras, int n)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  int i;
  for (i = 0; i < n; i++) {
    if (i) doc_insert(d, &c, "\n", 1, NULL);
    doc_insert(d, &c, paras[i] + 2, (u32)strlen(paras[i] + 2), NULL);
  }
  for (i = 0; i < n; i++) {
    int l = paras[i][0] - '0';
    doc_apply_style(d, (u32)i, (u32)i, l ? outline_heading_style(d, l) : 0);
  }
  doc_clear_undo(d);
  return d;
}
static int levels_are(Doc *d, const char *want)
{
  char got[64];
  u32 i;
  for (i = 0; i < d->np && i < 63; i++) got[i] = (char)('0' + outline_level(d, i));
  got[i] = 0;
  if (strcmp(got, want)) { printf("  levels %s, wanted %s\n", got, want); return 0; }
  return 1;
}
static int first_letters(Doc *d, const char *want)
{
  char got[64];
  u32 i;
  for (i = 0; i < d->np && i < 63; i++) got[i] = d->p[i]->len ? d->p[i]->t[0] : '.';
  got[i] = 0;
  if (strcmp(got, want)) { printf("  order %s, wanted %s\n", got, want); return 0; }
  return 1;
}

static const char *const sample[] = { "1:A chapter", "0:b text", "2:C section", "0:d text", "0:e text", "2:F section", "1:G chapter", "0:h text" };

static void levels(void)
{
  Doc *d = outline_doc(sample, 8), *b;
  ParaFmt pf;
  int k;
  ok("outline: heading styles give their level", levels_are(d, "10200210"));
  ok("outline: heading style names", heading_style_level("heading 3") == 3 && heading_style_level("Heading 9") == 9 && !heading_style_level("heading 10") && !heading_style_level("Title"));
  ok("outline: built-in Heading 1 has level 1", d->styles[doc_style(d, "Heading1")].pf.outlvl == 1 && d->styles[0].pf.outlvl == 0);
  k = outline_heading_style(d, 6);
  ok("outline: heading 6 made with level 6", k > 0 && d->styles[k].pf.outlvl == 6 && !strcmp(d->styles[k].name, "heading 6") && outline_heading_style(d, 6) == k);
  ok("outline: parts", outline_end(d, 0) == 6 && outline_end(d, 2) == 5 && outline_end(d, 1) == 2 && outline_end(d, 6) == 8 && outline_end(d, 5) == 6);
  ok("outline: has kids", outline_has_kids(d, 0) && outline_has_kids(d, 2) && !outline_has_kids(d, 5) && !outline_has_kids(d, 1));
  /* a paragraph's own outline level (the Paragraph window's): kept through .docx, used by the TOC */
  pf = d->p[1]->pf; pf.outlvl = 3;
  doc_para_format(d, 1, 1, PM_OUTLINE, &pf);
  pf = d->p[5]->pf; pf.outlvl = 0;
  doc_para_format(d, 5, 5, PM_OUTLINE, &pf);              /* a heading style shown as body text */
  ok("outline: own levels", levels_are(d, "13200010"));
  b = docx_again(d, NULL);
  ok("outline: .docx keeps own and style levels", b && levels_are(b, "13200010") && !strcmp(b->styles[b->p[5]->pf.style].name, "heading 2") &&
     b->styles[doc_style(b, "Heading6")].pf.outlvl == 6);
  {
    size_t n;
    u8 *data = docx_save(d, &n);
    char *xml = NULL;
    (void)xml;
    ok("outline: written as w:outlineLvl", data != NULL);
    free(data);
  }
  doc_free(b);
  doc_free(d);
}

static void promote(void)
{
  Doc *d = outline_doc(sample, 8);
  Pos c;
  ok("promote: heading 2 to 1", outline_promote(d, 2, 2, -1) && levels_are(d, "10100210"));
  ok("promote: heading 1 stays", !outline_promote(d, 0, 0, -1) && levels_are(d, "10100210"));
  ok("promote: body text takes the heading above's level", outline_promote(d, 4, 4, -1) && levels_are(d, "10101210"));
  ok("demote: body text stays", !outline_promote(d, 3, 3, 1));
  ok("demote: a range, body text left", outline_promote(d, 0, 3, 1) && levels_are(d, "20201210"));
  ok("demote: applies the Heading style", !strcmp(d->styles[d->p[0]->pf.style].name, "heading 2"));
  ok("demote: one undo step", doc_undo(d, &c) && levels_are(d, "10101210"));
  ok("to body text: Normal", outline_set_level(d, 0, 0, 0) && levels_are(d, "00101210") && d->p[0]->pf.style == 0);
  ok("to heading 1", outline_set_level(d, 5, 7, 1) && levels_are(d, "00101111"));
  ok("demote: to heading 4 (made)", outline_set_level(d, 7, 7, 3) && outline_promote(d, 7, 7, 1) && levels_are(d, "00101114") &&
     doc_style(d, "Heading4") >= 0);
  {
    /* a Normal paragraph with an outline level of its own changes that level, keeping its style */
    ParaFmt pf = d->p[1]->pf;
    pf.outlvl = 4;
    doc_para_format(d, 1, 1, PM_OUTLINE, &pf);
    ok("promote: own level", outline_promote(d, 1, 1, -1) && levels_are(d, "03101114") && d->p[1]->pf.style == 0);
  }
  doc_free(d);
}

static void moving(void)
{
  Doc *d = outline_doc(sample, 8);
  u8 hid[16];
  Para *coll[2];
  u32 na = 99;
  Pos c;
  ok("move: down one", outline_move(d, 1, 1, 1, NULL, &na) && first_letters(d, "ACbdeFGh") && na == 2);
  ok("move: up one, undo", doc_undo(d, &c) && first_letters(d, "AbCdeFGh"));
  ok("move: not past the start", !outline_move(d, 0, 0, -1, NULL, NULL));
  ok("move: not past the end", !outline_move(d, 7, 7, 1, NULL, NULL));
  ok("move: a range up", outline_move(d, 3, 4, -1, NULL, &na) && first_letters(d, "AbdeCFGh") && na == 2);
  doc_undo(d, &c);
  /* collapsed: C hides d and e, which move with it */
  coll[0] = d->p[2];
  outline_hidden(d, coll, 1, 0, hid);
  ok("hidden: under a collapsed heading", !hid[2] && hid[3] && hid[4] && !hid[5] && !hid[0]);
  ok("move: a collapsed part down past F", outline_move(d, 2, 4, 1, hid, &na) && first_letters(d, "AbFCdeGh") && na == 3);
  doc_undo(d, &c);
  coll[0] = d->p[2];
  outline_hidden(d, coll, 1, 0, hid);
  ok("move: F up past the collapsed C", outline_move(d, 5, 5, -1, hid, &na) && first_letters(d, "AbFCdeGh") && na == 2);
  doc_undo(d, &c);
  ok("move_to: a chapter to the end", outline_move_to(d, 0, 5, 8, &na) && first_letters(d, "GhAbCdeF") && na == 2);
  doc_undo(d, &c);
  outline_hidden(d, NULL, 0, 1, hid);
  ok("hidden: level 1 only", !hid[0] && hid[1] && hid[2] && hid[5] && !hid[6] && hid[7]);
  outline_hidden(d, NULL, 0, 2, hid);
  ok("hidden: levels 1-2", !hid[0] && hid[1] && !hid[2] && hid[3] && !hid[5] && !hid[6]);
  {
    /* the last paragraph keeps the last section's format */
    SectFmt s;
    sect_default(&s);
    s.cols = 2;
    d->p[7]->pf.sect = (u16)doc_add_sect(d, &s);
    ok("move: last paragraph's section stays last", outline_move(d, 7, 7, -1, NULL, NULL) && d->p[7]->pf.sect && !d->p[6]->pf.sect && first_letters(d, "AbCdeFhG"));
  }
  doc_free(d);
}

static void toc_and_layout(void)
{
  Doc *d = outline_doc(sample, 8);
  Layout *L;
  ParaFmt pf;
  Pos at = doc_start(d);
  u8 hid[16];
  Para *coll[1];
  int again;
  u32 i, n;
  pf = d->p[4]->pf; pf.outlvl = 2;
  doc_para_format(d, 4, 4, PM_OUTLINE, &pf);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("toc: takes own outline levels", doc_toc(d, L, &at, &again));
  for (i = 0, n = 0; i < d->np; i++) if (toc_para(d, d->p[i]) && strstr(d->p[i]->t, "e text")) n++;
  ok("toc: the Normal paragraph with level 2 listed", n == 1);
  ok("toc: its title isn't a heading", outline_level(d, 0) == 0);
  lay_free(L);
  doc_free(d);
  /* Outline view's layout: indents by level, hidden paragraphs take no room */
  d = outline_doc(sample, 8);
  L = lay_new(d, &fs);
  L->webw = 9000; L->outline = OL_ON;
  lay_update(L);
  ok("layout: heading 1 at the gutter", L->pl[0].lines[0].left == OL_GUTTER);
  ok("layout: heading 2 one step in, body under it", L->pl[2].lines[0].left == OL_GUTTER + OL_STEP && L->pl[3].lines[0].left == OL_GUTTER + 2 * OL_STEP &&
     L->pl[1].lines[0].left == OL_GUTTER + OL_STEP);
  coll[0] = d->p[0];
  outline_hidden(d, coll, 1, 0, hid);
  L->ohid = hid; L->nohid = d->np;
  lay_update(L);
  ok("layout: hidden paragraphs have no height", L->pl[1].nlines == 1 && L->pl[1].lines[0].height == 0 && L->pl[5].lines[0].height == 0 &&
     L->pl[6].lines[0].height > 0 && L->placed[L->pl[6].placed0].y == L->placed[L->pl[0].placed0].y + L->pl[0].lines[0].height + 60);
  {
    Pos p = lay_updown(L, P(0, 0), 2000, 1);
    ok("layout: down skips hidden paragraphs", p.para == 6);
  }
  /* first line only */
  {
    Pos e = doc_end_pos(d);
    const char *more = " and a long paragraph of body text that will need more than one line in a narrow window, so that only its first is shown";
    doc_insert(d, &e, more, (u32)strlen(more), NULL);
  }
  L->ohid = NULL; L->nohid = 0;
  L->outline = OL_ON | OL_FIRST | OL_PLAIN;
  L->oplain = d->styles[0].cf;
  lay_update(L);
  ok("layout: first line only", L->pl[7].nlines == 1 && L->pl[7].ocut);
  ok("layout: plain text", L->pl[0].nboxes && L->pl[0].boxes[0].f.size == d->styles[0].cf.size && !(L->pl[0].boxes[0].f.flags & CF_BOLD));
  L->outline = 0;
  lay_update(L);
  ok("layout: back to normal", L->pl[7].nlines > 1 && L->pl[0].lines[0].left == 0);
  lay_free(L);
  doc_free(d);
}

static void word_files(void)
{
  const char *files[] = { "tests/data/word.docx", "tests/data/lo.docx", "tests/data/wordcraft-long.docx" };
  int f;
  for (f = 0; f < 3; f++) {
    char err[200], what[120];
    unsigned lost;
    Doc *a = doc_load_file(files[f], &lost, err, sizeof err), *b;
    u32 i, nh = 0, same = 1;
    int k;
    if (!a) { ok("outline: Word file loads", 0); continue; }
    k = doc_style(a, "Heading1");
    for (i = 0; i < a->np; i++) if (outline_level(a, i)) nh++;
    b = docx_again(a, NULL);
    if (b && b->np == a->np) { for (i = 0; i < a->np; i++) if (outline_level(a, i) != outline_level(b, i)) same = 0; } else same = 0;
    sprintf(what, "outline: %s headings have levels and keep them", files[f]);
    ok(what, (k < 0 || a->styles[k].pf.outlvl == 1) && nh > 0 && same);
    doc_free(a); doc_free(b);
  }
}

void test_wp11(void)
{
  levels();
  promote();
  moving();
  toc_and_layout();
  word_files();
}
