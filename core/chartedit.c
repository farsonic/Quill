/*
 * chartedit.c - see chartedit.h: charts made and changed (WP15b). The chart part writer is adapted
 * from Ledger's xlsx_draw.c (same author; written from ECMA-376's DrawingML charts), here with the
 * values cached in the part and kept in an embedded workbook laid out as the data grid, so Word's
 * Edit Data finds them where the part's formulas say.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "chartedit.h"
#include "zip.h"

#define CT_CHART "application/vnd.openxmlformats-officedocument.drawingml.chart+xml"
#define CT_CHARTEX "application/vnd.ms-office.chartex+xml"
#define RT_CHART "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"
#define RT_CHARTEX "http://schemas.microsoft.com/office/2014/relationships/chartEx"
#define RT_PACKAGE "http://schemas.openxmlformats.org/officeDocument/2006/relationships/package"
#define CT_XLSX "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"

static void xesc(Buf *o, const char *s) { xml_escape(o, s ? s : "", s ? strlen(s) : 0); }
static char *sdup(const char *s) { return str_dup(s ? s : ""); }

/* ------------------------------------------------------------------ the grid */
CGrid *cgrid_new(int rows, int cols)
{
  CGrid *g;
  if (rows < 1 || cols < 1 || rows > 1000 || cols > 256 || !(g = (CGrid *)calloc(1, sizeof *g))) return NULL;
  if (!(g->cell = (char **)calloc((size_t)rows * (size_t)cols, sizeof(char *)))) { free(g); return NULL; }
  g->rows = rows; g->cols = cols;
  return g;
}
void cgrid_free(CGrid *g)
{
  int i;
  if (!g) return;
  for (i = 0; i < g->rows * g->cols; i++) free(g->cell[i]);
  free(g->cell);
  free(g);
}
const char *cgrid_get(const CGrid *g, int r, int c)
{
  const char *s;
  if (!g || r < 0 || c < 0 || r >= g->rows || c >= g->cols) return "";
  s = g->cell[r * g->cols + c];
  return s ? s : "";
}
int cgrid_set(CGrid *g, int r, int c, const char *s)
{
  char *t = NULL;
  if (!g || r < 0 || c < 0 || r >= g->rows || c >= g->cols) return 0;
  if (s && s[0] && !(t = str_dup(s))) return 0;
  free(g->cell[r * g->cols + c]);
  g->cell[r * g->cols + c] = t;
  return 1;
}
int cgrid_resize(CGrid *g, int rows, int cols)
{
  char **n;
  int r, c;
  if (!g || rows < 1 || cols < 1 || rows > 1000 || cols > 256) return 0;
  if (!(n = (char **)calloc((size_t)rows * (size_t)cols, sizeof(char *)))) return 0;
  for (r = 0; r < g->rows; r++)
    for (c = 0; c < g->cols; c++) {
      if (r < rows && c < cols) n[r * cols + c] = g->cell[r * g->cols + c];
      else free(g->cell[r * g->cols + c]);
    }
  free(g->cell);
  g->cell = n; g->rows = rows; g->cols = cols;
  return 1;
}

/* a typed number: digits with a decimal point (or comma), a sign, an exponent; thousands commas
   before a point are skipped. CV_NONE if it isn't one */
double chart_atof(const char *s)
{
  double v = 0, f = 0.1, m = 1;
  int neg = 0, any = 0, e = 0, eneg = 0, pct = 0, dot = strchr(s ? s : "", '.') != NULL;
  if (!s) return CV_NONE;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
  if (*s == '$') s++;
  for (; (*s >= '0' && *s <= '9') || (*s == ',' && dot); s++) if (*s != ',') { v = v * 10 + (*s - '0'); any = 1; }
  if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9') { v += (*s++ - '0') * f; f /= 10; any = 1; } }
  if (!any) return CV_NONE;
  if (*s == 'e' || *s == 'E') {
    s++;
    if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { if (e < 400) e = e * 10 + (*s - '0'); s++; }
    if (e > 300) e = 300;
    while (e--) m *= 10;
    v = eneg ? v / m : v * m;
  }
  while (*s == ' ') s++;
  if (*s == '%') { pct = 1; s++; }
  while (*s == ' ') s++;
  if (*s) return CV_NONE;
  if (pct) v /= 100;
  return neg ? -v : v;
}
static int is_xy(int k) { return k == CK_SCATTER || k == CK_SCATTERLINE || k == CK_BUBBLE; }
static void num_text(double v, char *out, size_t cap)
{
  if (CV_IS_NONE(v)) { out[0] = 0; return; }
  chart_fmt_value(v, "General", out, cap);
}
CGrid *chart_to_grid(const Chart *c)
{
  int k, i, n = 0, bubble = c->kind == CK_BUBBLE, xy = is_xy(c->kind), w = bubble ? 2 : 1;
  CGrid *g;
  char t[64];
  const CSeries *cs = NULL;
  for (k = 0; k < c->ns; k++) {
    if (c->s[k].nv > n) n = c->s[k].nv;
    if (c->s[k].ncat > n) n = c->s[k].ncat;
    if (xy && c->s[k].nx > n) n = c->s[k].nx;
  }
  if (n > 999) n = 999;
  if (!(g = cgrid_new(n + 1, 1 + w * (c->ns ? c->ns : 1)))) return NULL;
  for (k = 0; k < c->ns && !cs; k++) if ((xy && c->s[k].x) || (!xy && c->s[k].ncat)) cs = &c->s[k];
  if (xy) cgrid_set(g, 0, 0, "X");
  for (i = 0; i < n; i++) {
    if (xy) {
      if (cs && i < cs->nx) num_text(cs->x[i], t, sizeof t); else snprintf(t, sizeof t, "%d", i + 1);
      cgrid_set(g, i + 1, 0, t);
    } else if (cs && i < cs->ncat && cs->cat[i]) cgrid_set(g, i + 1, 0, cs->cat[i]);
  }
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    if (s->name) cgrid_set(g, 0, 1 + w * k, s->name);
    else { snprintf(t, sizeof t, "Series %d", k + 1); cgrid_set(g, 0, 1 + w * k, t); }
    if (bubble) cgrid_set(g, 0, 2 + w * k, "Size");
    for (i = 0; i < n; i++) {
      if (i < s->nv) { num_text(s->v[i], t, sizeof t); cgrid_set(g, i + 1, 1 + w * k, t); }
      if (bubble && i < s->nsize) { num_text(s->size[i], t, sizeof t); cgrid_set(g, i + 1, 2 + w * k, t); }
    }
  }
  return g;
}
/* rows of the grid in use: up to the last with anything in it */
static int grid_rows(const CGrid *g)
{
  int r, c;
  for (r = g->rows - 1; r >= 1; r--) for (c = 0; c < g->cols; c++) if (cgrid_get(g, r, c)[0]) return r + 1;
  return 1;
}
static int grid_cols(const CGrid *g)
{
  int r, c;
  for (c = g->cols - 1; c >= 1; c--) for (r = 0; r < g->rows; r++) if (cgrid_get(g, r, c)[0]) return c + 1;
  return 1;
}
static void series_clear(CSeries *s)
{
  int i;
  free(s->name);
  for (i = 0; i < s->ncat; i++) free(s->cat[i]);
  free(s->cat); free(s->x); free(s->v); free(s->size);
  memset(s, 0, sizeof *s);
}
int chart_from_grid(Chart *c, const CGrid *g)
{
  int rows = grid_rows(g), cols = grid_cols(g), n = rows - 1, bubble = c->kind == CK_BUBBLE, xy = is_xy(c->kind), w = bubble ? 2 : 1;
  int ns = (cols - 1 + w - 1) / w, k, i, bad = 0;
  CSeries *old = c->s;
  int oldn = c->ns;
  if (ns < 1) ns = 1;
  if (!(c->s = (CSeries *)calloc((size_t)ns, sizeof(CSeries)))) { c->s = old; return 0; }
  c->ns = ns;
  for (k = 0; k < ns; k++) {
    CSeries *s = &c->s[k];
    const char *nm = cgrid_get(g, 0, 1 + w * k);
    s->kind = CK_SAME;
    if (k < oldn) { s->color = old[k].color; s->line = old[k].line; s->kind = old[k].kind; s->secondary = old[k].secondary; s->smooth = old[k].smooth; s->nomarker = old[k].nomarker; }
    if (nm[0] && !(s->name = str_dup(nm))) bad = 1;
    if (n > 0) {
      s->v = (double *)malloc((size_t)n * sizeof(double));
      if (!s->v) { bad = 1; continue; }
      s->nv = n;
      if (xy) { if ((s->x = (double *)malloc((size_t)n * sizeof(double)))) s->nx = n; else bad = 1; }
      else if ((s->cat = (char **)calloc((size_t)n, sizeof(char *)))) s->ncat = n; else bad = 1;
      if (bubble) { if ((s->size = (double *)malloc((size_t)n * sizeof(double)))) s->nsize = n; else bad = 1; }
      for (i = 0; i < n && !bad; i++) {
        s->v[i] = chart_atof(cgrid_get(g, i + 1, 1 + w * k));
        if (xy) { s->x[i] = chart_atof(cgrid_get(g, i + 1, 0)); if (CV_IS_NONE(s->x[i])) s->x[i] = i + 1; }
        else if (!(s->cat[i] = sdup(cgrid_get(g, i + 1, 0)))) bad = 1;
        if (bubble) { s->size[i] = chart_atof(cgrid_get(g, i + 1, 2 + w * k)); if (CV_IS_NONE(s->size[i])) s->size[i] = 1; }
      }
    }
  }
  for (k = 0; k < oldn; k++) series_clear(&old[k]);
  free(old);
  return !bad;
}

