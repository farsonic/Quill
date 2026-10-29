/* test_wp21b.c - the native tests of WP21b Table drawing and cell options (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp21b() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/table.h"
#include "../core/tstyle.h"
#include "../core/layout.h"
#include "../core/tbldraw.h"
#include "../core/pdf.h"
#include "../core/zip.h"

/* bytes found in a buffer */
static int has_bytes(const u8 *h, size_t n, const char *needle)
{
  size_t k = strlen(needle), i;
  for (i = 0; i + k <= n; i++) if (!memcmp(h + i, needle, k)) return 1;
  return 0;
}
/* text in one of a PDF's (deflated) streams */
static int pdf_has(const u8 *pdf, size_t n, const char *needle)
{
  size_t i;
  u8 *out = (u8 *)malloc(1 << 20);
  int found = 0;
  if (!out) return 0;
  for (i = 0; i + 9 < n && !found; i++)
    if (!memcmp(pdf + i, "stream\n", 7)) {
      long m = zinflate(pdf + i + 9, n - i - 9, out, 1 << 20);
      if (m > 0 && has_bytes(out, (size_t)m, needle)) found = 1;
    }
  free(out);
  return found;
}

/* a document "Before", a rows x cols table whose cells read A, B, C..., then "After" */
static Doc *grid_doc(int rows, int cols, i32 width)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  int r, k;
  doc_insert(d, &c, "Before\nAfter", 12, NULL);
  c = P(1, 0);
  tbl_insert(d, &c, rows, cols, width);
  for (r = rows - 1; r >= 0; r--)
    for (k = cols - 1; k >= 0; k--) {
      char s[2];
      Pos p = P(1 + (u32)(r * cols + k), 0);
      s[0] = (char)('A' + r * cols + k); s[1] = 0;
      doc_insert(d, &p, s, 1, NULL);
    }
  doc_clear_undo(d);
  return d;
}
/* the text of the cell at row, col of the table at paragraph 1 ("?" if there is none) */
static const char *cell_at(Doc *d, int row, int col)
{
  u32 k = tbl_cell_para(d, P(1, 0), row, col);
  return k == (u32)-1 ? "?" : d->p[k]->t;
}
static int row_cells(Doc *d, int row)
{
  u32 i;
  int n = 0, col = -1;
  for (i = 0; i < d->np; i++) if (d->p[i]->pf.cell.table && d->p[i]->pf.cell.row == row && d->p[i]->pf.cell.col != col) { n++; col = d->p[i]->pf.cell.col; }
  return n;
}
static const CellBox *box_of(Layout *L, int row, int col)
{
  u32 i;
  for (i = 0; i < L->ncells; i++) if (L->cells[i].row == row && L->cells[i].col == col && !L->cells[i].depth) return &L->cells[i];
  return NULL;
}

