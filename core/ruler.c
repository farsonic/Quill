/*
 * ruler.c - the ruler's sums: see ruler.h. The rules are the Amiga ruler's (amiga/ruler.c, and the
 * view's MUIM_Ruler_Set and CMD_COLEDGE in amiga/view.c), moved from pixels to twips.
 */
#include <string.h>
#include "ruler.h"
#include "table.h"

#define LETTER_W 12240            /* US Letter's width: an inch ruler; any other page a centimetre one */

void ruler_geom(Doc *d, Layout *L, Pos p, int metric, RulerGeom *g)
{
  const Para *para;
  const PageSetup *ps;
  Table *t;
  i32 x, y, h;
  int k;
  memset(g, 0, sizeof *g);
  p = pos_clamp(d, p);
  para = d->p[p.para];
  lay_caret(L, p, &g->page, &x, &y, &h);
  ps = lay_page(L, g->page);
  g->pagew = ps->width; g->pageh = ps->height;
  g->mleft = ps->left; g->mright = ps->right; g->mtop = ps->top; g->mbottom = ps->bottom;
  g->x0 = ps->left;
  g->width = ps->width - ps->left - ps->right;
  t = doc_table(d, &para->pf);
  if (p.para < L->npl && L->pl[p.para].laid && L->pl[p.para].placed0 < L->nplaced) {
    const ParaLay *pl = &L->pl[p.para];
    if (t) {
      /* in a table: the cell's text area as laid out (nested and centred tables too), and the
         table's column edges, measured from its left edge (the cells' left margin before the first) */
      const Placed *pc = &L->placed[pl->placed0];
      i32 cx = 0;
      int n = t->ncols < RULER_COLS ? t->ncols : RULER_COLS;
      g->x0 = ps->left + pc->x;
      g->width = pc->w;
      for (k = 0; k < para->pf.cell.col && k < t->ncols; k++) cx += t->grid[k];
      g->tblx0 = g->x0 - cx - t->marl;
      g->ncolx = n + 1;
      g->table = para->pf.cell.table;
      for (k = 0; k < n; k++) g->colx[k + 1] = g->colx[k] + t->grid[k];
    } else {
      /* in a section's column: that column (the line the caret is on says which) */
      u32 li = 0;
      const Placed *pc;
      while (li + 1 < pl->nlines && p.off >= pl->lines[li + 1].start) li++;
      pc = &L->placed[pl->placed0 + li < L->nplaced ? pl->placed0 + li : pl->placed0];
      if (pc->w < g->width) { g->x0 = ps->left + pc->x; g->width = pc->w; }
    }
  }
  g->left = para->pf.left; g->first = para->pf.first; g->right = para->pf.right;
  memcpy(g->tabs, para->pf.tabs, sizeof g->tabs);
  memcpy(g->tabkind, para->pf.tabkind, sizeof g->tabkind);
  memcpy(g->tablead, para->pf.tablead, sizeof g->tablead);
  g->metric = (u8)(metric < 0 ? d->page.width != LETTER_W : metric != 0);
}

void ruler_unit(int metric, i32 *unit, int *nsub)
{
  /* a centimetre (567 twips) in quarters; an inch in eighths */
  *unit = metric ? 567 : 1440;
  *nsub = metric ? 4 : 8;
}

i32 ruler_snap(i32 t, int metric)
{
  i32 q = metric ? 142 : 180;         /* a quarter centimetre, an eighth of an inch */
  return t < 0 ? -((-t + q / 2) / q) * q : ((t + q / 2) / q) * q;
}

static i32 dist(i32 a, i32 b) { return a > b ? a - b : b - a; }

int ruler_hit(const RulerGeom *g, i32 x, int top, i32 slop)
{
  i32 best = slop;
  int k, part = RP_NONE;
  /* a table's column edges first: they are drawn over everything else */
  for (k = 1; k < g->ncolx; k++)
    if (dist(x, g->tblx0 + g->colx[k]) <= slop / 2) part = RP_COL + k;
  if (part) return part;
  /* the first-line marker hangs from the top; the left and right ones and the tab stops stand on
     the bottom */
  if (top) return dist(x, g->x0 + g->left + g->first) < slop ? RP_FIRST : RP_NONE;
  for (k = 0; k < RULER_TABS && g->tabs[k]; k++)
    if (g->tabs[k] > 0 && dist(x, g->x0 + g->left + g->tabs[k]) < slop * 2 / 3) return RP_TAB + k;
  if (dist(x, g->x0 + g->left) < best) { part = RP_LEFT; best = dist(x, g->x0 + g->left); }
  if (dist(x, g->x0 + g->width - g->right) < best) part = RP_RIGHT;
  return part;
}

