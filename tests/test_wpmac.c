/* test_wpmac.c - the native tests of what Quill's classic Mac OS port (mac/, docs/mac.md) put in the
   core: the ruler's sums (core/ruler.c), which the Mac's ruler draws and drags with. Checks with ok()
   (tests/testutil.h); coretest.c's main() calls test_wpmac() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/ruler.h"
#include "../core/table.h"
#include "../core/layout.h"
#include "../core/edcmd.h"
#include "../core/field.h"

/* the ruler for the caret at p, laid out afresh */
static void geom_at(Doc *d, Pos p, RulerGeom *g)
{
  Layout *L = lay_new(d, &fs);
  lay_update(L);
  ruler_geom(d, L, p, -1, g);
  lay_free(L);
}

static void test_units(void)
{
  i32 unit;
  int nsub;
  ok("ruler: an eighth of an inch rounds up", ruler_snap(100, 0) == 180);
  ok("ruler: and down", ruler_snap(89, 0) == 0);
  ok("ruler: below zero too", ruler_snap(-100, 0) == -180);
  ok("ruler: a quarter centimetre", ruler_snap(150, 1) == 142 && ruler_snap(300, 1) == 284);
  ruler_unit(0, &unit, &nsub);
  ok("ruler: inches in eighths", unit == 1440 && nsub == 8);
  ruler_unit(1, &unit, &nsub);
  ok("ruler: centimetres in quarters", unit == 567 && nsub == 4);
}

static void test_indents(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  RulerGeom g;
  int part;
  const char *text = "A paragraph to indent, long enough to run onto a second line of the page.";
  doc_insert(d, &c, text, (u32)strlen(text), NULL);
  doc_clear_undo(d);
  geom_at(d, P(0, 3), &g);
  ok("ruler: an A4 page has centimetres", g.metric == 1);
  ok("ruler: the text area between the margins", g.x0 == 1440 && g.width == 11906 - 2880 && g.page == 0);
  ok("ruler: the page's margins", g.mtop == 1440 && g.mbottom == 1440 && g.pageh == 16838);
  /* which marker a click takes: slop of six pixels at 72 dpi is 120 twips */
  ok("ruler: the first-line marker at the top", ruler_hit(&g, g.x0 + 30, 1, 120) == RP_FIRST);
  ok("ruler: the left one at the bottom", ruler_hit(&g, g.x0 - 30, 0, 120) == RP_LEFT);
  ok("ruler: the right one", ruler_hit(&g, g.x0 + g.width + 50, 0, 120) == RP_RIGHT);
  ok("ruler: nothing between them", ruler_hit(&g, g.x0 + 3000, 0, 120) == RP_NONE && ruler_hit(&g, g.x0 + 3000, 1, 120) == RP_NONE);
  /* the left indent dragged in half an inch: the first line stays (a hanging indent) */
  part = ruler_hit(&g, g.x0, 0, 120);
  ruler_drag(&g, part, g.x0 + 2 * 567 + 20, 0);
  ok("ruler: the left indent snaps to a quarter centimetre", g.left == 8 * 142 && g.first == -8 * 142);
  ok("ruler: the drag changes the paragraph", ruler_apply(d, 0, 0, &c, &g, part));
  ok("ruler: its indents as dragged", d->p[0]->pf.left == 1136 && d->p[0]->pf.first == -1136 && d->p[0]->pf.right == 0);
  ok("ruler: a drag that changes nothing is no edit", !ruler_apply(d, 0, 0, &c, &g, part));
  ok("ruler: undone in one step", doc_undo(d, &c) && d->p[0]->pf.left == 0 && d->p[0]->pf.first == 0 && !doc_can_undo(d));
  /* the right indent and the first line, with an inch ruler */
  d->page.width = 12240;
  geom_at(d, P(0, 3), &g);
  ok("ruler: a Letter page has inches", g.metric == 0 && g.width == 12240 - 2880);
  ruler_drag(&g, RP_RIGHT, g.x0 + g.width - 1440 - 50, 0);
  ruler_drag(&g, RP_FIRST, g.x0 + 720 + 60, 0);
  ok("ruler: right and first line dragged", g.right == 1440 && g.first == 720 && g.left == 0);
  ok("ruler: kept", ruler_apply(d, 0, 0, &c, &g, RP_RIGHT) && d->p[0]->pf.right == 1440 && d->p[0]->pf.first == 720);
  ok("ruler: the left indent never goes past the margin", (ruler_drag(&g, RP_LEFT, g.x0 - 900, 0), g.left == 0));
  doc_free(d);
}