static void cells_options(void)
{
  Doc *d = grid_doc(2, 2, 6000), *e;
  Pos c;
  unsigned lost = 0;
  ok("cell align: bottom right", tbl_cell_align(d, P(2, 0), P(2, 0), 2, AL_RIGHT) && d->p[2]->pf.cell.valign == 2 && d->p[2]->pf.align == AL_RIGHT &&
     d->p[1]->pf.cell.valign == 0 && d->p[1]->pf.align == AL_LEFT);
  ok("cell align: a block, middle centre", tbl_cell_align(d, P(1, 0), P(4, 0), 1, AL_CENTER) && d->p[1]->pf.cell.valign == 1 && d->p[4]->pf.align == AL_CENTER);
  ok("cell align: undone in one step", doc_undo(d, &c) && d->p[4]->pf.cell.valign == 0 && d->p[2]->pf.cell.valign == 2);
  ok("text direction: turned down", tbl_cell_dir(d, P(1, 0), P(1, 0), TD_DOWN) && d->p[1]->pf.cell.tdir == TD_DOWN && d->p[2]->pf.cell.tdir == TD_HORZ);
  {
    i16 m[4] = { 100, 400, 50, 200 };
    ok("cell margins: own left and top", tbl_cell_mar(d, P(3, 0), P(3, 0), (1 << E_LEFT) | (1 << E_TOP), m) && d->p[3]->pf.cell.marset == ((1 << E_LEFT) | (1 << E_TOP)) &&
       d->p[3]->pf.cell.mar[E_LEFT] == 400 && d->p[3]->pf.cell.mar[E_RIGHT] == 0);
  }
  {
    Table *t = doc_table(d, &d->p[1]->pf), nt = *t;
    strcpy(nt.caption, "Prices"); strcpy(nt.descr, "What things cost & when");
    nt.floating = 1; nt.hanchor = TA_MARGIN; nt.vanchor = TA_TEXT; nt.xspec = PA_END; nt.ty = 240; nt.dl = 180; nt.dr = 180; nt.db = 120; nt.nooverlap = 1;
    c = P(1, 0);
    ok("alt text and floating: set on the table", tbl_set_table(d, &c, &nt) && doc_table(d, &d->p[2]->pf)->floating);
  }
  e = docx_again(d, &lost);
  ok("cell options: saved and read back", e && e->np == d->np && e->p[1]->pf.cell.tdir == TD_DOWN && e->p[2]->pf.cell.valign == 2 && e->p[2]->pf.align == AL_RIGHT &&
     e->p[3]->pf.cell.marset == ((1 << E_LEFT) | (1 << E_TOP)) && e->p[3]->pf.cell.mar[E_LEFT] == 400 && e->p[3]->pf.cell.mar[E_TOP] == 100);
  if (e) {
    Table *t = doc_table(e, &e->p[1]->pf);
    ok("alt text and floating: saved and read back", t && !strcmp(t->caption, "Prices") && !strcmp(t->descr, "What things cost & when") && t->floating &&
       t->hanchor == TA_MARGIN && t->vanchor == TA_TEXT && t->xspec == PA_END && t->ty == 240 && t->dl == 180 && t->db == 120 && t->nooverlap);
    doc_free(e);
  }
  doc_free(d);
}

static void delete_cells(void)
{
  Doc *d = grid_doc(3, 3, 6000);
  Pos c = P(2, 0);
  ok("delete cells: shift left", tbl_delete_cells(d, P(2, 0), P(2, 0), DC_LEFT, &c) && row_cells(d, 0) == 2 && !strcmp(cell_at(d, 0, 0), "A") &&
     !strcmp(cell_at(d, 0, 1), "C") && row_cells(d, 1) == 3 && !strcmp(cell_at(d, 1, 1), "E"));
  doc_undo(d, &c);
  c = P(5, 0);
  ok("delete cells: shift up", tbl_delete_cells(d, P(5, 0), P(5, 0), DC_UP, &c) && !strcmp(cell_at(d, 1, 1), "H") && !strcmp(cell_at(d, 2, 1), "") &&
     !strcmp(cell_at(d, 0, 1), "B") && !strcmp(cell_at(d, 1, 0), "D"));
  doc_undo(d, &c);
  c = P(5, 0);
  ok("delete cells: the whole row", tbl_delete_cells(d, P(5, 0), P(5, 0), DC_ROW, &c) && !strcmp(cell_at(d, 1, 0), "G") && cell_at(d, 2, 0)[0] == '?');
  doc_undo(d, &c);
  c = P(2, 0);
  ok("delete cells: two whole columns", tbl_delete_cells(d, P(2, 0), P(3, 0), DC_COL, &c) && doc_table(d, &d->p[1]->pf)->ncols == 1 &&
     !strcmp(cell_at(d, 1, 0), "D") && row_cells(d, 2) == 1 && runs_sane(d));
  doc_free(d);
}

