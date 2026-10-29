/* test_wp21a.c - the native tests of WP21a Table styles and quick tables (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp21a() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/table.h"
#include "../core/tstyle.h"
#include "../core/tblquick.h"
#include "../core/layout.h"
#include "../core/tbldraw.h"
#include "../core/zip.h"

static Doc *table_doc(int rows, int cols)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  doc_insert(d, &c, "Before\nAfter", 12, NULL);
  c = P(1, 0);
  tbl_insert(d, &c, rows, cols, 6000);
  doc_clear_undo(d);
  return d;
}
/* the styles.xml a document is saved with (malloc'd, NULL if that failed) */
static char *styles_xml(Doc *d)
{
  size_t n = 0, m = 0;
  u8 *z = docx_save(d, &n), *x = NULL;
  ZipReader r;
  int k;
  if (!z) return NULL;
  if (zip_open(&r, z, n) && (k = zip_find(&r, "word/styles.xml")) >= 0) x = zip_read(&r, k, &m);
  zip_close(&r);
  free(z);
  if (x) { char *s = (char *)realloc(x, m + 1); if (s) { s[m] = 0; return s; } free(x); }
  return NULL;
}
static const char *cell_at(Doc *d, Pos in, int row, int col)
{
  u32 k = tbl_cell_para(d, in, row, col);
  return k == (u32)-1 ? "?" : d->p[k]->t;
}

static void style_editor(void)
{
  Doc *d = table_doc(3, 3), *e;
  TableStyle s;
  int k, k2;
  Pos c = P(1, 0);
  unsigned lost = 0;
  char *x;
  tstyle_blank(&s, d, 0, "My Report");
  for (k = 0; k < TE_N; k++) { s.c[TC_WHOLE].b[k].set = 1; s.c[TC_WHOLE].b[k].style = PB_SINGLE; s.c[TC_WHOLE].b[k].width = 4; s.c[TC_WHOLE].b[k].color = 0x808080; }
  s.c[TC_FIRSTROW].used = 1; s.c[TC_FIRSTROW].shade_set = 1; s.c[TC_FIRSTROW].shade = 0x203864;
  s.c[TC_FIRSTROW].cmask = CM_BOLD | CM_COLOR; s.c[TC_FIRSTROW].cf.flags = CF_BOLD; s.c[TC_FIRSTROW].cf.color = 0xFFFFFF;
  s.c[TC_FIRSTROW].valign_set = 1; s.c[TC_FIRSTROW].valign = 2;
  s.c[TC_LASTROW].used = 1; s.c[TC_LASTROW].b[TE_TOP].set = 1; s.c[TC_LASTROW].b[TE_TOP].style = PB_DOUBLE; s.c[TC_LASTROW].b[TE_TOP].width = 4;
  s.c[TC_BAND1H].used = 1; s.c[TC_BAND1H].shade_set = 1; s.c[TC_BAND1H].shade = 0xDDEBF7;
  s.c[TC_NW].used = 1; s.c[TC_NW].shade_set = 1; s.c[TC_NW].shade = 0xC00000;
  k = tstyle_define(d, 0, &s, c);
  ok("new table style: added with an id from its name", k > 0 && !strcmp(d->tstyles[k - 1].id, "MyReport") && d->tstyles[k - 1].custom);
  k2 = tstyle_define(d, 0, &s, c);
  ok("new table style: the same name again gets its own id", k2 > 0 && k2 != k && !strcmp(d->tstyles[k2 - 1].id, "MyReport2"));
  ok("new table style: applied", tbl_apply_style(d, &c, k, LK_FIRSTROW | LK_LASTROW | LK_FIRSTCOL | LK_NOVBAND) &&
     d->p[2]->pf.cell.shade == 0x203864 && d->p[1]->pf.cell.shade == 0xC00000 && d->p[2]->pf.cell.valign == 2 && d->p[4]->pf.cell.shade == 0xDDEBF7 &&
     d->p[8]->pf.cell.bstyle[E_TOP] == PB_DOUBLE && (d->p[2]->r[0].f.flags & CF_BOLD) && d->p[2]->r[0].f.color == 0xFFFFFF);
  x = styles_xml(d);
  ok("new table style: written with its parts", x && strstr(x, "w:customStyle=\"1\" w:styleId=\"MyReport\"") && strstr(x, "w:type=\"nwCell\"") &&
     strstr(x, "<w:vAlign w:val=\"bottom\"/>") && strstr(x, "w:fill=\"DDEBF7\""));
  free(x);
  e = docx_again(d, &lost);
  ok("new table style: read back the same", e && doc_tstyle(e, "MyReport") && e->tstyles[doc_tstyle(e, "MyReport") - 1].custom &&
     e->tstyles[doc_tstyle(e, "MyReport") - 1].c[TC_FIRSTROW].valign == 2 && e->tstyles[doc_tstyle(e, "MyReport") - 1].c[TC_NW].shade == 0xC00000 &&
     e->p[2]->pf.cell.valign == 2 && same_doc(d, e));
  doc_free(e);
  /* Modify: the heading row green; the table follows */
  s = d->tstyles[k - 1];
  s.c[TC_FIRSTROW].shade = 0x00B050;
  strcpy(s.name, "My Report Green");
  ok("modify table style: the table follows", tstyle_define(d, k, &s, c) == k && !strcmp(d->tstyles[k - 1].id, "MyReport") &&
     !strcmp(d->tstyles[k - 1].name, "My Report Green") && d->p[2]->pf.cell.shade == 0x00B050 && tstyle_uses(d, k) == 1);
  ok("modify table style: one undo step for the tables", doc_undo(d, &c) && d->p[2]->pf.cell.shade == 0x203864);
  doc_redo(d, &c);
  /* Delete: the table keeps its look without a style */
  ok("delete table style: gone, the table keeps its look", tstyle_delete(d, k, c) && d->tstyles[k - 1].gone && doc_table(d, &d->p[1]->pf)->tstyle == 0 &&
     d->p[2]->pf.cell.shade == 0x00B050 && tstyle_uses(d, k) == 0);
  ok("delete table style: Table Grid can't go", !tstyle_delete(d, doc_tstyle(d, "TableGrid"), c));
  x = styles_xml(d);
  ok("delete table style: not saved", x && !strstr(x, "\"MyReport\"") && strstr(x, "\"MyReport2\""));
  free(x);
  doc_free(d);
}

