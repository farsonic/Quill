/*
 * chart.c - see chart.h: the chart model (series with their cached values), reading a DrawingML
 * chart part (c:chartSpace) or an Excel 2016 one (cx:chartSpace), and the chart laid out as
 * drawing primitives for every kind: column, bar, line, area (clustered, stacked, 100%), pie,
 * doughnut, scatter, bubble, radar, stock, combo, and Excel 2016's histogram, box & whisker,
 * waterfall, funnel, treemap and sunburst.
 *
 * Adapted from Ledger's chart.c and xlsx_draw.c (same author; written from the documented behaviour
 * of Excel's charts, ECMA-376's DrawingML charts and GridCraft's chart crate). Quill has no maths
 * library and no FPU code: the few functions needed (floor, powers of ten, square root, sine and
 * cosine through util.c's integer tables) are done here with plain arithmetic.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "chart.h"
#include "xml.h"

#define FILL_ON PRIM_ON
int chart_nofp;
#define M_PI_ 3.14159265358979323846
#define MAXPTS 4000              /* points a series shows at most */
#define BIG 1.0e300                /* (only ever compared with positive numbers: see below) */

/* ------------------------------------------------------------------ arithmetic without libm */
/* AROS's mathieeedoubbas.library, which does vbcc's double arithmetic here, compares two negative
   numbers wrongly once they are large (-1e30 <= -1e29 comes out false; -1e10 <= -1e9 is right), so
   nothing below compares a negative number with a large negative one: signs, infinities and the
   empty point are told from the bits, and the smallest and largest values are gathered from the
   first one found (Rng) instead of from -BIG */
static u32 dhi(double v)
{
  static int hw = -1;
  union { double d; u32 w[2]; } u;
  if (hw < 0) { u.d = 1.0; hw = u.w[0] == 0x3FF00000UL ? 0 : 1; }
  u.d = v;
  return u.w[hw];
}
static int q_neg(double v) { return (dhi(v) & 0x80000000UL) != 0; }
static double q_abs(double v) { return q_neg(v) ? -v : v; }
static int q_finite(double v) { return (dhi(v) & 0x7FF00000UL) != 0x7FF00000UL && q_abs(v) < BIG; }
int chart_is_none(double v)
{
  static double none = CV_NONE;
  union { double d; u32 w[2]; } a, b;
  a.d = v; b.d = none;
  return a.w[0] == b.w[0] && a.w[1] == b.w[1];
}
/* the smallest and largest of the values added */
typedef struct { double lo, hi; int any; } Rng;
static void rng_add(Rng *r, double v)
{
  if (!r->any) { r->lo = r->hi = v; r->any = 1; return; }
  if (v < r->lo) r->lo = v;
  if (v > r->hi) r->hi = v;
}
/* floor, exact for every double (past 2^52 every double is a whole number; below that the value is
   split at 2^31 so each half fits a long, as vbcc's own floor doesn't manage) */
static double q_floor(double v)
{
  long l;
  if (!q_finite(v)) return v;
  if (q_abs(v) < 2.0e9) {
    l = (long)v;
    if ((double)l > v) l--;
    return (double)l;
  }
  if (q_abs(v) >= 4.5e15) return v;
  {
    double h = (double)(long)(v / 2147483648.0) * 2147483648.0, r = v - h;     /* (both exact) */
    l = (long)r;
    if ((double)l > r) l--;
    return h + (double)l;
  }
}
static double q_ceil(double v) { return -q_floor(-v); }
static double q_pow10(int e)
{
  double r = 1, b = 10;
  int n = e < 0 ? -e : e;
  while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
  return e < 0 ? 1 / r : r;
}
/* the power of ten at or below v (v > 0): 10^e with e returned through *e */
static double q_mag(double v, int *e)
{
  int k = 0;
  double m = 1;
  if (!(v > 0) || !q_finite(v)) { if (e) *e = 0; return 1; }
  while (m * 10 <= v && k < 300) { m *= 10; k++; }
  while (m > v && k > -300) { m /= 10; k--; }
  if (e) *e = k;
  return m;
}
static double q_sqrt(double v)
{
  double r;
  int i;
  if (!(v > 0)) return 0;
  r = v > 1 ? v / 2 : 1;
  for (i = 0; i < 60; i++) { double n = (r + v / r) / 2; if (q_abs(n - r) <= r * 1e-12) { r = n; break; } r = n; }
  return r;
}
/* sine and cosine of a (radians), through the tables in tenths of a degree */
static int tenths(double a)
{
  double t = a * 1800.0 / M_PI_;
  long l;
  t -= q_floor(t / 3600.0) * 3600.0;
  l = (long)(t + 0.5);
  return (int)(l % 3600);
}
static double q_sin(double a) { return (double)isin(tenths(a)) / TRIG_ONE; }
static double q_cos(double a) { return (double)icos(tenths(a)) / TRIG_ONE; }
/* a decimal number (as in the parts' <c:v>); CV_NONE if there is none */
static double q_atof(const char *s)
{
  double v = 0, f = 0.1;
  int neg = 0, any = 0, e = 0, eneg = 0;
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
  while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; }
  if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { v += (*s++ - '0') * f; f /= 10; any = 1; } }
  if (!any) return CV_NONE;
  if (*s == 'e' || *s == 'E') {
    s++;
    if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { if (e < 400) e = e * 10 + (*s - '0'); s++; }
    if (e > 300) e = 300;
    v *= q_pow10(eneg ? -e : e);
  }
  return neg ? -v : v;
}

/* ------------------------------------------------------------------ primitives */
void prims_init(PrimList *l) { memset(l, 0, sizeof *l); }
void prims_free(PrimList *l)
{
  int i;
  for (i = 0; i < l->n; i++) { free(l->p[i].pts); free(l->p[i].text); }
  free(l->p);
  memset(l, 0, sizeof *l);
}
Prim *prim_add(PrimList *l, int t, u16 elem)
{
  Prim *p;
  if (l->n == l->cap) {
    int cap = l->cap ? l->cap * 2 : 64;
    Prim *np;
    if (l->n > 200000 || !(np = (Prim *)realloc(l->p, (size_t)cap * sizeof(Prim)))) { l->oom = 1; return NULL; }
    l->p = np; l->cap = cap;
  }
  p = &l->p[l->n++];
  memset(p, 0, sizeof *p);
  p->t = (u8)t; p->elem = elem;
  return p;
}

/* ------------------------------------------------------------------ kinds */
static const char *const knames[CK_N] = {
  "Clustered Column", "Stacked Column", "100% Stacked Column", "Clustered Bar", "Stacked Bar", "100% Stacked Bar",
  "Line", "Line with Markers", "Stacked Line", "Pie", "Doughnut", "Area", "Stacked Area", "100% Stacked Area",
  "Scatter", "Scatter with Lines", "Bubble", "Radar", "Histogram", "Waterfall", "Funnel", "Treemap", "Sunburst",
  "Box & Whisker", "Stock", "Combo", "Chart"
};
const char *chart_kind_name(int k) { return k >= 0 && k < CK_N ? knames[k] : "Chart"; }
int chart_kind_by_name(const char *name)
{
  int k;
  for (k = 0; k < CK_N; k++) if (str_ieq(knames[k], name)) return k;
  return -1;
}
int chart_is_pielike(int k) { return k == CK_PIE || k == CK_DOUGHNUT || k == CK_FUNNEL || k == CK_TREEMAP || k == CK_SUNBURST; }
int chart_is_cartesian(int k) { return !chart_is_pielike(k) && k != CK_RADAR && k != CK_OTHER; }
static int is_bar(int k) { return k == CK_BAR || k == CK_BARSTACK || k == CK_BAR100; }
static int is_col(int k) { return k == CK_COL || k == CK_COLSTACK || k == CK_COL100; }
static int is_line(int k) { return k == CK_LINE || k == CK_LINEMARK || k == CK_LINESTACK; }
static int is_area(int k) { return k == CK_AREA || k == CK_AREASTACK || k == CK_AREA100; }
static int is_xy(int k) { return k == CK_SCATTER || k == CK_SCATTERLINE || k == CK_BUBBLE; }

/* ------------------------------------------------------------------ the model */
Chart *chart_new(int kind)
{
  Chart *c = (Chart *)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->kind = (u8)(kind >= 0 && kind < CK_N ? kind : CK_COL);
  c->has_title = 1;
  c->legend = LG_BOTTOM;
  c->hgrid = 1;
  c->style = 1;
  strcpy(c->fmt, "General");
  return c;
}
static void strs_free(char **s, int n) { int i; for (i = 0; i < n; i++) free(s[i]); free(s); }
static void series_clear(CSeries *s)
{
  free(s->name); strs_free(s->cat, s->ncat); free(s->x); free(s->v); free(s->size);
  memset(s, 0, sizeof *s);
}
void chart_free(Chart *c)
{
  int i;
  if (!c) return;
  for (i = 0; i < c->ns; i++) series_clear(&c->s[i]);
  free(c->s); free(c->title); free(c->xtitle); free(c->ytitle);
  free(c);
}
static char *sdup(const char *s, int *bad) { char *r; if (!s) return NULL; if (!(r = str_dup(s))) *bad = 1; return r; }
static double *ddup(const double *v, int n, int *bad)
{
  double *r;
  if (!v || n <= 0) return NULL;
  if (!(r = (double *)malloc((size_t)n * sizeof(double)))) { *bad = 1; return NULL; }
  memcpy(r, v, (size_t)n * sizeof(double));
  return r;
}
Chart *chart_copy(const Chart *c)
{
  Chart *n = (Chart *)malloc(sizeof *n);
  int i, j, bad = 0;
  if (!n) return NULL;
  *n = *c;
  n->title = sdup(c->title, &bad); n->xtitle = sdup(c->xtitle, &bad); n->ytitle = sdup(c->ytitle, &bad);
  n->s = NULL; n->ns = 0;
  if (c->ns && !(n->s = (CSeries *)calloc((size_t)c->ns, sizeof(CSeries)))) bad = 1;
  for (i = 0; i < c->ns && !bad; i++) {
    CSeries *d = &n->s[i];
    const CSeries *s = &c->s[i];
    *d = *s;
    d->name = sdup(s->name, &bad);
    d->cat = NULL; d->ncat = 0;
    if (s->ncat && (d->cat = (char **)calloc((size_t)s->ncat, sizeof(char *)))) {
      d->ncat = s->ncat;
      for (j = 0; j < s->ncat; j++) d->cat[j] = sdup(s->cat[j], &bad);
    } else if (s->ncat) bad = 1;
    d->x = ddup(s->x, s->nx, &bad); d->v = ddup(s->v, s->nv, &bad); d->size = ddup(s->size, s->nsize, &bad);
    n->ns = i + 1;
  }
  if (bad) { chart_free(n); return NULL; }
  return n;
}
CSeries *chart_add_series(Chart *c)
{
  CSeries *ns;
  if (c->ns >= 255 || !(ns = (CSeries *)realloc(c->s, (size_t)(c->ns + 1) * sizeof(CSeries)))) return NULL;
  c->s = ns;
  memset(&c->s[c->ns], 0, sizeof(CSeries));
  c->s[c->ns].kind = CK_SAME;
  return &c->s[c->ns++];
}

/* ------------------------------------------------------------------ colours */
static const u32 accents0[6] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
/* rgb mixed towards white (t > 0) or black (t < 0) by |t| (0..1) */
static u32 tint(u32 rgb, double t)
{
  int ch[3], i;
  ch[0] = (int)(rgb >> 16) & 255; ch[1] = (int)(rgb >> 8) & 255; ch[2] = (int)rgb & 255;
  for (i = 0; i < 3; i++) ch[i] = t >= 0 ? (int)(ch[i] + (255 - ch[i]) * t + 0.5) : (int)(ch[i] * (1 + t) + 0.5);
  return ((u32)ch[0] << 16) | ((u32)ch[1] << 8) | (u32)ch[2];
}
static u32 accent(const Chart *c, int k) { k %= 6; return c->accent[k] ? c->accent[k] & 0xFFFFFF : accents0[k]; }
static u32 palette_rgb(const Chart *c, int k, int n)
{
  static const double cyc[4] = { 0, -0.25, 0.4, -0.5 };
  int palette = c->palette;
  if (palette == 0) return tint(accent(c, k), cyc[(k / 6) % 4]);
  {
    /* one colour's shades, darkest first, spread over the n entries */
    u32 base = palette == 1 ? accent(c, 0) : accent(c, (palette - 2) % 6);
    double t = n > 1 ? -0.5 + 1.2 * k / (n - 1) : 0;
    return tint(base, t);
  }
}
u32 chart_series_rgb(const Chart *c, int k, int n)
{
  if (k >= 0 && k < c->ns && c->s[k].color) return c->s[k].color & 0xFFFFFF;
  return palette_rgb(c, k, n);
}

