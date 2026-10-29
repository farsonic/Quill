/* tbldraw.c - see tbldraw.h (WP21b; the Border Painter is WP21a's) */
#include <stdlib.h>
#include <string.h>
#include "tbldraw.h"
#include "table.h"

#define SLACK 150               /* how near (twips) a click must be to an edge */

static i32 iabs(i32 v) { return v < 0 ? -v : v; }
/* the first paragraph of a laid out cell (table, row, grid column), as a position; 0 if none */
static int box_pos(Doc *d, const CellBox *c, Pos *p)
{
  u32 i;
  for (i = 0; i < d->np; i++) {
    const Cell *x = cell_in(&d->p[i]->pf, c->table);
    if (x && x->row == c->row && x->col == c->col) { p->para = i; p->off = 0; return 1; }
  }
  return 0;
}
/* the overlap of a0..a1 and b0..b1 */
static i32 overlap(i32 a0, i32 a1, i32 b0, i32 b1)
{
  i32 lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
  return hi > lo ? hi - lo : 0;
}
/* the last row a cell's merge (down from row) reaches */
static int chain_end(Doc *d, Pos in, int row, int col)
{
  u32 p;
  int r = row;
  for (;;) {
    p = tbl_cell_para(d, in, r + 1, col);
    if (p == (u32)-1 || d->p[p]->pf.cell.vmerge != VM_CONT || d->p[p]->pf.cell.col != col) break;
    r++;
  }
  return r;
}
/* the table's left edge on the page, from one of its cells' boxes */
static i32 table_left(Doc *d, const CellBox *c)
{
  Table *t = c->table && c->table <= d->ntables ? &d->tables[c->table - 1] : NULL;
  i32 x = c->x;
  int k;
  if (t) for (k = 0; k < c->col && k < t->ncols; k++) x -= t->grid[k];
  return x;
}

int tbl_draw_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, Pos *caret)
{
  i32 lx = x0 < x1 ? x0 : x1, rx = x0 < x1 ? x1 : x0, ty = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
  i32 dx = rx - lx, dy = by - ty;
  u32 i;
  if (dx < 120 && dy < 120) return 0;
  if (dy * 3 <= dx) {
    /* a line across: the cells it runs through (at least a third of their width), in the top row
       of the first table it meets */
    i32 y = (y0 + y1) / 2;
    u8 cols[MAX_COLS];
    int n = 0, row = -1;
    u16 tab = 0;
    Pos in;
    for (i = 0; i < L->ncells; i++) {
      const CellBox *c = &L->cells[i];
      if (c->page != page || c->depth || y <= c->y + 60 || y >= c->y + c->h - 60 || overlap(lx, rx, c->x, c->x + c->w) * 3 < c->w) continue;
      if (!tab) { tab = c->table; row = c->row; }
      if (c->table == tab && c->row < row) row = c->row;
    }
    if (!tab) return 0;
    for (i = 0; i < L->ncells && n < MAX_COLS; i++) {
      const CellBox *c = &L->cells[i];
      if (c->page != page || c->table != tab || c->row != row || c->depth || y <= c->y + 60 || y >= c->y + c->h - 60 ||
          overlap(lx, rx, c->x, c->x + c->w) * 3 < c->w) continue;
      if (!n && !box_pos(d, c, &in)) return 0;
      cols[n++] = c->col;
    }
    if (!n || !tbl_hsplit(d, in, row, cols, n)) return 0;
    *caret = in;
    return 1;
  }
  if (dx * 3 <= dy) {
    /* a line down: the cells it runs through, in the first table it meets */
    i32 x = (x0 + x1) / 2, tl = 0;
    int r0 = 0x7FFF, r1 = -1;
    u16 tab = 0;
    Pos in;
    for (i = 0; i < L->ncells; i++) {
      const CellBox *c = &L->cells[i];
      if (c->page != page || c->depth || x <= c->x + 60 || x >= c->x + c->w - 60 || overlap(ty, by, c->y, c->y + c->h) * 3 < c->h) continue;
      if (!tab) { tab = c->table; tl = table_left(d, c); if (!box_pos(d, c, &in)) return 0; }
      if (c->table != tab) continue;
      if (c->row < r0) r0 = c->row;
      {
        int e = chain_end(d, in, c->row, c->col);
        if (e > r1) r1 = e;
      }
    }
    if (!tab || !tbl_vsplit(d, in, x - tl, r0, r1)) return 0;
    *caret = in;
    return 1;
  }
  /* a box: a table of one cell that size, where it was drawn (not over a table) */
  for (i = 0; i < L->ncells; i++) {
    const CellBox *c = &L->cells[i];
    if (c->page == page && overlap(lx, rx, c->x, c->x + c->w) && overlap(ty, by, c->y, c->y + c->h)) return 0;
  }
  if (dx < 360 || dy < 200) return 0;
  {
    Pos at = lay_hit(L, page, lx, ty);
    const PageSetup *pg = lay_page(L, page);
    i32 colx = 0;
    Table nt, *t;
    Pos c;
    if (at.para >= d->np || in_table(&d->p[at.para]->pf)) return 0;
    if (at.para < L->npl && L->pl[at.para].placed0 < L->nplaced) colx = L->placed[L->pl[at.para].placed0].x;
    at.off = 0;
    doc_begin(d, at);
    if (!tbl_insert(d, &at, 1, 1, dx)) { doc_end(d, at); return 0; }
    c = at;
    if ((t = doc_table(d, &d->p[c.para]->pf))) {
      nt = *t;
      nt.indent = lx - pg->left - colx;
      if (nt.indent < -1440) nt.indent = -1440;
      nt.autofit = 0;
      tbl_set_table(d, &c, &nt);
    }
    tbl_cell_format(d, c, c, TF_ROWH, (u32)(i32)(dy > 31000 ? 31000 : dy));
    doc_end(d, c);
    *caret = c;
    return 1;
  }
}

