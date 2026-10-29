/* table.c - table editing (see table.h). Each operation builds the table's new paragraphs in a
   PList and swaps them in with commit, so it is one doc_replace and one undo step */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "table.h"
#include "tstyle.h"

/* Tables inside table cells: an operation works on the innermost table at the caret (cur); its
   paragraphs include those of tables nested in its cells, whose cell in it is their outer one. CELL(i)
   is paragraph i's cell in cur (for one outside cur, its own cell, as before nesting) */
static u16 cur;
/* the Cell paragraph i has in cur (see above): a pointer into the paragraph itself */
static Cell *cellp(Doc *d, u32 i)
{
  Cell *c = (Cell *)cell_in(&d->p[i]->pf, cur);
  return c ? c : &d->p[i]->pf.cell;
}
#define CELL(i) (*cellp(d, (i)))
/* a new paragraph's cell in cur */
static Cell *pcell(Para *p) { return cur && p->pf.outer.table == cur ? &p->pf.outer : &p->pf.cell; }
/* make pi's innermost table the one operated on (cur; 0 past the document's end). Every public
   operation calls this first, as cur is shared by everything here */
static void use_table(Doc *d, u32 pi) { cur = pi < d->np ? d->p[pi]->pf.cell.table : 0; }
/* paragraphs i and j in the same cell of table t */
static int same_in(Doc *d, u32 i, u32 j, u16 t)
{
  const Cell *a = cell_in(&d->p[i]->pf, t), *b = cell_in(&d->p[j]->pf, t);
  return a && b && a->row == b->row && a->col == b->col;
}
/* the first paragraph of table t that pi is in (paragraphs of tables nested in t count as t's);
   pi itself if t is 0 */
static u32 tbl_first_t(Doc *d, u32 pi, u16 t)
{
  while (pi > 0 && t && cell_in(&d->p[pi - 1]->pf, t)) pi--;
  return pi;
}
/* one past the last paragraph of table t from pi on; pi + 1 if t is 0 */
static u32 tbl_end_t(Doc *d, u32 pi, u16 t)
{
  if (!t) return pi + 1;
  while (pi < d->np && cell_in(&d->p[pi]->pf, t)) pi++;
  return pi;
}
/* one past the last paragraph of pi's cell in table t (pi + 1 if pi isn't in t) */
static u32 cell_end_t(Doc *d, u32 pi, u16 t)
{
  u32 j = pi + 1;
  if (!cell_in(&d->p[pi]->pf, t)) return j;
  while (j < d->np && same_in(d, j, pi, t)) j++;
  return j;
}
/* the public versions work on pi's own (innermost) table, whatever cur is */
u32 tbl_first(Doc *d, u32 pi) { return tbl_first_t(d, pi, d->p[pi]->pf.cell.table); }
u32 tbl_end(Doc *d, u32 pi) { return tbl_end_t(d, pi, d->p[pi]->pf.cell.table); }
/* back from pi to the first paragraph of its cell in its own table */
u32 cell_first(Doc *d, u32 pi)
{
  u16 t = d->p[pi]->pf.cell.table;
  while (pi > 0 && t && same_in(d, pi - 1, pi, t)) pi--;
  return pi;
}
/* one past the last paragraph of pi's cell in its own table */
u32 cell_end(Doc *d, u32 pi) { return cell_end_t(d, pi, d->p[pi]->pf.cell.table); }

/* a growing list of new paragraphs */
typedef struct { Para **p; u32 n, cap; int oom; } PList;
/* append p to l. A NULL p (a failed para_new or para_copy) or a failed grow sets l->oom instead, so
   callers can add without checking and let commit give up; on a failed grow p is freed */
static void pl_add(PList *l, Para *p)
{
  if (!p) { l->oom = 1; return; }
  if (l->n >= l->cap) {
    u32 nc = l->cap ? l->cap * 2 : 32;
    Para **np = (Para **)realloc(l->p, nc * sizeof(Para *));
    if (!np) { para_free(p); l->oom = 1; return; }
    l->p = np; l->cap = nc;
  }
  l->p[l->n++] = p;
}
/* free l's paragraphs and the list itself, leaving it empty (for giving up on a rebuild) */
static void pl_drop(PList *l)
{
  u32 i;
  for (i = 0; i < l->n; i++) para_free(l->p[i]);
  free(l->p);
  memset(l, 0, sizeof *l);
}
/* copy paragraph pi with a new cell (in cur: its outer cell, if it is in a table nested in cur) */
static Para *copy_cell(Doc *d, u32 pi, const Cell *c)
{
  Para *p = para_copy(d->p[pi]);
  if (p) { if (cur && p->pf.outer.table == cur) p->pf.outer = *c; else p->pf.cell = *c; }
  return p;
}
/* an empty paragraph for a new cell, formatted like paragraph like */
static Para *empty_cell(Doc *d, u32 like, const Cell *c)
{
  ParaFmt pf = d->p[like]->pf;
  if (cur && pf.outer.table == cur) memset(&pf.outer, 0, sizeof pf.outer);   /* a paragraph of the cell itself, not of a table in it */
  pf.cell = *c; pf.list = 0; pf.level = 0; pf.pagebreak = 0;
  return para_new(&pf, &d->p[like]->r[0].f);
}
/* put l in place of paragraphs [a, b) with doc_replace (one undo step) and free the list; the caret
   goes to the start of new paragraph at (clamped to the document's last paragraph). 0 if l ran out
   of memory (its paragraphs are freed) or doc_replace failed */
static int commit(Doc *d, u32 a, u32 b, PList *l, Pos *caret, u32 at)
{
  Pos was = *caret;
  if (l->oom) { pl_drop(l); return 0; }
  if (!doc_replace(d, a, b - a, l->p, l->n, was)) { free(l->p); return 0; }
  free(l->p);
  caret->para = a + at < d->np ? a + at : d->np - 1;
  caret->off = 0;
  return 1;
}

/* tbl_insert's work, cur already set. A caret in the middle of a paragraph splits it first and the
   table goes after the first half; inside a cell the table is nested (one level only). Cell margins
   of 108 twips each side are Word's default. Between doc_begin and doc_end, so the split and the
   table are one undo step */
static int tbl_insert_(Doc *d, Pos *at, int rows, int cols, i32 width)
{
  Table t;
  PList l;
  Cell c;
  ParaFmt pf;
  u32 idx;
  int r, k, id, ok;
  Cell outer;
  /* in a cell: a table nested in it (one level deep) */
  if (rows < 1 || cols < 1 || cols > MAX_COLS || rows > 32767 || at->para >= d->np || is_nested(&d->p[at->para]->pf)) return 0;
  outer = d->p[at->para]->pf.cell;
  memset(&t, 0, sizeof t);
  t.ncols = (u8)cols; t.marl = t.marr = 108;
  t.tstyle = (u8)tstyle_use_builtin(d, 0);           /* Table Grid, as Word's new tables */
  t.look = LK_DEFAULT;
  for (k = 0; k < cols; k++) t.grid[k] = width / cols;
  if (!(id = doc_add_table(d, &t))) return 0;
  doc_begin(d, *at);
  if (at->off > 0 && at->off < d->p[at->para]->len) {
    if (!doc_insert(d, at, "\n", 1, NULL)) { doc_end(d, *at); return 0; }
  }
  idx = at->off ? at->para + 1 : at->para;
  memset(&l, 0, sizeof l);
  memset(&c, 0, sizeof c);
  c.table = (u16)id; c.span = 1; c.borders = BD_ALL; c.shade = COLOR_AUTO;
  for (k = 0; k < 4; k++) c.bcolor[k] = COLOR_AUTO;
  pf = d->styles[0].pf; pf.style = 0;
  pf.outer = outer;
  for (r = 0; r < rows; r++)
    for (k = 0; k < cols; k++) {
      c.row = (u16)r; c.col = (u8)k;
      pf.cell = c;
      pl_add(&l, para_new(&pf, &d->styles[0].cf));
    }
  /* a paragraph after it: at the document's end, or in the cell (Word ends a cell with one) */
  if (outer.table ? !(idx < d->np && !is_nested(&d->p[idx]->pf) && same_cell(&d->p[idx]->pf, &d->p[at->para]->pf)) : idx >= d->np) {
    pf.cell = outer; memset(&pf.outer, 0, sizeof pf.outer);
    pl_add(&l, para_new(&pf, &d->styles[0].cf));
  }
  ok = commit(d, idx, idx, &l, at, 0);
  doc_end(d, *at);
  return ok;
}

/* an empty row above (below) the caret's, a copy of its cells' layout (spans, borders, fill, but not
   vertical merging); the rows after it are renumbered. The caret goes to the new row's first cell */
static int tbl_row_insert_(Doc *d, Pos *caret, int below)
{
  u32 a, b, i, at = 0;
  u16 r;
  PList l;
  int placed = 0;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf)) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  r = CELL(caret->para).row;
  memset(&l, 0, sizeof l);
  for (i = a; i <= b; i++) {
    /* the new row goes before row r's first paragraph, or after its last */
    int here = !placed && (i == b || (below ? CELL(i).row > r : CELL(i).row >= r));
    if (here) {
      u32 j;
      placed = 1; at = l.n;
      for (j = a; j < b; j++)
        if (CELL(j).row == r && (j == a || !same_in(d, j - 1, j, cur))) {
          Cell c = CELL(j);
          c.row = (u16)(below ? r + 1 : r); c.vmerge = VM_NONE;
          pl_add(&l, empty_cell(d, j, &c));
        }
    }
    if (i < b) {
      Cell c = CELL(i);
      if (below ? c.row > r : c.row >= r) c.row++;
      pl_add(&l, copy_cell(d, i, &c));
    }
  }
  return commit(d, a, b, &l, caret, at);
}

/* the caret's whole table out of the document; the caret goes to what followed it. A nested table
   leaves a paragraph in its cell if the cell would otherwise be empty, and a table that was the whole
   document leaves one empty paragraph */