/* ------------------------------------------------------------------ numbers as text */
/* v with dec decimals (0-9), thousands separated if asked, into out */
static void fixed(double v, int dec, int thousands, char *out, size_t cap)
{
  char dig[400];
  int nd = 0, i, neg = q_neg(v), n = 0, intd;
  double a = q_abs(v), ip, fp, sc = q_pow10(dec);
  /* rounded at the last decimal, then the whole and fractional digits */
  a = q_floor(a * sc + 0.5);
  if (a >= 1e300) { snprintf(out, cap, "#NUM!"); return; }
  ip = q_floor(a / sc); fp = a - ip * sc;
  if (fp >= sc) { ip += 1; fp -= sc; }
  if (fp < 0) { ip -= 1; fp += sc; }
  if (ip >= 1e15) { snprintf(out, cap, "%s1E+%d", neg ? "-" : "", 15); return; }
  /* whole digits (backwards) */
  do { double q = q_floor(ip / 10); int d = (int)(ip - q * 10 + 0.5); if (d > 9) d = 9; if (d < 0) d = 0; dig[nd++] = (char)('0' + d); ip = q; } while (ip >= 1 && nd < 300);
  intd = nd;
  if (neg && (intd > 1 || dig[0] != '0' || fp > 0.5)) { if ((size_t)n + 1 < cap) out[n++] = '-'; }
  for (i = intd - 1; i >= 0; i--) {
    if ((size_t)n + 2 >= cap) break;
    out[n++] = dig[i];
    if (thousands && i > 0 && i % 3 == 0) out[n++] = ',';
  }
  if (dec > 0 && (size_t)n + dec + 2 < cap) {
    long f = (long)(fp + 0.5);
    out[n++] = '.';
    for (i = dec - 1; i >= 0; i--) { out[n + i] = (char)('0' + f % 10); f /= 10; }
    n += dec;
  }
  out[n] = 0;
}
/* Excel's General: up to 10 significant digits, small and large numbers in E notation */
static void general(double v, char *out, size_t cap)
{
  double a = q_abs(v);
  int e, dec;
  if (!(dhi(a) & 0x7FF00000UL) || ((dhi(a) >> 20) & 0x7FF) < 30) { snprintf(out, cap, "0"); return; }    /* (zero, or below 1e-299) */
  q_mag(a, &e);
  if (e >= 11 || e < -9) {
    /* d.ddddE+xx */
    double m = a / q_pow10(e);
    char t[40];
    int d;
    fixed(m, 5, 0, t, sizeof t);
    if (t[0] == '1' && t[1] == '0' && t[2] == '.') { e++; fixed(a / q_pow10(e), 5, 0, t, sizeof t); }
    d = (int)strlen(t);
    while (d > 1 && t[d - 1] == '0') t[--d] = 0;
    if (d > 0 && t[d - 1] == '.') t[--d] = 0;
    snprintf(out, cap, "%s%sE%c%02d", q_neg(v) ? "-" : "", t, e < 0 ? '-' : '+', e < 0 ? -e : e);
    return;
  }
  dec = 9 - (e > 0 ? e : 0);
  if (dec > 9) dec = 9;
  if (dec < 0) dec = 0;
  fixed(v, dec, 0, out, cap);
  if (strchr(out, '.')) {
    size_t n = strlen(out);
    while (n > 0 && out[n - 1] == '0') out[--n] = 0;
    if (n > 0 && out[n - 1] == '.') out[--n] = 0;
  }
  if (!strcmp(out, "-0")) strcpy(out, "0");
}
void chart_fmt_value(double v, const char *fmt, char *out, size_t cap)
{
  char pre[24] = "", suf[24] = "", num[96];
  const char *f = fmt ? fmt : "", *semi;
  int dec = 0, thou = 0, pct = 0, seen = 0, indec = 0, np = 0, ns = 0, digits = 0;
  size_t flen;
  out[0] = 0;
  if (!q_finite(v)) { snprintf(out, cap, "#NUM!"); return; }
  /* the first section is the one for every value here (the sign is added by hand) */
  semi = strchr(f, ';');
  flen = semi ? (size_t)(semi - f) : strlen(f);
  if (!flen || !strncmp(f, "General", 7) || !strncmp(f, "@", 1)) { general(v, out, cap); return; }
  while (flen) {
    char ch = *f;
    if (ch == '[') {
      /* [Red], [>100]...: left out; [$€-407]: the currency sign */
      const char *e = memchr(f, ']', flen);
      if (!e) break;
      if (f[1] == '$') {
        const char *q = f + 2;
        while (q < e && *q != '-') { if (seen) { if (ns + 1 < (int)sizeof suf) suf[ns++] = *q; } else if (np + 1 < (int)sizeof pre) pre[np++] = *q; q++; }
      }
      flen -= (size_t)(e - f + 1); f = e + 1;
      continue;
    }
    if (ch == '"') {
      const char *e = memchr(f + 1, '"', flen - 1);
      const char *q;
      if (!e) break;
      for (q = f + 1; q < e; q++) { if (seen) { if (ns + 1 < (int)sizeof suf) suf[ns++] = *q; } else if (np + 1 < (int)sizeof pre) pre[np++] = *q; }
      flen -= (size_t)(e - f + 1); f = e + 1;
      continue;
    }
    if (ch == '\\' && flen > 1) { if (seen) { if (ns + 1 < (int)sizeof suf) suf[ns++] = f[1]; } else if (np + 1 < (int)sizeof pre) pre[np++] = f[1]; f += 2; flen -= 2; continue; }
    if (ch == '0' || ch == '#' || ch == '?') { seen = 1; digits = 1; if (indec) dec++; }
    else if (ch == '.') { seen = 1; indec = 1; }
    else if (ch == ',') { if (!indec && seen) thou = 1; }
    else if (ch == '%') { pct = 1; if (ns + 1 < (int)sizeof suf) suf[ns++] = '%'; }
    else if (ch == '_' || ch == '*') { if (flen > 1) { f++; flen--; } }
    else if (ch == 'E' || ch == 'e' || ch == 'd' || ch == 'm' || ch == 'y' || ch == 'h' || ch == 's') { general(v, out, cap); return; }
    else if (ch != ' ' || seen) { if (seen) { if (ns + 1 < (int)sizeof suf) suf[ns++] = ch; } else if (np + 1 < (int)sizeof pre) pre[np++] = ch; }
    f++; flen--;
  }
  pre[np] = 0; suf[ns] = 0;
  if (!digits) { general(v, out, cap); return; }
  if (dec > 9) dec = 9;
  fixed(pct ? v * 100 : v, dec, thou, num, sizeof num);
  if (num[0] == '-') snprintf(out, cap, "-%s%s%s", pre, num + 1, suf);
  else snprintf(out, cap, "%s%s%s", pre, num, suf);
}

/* ------------------------------------------------------------------ the values to draw */
typedef struct {
  char *name; double *v, *x, *size; int n; u32 rgb;
} CDSeries;
typedef struct { char **cat; int ncat; CDSeries *s; int ns; } CData;
static void cdata_free(CData *d)
{
  int i;
  strs_free(d->cat, d->ncat);
  for (i = 0; i < d->ns; i++) { free(d->s[i].name); free(d->s[i].v); free(d->s[i].x); free(d->s[i].size); }
  free(d->s);
  memset(d, 0, sizeof *d);
}
/* the series' values as the layout wants them: names, categories (the first series' labels, else 1, 2, 3 ...) */
static int chart_resolve(const Chart *c, CData *d)
{
  int k, i, bad = 0;
  memset(d, 0, sizeof *d);
  if (!c->ns) return 1;
  if (!(d->s = (CDSeries *)calloc((size_t)c->ns, sizeof(CDSeries)))) return 0;
  d->ns = c->ns;
  for (k = 0; k < c->ns; k++) {
    const CSeries *s = &c->s[k];
    CDSeries *o = &d->s[k];
    int n = s->nv > MAXPTS ? MAXPTS : s->nv;
    o->v = (double *)malloc((size_t)(n + 1) * sizeof(double));
    if (!o->v) { bad = 1; break; }
    if (n) memcpy(o->v, s->v, (size_t)n * sizeof(double));
    o->n = n;
    if (s->name) o->name = str_dup(s->name);
    else { char t[24]; sprintf(t, "Series%d", k + 1); o->name = str_dup(t); }
    if (!o->name) { bad = 1; break; }
    if (is_xy(c->kind) && s->x && s->nx > 0) {
      for (i = 0; i < s->nx; i++) if (CV_IS_NONE(s->x[i])) break;
      if (i == s->nx && (o->x = ddup(s->x, s->nx, &bad))) { if (s->nx < o->n) o->n = s->nx; }
    }
    if (c->kind == CK_BUBBLE) {
      if (s->size && s->nsize > 0) { o->size = ddup(s->size, s->nsize, &bad); if (o->size && s->nsize < o->n) o->n = s->nsize; }
      else o->size = ddup(o->v, o->n, &bad);
    }
    o->rgb = chart_series_rgb(c, k, c->ns);
  }
  if (bad) { cdata_free(d); return 0; }
  for (k = 0; k < c->ns && !c->s[k].ncat; k++) ;
  if (k < c->ns && !is_xy(c->kind)) {
    int n = c->s[k].ncat > MAXPTS ? MAXPTS : c->s[k].ncat;
    if ((d->cat = (char **)calloc((size_t)n + 1, sizeof(char *)))) {
      for (i = 0; i < n; i++) d->cat[i] = str_dup(c->s[k].cat[i] ? c->s[k].cat[i] : "");
      d->ncat = n;
    }
  }
  {
    int most = 0;
    for (i = 0; i < d->ns; i++) if (d->s[i].n > most) most = d->s[i].n;
    if (d->ncat < most) {
      char **nc = (char **)realloc(d->cat, (size_t)most * sizeof(char *));
      if (nc) {
        d->cat = nc;
        for (i = d->ncat; i < most; i++) { char t[16]; sprintf(t, "%d", i + 1); d->cat[i] = str_dup(t); }
        d->ncat = most;
      }
    }
  }
  for (i = 0; i < d->ncat; i++) if (!d->cat[i] && !(d->cat[i] = str_dup(""))) { cdata_free(d); return 0; }
  return 1;
}

/* ------------------------------------------------------------------ laying out: helpers */
#define TEXT_RGB 0x595959
#define DARK_RGB 0x262626
#define GRID_RGB 0xD9D9D9
#define AXIS_RGB 0xBFBFBF
#define FILLRGB(c) (FILL_ON | ((c) & 0xFFFFFF))

typedef struct {
  const Chart *c; CData *d;
  PrimList *l;
  TextWidthFn tw; void *ctx;
  int fs, fst;                     /* label and title text sizes (pixels) */
  u32 text;                        /* text colour */
  int dark;                        /* a dark chart style */
} R;

static i32 tw(R *r, const char *s, int px, int bold)
{
  if (!s || !*s) return 0;
  if (r->tw) return r->tw(r->ctx, s, px, bold);
  return (i32)(strlen(s) * px * 55 / 100);
}
static i32 rnd(double v) { return (i32)q_floor(v + 0.5); }
static Prim *rect(R *r, u16 el, double x, double y, double w, double h, u32 fill, u32 line)
{
  Prim *p = prim_add(r->l, PR_RECT, el);
  if (!p) return NULL;
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  p->x = rnd(x); p->y = rnd(y);
  p->w = rnd(x + w) - p->x; p->h = rnd(y + h) - p->y;
  if (p->w < 1) p->w = 1;
  if (p->h < 1) p->h = 1;
  p->fill = fill; p->line = line; p->width = 1;
  return p;
}
static Prim *pts_prim(R *r, int t, u16 el, const double *xy, int n, u32 fill, u32 line, int width)
{
  Prim *p;
  int i;
  if (n < 2 || !(p = prim_add(r->l, t, el))) return NULL;
  if (!(p->pts = (i32 *)malloc((size_t)n * 2 * sizeof(i32)))) { r->l->n--; r->l->oom = 1; return NULL; }
  for (i = 0; i < 2 * n; i++) p->pts[i] = rnd(xy[i]);
  p->np = n; p->fill = fill; p->line = line; p->width = (u8)(width < 1 ? 1 : width);
  return p;
}
static Prim *line2(R *r, u16 el, double x0, double y0, double x1, double y1, u32 rgb, int width)
{
  double xy[4];
  xy[0] = x0; xy[1] = y0; xy[2] = x1; xy[3] = y1;
  return pts_prim(r, PR_LINE, el, xy, 2, 0, FILLRGB(rgb), width);
}
static Prim *ellipse(R *r, u16 el, double cx, double cy, double rx, double ry, u32 fill, u32 line)
{
  Prim *p = prim_add(r->l, PR_ELLIPSE, el);
  if (!p) return NULL;
  p->x = rnd(cx - rx); p->y = rnd(cy - ry);
  p->w = rnd(2 * rx); p->h = rnd(2 * ry);
  if (p->w < 1) p->w = 1;
  if (p->h < 1) p->h = 1;
  p->fill = fill; p->line = line; p->width = 1;
  return p;
}
static Prim *text(R *r, u16 el, double x, double y, const char *s, int px, u32 rgb, int bold, int align, int valign, int vert)
{
  Prim *p;
  if (!s || !*s || !(p = prim_add(r->l, PR_TEXT, el))) return NULL;
  if (!(p->text = str_dup(s))) { r->l->n--; r->l->oom = 1; return NULL; }
  p->x = rnd(x); p->y = rnd(y);
  p->size = (u8)(px > 255 ? 255 : px); p->bold = (u8)bold; p->align = (u8)align; p->valign = (u8)valign; p->vert = (u8)vert;
  p->line = FILLRGB(rgb);
  p->w = tw(r, s, px, bold); p->h = px * 6 / 5;
  return p;
}
/* a pie or doughnut slice from a0 to a1 (radians clockwise from 12 o'clock) */
static Prim *wedge(R *r, u16 el, double cx, double cy, double ro, double ri, double a0, double a1, u32 fill, u32 line)
{
  double xy[2 * 200];
  int n = 0, steps, i;
  steps = (int)((a1 - a0) / (M_PI_ / 45)) + 1;
  if (steps > 90) steps = 90;
  for (i = 0; i <= steps; i++) {
    double a = a0 + (a1 - a0) * i / steps;
    xy[n++] = cx + ro * q_sin(a); xy[n++] = cy - ro * q_cos(a);
  }
  if (ri > 0.5) {
    for (i = steps; i >= 0; i--) {
      double a = a0 + (a1 - a0) * i / steps;
      xy[n++] = cx + ri * q_sin(a); xy[n++] = cy - ri * q_cos(a);
    }
  } else { xy[n++] = cx; xy[n++] = cy; }
  return pts_prim(r, PR_POLY, el, xy, n / 2, fill, line, 1);
}
static void marker(R *r, u16 el, double x, double y, int k, u32 rgb, int size)
{
  /* Excel's automatic markers go round: diamond, square, triangle, circle... */
  double s = size, xy[8];
  switch (k % 4) {
  case 0: xy[0] = x; xy[1] = y - s; xy[2] = x + s; xy[3] = y; xy[4] = x; xy[5] = y + s; xy[6] = x - s; xy[7] = y; pts_prim(r, PR_POLY, el, xy, 4, FILLRGB(rgb), FILLRGB(rgb), 1); break;
  case 1: rect(r, el, x - s + 1, y - s + 1, 2 * s - 1, 2 * s - 1, FILLRGB(rgb), FILLRGB(rgb)); break;
  case 2: xy[0] = x; xy[1] = y - s; xy[2] = x + s; xy[3] = y + s - 1; xy[4] = x - s; xy[5] = y + s - 1; pts_prim(r, PR_POLY, el, xy, 3, FILLRGB(rgb), FILLRGB(rgb), 1); break;
  default: ellipse(r, el, x, y, s, s, FILLRGB(rgb), FILLRGB(rgb)); break;
  }
}
/* a value as the chart's number format shows it (percentages for 100% charts) */
static void fmtnum(R *r, double v, int pct, char *out, size_t cap)
{
  if (pct) { chart_fmt_value(v, "0%", out, cap); return; }
  if (q_abs(v) < 1e-12) v = 0;
  chart_fmt_value(v, r->c->fmt, out, cap);
}