/* ------------------------------------------------------------------ making and changing */
int chart_is_ex(int k) { return k == CK_HISTOGRAM || k == CK_WATERFALL || k == CK_FUNNEL || k == CK_TREEMAP || k == CK_SUNBURST || k == CK_BOXWHISKER; }
static int pie1(int k) { return k == CK_PIE || k == CK_DOUGHNUT || k == CK_FUNNEL || k == CK_TREEMAP || k == CK_SUNBURST || k == CK_WATERFALL || k == CK_HISTOGRAM; }
Chart *chart_sample(int kind)
{
  /* Word's sample sheet; the kinds that want other data get theirs */
  static const char *const g3[5][4] = { { "", "Series 1", "Series 2", "Series 3" }, { "Category 1", "4.3", "2.4", "2" },
    { "Category 2", "2.5", "4.4", "2" }, { "Category 3", "3.5", "1.8", "3" }, { "Category 4", "4.5", "2.8", "5" } };
  static const char *const gpie[5][2] = { { "", "Sales" }, { "1st Qtr", "8.2" }, { "2nd Qtr", "3.2" }, { "3rd Qtr", "1.4" }, { "4th Qtr", "1.2" } };
  static const char *const gxy[4][4] = { { "X", "Y values", "", "" }, { "0.7", "2.7", "", "" }, { "1.8", "3.2", "", "" }, { "2.6", "0.8", "", "" } };
  static const char *const gbub[4][3] = { { "X", "Y values", "Size" }, { "0.7", "2.7", "10" }, { "1.8", "3.2", "4" }, { "2.6", "0.8", "8" } };
  static const char *const gstk[6][4] = { { "", "High", "Low", "Close" }, { "01/05/2002", "55", "11", "25" }, { "01/06/2002", "57", "12", "38" },
    { "01/07/2002", "57", "13", "50" }, { "01/08/2002", "58", "11", "35" }, { "01/09/2002", "58", "25", "43" } };
  static const char *const ghist[9] = { "Values", "12", "15", "18", "22", "25", "27", "31", "35" };
  static const char *const gwf[6][2] = { { "", "Amount" }, { "Start", "100" }, { "Sales", "40" }, { "Costs", "-30" }, { "Tax", "-10" }, { "Other", "20" } };
  Chart *c = chart_new(kind);
  CGrid *g = NULL;
  int r, k;
  if (!c) return NULL;
  if (kind == CK_PIE || kind == CK_DOUGHNUT || kind == CK_FUNNEL || kind == CK_TREEMAP || kind == CK_SUNBURST) {
    if ((g = cgrid_new(5, 2))) for (r = 0; r < 5; r++) for (k = 0; k < 2; k++) cgrid_set(g, r, k, gpie[r][k]);
    c->has_title = 1; c->legend = kind == CK_FUNNEL ? LG_NONE : LG_BOTTOM; c->hgrid = 0;
  } else if (kind == CK_SCATTER || kind == CK_SCATTERLINE) {
    if ((g = cgrid_new(4, 2))) for (r = 0; r < 4; r++) for (k = 0; k < 2; k++) cgrid_set(g, r, k, gxy[r][k]);
    c->legend = LG_NONE; c->vgrid = 0;
  } else if (kind == CK_BUBBLE) {
    if ((g = cgrid_new(4, 3))) for (r = 0; r < 4; r++) for (k = 0; k < 3; k++) cgrid_set(g, r, k, gbub[r][k]);
    c->legend = LG_NONE;
  } else if (kind == CK_STOCK) {
    if ((g = cgrid_new(6, 4))) for (r = 0; r < 6; r++) for (k = 0; k < 4; k++) cgrid_set(g, r, k, gstk[r][k]);
    c->legend = LG_BOTTOM;
  } else if (kind == CK_HISTOGRAM || kind == CK_BOXWHISKER) {
    if ((g = cgrid_new(9, 2))) for (r = 0; r < 9; r++) { char t[16]; snprintf(t, sizeof t, "%d", r); cgrid_set(g, r, 0, r ? t : ""); cgrid_set(g, r, 1, ghist[r]); }
    c->legend = LG_NONE;
  } else if (kind == CK_WATERFALL) {
    if ((g = cgrid_new(6, 2))) for (r = 0; r < 6; r++) for (k = 0; k < 2; k++) cgrid_set(g, r, k, gwf[r][k]);
    c->legend = LG_NONE;
  } else {
    if ((g = cgrid_new(5, 4))) for (r = 0; r < 5; r++) for (k = 0; k < 4; k++) cgrid_set(g, r, k, g3[r][k]);
    if (kind == CK_COMBO) c->legend = LG_BOTTOM;
  }
  if (!g || !chart_from_grid(c, g)) { cgrid_free(g); chart_free(c); return NULL; }
  cgrid_free(g);
  if (kind == CK_COMBO && c->ns >= 3) { c->s[0].kind = CK_COL; c->s[1].kind = CK_COL; c->s[2].kind = CK_LINE; c->s[2].secondary = 1; }
  if (kind == CK_RADAR) c->hgrid = 1;
  return c;
}
void chart_set_kind(Chart *c, int kind)
{
  int k, was = c->kind;
  if (kind < 0 || kind >= CK_N || kind == CK_OTHER) return;
  if (is_xy(kind) != is_xy(was) || (kind == CK_BUBBLE) != (was == CK_BUBBLE)) {
    /* the data reshaped through the grid: scatter's X values from the categories (1, 2, 3 ... when
       they aren't numbers) and back */
    CGrid *g = chart_to_grid(c);
    if (g) {
      int r, bub = kind == CK_BUBBLE, wasbub = was == CK_BUBBLE;
      CGrid *h = NULL;
      if (bub && !wasbub && (h = cgrid_new(g->rows, 1 + 2 * (g->cols - 1)))) {
        for (r = 0; r < g->rows; r++) {
          cgrid_set(h, r, 0, cgrid_get(g, r, 0));
          for (k = 1; k < g->cols; k++) { cgrid_set(h, r, 2 * k - 1, cgrid_get(g, r, k)); cgrid_set(h, r, 2 * k, r ? "1" : "Size"); }
        }
      } else if (!bub && wasbub && (h = cgrid_new(g->rows, 1 + (g->cols - 1) / 2))) {
        for (r = 0; r < g->rows; r++) { cgrid_set(h, r, 0, cgrid_get(g, r, 0)); for (k = 1; k < h->cols; k++) cgrid_set(h, r, k, cgrid_get(g, r, 2 * k - 1)); }
      }
      if (h) { cgrid_free(g); g = h; }
      if (is_xy(kind)) for (r = 1; r < g->rows; r++) if (CV_IS_NONE(chart_atof(cgrid_get(g, r, 0)))) { char t[16]; snprintf(t, sizeof t, "%d", r); cgrid_set(g, r, 0, t); }
      c->kind = (u8)kind;
      chart_from_grid(c, g);
      cgrid_free(g);
    }
  }
  c->kind = (u8)kind;
  if (pie1(kind)) while (c->ns > 1) series_clear(&c->s[--c->ns]);
  if (kind != CK_COMBO) for (k = 0; k < c->ns; k++) { c->s[k].kind = CK_SAME; c->s[k].secondary = 0; }
  else if (was != CK_COMBO) for (k = 0; k < c->ns; k++) { c->s[k].kind = (u8)(k == c->ns - 1 && c->ns > 1 ? CK_LINE : CK_COL); c->s[k].secondary = 0; }
  if (kind == CK_PIE || kind == CK_DOUGHNUT) for (k = 0; k < c->ns; k++) c->s[k].color = c->s[k].line = 0;
}
int chart_switch_rowcol(Chart *c)
{
  CGrid *g, *t;
  int r, k, ok;
  if (is_xy(c->kind) || !(g = chart_to_grid(c))) return 0;
  if (!(t = cgrid_new(g->cols, g->rows))) { cgrid_free(g); return 0; }
  for (r = 0; r < g->rows; r++) for (k = 0; k < g->cols; k++) if (r || k) cgrid_set(t, k, r, cgrid_get(g, r, k));
  for (k = 0; k < c->ns; k++) c->s[k].color = c->s[k].line = 0;    /* (the colours belonged to the old series) */
  ok = chart_from_grid(c, t);
  cgrid_free(g); cgrid_free(t);
  return ok;
}
void chart_quick_layout(Chart *c, int n)
{
  /* title, legend, data labels, gridlines across and up, axis titles (Word's layouts as near as the model goes) */
  static const u8 L[CHART_QUICK_LAYOUTS][6] = {
    { 1, LG_RIGHT, 0, 1, 0, 0 }, { 1, LG_TOP, 1, 0, 0, 0 }, { 1, LG_BOTTOM, 0, 1, 0, 0 }, { 0, LG_BOTTOM, 1, 0, 0, 0 },
    { 1, LG_NONE, 0, 1, 0, 2 }, { 1, LG_NONE, 0, 1, 0, 3 }, { 0, LG_RIGHT, 0, 1, 1, 3 }, { 1, LG_NONE, 0, 0, 0, 3 },
    { 1, LG_RIGHT, 0, 1, 0, 3 }, { 1, LG_RIGHT, 1, 1, 0, 0 } };
  const u8 *l;
  if (n < 0 || n >= CHART_QUICK_LAYOUTS) return;
  l = L[n];
  c->has_title = l[0]; c->legend = l[1]; c->labels = l[2]; c->hgrid = l[3]; c->vgrid = l[4];
  free(c->xtitle); free(c->ytitle); c->xtitle = c->ytitle = NULL;
  if (chart_is_cartesian(c->kind)) {
    if (l[5] & 1) c->xtitle = str_dup("Axis Title");
    if (l[5] & 2) c->ytitle = str_dup("Axis Title");
  }
}
const char *const chart_element_names[CEL_N] = { "Axis Titles: Primary Horizontal", "Axis Titles: Primary Vertical", "Chart Title: None", "Chart Title: Above Chart",
  "Data Labels: None", "Data Labels: Show", "Gridlines: Primary Major Horizontal", "Gridlines: Primary Major Vertical",
  "Legend: None", "Legend: Right", "Legend: Top", "Legend: Left", "Legend: Bottom" };
