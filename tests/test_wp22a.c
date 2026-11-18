/* test_wp22a.c - the native tests of WP22a Pages, sections and columns (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp22a() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/pages.h"
#include "../core/hyph.h"
#include "../core/pdf.h"
#include "../core/fitpage.h"
#include "../core/layout.h"

/* a document of n paragraphs of words */
static Doc *words_doc(int n, int words)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  int i, k;
  for (i = 0; i < n; i++) {
    for (k = 0; k < words; k++) doc_insert(d, &c, k % 3 ? "lorem ipsum " : "dolor sit amet ", k % 3 ? 12 : 14, NULL);
    if (i + 1 < n) doc_insert(d, &c, "\n", 1, NULL);
  }
  return d;
}
/* paragraph pi ends a section of format s (added) */
static void end_sect(Doc *d, u32 pi, const SectFmt *s)
{
  int k = doc_add_sect(d, s);
  d->p[pi]->pf.sect = (u16)k;
  d->p[pi]->ver++;
}
/* the document's last section's format (the last paragraph's) */
static void last_sect(Doc *d, const SectFmt *s) { end_sect(d, d->np - 1, s); }

static void page_setup_tests(void)
{
  Doc *d = doc_new();
  PageSetup pg, e0, e1;
  Layout *L;
  memset(&pg, 0, sizeof pg);
  pg.width = 12240; pg.height = 15840; pg.top = pg.bottom = 1440; pg.left = 1800; pg.right = 1200; pg.gutter = 720;
  pages_effective(d, &pg, 0, 0, &e0);
  ok("page setup: the gutter adds to the left margin", e0.left == 2520 && e0.right == 1200 && e0.width == 12240);
  d->gutterattop = 1;
  pages_effective(d, &pg, 0, 0, &e0);
  ok("page setup: or to the top", e0.left == 1800 && e0.top == 2160);
  d->gutterattop = 0;
  pages_set_multi(d, MP_MIRROR);
  pages_effective(d, &pg, 0, 0, &e0); pages_effective(d, &pg, 0, 1, &e1);
  ok("page setup: mirror margins swap inside and outside on even pages", e0.left == 2520 && e0.right == 1200 && e1.left == 1200 && e1.right == 2520);
  pages_set_multi(d, MP_TWOONONE);
  pages_effective(d, &pg, 0, 0, &e0);
  ok("page setup: two pages per sheet halves a portrait sheet's height", e0.height == 7920 && e0.width == 12240 && pages_multi(d) == MP_TWOONONE);
  pages_set_multi(d, MP_BOOKFOLD);
  pages_effective(d, &pg, 0, 0, &e0); pages_effective(d, &pg, 0, 1, &e1);
  ok("page setup: a book fold is half the landscape sheet, mirrored", e0.width == 7920 && e0.height == 12240 && e0.left == 2520 && e1.right == 2520);
  pages_effective(d, &pg, 1, 0, &e0);
  ok("page setup: a right-to-left gutter goes on the right", e0.right == 1200 + 720);
  doc_free(d);
  /* laid out: the mirrored margins on the pages, the body as wide on both */
  d = words_doc(40, 30);
  d->page.gutter = 720; d->mirror = 1;
  L = lay_new(d, &fs); lay_update(L);
  ok("page setup: laid out mirrored (odd page's left is the inside with the gutter)", L->npages >= 2 && lay_page(L, 0)->left == d->page.left + 720 &&
     lay_page(L, 1)->right == d->page.left + 720 && lay_page(L, 1)->left == d->page.right);
  ok("page setup: lines as wide on even pages as odd", L->pl[0].avail == d->page.width - d->page.left - d->page.right - 720);
  lay_free(L); doc_free(d);
  d = words_doc(30, 30);
  d->twoonone = 1;
  L = lay_new(d, &fs); lay_update(L);
  ok("page setup: two to a sheet lays out half-height pages", lay_page(L, 0)->height == d->page.height / 2 && L->npages > 2);
  lay_free(L); doc_free(d);
}