static void test_tabs(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  RulerGeom g;
  int part, a;
  doc_insert(d, &c, "One\ttwo\tthree", 13, NULL);
  doc_insert(d, &c, "\nNext", 5, NULL);
  d->page.width = 12240;
  doc_clear_undo(d);
  geom_at(d, P(0, 0), &g);
  ok("ruler: no tab stops yet", g.tabs[0] == 0);
  ok("ruler: no tab stop outside the text", ruler_add_tab(&g, g.x0 - 10, TAB_LEFT) == RP_NONE);
  part = ruler_add_tab(&g, g.x0 + 2900, TAB_CENTER);
  ok("ruler: a click makes a tab stop", part == RP_TAB);
  ruler_drag(&g, part, g.x0 + 2900, 0);
  ok("ruler: snapped to an eighth", g.tabs[0] == 2880 && g.tabkind[0] == TAB_CENTER);
  a = ruler_add_tab(&g, g.x0 + 1450, TAB_RIGHT);
  ruler_drag(&g, a, g.x0 + 1450, 0);
  ok("ruler: a second one before it", a == RP_TAB + 1 && g.tabs[1] == 1440);
  ok("ruler: kept in order, on both paragraphs selected", ruler_apply(d, 1, 0, &c, &g, a));
  ok("ruler: the stops sorted with their kinds", d->p[0]->pf.tabs[0] == 1440 && d->p[0]->pf.tabkind[0] == TAB_RIGHT &&
     d->p[0]->pf.tabs[1] == 2880 && d->p[0]->pf.tabkind[1] == TAB_CENTER && d->p[0]->pf.tabs[2] == 0);
  ok("ruler: the second paragraph too", d->p[1]->pf.tabs[0] == 1440 && d->p[1]->pf.tabs[1] == 2880);
  geom_at(d, P(0, 0), &g);
  ok("ruler: a tab stop is hit before the indents", ruler_hit(&g, g.x0 + 1440 + 40, 0, 120) == RP_TAB);
  ruler_drag(&g, RP_TAB, g.x0 + 1440, 1);
  ok("ruler: dragged off the ruler, it goes", g.tabs[0] == -1);
  ok("ruler: and is left out", ruler_apply(d, 0, 0, &c, &g, RP_TAB) && d->p[0]->pf.tabs[0] == 2880 && d->p[0]->pf.tabs[1] == 0);
  ok("ruler: the other paragraph keeps its stops", d->p[1]->pf.tabs[0] == 1440);
  geom_at(d, P(0, 0), &g);
  ruler_drag(&g, RP_TAB, g.x0 - 500, 0);
  ok("ruler: dragged left of the indent, it goes too", g.tabs[0] == -1);
  /* eight is all a paragraph has */
  for (a = 0; a < RULER_TABS; a++) g.tabs[a] = 720 * (a + 1);
  ok("ruler: no ninth tab stop", ruler_add_tab(&g, g.x0 + 300, TAB_LEFT) == RP_NONE);
  doc_free(d);
}

static void test_columns(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  RulerGeom g;
  Table *t;
  int part;
  doc_insert(d, &c, "Before\nAfter", 12, NULL);
  c = P(1, 0);
  tbl_insert(d, &c, 2, 2, 6000);
  d->page.width = 12240;                /* (an inch ruler: steps of an eighth) */
  doc_clear_undo(d);
  t = doc_table(d, &d->p[c.para]->pf);
  ok("ruler: a table of two columns", t && t->ncols == 2);
  geom_at(d, c, &g);
  ok("ruler: its three column edges", g.ncolx == 3 && g.colx[1] == t->grid[0] && g.colx[2] == t->grid[0] + t->grid[1]);
  ok("ruler: the cell's text area", g.width < 6000 && g.x0 > g.tblx0);
  part = ruler_hit(&g, g.tblx0 + g.colx[1] + 20, 0, 120);
  ok("ruler: the edge between them is hit first", part == RP_COL + 1);
  ruler_drag(&g, part, g.tblx0 + g.colx[2] + 1000, 0);
  ok("ruler: an edge keeps from its neighbour", g.colx[1] == g.colx[2] - RULER_MINCOL);
  ruler_drag(&g, part, g.tblx0 + 3600 + 30, 0);
  ok("ruler: the column made wider", ruler_apply(d, c.para, c.para, &c, &g, part));
  t = doc_table(d, &d->p[c.para]->pf);
  ok("ruler: the widths as dragged, the table as wide", t && t->grid[0] == 3600 && t->grid[0] + t->grid[1] == 6000);
  ok("ruler: undone in one step", doc_undo(d, &c) && (t = doc_table(d, &d->p[c.para]->pf)) && t->grid[0] == 3000);
  doc_free(d);
}