static int tbl_delete_(Doc *d, Pos *caret)
{
  u32 a, b;
  PList l;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf)) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  memset(&l, 0, sizeof l);
  if (is_nested(&d->p[a]->pf)) {        /* a nested table: its cell keeps a paragraph */
    const Cell *o = &d->p[a]->pf.outer;
    int kept = (a > 0 && same_in(d, a - 1, a, o->table)) || (b < d->np && same_in(d, b, a, o->table));
    if (!kept) { ParaFmt pf = d->styles[0].pf; pf.style = 0; pf.cell = *o; pl_add(&l, para_new(&pf, &d->styles[0].cf)); }
  } else if (b - a == d->np) {          /* the whole document: an empty paragraph is left */
    ParaFmt pf = d->styles[0].pf;
    pf.style = 0;
    pl_add(&l, para_new(&pf, &d->styles[0].cf));
  }
  return commit(d, a, b, &l, caret, 0);
}

/* the caret's row out, the rows below moving up; the only row takes the table with it. The caret
   goes into the next row at about the same column, or into the row above if it was the last */
static int tbl_row_delete_(Doc *d, Pos *caret)
{
  u32 a, b, i, at = 0;
  u16 r;
  u8 col;
  PList l;
  int found = 0, any = 0;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf)) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  r = CELL(caret->para).row; col = CELL(caret->para).col;
  for (i = a; i < b; i++) if (CELL(i).row != r) any = 1;
  if (!any) return tbl_delete(d, caret);
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    if (c.row == r) continue;
    if (c.row > r) c.row--;
    if (!found && c.row >= r && c.col >= col) { found = 1; at = l.n; }
    pl_add(&l, copy_cell(d, i, &c));
  }
  if (!found) {                         /* the last row went: the caret goes into the one above */
    for (i = l.n; i > 0; i--) if (pcell(l.p[i - 1])->col <= col) { at = i - 1; break; }
    if (at < l.n) while (at > 0 && pcell(l.p[at - 1])->row == pcell(l.p[at])->row && pcell(l.p[at - 1])->col == pcell(l.p[at])->col) at--;
  }
  return commit(d, a, b, &l, caret, at);
}

/* a new table like t with a new grid; returns its number */
static int new_grid(Doc *d, const Table *t, const i32 *grid, int ncols)
{
  Table nt = *t;
  int k;
  nt.ncols = (u8)ncols;
  for (k = 0; k < MAX_COLS; k++) nt.grid[k] = k < ncols ? grid[k] : 0;
  return doc_add_table(d, &nt);
}

/* a new grid column left (right) of the caret's cell. The table keeps its total width, the other
   columns giving up their share; the new grid needs a new Table (the old one stays for undo). In a
   row where a cell spans the place, that cell just gets wider. The caret goes to the new cell in its
   row */
static int tbl_col_insert_(Doc *d, Pos *caret, int right)
{
  u32 a, b, i, at = 0;
  Table *t;
  i32 grid[MAX_COLS], total = 0, w;
  int ins, k, n, id;
  u16 crow;
  PList l;
  if (caret->para >= d->np || !(t = doc_table(d, &d->p[caret->para]->pf)) || t->ncols >= MAX_COLS) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  {
    const Cell *cc = &CELL(caret->para);
    ins = right ? cc->col + cc->span : cc->col;
    crow = cc->row;
  }
  n = t->ncols;
  if (ins > n) ins = n;
  for (k = 0; k < n; k++) total += t->grid[k];
  /* the table keeps its width: the new column gets an equal share, the others shrink */
  w = total / (n + 1);
  for (k = 0; k < n; k++) grid[k < ins ? k : k + 1] = (i32)((long)t->grid[k] * (total - w) / (total ? total : 1));
  grid[ins] = w;
  if (!(id = new_grid(d, t, grid, n + 1))) return 0;
  memset(&l, 0, sizeof l);
  i = a;
  while (i < b) {
    u16 r = CELL(i).row;
    int done = 0;
    u32 rowstart = i;
    while (i < b && CELL(i).row == r) {
      Cell c = CELL(i);
      int first = i == rowstart || !same_in(d, i - 1, i, cur);
      if (first && !done && c.col >= ins) {        /* the new cell goes before this one */
        Cell nc = c;
        nc.table = (u16)id; nc.col = (u8)ins; nc.span = 1; nc.vmerge = VM_NONE;
        if (r == crow) at = l.n;
        pl_add(&l, empty_cell(d, i, &nc));
        done = 1;
      }
      if (c.col >= ins) c.col++;
      else if (c.col + c.span > ins) { c.span++; done = 1; }   /* a cell spanning the place grows */
      c.table = (u16)id;
      pl_add(&l, copy_cell(d, i, &c));
      i++;
    }
    if (!done) {                                    /* at the row's end */
      Cell nc = CELL(i - 1);
      nc.table = (u16)id; nc.col = (u8)ins; nc.span = 1; nc.vmerge = VM_NONE;
      if (r == crow) at = l.n;
      pl_add(&l, empty_cell(d, i - 1, &nc));
    }
  }
  return commit(d, a, b, &l, caret, at);
}

/* the grid column the caret's cell starts in, out: cells only in it go, cells spanning it get
   narrower, rows left with no cells disappear, and the narrower grid gets a new Table. Deleting the
   last column deletes the table */
static int tbl_col_delete_(Doc *d, Pos *caret)
{
  u32 a, b, i, at = 0;
  Table *t;
  i32 grid[MAX_COLS];
  int col, k, n, id, gotcaret = 0;
  u16 crow, newrow = 0, lastrow = 0xFFFF;
  PList l;
  if (caret->para >= d->np || !(t = doc_table(d, &d->p[caret->para]->pf))) return 0;
  if (t->ncols <= 1) return tbl_delete(d, caret);
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  col = CELL(caret->para).col; crow = CELL(caret->para).row;
  n = t->ncols;
  for (k = 0; k < n - 1; k++) grid[k] = t->grid[k < col ? k : k + 1];
  if (!(id = new_grid(d, t, grid, n - 1))) return 0;
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    u16 oldrow = c.row;
    if (c.col <= col && c.col + c.span > col) {
      if (c.span == 1) continue;                    /* the cell goes */
      c.span--;
    } else if (c.col > col) c.col--;
    if (oldrow != lastrow) { if (lastrow != 0xFFFF) newrow++; lastrow = oldrow; }   /* rows left empty disappear */
    c.row = newrow; c.table = (u16)id;
    if (!gotcaret && oldrow == crow && c.col + c.span >= col) { at = l.n; gotcaret = 1; }
    pl_add(&l, copy_cell(d, i, &c));
  }
  if (!l.n) { pl_drop(&l); return tbl_delete(d, caret); }
  return commit(d, a, b, &l, caret, at);
}

/* the caret to the start of the next (dir > 0) or previous cell of its table, for Tab-style moving;
   0 (caret unmoved) at the table's first or last cell */
static int tbl_next_cell_(Doc *d, Pos *caret, int dir)
{
  u32 pi = caret->para;
  if (pi >= d->np || !in_table(&d->p[pi]->pf)) return 0;
  if (dir > 0) {
    u32 e = cell_end(d, pi);
    if (e >= d->np || CELL(e).table != CELL(pi).table) return 0;
    caret->para = e;
  } else {
    u32 s = cell_first(d, pi);
    if (!s || CELL(s - 1).table != CELL(pi).table) return 0;
    caret->para = cell_first(d, s - 1);
  }
  caret->off = 0;
  return 1;
}

/* the cells touched by a..b (in a's table) get one property, what being a TF_ (see table.h): cell by
   cell for TF_VALIGN and TF_SHADE, whole rows for the others. Within one row that means the cells
   from a's to b's; over several rows, the block of columns between them. Positions aren't moved */
static int tbl_cell_format_(Doc *d, Pos pa, Pos pb, int what, u32 value)
{
  u32 a, b, i, lo, hi, at = 0;
  Cell s, e;
  PList l;
  Pos keep = pa;
  if (pos_cmp(pa, pb) > 0) { Pos t = pa; pa = pb; pb = t; }
  if (pa.para >= d->np || !in_table(&d->p[pa.para]->pf)) return 0;
  a = tbl_first(d, pa.para); b = tbl_end(d, pa.para);
  if (pb.para >= b) pb.para = b - 1;
  s = CELL(pa.para); e = CELL(pb.para);
  lo = cell_first(d, pa.para); hi = cell_end(d, pb.para);
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    /* a block of cells: rows s..e, the columns between them (one row: the cells from s to e) */
    int in = s.row == e.row ? (i >= lo && i < hi)
                            : (c.row >= s.row && c.row <= e.row && c.col + c.span > (s.col < e.col ? s.col : e.col) && c.col <= (s.col > e.col ? s.col : e.col));
    if (what >= TF_ROWH) in = c.row >= (s.row < e.row ? s.row : e.row) && c.row <= (s.row > e.row ? s.row : e.row);    /* whole rows */
    if (in) {
      switch (what) {
      case TF_VALIGN: c.valign = (u8)value; break;
      case TF_SHADE: c.shade = value; break;
      case TF_ROWH: c.rowh = (i16)(i32)value; break;
      case TF_HEADER: c.header = (u8)value; break;
      case TF_CANTSPLIT: c.cantsplit = (u8)value; break;
      }
    }
    if (i == keep.para) at = l.n;
    pl_add(&l, copy_cell(d, i, &c));
  }
  if (!commit(d, a, b, &l, &keep, at)) return 0;
  return 1;
}
/* how many rows table cur has among paragraphs [a, b): the highest row number plus one */
static int table_rows(Doc *d, u32 a, u32 b)
{
  u32 i;
  int n = 0;
  for (i = a; i < b; i++) if (CELL(i).row + 1 > n) n = CELL(i).row + 1;
  return n;
}
/* put the caret back where it was in the rebuilt table (commit puts it at a paragraph's start) */
static int commit_keep(Doc *d, u32 a, u32 b, PList *l, Pos *caret)
{
  Pos was = *caret;
  if (!commit(d, a, b, l, caret, was.para - a)) return 0;
  caret->off = was.off <= d->p[caret->para]->len ? was.off : d->p[caret->para]->len;
  return 1;
}
/* tbl_apply_style's work. A new Table carries the style and look (and the style's cell margins, if
   it sets them); each cell's borders and fill come from the style for its place. Text formatting the
   old or the new style touches goes back to the paragraph style's first and then takes the new
   style's, so nothing of the old style is left behind */