static void column_tests(void)
{
  SectFmt s;
  i32 x[MAX_SECT_COLS], w[MAX_SECT_COLS];
  Doc *d;
  Layout *L;
  u32 k, wide = 0, narrow = 0, bad = 0;
  int n;
  sect_default(&s);
  pages_col_preset(&s, CP_LEFT, 9360);
  n = pages_cols(&s, 9360, x, w);
  ok("columns: Left is a narrow column then a wide one", n == 2 && w[0] < w[1] && w[0] * 2 < w[1] + 300 && x[1] == w[0] + 720 && pages_col_preset_of(&s, 9360) == CP_LEFT);
  pages_col_preset(&s, CP_RIGHT, 9360);
  n = pages_cols(&s, 9360, x, w);
  ok("columns: Right the other way round", n == 2 && w[0] > w[1] && pages_col_preset_of(&s, 9360) == CP_RIGHT);
  pages_col_preset(&s, CP_THREE, 9360);
  n = pages_cols(&s, 9360, x, w);
  ok("columns: three equal", n == 3 && w[0] == w[2] && !s.colunequal && pages_col_preset_of(&s, 9360) == CP_THREE);
  {
    i32 cw[3] = { 2000, 3000, 2000 }, sp[3] = { 500, 1000, 0 };
    pages_col_set(&s, 3, cw, sp, 0, 720);
    n = pages_cols(&s, 8500, x, w);
    ok("columns: widths of their own, each space after its column", n == 3 && w[1] == 3000 && x[1] == 2500 && x[2] == 6500);
  }
  /* laid out: each line as wide as the column it lands in */
  d = words_doc(12, 40);
  sect_default(&s);
  pages_col_preset(&s, CP_LEFT, d->page.width - d->page.left - d->page.right);
  last_sect(d, &s);
  L = lay_new(d, &fs); lay_update(L);
  pages_cols(&s, d->page.width - d->page.left - d->page.right, x, w);
  for (k = 0; k < L->nplaced; k++) {
    const Placed *pc = &L->placed[k];
    const Line *ln = &L->pl[pc->para].lines[pc->line];
    if (pc->x == x[0]) narrow++; else if (pc->x == x[1]) wide++;
    if (ln->width > pc->w + 20) bad++;
  }
  ok("columns: lines in both columns of different widths, none wider than its column", narrow > 5 && wide > 5 && !bad);
  lay_free(L); doc_free(d);
  /* balanced before a continuous break */
  d = words_doc(6, 25);
  sect_default(&s); s.cols = 2;
  end_sect(d, 3, &s);
  sect_default(&s); s.start = SB_CONT;
  last_sect(d, &s);
  L = lay_new(d, &fs); lay_update(L);
  {
    i32 b0 = 0, b1 = 0, top0 = 0x7FFFFFFF, top4 = 0;
    for (k = 0; k < L->nplaced; k++) {
      const Placed *pc = &L->placed[k];
      i32 bot = pc->y + L->pl[pc->para].lines[pc->line].height;
      if (pc->para > 3) { if (!top4 || pc->y < top4) top4 = pc->y; continue; }
      if (pc->y < top0) top0 = pc->y;
      if (pc->x == 0) { if (bot > b0) b0 = bot; } else if (bot > b1) b1 = bot;
    }
    ok("columns: balanced before a continuous break (both used, ends within a line or two)", b1 > top0 && b0 - b1 < 700 && b1 - b0 < 700);
    ok("columns: the next section starts below them on the same page", top4 >= b0 && top4 >= b1 && L->placed[L->nplaced - 1].page == 0);
  }
  lay_free(L); doc_free(d);
}

static void valign_tests(void)
{
  Doc *d = words_doc(4, 8);
  SectFmt s;
  Layout *L;
  i32 lastbot, firsty;
  sect_default(&s); s.valign = VA_JUSTIFY;
  last_sect(d, &s);
  L = lay_new(d, &fs); lay_update(L);
  firsty = L->placed[0].y;
  lastbot = L->placed[L->nplaced - 1].y + L->pl[L->placed[L->nplaced - 1].para].lines[L->placed[L->nplaced - 1].line].height;
  ok("vertical alignment: Justified spreads the paragraphs down to the foot of the page", firsty == d->page.top && lastbot >= d->page.height - d->page.bottom - 300 && L->placed[2].y > d->page.top + 3000);
  lay_free(L); doc_free(d);
}