void chart_nice_scale(double lo, double hi, int maxticks, double *min, double *max, double *major)
{
  double range, step, mag, f;
  if (!q_finite(lo) || !q_finite(hi)) { lo = 0; hi = 1; }
  if (hi < lo) { double t = lo; lo = hi; hi = t; }
  if (hi == lo) { if (hi == 0) hi = 1; else if (hi > 0) lo = 0; else hi = 0; }
  if (maxticks < 2) maxticks = 2;
  range = hi - lo;
  step = range / maxticks;
  mag = q_mag(step, NULL);
  f = step / mag;
  step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * mag;
  *min = q_floor(lo / step + 1e-9) * step;
  *max = q_ceil(hi / step - 1e-9) * step;
  if (*max <= *min) *max = *min + step;
  *major = step;
}

/* ------------------------------------------------------------------ the frame: title, legend, plot area */
typedef struct { const char *label; u32 rgb; int kind; } LegEnt;   /* kind: 0 square, 1 line, 2 marker, 3 line and marker */
static int series_kind(const Chart *c, int k)
{
  int sk = c->s[k].kind;
  if (c->kind == CK_COMBO) {
    if (sk != CK_SAME && sk < CK_N) return sk;
    return k == 0 || c->ns == 1 ? CK_COL : CK_LINE;
  }
  return c->kind;
}
static int legend_kind(const Chart *c, int k)
{
  int sk = series_kind(c, k);
  if (sk == CK_LINEMARK || (sk == CK_RADAR && c->style != 3)) return 3;
  if (is_line(sk) || sk == CK_SCATTERLINE || sk == CK_RADAR || sk == CK_STOCK) return 1;
  if (sk == CK_SCATTER || sk == CK_BUBBLE) return 2;
  return 0;
}
/* the legend in the box x, y, w, h (vertical: a column) */
static void draw_legend(R *r, const LegEnt *e, int n, double x, double y, double w, double h, int vertical)
{
  int i, fs = r->fs, sw = fs * 7 / 10 + 1, gap = fs / 2 + 2, line = fs * 13 / 10;
  double cx = x, cy = y;
  rect(r, CE_LEGEND, x, y, w, h, r->c->efill[CE_LEGEND], r->c->eline[CE_LEGEND]);
  if (!vertical) {
    /* rows, centred */
    double roww = 0;
    int first = 0;
    cy = y + 2;
    for (i = 0; i <= n; i++) {
      double ew = i < n ? sw + 4 + tw(r, e[i].label, fs, 0) + gap : 0;
      if (i == n || (roww + ew > w && i > first)) {
        int j;
        cx = x + (w - roww + gap) / 2;
        for (j = first; j < i; j++) {
          double my = cy + line / 2.0;
          if (e[j].kind == 1 || e[j].kind == 3) { line2(r, CE_LEGEND, cx, my, cx + sw + 4, my, e[j].rgb, 2); if (e[j].kind == 3) marker(r, CE_LEGEND, cx + sw / 2.0 + 2, my, j, e[j].rgb, 2); cx += sw + 8; }
          else if (e[j].kind == 2) { marker(r, CE_LEGEND, cx + sw / 2.0, my, j, e[j].rgb, 3); cx += sw + 4; }
          else { rect(r, CE_LEGEND, cx, my - sw / 2.0, sw, sw, FILLRGB(e[j].rgb), 0); cx += sw + 4; }
          text(r, CE_LEGEND, cx, my, e[j].label, fs, r->text, 0, 0, 1, 0);
          cx += tw(r, e[j].label, fs, 0) + gap;
        }
        cy += line; first = i; roww = 0;
        if (cy + line > y + h + 1) break;
      }
      roww += ew;
    }
  } else {
    cy = y + (h - n * line) / 2.0;
    if (cy < y) cy = y;
    for (i = 0; i < n && cy + line <= y + h + 1; i++) {
      double my = cy + line / 2.0;
      cx = x + 2;
      if (e[i].kind == 1 || e[i].kind == 3) { line2(r, CE_LEGEND, cx, my, cx + sw + 4, my, e[i].rgb, 2); if (e[i].kind == 3) marker(r, CE_LEGEND, cx + sw / 2.0 + 2, my, i, e[i].rgb, 2); cx += sw + 8; }
      else if (e[i].kind == 2) { marker(r, CE_LEGEND, cx + sw / 2.0, my, i, e[i].rgb, 3); cx += sw + 4; }
      else { rect(r, CE_LEGEND, cx, my - sw / 2.0, sw, sw, FILLRGB(e[i].rgb), 0); cx += sw + 4; }
      text(r, CE_LEGEND, cx, my, e[i].label, fs, r->text, 0, 0, 1, 0);
      cy += line;
    }
  }
}
/* the legend's entries: the series, or the categories of a pie-like chart (point colours) */
static int legend_entries(R *r, LegEnt *e, int max)
{
  const Chart *c = r->c;
  CData *d = r->d;
  int n = 0, i;
  if (chart_is_pielike(c->kind) && c->kind != CK_FUNNEL) {
    int np = d->ns ? d->s[0].n : 0;
    for (i = 0; i < np && i < d->ncat && n < max; i++) { e[n].label = d->cat[i]; e[n].rgb = palette_rgb(c, i, np); e[n].kind = 0; n++; }
    return n;
  }
  if (c->kind == CK_WATERFALL) {
    static const char *const wf[3] = { "Increase", "Decrease", "Total" };
    for (i = 0; i < 3; i++) { e[n].label = wf[i]; e[n].rgb = palette_rgb(c, i, 3); e[n].kind = 0; n++; }
    return n;
  }
  for (i = 0; i < d->ns && n < max; i++) { e[n].label = d->s[i].name; e[n].rgb = d->s[i].rgb; e[n].kind = legend_kind(c, i); n++; }
  return n;
}

