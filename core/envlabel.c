/*
 * envlabel.c - envelopes and labels (WP4, core/envlabel.h). Sizes are the products' published
 * dimensions (Avery and the envelope standards), in twips.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "envlabel.h"
#include "field.h"
#include "table.h"

#define IN(th) ((i32)((th) * 1440L / 1000))          /* thousandths of an inch */
#define MM(t) ((i32)((t) * 1440L / 254))             /* tenths of a millimetre */

const EnvSize env_sizes[] = {
  { "Size 10 (4 1/8 x 9 1/2 in)", IN(9500), IN(4125) }, { "Size 6 3/4 (3 5/8 x 6 1/2 in)", IN(6500), IN(3625) },
  { "Monarch (3 7/8 x 7 1/2 in)", IN(7500), IN(3875) }, { "Size 9 (3 7/8 x 8 7/8 in)", IN(8875), IN(3875) },
  { "Size 11 (4 1/2 x 10 3/8 in)", IN(10375), IN(4500) }, { "Size 12 (4 3/4 x 11 in)", IN(11000), IN(4750) },
  { "DL (110 x 220 mm)", MM(2200), MM(1100) }, { "C4 (229 x 324 mm)", MM(3240), MM(2290) }, { "C5 (162 x 229 mm)", MM(2290), MM(1620) },
  { "C6 (114 x 162 mm)", MM(1620), MM(1140) }, { "C6/C5 (114 x 229 mm)", MM(2290), MM(1140) }, { "B5 (176 x 250 mm)", MM(2500), MM(1760) },
  { "B6 (125 x 176 mm)", MM(1760), MM(1250) } };
const int env_nsizes = (int)(sizeof env_sizes / sizeof env_sizes[0]);