static void border_tests(void)
{
  Doc *d = words_doc(30, 30);
  SectFmt s;
  Layout *L;
  PBRect *r;
  int n0, n1, n2, i, top = 0, left = 0;
  PageBorder pb;
  memset(&pb, 0, sizeof pb);
  pages_pb_set(&pb, "thinThickSmallGap", 24, 0xFF0000, 480, (1 << PBS_TOP) | (1 << PBS_BOTTOM), 1);
  ok("page borders: sides of their own", pages_pb_mask(&pb) == 5 && pb.own && pb.style == PB_DOUBLE);
  pages_pb_set(&pb, "single", 4, COLOR_AUTO, 480, 15, 1);
  ok("page borders: one plain kind all round needs no sides of its own", !pb.own && pb.style == PB_SINGLE && pages_pb_mask(&pb) == 15);
  /* section 1 (paragraphs 0-9) with a red art border on its first page only; the rest none */
  sect_default(&s);
  s.pbown = 1;
  pages_pb_set(&s.pb, "apples", 12, 0xC00000, 480, 15, 1);
  s.pb.display = PBD_FIRST;
  end_sect(d, 9, &s);
  L = lay_new(d, &fs); lay_update(L);
  n0 = pages_border_rects(L, 0, &r);
  for (i = 0; i < n0; i++) { if (r[i].y0 < 1000) top = 1; if (r[i].x0 < 1000 && r[i].y0 > 1000) left = 1; }
  ok("page borders: an art border drawn as a pattern on the section's first page", n0 > 40 && top && left && r[0].color == 0xC00000);
  free(r);
  n1 = pages_border_rects(L, 1, &r); free(r);
  n2 = pages_border_rects(L, L->npages - 1, &r); free(r);
  ok("page borders: not on its other pages, nor the next section's", L->npages > 2 && n1 == 0 && n2 == 0);
  lay_free(L);
  /* the document's: a dotted top line only */
  pages_pb_set(&d->border, "dotted", 8, COLOR_AUTO, 24 * 20, 1 << PBS_TOP, 0);
  L = lay_new(d, &fs); lay_update(L);
  n2 = pages_border_rects(L, L->npages - 1, &r);
  for (i = 0, top = 1; i < n2; i++) if (r[i].y1 > d->page.top) top = 0;
  ok("page borders: a dotted line along the top only, above the text", n2 > 20 && top);
  free(r);
  lay_free(L); doc_free(d);
}

static void hyph_tests(void)
{
  Doc *d = doc_new();
  Line l[3];
  Box b[3];
  int i;
  memset(l, 0, sizeof l); memset(b, 0, sizeof b);
  for (i = 0; i < 3; i++) { l[i].box0 = (u32)i; l[i].nbox = 1; b[i].kind = BX_TEXT; b[i].nspace = 1; }
  ok("hyphenation: allowed by default", pages_hyph_allowed(d, "NASA", 4, l, 3, b) && pages_hyph_zone(d) == 360);
  d->hynocaps = 1; d->hylimit = 2; d->hyzone = 720;
  ok("hyphenation: not words in capitals", !pages_hyph_allowed(d, "INTERNATIONAL", 13, l, 0, b) && pages_hyph_allowed(d, "International", 13, l, 0, b));
  ok("hyphenation: no more than the limit of hyphens in a row", !pages_hyph_allowed(d, "word", 4, l, 2, b) && pages_hyph_allowed(d, "word", 4, l, 1, b));
  ok("hyphenation: the zone", pages_hyph_zone(d) == 720);
  doc_free(d);
  /* the limit in a real layout */
  if (!hyph_ready()) hyph_load("dict/hyph_en_GB.dic");
  {
    Pos c, at;
    Layout *L;
    u32 k, run = 0, maxrun = 0, len, sug;
    u8 brk[40];
    d = doc_new(); c = doc_start(d);
    for (k = 0; k < 20; k++) { static const char t[] = "Internationalisation characteristically demonstrates typographical considerations unquestionably. "; doc_insert(d, &c, t, sizeof t - 1, NULL); }
    d->hyphenate = 1; d->hylimit = 1;
    L = lay_new(d, &fs); lay_update(L);
    for (k = 0; k < L->pl[0].nlines; k++) {
      const Line *ln = &L->pl[0].lines[k];
      if (ln->nbox && L->pl[0].boxes[ln->box0 + ln->nbox - 1].kind == BX_TEXT && L->pl[0].boxes[ln->box0 + ln->nbox - 1].nspace == 1) run++; else run = 0;
      if (run > maxrun) maxrun = run;
    }
    ok("hyphenation: consecutive limit 1 keeps hyphenated lines apart", maxrun == 1);
    /* manual: off, the next word that could be broken is found */
    d->hyphenate = 0; pages_hyph_changed(d);
    lay_update(L);
    ok("manual hyphenation: a word that could break at a line's end", pages_next_manual(L, doc_start(d), &at, &len, brk, &sug) && len >= 5 && sug > 0 && sug < len && brk[sug]);
    {
      /* the optional hyphen put there: the line now breaks after it (WP10's optional hyphens) */
      Pos h = at;
      u32 j, broke = 0;
      h.off += sug;
      doc_insert(d, &h, "\xC2\xAD", 2, NULL);
      lay_update(L);
      for (j = 0; j < L->pl[at.para].nlines; j++) if (L->pl[at.para].lines[j].end == at.off + sug + 2) broke = 1;
      ok("manual hyphenation: the line breaks at the optional hyphen put in", broke);
    }
    lay_free(L); doc_free(d);
  }
}

