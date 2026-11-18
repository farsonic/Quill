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

void test_wpmac(void)
{
  test_units();
  test_indents();
  test_tabs();
  test_columns();
}
