/* tstyle.c - see tstyle.h */
#include <stdio.h>
#include <string.h>
#include "tstyle.h"

/* ------------------------------------------------------------------ */
/* the gallery: Quill's own styles (Table Grid is Word's plain grid, kept under its name so documents
   from Word match) */
static const struct { const char *name; u32 rgb; } accents[] = {
  { "Blue", 0x3B5BDB }, { "Teal", 0x0C8599 }, { "Green", 0x2B8A3E }, { "Orange", 0xE8590C },
  { "Red", 0xC92A2A }, { "Purple", 0x7048E8 }, { "Grey", 0x495057 }
};
#define NACC ((int)(sizeof accents / sizeof accents[0]))
enum { G_GRID, G_LINES, G_LIST, G_BANDED, G_GRIDC = G_BANDED + NACC, G_N = G_GRIDC + NACC };
static TableStyle built[G_N];
static int built_ready;

/* a colour mixed with white: keep parts in 16 of c */
static u32 tint(u32 c, int keep)
{
  u32 r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  r = (r * keep + 255 * (16 - keep)) / 16; g = (g * keep + 255 * (16 - keep)) / 16; b = (b * keep + 255 * (16 - keep)) / 16;
  return r << 16 | g << 8 | b;
}
/* small builders for the gallery: one edge set (width in eighths of a point, as Word's w:sz),
   every edge of a part explicitly without lines, and bold text for a part */
static void line(TBorder *b, u8 style, u8 width, u32 color) { b->set = 1; b->style = style; b->width = width; b->color = color; }
static void nolines(TCond *c)
{
  int k;
  for (k = 0; k < TE_N; k++) line(&c->b[k], PB_NONE, 0, COLOR_AUTO);
}
static void bold(TCond *c) { c->cmask |= CM_BOLD; c->cf.flags |= CF_BOLD; }
/* fills the gallery table once, on first use: Table Grid, Quill Lines, Quill List, then a
   Banded and a Grid style for each accent colour. Ids are fixed, so a document saved with
   one of them finds it again by id */