/* the cell edge nearest a point (within SLACK): the box and edge (E_); -1 if none */
static int nearest_edge(Layout *L, u32 page, i32 x, i32 y, int *edge)
{
  u32 i;
  int best = -1;
  i32 bd = SLACK + 1;
  for (i = 0; i < L->ncells; i++) {
    const CellBox *c = &L->cells[i];
    i32 dd[4];
    int e;
    if (c->page != page || c->depth) continue;
    if (x < c->x - SLACK || x > c->x + c->w + SLACK || y < c->y - SLACK || y > c->y + c->h + SLACK) continue;
    dd[E_TOP] = x >= c->x && x <= c->x + c->w ? iabs(y - c->y) : 0x7FFF;
    dd[E_BOTTOM] = x >= c->x && x <= c->x + c->w ? iabs(y - (c->y + c->h)) : 0x7FFF;
    dd[E_LEFT] = y >= c->y && y <= c->y + c->h ? iabs(x - c->x) : 0x7FFF;
    dd[E_RIGHT] = y >= c->y && y <= c->y + c->h ? iabs(x - (c->x + c->w)) : 0x7FFF;
    for (e = 0; e < 4; e++) if (dd[e] < bd) { bd = dd[e]; best = (int)i; *edge = e; }
  }
  return best;
}
/* the box across edge e of box k (sharing that edge, at the point x, y), -1 if none */
static int across(Layout *L, int k, int e, i32 x, i32 y)
{
  const CellBox *a = &L->cells[k];
  u32 i;
  for (i = 0; i < L->ncells; i++) {
    const CellBox *b = &L->cells[i];
    if ((int)i == k || b->page != a->page || b->table != a->table || b->depth) continue;
    switch (e) {
    case E_RIGHT: if (iabs(b->x - (a->x + a->w)) < 40 && y >= b->y && y <= b->y + b->h) return (int)i; break;
    case E_LEFT: if (iabs(b->x + b->w - a->x) < 40 && y >= b->y && y <= b->y + b->h) return (int)i; break;
    case E_BOTTOM: if (iabs(b->y - (a->y + a->h)) < 40 && x >= b->x && x <= b->x + b->w) return (int)i; break;
    default: if (iabs(b->y + b->h - a->y) < 40 && x >= b->x && x <= b->x + b->w) return (int)i;
    }
  }
  return -1;
}