/* ------------------------------------------------------------------ cartesian charts */
typedef struct {
  double x, y, w, h;               /* the plot area */
  double min, max, major;          /* the value axis */
  double smin, smax, smajor;       /* the secondary one */
  int secondary;
  int horiz;                       /* bars: categories up the left (the first at the bottom), values across */
  int pct;                         /* 100% stacked: values are fractions */
  int ncat;
} Plot;
static double vpos(const Plot *p, double v, int sec)
{
  double lo = sec ? p->smin : p->min, hi = sec ? p->smax : p->max, f = hi > lo ? (v - lo) / (hi - lo) : 0;
  if (f < -0.05) f = -0.05;
  if (f > 1.05) f = 1.05;
  return p->horiz ? p->x + f * p->w : p->y + p->h - f * p->h;
}
/* where category slot i starts (bars: its top) */
static double cstart(const Plot *p, double i)
{
  double slot = (p->horiz ? p->h : p->w) / (p->ncat ? p->ncat : 1);
  return p->horiz ? p->y + p->h - (i + 1) * slot : p->x + i * slot;
}
static double cpos(const Plot *p, double i)   /* the middle of category slot i */
{
  return cstart(p, i) + (p->horiz ? p->h : p->w) / (p->ncat ? p->ncat : 1) / 2;
}
static int stacked(int k) { return k == CK_COLSTACK || k == CK_COL100 || k == CK_BARSTACK || k == CK_BAR100 || k == CK_LINESTACK || k == CK_AREASTACK || k == CK_AREA100; }
static int pct100(int k) { return k == CK_COL100 || k == CK_BAR100 || k == CK_AREA100; }
static double val(const CData *d, int k, int i) { return k < d->ns && i < d->s[k].n ? d->s[k].v[i] : CV_NONE; }
/* the extent of the values to show on the (secondary) value axis */
static void value_range(R *r, int sec, double *lo, double *hi)
{
  const Chart *c = r->c;
  CData *d = r->d;
  int k, i, ncat = d->ncat;
  int st = stacked(c->kind);
  Rng g;
  memset(&g, 0, sizeof g);
  if (pct100(c->kind)) { *lo = 0; *hi = 1; for (i = 0; i < ncat; i++) for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (!CV_IS_NONE(v) && q_neg(v) && v != 0) *lo = -1; } return; }
  if (st) {
    for (i = 0; i < ncat; i++) {
      double pos = 0, neg = 0;
      for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (CV_IS_NONE(v)) continue; if (!q_neg(v) || c->kind == CK_LINESTACK) pos += v; else neg += v; }
      rng_add(&g, pos); rng_add(&g, neg);
    }
  } else if (c->kind == CK_WATERFALL) {
    double t = 0;
    for (i = 0; i < ncat; i++) { double v = val(d, 0, i); if (CV_IS_NONE(v)) continue; t += v; rng_add(&g, t); rng_add(&g, t - v); }
  } else {
    for (k = 0; k < d->ns; k++) {
      if (c->kind == CK_COMBO && (c->s[k].secondary != 0) != (sec != 0)) continue;
      for (i = 0; i < d->s[k].n; i++) { double v = d->s[k].v[i]; if (CV_IS_NONE(v)) continue; rng_add(&g, v); }
    }
  }
  *lo = g.lo; *hi = g.hi;
  if (!g.any) { *lo = 0; *hi = 1; }
  /* bars and areas stand on zero */
  if (*lo > 0 && c->kind != CK_LINE && c->kind != CK_LINEMARK && c->kind != CK_STOCK && !(c->kind == CK_COMBO && sec)) *lo = 0;
  if (*hi < 0) *hi = 0;
}
static void axis_scale(R *r, const CAxis *ax, double lo, double hi, double len, double *min, double *max, double *major)
{
  int ticks = (int)(len / (r->fs * 2.6));
  if (ticks < 2) ticks = 2;
  if (ticks > 10) ticks = 10;
  if (ax->minset) lo = ax->min;
  if (ax->maxset) hi = ax->max;
  chart_nice_scale(lo, hi, ticks, min, max, major);
  if (ax->minset) *min = ax->min;
  if (ax->maxset) *max = ax->max;
  if (ax->majset && ax->major > 0 && (*max - *min) / ax->major < 200) *major = ax->major;
  if (*max <= *min) *max = *min + 1;
}
/* value axis labels' widest */
static i32 vlabels_width(R *r, double min, double max, double major, int pct)
{
  double v;
  i32 w = 0;
  char t[64];
  int n = 0;
  for (v = min; v <= max + major * 1e-6 && n < 60; v += major, n++) { fmtnum(r, v, pct, t, sizeof t); if (tw(r, t, r->fs, 0) > w) w = tw(r, t, r->fs, 0); }
  return w;
}
static void draw_value_axis(R *r, Plot *p, int sec)
{
  double min = sec ? p->smin : p->min, max = sec ? p->smax : p->max, major = sec ? p->smajor : p->major, v;
  char t[64];
  int n = 0;
  for (v = min; v <= max + major * 1e-6 && n < 60; v += major, n++) {
    double q = vpos(p, v, sec);
    fmtnum(r, q_abs(v) < major * 1e-9 ? 0 : v, p->pct, t, sizeof t);
    if (p->horiz) {
      if (r->c->hgrid && !sec) line2(r, CE_NONE, q, p->y, q, p->y + p->h, GRID_RGB, 1);
      text(r, CE_YAXIS, q, p->y + p->h + 3, t, r->fs, r->text, 0, 1, 0, 0);
    } else {
      if (r->c->hgrid && !sec) line2(r, CE_NONE, p->x, q, p->x + p->w, q, GRID_RGB, 1);
      if (sec) text(r, CE_YAXIS, p->x + p->w + 4, q, t, r->fs, r->text, 0, 0, 1, 0);
      else text(r, CE_YAXIS, p->x - 4, q, t, r->fs, r->text, 0, 2, 1, 0);
    }
  }
}
static void draw_cat_axis(R *r, Plot *p)
{
  int i, step = 1;
  double slot = (p->horiz ? p->h : p->w) / (p->ncat ? p->ncat : 1), base;
  CData *d = r->d;
  i32 widest = 0;
  for (i = 0; i < p->ncat; i++) { i32 w = tw(r, d->cat[i], r->fs, 0); if (w > widest) widest = w; }
  if (!p->horiz) { while (step < p->ncat && slot * step < widest + 6) step++; }
  else { while (step < p->ncat && slot * step < r->fs + 2) step++; }
  base = vpos(p, p->min < 0 && p->max > 0 ? 0 : p->min, 0);
  if (p->horiz) line2(r, CE_XAXIS, base, p->y, base, p->y + p->h, AXIS_RGB, 1);
  else line2(r, CE_XAXIS, p->x, base, p->x + p->w, base, AXIS_RGB, 1);
  for (i = 0; i < p->ncat; i += step) {
    double q = cpos(p, i);
    if (r->c->vgrid) {
      double e = cstart(p, i);
      if (p->horiz) line2(r, CE_NONE, p->x, e, p->x + p->w, e, GRID_RGB, 1);
      else line2(r, CE_NONE, e, p->y, e, p->y + p->h, GRID_RGB, 1);
    }
    if (p->horiz) text(r, CE_XAXIS, p->x - 4, q, d->cat[i], r->fs, r->text, 0, 2, 1, 0);
    else text(r, CE_XAXIS, q, p->y + p->h + 3, d->cat[i], r->fs, r->text, 0, 1, 0, 0);
  }
}
static void label_at(R *r, double x, double y, double v, int pct, int valign, int align)
{
  char t[64];
  if (CV_IS_NONE(v)) return;
  fmtnum(r, v, pct, t, sizeof t);
  text(r, CE_NONE, x, y, t, r->fs, r->text, 0, align, valign, 0);
}
/* columns or bars of the series whose kind is a column or bar kind */
static void draw_bars(R *r, Plot *p, int kind)
{
  CData *d = r->d;
  const Chart *c = r->c;
  int k, i, nb = 0, st = stacked(kind), bi = 0;
  double slot = (p->horiz ? p->h : p->w) / (p->ncat ? p->ncat : 1), group = slot / 2.5 * 1.5, bw;
  int outline = c->style == 2 || c->style == 8;
  for (k = 0; k < d->ns; k++) if (is_col(series_kind(c, k)) || is_bar(series_kind(c, k)) || c->kind == CK_HISTOGRAM) nb++;
  if (!nb) return;
  if (c->kind == CK_HISTOGRAM) group = slot;
  bw = st ? group : group / nb;
  for (i = 0; i < p->ncat; i++) {
    double pos = 0, neg = 0, tot = 0;
    if (pct100(kind)) for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (!CV_IS_NONE(v)) tot += q_abs(v); }
    bi = 0;
    for (k = 0; k < d->ns; k++) {
      int sk = series_kind(c, k), sec = c->kind == CK_COMBO && c->s[k].secondary;
      double v = val(d, k, i), a, b, s0;
      u32 rgb = d->s[k].rgb, lc;
      if (!(is_col(sk) || is_bar(sk) || c->kind == CK_HISTOGRAM)) continue;
      if (CV_IS_NONE(v)) { bi++; continue; }
      if (pct100(kind)) v = tot > 0 ? v / tot : 0;
      if (st) { if (v >= 0) { a = pos; pos += v; b = pos; } else { a = neg; neg += v; b = neg; } }
      else { a = 0; b = v; }
      if (!st && p->min > 0 && !sec) a = p->min;
      /* bars: the first series at the bottom of its group, as Word draws them */
      s0 = cstart(p, i) + (slot - group) / 2 + (st ? 0 : (p->horiz ? (nb - 1 - bi) : bi) * bw);
      lc = outline ? FILLRGB(tint(rgb, -0.5)) : c->kind == CK_HISTOGRAM ? FILLRGB(0xFFFFFF) : 0;
      if (c->s[k].line) lc = c->s[k].line;
      if (p->horiz) rect(r, (u16)(CE_SERIES + k), vpos(p, a, sec), s0, vpos(p, b, sec) - vpos(p, a, sec), bw, FILLRGB(rgb), lc);
      else rect(r, (u16)(CE_SERIES + k), s0, vpos(p, b, sec), bw, vpos(p, a, sec) - vpos(p, b, sec), FILLRGB(rgb), lc);
      if (c->labels) {
        double mid = s0 + bw / 2, end = vpos(p, st ? (a + b) / 2 : b, sec);
        if (p->horiz) label_at(r, st ? end : end + 3, mid, st || pct100(kind) ? val(d, k, i) : v, 0, 1, st ? 1 : 0);
        else label_at(r, mid, st ? end : end - 2, val(d, k, i), 0, st ? 1 : 2, 1);
      }
      bi++;
    }
  }
}
static void draw_lines(R *r, Plot *p, int area)
{
  CData *d = r->d;
  const Chart *c = r->c;
  int k, i;
  double *xy = (double *)malloc((size_t)(p->ncat * 2 + 4) * 2 * sizeof(double)), *cum = (double *)calloc((size_t)p->ncat + 1, sizeof(double)), *tot = (double *)calloc((size_t)p->ncat + 1, sizeof(double));
  int lw = c->style == 3 ? 3 : 2;
  if (!xy || !cum || !tot) { free(xy); free(cum); free(tot); r->l->oom = 1; return; }
  for (i = 0; i < p->ncat; i++) for (k = 0; k < d->ns; k++) { double v = val(d, k, i); if (!CV_IS_NONE(v)) tot[i] += q_abs(v); }
  /* areas are drawn last series first when not stacked, so the front one is the first */
  for (k = area && !stacked(c->kind) ? d->ns - 1 : 0; area && !stacked(c->kind) ? k >= 0 : k < d->ns; k += area && !stacked(c->kind) ? -1 : 1) {
    int sk = series_kind(c, k), sec = c->kind == CK_COMBO && c->s[k].secondary, n = 0;
    int st = stacked(sk), mark = sk == CK_LINEMARK && !c->s[k].nomarker;
    u32 rgb = d->s[k].rgb;
    if (area ? !is_area(sk) : !is_line(sk)) continue;
    if (area) {
      /* the top edge, then back along the bottom (the series below, or zero) */
      for (i = 0; i < p->ncat; i++) {
        double v = val(d, k, i), b = st ? cum[i] : 0;
        if (CV_IS_NONE(v)) v = 0;
        if (pct100(c->kind)) v = tot[i] > 0 ? v / tot[i] : 0;
        xy[n++] = p->ncat > 1 ? p->x + p->w * i / (p->ncat - 1) : cpos(p, i); xy[n++] = vpos(p, b + v, sec);
      }
      for (i = p->ncat - 1; i >= 0; i--) { xy[n++] = p->ncat > 1 ? p->x + p->w * i / (p->ncat - 1) : cpos(p, i); xy[n++] = vpos(p, st ? cum[i] : (p->min > 0 ? p->min : 0), sec); }
      pts_prim(r, PR_POLY, (u16)(CE_SERIES + k), xy, n / 2, FILLRGB(rgb), c->s[k].line ? c->s[k].line : (c->style == 2 ? FILLRGB(tint(rgb, -0.4)) : 0), 1);
      for (i = 0; i < p->ncat; i++) {
        double v = val(d, k, i);
        if (CV_IS_NONE(v)) v = 0;
        if (pct100(c->kind)) v = tot[i] > 0 ? v / tot[i] : 0;
        if (c->labels) label_at(r, p->ncat > 1 ? p->x + p->w * i / (p->ncat - 1) : cpos(p, i), vpos(p, (st ? cum[i] : 0) + v / 2, sec), val(d, k, i), 0, 1, 1);
        if (st) cum[i] += v;
      }
      continue;
    }
    /* a line: runs of points, broken at empty points */
    for (i = 0; i <= p->ncat; i++) {
      double v = i < p->ncat ? val(d, k, i) : CV_NONE;
      if (!CV_IS_NONE(v)) {
        if (st) { cum[i] += v; v = cum[i]; }
        xy[n++] = cpos(p, i); xy[n++] = vpos(p, v, sec);
      }
      if (CV_IS_NONE(v) || i == p->ncat) {
        if (n >= 4) pts_prim(r, PR_LINE, (u16)(CE_SERIES + k), xy, n / 2, 0, FILLRGB(c->s[k].line ? c->s[k].line : rgb), lw);
        n = 0;
      }
    }
    for (i = 0; i < p->ncat; i++) {
      double v = val(d, k, i);
      if (CV_IS_NONE(v)) continue;
      if (st) v = cum[i];
      if (mark) marker(r, (u16)(CE_SERIES + k), cpos(p, i), vpos(p, v, sec), k, rgb, c->style == 3 ? 4 : 3);
      if (c->labels) label_at(r, cpos(p, i) + 4, vpos(p, v, sec) - 3, val(d, k, i), 0, 2, 0);
    }
  }
  free(xy); free(cum); free(tot);
}
static void draw_stock(R *r, Plot *p)
{
  CData *d = r->d;
  int i, o = d->ns >= 4;   /* open, high, low, close; or high, low, close */
  double slot = p->w / (p->ncat ? p->ncat : 1), bw = slot * 0.4;
  for (i = 0; i < p->ncat; i++) {
    double x = cpos(p, i), hi = val(d, o, i), lo = val(d, o + 1, i), cl = val(d, o + 2, i);
    if (!CV_IS_NONE(hi) && !CV_IS_NONE(lo)) line2(r, CE_SERIES + (u16)o, x, vpos(p, hi, 0), x, vpos(p, lo, 0), 0x404040, 1);
    if (o && !CV_IS_NONE(val(d, 0, i)) && !CV_IS_NONE(cl)) {
      double op = val(d, 0, i);
      rect(r, CE_SERIES, x - bw / 2, vpos(p, op > cl ? op : cl, 0), bw, q_abs(vpos(p, op, 0) - vpos(p, cl, 0)) + 1, FILLRGB(cl >= op ? 0xFFFFFF : 0x404040), FILLRGB(0x404040));
    } else if (!CV_IS_NONE(cl)) line2(r, CE_SERIES + (u16)(o + 2), x, vpos(p, cl, 0), x + bw / 2 + 2, vpos(p, cl, 0), 0x404040, 2);
    if (r->c->labels && !CV_IS_NONE(cl)) label_at(r, x + bw / 2 + 3, vpos(p, cl, 0), cl, 0, 1, 0);
  }
}
static void draw_waterfall(R *r, Plot *p)
{
  CData *d = r->d;
  int i, havep = 0;
  double slot = p->w / (p->ncat ? p->ncat : 1), bw = slot * 0.6, t = 0;
  for (i = 0; i < p->ncat; i++) {
    double v = val(d, 0, i), x0;
    u32 rgb;
    if (CV_IS_NONE(v)) continue;
    x0 = p->x + i * slot + (slot - bw) / 2;
    rgb = palette_rgb(r->c, v >= 0 ? 0 : 1, 3);
    rect(r, CE_SERIES, x0, vpos(p, v >= 0 ? t + v : t, 0), bw, q_abs(vpos(p, t, 0) - vpos(p, t + v, 0)), FILLRGB(rgb), 0);
    if (havep) line2(r, CE_NONE, x0 - (slot - bw), vpos(p, t, 0), x0, vpos(p, t, 0), 0x7F7F7F, 1);
    t += v; havep = 1;
    label_at(r, x0 + bw / 2, vpos(p, v >= 0 ? t : t - v, 0) - 2, v, 0, 2, 1);
  }
}
/* histogram bins of the first series (Scott's rule, as Excel's automatic bins) */
static int histogram(R *r, char ***labels, double **counts)
{
  CData *d = r->d;
  double mn, mx, sum = 0, ss = 0, bw, sd;
  int n = 0, i, nb;
  Rng g;
  memset(&g, 0, sizeof g);
  if (!d->ns) return 0;
  for (i = 0; i < d->s[0].n; i++) { double v = d->s[0].v[i]; if (CV_IS_NONE(v)) continue; n++; sum += v; rng_add(&g, v); }
  if (!n) return 0;
  mn = g.lo; mx = g.hi;
  for (i = 0; i < d->s[0].n; i++) { double v = d->s[0].v[i]; if (!CV_IS_NONE(v)) ss += (v - sum / n) * (v - sum / n); }
  sd = n > 1 ? q_sqrt(ss / (n - 1)) : 0;
  {
    /* n to the third: by halving */
    double lo = 0, hi = n > 1 ? n : 1, c3;
    int it;
    for (it = 0; it < 60; it++) { c3 = (lo + hi) / 2; if (c3 * c3 * c3 > n) hi = c3; else lo = c3; }
    c3 = (lo + hi) / 2;
    bw = sd > 0 && c3 > 0 ? 3.5 * sd / c3 : 1;
  }
  nb = bw > 0 ? (int)q_ceil((mx - mn) / bw) : 1;
  if (nb < 1) nb = 1;
  if (nb > 50) { nb = 50; bw = (mx - mn) / 50; }
  *labels = (char **)calloc((size_t)nb, sizeof(char *));
  *counts = (double *)calloc((size_t)nb, sizeof(double));
  if (!*labels || !*counts) { free(*labels); free(*counts); return 0; }
  for (i = 0; i < d->s[0].n; i++) {
    double v = d->s[0].v[i];
    int b;
    if (CV_IS_NONE(v)) continue;
    b = bw > 0 ? (int)q_ceil((v - mn) / bw) - 1 : 0;
    if (b < 0) b = 0;
    if (b >= nb) b = nb - 1;
    (*counts)[b]++;
  }
  for (i = 0; i < nb; i++) {
    char a[40], b[40], t[90];
    general(mn + i * bw, a, sizeof a); general(mn + (i + 1) * bw, b, sizeof b);
    snprintf(t, sizeof t, "%c%s, %s]", i ? '(' : '[', a, b);
    (*labels)[i] = str_dup(t);
    if (!(*labels)[i]) (*labels)[i] = str_dup("");
  }
  return nb;
}
static double quartile_exc(const double *s, int n, double q)
{
  double h = (n + 1) * q;
  int lo = (int)q_floor(h);
  if (n <= 0) return 0;
  if (lo < 1) return s[0];
  if (lo >= n) return s[n - 1];
  return s[lo - 1] + (h - lo) * (s[lo] - s[lo - 1]);
}
static int dcmp_(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void draw_box(R *r, Plot *p)
{
  CData *d = r->d;
  int k;
  double slot = p->w / (p->ncat ? p->ncat : 1), bw = slot * 0.5;
  for (k = 0; k < d->ns; k++) {
    double *s = (double *)malloc((size_t)(d->s[k].n + 1) * sizeof(double)), q1, q2, q3, iqr, lo, hi, mean = 0, x;
    Rng g;
    int n = 0, i;
    u32 rgb = d->s[k].rgb;
    if (!s) continue;
    for (i = 0; i < d->s[k].n; i++) if (!CV_IS_NONE(d->s[k].v[i])) { s[n++] = d->s[k].v[i]; mean += d->s[k].v[i]; }
    if (!n) { free(s); continue; }
    mean /= n;
    qsort(s, (size_t)n, sizeof(double), dcmp_);
    q1 = quartile_exc(s, n, 0.25); q2 = quartile_exc(s, n, 0.5); q3 = quartile_exc(s, n, 0.75); iqr = q3 - q1;
    memset(&g, 0, sizeof g);
    for (i = 0; i < n; i++) if (s[i] >= q1 - 1.5 * iqr && s[i] <= q3 + 1.5 * iqr) rng_add(&g, s[i]);
    lo = g.any ? g.lo : q1; hi = g.any ? g.hi : q3;
    x = cpos(p, k);
    line2(r, (u16)(CE_SERIES + k), x, vpos(p, hi, 0), x, vpos(p, q3, 0), 0x404040, 1);
    line2(r, (u16)(CE_SERIES + k), x, vpos(p, lo, 0), x, vpos(p, q1, 0), 0x404040, 1);
    line2(r, (u16)(CE_SERIES + k), x - bw / 4, vpos(p, hi, 0), x + bw / 4, vpos(p, hi, 0), 0x404040, 1);
    line2(r, (u16)(CE_SERIES + k), x - bw / 4, vpos(p, lo, 0), x + bw / 4, vpos(p, lo, 0), 0x404040, 1);
    rect(r, (u16)(CE_SERIES + k), x - bw / 2, vpos(p, q3, 0), bw, vpos(p, q1, 0) - vpos(p, q3, 0), FILLRGB(rgb), FILLRGB(tint(rgb, -0.4)));
    line2(r, (u16)(CE_SERIES + k), x - bw / 2, vpos(p, q2, 0), x + bw / 2, vpos(p, q2, 0), 0xFFFFFF, 1);
    /* the mean as a cross, the outliers as rings */
    line2(r, (u16)(CE_SERIES + k), x - 3, vpos(p, mean, 0) - 3, x + 3, vpos(p, mean, 0) + 3, 0xFFFFFF, 1);
    line2(r, (u16)(CE_SERIES + k), x - 3, vpos(p, mean, 0) + 3, x + 3, vpos(p, mean, 0) - 3, 0xFFFFFF, 1);
    for (i = 0; i < n; i++) if (s[i] < lo || s[i] > hi) ellipse(r, (u16)(CE_SERIES + k), x, vpos(p, s[i], 0), 3, 3, 0, FILLRGB(rgb));
    if (r->c->labels) label_at(r, x + bw / 2 + 3, vpos(p, q2, 0), q2, 0, 1, 0);
    free(s);
  }
}
/* the box left for the plot after axis labels and titles; the cartesian chart drawn there */
static void draw_cartesian(R *r, double x, double y, double w, double h)
{
  const Chart *c = r->c;
  CData *d = r->d;
  Plot p;
  double lo, hi, slo = 0, shi = 1;
  i32 lw, cw = 0, sw = 0;
  int k, i, horiz = is_bar(c->kind), xytitle = 0;
  char **hl = NULL;
  double *hc = NULL;
  int nh = 0;
  char **savecat = NULL;
  int savencat = 0, swapped = 0;
  memset(&p, 0, sizeof p);
  p.horiz = horiz;
  p.pct = pct100(c->kind);
  if (c->kind == CK_HISTOGRAM) {
    /* the bins become the categories of one series of counts */
    nh = histogram(r, &hl, &hc);
    savecat = d->cat; savencat = d->ncat; swapped = 1;
    d->cat = hl; d->ncat = nh;
    if (d->ns) { free(d->s[0].v); d->s[0].v = hc; d->s[0].n = nh; hc = NULL; }
  }
  if (c->kind == CK_BOXWHISKER) {
    /* one box a series, named below it */
    savecat = d->cat; savencat = d->ncat; swapped = 1;
    d->cat = (char **)calloc((size_t)d->ns + 1, sizeof(char *));
    for (k = 0; d->cat && k < d->ns; k++) d->cat[k] = d->s[k].name;
    d->ncat = d->cat ? d->ns : 0;
  }
  p.ncat = d->ncat;
  value_range(r, 0, &lo, &hi);
  if (c->kind == CK_BOXWHISKER) {
    Rng g;
    memset(&g, 0, sizeof g);
    for (k = 0; k < d->ns; k++) for (i = 0; i < d->s[k].n; i++) if (!CV_IS_NONE(d->s[k].v[i])) rng_add(&g, d->s[k].v[i]);
    lo = g.any ? g.lo : 0; hi = g.any ? g.hi : 1;
  }
  for (k = 0; k < c->ns; k++) if (c->kind == CK_COMBO && c->s[k].secondary) p.secondary = 1;
  if (p.secondary) value_range(r, 1, &slo, &shi);
  /* room for the axis titles and labels */
  if (c->ytitle && *c->ytitle) { x += r->fs * 3 / 2 + 2; w -= r->fs * 3 / 2 + 2; xytitle = 1; }
  if (c->xtitle && *c->xtitle) h -= r->fs * 3 / 2 + 2;
  axis_scale(r, &c->va, lo, hi, horiz ? w : h, &p.min, &p.max, &p.major);
  if (p.secondary) { CAxis none; memset(&none, 0, sizeof none); axis_scale(r, &none, slo, shi, h, &p.smin, &p.smax, &p.smajor); }
  lw = vlabels_width(r, p.min, p.max, p.major, p.pct);
  if (p.secondary) sw = vlabels_width(r, p.smin, p.smax, p.smajor, 0) + 6;
  if (horiz) { for (i = 0; i < d->ncat; i++) { i32 t = tw(r, d->cat[i], r->fs, 0); if (t > cw) cw = t; } if (cw > w / 3) cw = (i32)(w / 3); }
  p.x = x + (horiz ? cw : lw) + 6;
  p.y = y + r->fs / 2 + 2;
  p.w = w - (p.x - x) - sw - 8;
  p.h = h - (p.y - y) - r->fs * 6 / 5 - 4;
  if (p.w < 10) p.w = 10;
  if (p.h < 10) p.h = 10;
  rect(r, CE_PLOT, p.x, p.y, p.w, p.h, c->efill[CE_PLOT] ? c->efill[CE_PLOT] : (c->style == 4 ? FILLRGB(0xF2F2F2) : 0), c->eline[CE_PLOT]);
  draw_value_axis(r, &p, 0);
  if (p.secondary) draw_value_axis(r, &p, 1);
  draw_cat_axis(r, &p);
  if (c->kind == CK_STOCK) draw_stock(r, &p);
  else if (c->kind == CK_WATERFALL) draw_waterfall(r, &p);
  else if (c->kind == CK_BOXWHISKER) draw_box(r, &p);
  else {
    int kind = c->kind == CK_COMBO || c->kind == CK_HISTOGRAM ? CK_COL : c->kind;
    draw_lines(r, &p, 1);
    draw_bars(r, &p, kind);
    draw_lines(r, &p, 0);
  }
  if (xytitle) text(r, CE_YAXIS, x - r->fs * 3 / 2, p.y + p.h / 2, c->ytitle, r->fs, r->text, 1, 1, 1, 1);
  if (c->xtitle && *c->xtitle) text(r, CE_XAXIS, p.x + p.w / 2, y + h + 2, c->xtitle, r->fs, r->text, 1, 1, 0, 0);
  if (swapped) {
    if (c->kind == CK_HISTOGRAM) { for (i = 0; i < d->ncat; i++) free(d->cat[i]); }
    free(d->cat);
    d->cat = savecat; d->ncat = savencat;
  }
  free(hc);
}
/* scatter and bubble: two value axes */
static void draw_xy(R *r, double x, double y, double w, double h)
{
  const Chart *c = r->c;
  CData *d = r->d;
  Plot p, q;
  double xlo, xhi, ylo, yhi, smax = 0, xmin, xmax, xmaj;
  Rng gx, gy;
  int k, i, lw = c->style == 3 ? 3 : 2;
  i32 lwid;
  double *xy;
  memset(&p, 0, sizeof p);
  memset(&gx, 0, sizeof gx); memset(&gy, 0, sizeof gy);
  for (k = 0; k < d->ns; k++)
    for (i = 0; i < d->s[k].n; i++) {
      double xv = d->s[k].x ? d->s[k].x[i] : i + 1, yv = d->s[k].v[i];
      if (CV_IS_NONE(yv) || CV_IS_NONE(xv)) continue;
      rng_add(&gx, xv); rng_add(&gy, yv);
      if (d->s[k].size && d->s[k].size[i] > smax) smax = d->s[k].size[i];
    }
  xlo = gx.lo; xhi = gx.hi; ylo = gy.lo; yhi = gy.hi;
  if (!gx.any) { xlo = 0; xhi = 1; ylo = 0; yhi = 1; }
  if (ylo > 0) ylo = 0;
  if (xlo > 0) xlo = 0;
  if (c->ytitle && *c->ytitle) { text(r, CE_YAXIS, x + r->fs * 3 / 4, y + h / 2, c->ytitle, r->fs, r->text, 1, 1, 1, 1); x += r->fs * 3 / 2 + 2; w -= r->fs * 3 / 2 + 2; }
  if (c->xtitle && *c->xtitle) { h -= r->fs * 3 / 2 + 2; text(r, CE_XAXIS, x + w / 2, y + h + 2, c->xtitle, r->fs, r->text, 1, 1, 0, 0); }
  axis_scale(r, &c->va, ylo, yhi, h, &p.min, &p.max, &p.major);
  axis_scale(r, &c->ha, xlo, xhi, w, &xmin, &xmax, &xmaj);
  lwid = vlabels_width(r, p.min, p.max, p.major, 0);
  p.x = x + lwid + 6; p.y = y + r->fs / 2 + 2;
  p.w = w - (p.x - x) - 10; p.h = h - (p.y - y) - r->fs * 6 / 5 - 4;
  if (p.w < 10) p.w = 10;
  if (p.h < 10) p.h = 10;
  rect(r, CE_PLOT, p.x, p.y, p.w, p.h, c->efill[CE_PLOT] ? c->efill[CE_PLOT] : (c->style == 4 ? FILLRGB(0xF2F2F2) : 0), c->eline[CE_PLOT]);
  draw_value_axis(r, &p, 0);
  /* the X axis: a value axis across */
  q = p; q.horiz = 1; q.min = xmin; q.max = xmax; q.major = xmaj;
  {
    double v;
    char t[64];
    int n = 0;
    for (v = xmin; v <= xmax + xmaj * 1e-6 && n < 60; v += xmaj, n++) {
      double px = vpos(&q, v, 0);
      if (c->vgrid) line2(r, CE_NONE, px, p.y, px, p.y + p.h, GRID_RGB, 1);
      general(q_abs(v) < xmaj * 1e-9 ? 0 : v, t, sizeof t);
      text(r, CE_XAXIS, px, p.y + p.h + 3, t, r->fs, r->text, 0, 1, 0, 0);
    }
    line2(r, CE_XAXIS, p.x, vpos(&p, p.min < 0 && p.max > 0 ? 0 : p.min, 0), p.x + p.w, vpos(&p, p.min < 0 && p.max > 0 ? 0 : p.min, 0), AXIS_RGB, 1);
  }
  xy = (double *)malloc(MAXPTS * 2 * sizeof(double));
  if (!xy) r->l->oom = 1;
  for (k = 0; k < d->ns && xy; k++) {
    u32 rgb = d->s[k].rgb;
    int n = 0;
    for (i = 0; i < d->s[k].n; i++) {
      double xv = d->s[k].x ? d->s[k].x[i] : i + 1, yv = d->s[k].v[i];
      if (CV_IS_NONE(yv) || CV_IS_NONE(xv)) continue;
      xy[n++] = vpos(&q, xv, 0); xy[n++] = vpos(&p, yv, 0);
    }
    if (c->kind == CK_SCATTERLINE && n >= 4) pts_prim(r, PR_LINE, (u16)(CE_SERIES + k), xy, n / 2, 0, FILLRGB(c->s[k].line ? c->s[k].line : rgb), lw);
    for (i = 0; i < n / 2; i++) {
      if (c->kind == CK_BUBBLE) {
        double sz = d->s[k].size ? d->s[k].size[i] : 1, rr = smax > 0 && sz > 0 ? q_sqrt(sz / smax) * (p.w < p.h ? p.w : p.h) / 8 : 2;
        ellipse(r, (u16)(CE_SERIES + k), xy[2 * i], xy[2 * i + 1], rr, rr, FILLRGB(rgb), FILLRGB(tint(rgb, -0.3)));
      } else if (!c->s[k].nomarker) marker(r, (u16)(CE_SERIES + k), xy[2 * i], xy[2 * i + 1], k, rgb, c->style == 3 ? 4 : 3);
    }
    if (c->labels) {
      int j = 0;
      for (i = 0; i < d->s[k].n; i++) {
        double yv = d->s[k].v[i], xv = d->s[k].x ? d->s[k].x[i] : i + 1;
        if (CV_IS_NONE(yv) || CV_IS_NONE(xv)) continue;
        label_at(r, xy[2 * j] + 4, xy[2 * j + 1] - 3, yv, 0, 2, 0);
        j++;
      }
    }
  }
  free(xy);
}
static void draw_radar(R *r, double x, double y, double w, double h)
{
  const Chart *c = r->c;
  CData *d = r->d;
  int n = d->ncat, k, i, t;
  double cx = x + w / 2, cy = y + h / 2 + r->fs / 2, rad = (w < h ? w : h) / 2 - r->fs * 2, min, max, major, lo = 0, hi;
  double *xy;
  Rng g;
  if (n < 1 || rad < 8) return;
  memset(&g, 0, sizeof g);
  rng_add(&g, 0);
  for (k = 0; k < d->ns; k++) for (i = 0; i < d->s[k].n; i++) if (!CV_IS_NONE(d->s[k].v[i])) rng_add(&g, d->s[k].v[i]);
  lo = g.lo; hi = g.hi;
  if (hi <= 0) hi = 1;
  axis_scale(r, &c->va, lo, hi, rad, &min, &max, &major);
  xy = (double *)malloc((size_t)(n + 1) * 2 * sizeof(double));
  if (!xy) { r->l->oom = 1; return; }
  /* the web: a polygon at each tick, a spoke to each category */
  for (t = 0; t <= 20 && min + t * major <= max + major * 1e-6; t++) {
    double rr = (t * major) / (max - min) * rad;
    char lab[64];
    for (i = 0; i < n; i++) { double a = 2 * M_PI_ * i / n; xy[2 * i] = cx + rr * q_sin(a); xy[2 * i + 1] = cy - rr * q_cos(a); }
    xy[2 * n] = xy[0]; xy[2 * n + 1] = xy[1];
    if (c->hgrid || t == 0) pts_prim(r, PR_LINE, CE_NONE, xy, n + 1, 0, FILLRGB(GRID_RGB), 1);
    fmtnum(r, min + t * major, 0, lab, sizeof lab);
    text(r, CE_YAXIS, cx - 3, cy - rr, lab, r->fs * 9 / 10, r->text, 0, 2, 1, 0);
  }
  for (i = 0; i < n; i++) {
    double a = 2 * M_PI_ * i / n, s = q_sin(a), co = q_cos(a);
    line2(r, CE_NONE, cx, cy, cx + rad * s, cy - rad * co, GRID_RGB, 1);
    text(r, CE_XAXIS, cx + (rad + 4) * s, cy - (rad + 4) * co, d->cat[i], r->fs, r->text, 0, s > 0.2 ? 0 : s < -0.2 ? 2 : 1, co > 0.2 ? 2 : co < -0.2 ? 0 : 1, 0);
  }
  rect(r, CE_PLOT, cx - rad, cy - rad, 2 * rad, 2 * rad, 0, 0);
  for (k = 0; k < d->ns; k++) {
    int m = 0;
    for (i = 0; i < n; i++) {
      double v = val(d, k, i), a = 2 * M_PI_ * i / n, rr;
      if (CV_IS_NONE(v)) v = min;
      rr = (v - min) / (max - min) * rad;
      xy[m++] = cx + rr * q_sin(a); xy[m++] = cy - rr * q_cos(a);
    }
    xy[m++] = xy[0]; xy[m++] = xy[1];
    pts_prim(r, PR_LINE, (u16)(CE_SERIES + k), xy, m / 2, 0, FILLRGB(c->s[k].line ? c->s[k].line : d->s[k].rgb), c->style == 3 ? 3 : 2);
    for (i = 0; i < n; i++) {
      if (c->style != 3) marker(r, (u16)(CE_SERIES + k), xy[2 * i], xy[2 * i + 1], k, d->s[k].rgb, 3);
      if (c->labels) label_at(r, xy[2 * i] + 4, xy[2 * i + 1] - 3, val(d, k, i), 0, 2, 0);
    }
  }
  free(xy);
}
/* ------------------------------------------------------------------ one-series charts */
static void draw_pie(R *r, double x, double y, double w, double h, int doughnut)
{
  const Chart *c = r->c;
  CData *d = r->d;
  double tot = 0, a = 0, cx = x + w / 2, cy = y + h / 2, rad = (w < h ? w : h) / 2 - 4, hole = (c->hole ? c->hole : 50) / 100.0;
  int i, n = d->ns ? d->s[0].n : 0;
  u32 sep = c->style == 5 ? FILLRGB(0x404040) : FILLRGB(0xFFFFFF);
  if (rad < 4 || !n) return;
  for (i = 0; i < n; i++) if (!CV_IS_NONE(d->s[0].v[i]) && d->s[0].v[i] > 0) tot += d->s[0].v[i];
  rect(r, CE_PLOT, cx - rad, cy - rad, 2 * rad, 2 * rad, 0, 0);
  if (tot <= 0) { ellipse(r, CE_PLOT, cx, cy, rad, rad, 0, FILLRGB(GRID_RGB)); return; }
  for (i = 0; i < n; i++) {
    double v = d->s[0].v[i], a1;
    u32 rgb = palette_rgb(c, i, n);
    if (CV_IS_NONE(v) || v <= 0) continue;
    a1 = a + 2 * M_PI_ * v / tot;
    wedge(r, (u16)CE_SERIES, cx, cy, rad, doughnut ? rad * hole : 0, a, a1, FILLRGB(rgb), sep);
    if (c->labels) {
      double m = (a + a1) / 2, lr = doughnut ? rad * (1 + hole) / 2 : rad * 0.62;
      char t[64];
      fmtnum(r, v, 0, t, sizeof t);
      text(r, CE_NONE, cx + lr * q_sin(m), cy - lr * q_cos(m), t, r->fs, 0xFFFFFF, 1, 1, 1, 0);
    }
    a = a1;
  }
}
static void draw_funnel(R *r, double x, double y, double w, double h)
{
  CData *d = r->d;
  int i, n = d->ns ? d->s[0].n : 0;
  double mx = 0, lw = 0, slot, bx, bw;
  if (!n) return;
  for (i = 0; i < n; i++) { double v = d->s[0].v[i]; if (!CV_IS_NONE(v) && q_abs(v) > mx) mx = q_abs(v); if (i < d->ncat && tw(r, d->cat[i], r->fs, 0) > lw) lw = tw(r, d->cat[i], r->fs, 0); }
  if (lw > w / 3) lw = w / 3;
  slot = h / n; bx = x + lw + 8; bw = w - lw - 12;
  rect(r, CE_PLOT, bx, y, bw, h, 0, 0);
  for (i = 0; i < n; i++) {
    double v = d->s[0].v[i], len, cy = y + i * slot;
    char t[64];
    if (i < d->ncat) text(r, CE_XAXIS, bx - 4, cy + slot / 2, d->cat[i], r->fs, r->text, 0, 2, 1, 0);
    if (CV_IS_NONE(v) || mx <= 0) continue;
    len = q_abs(v) / mx * bw;
    rect(r, (u16)CE_SERIES, bx + (bw - len) / 2, cy + slot * 0.1, len, slot * 0.8, FILLRGB(d->s[0].rgb), 0);
    fmtnum(r, v, 0, t, sizeof t);
    text(r, CE_NONE, bx + bw / 2, cy + slot / 2, t, r->fs, 0xFFFFFF, 0, 1, 1, 0);
  }
}
/* squarified treemap of positive values into x, y, w, h */
typedef struct { double v; int i; } TItem;
static int titem_cmp(const void *a, const void *b) { double x = ((const TItem *)a)->v, y = ((const TItem *)b)->v; return x > y ? -1 : x < y; }
static double worst(const TItem *it, int n, double len, double scale)
{
  double s = 0, mn = BIG, mx = 0, rr;
  int i;
  for (i = 0; i < n; i++) { double a = it[i].v * scale; s += a; if (a < mn) mn = a; if (a > mx) mx = a; }
  if (s <= 0 || len <= 0) return BIG;
  rr = (len * len * mx) / (s * s);
  { double r2 = (s * s) / (len * len * mn); return rr > r2 ? rr : r2; }
}
static void draw_treemap(R *r, double x, double y, double w, double h)
{
  CData *d = r->d;
  int n = d->ns ? d->s[0].n : 0, m = 0, i, start;
  TItem *it;
  double tot = 0, scale;
  if (!n || !(it = (TItem *)malloc((size_t)n * sizeof(TItem)))) return;
  for (i = 0; i < n; i++) { double v = d->s[0].v[i]; if (!CV_IS_NONE(v) && v > 0) { it[m].v = v; it[m].i = i; m++; tot += v; } }
  qsort(it, (size_t)m, sizeof(TItem), titem_cmp);
  rect(r, CE_PLOT, x, y, w, h, 0, 0);
  if (tot <= 0) { free(it); return; }
  scale = w * h / tot;
  start = 0;
  while (start < m) {
    double len = w < h ? w : h, rowsum = 0, thick, off;
    int end = start + 1, k;
    while (end < m && worst(it + start, end + 1 - start, len, scale) <= worst(it + start, end - start, len, scale)) end++;
    for (k = start; k < end; k++) rowsum += it[k].v * scale;
    thick = rowsum / len;
    off = 0;
    for (k = start; k < end; k++) {
      double a = it[k].v * scale, l = a / thick, rx, ry, rw, rh;
      u32 rgb = palette_rgb(r->c, it[k].i, n);
      if (w >= h) { rx = x; ry = y + off; rw = thick; rh = l; } else { rx = x + off; ry = y; rw = l; rh = thick; }
      rect(r, (u16)CE_SERIES, rx, ry, rw, rh, FILLRGB(rgb), FILLRGB(0xFFFFFF));
      if (it[k].i < d->ncat && rw > 20 && rh > r->fs + 4) {
        text(r, CE_NONE, rx + 3, ry + 2, d->cat[it[k].i], r->fs, 0xFFFFFF, 0, 0, 0, 0);
        if (r->c->labels && rh > 2 * r->fs + 6) { char t[64]; fmtnum(r, it[k].v, 0, t, sizeof t); text(r, CE_NONE, rx + 3, ry + 3 + r->fs * 6 / 5, t, r->fs, 0xFFFFFF, 0, 0, 0, 0); }
      }
      off += l;
    }
    if (w >= h) { x += thick; w -= thick; } else { y += thick; h -= thick; }
    start = end;
  }
  free(it);
}
/* a kind Quill doesn't draw: its plot area, crossed faintly, with what it is */
static void draw_other(R *r, double x, double y, double w, double h)
{
  char t[80];
  rect(r, CE_PLOT, x, y, w, h, FILLRGB(0xF2F2F2), FILLRGB(GRID_RGB));
  line2(r, CE_NONE, x, y, x + w, y + h, GRID_RGB, 1);
  line2(r, CE_NONE, x + w, y, x, y + h, GRID_RGB, 1);
  snprintf(t, sizeof t, "%s", r->c->kindname[0] ? r->c->kindname : "Chart");
  text(r, CE_PLOT, x + w / 2, y + h / 2, t, r->fs, r->text, 0, 1, 1, 0);
}

/* ------------------------------------------------------------------ the whole chart */
int chart_render(const Chart *c, int w, int h, TextWidthFn twf, void *ctx, PrimList *out)
{
  R r;
  CData d;
  LegEnt ents[64];
  int ne = 0;
  double x0 = 6, y0 = 6, x1 = w - 6, y1 = h - 6;
  const char *title = NULL;
  char tbuf[256];
  memset(&r, 0, sizeof r);
  r.c = c; r.d = &d; r.l = out; r.tw = twf; r.ctx = ctx;
  r.fs = 13; r.fst = 18;
  /* small charts: smaller text, as Word's would be scaled down to fit */
  if (w < 260 || h < 160) { r.fs = 11; r.fst = 14; }
  r.dark = c->style == 5;
  r.text = r.dark ? 0xF2F2F2 : TEXT_RGB;
  if (w < 4 || h < 4) return 1;
  if (!chart_resolve(c, &d)) return 0;
  /* the chart area */
  rect(&r, CE_CHART, 0, 0, w, h, c->efill[CE_CHART] ? c->efill[CE_CHART] : FILLRGB(r.dark ? 0x404040 : 0xFFFFFF), c->eline[CE_CHART] == CE_NOLINE ? 0 : c->eline[CE_CHART] ? c->eline[CE_CHART] : FILLRGB(GRID_RGB));
  /* the title */
  if (c->has_title) {
    if (c->title && *c->title) title = c->title;
    else if (d.ns == 1 && d.s[0].name && *d.s[0].name && c->s[0].name) title = d.s[0].name;
    else title = "Chart Title";
    snprintf(tbuf, sizeof tbuf, "%s", title);
    if (c->efill[CE_TITLE] || c->eline[CE_TITLE]) {
      i32 tw0 = tw(&r, tbuf, r.fst, 0);
      rect(&r, CE_TITLE, w / 2.0 - tw0 / 2.0 - 4, y0, tw0 + 8, r.fst * 6 / 5 + 2, c->efill[CE_TITLE], c->eline[CE_TITLE]);
    }
    text(&r, CE_TITLE, w / 2.0, y0 + 1, tbuf, r.fst, r.dark ? 0xFFFFFF : DARK_RGB, 0, 1, 0, 0);
    y0 += r.fst * 6 / 5 + 6;
  }
  /* the legend */
  if (c->legend != LG_NONE && c->kind != CK_OTHER) ne = legend_entries(&r, ents, 64);
  if (ne) {
    int line = r.fs * 13 / 10, i;
    if (c->legend == LG_LEFT || c->legend == LG_RIGHT) {
      i32 lwid = 0;
      for (i = 0; i < ne; i++) { i32 t = tw(&r, ents[i].label, r.fs, 0) + r.fs + 12; if (t > lwid) lwid = t; }
      if (lwid > (x1 - x0) / 3) lwid = (i32)((x1 - x0) / 3);
      if (c->legend == LG_RIGHT) { draw_legend(&r, ents, ne, x1 - lwid, y0, lwid, y1 - y0, 1); x1 -= lwid + 4; }
      else { draw_legend(&r, ents, ne, x0, y0, lwid, y1 - y0, 1); x0 += lwid + 4; }
    } else {
      /* rows: as many as the entries need */
      double roww = 0, avail = x1 - x0;
      int rows = 1;
      for (i = 0; i < ne; i++) {
        double ew = r.fs * 7 / 10 + 1 + 8 + tw(&r, ents[i].label, r.fs, 0) + r.fs / 2 + 2;
        if (roww + ew > avail && roww > 0) { rows++; roww = 0; }
        roww += ew;
      }
      if (rows > 3) rows = 3;
      if (c->legend == LG_BOTTOM) { draw_legend(&r, ents, ne, x0, y1 - rows * line - 2, x1 - x0, rows * line + 2, 0); y1 -= rows * line + 6; }
      else { draw_legend(&r, ents, ne, x0, y0, x1 - x0, rows * line + 2, 0); y0 += rows * line + 6; }
    }
  }
  if (x1 - x0 > 8 && y1 - y0 > 8) {
    if (c->kind == CK_OTHER) draw_other(&r, x0, y0, x1 - x0, y1 - y0);
    else if (c->kind == CK_PIE || c->kind == CK_DOUGHNUT) draw_pie(&r, x0, y0, x1 - x0, y1 - y0, c->kind == CK_DOUGHNUT);
    else if (c->kind == CK_SUNBURST) draw_pie(&r, x0, y0, x1 - x0, y1 - y0, 1);
    else if (c->kind == CK_FUNNEL) draw_funnel(&r, x0, y0, x1 - x0, y1 - y0);
    else if (c->kind == CK_TREEMAP) draw_treemap(&r, x0, y0, x1 - x0, y1 - y0);
    else if (c->kind == CK_RADAR) draw_radar(&r, x0, y0, x1 - x0, y1 - y0);
    else if (is_xy(c->kind)) draw_xy(&r, x0, y0, x1 - x0, y1 - y0);
    else draw_cartesian(&r, x0, y0, x1 - x0, y1 - y0);
  }
  cdata_free(&d);
  return !out->oom;
}

/* ------------------------------------------------------------------ reading a chart part */
/* a colour element (srgbClr, schemeClr, sysClr, prstClr) into PRIM_ON | rgb (0 if it isn't one), with
   its shade, tint and luminance changes (as docx.c's dml_color). x is at its START and is read to
   its END: the element is taken off the stack s then */
static u32 clr_base(Xml *x, const u32 *theme)
{
  static const char *const tn[12] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
  static const u32 office[12] = { 0, 0xFFFFFF, 0x44546A, 0xE7E6E6, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 };
  const char *v = xml_attr(x, "val");
  if (!strcmp(x->name, "srgbClr") && v && strlen(v) >= 6) return FILLRGB(strtoul(v, NULL, 16));
  if (!strcmp(x->name, "sysClr")) { const char *l = xml_attr(x, "lastClr"); return FILLRGB(l ? strtoul(l, NULL, 16) : (v && !strcmp(v, "window") ? 0xFFFFFF : 0)); }
  if (!strcmp(x->name, "schemeClr") && v) {
    const char *m = !strcmp(v, "tx1") ? "dk1" : !strcmp(v, "bg1") ? "lt1" : !strcmp(v, "tx2") ? "dk2" : !strcmp(v, "bg2") ? "lt2" : v;
    int k;
    for (k = 0; k < 12; k++) if (!strcmp(m, tn[k])) return FILLRGB(theme && theme[k] != 0xFF000000UL ? theme[k] : office[k]);
    return FILLRGB(0);
  }
  if (!strcmp(x->name, "prstClr") && v) {
    static const struct { const char *n; u32 c; } p[] = { { "black", 0 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 } };
    int i;
    for (i = 0; i < (int)(sizeof p / sizeof p[0]); i++) if (!strcmp(p[i].n, v)) return FILLRGB(p[i].c);
    return FILLRGB(0);
  }
  return 0;
}
u32 chart_dml_color(Xml *x, const u32 *theme)
{
  int d0 = x->depth;
  u32 col = clr_base(x, theme);
  long shade = 100000, tnt = 100000, lmod = 100000, loff = 0;
  long r, g, b;
  if (x->pending_end) { xml_next(x); return col; }
  while (xml_next(x) != XT_EOF) {
    const char *v;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START || !(v = xml_attr(x, "val"))) continue;
    if (!strcmp(x->name, "shade")) shade = atol(v);
    else if (!strcmp(x->name, "tint")) tnt = atol(v);
    else if (!strcmp(x->name, "lumMod")) lmod = atol(v);
    else if (!strcmp(x->name, "lumOff")) loff = atol(v);
  }
  if (!col) return 0;
  r = (long)((col >> 16) & 255); g = (long)((col >> 8) & 255); b = (long)(col & 255);
  if (shade != 100000) { r = r * shade / 100000; g = g * shade / 100000; b = b * shade / 100000; }
  if (tnt != 100000) { r = 255 - (255 - r) * tnt / 100000; g = 255 - (255 - g) * tnt / 100000; b = 255 - (255 - b) * tnt / 100000; }
  if (lmod != 100000 || loff) {
    r = r * lmod / 100000 + 255 * loff / 100000; g = g * lmod / 100000 + 255 * loff / 100000; b = b * lmod / 100000 + 255 * loff / 100000;
  }
  r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
  return FILLRGB(((u32)r << 16) | ((u32)g << 8) | (u32)b);
}
static int is_clr(const char *n) { return !strcmp(n, "srgbClr") || !strcmp(n, "schemeClr") || !strcmp(n, "sysClr") || !strcmp(n, "prstClr"); }