static void make_builtins(void)
{
  int i, k;
  TableStyle *s;
  memset(built, 0, sizeof built);
  s = &built[G_GRID];
  strcpy(s->id, "TableGrid"); strcpy(s->name, "Table Grid");
  s->c[TC_WHOLE].used = 1;
  for (k = 0; k < TE_N; k++) line(&s->c[TC_WHOLE].b[k], PB_SINGLE, 4, COLOR_AUTO);
  s = &built[G_LINES];                       /* lines above and below, and under the heading */
  strcpy(s->id, "QuillLines"); strcpy(s->name, "Quill Lines");
  s->c[TC_WHOLE].used = 1; nolines(&s->c[TC_WHOLE]);
  line(&s->c[TC_WHOLE].b[TE_TOP], PB_SINGLE, 12, COLOR_AUTO);
  line(&s->c[TC_WHOLE].b[TE_BOTTOM], PB_SINGLE, 12, COLOR_AUTO);
  s->c[TC_FIRSTROW].used = 1; bold(&s->c[TC_FIRSTROW]);
  line(&s->c[TC_FIRSTROW].b[TE_BOTTOM], PB_SINGLE, 4, COLOR_AUTO);
  s->c[TC_LASTROW].used = 1; bold(&s->c[TC_LASTROW]);
  line(&s->c[TC_LASTROW].b[TE_TOP], PB_SINGLE, 4, COLOR_AUTO);
  s = &built[G_LIST];                        /* light rules between the rows, a double line over the total */
  strcpy(s->id, "QuillList"); strcpy(s->name, "Quill List");
  s->c[TC_WHOLE].used = 1; nolines(&s->c[TC_WHOLE]);
  line(&s->c[TC_WHOLE].b[TE_INSIDEH], PB_SINGLE, 4, 0xBFBFBF);
  line(&s->c[TC_WHOLE].b[TE_BOTTOM], PB_SINGLE, 4, 0xBFBFBF);
  s->c[TC_FIRSTROW].used = 1; bold(&s->c[TC_FIRSTROW]);
  line(&s->c[TC_FIRSTROW].b[TE_BOTTOM], PB_SINGLE, 8, COLOR_AUTO);
  s->c[TC_LASTROW].used = 1; bold(&s->c[TC_LASTROW]);
  line(&s->c[TC_LASTROW].b[TE_TOP], PB_DOUBLE, 4, COLOR_AUTO);
  s->c[TC_FIRSTCOL].used = 1; bold(&s->c[TC_FIRSTCOL]);
  for (i = 0; i < NACC; i++) {
    u32 a = accents[i].rgb;
    s = &built[G_BANDED + i];                /* a filled heading in the colour, rows banded in a tint of it */
    sprintf(s->id, "QuillBanded%s", accents[i].name); sprintf(s->name, "Quill Banded %s", accents[i].name);
    s->c[TC_WHOLE].used = 1; nolines(&s->c[TC_WHOLE]);
    line(&s->c[TC_WHOLE].b[TE_BOTTOM], PB_SINGLE, 8, a);
    s->c[TC_BAND1H].used = 1; s->c[TC_BAND1H].shade_set = 1; s->c[TC_BAND1H].shade = tint(a, 3);
    s->c[TC_BAND1V].used = 1; s->c[TC_BAND1V].shade_set = 1; s->c[TC_BAND1V].shade = tint(a, 3);
    s->c[TC_FIRSTROW].used = 1; bold(&s->c[TC_FIRSTROW]);
    s->c[TC_FIRSTROW].shade_set = 1; s->c[TC_FIRSTROW].shade = a;
    s->c[TC_FIRSTROW].cmask |= CM_COLOR; s->c[TC_FIRSTROW].cf.color = 0xFFFFFF;
    s->c[TC_LASTROW].used = 1; bold(&s->c[TC_LASTROW]);
    line(&s->c[TC_LASTROW].b[TE_TOP], PB_DOUBLE, 4, a);
    s->c[TC_LASTROW].shade_set = 1; s->c[TC_LASTROW].shade = COLOR_AUTO;
    s->c[TC_FIRSTCOL].used = 1; bold(&s->c[TC_FIRSTCOL]);
    s->c[TC_LASTCOL].used = 1; bold(&s->c[TC_LASTCOL]);
    s = &built[G_GRIDC + i];                 /* a grid in a tint of the colour, the heading underlined in it */
    sprintf(s->id, "QuillGrid%s", accents[i].name); sprintf(s->name, "Quill Grid %s", accents[i].name);
    s->c[TC_WHOLE].used = 1;
    for (k = 0; k < TE_N; k++) line(&s->c[TC_WHOLE].b[k], PB_SINGLE, 4, tint(a, 8));
    s->c[TC_BAND1H].used = 1; s->c[TC_BAND1H].shade_set = 1; s->c[TC_BAND1H].shade = tint(a, 2);
    s->c[TC_FIRSTROW].used = 1; bold(&s->c[TC_FIRSTROW]);
    line(&s->c[TC_FIRSTROW].b[TE_BOTTOM], PB_SINGLE, 12, a);
    s->c[TC_FIRSTROW].cmask |= CM_COLOR; s->c[TC_FIRSTROW].cf.color = a;
    s->c[TC_LASTROW].used = 1; bold(&s->c[TC_LASTROW]);
    line(&s->c[TC_LASTROW].b[TE_TOP], PB_DOUBLE, 4, a);
    s->c[TC_FIRSTCOL].used = 1; bold(&s->c[TC_FIRSTCOL]);
    s->c[TC_LASTCOL].used = 1; bold(&s->c[TC_LASTCOL]);
  }
  built_ready = 1;
}
/* the number of gallery styles, for the ribbon's table style list */
int tstyle_builtin_count(void) { return G_N; }
/* gallery style i (built on first call; static, do not free), or NULL out of range */
const TableStyle *tstyle_builtin(int i)
{
  if (!built_ready) make_builtins();
  return i >= 0 && i < G_N ? &built[i] : NULL;
}
/* reuses the document's copy of the style if it already has one with the same id, so
   applying the same gallery style twice does not add duplicates */
int tstyle_use_builtin(Doc *d, int i)
{
  const TableStyle *s = tstyle_builtin(i);
  int k;
  if (!s) return 0;
  if ((k = doc_tstyle(d, s->id))) return k;
  return doc_add_tstyle(d, s);
}

/* ------------------------------------------------------------------ */
/* a style's parts on a cell */
/* one edge from a style part onto the cell: an unset edge leaves what lower layers gave,
   an explicit "none" removes it, anything else replaces it */