static int tbl_apply_style_(Doc *d, Pos *caret, int ts, u16 look)
{
  Table nt, *t;
  u32 a, b, i;
  unsigned m;
  int id, nrows, old, k, anyalign = 0;
  PList l;
  if (caret->para >= d->np || !(t = doc_table(d, &d->p[caret->para]->pf))) return 0;
  if (ts < 0 || ts > d->ntstyles) return 0;
  nt = *t;
  old = t->tstyle;
  nt.tstyle = (u8)ts; nt.look = look;
  if (ts && d->tstyles[ts - 1].mar_set) { nt.marl = d->tstyles[ts - 1].marl; nt.marr = d->tstyles[ts - 1].marr; }
  if (!(id = doc_add_table(d, &nt))) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  nrows = table_rows(d, a, b);
  m = tstyle_cmask(d, old) | tstyle_cmask(d, ts);
  /* does either style align paragraphs? Then the text goes back to its own style's alignment first */
  {
    const int st[2] = { old, ts };
    int j, c;
    for (j = 0; j < 2; j++) if (st[j] > 0 && st[j] <= d->ntstyles) for (c = 0; c < TC_N; c++) if (d->tstyles[st[j] - 1].c[c].align_set) anyalign = 1;
  }
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    CellLook lk;
    Para *p;
    u32 r;
    tstyle_cell(d, ts, look, c.row, c.col, c.span, nrows, nt.ncols, NULL, &lk);
    c.table = (u16)id; c.borders = lk.borders; c.shade = lk.shade;
    if (lk.valign_set) c.valign = lk.valign;             /* (WP21b) */
    for (k = 0; k < 4; k++) { c.bstyle[k] = lk.bstyle[k]; c.bwidth[k] = lk.bwidth[k]; c.bcolor[k] = lk.bcolor[k]; }
    if (!(p = copy_cell(d, i, &c))) { l.oom = 1; continue; }
    if (anyalign) p->pf.align = lk.align_set ? lk.align : d->styles[p->pf.style].pf.align;
    for (r = 0; r < p->nr; r++) {
      cf_apply(&p->r[r].f, m, &d->styles[p->pf.style].cf);
      cf_apply(&p->r[r].f, lk.cmask, &lk.cf);
    }
    para_normalise(p);
    pl_add(&l, p);
  }
  return commit_keep(d, a, b, &l, caret);
}
/* every cell of the caret's table pointed at a new Table copied from nt; cells, text and caret are
   otherwise as they were */
static int tbl_set_table_(Doc *d, Pos *caret, const Table *nt)
{
  u32 a, b, i;
  int id;
  PList l;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf)) return 0;
  if (!(id = doc_add_table(d, nt))) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    c.table = (u16)id;
    pl_add(&l, copy_cell(d, i, &c));
  }
  return commit_keep(d, a, b, &l, caret);
}

/* ------------------------------------------------------------------ */
/* a table's cells: each one's paragraphs and Cell */
typedef struct { u32 p0, p1; Cell c; } CInfo;
typedef struct { CInfo *v; int n; int nrows; u32 a, b; } CList;
/* fill cl with the cells of pi's table cur (use_table first) in paragraph order, and count its rows.
   The caller frees cl->v; 0 if out of memory */
static int cells_of(Doc *d, u32 pi, CList *cl)
{
  u32 i;
  memset(cl, 0, sizeof *cl);
  cl->a = tbl_first(d, pi); cl->b = tbl_end(d, pi);
  if (!(cl->v = (CInfo *)malloc((cl->b - cl->a) * sizeof(CInfo)))) return 0;
  for (i = cl->a; i < cl->b; i = cell_end_t(d, i, cur)) {
    CInfo *ci = &cl->v[cl->n++];
    ci->p0 = i; ci->p1 = cell_end_t(d, i, cur); ci->c = CELL(i);
    if (ci->c.row + 1 > cl->nrows) cl->nrows = ci->c.row + 1;
  }
  return 1;
}
/* the cell covering grid column col in row r (-1 if none) */
static int cell_at(const CList *cl, int r, int col)
{
  int k;
  for (k = 0; k < cl->n; k++) if (cl->v[k].c.row == r && col >= cl->v[k].c.col && col < cl->v[k].c.col + cl->v[k].c.span) return k;
  return -1;
}
/* the block of cells between a's and b's: rows r0..r1, grid columns c0..c1, grown to whole cells
   (merged ones included) */
static void block_of(Doc *d, const CList *cl, Pos pa, Pos pb, int *r0, int *r1, int *c0, int *c1)
{
  const Cell *x = &CELL(pa.para), *y = &CELL(pb.para);
  int k, grew = 1, guard = 0;
  *r0 = x->row < y->row ? x->row : y->row; *r1 = x->row > y->row ? x->row : y->row;
  *c0 = x->col < y->col ? x->col : y->col;
  *c1 = (x->col + x->span > y->col + y->span ? x->col + x->span : y->col + y->span) - 1;
  while (grew && guard++ < 64) {
    grew = 0;
    for (k = 0; k < cl->n; k++) {
      const Cell *c = &cl->v[k].c;
      int in = c->row >= *r0 && c->row <= *r1 && c->col <= *c1 && c->col + c->span - 1 >= *c0;
      if (!in) continue;
      if (c->col < *c0) { *c0 = c->col; grew = 1; }
      if (c->col + c->span - 1 > *c1) { *c1 = c->col + c->span - 1; grew = 1; }
      /* a merged cell going on above or below */
      if (c->vmerge == VM_CONT && c->row == *r0 && *r0 > 0) { int u = cell_at(cl, *r0 - 1, c->col); if (u >= 0 && cl->v[u].c.vmerge) { (*r0)--; grew = 1; } }
      if (c->vmerge && c->row == *r1) { int u = cell_at(cl, *r1 + 1, c->col); if (u >= 0 && cl->v[u].c.vmerge == VM_CONT) { (*r1)++; grew = 1; } }
    }
  }
}
/* the whole of cell c lies inside rows r0..r1, grid columns c0..c1 */
static int in_block(const Cell *c, int r0, int r1, int c0, int c1)
{
  return c->row >= r0 && c->row <= r1 && c->col >= c0 && c->col + c->span - 1 <= c1;
}

/* merge the block of cells from a's to b's into one: the top left cell takes the block's columns
   (and rows: the ones below are merged into it) and the text of all of them */
static int tbl_merge_(Doc *d, Pos pa, Pos pb)
{
  CList cl;
  int r0, r1, c0, c1, k, tl = -1, n = 0;
  u32 i, at = 0;
  PList l;
  Cell top;
  if (pa.para >= d->np || pb.para >= d->np || !in_table(&d->p[pa.para]->pf) || CELL(pa.para).table != CELL(pb.para).table) return 0;
  if (!cells_of(d, pa.para, &cl)) return 0;
  block_of(d, &cl, pa, pb, &r0, &r1, &c0, &c1);
  for (k = 0; k < cl.n; k++) if (in_block(&cl.v[k].c, r0, r1, c0, c1)) { n++; if (tl < 0) tl = k; }
  if (n < 2 || tl < 0 || cl.v[tl].c.row != r0 || cl.v[tl].c.col != c0) { free(cl.v); return 0; }
  top = cl.v[tl].c;
  top.span = (u8)(c1 - c0 + 1); top.vmerge = r1 > r0 ? VM_START : VM_NONE;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    if (k == tl) {
      int j;
      at = l.n;
      for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &top));
      /* the other cells' text, in reading order (their empty paragraphs left out) */
      for (j = 0; j < cl.n; j++) {
        if (j == tl || !in_block(&cl.v[j].c, r0, r1, c0, c1)) continue;
        for (i = cl.v[j].p0; i < cl.v[j].p1; i++) if (d->p[i]->len) pl_add(&l, copy_cell(d, i, &top));
      }
      continue;
    }
    if (in_block(&ci->c, r0, r1, c0, c1)) {
      /* the first cell of a lower row in the block stands for the merged cell there */
      if (ci->c.row > r0 && ci->c.col == c0) {
        Cell c = top;
        c.row = ci->c.row; c.vmerge = VM_CONT; c.rowh = ci->c.rowh; c.header = ci->c.header; c.cantsplit = ci->c.cantsplit;
        pl_add(&l, empty_cell(d, ci->p0, &c));
      }
      continue;
    }
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &ci->c));
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, &pa, at);
  }
}

/* the caret's cell split across into cols cells side by side over its width. If cols is more than
   the grid columns it spans, the grid gains columns (a new Table) and the cells above and below that
   cover the place grow to match. Cells merged down into it are split the same way. The text stays in
   the first part; the others are empty */
static int split_cols(Doc *d, Pos *caret, int cols)
{
  CList cl;
  Table *t, nt;
  int me, k, extra, id, j;
  i32 w = 0;
  u32 i, at = 0;
  PList l;
  Cell mc;
  if (!(t = doc_table(d, &d->p[caret->para]->pf)) || !cells_of(d, caret->para, &cl)) return 0;
  for (me = 0; me < cl.n && !(caret->para >= cl.v[me].p0 && caret->para < cl.v[me].p1); me++) ;
  mc = cl.v[me].c;
  extra = cols > mc.span ? cols - mc.span : 0;
  if (t->ncols + extra > MAX_COLS) { free(cl.v); return 0; }
  nt = *t;
  for (k = mc.col; k < mc.col + mc.span; k++) w += t->grid[k];
  if (extra) {
    /* new grid columns: the cell's columns make way for equal ones */
    for (k = t->ncols - 1; k >= mc.col + mc.span; k--) nt.grid[k + extra] = t->grid[k];
    for (k = 0; k < cols; k++) nt.grid[mc.col + k] = w / cols;
    nt.ncols = (u8)(t->ncols + extra);
  }
  if (!(id = doc_add_table(d, &nt))) { free(cl.v); return 0; }
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    c.table = (u16)id;
    /* other cells: a cell spanning the split columns grows, one right of them moves along */
    if (k != me && !(c.row != mc.row && c.vmerge && c.col == mc.col && c.span == mc.span && mc.vmerge)) {
      if (c.col >= mc.col + mc.span) c.col = (u8)(c.col + extra);
      else if (c.col + c.span > mc.col) c.span = (u8)(c.span + extra);
      for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
      continue;
    }
    /* ours (and, when ours is merged down, the cells merged into it): cols cells over its columns */
    {
      int base = mc.span + extra;               /* the grid columns it now covers */
      int from = mc.col;
      for (j = 0; j < cols; j++) {
        Cell n = c;
        int to = mc.col + (base * (j + 1)) / cols;
        n.col = (u8)from; n.span = (u8)(to - from > 0 ? to - from : 1);
        if (j == 0) {
          if (k == me) at = l.n;
          for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &n));
        } else pl_add(&l, empty_cell(d, ci->p1 - 1, &n));
        from = to;
      }
    }
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, caret, at);
  }
}
/* the caret's cell split down into rows cells: rows - 1 new rows go in below its row, and the row's
   other cells are merged down over them, so only this cell looks split */