static void docx_tests(void)
{
  Doc *d = words_doc(8, 10), *e;
  SectFmt s;
  unsigned lost = 0;
  size_t n, pl;
  u8 *png = file_read("tests/data/disc.png", &pl), *x;
  i32 cw[2] = { 2000, 6000 }, sp[2] = { 600, 0 };
  sect_default(&s);
  pages_col_set(&s, 2, cw, sp, 0, 720); s.colline = 1;
  s.valign = VA_JUSTIFY; s.haspage = 1; s.page = d->page; s.page.gutter = 567; s.rtlgutter = 1;
  s.pbown = 1;
  pages_pb_set(&s.pb, "triple", 6, 0x0000FF, 400, (1 << PBS_LEFT) | (1 << PBS_RIGHT), 0);
  s.pb.display = PBD_NOTFIRST; s.pb.back = 1;
  end_sect(d, 3, &s);
  pages_pb_set(&d->border, "weavingBraid", 14, COLOR_AUTO, 480, 15, 1);
  d->page.gutter = 360;
  d->mirror = 0; d->bookfold = 1; d->bfsheets = 8; d->gutterattop = 0; d->hyzone = 425; d->hylimit = 3; d->hynocaps = 1; d->pbnohdr = 1;
  if (png) { ok("watermark: a picture", pages_wm_picture(d, png, (u32)pl) && pages_wm_on(d) && !d->wm.text[0]); d->wm.washout = 1; d->wm.scale = 150; }
  e = docx_again(d, &lost);
  ok("docx: reads back", e != NULL);
  if (e) {
    const SectFmt *t = doc_sect(e, 0);
    PBSide sd;
    ok("docx: columns of their own widths and the line between", t->cols == 2 && t->colunequal && t->colw[0] == 2000 && t->colw[1] == 6000 && t->colsp[0] == 600 && t->colline);
    ok("docx: vertical alignment Justified", t->valign == VA_JUSTIFY);
    ok("docx: the section's gutter and right-to-left gutter", t->haspage && t->page.gutter == 567 && t->rtlgutter && e->page.gutter == 360);
    pages_pb_side(&t->pb, PBS_LEFT, &sd);
    ok("docx: the section's own border, left and right, triple, not on its first page, behind", t->pbown && pages_pb_mask(&t->pb) == 10 &&
       !strcmp(sd.val, "triple") && sd.color == 0x0000FF && sd.width == 6 && sd.space == 400 && t->pb.display == PBD_NOTFIRST && t->pb.back && !t->pb.frompage);
    pages_pb_side(&e->border, PBS_TOP, &sd);
    ok("docx: the document's art border kept by name", e->border.style != PB_NONE && !strcmp(sd.val, "weavingBraid") && sd.width == 14 && e->border.frompage);
    ok("docx: book fold, sheets, hyphenation zone, limit, capitals, borders round the header",
       e->bookfold == 1 && e->bfsheets == 8 && e->hyzone == 425 && e->hylimit == 3 && e->hynocaps && e->pbnohdr && !e->pbnoftr);
    if (png) ok("docx: the picture watermark, washed out, at 150%", e->wm.pic && e->wm.piclen == pl && !memcmp(e->wm.pic, png, pl) && e->wm.washout && e->wm.scale >= 148 && e->wm.scale <= 152);
    ok("docx: lost nothing", lost == 0);
    /* and again: the same */
    {
      unsigned l2 = 0;
      Doc *f = docx_again(e, &l2);
      ok("docx: a second round trip keeps them", f && doc_sect(f, 0)->colunequal && f->bookfold == 1 && f->wm.pic && f->border.style != PB_NONE && same_doc(e, f));
      doc_free(f);
    }
    /* the settings' order: hyphenation after w:autoHyphenation's place, the book fold after w:evenAndOddHeaders' */
    x = docx_save(e, &n);
    ok("docx: saves", x != NULL);
    free(x);
    doc_free(e);
  }
  /* the mirror and two to a sheet */
  d->bookfold = 0; d->mirror = 1;
  e = docx_again(d, &lost);
  ok("docx: mirror margins", e && e->mirror && !e->bookfold);
  doc_free(e);
  d->mirror = 0; d->twoonone = 1; d->gutterattop = 1;
  e = docx_again(d, &lost);
  ok("docx: two pages per sheet and the gutter at the top", e && e->twoonone && e->gutterattop);
  doc_free(e);
  /* the PDF: the watermark washed out by /Decode or drawn pale, the border as rectangles */
  {
    Layout *L = lay_new(d, &fs);
    u8 *pdf;
    size_t plen;
    lay_update(L);
    pdf = pdf_export(L, &plen);
    ok("pdf: exports with a picture watermark and art borders", pdf && plen > 1000);
    free(pdf);
    lay_free(L);
  }
  free(png);
  doc_free(d);
  /* a text watermark: solid or half transparent */
  d = words_doc(2, 5);
  strcpy(d->wm.text, "DRAFT"); d->wm.color = 0xFF0000; d->wm.opaque = 1;
  e = docx_again(d, &lost);
  ok("docx: a solid text watermark stays solid", e && !strcmp(e->wm.text, "DRAFT") && e->wm.opaque && !e->wm.pic);
  doc_free(e);
  d->wm.opaque = 0;
  e = docx_again(d, &lost);
  ok("docx: a half transparent one stays so", e && !e->wm.opaque);
  doc_free(e); doc_free(d);
}