const char *const label_vendors[] = { "Avery US Letter", "Avery A4/A5", "Other" };
const int label_nvendors = 3;
#define LET IN(8500), IN(11000)
#define A4 MM(2100), MM(2970)
const LabelDef label_defs[] = {
  { "Avery US Letter", "5160 Easy Peel Address Labels", "Address labels, 30 per sheet", LET, IN(500), IN(188), IN(2625), IN(1000), IN(2750), IN(1000), 3, 10 },
  { "Avery US Letter", "5161 Easy Peel Address Labels", "Address labels, 20 per sheet", LET, IN(500), IN(156), IN(4000), IN(1000), IN(4188), IN(1000), 2, 10 },
  { "Avery US Letter", "5162 Easy Peel Address Labels", "Address labels, 14 per sheet", LET, IN(833), IN(156), IN(4000), IN(1333), IN(4188), IN(1333), 2, 7 },
  { "Avery US Letter", "5163 Shipping Labels", "Shipping labels, 10 per sheet", LET, IN(500), IN(156), IN(4000), IN(2000), IN(4188), IN(2000), 2, 5 },
  { "Avery US Letter", "5164 Shipping Labels", "Shipping labels, 6 per sheet", LET, IN(500), IN(156), IN(4000), IN(3333), IN(4188), IN(3333), 2, 3 },
  { "Avery US Letter", "5165 Full-Sheet Labels", "Full sheet, 1 per sheet", LET, 0, 0, IN(8500), IN(11000), IN(8500), IN(11000), 1, 1 },
  { "Avery US Letter", "5167 Return Address Labels", "Return address labels, 80 per sheet", LET, IN(500), IN(281), IN(1750), IN(500), IN(2063), IN(500), 4, 20 },
  { "Avery US Letter", "5168 Shipping Labels", "Shipping labels, 4 per sheet", LET, IN(500), IN(500), IN(3500), IN(5000), IN(4000), IN(5000), 2, 2 },
  { "Avery US Letter", "5195 Return Address Labels", "Return address labels, 60 per sheet", LET, IN(500), IN(281), IN(1750), IN(667), IN(2063), IN(667), 4, 15 },
  { "Avery US Letter", "5366 File Folder Labels", "Filing labels, 30 per sheet", LET, IN(500), IN(531), IN(3438), IN(667), IN(4000), IN(667), 2, 15 },
  { "Avery US Letter", "5371 Business Cards", "Business cards, 10 per sheet", LET, IN(500), IN(750), IN(3500), IN(2000), IN(3500), IN(2000), 2, 5 },
  { "Avery US Letter", "5395 Name Badge Labels", "Name badges, 8 per sheet", LET, IN(625), IN(688), IN(3375), IN(2333), IN(3750), IN(2500), 2, 4 },
  { "Avery US Letter", "8160 Address Labels", "Address labels, 30 per sheet", LET, IN(500), IN(188), IN(2625), IN(1000), IN(2750), IN(1000), 3, 10 },
  { "Avery A4/A5", "L7159 Address Labels", "Address labels, 24 per sheet", A4, MM(129), MM(65), MM(635), MM(339), MM(660), MM(339), 3, 8 },
  { "Avery A4/A5", "L7160 Address Labels", "Address labels, 21 per sheet", A4, MM(151), MM(72), MM(635), MM(381), MM(660), MM(381), 3, 7 },
  { "Avery A4/A5", "L7161 Address Labels", "Address labels, 18 per sheet", A4, MM(88), MM(72), MM(635), MM(466), MM(660), MM(466), 3, 6 },
  { "Avery A4/A5", "L7162 Address Labels", "Address labels, 16 per sheet", A4, MM(129), MM(47), MM(991), MM(339), MM(1016), MM(339), 2, 8 },
  { "Avery A4/A5", "L7163 Address Labels", "Address labels, 14 per sheet", A4, MM(152), MM(47), MM(991), MM(381), MM(1016), MM(381), 2, 7 },
  { "Avery A4/A5", "L7165 Parcel Labels", "Parcel labels, 8 per sheet", A4, MM(131), MM(47), MM(991), MM(677), MM(1016), MM(677), 2, 4 },
  { "Avery A4/A5", "L7167 Shipping Labels", "Full sheet, 1 per sheet", A4, MM(40), MM(52), MM(1996), MM(2891), MM(1996), MM(2891), 1, 1 },
  { "Avery A4/A5", "L7173 Shipping Labels", "Shipping labels, 10 per sheet", A4, MM(60), MM(47), MM(991), MM(570), MM(1016), MM(570), 2, 5 },
  { "Avery A4/A5", "L7651 Mini Labels", "Mini labels, 65 per sheet", A4, MM(107), MM(47), MM(381), MM(212), MM(406), MM(212), 5, 13 },
  { "Avery A4/A5", "J8160 Address Labels", "Address labels, 21 per sheet", A4, MM(151), MM(72), MM(635), MM(381), MM(660), MM(381), 3, 7 },
  { "Avery A4/A5", "C32011 Business Cards", "Business cards, 10 per sheet", A4, MM(215), MM(150), MM(850), MM(540), MM(850), MM(540), 2, 5 },
  { "Other", "30 Per Page (1 x 2 5/8 in)", "Address labels, Letter", LET, IN(500), IN(188), IN(2625), IN(1000), IN(2750), IN(1000), 3, 10 },
  { "Other", "24 Per Page (70 x 37 mm)", "Address labels, A4", A4, MM(45), 0, MM(700), MM(370), MM(700), MM(370), 3, 8 },
  { "Other", "40 Per Page (52.5 x 29.7 mm)", "Small labels, A4", A4, 0, 0, MM(525), MM(297), MM(525), MM(297), 4, 10 } };
const int label_ndefs = (int)(sizeof label_defs / sizeof label_defs[0]);