static const u8 lg_of[5] = { LG_NONE, LG_RIGHT, LG_TOP, LG_LEFT, LG_BOTTOM };
int chart_element_on(const Chart *c, int w)
{
  switch (w) {
  case CEL_XTITLE: return c->xtitle != NULL;
  case CEL_YTITLE: return c->ytitle != NULL;
  case CEL_TITLE_NONE: return !c->has_title;
  case CEL_TITLE_ABOVE: return c->has_title;
  case CEL_LABELS_NONE: return !c->labels;
  case CEL_LABELS_SHOW: return c->labels;
  case CEL_HGRID: return c->hgrid;
  case CEL_VGRID: return c->vgrid;
  default: return w >= CEL_LEGEND_NONE && w < CEL_N && c->legend == lg_of[w - CEL_LEGEND_NONE];
  }
}
void chart_add_element(Chart *c, int w)
{
  switch (w) {
  case CEL_XTITLE: if (c->xtitle) { free(c->xtitle); c->xtitle = NULL; } else c->xtitle = str_dup("Axis Title"); break;
  case CEL_YTITLE: if (c->ytitle) { free(c->ytitle); c->ytitle = NULL; } else c->ytitle = str_dup("Axis Title"); break;
  case CEL_TITLE_NONE: c->has_title = 0; break;
  case CEL_TITLE_ABOVE: c->has_title = 1; break;
  case CEL_LABELS_NONE: c->labels = 0; break;
  case CEL_LABELS_SHOW: c->labels = 1; break;
  case CEL_HGRID: c->hgrid = (u8)!c->hgrid; break;
  case CEL_VGRID: c->vgrid = (u8)!c->vgrid; break;
  default: if (w >= CEL_LEGEND_NONE && w < CEL_N) c->legend = lg_of[w - CEL_LEGEND_NONE]; break;
  }
}