static void quick_tables(void)
{
  int k;
  for (k = 0; k < QT_N; k++) {
    Doc *d = doc_new();
    Pos c = doc_start(d);
    char what[64];
    int ok1 = tbl_quick(d, &c, k, 2026, 10, NULL, 9000);
    sprintf(what, "quick tables: kind %d goes in", k);
    ok(what, ok1 && in_table(&d->p[c.para]->pf) && runs_sane(d) && doc_can_undo(d));
    if (k == QT_CAL1) {
      /* October 2026 starts on a Thursday */
      ok("quick tables: a calendar's title, days and dates", date_wday(2026, 10, 1) == 3 && !strcmp(cell_at(d, c, 0, 0), "October 2026") &&
         d->p[c.para]->pf.cell.span == 7 && !strcmp(cell_at(d, c, 1, 0), "Mo") && !strcmp(cell_at(d, c, 2, 3), "1") && !strcmp(cell_at(d, c, 6, 5), "31"));
    }
    if (k == QT_CAL2) ok("quick tables: Sunday first", !strcmp(cell_at(d, c, 1, 0), "Su") && !strcmp(cell_at(d, c, 2, 4), "1"));
    if (k == QT_CAL3) ok("quick tables: week numbers", !strcmp(cell_at(d, c, 2, 0), "40") && !strcmp(cell_at(d, c, 3, 0), "41"));
    if (k == QT_MATRIX) ok("quick tables: a matrix", !strcmp(cell_at(d, c, 0, 2), "Point B") && !strcmp(cell_at(d, c, 2, 1), "12"));
    if (k == QT_SUBHEADS1) ok("quick tables: subheadings across", !strcmp(cell_at(d, c, 1, 0), "Subheading 1") && d->p[tbl_cell_para(d, c, 1, 0)]->pf.cell.span == 4 &&
                              !strcmp(cell_at(d, c, 8, 1), "963"));
    {
      Doc *e = docx_again(d, NULL);
      sprintf(what, "quick tables: kind %d saved and read back", k);
      ok(what, e && same_doc(d, e));
      doc_free(e);
    }
    doc_free(d);
  }
}