int ruler_add_tab(RulerGeom *g, i32 x, int kind)
{
  int k;
  if (x <= g->x0 || x >= g->x0 + g->width) return RP_NONE;
  for (k = 0; k < RULER_TABS && g->tabs[k]; k++) ;
  if (k == RULER_TABS) return RP_NONE;
  g->tabs[k] = 1;                     /* (placed by the drag that follows) */
  g->tabkind[k] = (u8)kind;
  g->tablead[k] = LEAD_NONE;
  return RP_TAB + k;
}

void ruler_drag(RulerGeom *g, int part, i32 x, int off)
{
  i32 t = ruler_snap(x - g->x0, g->metric);
  if (part >= RP_COL) {
    int k = part - RP_COL;
    i32 c = ruler_snap(x - g->tblx0, g->metric);
    if (k < 1 || k >= g->ncolx) return;
    if (c < g->colx[k - 1] + RULER_MINCOL) c = g->colx[k - 1] + RULER_MINCOL;
    if (k + 1 < g->ncolx && c > g->colx[k + 1] - RULER_MINCOL) c = g->colx[k + 1] - RULER_MINCOL;
    g->colx[k] = c;
  } else if (part >= RP_TAB) {
    int k = part - RP_TAB;
    if (k < RULER_TABS) g->tabs[k] = off || t - g->left <= 0 ? -1 : t - g->left;
  } else if (part == RP_FIRST) {
    g->first = t - g->left;
  } else if (part == RP_LEFT) {
    /* the first line stays where it is: only the rest of the paragraph moves */
    i32 abs1 = g->left + g->first;
    g->left = t < 0 ? 0 : t;
    g->first = abs1 - g->left;
  } else if (part == RP_RIGHT) {
    g->right = g->width - t;
  }
}

/* a column edge dragged: the column left of it takes the new width, the one right of it gives or
   takes the difference (the last edge: the table's width changes) */
static int col_edge(Doc *d, Pos *caret, const RulerGeom *g, int edge)
{
  Table *t, nt;
  i32 left = 0, w, diff;
  int k;
  if (caret->para >= d->np) return 0;
  t = doc_table(d, &d->p[caret->para]->pf);
  if (!t || d->p[caret->para]->pf.cell.table != g->table || edge < 1 || edge > t->ncols || edge >= g->ncolx) return 0;
  nt = *t;
  for (k = 0; k < edge - 1; k++) left += t->grid[k];
  w = g->colx[edge] - left;
  if (w < RULER_MINCOL) w = RULER_MINCOL;
  diff = w - t->grid[edge - 1];
  if (edge < t->ncols) {
    if (t->grid[edge] - diff < RULER_MINCOL) diff = t->grid[edge] - RULER_MINCOL;
    nt.grid[edge] = t->grid[edge] - diff;
  }
  if (!diff) return 0;
  nt.grid[edge - 1] = t->grid[edge - 1] + diff;
  return tbl_set_table(d, caret, &nt);
}

int ruler_apply(Doc *d, u32 pa, u32 pb, Pos *caret, const RulerGeom *g, int part)
{
  ParaFmt pf;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pb >= d->np) return 0;
  if (part >= RP_COL) return col_edge(d, caret, g, part - RP_COL);
  memset(&pf, 0, sizeof pf);
  if (part >= RP_TAB) {
    /* the stops in order, those dragged off left out */
    int i, j, n = 0;
    for (i = 0; i < RULER_TABS; i++)
      if (g->tabs[i] > 0) {
        for (j = n; j > 0 && pf.tabs[j - 1] > g->tabs[i]; j--) {
          pf.tabs[j] = pf.tabs[j - 1]; pf.tabkind[j] = pf.tabkind[j - 1]; pf.tablead[j] = pf.tablead[j - 1];
        }
        pf.tabs[j] = g->tabs[i]; pf.tabkind[j] = g->tabkind[i]; pf.tablead[j] = g->tablead[i];
        n++;
      }
    if (pa == pb && !memcmp(pf.tabs, d->p[pa]->pf.tabs, sizeof pf.tabs) && !memcmp(pf.tabkind, d->p[pa]->pf.tabkind, sizeof pf.tabkind))
      return 0;
    return doc_para_format(d, pa, pb, PM_TABS, &pf);
  }
  if (part == RP_FIRST || part == RP_LEFT || part == RP_RIGHT) {
    const ParaFmt *now = &d->p[pa]->pf;
    if (pa == pb && now->left == g->left && now->first == g->first && now->right == g->right) return 0;
    pf.left = g->left; pf.first = g->first; pf.right = g->right;
    return doc_para_format(d, pa, pb, PM_INDENT, &pf);
  }
  return 0;
}