static int split_rows(Doc *d, Pos *caret, int rows)
{
  CList cl;
  int me, k, r, j, mrow, add;
  u32 i, at = 0;
  PList l;
  Cell mc;
  if (!cells_of(d, caret->para, &cl)) return 0;
  for (me = 0; me < cl.n && !(caret->para >= cl.v[me].p0 && caret->para < cl.v[me].p1); me++) ;
  mc = cl.v[me].c; mrow = mc.row;
  add = rows - 1;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    if (c.row > mrow) c.row = (u16)(c.row + add);
    else if (c.row == mrow && k != me && !c.vmerge) c.vmerge = VM_START;   /* the row's other cells reach down over the new rows */
    if (k == me) { at = l.n; c.vmerge = VM_NONE; }
    if (k == me && mc.vmerge == VM_START) c.vmerge = VM_NONE;
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
    /* after the row's last cell: the new rows */
    if (ci->c.row == mrow && (k + 1 == cl.n || cl.v[k + 1].c.row != mrow)) {
      for (r = 1; r <= add; r++)
        for (j = 0; j < cl.n; j++) {
          Cell n;
          if (cl.v[j].c.row != mrow) continue;
          n = cl.v[j].c;
          n.row = (u16)(mrow + r);
          n.vmerge = j == me ? VM_NONE : VM_CONT;
          pl_add(&l, empty_cell(d, cl.v[j].p1 - 1, &n));
        }
    }
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, caret, at);
  }
}
/* a vertically merged cell: the cells merged into it become cells of their own again */
static int unmerge_down(Doc *d, Pos *caret)
{
  CList cl;
  int me, k, r;
  u32 i, at = 0;
  PList l;
  Cell mc;
  if (!cells_of(d, caret->para, &cl)) return 0;
  for (me = 0; me < cl.n && !(caret->para >= cl.v[me].p0 && caret->para < cl.v[me].p1); me++) ;
  mc = cl.v[me].c;
  /* back up to where the merge starts */
  r = mc.row;
  while (mc.vmerge == VM_CONT && r > 0) { int u = cell_at(&cl, r - 1, mc.col); if (u < 0 || !cl.v[u].c.vmerge) break; r--; me = u; mc = cl.v[u].c; }
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    if (c.col == mc.col && c.vmerge && c.row >= mc.row) {
      int chainok = 1, rr;
      for (rr = mc.row + 1; rr <= c.row; rr++) { int u = cell_at(&cl, rr, mc.col); if (u < 0 || cl.v[u].c.vmerge != VM_CONT) chainok = 0; }
      if (chainok) c.vmerge = VM_NONE;
    }
    if (k == me) at = l.n;
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, caret, at);
  }
}
/* tbl_split_cells's work. A cell merged down comes apart first (each row's part a cell again) and
   each part is then split across; any other cell is split across, then down. 0 if there's nothing
   to do (1 x 1 of a plain cell). One undo step */
static int tbl_split_cells_(Doc *d, Pos *caret, int cols, int rows)
{
  Cell c;
  int ok = 1;
  Pos was = *caret;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf) || cols < 1 || rows < 1 || cols > MAX_COLS || rows > 64) return 0;
  c = CELL(caret->para);
  if (cols == 1 && rows == 1 && c.span == 1 && !c.vmerge) return 0;
  doc_begin(d, was);
  if (c.vmerge) {
    /* merged down: each row's part becomes a cell again, and each of those is split across */
    CList cl;
    int r0 = c.row, r1 = c.row, k, r;
    if ((ok = cells_of(d, caret->para, &cl))) {
      while (c.vmerge == VM_CONT && r0 > 0) {                     /* up to where the merge starts */
        k = cell_at(&cl, r0 - 1, c.col);
        if (k < 0 || !cl.v[k].c.vmerge) break;
        r0--;
        if (cl.v[k].c.vmerge == VM_START) break;
      }
      while (1) { k = cell_at(&cl, r1 + 1, c.col); if (k < 0 || cl.v[k].c.vmerge != VM_CONT) break; r1++; }
      free(cl.v);
      ok = unmerge_down(d, caret);
      for (r = r0; ok && r <= r1 && (cols > 1 || c.span > 1); r++) {
        u32 i, a, b;
        use_table(d, caret->para);
        a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
        for (i = a; i < b; i++) if (CELL(i).table && CELL(i).row == r && CELL(i).col == c.col) break;
        if (i < b && CELL(i).span > 1) { Pos q; q.para = i; q.off = 0; ok = split_cols(d, &q, cols); if (r == c.row) *caret = q; }
      }
    }
  } else {
    if (cols > 1 || c.span > 1) ok = split_cols(d, caret, cols);
    use_table(d, caret->para);
    if (ok && rows > 1) ok = split_rows(d, caret, rows);
  }
  doc_end(d, *caret);
  return ok;
}
/* a merged cell back into single cells: its span split into one cell per grid column, and any
   vertical merge undone, through tbl_split_cells. 0 for a cell that isn't merged */
static int tbl_split_(Doc *d, Pos *caret)
{
  Cell c;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf)) return 0;
  c = CELL(caret->para);
  if (c.span < 2 && !c.vmerge) return 0;
  return tbl_split_cells(d, caret, c.span, 1);
}

/* the table split above the caret's row: those rows a new table, after an empty paragraph */
static int tbl_split_table_(Doc *d, Pos *caret)
{
  u32 a, b, i, at = 0;
  Table *t;
  int id;
  u16 r;
  PList l;
  ParaFmt pf;
  if (caret->para >= d->np || !(t = doc_table(d, &d->p[caret->para]->pf))) return 0;
  r = CELL(caret->para).row;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  if (r == 0) {                                     /* at the first row: a paragraph above the table */
    memset(&l, 0, sizeof l);
    pf = d->styles[0].pf; pf.style = 0;
    if (is_nested(&d->p[a]->pf)) pf.cell = d->p[a]->pf.outer;
    pl_add(&l, para_new(&pf, &d->styles[0].cf));
    for (i = a; i < b; i++) pl_add(&l, copy_cell(d, i, &CELL(i)));
    return commit(d, a, b, &l, caret, 0);
  }
  if (!(id = doc_add_table(d, t))) return 0;
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    if (c.row >= r) {
      if (c.row == r && c.vmerge == VM_CONT) c.vmerge = VM_START;
      if (c.row == r && (i == a || CELL(i - 1).row < r)) {
        pf = d->styles[0].pf; pf.style = 0;
        if (is_nested(&d->p[a]->pf)) pf.cell = d->p[a]->pf.outer;
        at = l.n;
        pl_add(&l, para_new(&pf, &d->styles[0].cf));
      }
      c.row = (u16)(c.row - r); c.table = (u16)id; c.header = 0;
    }
    pl_add(&l, copy_cell(d, i, &c));
  }
  return commit(d, a, b, &l, caret, at);
}

/* the cells touched by a..b, as a block: rows r0..r1, grid columns c0..c1. Fills cl with all the
   table's cells (the caller frees cl->v); 0 if a..b doesn't start in a table, or out of memory */
static int sel_block(Doc *d, Pos pa, Pos pb, CList *cl, int *r0, int *r1, int *c0, int *c1)
{
  if (pos_cmp(pa, pb) > 0) { Pos t = pa; pa = pb; pb = t; }
  if (pa.para >= d->np || !in_table(&d->p[pa.para]->pf)) return 0;
  if (!cells_of(d, pa.para, cl)) return 0;
  if (pb.para >= cl->b) pb.para = cl->b - 1;
  block_of(d, cl, pa, pb, r0, r1, c0, c1);
  return 1;
}
/* tbl_borders's work: the edges the TB_ choice picks, set (or cleared, for TB_NONE or PB_NONE) on
   each cell of the block. An edge is outside when it lies on the block's edge; a cell merged down
   has its bottom at the merge's last row. Positions aren't moved */
static int tbl_borders_(Doc *d, Pos pa, Pos pb, int which, u8 style, u8 width, u32 color)
{
  CList cl;
  int r0, r1, c0, c1, k, e;
  u32 i;
  PList l;
  Pos keep = pa;
  if (!sel_block(d, pa, pb, &cl, &r0, &r1, &c0, &c1)) return 0;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    if (in_block(&c, r0, r1, c0, c1)) {
      /* which of the cell's edges are on the block's outside, which inside it */
      int out[4], set[4], on[4];
      int bottom = c.row;
      { int u = k; while (1) { int v = cell_at(&cl, bottom + 1, c.col); if (v < 0 || cl.v[v].c.vmerge != VM_CONT || !cl.v[u].c.vmerge) break; bottom++; u = v; } }
      out[E_TOP] = c.row == r0; out[E_BOTTOM] = bottom >= r1 || c.row == r1; out[E_LEFT] = c.col == c0; out[E_RIGHT] = c.col + c.span - 1 == c1;
      for (e = 0; e < 4; e++) {
        int horiz = e == E_TOP || e == E_BOTTOM;
        set[e] = 1;
        switch (which) {
        case TB_ALL: on[e] = 1; break;
        case TB_NONE: on[e] = 0; break;
        case TB_OUTSIDE: set[e] = out[e]; on[e] = 1; break;
        case TB_INSIDE: set[e] = !out[e]; on[e] = 1; break;
        case TB_INSIDE_H: set[e] = !out[e] && horiz; on[e] = 1; break;
        case TB_INSIDE_V: set[e] = !out[e] && !horiz; on[e] = 1; break;
        case TB_TOP_ONLY: set[e] = out[e] && e == E_TOP; on[e] = 1; break;
        case TB_BOTTOM_ONLY: set[e] = out[e] && e == E_BOTTOM; on[e] = 1; break;
        case TB_LEFT_ONLY: set[e] = out[e] && e == E_LEFT; on[e] = 1; break;
        case TB_RIGHT_ONLY: set[e] = out[e] && e == E_RIGHT; on[e] = 1; break;
        default: set[e] = 0; on[e] = 0;
        }
        if (!set[e]) continue;
        if (on[e] && style != PB_NONE) { c.borders |= (u8)(1 << e); c.bstyle[e] = style; c.bwidth[e] = width; c.bcolor[e] = color; }
        else c.borders &= (u8)~(1 << e);
      }
    }
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit_keep(d, a, b, &l, &keep);
  }
}