/* a file's bytes (malloc'd), NULL if it can't be read */
static u8 *slurp(const char *path, u32 *n)
{
  FILE *f = fopen(path, "rb");
  u8 *b;
  long len;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((b = (u8 *)malloc((size_t)len)) && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
  fclose(f);
  *n = (u32)len;
  return b;
}

/* the Insert strip's commands (core/edcmd.c) */
static void test_insert(void)
{
  Doc *d = doc_new(), *r;
  ESel s;
  Table *t;
  u8 *png;
  u32 n, i;
  int k, found;
  memset(&s, 0, sizeof s);
  ok("edcmd: text in place of nothing", ec_text(d, &s, "Hello world") && text_is(d, "Hello world"));
  s.anchor = P(0, 6); s.caret = P(0, 11);
  ok("edcmd: text over a selection", ec_text(d, &s, "Mac") && text_is(d, "Hello Mac") && !pos_cmp(s.caret, P(0, 9)));
  doc_clear_undo(d);
  /* a table: 2 rows of 3 at the end, as wide as the page's text */
  ok("edcmd: a table", ec_table(d, &s, 2, 3));
  t = doc_table(d, &d->p[s.caret.para]->pf);
  ok("edcmd: three columns, the page's width", t && t->ncols == 3 && t->grid[0] == (11906 - 2880) / 3 && t->grid[2] == t->grid[0]);
  ok("edcmd: the caret in its first cell", t && d->p[s.caret.para]->pf.cell.row == 0 && d->p[s.caret.para]->pf.cell.col == 0);
  ok("edcmd: one undo step", doc_undo(d, &s.caret) && !doc_table(d, &d->p[d->np - 1]->pf));
  doc_redo(d, &s.caret);
  s.anchor = s.caret = pos_clamp(d, s.caret);
  for (i = 0; i < d->np && !in_table(&d->p[i]->pf); i++) ;
  s.anchor = s.caret = P(i, 0);
  ok("edcmd: a row below", ec_table_op(d, &s, ET_ROW_BELOW));
  ok("edcmd: a column right", ec_table_op(d, &s, ET_COL_RIGHT) && (t = doc_table(d, &d->p[s.caret.para]->pf)) && t->ncols == 4);
  ok("edcmd: a column deleted", ec_table_op(d, &s, ET_DEL_COL) && (t = doc_table(d, &d->p[s.caret.para]->pf)) && t->ncols == 3);
  ok("edcmd: the table deleted", ec_table_op(d, &s, ET_DEL_TABLE));
  for (i = 0, found = 0; i < d->np; i++) found |= in_table(&d->p[i]->pf);
  ok("edcmd: no table left, and no table command outside one", !found && !ec_table_op(d, &s, ET_ROW_ABOVE));
  /* breaks and sections */
  s.anchor = s.caret = P(0, 5);
  ok("edcmd: a page break", ec_break(d, &s, 0) && d->p[s.caret.para]->pf.pagebreak && s.caret.para == 1);
  ok("edcmd: a column break", ec_break(d, &s, 1) && d->p[s.caret.para]->pf.colbreak);
  n = d->np;
  ok("edcmd: a section break", ec_section(d, &s, SB_CONT) && d->np == n + 1 && d->p[s.caret.para - 1]->pf.sect);
  ok("edcmd: the next section starts on the same page", doc_sect(d, s.caret.para)->start == SB_CONT);
  /* a picture, narrowed to the column */
  png = slurp("tests/data/disc.png", &n);
  ok("edcmd: the test picture", png != NULL);
  k = png ? ec_picture(d, &s, png, n) : -1;
  ok("edcmd: a picture at the caret", k >= 0 && IS_PIC(d->p[s.caret.para]->t + s.caret.off - 4));
  ok("edcmd: no wider than the column", k >= 0 && d->images[k].cx <= ec_text_width(d, s.caret.para));
  {
    u8 *junk = (u8 *)malloc(13);
    memcpy(junk, "not a picture", 13);
    ok("edcmd: not a picture: nothing", ec_picture(d, &s, junk, 13) < 0);
  }
  /* a footnote with its text, an endnote, the date */
  k = ec_note(d, &s, 0, "A note's text.");
  ok("edcmd: a footnote", k == 0 && d->nnotes == 1 && IS_NOTE(d->p[s.caret.para]->t + s.caret.off - 4));
  {
    char *nt = k >= 0 ? story_text(&d->notes[k]) : NULL;
    ok("edcmd: its text", nt && strstr(nt, "A note's text.") != NULL);
    free(nt);
  }
  ok("edcmd: an endnote", ec_note(d, &s, 1, "End.") == 0 && d->nenotes == 1);
  ok("edcmd: a date field", ec_field(d, &s, "DATE \\@ \"d MMMM yyyy\""));
  ok("edcmd: a page number field", ec_field(d, &s, "PAGE"));
  {
    Pos at[8];
    ok("edcmd: two fields in the text", field_list(d, doc_start(d), doc_end_pos(d), at, 8) == 2);
  }
  /* all of it kept in a .docx */
  r = docx_again(d, NULL);
  ok("edcmd: read back from .docx", r != NULL);
  if (r) {
    for (i = 0, found = 0; i < r->np; i++) found += r->p[i]->pf.pagebreak;
    ok("edcmd: .docx keeps the page break, picture and notes", found >= 1 && r->nimages >= 1 && r->nnotes == 1 && r->nenotes == 1);
    doc_free(r);
  }
  doc_free(d);
}

/* the Layout strip's commands and the dialogs' measurements */
static void test_layout(void)
{
  Doc *d = doc_new(), *r;
  ESel s;
  ParaFmt pf;
  i32 tw;
  char t[16];
  memset(&s, 0, sizeof s);
  ec_text(d, &s, "One\nTwo\nThree");
  doc_clear_undo(d);
  /* measurements */
  ok("measure: inches", ec_measure("1.25", EU_INCH, &tw) && tw == 1800);
  ok("measure: a comma for the point", ec_measure("2,5", EU_CM, &tw) && tw == 1418);
  ok("measure: points", ec_measure("12", EU_PT, &tw) && tw == 240);
  ok("measure: below zero", ec_measure(" -0.5 ", EU_INCH, &tw) && tw == -720);
  ok("measure: not a number", !ec_measure("abc", EU_INCH, &tw) && !ec_measure("", EU_INCH, &tw) && !ec_measure("1.2x", EU_INCH, &tw));
  ec_measure_text(1800, EU_INCH, t, sizeof t);
  ok("measure: back to text", !strcmp(t, "1.25"));
  ec_measure_text(1440, EU_INCH, t, sizeof t);
  ok("measure: whole", !strcmp(t, "1"));
  ec_measure_text(-720, EU_INCH, t, sizeof t);
  ok("measure: negative", !strcmp(t, "-0.5"));
  ec_measure_text(1134, EU_CM, t, sizeof t);
  ok("measure: centimetres", !strcmp(t, "2"));
  /* the page: Letter landscape, narrow margins */
  ok("page setup", ec_page_setup(d, 15840, 12240, 720, 720, 720, 720) && d->page.width == 15840 && d->page.left == 720);
  ok("page setup: nothing new, no change", !ec_page_setup(d, 15840, 12240, 720, 720, 720, 720));
  ok("page setup: margins wider than the page refused", !ec_page_setup(d, 12240, 15840, 720, 720, 6000, 6000));
  /* columns and line numbers */
  s.anchor = s.caret = P(1, 0);
  ok("two columns", ec_columns(d, &s, 2) && doc_sect(d, 1)->cols == 2);
  ok("columns undone in one step", doc_undo(d, &s.caret) && doc_sect(d, 1)->cols <= 1);
  ec_columns(d, &s, 3);
  ok("line numbers", ec_line_numbers(d, &s, LN_PAGE) && doc_sect(d, 0)->linenum == LN_PAGE);
  /* paragraph formatting on the selection */
  memset(&pf, 0, sizeof pf);
  pf.before = 240; pf.after = 120;
  s.anchor = P(0, 1); s.caret = P(1, 1);
  ok("paragraph spacing on two paragraphs", ec_para(d, &s, PM_SPACING, &pf) && d->p[0]->pf.before == 240 && d->p[1]->pf.after == 120 && d->p[2]->pf.before != 240);
  r = docx_again(d, NULL);
  ok("layout kept in .docx", r && r->page.width == 15840 && doc_sect(r, 0)->cols == 3 && doc_sect(r, 0)->linenum == LN_PAGE && r->p[1]->pf.after == 120);
  doc_free(r);
  doc_free(d);
}

void test_wpmac(void)
{
  test_insert();
  test_layout();
  test_units();
  test_indents();
  test_tabs();
  test_columns();
}