int tbl_erase_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, Pos *caret)
{
  i32 x = (x0 + x1) / 2, y = (y0 + y1) / 2;
  int e = 0, k = nearest_edge(L, page, x, y, &e), o;
  Pos pa, pb;
  if (k < 0 || !box_pos(d, &L->cells[k], &pa)) return 0;
  o = across(L, k, e, x, y);
  if (o >= 0 && box_pos(d, &L->cells[o], &pb)) {
    /* an edge between two cells: they become one */
    if (!tbl_merge(d, pa, pb)) return 0;
    *caret = pos_cmp(pa, pb) < 0 ? pa : pb;
    if (caret->para >= d->np) *caret = pa;
    return 1;
  }
  {
    /* the table's outside: the line goes */
    TEdge te;
    te.row = L->cells[k].row; te.col = L->cells[k].col; te.edge = (u8)e;
    if (!tbl_set_edges(d, pa, &te, 1, PB_NONE, 0, COLOR_AUTO)) return 0;
    *caret = pa;
    return 1;
  }
}

int tbl_paint_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, u8 style, u8 width, u32 color, Pos *caret)
{
  TEdge te[128];
  int n = 0, e = 0, k;
  u16 tab = 0;
  Pos in;
  i32 dx = iabs(x1 - x0), dy = iabs(y1 - y0);
  u32 i;
  if (dx < 200 && dy < 200) {
    /* a click: the nearest edge, and the same edge seen from the cell across it */
    i32 x = (x0 + x1) / 2, y = (y0 + y1) / 2;
    int o;
    if ((k = nearest_edge(L, page, x, y, &e)) < 0 || !box_pos(d, &L->cells[k], &in)) return 0;
    te[n].row = L->cells[k].row; te[n].col = L->cells[k].col; te[n].edge = (u8)e; n++;
    if ((o = across(L, k, e, x, y)) >= 0) { te[n].row = L->cells[o].row; te[n].col = L->cells[o].col; te[n].edge = (u8)(e ^ 2); n++; }
  } else {
    /* a stroke: every edge lying along it (of the first table it meets) */
    int horiz = dx >= dy;
    i32 s0 = horiz ? (x0 < x1 ? x0 : x1) : (y0 < y1 ? y0 : y1), s1 = horiz ? (x0 < x1 ? x1 : x0) : (y0 < y1 ? y1 : y0);
    i32 at = horiz ? (y0 + y1) / 2 : (x0 + x1) / 2;
    for (i = 0; i < L->ncells && n < 128; i++) {
      const CellBox *c = &L->cells[i];
      int a, b;
      if (c->page != page || c->depth || (tab && c->table != tab)) continue;
      a = horiz ? E_TOP : E_LEFT; b = horiz ? E_BOTTOM : E_RIGHT;
      for (e = a; e <= b; e += 2) {
        i32 pos = horiz ? (e == E_TOP ? c->y : c->y + c->h) : (e == E_LEFT ? c->x : c->x + c->w);
        i32 len = horiz ? c->w : c->h;
        i32 ov = horiz ? overlap(s0, s1, c->x, c->x + c->w) : overlap(s0, s1, c->y, c->y + c->h);
        if (iabs(pos - at) > SLACK / 2 + 30 || ov * 2 < len) continue;
        if (!tab) { if (!box_pos(d, c, &in)) return 0; tab = c->table; }
        if (n < 128) { te[n].row = c->row; te[n].col = c->col; te[n].edge = (u8)e; n++; }
      }
    }
  }
  if (!n || !tbl_set_edges(d, in, te, n, style, width, color)) return 0;
  *caret = in;
  return 1;
}