static void turned_text(void)
{
  Doc *d = grid_doc(1, 3, 6000);
  Layout *L;
  u32 i, page;
  i32 x, y, h, len = 0;
  int turned = 0;
  Pos c = P(2, 1), hit;
  doc_insert(d, &c, "Turned cell text", 16, NULL);
  tbl_cell_dir(d, P(2, 0), P(2, 0), TD_DOWN);
  L = lay_new(d, &fs); lay_update(L);
  for (i = 0; i < L->nplaced; i++) if (L->placed[i].para == 2) { turned = L->placed[i].turn; len = L->placed[i].tlen; x = L->placed[i].x; }
  {
    const CellBox *b = box_of(L, 0, 1);
    ok("turned text: one turned line down the cell, the row as tall as it", turned == TD_DOWN && b && len > 1200 && b->h >= len);
  }
  lay_caret(L, P(2, 0), &page, &x, &y, &h);
  {
    i32 y0 = y;
    lay_caret(L, P(2, 17), &page, &x, &y, &h);
    ok("turned text: the caret lies across and moves down", lay_caret_turn == TD_DOWN && lay_caret_w > 100 && y > y0 + 600);
    hit = lay_hit(L, page, x + lay_caret_w / 2, y0 + (y - y0) * 6 / 10);
    ok("turned text: a click lands along it", hit.para == 2 && hit.off > 4 && hit.off < 17);
  }
  {
    size_t n = 0;
    u8 *pdf = pdf_export(L, &n);
    ok("turned text: a turned frame in the PDF", pdf && n && pdf_has(pdf, n, "0 -1 1 0 "));
    free(pdf);
  }
  lay_free(L);
  tbl_cell_dir(d, P(2, 0), P(2, 0), TD_UP);
  tbl_cell_format(d, P(2, 0), P(2, 0), TF_ROWH, (u32)(i32)-800);
  L = lay_new(d, &fs); lay_update(L);
  for (i = 0, turned = 0; i < L->nplaced; i++) if (L->placed[i].para == 2 && L->placed[i].turn == TD_UP) turned++;
  ok("turned text: up, wrapped in an exact row", turned >= 2 && box_of(L, 0, 1) && box_of(L, 0, 1)->h == 800);
  lay_free(L);
  doc_free(d);
}

static void margins_and_floating(void)
{
  Doc *d = grid_doc(1, 2, 3000);
  Layout *L;
  u32 i;
  i32 x0 = 0, x1 = 0, body;
  i16 m[4] = { 0, 600, 0, 0 };
  Pos c;
  L = lay_new(d, &fs); lay_update(L);
  for (i = 0; i < L->nplaced; i++) if (L->placed[i].para == 1) x0 = L->placed[i].x;
  lay_free(L);
  tbl_cell_mar(d, P(1, 0), P(1, 0), 1 << E_LEFT, m);
  L = lay_new(d, &fs); lay_update(L);
  for (i = 0; i < L->nplaced; i++) if (L->placed[i].para == 1) x1 = L->placed[i].x;
  ok("cell margins: the text further in", x1 - x0 == 600 - 108);
  lay_free(L);
  {
    /* a floating table at the right margin: the text after it runs beside it */
    Table nt = *doc_table(d, &d->p[1]->pf);
    Pos e = P(d->np - 1, 0);
    int k;
    nt.floating = 1; nt.hanchor = TA_MARGIN; nt.vanchor = TA_TEXT; nt.xspec = PA_END; nt.dl = 200;
    c = P(1, 0);
    tbl_set_table(d, &c, &nt);
    e.off = d->p[e.para]->len;
    for (k = 0; k < 30; k++) doc_insert(d, &e, " and some more words", 20, NULL);
    L = lay_new(d, &fs); lay_update(L);
    body = d->page.width - d->page.left - d->page.right;
    {
      ParaLay *pl = &L->pl[d->np - 1];
      const CellBox *b = box_of(L, 0, 0);
      Placed *p0 = &L->placed[pl->placed0];
      ok("floating table: placed at the right margin", L->ntfl == 1 && b && L->tfl[0].x + L->tfl[0].w == d->page.width - d->page.right);
      ok("floating table: the text after it wraps beside it", p0->y < L->tfl[0].y + L->tfl[0].h && pl->lines[0].width < body - 3000 + 200 &&
         L->placed[pl->placed0 + pl->nlines - 1].y >= L->tfl[0].y + L->tfl[0].h);
    }
    lay_free(L);
  }
  doc_free(d);
}