/* append p's text from byte x to y, in its formats */
static void append_slice(Para *dst, const Para *p, u32 x, u32 y)
{
  u32 r, start = 0;
  for (r = 0; r < p->nr && x < y; r++) {
    u32 e = p->r[r].end, s = start > x ? start : x, t = e < y ? e : y;
    if (t > s) para_append(dst, p->t + s, t - s, &p->r[r].f);
    start = e;
  }
}
/* see table.h. The number of columns is the most separators any paragraph has, plus one, at most
   MAX_COLS (past that, the rest of a paragraph goes into its last cell). Each cell keeps its
   paragraph's formatting, less page and column breaks and drop caps. 0 if any of the paragraphs is in
   a table or ends a section. One undo step */
int tbl_from_text(Doc *d, u32 pa, u32 pb, int sep, i32 width, Pos *caret)
{
  Table t;
  PList l;
  u32 i, x;
  int ncols = 1, id, k;
  Cell c;
  if (pa > pb || pb >= d->np) return 0;
  for (i = pa; i <= pb; i++) if (in_table(&d->p[i]->pf) || d->p[i]->pf.sect) return 0;
  if (sep) for (i = pa; i <= pb; i++) {
    int n = 1;
    for (x = 0; x < d->p[i]->len; x++) if (d->p[i]->t[x] == sep) n++;
    if (n > ncols) ncols = n;
  }
  if (ncols > MAX_COLS) ncols = MAX_COLS;
  memset(&t, 0, sizeof t);
  t.ncols = (u8)ncols; t.marl = t.marr = 108; t.look = LK_DEFAULT;
  t.tstyle = (u8)tstyle_use_builtin(d, 0);
  for (k = 0; k < ncols; k++) t.grid[k] = width / ncols;
  if (!(id = doc_add_table(d, &t))) return 0;
  memset(&l, 0, sizeof l);
  memset(&c, 0, sizeof c);
  c.table = (u16)id; c.span = 1; c.borders = BD_ALL; c.shade = COLOR_AUTO;
  for (k = 0; k < 4; k++) c.bcolor[k] = COLOR_AUTO;
  for (i = pa; i <= pb; i++) {
    Para *p = d->p[i];
    u32 from = 0;
    int col = 0;
    c.row = (u16)(i - pa);
    for (x = 0; x <= p->len && col < ncols; x++) {
      if (x < p->len && (!sep || p->t[x] != sep || col == ncols - 1)) continue;
      {
        ParaFmt pf = p->pf;
        Para *q;
        c.col = (u8)col;
        pf.cell = c; pf.pagebreak = 0; pf.colbreak = 0; pf.dropcap = 0;
        if ((q = para_new(&pf, para_fmt_at(p, from)))) append_slice(q, p, from, x);
        pl_add(&l, q);
      }
      col++; from = x + 1;
    }
    for (; col < ncols; col++) { c.col = (u8)col; pl_add(&l, empty_cell(d, i, &c)); }
  }
  if (pb + 1 >= d->np) { ParaFmt pf = d->styles[0].pf; pf.style = 0; pl_add(&l, para_new(&pf, &d->styles[0].cf)); }
  return commit(d, pa, pb + 1, &l, caret, 0);
}
/* tbl_to_text's work: a paragraph per row, formatted like the row's first paragraph; the cells'
   text joined with sep, paragraphs of one cell with a space */
static int tbl_to_text_(Doc *d, Pos *caret, int sep)
{
  u32 a, b, i;
  PList l;
  Para *q = NULL;
  int row = -1;
  char s[2];
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf)) return 0;
  a = tbl_first(d, caret->para); b = tbl_end(d, caret->para);
  s[0] = (char)(sep ? sep : '\t'); s[1] = 0;
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Para *p = d->p[i];
    if (CELL(i).row != row) {
      ParaFmt pf = p->pf;
      if (q) pl_add(&l, q);
      /* a nested table's rows become paragraphs of the cell it was in */
      if (pf.outer.table && pf.outer.table != cur) pf.cell = pf.outer; else memset(&pf.cell, 0, sizeof pf.cell);
      memset(&pf.outer, 0, sizeof pf.outer);
      q = para_new(&pf, &p->r[0].f);
      row = CELL(i).row;
    } else if (q) para_append(q, same_in(d, i - 1, i, cur) ? " " : s, 1, para_fmt_before(q, q->len));
    if (q) append_slice(q, p, 0, p->len);
  }
  if (q) pl_add(&l, q);
  return commit(d, a, b, &l, caret, 0);
}

/* sorting rows by a column's text */
typedef struct { int row; const char *key; long long num; int isnum; } SortKey;
static int sort_desc, sort_numeric;
/* a number at the start of s (spaces, a sign, digits with thousands commas, a decimal point): in
   ten-thousandths */
static int parse_num(const char *s, long long *out)
{
  long long v = 0, f = 0, div = 1;
  int neg = 0, any = 0;
  while (*s == ' ' || *s == '\t' || *s == '$' || (u8)*s == 0xC2 || (u8)*s == 0xA3) s++;     /* currency signs */
  if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
  while ((*s >= '0' && *s <= '9') || (*s == ',' && s[1] >= '0' && s[1] <= '9')) { if (*s != ',') { v = v * 10 + (*s - '0'); any = 1; } s++; }
  if (*s == '.') for (s++; *s >= '0' && *s <= '9'; s++) { if (div < 10000) { f = f * 10 + (*s - '0'); div *= 10; } any = 1; }
  while (div < 10000) { f *= 10; div *= 10; }
  *out = (v * 10000 + f) * (neg ? -1 : 1);
  return any;
}
/* qsort comparison for tbl_sort (qsort takes no context, hence the sort_ statics). Numbers come
   before text either way; text compares ignoring case (ASCII letters only). Ties go by the original
   row, since qsort isn't stable */
static int key_cmp(const void *x, const void *y)
{
  const SortKey *a = (const SortKey *)x, *b = (const SortKey *)y;
  int r;
  if (sort_numeric && (a->isnum || b->isnum)) {
    if (a->isnum != b->isnum) return a->isnum ? -1 : 1;              /* text after numbers, either way */
    r = a->num < b->num ? -1 : a->num > b->num;
  } else {
    const char *p = a->key, *q = b->key;
    for (r = 0; *p || *q; p++, q++) {
      int u = (u8)*p, v = (u8)*q;
      if (u >= 'A' && u <= 'Z') u += 32;
      if (v >= 'A' && v <= 'Z') v += 32;
      if (u != v) { r = u < v ? -1 : 1; break; }
    }
  }
  if (sort_desc) r = -r;
  return r ? r : a->row - b->row;                                     /* stable */
}
/* tbl_sort's work. With header, the first row and any heading rows after it stay put. The key is the
   text of the first paragraph of the cell covering grid column col; at least two rows are needed. A
   table style with banded rows is applied again afterwards so the bands stay in order, in the same
   undo step as the sort */
static int tbl_sort_(Doc *d, Pos *caret, int col, int descending, int numeric, int header)
{
  CList cl;
  SortKey *keys;
  int r, k, first, *order;
  PList l;
  if (caret->para >= d->np || !in_table(&d->p[caret->para]->pf) || !cells_of(d, caret->para, &cl)) return 0;
  for (k = 0; k < cl.n; k++) if (cl.v[k].c.vmerge) { free(cl.v); return 0; }    /* rows held together by merged cells */
  first = header ? 1 : 0;
  while (header && first < cl.nrows) { int hdr = 0; for (k = 0; k < cl.n; k++) if (cl.v[k].c.row == first && cl.v[k].c.header) hdr = 1; if (!hdr) break; first++; }
  if (cl.nrows - first < 2) { free(cl.v); return 0; }
  keys = (SortKey *)calloc((size_t)cl.nrows, sizeof(SortKey));
  order = (int *)malloc((size_t)cl.nrows * sizeof(int));
  if (!keys || !order) { free(keys); free(order); free(cl.v); return 0; }
  for (r = 0; r < cl.nrows; r++) {
    int ci = cell_at(&cl, r, col);
    keys[r].row = r;
    keys[r].key = ci >= 0 ? d->p[cl.v[ci].p0]->t : "";
    keys[r].isnum = parse_num(keys[r].key, &keys[r].num);
  }
  sort_desc = descending; sort_numeric = numeric;
  qsort(keys + first, (size_t)(cl.nrows - first), sizeof(SortKey), key_cmp);
  for (r = 0; r < cl.nrows; r++) order[r] = keys[r].row;
  memset(&l, 0, sizeof l);
  for (r = 0; r < cl.nrows; r++)
    for (k = 0; k < cl.n; k++)
      if (cl.v[k].c.row == order[r]) {
        Cell c = cl.v[k].c;
        u32 i;
        c.row = (u16)r;
        /* lines and fills that belong to a place in the table (a style's bands) stay with the row's place */
        for (i = cl.v[k].p0; i < cl.v[k].p1; i++) pl_add(&l, copy_cell(d, i, &c));
      }
  free(keys); free(order);
  {
    u32 a = cl.a, b = cl.b;
    Table *t = doc_table(d, &d->p[a]->pf);
    int ts = t ? t->tstyle : 0;
    u16 look = t ? t->look : 0;
    int bands = 0, ok;
    free(cl.v);
    if (ts > 0 && ts <= d->ntstyles) {
      const TableStyle *st = &d->tstyles[ts - 1];
      bands = st->c[TC_BAND1H].used || st->c[TC_BAND2H].used || st->c[TC_LASTROW].used;
    }
    doc_begin(d, *caret);
    ok = commit(d, a, b, &l, caret, 0);
    if (ok && bands) tbl_apply_style(d, caret, ts, look);      /* banded rows' fills where they belong again */
    doc_end(d, *caret);
    return ok;
  }
}