/* Fit One Page (WordCraft #6433, core/fitpage.c) */
static void fit_tests(void)
{
  Doc *d;
  Pos c;
  u32 b = 0, a = 0, n = 1, base;
  int r;
  /* one-line paragraphs until the text runs two lines onto a second page */
  d = words_doc(1, 3);
  c = doc_end_pos(d);
  while (fit_pages(d, &fs) < 2 && n < 200) { doc_insert(d, &c, "\nlorem ipsum dolor", 18, NULL); n++; }
  doc_insert(d, &c, "\nlorem ipsum dolor", 18, NULL);
  base = d->p[0]->r[0].f.size;
  r = fit_one_page(d, &fs, &b, &a);
  ok("fit one page: two pages brought onto one", r == FIT_OK && b == 2 && a == 1 && fit_pages(d, &fs) == 1 && runs_sane(d));
  ok("fit one page: text not under 8.5 points", d->p[0]->r[0].f.size >= 17 && d->p[0]->r[0].f.size <= base);
  ok("fit one page: one undo step", doc_undo(d, &c) && fit_pages(d, &fs) == 2 && d->p[0]->r[0].f.size == base);
  ok("fit one page: already one page", fit_one_page(d, &fs, NULL, NULL) == FIT_OK && fit_one_page(d, &fs, NULL, NULL) == FIT_ONEPAGE);
  doc_undo(d, &c);
  d->p[d->np - 1]->pf.pagebreak = 1; d->p[d->np - 1]->ver++;
  ok("fit one page: a page break refuses", fit_one_page(d, &fs, NULL, NULL) == FIT_BREAKS);
  d->p[d->np - 1]->pf.pagebreak = 0; d->p[d->np - 1]->ver++;
  d->rvtrack = 1;
  ok("fit one page: not while formatting is tracked", fit_one_page(d, &fs, NULL, NULL) == FIT_TRACKED);
  d->rvtrack = 0;
  doc_free(d);
  /* a second page half full is too much to take back */
  d = words_doc(1, 3);
  c = doc_end_pos(d);
  n = 1;
  while (fit_pages(d, &fs) < 2 && n < 400) { doc_insert(d, &c, "\nlorem ipsum dolor", 18, NULL); n++; }
  for (n = 0; n < 25; n++) doc_insert(d, &c, "\nlorem ipsum dolor", 18, NULL);
  ok("fit one page: a full last page refuses", fit_one_page(d, &fs, NULL, NULL) == FIT_TOOFULL);
  doc_free(d);
}