static void csv(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  const char *t = "Name,Price,Note\r\nTea,1.20,\"hot, sweet\"\r\n\"Bun \"\"large\"\"\",2,\"two\nlines\"\r\n";
  ok("spreadsheet: CSV as a table", tbl_from_csv(d, &c, t, strlen(t), 9000) && in_table(&d->p[c.para]->pf) && doc_table(d, &d->p[c.para]->pf)->ncols == 3 &&
     !strcmp(cell_at(d, c, 1, 2), "hot, sweet") && !strcmp(cell_at(d, c, 2, 0), "Bun \"large\"") && !strcmp(cell_at(d, c, 2, 2), "two\vlines") &&
     d->p[tbl_cell_para(d, c, 1, 1)]->pf.align == AL_RIGHT && d->p[tbl_cell_para(d, c, 1, 0)]->pf.align == AL_LEFT && d->p[c.para]->pf.cell.header);
  doc_free(d);
  d = doc_new(); c = doc_start(d);
  t = "Stadt;Einwohner\nK\xF6ln;1084000\n\n";
  ok("spreadsheet: semicolons, Latin-1, a blank line", tbl_from_csv(d, &c, t, strlen(t), 9000) && doc_table(d, &d->p[c.para]->pf)->ncols == 2 &&
     !strcmp(cell_at(d, c, 1, 0), "K\xC3\xB6ln") && tbl_cell_para(d, c, 2, 0) == (u32)-1 && doc_undo(d, &c) && !in_table(&d->p[0]->pf));
  doc_free(d);
}

static void border_painter(void)
{
  Doc *d = table_doc(2, 2);
  Layout *L = lay_new(d, &fs);
  const CellBox *a = NULL, *b = NULL;
  Pos c;
  u32 i;
  lay_update(L);
  for (i = 0; i < L->ncells; i++) { if (L->cells[i].row == 0 && L->cells[i].col == 0) a = &L->cells[i]; if (L->cells[i].row == 0 && L->cells[i].col == 1) b = &L->cells[i]; }
  ok("border painter: a click paints the edge between two cells, both sides", a && b &&
     tbl_paint_stroke(d, L, a->page, a->x + a->w + 20, a->y + a->h / 2, a->x + a->w + 20, a->y + a->h / 2, PB_DOUBLE, 12, 0xFF0000, &c) &&
     d->p[1]->pf.cell.bstyle[E_RIGHT] == PB_DOUBLE && d->p[1]->pf.cell.bcolor[E_RIGHT] == 0xFF0000 && d->p[2]->pf.cell.bstyle[E_LEFT] == PB_DOUBLE &&
     d->p[2]->pf.cell.bwidth[E_LEFT] == 12 && d->p[1]->pf.cell.bstyle[E_TOP] != PB_DOUBLE);
  lay_update(L);
  for (i = 0; i < L->ncells; i++) if (L->cells[i].row == 0 && L->cells[i].col == 0) a = &L->cells[i];
  ok("border painter: a stroke along the top paints the row's top edges", a &&
     tbl_paint_stroke(d, L, a->page, a->x + 20, a->y, a->x + 2 * a->w - 20, a->y + 10, PB_DASHED, 8, 0x0000FF, &c) &&
     d->p[1]->pf.cell.bstyle[E_TOP] == PB_DASHED && d->p[2]->pf.cell.bstyle[E_TOP] == PB_DASHED && d->p[3]->pf.cell.bstyle[E_TOP] != PB_DASHED);
  lay_free(L);
  doc_free(d);
}

void test_wp21a(void)
{
  style_editor();
  quick_tables();
  csv();
  border_painter();
}