/* ------------------------------------------------------------------ */
/* formulas: Word's =SUM(ABOVE) and its relatives, worked out now (numbers in ten-thousandths) */
typedef struct { Doc *d; CList cl; int row, col; const char *s; int err; } Fx;
/* skip spaces in the formula */
static void fx_space(Fx *f) { while (*f->s == ' ') f->s++; }
/* the number in cell k (0 if none: then *has is 0) */
static long long cell_num(Fx *f, int k, int *has)
{
  u32 i;
  long long v = 0;
  *has = 0;
  if (k < 0) return 0;
  for (i = f->cl.v[k].p0; i < f->cl.v[k].p1 && !*has; i++) *has = parse_num(f->d->p[i]->t, &v);
  return v;
}
typedef struct { long long sum, prod, max, min; int n; } Agg;
/* v (ten-thousandths) into the running sum, product, maximum, minimum and count; the product is
   scaled back after each multiply to stay in ten-thousandths */
static void agg_add(Agg *a, long long v)
{
  if (!a->n) { a->max = a->min = v; a->prod = 10000; }
  a->sum += v; a->prod = a->prod * v / 10000; a->n++;
  if (v > a->max) a->max = v;
  if (v < a->min) a->min = v;
}
/* the number in the cell at row r, grid column c, if it has one. Only a cell that starts there
   counts, so a cell spanning several columns is added once; cells without a number are left out (of
   COUNT and AVERAGE too) */
static void agg_cell(Fx *f, Agg *a, int r, int c)
{
  int has, k = cell_at(&f->cl, r, c);
  long long v;
  if (k < 0 || f->cl.v[k].c.row != r || f->cl.v[k].c.col != c) return;   /* each cell once */
  v = cell_num(f, k, &has);
  if (has) agg_add(a, v);
}
static long long fx_expr(Fx *f);
/* a cell name (A1: one or two letters, then the row number): its row and column, both counted from
   0, and the parse moves past it. 0 if there isn't one here (nothing used up) */
static int fx_ref(Fx *f, int *r, int *c)
{
  const char *p = f->s;
  int col = 0, row = 0, n = 0;
  while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) { col = col * 26 + ((*p | 32) - 'a' + 1); p++; n++; }
  if (!n || n > 2 || *p < '1' || *p > '9') return 0;
  while (*p >= '0' && *p <= '9') { row = row * 10 + (*p - '0'); p++; }
  *r = row - 1; *c = col - 1;
  f->s = p;
  return 1;
}
/* the keyword w (given in upper case) here as a whole word, in any case: move past it and return 1 */
static int fx_word(Fx *f, const char *w)
{
  size_t n = strlen(w), i;
  for (i = 0; i < n; i++) if ((f->s[i] & ~32) != w[i]) return 0;
  if ((f->s[n] >= 'A' && f->s[n] <= 'Z') || (f->s[n] >= 'a' && f->s[n] <= 'z') || (f->s[n] >= '0' && f->s[n] <= '9')) return 0;
  f->s += n;
  return 1;
}
/* a function's arguments into a */
static void fx_args(Fx *f, Agg *a)
{
  int r, c, r2, c2, k;
  do {
    fx_space(f);
    if (fx_word(f, "ABOVE")) { for (r = 0; r < f->row; r++) agg_cell(f, a, r, f->col); }
    else if (fx_word(f, "BELOW")) { for (r = f->row + 1; r < f->cl.nrows; r++) agg_cell(f, a, r, f->col); }
    else if (fx_word(f, "LEFT")) { for (c = 0; c < f->col; c++) agg_cell(f, a, f->row, c); }
    else if (fx_word(f, "RIGHT")) { for (c = f->col + 1; c < MAX_COLS; c++) agg_cell(f, a, f->row, c); }
    else {
      const char *save = f->s;
      if (fx_ref(f, &r, &c)) {
        fx_space(f);
        if (*f->s == ':') {
          f->s++; fx_space(f);
          if (!fx_ref(f, &r2, &c2)) { f->err = 1; return; }
          if (r2 < r) { k = r; r = r2; r2 = k; }
          if (c2 < c) { k = c; c = c2; c2 = k; }
          for (k = r; k <= r2; k++) { int j; for (j = c; j <= c2; j++) agg_cell(f, a, k, j); }
        } else { f->s = save; agg_add(a, fx_expr(f)); }
      } else agg_add(a, fx_expr(f));
    }
    fx_space(f);
  } while (!f->err && *f->s == ',' && f->s++);
}
/* a factor: a signed factor, a bracketed expression, a number, a function and its arguments (SUM
   and the rest, any case) or a cell name. Sets f->err on a syntax error. AVERAGE, MAX, MIN and PRODUCT
   of nothing give 0; INT cuts towards zero */
static long long fx_factor(Fx *f)
{
  long long v;
  int r, c;
  fx_space(f);
  if (*f->s == '-') { f->s++; return -fx_factor(f); }
  if (*f->s == '+') { f->s++; return fx_factor(f); }
  if (*f->s == '(') {
    f->s++; v = fx_expr(f); fx_space(f);
    if (*f->s == ')') f->s++; else f->err = 1;
    return v;
  }
  if ((*f->s >= '0' && *f->s <= '9') || *f->s == '.') {
    parse_num(f->s, &v);
    while ((*f->s >= '0' && *f->s <= '9') || *f->s == '.' || *f->s == ',') f->s++;
    return v;
  }
  {
    static const char *const fn[] = { "SUM", "AVERAGE", "COUNT", "MAX", "MIN", "PRODUCT", "ABS", "INT" };
    int k;
    for (k = 0; k < 8; k++) {
      const char *save = f->s;
      if (fx_word(f, fn[k])) {
        Agg a;
        fx_space(f);
        if (*f->s != '(') { f->s = save; break; }
        f->s++;
        memset(&a, 0, sizeof a);
        fx_args(f, &a);
        fx_space(f);
        if (*f->s == ')') f->s++; else f->err = 1;
        switch (k) {
        case 0: return a.sum;
        case 1: return a.n ? a.sum / a.n : 0;
        case 2: return (long long)a.n * 10000;
        case 3: return a.n ? a.max : 0;
        case 4: return a.n ? a.min : 0;
        case 5: return a.n ? a.prod : 0;
        case 6: return a.sum < 0 ? -a.sum : a.sum;
        default: return a.sum / 10000 * 10000;
        }
      }
    }
  }
  if (fx_ref(f, &r, &c)) { int has; return cell_num(f, cell_at(&f->cl, r, c), &has); }
  f->err = 1;
  return 0;
}
/* factors joined by * and /, a trailing % dividing by 100; the values are fixed point, so products
   and quotients are scaled back. Dividing by zero sets f->err to 2 */
static long long fx_term(Fx *f)
{
  long long v = fx_factor(f);
  for (;;) {
    fx_space(f);
    if (*f->s == '*') { f->s++; v = v * fx_factor(f) / 10000; }
    else if (*f->s == '/') { long long w; f->s++; w = fx_factor(f); if (!w) { f->err = 2; return 0; } v = v * 10000 / w; }
    else if (*f->s == '%') { f->s++; v /= 100; }
    else return v;
  }
}
/* terms joined by + and -: the top of the recursive descent (expression, term, factor) */
static long long fx_expr(Fx *f)
{
  long long v = fx_term(f);
  for (;;) {
    fx_space(f);
    if (*f->s == '+') { f->s++; v += fx_term(f); }
    else if (*f->s == '-') { f->s++; v -= fx_term(f); }
    else return v;
  }
}
/* tbl_formula's work: expr (its '=' optional) worked out for the caret's cell, the result written
   with up to four decimals (trailing zeros dropped) and no thousands separators. On failure 0 with
   "!Syntax Error" or "!Zero Divide" in out, or 0 with out untouched if cap is too small */
static int tbl_formula_(Doc *d, Pos caret, const char *expr, char *out, size_t cap)
{
  Fx f;
  long long v, ip, fp;
  int neg;
  char frac[8];
  if (caret.para >= d->np || !in_table(&d->p[caret.para]->pf)) return 0;
  memset(&f, 0, sizeof f);
  f.d = d;
  if (!cells_of(d, caret.para, &f.cl)) return 0;
  f.row = CELL(caret.para).row; f.col = CELL(caret.para).col;
  f.s = expr;
  while (*f.s == ' ') f.s++;
  if (*f.s == '=') f.s++;
  v = fx_expr(&f);
  fx_space(&f);
  if (*f.s) f.err = 1;
  free(f.cl.v);
  if (f.err) { snprintf(out, cap, f.err == 2 ? "!Zero Divide" : "!Syntax Error"); return 0; }
  neg = v < 0; if (neg) v = -v;
  ip = v / 10000; fp = v % 10000;
  if (fp) {
    int k;
    sprintf(frac, ".%04d", (int)fp);
    for (k = (int)strlen(frac) - 1; k > 0 && frac[k] == '0'; k--) frac[k] = 0;
  } else frac[0] = 0;
  {
    char digits[24];
    int n = 0;
    do { digits[n++] = (char)('0' + (int)(ip % 10)); ip /= 10; } while (ip && n < 22);
    if (cap < (size_t)n + 10) return 0;
    if (neg) *out++ = '-';
    while (n) *out++ = digits[--n];
    strcpy(out, frac);
  }
  return 1;
}