/* an element stack, for knowing where in the XML a token is */
#define STK 40
typedef struct { char n[STK][32]; int sp; } Stk;
static void stk_push(Stk *s, const char *name) { if (s->sp < STK) { strncpy(s->n[s->sp], name, 31); s->n[s->sp][31] = 0; } s->sp++; }
static void stk_pop(Stk *s) { if (s->sp > 0) s->sp--; }
/* the name k levels up from the current element (0: the current one) */
static const char *up(const Stk *s, int k) { int i = s->sp - 1 - k; return i >= 0 && i < STK ? s->n[i] : ""; }
static int within(const Stk *s, const char *name) { int i; for (i = 0; i < s->sp && i < STK; i++) if (!strcmp(s->n[i], name)) return 1; return 0; }
static int within_from(const Stk *s, const char *name, int from) { int i; for (i = from; i < s->sp && i < STK; i++) if (!strcmp(s->n[i], name)) return 1; return 0; }
static u32 clr(Xml *x, const u32 *theme, Stk *s) { u32 c = chart_dml_color(x, theme); stk_pop(s); return c; }
static void set_str(char **p, const char *s) { free(*p); *p = s ? str_dup(s) : NULL; }
static int truthy(const char *v) { return v && (atoi(v) || !strcmp(v, "true")); }