/* a paragraph in Normal with no space round it */
static Para *plain_para(Doc *d, const Cell *cell)
{
  ParaFmt pf = d->styles[0].pf;
  pf.before = pf.after = 0;
  pf.line = 240; pf.linerule = LS_AUTO;
  if (cell) pf.cell = *cell;
  return para_new(&pf, &d->styles[0].cf);
}
/* the lines of text as paragraphs added to arr (*n so far), each made by mk; 0 oom */
static int add_lines(Doc *d, Para ***arr, u32 *n, u32 *cap, const char *text, const Cell *cell)
{
  const char *s = text ? text : "";
  do {
    const char *e = strchr(s, '\n');
    size_t len = e ? (size_t)(e - s) : strlen(s);
    Para *p = plain_para(d, cell);
    if (!p) return 0;
    if (len && s[len - 1] == '\r') len--;
    if (len && !para_append(p, s, (u32)len, &d->styles[0].cf)) { para_free(p); return 0; }
    if (*n == *cap) {
      Para **na = (Para **)realloc(*arr, (size_t)(*cap = *cap ? *cap * 2 : 16) * sizeof(Para *));
      if (!na) { para_free(p); return 0; }
      *arr = na;
    }
    (*arr)[(*n)++] = p;
    s = e ? e + 1 : NULL;
  } while (s);
  return 1;
}
void env_delivery_place(int size, i32 *left, i32 *top)
{
  const EnvSize *e = &env_sizes[size >= 0 && size < env_nsizes ? size : 0];
  *left = e->w * 42 / 100;
  *top = e->h * 485 / 1000;
}
static void env_page(const EnvSpec *e, PageSetup *pg)
{
  const EnvSize *s = &env_sizes[e->size >= 0 && e->size < env_nsizes ? e->size : 0];
  memset(pg, 0, sizeof *pg);
  pg->width = s->w; pg->height = s->h;
  pg->top = 360; pg->left = 360; pg->right = 360; pg->bottom = 360;
  pg->header = 0; pg->footer = 0;
}
/* the envelope's paragraphs: the return address at the top left, the delivery address where it goes */
static int env_paras(Doc *d, const EnvSpec *e, const PageSetup *pg, Para ***arr, u32 *n)
{
  u32 cap = 0, first, i, nret;
  i32 dl = e->dleft, dt = e->dtop, line = d->styles[0].cf.size * 12, before;
  *arr = NULL; *n = 0;
  if (!dl || !dt) env_delivery_place(e->size, &dl, &dt);
  if (e->ret && e->ret[0] && !add_lines(d, arr, n, &cap, e->ret, NULL)) return 0;
  nret = *n;
  first = *n;
  if (!add_lines(d, arr, n, &cap, e->delivery, NULL)) return 0;
  before = dt - pg->top - (i32)nret * line;
  if (before < 0) before = 0;
  for (i = first; i < *n; i++) {
    Para *p = (*arr)[i];
    p->pf.left = dl - pg->left;
    if (i == first) p->pf.before = before;
  }
  return 1;
}
Doc *env_doc(const EnvSpec *e)
{
  Doc *d = doc_new();
  Para **arr = NULL;
  u32 n = 0, i;
  if (!d) return NULL;
  env_page(e, &d->page);
  if (!env_paras(d, e, &d->page, &arr, &n) || !doc_replace(d, 0, d->np, arr, n, doc_start(d))) {
    for (i = 0; arr && i < n; i++) para_free(arr[i]);
    free(arr); doc_free(d);
    return NULL;
  }
  free(arr);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}
int env_add_to_doc(Doc *d, const EnvSpec *e)
{
  Para **arr = NULL;
  u32 n = 0, i;
  SectFmt s;
  int sn;
  Pos z;
  sect_default(&s);
  s.start = SB_NEXT;
  s.haspage = 1;
  env_page(e, &s.page);
  if (!(sn = doc_add_sect(d, &s)) || !env_paras(d, e, &s.page, &arr, &n) || !n) { for (i = 0; arr && i < n; i++) para_free(arr[i]); free(arr); return 0; }
  arr[n - 1]->pf.sect = (u16)sn;
  z.para = 0; z.off = 0;
  if (!doc_replace(d, 0, 0, arr, n, z)) { free(arr); return 0; }
  free(arr);
  return 1;
}