/* ------------------------------------------------------------------ the chart part (after Ledger's xlsx_draw.c) */
static void solid(Buf *o, u32 rgb) { buf_printf(o, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", (unsigned long)(rgb & 0xFFFFFF)); }
/* <c:spPr> with a fill and an outline (0: none written, so the default) */
static void sppr(Buf *o, const char *ns, u32 fill, u32 line, int lw)
{
  if (!fill && !line) return;
  buf_printf(o, "<%s:spPr>", ns);
  if (fill) solid(o, fill);
  if (line == CE_NOLINE) buf_adds(o, "<a:ln><a:noFill/></a:ln>");
  else if (line) { buf_printf(o, "<a:ln w=\"%d\">", lw > 0 ? lw : 9525); solid(o, line); buf_adds(o, "</a:ln>"); }
  buf_printf(o, "</%s:spPr>", ns);
}
static void rich(Buf *o, const char *s, int size, int bold)
{
  buf_adds(o, "<c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr");
  if (size) buf_printf(o, " sz=\"%d\"", size);
  buf_printf(o, " b=\"%d\"/></a:pPr><a:r><a:rPr lang=\"en-GB\"", bold);
  if (size) buf_printf(o, " sz=\"%d\"", size);
  buf_printf(o, " b=\"%d\"/><a:t>", bold);
  xesc(o, s);
  buf_adds(o, "</a:t></a:r></a:p></c:rich></c:tx>");
}
/* the sheet's column letters for column c (0: A) */
static void col_name(int c, char *out)
{
  char t[8];
  int n = 0, i;
  c++;
  while (c > 0 && n < 7) { t[n++] = (char)('A' + (c - 1) % 26); c = (c - 1) / 26; }
  for (i = 0; i < n; i++) out[i] = t[n - 1 - i];
  out[n] = 0;
}
static void ref(Buf *o, int col, int r0, int r1)
{
  char cn[8];
  col_name(col, cn);
  if (r0 == r1) buf_printf(o, "Sheet1!$%s$%d", cn, r0 + 1);
  else buf_printf(o, "Sheet1!$%s$%d:$%s$%d", cn, r0 + 1, cn, r1 + 1);
}
static void num_pts(Buf *o, const double *v, int n, const char *fmt, const char *tag)
{
  int i;
  char t[64];
  buf_printf(o, "<c:%s><c:formatCode>", tag); xesc(o, fmt && fmt[0] ? fmt : "General");
  buf_printf(o, "</c:formatCode><c:ptCount val=\"%d\"/>", n);
  for (i = 0; i < n; i++) if (v && !CV_IS_NONE(v[i])) { num_text(v[i], t, sizeof t); buf_printf(o, "<c:pt idx=\"%d\"><c:v>%s</c:v></c:pt>", i, t); }
  buf_printf(o, "</c:%s>", tag);
}
static void str_pts(Buf *o, char **s, int n, const char *tag)
{
  int i;
  buf_printf(o, "<c:%s><c:ptCount val=\"%d\"/>", tag, n);
  for (i = 0; i < n; i++) { buf_printf(o, "<c:pt idx=\"%d\"><c:v>", i); xesc(o, s && s[i] ? s[i] : ""); buf_adds(o, "</c:v></c:pt>"); }
  buf_printf(o, "</c:%s>", tag);
}
/* a series' values: with the workbook, formulas into it and the values cached; without, literal */
static void w_nums(Buf *o, int embed, int col, const double *v, int n, const char *fmt)
{
  if (embed && n > 0) { buf_adds(o, "<c:numRef><c:f>"); ref(o, col, 1, n); buf_adds(o, "</c:f>"); num_pts(o, v, n, fmt, "numCache"); buf_adds(o, "</c:numRef>"); }
  else num_pts(o, v, n, fmt, "numLit");
}
static int family(int k)
{
  if (k == CK_COL || k == CK_COLSTACK || k == CK_COL100 || k == CK_BAR || k == CK_BARSTACK || k == CK_BAR100 || k == CK_HISTOGRAM || k == CK_WATERFALL) return 1;
  if (k == CK_LINE || k == CK_LINEMARK || k == CK_LINESTACK) return 2;
  if (k == CK_AREA || k == CK_AREASTACK || k == CK_AREA100) return 3;
  return 0;
}
static int sk_of(const Chart *c, int k)
{
  int s = c->s[k].kind;
  if (c->kind != CK_COMBO) return c->kind;
  if (s != CK_SAME && s < CK_N && s != CK_COMBO) return s;
  return k == 0 || c->ns == 1 ? CK_COL : CK_LINE;
}
static void dlbls(Buf *o, int on, int pct)
{
  buf_printf(o, "<c:dLbls><c:showLegendKey val=\"0\"/><c:showVal val=\"%d\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/><c:showPercent val=\"%d\"/><c:showBubbleSize val=\"0\"/></c:dLbls>", on && !pct, on && pct);
}
/* the categories of the chart: the first series' that has them */
static const CSeries *cat_series(const Chart *c)
{
  int k;
  for (k = 0; k < c->ns; k++) if (is_xy(c->kind) ? c->s[k].x != NULL : c->s[k].ncat > 0) return &c->s[k];
  return NULL;
}
/* one series of a chart group of kind kind; w: its grid columns (bubble 2) */
static void w_series(const Chart *c, int k, int kind, int embed, Buf *o)
{
  const CSeries *s = &c->s[k];
  const CSeries *cs = cat_series(c);
  int fam = family(kind), i, w = c->kind == CK_BUBBLE ? 2 : 1, col = 1 + w * k;
  u32 rgb = chart_series_rgb(c, k, c->ns);
  char nm[32];
  buf_printf(o, "<c:ser><c:idx val=\"%d\"/><c:order val=\"%d\"/>", k, k);
  snprintf(nm, sizeof nm, "Series %d", k + 1);
  if (embed) { buf_adds(o, "<c:tx><c:strRef><c:f>"); ref(o, col, 0, 0); buf_adds(o, "</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>"); xesc(o, s->name ? s->name : nm); buf_adds(o, "</c:v></c:pt></c:strCache></c:strRef></c:tx>"); }
  else { buf_adds(o, "<c:tx><c:v>"); xesc(o, s->name ? s->name : nm); buf_adds(o, "</c:v></c:tx>"); }
  /* its look, written out, so other programs show Quill's colours */
  if (fam == 2 || kind == CK_RADAR || kind == CK_SCATTERLINE || kind == CK_SCATTER) {
    buf_adds(o, "<c:spPr>");
    if (kind == CK_SCATTER) buf_adds(o, "<a:ln w=\"25400\" cap=\"rnd\"><a:noFill/><a:round/></a:ln>");
    else { buf_printf(o, "<a:ln w=\"%d\" cap=\"rnd\">", c->style == 3 ? 38100 : 28575); solid(o, s->line ? s->line : rgb); buf_adds(o, "<a:round/></a:ln>"); }
    buf_adds(o, "</c:spPr>");
    if (kind == CK_LINEMARK || kind == CK_SCATTER || kind == CK_SCATTERLINE || kind == CK_RADAR) {
      static const char *const sym[4] = { "circle", "square", "diamond", "triangle" };
      if (s->nomarker) buf_adds(o, "<c:marker><c:symbol val=\"none\"/></c:marker>");
      else { buf_printf(o, "<c:marker><c:symbol val=\"%s\"/><c:size val=\"5\"/><c:spPr>", sym[k % 4]); solid(o, rgb); buf_adds(o, "<a:ln w=\"9525\">"); solid(o, rgb); buf_adds(o, "</a:ln></c:spPr></c:marker>"); }
    } else buf_adds(o, "<c:marker><c:symbol val=\"none\"/></c:marker>");
  } else if (kind == CK_STOCK) {
    buf_adds(o, "<c:spPr><a:ln w=\"19050\"><a:noFill/></a:ln></c:spPr><c:marker><c:symbol val=\"none\"/></c:marker>");
  } else if (kind == CK_PIE || kind == CK_DOUGHNUT) {
    buf_adds(o, "<c:spPr><a:ln w=\"19050\">"); solid(o, 0xFFFFFF); buf_adds(o, "</a:ln></c:spPr>");
  } else {
    buf_adds(o, "<c:spPr>"); solid(o, rgb);
    if (s->line) { buf_adds(o, "<a:ln>"); solid(o, s->line); buf_adds(o, "</a:ln>"); }
    buf_adds(o, "</c:spPr>");
    if (fam == 1 || kind == CK_BUBBLE) buf_adds(o, "<c:invertIfNegative val=\"0\"/>");
  }
  if (kind == CK_PIE || kind == CK_DOUGHNUT) {
    /* each slice's colour */
    for (i = 0; i < s->nv; i++) {
      Chart t = *c;
      t.ns = 0;
      buf_printf(o, "<c:dPt><c:idx val=\"%d\"/><c:bubble3D val=\"0\"/><c:spPr>", i);
      solid(o, chart_series_rgb(&t, i, s->nv));
      buf_adds(o, "<a:ln w=\"19050\">"); solid(o, 0xFFFFFF); buf_adds(o, "</a:ln></c:spPr></c:dPt>");
    }
  }
  if (is_xy(kind)) {
    int n = s->nv;
    buf_adds(o, "<c:xVal>"); w_nums(o, embed, 0, s->x ? s->x : (cs ? cs->x : NULL), s->x ? s->nx : 0, "General"); buf_adds(o, "</c:xVal>");
    buf_adds(o, "<c:yVal>"); w_nums(o, embed, col, s->v, n, c->fmt); buf_adds(o, "</c:yVal>");
    if (kind == CK_BUBBLE) {
      buf_adds(o, "<c:bubbleSize>"); w_nums(o, embed, col + 1, s->size ? s->size : s->v, s->size ? s->nsize : n, "General"); buf_adds(o, "</c:bubbleSize>");
      buf_adds(o, "<c:bubble3D val=\"0\"/>");
    } else buf_printf(o, "<c:smooth val=\"%d\"/>", s->smooth);
  } else {
    char **cat = s->ncat ? s->cat : cs ? cs->cat : NULL;
    int ncat = s->ncat ? s->ncat : cs ? cs->ncat : 0;
    if (ncat) {
      buf_adds(o, "<c:cat>");
      if (embed) { buf_adds(o, "<c:strRef><c:f>"); ref(o, 0, 1, ncat); buf_adds(o, "</c:f>"); str_pts(o, cat, ncat, "strCache"); buf_adds(o, "</c:strRef>"); }
      else str_pts(o, cat, ncat, "strLit");
      buf_adds(o, "</c:cat>");
    }
    buf_adds(o, "<c:val>"); w_nums(o, embed, col, s->v, s->nv, c->fmt); buf_adds(o, "</c:val>");
    if (fam == 2) buf_printf(o, "<c:smooth val=\"%d\"/>", s->smooth);
  }
  buf_adds(o, "</c:ser>");
}
static void w_axis(Buf *o, int cat, int id, int cross, const char *pos, int grid, const char *title, const CAxis *ax, int del, int crossmax, const char *fmt)
{
  char t[64];
  buf_printf(o, "<c:%s><c:axId val=\"%d\"/><c:scaling><c:orientation val=\"minMax\"/>", cat ? "catAx" : "valAx", id);
  if (ax && ax->maxset) { num_text(ax->max, t, sizeof t); buf_printf(o, "<c:max val=\"%s\"/>", t); }
  if (ax && ax->minset) { num_text(ax->min, t, sizeof t); buf_printf(o, "<c:min val=\"%s\"/>", t); }
  buf_printf(o, "</c:scaling><c:delete val=\"%d\"/><c:axPos val=\"%s\"/>", del, pos);
  if (grid) buf_adds(o, "<c:majorGridlines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"D9D9D9\"/></a:solidFill></a:ln></c:spPr></c:majorGridlines>");
  if (title && *title) { buf_adds(o, "<c:title>"); rich(o, title, 1000, 1); buf_adds(o, "<c:overlay val=\"0\"/></c:title>"); }
  if (!cat && fmt && fmt[0] && strcmp(fmt, "General")) { buf_adds(o, "<c:numFmt formatCode=\""); xesc(o, fmt); buf_adds(o, "\" sourceLinked=\"0\"/>"); }
  else buf_adds(o, "<c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>");
  buf_adds(o, "<c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>");
  buf_adds(o, "<c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"BFBFBF\"/></a:solidFill></a:ln></c:spPr>");
  buf_printf(o, "<c:crossAx val=\"%d\"/><c:crosses val=\"%s\"/>", cross, crossmax ? "max" : "autoZero");
  if (cat) buf_adds(o, "<c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/>");
  else {
    buf_adds(o, "<c:crossBetween val=\"between\"/>");
    if (ax && ax->majset) { num_text(ax->major, t, sizeof t); buf_printf(o, "<c:majorUnit val=\"%s\"/>", t); }
  }
  buf_printf(o, "</c:%s>", cat ? "catAx" : "valAx");
}
static void w_chart(const Chart *c, int embed, Buf *o)
{
  int k, g, kind = c->kind, horiz = kind == CK_BAR || kind == CK_BARSTACK || kind == CK_BAR100, any2 = 0;
  static const char *const lgpos[5] = { "", "b", "t", "l", "r" };
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
              "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
              "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><c:date1904 val=\"0\"/><c:lang val=\"en-US\"/><c:roundedCorners val=\"0\"/>");
  buf_printf(o, "<c:style val=\"%d\"/>", c->style >= 1 && c->style <= 47 ? c->style + 1 : 2);
  buf_adds(o, "<c:chart>");
  if (c->has_title) {
    buf_adds(o, "<c:title>");
    if (c->title && *c->title) rich(o, c->title, 1400, 0);
    buf_adds(o, "<c:overlay val=\"0\"/>");
    sppr(o, "c", c->efill[CE_TITLE], c->eline[CE_TITLE], 0);
    buf_adds(o, "</c:title><c:autoTitleDeleted val=\"0\"/>");
  } else buf_adds(o, "<c:autoTitleDeleted val=\"1\"/>");
  buf_adds(o, "<c:plotArea><c:layout/>");
  /* one chart group a kind (combo charts: columns, lines, areas, on either axis) */
  for (g = 0; g < 6; g++) {
    int fam = g % 3 + 1, sec = g >= 3, gk = -1, first = 1;
    if (kind != CK_COMBO) { if (g) break; gk = kind; }
    else {
      for (k = 0; k < c->ns; k++) if (family(sk_of(c, k)) == fam && (c->s[k].secondary != 0) == sec) { gk = sk_of(c, k); break; }
      if (gk < 0) continue;
      if (sec) any2 = 1;
    }
    switch (gk) {
    case CK_COL: case CK_COLSTACK: case CK_COL100: case CK_BAR: case CK_BARSTACK: case CK_BAR100:
      buf_printf(o, "<c:barChart><c:barDir val=\"%s\"/><c:grouping val=\"%s\"/><c:varyColors val=\"0\"/>", horiz && kind != CK_COMBO ? "bar" : "col",
                 gk == CK_COLSTACK || gk == CK_BARSTACK ? "stacked" : gk == CK_COL100 || gk == CK_BAR100 ? "percentStacked" : "clustered");
      break;
    case CK_LINE: case CK_LINEMARK: case CK_LINESTACK:
      buf_printf(o, "<c:lineChart><c:grouping val=\"%s\"/><c:varyColors val=\"0\"/>", gk == CK_LINESTACK ? "stacked" : "standard"); break;
    case CK_STOCK: buf_adds(o, "<c:stockChart>"); break;
    case CK_AREA: case CK_AREASTACK: case CK_AREA100:
      buf_printf(o, "<c:areaChart><c:grouping val=\"%s\"/><c:varyColors val=\"0\"/>", gk == CK_AREA ? "standard" : gk == CK_AREASTACK ? "stacked" : "percentStacked"); break;
    case CK_PIE: buf_adds(o, "<c:pieChart><c:varyColors val=\"1\"/>"); break;
    case CK_DOUGHNUT: buf_adds(o, "<c:doughnutChart><c:varyColors val=\"1\"/>"); break;
    case CK_SCATTER: case CK_SCATTERLINE: buf_printf(o, "<c:scatterChart><c:scatterStyle val=\"%s\"/><c:varyColors val=\"0\"/>", gk == CK_SCATTER ? "lineMarker" : "lineMarker"); break;
    case CK_BUBBLE: buf_adds(o, "<c:bubbleChart><c:varyColors val=\"0\"/>"); break;
    case CK_RADAR: buf_adds(o, "<c:radarChart><c:radarStyle val=\"marker\"/><c:varyColors val=\"0\"/>"); break;
    default: buf_adds(o, "<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>"); gk = CK_COL; break;
    }
    for (k = 0; k < c->ns; k++) {
      int s_k = kind == CK_COMBO ? sk_of(c, k) : gk;
      if (kind == CK_COMBO && (family(s_k) != fam || (c->s[k].secondary != 0) != sec)) continue;
      if ((gk == CK_PIE || gk == CK_DOUGHNUT) && !first) break;    /* one series in a pie */
      w_series(c, k, s_k, embed, o);
      first = 0;
    }
    dlbls(o, c->labels, 0);
    switch (gk) {
    case CK_COL: case CK_COLSTACK: case CK_COL100: case CK_BAR: case CK_BARSTACK: case CK_BAR100:
      buf_printf(o, "<c:gapWidth val=\"%d\"/>%s<c:axId val=\"%d\"/><c:axId val=\"%d\"/></c:barChart>", c->style == 2 ? 50 : 150, gk == CK_COL || gk == CK_BAR ? "" : "<c:overlap val=\"100\"/>",
                 sec ? 500003 : 500001, sec ? 500004 : 500002); break;
    case CK_LINE: case CK_LINEMARK: case CK_LINESTACK:
      buf_printf(o, "<c:marker val=\"1\"/><c:axId val=\"%d\"/><c:axId val=\"%d\"/></c:lineChart>", sec ? 500003 : 500001, sec ? 500004 : 500002); break;
    case CK_STOCK:
      buf_adds(o, "<c:hiLowLines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"404040\"/></a:solidFill></a:ln></c:spPr></c:hiLowLines>");
      if (c->ns >= 4) buf_adds(o, "<c:upDownBars><c:gapWidth val=\"150\"/><c:upBars><c:spPr><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></c:spPr></c:upBars><c:downBars><c:spPr><a:solidFill><a:srgbClr val=\"404040\"/></a:solidFill></c:spPr></c:downBars></c:upDownBars>");
      buf_adds(o, "<c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:stockChart>"); break;
    case CK_AREA: case CK_AREASTACK: case CK_AREA100:
      buf_printf(o, "<c:axId val=\"%d\"/><c:axId val=\"%d\"/></c:areaChart>", sec ? 500003 : 500001, sec ? 500004 : 500002); break;
    case CK_PIE: buf_adds(o, "<c:firstSliceAng val=\"0\"/></c:pieChart>"); break;
    case CK_DOUGHNUT: buf_printf(o, "<c:firstSliceAng val=\"0\"/><c:holeSize val=\"%d\"/></c:doughnutChart>", c->hole >= 10 && c->hole <= 90 ? c->hole : 50); break;
    case CK_SCATTER: case CK_SCATTERLINE: buf_adds(o, "<c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:scatterChart>"); break;
    case CK_BUBBLE: buf_adds(o, "<c:bubbleScale val=\"100\"/><c:showNegBubbles val=\"0\"/><c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:bubbleChart>"); break;
    case CK_RADAR: buf_adds(o, "<c:axId val=\"500001\"/><c:axId val=\"500002\"/></c:radarChart>"); break;
    }
  }
  /* the axes */
  if (is_xy(kind)) {
    w_axis(o, 0, 500001, 500002, "b", c->vgrid, c->xtitle, &c->ha, 0, 0, NULL);
    w_axis(o, 0, 500002, 500001, "l", c->hgrid, c->ytitle, &c->va, 0, 0, c->fmt);
  } else if (kind != CK_PIE && kind != CK_DOUGHNUT) {
    w_axis(o, 1, 500001, 500002, horiz ? "l" : "b", c->vgrid, horiz ? c->ytitle : c->xtitle, NULL, 0, 0, NULL);
    w_axis(o, 0, 500002, 500001, horiz ? "b" : "l", c->hgrid, horiz ? c->xtitle : c->ytitle, &c->va, 0, 0, c->fmt);
    if (any2) { w_axis(o, 1, 500003, 500004, "b", 0, NULL, NULL, 1, 0, NULL); w_axis(o, 0, 500004, 500003, "r", 0, NULL, NULL, 0, 1, NULL); }
  }
  sppr(o, "c", c->efill[CE_PLOT], c->eline[CE_PLOT], 0);
  buf_adds(o, "</c:plotArea>");
  if (c->legend != LG_NONE && c->legend <= LG_RIGHT) {
    buf_printf(o, "<c:legend><c:legendPos val=\"%s\"/><c:overlay val=\"0\"/>", lgpos[c->legend]);
    sppr(o, "c", c->efill[CE_LEGEND], c->eline[CE_LEGEND], 0);
    buf_adds(o, "</c:legend>");
  }
  buf_adds(o, "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>");
  sppr(o, "c", c->efill[CE_CHART] ? c->efill[CE_CHART] : (PRIM_ON | 0xFFFFFF), c->eline[CE_CHART] ? c->eline[CE_CHART] : (PRIM_ON | 0xD9D9D9), 9525);
  if (c->font[0]) {
    buf_adds(o, "<c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr><a:latin typeface=\"");
    xesc(o, c->font);
    buf_adds(o, "\"/></a:defRPr></a:pPr><a:endParaRPr lang=\"en-US\"/></a:p></c:txPr>");
  }
  if (embed) buf_adds(o, "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData>");
  buf_adds(o, "</c:chartSpace>");
}
/* Excel 2016's kinds: a chartex part (Ledger's w_chartex, the data literal with formulas into the workbook) */
static void w_chartex(const Chart *c, int embed, Buf *o)
{
  int k, i;
  static const char *const layout[] = { "clusteredColumn", "waterfall", "funnel", "treemap", "sunburst", "boxWhisker" };
  int li = c->kind == CK_HISTOGRAM ? 0 : c->kind == CK_WATERFALL ? 1 : c->kind == CK_FUNNEL ? 2 : c->kind == CK_TREEMAP ? 3 : c->kind == CK_SUNBURST ? 4 : 5;
  static const char *const lgpos[5] = { "", "b", "t", "l", "r" };
  const CSeries *cs = cat_series(c);
  char t[64];
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
              "<cx:chartSpace xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
              "xmlns:cx=\"http://schemas.microsoft.com/office/drawing/2014/chartex\"><cx:chartData>");
  if (embed) buf_adds(o, "<cx:externalData r:id=\"rId1\" cx:autoUpdate=\"0\"/>");
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    buf_printf(o, "<cx:data id=\"%d\">", k);
    if (cs && c->kind != CK_HISTOGRAM && c->kind != CK_BOXWHISKER) {
      buf_adds(o, "<cx:strDim type=\"cat\">");
      if (embed) { buf_adds(o, "<cx:f>"); ref(o, 0, 1, cs->ncat); buf_adds(o, "</cx:f>"); }
      buf_printf(o, "<cx:lvl ptCount=\"%d\">", cs->ncat);
      for (i = 0; i < cs->ncat; i++) { buf_printf(o, "<cx:pt idx=\"%d\">", i); xesc(o, cs->cat[i]); buf_adds(o, "</cx:pt>"); }
      buf_adds(o, "</cx:lvl></cx:strDim>");
    }
    buf_printf(o, "<cx:numDim type=\"%s\">", c->kind == CK_TREEMAP || c->kind == CK_SUNBURST ? "size" : "val");
    if (embed && s->nv) { buf_adds(o, "<cx:f>"); ref(o, 1 + k, 1, s->nv); buf_adds(o, "</cx:f>"); }
    buf_printf(o, "<cx:lvl ptCount=\"%d\" formatCode=\"General\">", s->nv);
    for (i = 0; i < s->nv; i++) if (!CV_IS_NONE(s->v[i])) { num_text(s->v[i], t, sizeof t); buf_printf(o, "<cx:pt idx=\"%d\">%s</cx:pt>", i, t); }
    buf_adds(o, "</cx:lvl></cx:numDim></cx:data>");
  }
  buf_adds(o, "</cx:chartData><cx:chart>");
  if (c->has_title) {
    const char *tt = c->title && *c->title ? c->title : (c->ns == 1 && c->s[0].name ? c->s[0].name : "Chart Title");
    buf_adds(o, "<cx:title pos=\"t\" align=\"ctr\" overlay=\"0\"><cx:tx><cx:txData><cx:v>"); xesc(o, tt); buf_adds(o, "</cx:v></cx:txData></cx:tx></cx:title>");
  }
  buf_adds(o, "<cx:plotArea><cx:plotAreaRegion>");
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    buf_printf(o, "<cx:series layoutId=\"%s\" uniqueId=\"{%08X-0000-4000-8000-%012X}\">", layout[li], 0x51550000u + (unsigned)k, (unsigned)k);
    buf_adds(o, "<cx:tx><cx:txData>");
    if (embed) { buf_adds(o, "<cx:f>"); ref(o, 1 + k, 0, 0); buf_adds(o, "</cx:f>"); }
    buf_adds(o, "<cx:v>"); xesc(o, s->name ? s->name : "Series"); buf_adds(o, "</cx:v></cx:txData></cx:tx>");
    if (s->color) { buf_adds(o, "<cx:spPr>"); solid(o, s->color); buf_adds(o, "</cx:spPr>"); }
    if (c->labels || c->kind == CK_WATERFALL || c->kind == CK_FUNNEL || c->kind == CK_TREEMAP || c->kind == CK_SUNBURST)
      buf_adds(o, "<cx:dataLabels pos=\"outEnd\"><cx:visibility seriesName=\"0\" categoryName=\"0\" value=\"1\"/></cx:dataLabels>");
    buf_printf(o, "<cx:dataId val=\"%d\"/>", k);
    if (c->kind == CK_HISTOGRAM) buf_adds(o, "<cx:layoutPr><cx:binning intervalClosed=\"r\"/></cx:layoutPr>");
    else if (c->kind == CK_BOXWHISKER) buf_adds(o, "<cx:layoutPr><cx:visibility meanLine=\"0\" meanMarker=\"1\" nonoutliers=\"0\" outliers=\"1\"/><cx:statistics quartileMethod=\"exclusive\"/></cx:layoutPr>");
    else if (c->kind == CK_TREEMAP) buf_adds(o, "<cx:layoutPr><cx:parentLabelLayout val=\"overlapping\"/></cx:layoutPr>");
    else if (c->kind == CK_WATERFALL) buf_adds(o, "<cx:layoutPr><cx:subtotals/></cx:layoutPr>");
    buf_adds(o, "</cx:series>");
    if (c->kind != CK_BOXWHISKER) break;          /* (one series, but box & whisker shows them all) */
  }
  buf_adds(o, "</cx:plotAreaRegion>");
  if (c->kind == CK_FUNNEL) buf_adds(o, "<cx:axis id=\"0\"><cx:catScaling gapWidth=\"0.06\"/><cx:tickLabels/></cx:axis>");
  else if (c->kind != CK_TREEMAP && c->kind != CK_SUNBURST) {
    buf_printf(o, "<cx:axis id=\"0\"><cx:catScaling gapWidth=\"%s\"/><cx:tickLabels/></cx:axis>", c->kind == CK_HISTOGRAM ? "0" : "0.5");
    buf_printf(o, "<cx:axis id=\"1\"><cx:valScaling/>%s<cx:tickLabels/></cx:axis>", c->hgrid ? "<cx:majorGridlines/>" : "");
  }
  buf_adds(o, "</cx:plotArea>");
  if (c->legend != LG_NONE && c->legend <= LG_RIGHT) buf_printf(o, "<cx:legend pos=\"%s\" align=\"ctr\" overlay=\"0\"/>", lgpos[c->legend]);
  buf_adds(o, "</cx:chart></cx:chartSpace>");
}
int chart_write_xml(const Chart *c, int embed, Buf *o)
{
  if (chart_is_ex(c->kind)) w_chartex(c, embed, o); else w_chart(c, embed, o);
  return !o->oom;
}