/* a cache being read (strCache, numCache, numLit, strLit, cx:lvl): its points by index */
typedef struct { int count; char **s; double *v; int cap; int isnum; char fmt[32]; } Cache;
static void cache_reset(Cache *c, int isnum)
{
  int i;
  for (i = 0; i < c->cap; i++) free(c->s ? c->s[i] : NULL);
  free(c->s); free(c->v);
  memset(c, 0, sizeof *c);
  c->isnum = isnum;
}
static void cache_put(Cache *c, int idx, const char *t, size_t n)
{
  if (idx < 0 || idx >= MAXPTS) return;
  if (idx >= c->cap) {
    int nc = idx + 16, i;
    char **ns = (char **)realloc(c->s, (size_t)nc * sizeof(char *));
    double *nv;
    if (!ns) return;
    c->s = ns;
    if (!(nv = (double *)realloc(c->v, (size_t)nc * sizeof(double)))) return;
    c->v = nv;
    for (i = c->cap; i < nc; i++) { c->s[i] = NULL; c->v[i] = CV_NONE; }
    c->cap = nc;
  }
  free(c->s[idx]);
  c->s[idx] = str_ndup(t ? t : "", n);
  c->v[idx] = q_atof(c->s[idx] ? c->s[idx] : "");
  if (idx + 1 > c->count) c->count = idx + 1;
}
/* the cache's points as labels (malloc'd array of count strings) */
static char **cache_texts(Cache *c, int *n)
{
  char **r;
  int i;
  *n = 0;
  if (c->count <= 0 || !(r = (char **)calloc((size_t)c->count, sizeof(char *)))) return NULL;
  for (i = 0; i < c->count; i++) {
    const char *s = i < c->cap && c->s[i] ? c->s[i] : "";
    char t[64];
    if (c->isnum && i < c->cap && c->s[i] && !CV_IS_NONE(c->v[i]) && c->fmt[0]) { chart_fmt_value(c->v[i], c->fmt, t, sizeof t); s = t; }
    r[i] = str_dup(s);
    if (!r[i]) { strs_free(r, i); return NULL; }
  }
  *n = c->count;
  return r;
}
static double *cache_nums(Cache *c, int *n)
{
  double *r;
  int i;
  *n = 0;
  if (c->count <= 0 || !(r = (double *)malloc((size_t)c->count * sizeof(double)))) return NULL;
  for (i = 0; i < c->count; i++) r[i] = i < c->cap && c->s[i] ? c->v[i] : CV_NONE;
  *n = c->count;
  return r;
}