/* ---- labels */
/* the label columns of a label table (gap columns between them when the labels have space between) */
static int has_gaps(const LabelDef *l) { return l->cols > 1 && l->hpitch > l->w + 20; }
Doc *label_doc(const LabelDef *l, const char *text, int single, int row, int col, int merge)
{
  Doc *d = doc_new();
  Table t;
  int gaps = has_gaps(l), r, c, tn;
  Para **arr = NULL, *last;
  u32 n = 0, cap = 0, i;
  i32 used;
  if (!d) return NULL;
  memset(&d->page, 0, sizeof d->page);
  d->page.width = l->pagew; d->page.height = l->pageh;
  d->page.top = l->top; d->page.left = l->side;
  used = l->side + (l->cols - 1) * l->hpitch + l->w;
  d->page.right = l->pagew - used > 0 ? l->pagew - used : 0;
  used = l->top + l->rows * l->vpitch + 40;
  d->page.bottom = l->pageh - used > 0 ? l->pageh - used : 0;
  memset(&t, 0, sizeof t);
  t.ncols = (u8)(gaps ? 2 * l->cols - 1 : l->cols);
  for (c = 0; c < t.ncols; c++) t.grid[c] = gaps && (c & 1) ? l->hpitch - l->w : l->w;
  t.marl = t.marr = 72;
  t.look = LK_DEFAULT;
  if (!(tn = doc_add_table(d, &t))) { doc_free(d); return NULL; }
  for (r = 0; r < l->rows; r++)
    for (c = 0; c < t.ncols; c++) {
      Cell cell;
      int label = !gaps || !(c & 1), lc = gaps ? c / 2 : c;
      memset(&cell, 0, sizeof cell);
      cell.table = (u16)tn; cell.row = (u16)r; cell.col = (u8)c; cell.span = 1;
      cell.rowh = (i16)-(l->vpitch > 32000 ? 32000 : l->vpitch);
      cell.valign = 1;
      cell.shade = COLOR_AUTO;
      for (i = 0; i < 4; i++) cell.bcolor[i] = COLOR_AUTO;
      if (!add_lines(d, &arr, &n, &cap, label && !merge && (!single || (r == row && lc == col)) ? text : "", &cell)) goto oom;
    }
  /* a tiny paragraph after the table (a document can't end in one) */
  if (!add_lines(d, &arr, &n, &cap, "", NULL)) goto oom;
  last = arr[n - 1];
  last->pf.line = 20; last->pf.linerule = LS_EXACT;
  last->r[0].f.size = 2;
  if (!doc_replace(d, 0, d->np, arr, n, doc_start(d))) { arr = NULL; goto oom; }
  free(arr);
  if (merge) label_update(d);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
oom:
  for (i = 0; arr && i < n; i++) para_free(arr[i]);
  free(arr); doc_free(d);
  return NULL;
}
/* whether table column c of a table is a label (not a gap between labels) */
static int label_col(const Table *t, int c)
{
  int gaps = t->ncols > 2 && (t->ncols & 1) && t->grid[1] < t->grid[0] / 2;
  return !gaps || !(c & 1);
}
int label_update(Doc *d)
{
  u32 pi, p0 = 0, p1, n = 0;
  Table *t = NULL;
  Fragment *f = NULL;
  int k, filled = 0;
  Pos a, b;
  for (pi = 0; pi < d->np && !d->p[pi]->pf.cell.table; pi++) ;
  if (pi == d->np || !(t = doc_table(d, &d->p[pi]->pf))) return 0;
  p0 = pi;
  p1 = cell_end(d, p0);
  a.para = p0; a.off = 0; b.para = p1 - 1; b.off = d->p[p1 - 1]->len;
  f = doc_fragment(d, a, b);                  /* NULL: the first label is empty */
  doc_begin(d, a);
  /* the other labels, from the last back (so the earlier ones stay where they are) */
  {
    u32 cells[2048];
    u32 end = tbl_end(d, p0);
    for (pi = p1; pi < end && n < 2048; pi = cell_end(d, pi)) if (label_col(t, d->p[pi]->pf.cell.col)) cells[n++] = pi;
    for (k = (int)n - 1; k >= 0; k--) {
      u32 cs = cells[k], ce = cell_end(d, cs);
      Pos s, e;
      s.para = cs; s.off = 0;
      e.para = ce - 1; e.off = d->p[ce - 1]->len;
      if (pos_cmp(s, e)) doc_delete(d, s, e);
      field_insert_result(d, &s, " NEXT ", "\xC2\xAB" "Next Record" "\xC2\xBB", NULL, NULL);
      if (f) doc_paste(d, &s, f, PASTE_KEEP);
      filled++;
    }
  }
  doc_end(d, a);
  fragment_free(f);
  return filled;
}