/* the operations, each on the innermost table at its position */
int tbl_insert(Doc *d, Pos *at, int rows, int cols, i32 width) { use_table(d, at->para); return tbl_insert_(d, at, rows, cols, width); }
int tbl_row_insert(Doc *d, Pos *caret, int below) { use_table(d, caret->para); return tbl_row_insert_(d, caret, below); }
int tbl_delete(Doc *d, Pos *caret) { use_table(d, caret->para); return tbl_delete_(d, caret); }
int tbl_row_delete(Doc *d, Pos *caret) { use_table(d, caret->para); return tbl_row_delete_(d, caret); }
int tbl_col_insert(Doc *d, Pos *caret, int right) { use_table(d, caret->para); return tbl_col_insert_(d, caret, right); }
int tbl_col_delete(Doc *d, Pos *caret) { use_table(d, caret->para); return tbl_col_delete_(d, caret); }
int tbl_next_cell(Doc *d, Pos *caret, int dir) { use_table(d, caret->para); return tbl_next_cell_(d, caret, dir); }
int tbl_cell_format(Doc *d, Pos pa, Pos pb, int what, u32 value) { use_table(d, pa.para); return tbl_cell_format_(d, pa, pb, what, value); }
int tbl_apply_style(Doc *d, Pos *caret, int ts, u16 look) { use_table(d, caret->para); return tbl_apply_style_(d, caret, ts, look); }
int tbl_set_table(Doc *d, Pos *caret, const Table *nt) { use_table(d, caret->para); return tbl_set_table_(d, caret, nt); }
int tbl_merge(Doc *d, Pos pa, Pos pb) { use_table(d, pa.para); return tbl_merge_(d, pa, pb); }
int tbl_split_cells(Doc *d, Pos *caret, int cols, int rows) { use_table(d, caret->para); return tbl_split_cells_(d, caret, cols, rows); }
int tbl_split(Doc *d, Pos *caret) { use_table(d, caret->para); return tbl_split_(d, caret); }
int tbl_split_table(Doc *d, Pos *caret) { use_table(d, caret->para); return tbl_split_table_(d, caret); }
int tbl_borders(Doc *d, Pos pa, Pos pb, int which, u8 style, u8 width, u32 color) { use_table(d, pa.para); return tbl_borders_(d, pa, pb, which, style, width, color); }
int tbl_to_text(Doc *d, Pos *caret, int sep) { use_table(d, caret->para); return tbl_to_text_(d, caret, sep); }
int tbl_sort(Doc *d, Pos *caret, int col, int descending, int numeric, int header) { use_table(d, caret->para); return tbl_sort_(d, caret, col, descending, numeric, header); }
int tbl_formula(Doc *d, Pos caret, const char *expr, char *out, size_t cap) { use_table(d, caret.para); return tbl_formula_(d, caret, expr, out, cap); }

/* blocks of cells (a selection over several cells): tbl_block's work. 0 unless b's paragraph is in
   a different cell of a's table */
static int tbl_block_(Doc *d, Pos a, Pos b, u16 *table, int *r0, int *r1, int *c0, int *c1)
{
  CList cl;
  if (a.para >= d->np || b.para >= d->np || !in_table(&d->p[a.para]->pf)) return 0;
  if (!cell_in(&d->p[b.para]->pf, cur) || same_in(d, a.para, b.para, cur)) return 0;
  if (!sel_block(d, a, b, &cl, r0, r1, c0, c1)) return 0;
  free(cl.v);
  *table = cur;
  return 1;
}
/* tbl_block_cells's work: first and end for each cell wholly in the block, in paragraph order */
static int tbl_block_cells_(Doc *d, Pos a, Pos b, u32 *first, u32 *end, int max)
{
  CList cl;
  int r0, r1, c0, c1, k, n = 0;
  u16 t;
  if (!tbl_block_(d, a, b, &t, &r0, &r1, &c0, &c1) || !sel_block(d, a, b, &cl, &r0, &r1, &c0, &c1)) return 0;
  for (k = 0; k < cl.n && n < max; k++)
    if (in_block(&cl.v[k].c, r0, r1, c0, c1)) { first[n] = cl.v[k].p0; end[n] = cl.v[k].p1; n++; }
  free(cl.v);
  return n;
}
/* the block entry points, likewise on the innermost table at a */
int tbl_block(Doc *d, Pos a, Pos b, u16 *table, int *r0, int *r1, int *c0, int *c1) { use_table(d, a.para); return tbl_block_(d, a, b, table, r0, r1, c0, c1); }
int tbl_block_cells(Doc *d, Pos a, Pos b, u32 *first, u32 *end, int max) { use_table(d, a.para); return tbl_block_cells_(d, a, b, first, end, max); }

/* ------------------------------------------------------------------ */
/* WP21b: cell options and drawing tables (see table.h). Each one is a rebuild of the table's
   paragraphs, so one doc_replace and one undo step, like the operations above */

/* merged-down chains made whole again after cells moved or went: a VM_CONT with no merged cell of
   the same columns above it starts a chain (or stands alone), a VM_START with nothing merged into it
   below is a plain cell */
static void fix_vmerge(PList *l)
{
  u32 i, j;
  for (i = 0; i < l->n; i++) {
    Cell *c = pcell(l->p[i]);
    int above = 0, below = 0;
    if (!c->vmerge) continue;
    for (j = 0; j < l->n; j++) {
      const Cell *o = pcell(l->p[j]);
      if (o->table != c->table || o->col != c->col || o->span != c->span) continue;
      if (o->row + 1 == c->row && o->vmerge) above = 1;
      if (o->row == c->row + 1 && o->vmerge == VM_CONT) below = 1;
    }
    if (c->vmerge == VM_CONT && !above) c->vmerge = (u8)(below ? VM_START : VM_NONE);
    else if (c->vmerge == VM_START && !below) c->vmerge = VM_NONE;
  }
}
/* the paragraph of table cur's cell at row, col (the one covering grid column col), or the table's
   end if there is none */
static u32 find_cell(Doc *d, u32 a, u32 b, int row, int col)
{
  u32 i;
  for (i = a; i < b; i++) { const Cell *c = &CELL(i); if (c->row == row && col >= c->col && col < c->col + c->span) return i; }
  return b;
}
u32 tbl_cell_para(Doc *d, Pos in, int row, int col)
{
  u32 a, b;
  if (in.para >= d->np || !in_table(&d->p[in.para]->pf)) return (u32)-1;
  use_table(d, in.para);
  a = tbl_first(d, in.para); b = tbl_end(d, in.para);
  a = find_cell(d, a, b, row, col);
  return a < b ? a : (u32)-1;
}

/* every cell of the block a..b (the caret's cell when a == b) changed by fn; the caret kept */
typedef void (*CellFn)(Cell *c, Para *p, const void *arg);
static int block_change(Doc *d, Pos pa, Pos pb, CellFn fn, const void *arg)
{
  CList cl;
  int r0, r1, c0, c1, k;
  u32 i;
  PList l;
  Pos keep = pa;
  if (!sel_block(d, pa, pb, &cl, &r0, &r1, &c0, &c1)) return 0;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    int in = in_block(&c, r0, r1, c0, c1);
    for (i = ci->p0; i < ci->p1; i++) {
      Para *p;
      Cell nc = c;
      if (in) fn(&nc, NULL, arg);
      p = copy_cell(d, i, &nc);
      if (p && in && !(cur && p->pf.outer.table == cur)) fn(pcell(p), p, arg);   /* (a nested table's paragraphs keep their own alignment) */
      pl_add(&l, p);
    }
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit_keep(d, a, b, &l, &keep);
  }
}
/* the 9-way alignment: arg points at two ints, the vertical alignment and the paragraphs' (-1: kept) */
static void fn_align(Cell *c, Para *p, const void *arg)
{
  const int *v = (const int *)arg;
  c->valign = (u8)v[0];
  if (p && v[1] >= 0) p->pf.align = (u8)v[1];
}
int tbl_cell_align(Doc *d, Pos a, Pos b, int valign, int align)
{
  int v[2];
  v[0] = valign < 0 ? 0 : valign > 2 ? 2 : valign; v[1] = align;
  use_table(d, pos_cmp(a, b) <= 0 ? a.para : b.para);
  return block_change(d, a, b, fn_align, v);
}
static void fn_dir(Cell *c, Para *p, const void *arg) { (void)p; c->tdir = (u8)*(const int *)arg; }
int tbl_cell_dir(Doc *d, Pos a, Pos b, int dir)
{
  if (dir < TD_HORZ || dir > TD_UP) return 0;
  use_table(d, pos_cmp(a, b) <= 0 ? a.para : b.para);
  return block_change(d, a, b, fn_dir, &dir);
}
typedef struct { u8 set; i16 mar[4]; } MarArg;
static void fn_mar(Cell *c, Para *p, const void *arg)
{
  const MarArg *m = (const MarArg *)arg;
  int k;
  (void)p;
  c->marset = m->set;
  for (k = 0; k < 4; k++) c->mar[k] = (i16)(m->set & (1 << k) ? m->mar[k] : 0);
}
int tbl_cell_mar(Doc *d, Pos a, Pos b, u8 set, const i16 *mar)
{
  MarArg m;
  int k;
  m.set = (u8)(set & 15);
  for (k = 0; k < 4; k++) m.mar[k] = mar ? (i16)(mar[k] < 0 ? 0 : mar[k] > 4000 ? 4000 : mar[k]) : 0;
  use_table(d, pos_cmp(a, b) <= 0 ? a.para : b.para);
  return block_change(d, a, b, fn_mar, &m);
}

/* Delete Cells: the block's rows, its grid columns, or its cells with the rest of their rows moving
   left (the rows end short, as in Word) or the rest of their columns moving up (empty cells coming in
   at the bottom) */