typedef struct { int id, cat, pos, grid, del; char title[200]; CAxis ax; } RAx;
static Chart *read_chart(const char *data, size_t len, const u32 *theme)
{
  Xml x;
  Stk st;
  Chart *c = chart_new(CK_COL);
  RAx ax[6];
  int nax = 0, k, i;
  int gkind = CK_COL, gfirst = 0, gbar = 0, gdir_bar = 0;
  int grp_axval[8], grp_first[8], grp_kind[8], grp_n = 0, anymark = 0, gaxis = 0;
  char grouping[24] = "", other[32] = "", sstyle[24] = "";
  int lnnone = 0;
  int has_title_el = 0, autodel = -1, fmtset = 0;
  Buf tb;
  Cache cache;
  int titledepth = -1, ser = -1, serdepth = -1, ptidx = -1, cachedepth = -1;
  if (!c) return NULL;
  c->legend = LG_NONE; c->hgrid = 0; c->has_title = 0;
  memset(&st, 0, sizeof st);
  memset(ax, 0, sizeof ax);
  memset(&cache, 0, sizeof cache);
  memset(grp_axval, 0, sizeof grp_axval);
  buf_init(&tb);
  if (theme) for (k = 0; k < 6; k++) c->accent[k] = theme[4 + k] != 0xFF000000UL ? FILLRGB(theme[4 + k]) : 0;
  xml_init(&x, data, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      const char *n = x.name, *v = xml_attr(&x, "val");
      stk_push(&st, n);
      /* the chart groups */
      if (st.sp >= 2 && !strcmp(up(&st, 1), "plotArea") && strstr(n, "Chart")) {
        gfirst = c->ns; gbar = 0; grouping[0] = 0; gaxis = 0;
        if (!strcmp(n, "barChart") || !strcmp(n, "bar3DChart")) { gkind = CK_COL; gbar = 1; gdir_bar = 0; }
        else if (!strcmp(n, "lineChart") || !strcmp(n, "line3DChart")) { gkind = CK_LINE; anymark = 0; }
        else if (!strcmp(n, "pieChart") || !strcmp(n, "pie3DChart") || !strcmp(n, "ofPieChart")) gkind = CK_PIE;
        else if (!strcmp(n, "doughnutChart")) gkind = CK_DOUGHNUT;
        else if (!strcmp(n, "areaChart") || !strcmp(n, "area3DChart")) gkind = CK_AREA;
        else if (!strcmp(n, "scatterChart")) gkind = CK_SCATTER;
        else if (!strcmp(n, "bubbleChart")) gkind = CK_BUBBLE;
        else if (!strcmp(n, "radarChart")) gkind = CK_RADAR;
        else if (!strcmp(n, "stockChart")) gkind = CK_STOCK;
        else { gkind = CK_OTHER; if (!other[0]) snprintf(other, sizeof other, "%.31s", n); }
      }
      else if (!strcmp(n, "barDir") && v) gdir_bar = !strcmp(v, "bar");
      else if (!strcmp(n, "grouping") && v) snprintf(grouping, sizeof grouping, "%s", v);
      else if (!strcmp(n, "scatterStyle") && v) snprintf(sstyle, sizeof sstyle, "%s", v);
      else if (ser >= 0 && !strcmp(n, "noFill") && st.sp == serdepth + 3 && !strcmp(up(&st, 1), "ln") && !strcmp(up(&st, 2), "spPr")) lnnone++;
      else if (!strcmp(n, "holeSize") && v) { int hs = atoi(v); c->hole = (u8)(hs >= 10 && hs <= 90 ? hs : 50); }
      else if (!strcmp(n, "ser") && strstr(up(&st, 1), "Chart")) {
        if (!chart_add_series(c)) { xml_skip(&x); stk_pop(&st); continue; }
        ser = c->ns - 1; serdepth = st.sp;
      }
      else if (!strcmp(n, "title") && titledepth < 0) {
        titledepth = st.sp; tb.len = 0; if (tb.data) tb.data[0] = 0;
        if (!within(&st, "valAx") && !within(&st, "catAx") && !within(&st, "dateAx") && !within(&st, "serAx")) has_title_el = 1;
      }
      else if (!strcmp(n, "autoTitleDeleted") && v) autodel = truthy(v);
      else if (!strcmp(n, "legendPos") && v) c->legend = !strcmp(v, "t") ? LG_TOP : !strcmp(v, "l") ? LG_LEFT : !strcmp(v, "b") ? LG_BOTTOM : LG_RIGHT;
      else if (!strcmp(n, "legend") && c->legend == LG_NONE) c->legend = LG_RIGHT;
      else if ((!strcmp(n, "showVal") || !strcmp(n, "showPercent")) && truthy(v) && within(&st, "dLbls") && !within(&st, "dLbl")) c->labels = 1;
      else if ((!strcmp(n, "catAx") || !strcmp(n, "valAx") || !strcmp(n, "dateAx")) && nax < 6) { memset(&ax[nax], 0, sizeof ax[nax]); ax[nax].cat = strcmp(n, "valAx") != 0; ax[nax].id = -1; nax++; }
      else if (nax && st.sp >= 2 && (!strcmp(up(&st, 1), "catAx") || !strcmp(up(&st, 1), "valAx") || !strcmp(up(&st, 1), "dateAx"))) {
        RAx *a = &ax[nax - 1];
        if (!strcmp(n, "axId") && v) a->id = atoi(v);
        else if (!strcmp(n, "axPos") && v) a->pos = v[0];
        else if (!strcmp(n, "majorGridlines")) a->grid = 1;
        else if (!strcmp(n, "delete") && v) a->del = truthy(v);
        else if (!strcmp(n, "majorUnit") && v) { a->ax.majset = 1; a->ax.major = q_atof(v); }
        else if (!strcmp(n, "numFmt") && !a->cat && !fmtset) {
          const char *fc = xml_attr(&x, "formatCode"), *sl = xml_attr(&x, "sourceLinked");
          if (fc && !truthy(sl)) { snprintf(c->fmt, sizeof c->fmt, "%s", fc); fmtset = 1; }
        }
      }
      else if (nax && (!strcmp(n, "min") || !strcmp(n, "max")) && !strcmp(up(&st, 1), "scaling") && v) {
        RAx *a = &ax[nax - 1];
        if (n[1] == 'i') { a->ax.minset = 1; a->ax.min = q_atof(v); } else { a->ax.maxset = 1; a->ax.max = q_atof(v); }
      }
      else if (!strcmp(n, "axId") && v && st.sp >= 2 && strstr(up(&st, 1), "Chart") && grp_n < 8) {
        /* a group's axes: the second is its value axis */
        if (gaxis++ == 1) grp_axval[grp_n] = atoi(v);
      }
      else if (ser >= 0 && !strcmp(n, "symbol") && v && !strcmp(up(&st, 1), "marker") && st.sp == serdepth + 2) { if (!strcmp(v, "none")) c->s[ser].nomarker = 1; else anymark = 1; }
      else if (ser >= 0 && !strcmp(n, "smooth") && v && st.sp == serdepth + 1) c->s[ser].smooth = (u8)truthy(v);
      else if (ser >= 0 && is_clr(n) && st.sp == serdepth + 3 && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "spPr")) c->s[ser].color = clr(&x, theme, &st);
      else if (ser >= 0 && is_clr(n) && st.sp == serdepth + 4 && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "ln") && !strcmp(up(&st, 3), "spPr")) c->s[ser].line = clr(&x, theme, &st);
      else if (ser >= 0 && (!strcmp(n, "strCache") || !strcmp(n, "numCache") || !strcmp(n, "numLit") || !strcmp(n, "strLit"))) {
        cache_reset(&cache, n[0] == 'n'); cachedepth = st.sp;
      }
      else if (cachedepth >= 0 && !strcmp(n, "pt")) { const char *ix = xml_attr(&x, "idx"); ptidx = ix ? atoi(ix) : -1; }
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "spPr")) {
        /* the chart area, plot area, legend or title's fill (and outline below) */
        const char *owner = up(&st, 3);
        int e = !strcmp(owner, "chartSpace") ? CE_CHART : !strcmp(owner, "plotArea") ? CE_PLOT : !strcmp(owner, "legend") ? CE_LEGEND : !strcmp(owner, "title") && !within(&st, "valAx") && !within(&st, "catAx") ? CE_TITLE : -1;
        if (e >= 0) c->efill[e] = clr(&x, theme, &st);
      }
      else if (is_clr(n) && !strcmp(up(&st, 1), "solidFill") && !strcmp(up(&st, 2), "ln") && !strcmp(up(&st, 3), "spPr")) {
        const char *owner = up(&st, 4);
        int e = !strcmp(owner, "chartSpace") ? CE_CHART : !strcmp(owner, "plotArea") ? CE_PLOT : !strcmp(owner, "legend") ? CE_LEGEND : !strcmp(owner, "title") && !within(&st, "valAx") && !within(&st, "catAx") ? CE_TITLE : -1;
        if (e >= 0) c->eline[e] = clr(&x, theme, &st);
      }
      else if (!strcmp(n, "noFill") && !strcmp(up(&st, 1), "ln") && !strcmp(up(&st, 2), "spPr") && !strcmp(up(&st, 3), "chartSpace")) c->eline[CE_CHART] = CE_NOLINE;
      else if (!strcmp(n, "style") && v && st.sp <= 2) { int sv = atoi(v); c->style = (u8)(sv >= 2 && sv <= 9 ? sv - 1 : 1); }
      else if (!strcmp(n, "latin") && !c->font[0]) { const char *t = xml_attr(&x, "typeface"); if (t && t[0]) snprintf(c->font, sizeof c->font, "%.47s", t); }
      continue;
    }
    if (x.type == XT_TEXT) {
      const char *n = up(&st, 0);
      if (titledepth >= 0 && (!strcmp(n, "t") || (!strcmp(n, "v") && within(&st, "strCache")))) buf_add(&tb, x.text.data, x.text.len);
      else if (cachedepth >= 0 && !strcmp(n, "v") && !strcmp(up(&st, 1), "pt")) cache_put(&cache, ptidx, x.text.data, x.text.len);
      else if (cachedepth >= 0 && !strcmp(n, "formatCode")) snprintf(cache.fmt, sizeof cache.fmt, "%.*s", (int)(x.text.len < 31 ? x.text.len : 31), x.text.data ? x.text.data : "");
      else if (ser >= 0 && !strcmp(n, "v") && !strcmp(up(&st, 1), "tx")) { char *t = str_ndup(x.text.data ? x.text.data : "", x.text.len); if (t) { free(c->s[ser].name); c->s[ser].name = t; } }
      continue;
    }
    if (x.type == XT_END) {
      const char *n = up(&st, 0);
      if (titledepth >= 0 && st.sp == titledepth) {
        if (has_title_el && !within(&st, "valAx") && !within(&st, "catAx") && !within(&st, "dateAx")) { c->has_title = 1; set_str(&c->title, tb.len ? tb.data : NULL); }
        else if (nax) snprintf(ax[nax - 1].title, sizeof ax[nax - 1].title, "%s", tb.data ? tb.data : "");
        titledepth = -1; has_title_el = 0;
      }
      if (cachedepth >= 0 && st.sp == cachedepth) {
        /* a cache read: it is the series' name, labels, values, X values or bubble sizes */
        CSeries *s = ser >= 0 ? &c->s[ser] : NULL;
        const char *part = up(&st, 2);    /* tx / cat / val / xVal / yVal / bubbleSize (above strRef or numRef) */
        if (!strcmp(n, "numLit") || !strcmp(n, "strLit")) part = up(&st, 1);
        if (s && within_from(&st, "ser", serdepth - 1)) {
          int cnt = 0;
          if (!strcmp(part, "tx")) {
            char **t = cache_texts(&cache, &cnt);
            if (t && cnt) {
              Buf nb;
              buf_init(&nb);
              for (i = 0; i < cnt; i++) { if (i && t[i][0]) buf_adds(&nb, " "); buf_adds(&nb, t[i]); }
              if (nb.data) { free(s->name); s->name = buf_take(&nb); }
              buf_free(&nb);
            }
            strs_free(t, cnt);
          } else if (!strcmp(part, "cat")) { strs_free(s->cat, s->ncat); s->cat = cache_texts(&cache, &s->ncat); }
          else if (!strcmp(part, "xVal")) {
            if (cache.isnum) { free(s->x); s->x = cache_nums(&cache, &s->nx); }
            else { strs_free(s->cat, s->ncat); s->cat = cache_texts(&cache, &s->ncat); }
          }
          else if (!strcmp(part, "val") || !strcmp(part, "yVal")) {
            free(s->v); s->v = cache_nums(&cache, &s->nv);
            if (!fmtset && cache.fmt[0] && strcmp(cache.fmt, "General")) { snprintf(c->fmt, sizeof c->fmt, "%s", cache.fmt); fmtset = 2; }
          }
          else if (!strcmp(part, "bubbleSize")) { free(s->size); s->size = cache_nums(&cache, &s->nsize); }
        }
        cachedepth = -1;
        cache_reset(&cache, 0);
      }
      if (ser >= 0 && st.sp == serdepth && !strcmp(n, "ser")) { ser = -1; serdepth = -1; }
      if (strstr(n, "Chart") && strcmp(n, "chart") && grp_n < 8 && st.sp >= 2 && !strcmp(up(&st, 1), "plotArea")) {
        int kk = gkind;
        if (gbar) {
          int stk = !strcmp(grouping, "stacked"), pct = !strcmp(grouping, "percentStacked");
          kk = gdir_bar ? (pct ? CK_BAR100 : stk ? CK_BARSTACK : CK_BAR) : (pct ? CK_COL100 : stk ? CK_COLSTACK : CK_COL);
        } else if (gkind == CK_LINE) kk = !strcmp(grouping, "stacked") || !strcmp(grouping, "percentStacked") ? CK_LINESTACK : anymark ? CK_LINEMARK : CK_LINE;
        else if (gkind == CK_AREA) kk = !strcmp(grouping, "stacked") ? CK_AREASTACK : !strcmp(grouping, "percentStacked") ? CK_AREA100 : CK_AREA;
        grp_kind[grp_n] = kk; grp_first[grp_n] = gfirst;
        grp_n++;
        gbar = 0;
      }
      stk_pop(&st);
    }
  }
  xml_free(&x);
  cache_reset(&cache, 0);
  if (grp_n == 0) { chart_free(c); buf_free(&tb); return NULL; }
  if (grp_n == 1) c->kind = (u8)grp_kind[0];
  else {
    int anyother = 0;
    for (k = 0; k < grp_n; k++) if (grp_kind[k] == CK_OTHER) anyother = 1;
    c->kind = anyother ? CK_OTHER : CK_COMBO;
    for (k = 0; k < grp_n && !anyother; k++) {
      int end = k + 1 < grp_n ? grp_first[k + 1] : c->ns;
      for (i = grp_first[k]; i < end; i++) { c->s[i].kind = (u8)grp_kind[k]; c->s[i].secondary = (u8)(grp_axval[k] != grp_axval[0]); }
    }
  }
  if (c->kind == CK_OTHER) {
    /* what it is, for its frame: "surface3DChart" says "3-D Surface" */
    static const struct { const char *tag, *name; } kn[] = { { "surfaceChart", "Surface" }, { "surface3DChart", "3-D Surface" }, { "ofPieChart", "Pie of Pie" } };
    size_t q;
    snprintf(c->kindname, sizeof c->kindname, "Chart");
    for (q = 0; q < sizeof kn / sizeof kn[0]; q++) if (!strcmp(other, kn[q].tag)) snprintf(c->kindname, sizeof c->kindname, "%s", kn[q].name);
  }
  /* the axes onto the chart */
  for (k = 0; k < nax; k++) {
    RAx *a = &ax[k];
    int vertical = a->pos == 'l' || a->pos == 'r';
    if (a->del && k >= 2) continue;
    if (!a->cat) {
      if ((c->kind == CK_SCATTER || c->kind == CK_SCATTERLINE || c->kind == CK_BUBBLE) && !vertical) { c->ha = a->ax; c->vgrid = (u8)a->grid; }
      else if (k < 2 || grp_n == 1) { c->va = a->ax; c->hgrid = (u8)a->grid; }
    } else if (k < 2) c->vgrid = (u8)a->grid;
    if (a->title[0] && k < 3) set_str((vertical && !is_bar(c->kind)) || (!vertical && is_bar(c->kind) && !a->cat) ? &c->ytitle : &c->xtitle, a->title);
  }
  if (!c->has_title && autodel == 0 && c->ns == 1 && grp_n == 1 && chart_is_pielike(c->kind)) c->has_title = 1;
  /* scatter: lines unless the series' lines are off (Word writes ln noFill for markers only) */
  if (c->kind == CK_SCATTER) {
    if (strcmp(sstyle, "marker") && lnnone < c->ns) c->kind = CK_SCATTERLINE;
  }
  if (c->kind == CK_PIE || c->kind == CK_DOUGHNUT) for (k = 0; k < c->ns; k++) c->s[k].color = c->s[k].line = 0;
  buf_free(&tb);
  return c;
}
/* Excel 2016's charts (chartex): the data are in cx:chartData by id, the series refer to them */
static Chart *read_chartex(const char *data, size_t len, const u32 *theme)
{
  Xml x;
  Stk st;
  Chart *c = chart_new(CK_COL);
  typedef struct { char id[16]; Cache cat, val; } Dat;
  Dat dat[16];
  int nd = 0, k, kind = -1, dimcat = -1, intitle = 0, ptidx = -1, inlvl = 0;
  Buf tb;
  char sid[16] = "", sv[300] = "";
  int inser = 0;
  if (!c) return NULL;
  c->legend = LG_NONE; c->has_title = 0; c->hgrid = 0;
  memset(&st, 0, sizeof st);
  memset(dat, 0, sizeof dat);
  buf_init(&tb);
  if (theme) for (k = 0; k < 6; k++) c->accent[k] = theme[4 + k] != 0xFF000000UL ? FILLRGB(theme[4 + k]) : 0;
  xml_init(&x, data, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      const char *n = x.name;
      stk_push(&st, n);
      if (!strcmp(n, "data") && nd < 16 && within(&st, "chartData")) { const char *id = xml_attr(&x, "id"); snprintf(dat[nd].id, sizeof dat[nd].id, "%s", id ? id : ""); nd++; }
      else if ((!strcmp(n, "strDim") || !strcmp(n, "numDim")) && nd) {
        const char *t = xml_attr(&x, "type");
        dimcat = t && (!strcmp(t, "cat") || !strcmp(t, "x")) ? 1 : t && (!strcmp(t, "val") || !strcmp(t, "y") || !strcmp(t, "size")) ? 0 : -1;
        if (dimcat >= 0) cache_reset(dimcat ? &dat[nd - 1].cat : &dat[nd - 1].val, n[0] == 'n');
      }
      else if (!strcmp(n, "lvl") && nd && dimcat >= 0) {
        Cache *ca = dimcat ? &dat[nd - 1].cat : &dat[nd - 1].val;
        const char *fc = xml_attr(&x, "formatCode");
        inlvl = ca->count == 0;        /* (the first level only: the innermost labels) */
        if (fc && inlvl) snprintf(ca->fmt, sizeof ca->fmt, "%s", fc);
      }
      else if (!strcmp(n, "pt") && inlvl) { const char *ix = xml_attr(&x, "idx"); ptidx = ix ? atoi(ix) : -1; }
      else if (!strcmp(n, "series")) {
        const char *l = xml_attr(&x, "layoutId");
        int kk = !l ? -1 : !strcmp(l, "clusteredColumn") ? CK_HISTOGRAM : !strcmp(l, "waterfall") ? CK_WATERFALL : !strcmp(l, "funnel") ? CK_FUNNEL :
                 !strcmp(l, "treemap") ? CK_TREEMAP : !strcmp(l, "sunburst") ? CK_SUNBURST : !strcmp(l, "boxWhisker") ? CK_BOXWHISKER : CK_OTHER;
        if (l && !strcmp(l, "paretoLine")) { xml_skip(&x); stk_pop(&st); continue; }
        if (kind < 0) { kind = kk; if (kk == CK_OTHER && l) snprintf(c->kindname, sizeof c->kindname, "%s", l); }
        inser = 1; sid[0] = 0; sv[0] = 0;
      }
      else if (!strcmp(n, "dataId") && inser) { const char *v = xml_attr(&x, "val"); snprintf(sid, sizeof sid, "%s", v ? v : ""); }
      else if (!strcmp(n, "title") && !within(&st, "axis")) { intitle = 1; tb.len = 0; if (tb.data) tb.data[0] = 0; c->has_title = 1; }
      else if (!strcmp(n, "legend")) { const char *p = xml_attr(&x, "pos"); c->legend = !p ? LG_RIGHT : p[0] == 'b' ? LG_BOTTOM : p[0] == 't' ? LG_TOP : p[0] == 'l' ? LG_LEFT : LG_RIGHT; }
      else if (!strcmp(n, "majorGridlines")) c->hgrid = 1;
      else if (!strcmp(n, "latin") && !c->font[0]) { const char *t = xml_attr(&x, "typeface"); if (t && t[0]) snprintf(c->font, sizeof c->font, "%.47s", t); }
      else if (!strcmp(n, "visibility") && within(&st, "dataLabels")) { const char *v = xml_attr(&x, "value"); if (v && truthy(v)) c->labels = 1; }
      continue;
    }
    if (x.type == XT_TEXT) {
      const char *n = up(&st, 0);
      if (intitle && (!strcmp(n, "v") || !strcmp(n, "t"))) buf_add(&tb, x.text.data, x.text.len);
      else if (inlvl && !strcmp(n, "pt") && nd && dimcat >= 0) cache_put(dimcat ? &dat[nd - 1].cat : &dat[nd - 1].val, ptidx, x.text.data, x.text.len);
      else if (inser && !strcmp(n, "v") && within(&st, "tx")) snprintf(sv, sizeof sv, "%.*s", (int)(x.text.len < 299 ? x.text.len : 299), x.text.data ? x.text.data : "");
      continue;
    }
    if (x.type == XT_END) {
      const char *n = up(&st, 0);
      if (!strcmp(n, "lvl")) inlvl = 0;
      if (!strcmp(n, "strDim") || !strcmp(n, "numDim")) dimcat = -1;
      if (!strcmp(n, "title") && intitle) { intitle = 0; set_str(&c->title, tb.len ? tb.data : NULL); }
      if (!strcmp(n, "series") && inser) {
        CSeries *s = chart_add_series(c);
        inser = 0;
        if (s) {
          for (k = 0; k < nd; k++)
            if (!strcmp(dat[k].id, sid)) {
              s->cat = cache_texts(&dat[k].cat, &s->ncat);
              s->v = cache_nums(&dat[k].val, &s->nv);
              if (dat[k].val.fmt[0]) snprintf(c->fmt, sizeof c->fmt, "%s", dat[k].val.fmt);
            }
          if (sv[0]) s->name = str_dup(sv);
        }
      }
      stk_pop(&st);
    }
  }
  xml_free(&x);
  buf_free(&tb);
  for (k = 0; k < 16; k++) { cache_reset(&dat[k].cat, 0); cache_reset(&dat[k].val, 0); }
  if (kind < 0 || !c->ns) {
    /* a kind or data not understood (a region map...): drawn as a frame with its title */
    if (kind < 0 && !c->has_title) { chart_free(c); return NULL; }
    if (kind < 0) kind = CK_OTHER;
  }
  c->kind = (u8)kind;
  if (c->kind == CK_OTHER && !c->kindname[0]) strcpy(c->kindname, "Chart");
  return c;
}
Chart *chart_read(const char *xml, size_t len, int ex, const u32 *theme)
{
  if (!xml || chart_nofp) return NULL;
  return ex ? read_chartex(xml, len, theme) : read_chart(xml, len, theme);
}