static void drawing(void)
{
  Doc *d = grid_doc(2, 2, 6000);
  Layout *L = lay_new(d, &fs);
  const CellBox *b;
  Pos c = P(1, 0);
  int ncols;
  lay_update(L);
  b = box_of(L, 0, 0);
  ok("draw table: a line down a cell splits it", b && tbl_draw_stroke(d, L, b->page, b->x + b->w / 3, b->y - 40, b->x + b->w / 3 + 10, b->y + b->h + 40, &c) &&
     (ncols = doc_table(d, &d->p[1]->pf)->ncols) == 3 && row_cells(d, 0) == 3 && row_cells(d, 1) == 2 && !strcmp(cell_at(d, 0, 0), "A"));
  lay_update(L);
  b = box_of(L, 1, 0);
  ok("draw table: a line across a row splits it in two", b && tbl_draw_stroke(d, L, b->page, b->x + 50, b->y + b->h / 2, b->x + b->w - 50, b->y + b->h / 2, &c) &&
     !strcmp(cell_at(d, 1, 0), "C") && !strcmp(cell_at(d, 2, 0), "") && d->p[tbl_cell_para(d, P(1, 0), 1, 2)]->pf.cell.vmerge == VM_START &&
     d->p[tbl_cell_para(d, P(1, 0), 2, 2)]->pf.cell.vmerge == VM_CONT);
  lay_update(L);
  {
    /* the eraser on the edge between A and the cell drawn beside it */
    const CellBox *a = box_of(L, 0, 0);
    ok("eraser: an inside edge goes, the cells merge", a && tbl_erase_stroke(d, L, a->page, a->x + a->w, a->y + a->h / 2, a->x + a->w + 5, a->y + a->h / 2, &c) &&
       d->p[tbl_cell_para(d, P(1, 0), 0, 0)]->pf.cell.span == 2);
  }
  lay_update(L);
  {
    const CellBox *a = box_of(L, 0, 0);
    ok("eraser: the outside edge loses its line", a && tbl_erase_stroke(d, L, a->page, a->x - 10, a->y + a->h / 2, a->x - 10, a->y + a->h / 2, &c) &&
       !(d->p[tbl_cell_para(d, P(1, 0), 0, 0)]->pf.cell.borders & BD_LEFT));
  }
  lay_update(L);
  {
    /* a box drawn below the table: a new table of one cell */
    u32 last = d->np - 1, page = 0;
    i32 x, y, h;
    u16 nt = (u16)d->ntables;
    lay_caret(L, P(last, 0), &page, &x, &y, &h);
    ok("draw table: a box makes a table that size", tbl_draw_stroke(d, L, page, x + 1440, y + 2, x + 1440 + 2880, y + 1000, &c) && in_table(&d->p[c.para]->pf) &&
       d->p[c.para]->pf.cell.table > nt && doc_table(d, &d->p[c.para]->pf)->grid[0] == 2880 && doc_table(d, &d->p[c.para]->pf)->indent == 1440 &&
       d->p[c.para]->pf.cell.rowh == 998);
    ok("draw table: the box is one undo step", doc_undo(d, &c) && d->ntables >= nt && !in_table(&d->p[d->np - 1]->pf) && runs_sane(d));
  }
  lay_free(L);
  doc_free(d);
}

void test_wp21b(void)
{
  cells_options();
  delete_cells();
  turned_text();
  margins_and_floating();
  drawing();
}