static int del_rows(Doc *d, Pos pa, Pos pb, Pos *caret)
{
  CList cl;
  int r0, r1, c0, c1, k, any = 0, n;
  u32 i, at = 0;
  PList l;
  if (!sel_block(d, pa, pb, &cl, &r0, &r1, &c0, &c1)) return 0;
  n = r1 - r0 + 1;
  for (k = 0; k < cl.n; k++) if (cl.v[k].c.row < r0 || cl.v[k].c.row > r1) any = 1;
  if (!any) { Pos c = pa; free(cl.v); *caret = c; return tbl_delete(d, caret); }
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    if (c.row >= r0 && c.row <= r1) continue;
    if (c.row > r1) c.row = (u16)(c.row - n);
    if (c.row == r0 && c.col <= c0 && c.col + c.span > c0) at = l.n;
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
  }
  if (!at && l.n && pcell(l.p[l.n - 1])->row < r0) {          /* the last rows went: the caret into the row above */
    for (i = l.n; i > 0; i--) if (pcell(l.p[i - 1])->col <= c0) { at = i - 1; break; }
    while (at > 0 && pcell(l.p[at - 1])->row == pcell(l.p[at])->row && pcell(l.p[at - 1])->col == pcell(l.p[at])->col) at--;
  }
  fix_vmerge(&l);
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, caret, at);
  }
}
static int del_cols(Doc *d, Pos pa, Pos pb, Pos *caret)
{
  CList cl;
  int r0, r1, c0, c1, k, ok = 1, n;
  if (!sel_block(d, pa, pb, &cl, &r0, &r1, &c0, &c1)) return 0;
  free(cl.v);
  doc_begin(d, *caret);
  for (n = c1 - c0 + 1; n > 0 && ok; n--) {
    /* a paragraph of a cell starting in grid column c0 (the table's number changes each time) */
    u32 a = tbl_first(d, pa.para), b = tbl_end(d, pa.para), i;
    Pos c;
    for (i = a; i < b && !(CELL(i).col == c0); i++) ;
    if (i >= b) break;
    c.para = i; c.off = 0;
    k = d->p[a]->pf.cell.table;
    ok = tbl_col_delete(d, &c);
    use_table(d, c.para < d->np ? c.para : 0);
    *caret = c;
    if (!ok || c.para >= d->np || !in_table(&d->p[c.para]->pf) || d->p[c.para]->pf.cell.table == k) break;   /* the table went */
    pa = c;
  }
  doc_end(d, *caret);
  return 1;
}
static int del_left(Doc *d, Pos pa, Pos pb, Pos *caret)
{
  CList cl;
  int r0, r1, c0, c1, k, w;
  u32 i, at = 0;
  PList l;
  if (!sel_block(d, pa, pb, &cl, &r0, &r1, &c0, &c1)) return 0;
  w = c1 - c0 + 1;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    if (c.row >= r0 && c.row <= r1) {
      if (in_block(&c, r0, r1, c0, c1)) continue;          /* the cell goes */
      if (c.col > c1) { c.col = (u8)(c.col - w); c.vmerge = VM_NONE; if (c.row == r0 && !at) at = l.n; }
    }
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
  }
  /* rows left with no cell at all keep one empty cell, so the table stays whole */
  for (k = r0; k <= r1; k++) {
    u32 j, last = (u32)-1;
    for (j = 0; j < l.n; j++) if (pcell(l.p[j])->row == k) break;
    if (j < l.n) continue;
    for (j = 0; j < l.n; j++) if (pcell(l.p[j])->row < k) last = j;
    {
      CInfo *src = NULL;
      int m;
      for (m = 0; m < cl.n && !src; m++) if (cl.v[m].c.row == k) src = &cl.v[m];
      if (src) {
        Cell c = src->c;
        Para *p;
        c.col = 0; c.span = 1; c.vmerge = VM_NONE;
        p = empty_cell(d, src->p0, &c);
        pl_add(&l, p);
        if (!l.oom && l.n > 1) {                            /* put it after the rows before it */
          u32 to = last == (u32)-1 ? 0 : last + 1, q;
          Para *mv = l.p[l.n - 1];
          for (q = l.n - 1; q > to; q--) l.p[q] = l.p[q - 1];
          l.p[to] = mv;
        }
      }
    }
  }
  fix_vmerge(&l);
  if (at >= l.n) at = 0;
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, caret, at);
  }
}
static int del_up(Doc *d, Pos pa, Pos pb, Pos *caret)
{
  CList cl;
  int r0, r1, c0, c1, k, n;
  u32 i, at = 0;
  PList l;
  if (!sel_block(d, pa, pb, &cl, &r0, &r1, &c0, &c1)) return 0;
  n = r1 - r0 + 1;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    if (c.row >= r0 && c.col >= c0 && c.col + c.span - 1 <= c1) {
      /* this cell takes the text of the one n rows below it (of the same columns), or none */
      int m, src = -1;
      for (m = 0; m < cl.n; m++) if (cl.v[m].c.row == c.row + n && cl.v[m].c.col == c.col && cl.v[m].c.span == c.span) { src = m; break; }
      if (c.row == r0 && c.col == c0) at = l.n;
      if (src >= 0) for (i = cl.v[src].p0; i < cl.v[src].p1; i++) pl_add(&l, copy_cell(d, i, &c));
      else pl_add(&l, empty_cell(d, ci->p0, &c));
      continue;
    }
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, caret, at);
  }
}
int tbl_delete_cells(Doc *d, Pos a, Pos b, int how, Pos *caret)
{
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (a.para >= d->np || !in_table(&d->p[a.para]->pf)) return 0;
  use_table(d, a.para);
  switch (how) {
  case DC_LEFT: return del_left(d, a, b, caret);
  case DC_UP: return del_up(d, a, b, caret);
  case DC_ROW: return del_rows(d, a, b, caret);
  case DC_COL: return del_cols(d, a, b, caret);
  }
  return 0;
}

/* lines on given edges of given cells (the Border Painter and the Eraser) */
int tbl_set_edges(Doc *d, Pos in, const TEdge *e, int n, u8 style, u8 width, u32 color)
{
  u32 a, b, i;
  PList l;
  Pos keep = in;
  int k, any = 0;
  if (in.para >= d->np || !in_table(&d->p[in.para]->pf) || n <= 0) return 0;
  use_table(d, in.para);
  a = tbl_first(d, in.para); b = tbl_end(d, in.para);
  memset(&l, 0, sizeof l);
  for (i = a; i < b; i++) {
    Cell c = CELL(i);
    for (k = 0; k < n; k++)
      if (e[k].row == c.row && e[k].col == c.col && e[k].edge < 4) {
        int x = e[k].edge;
        any = 1;
        if (style == PB_NONE) c.borders &= (u8)~(1 << x);
        else { c.borders |= (u8)(1 << x); c.bstyle[x] = style; c.bwidth[x] = width; c.bcolor[x] = color; }
      }
    pl_add(&l, copy_cell(d, i, &c));
  }
  if (!any) { pl_drop(&l); return 0; }
  return commit_keep(d, a, b, &l, &keep);
}

/* drawing a line down through cells: rows r0..r1 get a cell edge at x (twips from the table's left
   edge). Where x isn't near a grid edge, the grid column there is split in two (a new Table; the cells
   covering it elsewhere span both parts); then each cell of those rows that x crosses becomes two,
   its text staying in the left one */
int tbl_vsplit(Doc *d, Pos in, i32 x, int r0, int r1)
{
  CList cl;
  Table *t, nt;
  i32 left = 0;
  int k, j, edge = -1, id, newcol = -1;
  u32 i, at = 0;
  PList l;
  if (in.para >= d->np || !(t = doc_table(d, &d->p[in.para]->pf))) return 0;
  use_table(d, in.para);
  if (r0 > r1) { k = r0; r0 = r1; r1 = k; }
  /* the grid edge at x, or the grid column x falls in */
  for (k = 0; k <= t->ncols; k++) {
    if (k > 0 && x - left > -90 && x - left < 90) { edge = k; break; }
    if (k < t->ncols && x > left + 90 && x < left + t->grid[k] - 90) { newcol = k; break; }
    if (k < t->ncols) left += t->grid[k];
  }
  if (edge < 0 && newcol < 0) return 0;
  if (edge >= t->ncols) return 0;                     /* the table's right edge: nothing to split */
  nt = *t;
  if (newcol >= 0) {
    if (t->ncols >= MAX_COLS) return 0;
    for (j = t->ncols - 1; j > newcol; j--) nt.grid[j + 1] = t->grid[j];
    nt.grid[newcol] = x - left; nt.grid[newcol + 1] = t->grid[newcol] - (x - left);
    nt.ncols = (u8)(t->ncols + 1);
    edge = newcol + 1;
  }
  if (!(id = doc_add_table(d, &nt)) || !cells_of(d, in.para, &cl)) return 0;
  memset(&l, 0, sizeof l);
  for (k = 0; k < cl.n; k++) {
    CInfo *ci = &cl.v[k];
    Cell c = ci->c;
    c.table = (u16)id;
    if (newcol >= 0) {
      if (c.col > newcol) c.col++;
      else if (c.col + c.span > newcol) c.span++;
    }
    if (c.row >= r0 && c.row <= r1 && c.col < edge && c.col + c.span > edge) {
      Cell rc = c;
      rc.col = (u8)edge; rc.span = (u8)(c.col + c.span - edge);
      c.span = (u8)(edge - c.col);
      if (in.para >= ci->p0 && in.para < ci->p1) at = l.n;
      for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
      pl_add(&l, empty_cell(d, ci->p1 - 1, &rc));
      continue;
    }
    if (in.para >= ci->p0 && in.para < ci->p1) at = l.n;
    for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
  }
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, &in, at);
  }
}
/* drawing a line across a row: the cells of row starting in the grid columns cols[0..n) become two,
   one above the other (a new row goes in below, with an empty cell under each of those); the row's
   other cells reach down over the new row (merged into it) */
static int crossed_col(const u8 *cols, int n, int col)
{
  int j;
  for (j = 0; j < n; j++) if (cols[j] == col) return 1;
  return 0;
}
int tbl_hsplit(Doc *d, Pos in, int row, const u8 *cols, int n)
{
  CList cl;
  int k, pass;
  u32 i, at = 0;
  PList l;
  if (in.para >= d->np || !in_table(&d->p[in.para]->pf) || n <= 0) return 0;
  use_table(d, in.para);
  if (!cells_of(d, in.para, &cl)) return 0;
  memset(&l, 0, sizeof l);
  /* the rows down to row; the new row; the rows after it, one further down */
  for (pass = 0; pass < 3; pass++)
    for (k = 0; k < cl.n; k++) {
      CInfo *ci = &cl.v[k];
      Cell c = ci->c;
      if (pass == 0 && c.row <= row) {
        if (c.row == row) {
          if (crossed_col(cols, n, c.col)) { if (c.vmerge == VM_START) c.vmerge = VM_NONE; }
          else if (!c.vmerge) c.vmerge = VM_START;
        }
        if (in.para >= ci->p0 && in.para < ci->p1) at = l.n;
        for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
      } else if (pass == 1 && c.row == row) {
        Cell nc = c;
        nc.row = (u16)(row + 1); nc.rowh = 0;
        if (crossed_col(cols, n, c.col)) {
          /* the lower half: a new empty cell, carrying on a chain the cell started */
          int m;
          nc.vmerge = VM_NONE;
          for (m = 0; m < cl.n; m++) if (cl.v[m].c.row == row + 1 && cl.v[m].c.col == c.col && cl.v[m].c.span == c.span && cl.v[m].c.vmerge == VM_CONT) nc.vmerge = VM_START;
        } else nc.vmerge = VM_CONT;
        pl_add(&l, empty_cell(d, ci->p1 - 1, &nc));
      } else if (pass == 2 && c.row > row) {
        c.row++;
        if (in.para >= ci->p0 && in.para < ci->p1) at = l.n;
        for (i = ci->p0; i < ci->p1; i++) pl_add(&l, copy_cell(d, i, &c));
      }
    }
  fix_vmerge(&l);
  {
    u32 a = cl.a, b = cl.b;
    free(cl.v);
    return commit(d, a, b, &l, &in, at);
  }
}