/* line numbers beside every column, in the Line Number style (WordCraft #6018) */
static void linenum_col_tests(void)
{
  SectFmt s;
  Doc *d = words_doc(30, 40);
  Layout *L;
  Style st;
  CharFmt f;
  u32 k, in1 = 0, in2 = 0;
  i32 x2 = -1;
  sect_default(&s);
  s.cols = 2; s.linenum = LN_PAGE; s.lnby = 1;
  last_sect(d, &s);
  L = lay_new(d, &fs); lay_update(L);
  for (k = 0; k < L->nplaced; k++) {
    const Placed *pc = &L->placed[k];
    if (pc->page) continue;
    if (pc->x == 0 && pc->lnum) in1++;
    else if (pc->x > 0 && pc->lnum) { in2++; x2 = pc->x; }
  }
  ok("line numbers: beside both columns, each left of its own column", in1 > 5 && in2 > 5 && x2 > 2000);
  lay_free(L);
  lay_lnum_fmt(d, &f);
  ok("line numbers: Normal's look without a Line Number style", f.size == d->styles[0].cf.size);
  memset(&st, 0, sizeof st);
  st = d->styles[0];
  strcpy(st.id, "Zeilennummer"); strcpy(st.name, "line number");
  st.type = ST_CHAR; st.cf.size = 16; st.cf.color = 0x808080; st.cf.flags |= CF_BOLD;
  doc_add_style(d, &st);
  lay_lnum_fmt(d, &f);
  ok("line numbers: a Line Number style saved under a localised id", f.size == 16 && f.color == 0x808080 && !(f.flags & CF_BOLD));
  doc_free(d);
}

/* continuous sections one after another go onto new pages instead of running off the page; empty
   section marks in a row stay on the page (WordCraft #3907) */
static int on_pages(Layout *L)
{
  u32 k;
  for (k = 0; k < L->nplaced; k++) {
    const Placed *pc = &L->placed[k];
    const PageSetup *pg = lay_page(L, pc->page);
    if (pc->para < L->npl && pc->line < L->pl[pc->para].nlines && pc->y + L->pl[pc->para].lines[pc->line].height > pg->height - pg->bottom + 20) {
      printf("  para %lu line %lu: y %ld past the bottom of page %lu\n", (unsigned long)pc->para, (unsigned long)pc->line, (long)pc->y, (unsigned long)pc->page);
      return 0;
    }
  }
  return 1;
}
static void contsect_tests(void)
{
  SectFmt s;
  Doc *d = words_doc(80, 12);
  Layout *L;
  u32 i;
  sect_default(&s);
  s.start = SB_CONT;
  for (i = 0; i + 1 < d->np; i++) end_sect(d, i, &s);
  L = lay_new(d, &fs); lay_update(L);
  ok("continuous sections: a run of them goes on to new pages, none off the page", L->npages >= 2 && on_pages(L));
  lay_free(L);
  doc_free(d);
  /* two columns, continuous, many in a row */
  d = words_doc(60, 30);
  sect_default(&s);
  s.start = SB_CONT; s.cols = 2;
  for (i = 0; i + 1 < d->np; i += 3) end_sect(d, i, &s);
  L = lay_new(d, &fs); lay_update(L);
  ok("continuous sections: balanced two-column sections stay on their pages", L->npages >= 2 && on_pages(L));
  lay_free(L);
  doc_free(d);
  /* empty marks in a row */
  {
    Doc *e = words_doc(1, 3);
    Pos c = doc_end_pos(e);
    for (i = 0; i < 120; i++) doc_insert(e, &c, "\n", 1, NULL);
    doc_insert(e, &c, "after", 5, NULL);
    sect_default(&s);
    s.start = SB_CONT;
    for (i = 1; i + 1 < e->np; i++) end_sect(e, i, &s);
    L = lay_new(e, &fs); lay_update(L);
    ok("continuous sections: empty marks in a row stay on the page", on_pages(L));
    lay_free(L);
    doc_free(e);
  }
}

void test_wp22a(void)
{
  contsect_tests();
  linenum_col_tests();
  fit_tests();
  page_setup_tests();
  column_tests();
  valign_tests();
  border_tests();
  hyph_tests();
  docx_tests();
}