static void put_edge(CellLook *o, int e, const TBorder *b)
{
  if (!b->set) return;
  if (b->style == PB_NONE) { o->borders &= (u8)~(1 << e); return; }
  o->borders |= (u8)(1 << e);
  o->bstyle[e] = b->style; o->bwidth[e] = b->width; o->bcolor[e] = b->color;
}
/* part c on a cell in the block of rows r0..r1, columns c0..c1 */
static void put_cond(CellLook *o, const TCond *c, int row, int col, int span, int r0, int r1, int c0, int c1)
{
  int k;
  if (!c->used) return;
  put_edge(o, E_TOP, &c->b[row == r0 ? TE_TOP : TE_INSIDEH]);
  put_edge(o, E_BOTTOM, &c->b[row == r1 ? TE_BOTTOM : TE_INSIDEH]);
  put_edge(o, E_LEFT, &c->b[col == c0 ? TE_LEFT : TE_INSIDEV]);
  put_edge(o, E_RIGHT, &c->b[col + span - 1 >= c1 ? TE_RIGHT : TE_INSIDEV]);
  for (k = 0; k < 4; k++) put_edge(o, k, &c->cb[k]);
  if (c->shade_set) o->shade = c->shade;
  if (c->cmask) { cf_apply(&o->cf, c->cmask, &c->cf); o->cmask |= c->cmask; }
  if (c->align_set) { o->align_set = 1; o->align = c->align; }
}
/* the chain of styles from ts back through what it is based on, the base first */
static int chain(Doc *d, int ts, const TableStyle **out)
{
  int n = 0, k;
  const TableStyle *v[8];
  while (ts > 0 && ts <= d->ntstyles && n < 8) {
    v[n++] = &d->tstyles[ts - 1];
    ts = d->tstyles[ts - 1].based[0] ? doc_tstyle(d, d->tstyles[ts - 1].based) : 0;
  }
  for (k = 0; k < n; k++) out[k] = v[n - 1 - k];
  return n;
}
static void cell_of(const TableStyle *const *ch, int n, u16 look, int row, int col, int span, int nrows, int ncols, const TBorder *direct, CellLook *o);
/* resolves the based-on chain (base first, so the derived style wins) and lays it on the cell */
void tstyle_cell(Doc *d, int ts, u16 look, int row, int col, int span, int nrows, int ncols, const TBorder *direct, CellLook *o)
{
  const TableStyle *ch[8];
  int n = chain(d, ts, ch);
  cell_of(ch, n, look, row, col, span, nrows, ncols, direct, o);
}
/* a single style with no direct lines and no spans; s may be NULL for a plain cell */
void tstyle_preview(const TableStyle *s, u16 look, int row, int col, int nrows, int ncols, CellLook *o)
{
  cell_of(&s, s ? 1 : 0, look, row, col, 1, nrows, ncols, NULL, o);
}
/* the layering itself, later parts winning: whole table (then the table's direct lines), banded
   columns and rows (skipping the heading/total rows and first/last columns when those are
   switched on, and counting bands from after them), first/last column, heading/total row, and
   the corners. Each part's own outer edges apply to its block: a band row is a block of one
   row, so its top and bottom count as outside lines */
static void cell_of(const TableStyle *const *ch, int n, u16 look, int row, int col, int span, int nrows, int ncols, const TBorder *direct, CellLook *o)
{
  int k, i;
  int fr = (look & LK_FIRSTROW) != 0, lr = (look & LK_LASTROW) != 0, fc = (look & LK_FIRSTCOL) != 0, lc = (look & LK_LASTCOL) != 0;
  int last;
  memset(o, 0, sizeof *o);
  o->shade = COLOR_AUTO;
  for (k = 0; k < 4; k++) o->bcolor[k] = COLOR_AUTO;
  if (span < 1) span = 1;
  last = col + span >= ncols;
  /* the whole table: the styles', then the table's own lines */
  for (i = 0; i < n; i++) put_cond(o, &ch[i]->c[TC_WHOLE], row, col, span, 0, nrows - 1, 0, ncols - 1);
  if (direct) {
    TCond dc;
    memset(&dc, 0, sizeof dc);
    dc.used = 1;
    for (k = 0; k < TE_N; k++) dc.b[k] = direct[k];
    put_cond(o, &dc, row, col, span, 0, nrows - 1, 0, ncols - 1);
  }
  for (i = 0; i < n; i++) {
    const TableStyle *s = ch[i];
    if (!(look & LK_NOVBAND) && !(fc && col == 0) && !(lc && last)) {
      int b = (col - fc) & 1;
      put_cond(o, &s->c[b ? TC_BAND2V : TC_BAND1V], row, col, span, 0, nrows - 1, col, col + span - 1);
    }
    if (!(look & LK_NOHBAND) && !(fr && row == 0) && !(lr && row == nrows - 1)) {
      int b = (row - fr) & 1;
      put_cond(o, &s->c[b ? TC_BAND2H : TC_BAND1H], row, col, span, row, row, 0, ncols - 1);
    }
    if (fc && col == 0) put_cond(o, &s->c[TC_FIRSTCOL], row, col, span, 0, nrows - 1, col, col + span - 1);
    if (lc && last) put_cond(o, &s->c[TC_LASTCOL], row, col, span, 0, nrows - 1, col, col + span - 1);
    if (fr && row == 0) put_cond(o, &s->c[TC_FIRSTROW], row, col, span, row, row, 0, ncols - 1);
    if (lr && row == nrows - 1) put_cond(o, &s->c[TC_LASTROW], row, col, span, row, row, 0, ncols - 1);
    if (fr && row == 0 && lc && last) put_cond(o, &s->c[TC_NE], row, col, span, row, row, col, col + span - 1);
    if (fr && row == 0 && fc && col == 0) put_cond(o, &s->c[TC_NW], row, col, span, row, row, col, col + span - 1);
    if (lr && row == nrows - 1 && lc && last) put_cond(o, &s->c[TC_SE], row, col, span, row, row, col, col + span - 1);
    if (lr && row == nrows - 1 && fc && col == 0) put_cond(o, &s->c[TC_SW], row, col, span, row, row, col, col + span - 1);
  }
}
/* lets callers know which character formatting applying the style would overwrite */
unsigned tstyle_cmask(Doc *d, int ts)
{
  const TableStyle *ch[8];
  int n = chain(d, ts, ch), i, k;
  unsigned m = 0;
  for (i = 0; i < n; i++) for (k = 0; k < TC_N; k++) if (ch[i]->c[k].used) m |= ch[i]->c[k].cmask;
  return m;
}