/* ------------------------------------------------------------------ the embedded workbook */
int chart_write_xlsx(const Chart *c, Buf *o)
{
  CGrid *g = chart_to_grid(c);
  ZipWriter z;
  Buf s;
  int r, k, ok;
  char cn[8];
  if (!g) return 0;
  buf_init(&s);
  buf_adds(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
  col_name(g->cols - 1, cn);
  buf_printf(&s, "<dimension ref=\"A1:%s%d\"/><sheetViews><sheetView tabSelected=\"1\" workbookViewId=\"0\"/></sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>"
                 "<cols><col min=\"1\" max=\"1\" width=\"14\" customWidth=\"1\"/></cols><sheetData>", cn, g->rows);
  for (r = 0; r < g->rows; r++) {
    buf_printf(&s, "<row r=\"%d\">", r + 1);
    for (k = 0; k < g->cols; k++) {
      const char *v = cgrid_get(g, r, k);
      double d;
      if (!v[0]) continue;
      col_name(k, cn);
      d = r && (k || is_xy(c->kind)) ? chart_atof(v) : CV_NONE;
      if (!CV_IS_NONE(d)) { char t[64]; num_text(d, t, sizeof t); buf_printf(&s, "<c r=\"%s%d\"><v>%s</v></c>", cn, r + 1, t); }
      else { buf_printf(&s, "<c r=\"%s%d\" t=\"inlineStr\"><is><t>", cn, r + 1); xesc(&s, v); buf_adds(&s, "</t></is></c>"); }
    }
    buf_adds(&s, "</row>");
  }
  buf_adds(&s, "</sheetData><pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/></worksheet>");
  cgrid_free(g);
  if (s.oom) { buf_free(&s); return 0; }
  zip_begin(&z);
  {
    static const char *const parts[4][2] = {
      { "[Content_Types].xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/></Types>" },
      { "_rels/.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>" },
      { "xl/workbook.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><bookViews><workbookView/></bookViews>"
        "<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>" },
      { "xl/_rels/workbook.xml.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>" } };
    static const char styles[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
      "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font></fonts>"
      "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
      "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
      "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
      "<cellXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
      "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";
    int i;
    ok = 1;
    for (i = 0; i < 4 && ok; i++) ok = zip_add(&z, parts[i][0], parts[i][1], strlen(parts[i][1]));
    if (ok) ok = zip_add(&z, "xl/styles.xml", styles, strlen(styles));
    if (ok) ok = zip_add(&z, "xl/worksheets/sheet1.xml", s.data, s.len);
  }
  buf_free(&s);
  if (ok) ok = zip_finish(&z);
  if (ok) buf_add(o, z.out.data, z.out.len);
  zip_free(&z);
  return ok && !o->oom;
}

/* ------------------------------------------------------------------ the pack */
int doc_add_graphic(Doc *d, ObjPack *pk, int kind, i32 cx, i32 cy)
{
  Image *ni, *im;
  if (!pk) return -1;
  if (d->nimages >= MAX_IMAGES || !(ni = (Image *)realloc(d->images, (size_t)(d->nimages + 1) * sizeof(Image)))) { pack_unref(pk); return -1; }
  d->images = ni;
  im = &d->images[d->nimages];
  img_defaults(im);
  im->kind = (u8)kind;
  im->pack = pk;
  im->cx = cx > 0 ? cx : 8640; im->cy = cy > 0 ? cy : 5040;
  im->pw = (u32)(im->cx / 15); im->ph = (u32)(im->cy / 15);
  im->dl = im->dr = 0;
  return d->nimages++;
}
/* the theme and font of the pack replaced */
static void keep_look(ObjPack *pk, const ObjPack *old)
{
  if (!old) return;
  snprintf(pk->font, sizeof pk->font, "%s", old->font);
  if (old->theme) { pk->theme = old->theme; pk->theme->refs++; }
}
#define RT_CSTYLE "http://schemas.microsoft.com/office/2011/relationships/chartStyle"
#define RT_CCOLOR "http://schemas.microsoft.com/office/2011/relationships/chartColorStyle"
ObjPack *chart_pack_make(const Chart *c, const ObjPack *old)
{
  ObjPack *pk;
  Buf x, w, rels;
  int ex = chart_is_ex(c->kind), ok, me;
  const char *name = ex ? "word/charts/chartEx1.xml" : "word/charts/chart1.xml";
  if (!(pk = pack_new(ex ? PK_CHARTEX : PK_CHART))) return NULL;
  keep_look(pk, old);
  buf_init(&x); buf_init(&w); buf_init(&rels);
  ok = chart_write_xml(c, 1, &x) && chart_write_xlsx(c, &w);
  buf_adds(&rels, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                  "<Relationship Id=\"rId1\" Type=\"" RT_PACKAGE "\" Target=\"../embeddings/Microsoft_Excel_Worksheet.xlsx\"/>");
  if (ok && old && !ex) {
    /* the chart style and colour parts of the chart replaced (Word's), so its gallery choices stay */
    int k, n = 2;
    for (k = 0; k < old->np; k++) {
      const PackPart *p = &old->p[k];
      const char *b;
      char nm[300];
      if (p->owner >= 0 || !p->ctype || (!strstr(p->ctype, "chartstyle+xml") && !strstr(p->ctype, "chartcolorstyle+xml"))) continue;
      b = strrchr(p->name, '/');
      snprintf(nm, sizeof nm, "word/charts/%s", b ? b + 1 : p->name);
      if (pack_add(pk, nm, p->ctype, NULL, NULL, RO_NONE, -1, p->data, p->len) < 0) continue;
      buf_printf(&rels, "<Relationship Id=\"rId%d\" Type=\"%s\" Target=\"", n++, strstr(p->ctype, "chartstyle+xml") ? RT_CSTYLE : RT_CCOLOR);
      xesc(&rels, b ? b + 1 : p->name);
      buf_adds(&rels, "\"/>");
    }
  }
  buf_adds(&rels, "</Relationships>");
  if (ok && !rels.oom) {
    me = pack_add(pk, name, ex ? CT_CHARTEX : CT_CHART, ex ? RT_CHARTEX : RT_CHART, NULL, RO_CHART, -1, x.data, x.len);
    ok = me >= 0 && pack_add(pk, ex ? "word/charts/_rels/chartEx1.xml.rels" : "word/charts/_rels/chart1.xml.rels", NULL, NULL, NULL, RO_NONE, me, rels.data, rels.len) >= 0 &&
         pack_add(pk, "word/embeddings/Microsoft_Excel_Worksheet.xlsx", CT_XLSX, NULL, NULL, RO_NONE, -1, w.data, w.len) >= 0;
  } else ok = 0;
  buf_free(&x); buf_free(&w); buf_free(&rels);
  if (ok && !(pk->chart = chart_copy(c))) ok = 0;
  if (!ok) { pack_unref(pk); return NULL; }
  return pk;
}
