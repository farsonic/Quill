/* raster.c - see raster.h (WP29) */
#include <stdlib.h>
#include <string.h>
#include "raster.h"
#include "ttraster.h"
#include "image.h"

typedef long long i64;
#define FX 65536                       /* 16.16: numbers, matrices, device points */

typedef struct { i64 a, b, c, d, e, f; } Mat;
/* a glyph drawn once, kept for its next use */
typedef struct { int font; u16 g; i32 ppem; TTBitmap bm; u8 used; } GCache;
#define GCN 512
#define MAXPTS 4096
typedef struct {
  Mat ctm;
  u8 fill[3], stroke[3];
  i64 lw;                              /* line width, user units (16.16) */
  int cap;
  i64 dash[8]; int ndash; i64 phase;
  i32 cx0, cy0, cx1, cy1;              /* the clip: a box of device pixels */
  i64 tc, th, fs;                      /* character spacing, horizontal scale (1.0 = FX), font size */
  int font, tr;
} GState;
typedef struct { i32 x0, y0, x1, y1; int dir; } Edge;
typedef struct {
  u8 *rgb; i32 W, H;
  GState gs[16]; int sp;
  /* the path being built: device points (24.8), each subpath's first point and whether it is closed */
  i32 *px, *py; int np;
  int sub[512]; u8 closed[512]; int nsub;
  i64 curx, cury;                      /* the current point, user units */
  int clip_next;                       /* W seen: the next painting clips */
  Edge *e; int ne, ecap;
  u16 *acc;
  i32 *xs; int *xd; int xcap;
  /* text */
  Mat tm, tlm;
  const RFont *fonts; int nfonts;
  TTRaster *tt; u8 *ttok;              /* each font's outlines (opened when first wanted: 1 open, 2 can't) */
  GCache *gc;
  const Image *const *imgs; int nimgs;
} R;

static i64 mulfx(i64 a, i64 b) { return (a * b) / FX; }
static i32 fdiv8(i64 v) { return (i32)(v >= 0 ? v / 256 : -((-v + 255) / 256)); }   /* 16.16 -> 24.8, rounded down */
static i64 isqrt64(i64 v)
{
  i64 r = 0, bit = (i64)1 << 62;
  if (v <= 0) return 0;
  while (bit > v) bit >>= 2;
  while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
  return r;
}
/* m = n x m (PDF's cm: the new matrix first) */
static void concat(Mat *m, const Mat *n)
{
  Mat r;
  r.a = mulfx(n->a, m->a) + mulfx(n->b, m->c);
  r.b = mulfx(n->a, m->b) + mulfx(n->b, m->d);
  r.c = mulfx(n->c, m->a) + mulfx(n->d, m->c);
  r.d = mulfx(n->c, m->b) + mulfx(n->d, m->d);
  r.e = mulfx(n->e, m->a) + mulfx(n->f, m->c) + m->e;
  r.f = mulfx(n->e, m->b) + mulfx(n->f, m->d) + m->f;
  *m = r;
}
static void xform(const Mat *m, i64 x, i64 y, i64 *ox, i64 *oy)
{
  *ox = mulfx(m->a, x) + mulfx(m->c, y) + m->e;
  *oy = mulfx(m->b, x) + mulfx(m->d, y) + m->f;
}
/* how much a matrix scales lengths (the square root of its determinant), 16.16 */
static i64 mat_scale(const Mat *m)
{
  i64 det = mulfx(m->a, m->d) - mulfx(m->b, m->c);
  if (det < 0) det = -det;
  return isqrt64(det * FX);
}

/* ---- filling: exact coverage across, four sample rows down a pixel, the non-zero rule */
static void edge_add(R *r, i32 x0, i32 y0, i32 x1, i32 y1)
{
  Edge *e;
  if (y0 == y1) return;
  if (r->ne == r->ecap) {
    int nc = r->ecap ? r->ecap * 2 : 256;
    Edge *ne = (Edge *)realloc(r->e, (size_t)nc * sizeof(Edge));
    if (!ne) return;
    r->e = ne; r->ecap = nc;
  }
  e = &r->e[r->ne++];
  if (y0 < y1) { e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1; e->dir = 1; }
  else { e->x0 = x1; e->y0 = y1; e->x1 = x0; e->y1 = y0; e->dir = -1; }
}
/* a closed polygon of n device points (24.8) */
static void poly_add(R *r, const i32 *x, const i32 *y, int n)
{
  int i;
  for (i = 0; i < n; i++) edge_add(r, x[i], y[i], x[(i + 1) % n], y[(i + 1) % n]);
}
/* the edges collected so far filled with colour c, inside the clip; then forgotten */
static void fill_edges(R *r, const u8 *c)
{
  GState *g = &r->gs[r->sp];
  i32 ymin = 0x7FFFFFFF, ymax = -0x7FFFFFFF, xmin = 0x7FFFFFFF, xmax = -0x7FFFFFFF, row, y0, y1, x0, x1;
  int i;
  if (!r->ne) return;
  for (i = 0; i < r->ne; i++) {
    Edge *e = &r->e[i];
    if (e->y0 < ymin) ymin = e->y0;
    if (e->y1 > ymax) ymax = e->y1;
    if (e->x0 < xmin) xmin = e->x0;
    if (e->x1 < xmin) xmin = e->x1;
    if (e->x0 > xmax) xmax = e->x0;
    if (e->x1 > xmax) xmax = e->x1;
  }
  y0 = ymin >> 8; y1 = (ymax + 255) >> 8; x0 = xmin >> 8; x1 = (xmax + 255) >> 8;
  if (y0 < g->cy0) y0 = g->cy0;
  if (y1 > g->cy1) y1 = g->cy1;
  if (x0 < g->cx0) x0 = g->cx0;
  if (x1 > g->cx1) x1 = g->cx1;
  if (x1 <= x0 || y1 <= y0) { r->ne = 0; return; }
  if (r->xcap < r->ne) {
    int *nd = (int *)realloc(r->xd, (size_t)r->ne * sizeof(int));
    i32 *nx = nd ? (i32 *)realloc(r->xs, (size_t)r->ne * sizeof(i32)) : NULL;
    if (nd) r->xd = nd;
    if (!nd || !nx) { r->ne = 0; return; }
    r->xs = nx; r->xcap = r->ne;
  }
  for (row = y0; row < y1; row++) {
    int s;
    u8 *px;
    for (s = 0; s < 4; s++) {
      i32 sy = row * 256 + 32 + s * 64;
      int n = 0, k, wind = 0;
      i32 start = 0;
      for (i = 0; i < r->ne; i++) {
        Edge *e = &r->e[i];
        if (sy < e->y0 || sy >= e->y1) continue;
        {
          i32 x = e->x0 + (i32)((i64)(sy - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0));
          int dd = e->dir;
          for (k = n; k > 0 && r->xs[k - 1] > x; k--) { r->xs[k] = r->xs[k - 1]; r->xd[k] = r->xd[k - 1]; }
          r->xs[k] = x; r->xd[k] = dd; n++;
        }
      }
      for (k = 0; k < n; k++) {
        int was = wind;
        wind += r->xd[k];
        if (!was && wind) start = r->xs[k];
        else if (was && !wind) {
          i32 a = start, b = r->xs[k], pa, pb, q;
          if (a < x0 * 256) a = x0 * 256;
          if (b > x1 * 256) b = x1 * 256;
          if (b <= a) continue;
          pa = a >> 8; pb = b >> 8;
          if (pa == pb) r->acc[pa - x0] += (u16)(b - a);
          else {
            r->acc[pa - x0] += (u16)(256 - (a & 255));
            for (q = pa + 1; q < pb; q++) r->acc[q - x0] += 256;
            if (pb < x1) r->acc[pb - x0] += (u16)(b & 255);
          }
        }
      }
    }
    px = r->rgb + ((size_t)row * r->W + x0) * 3;
    for (i = 0; i < x1 - x0; i++, px += 3) {
      u32 a = r->acc[i];
      if (!a) continue;
      r->acc[i] = 0;
      a >>= 2;
      if (a >= 256) { px[0] = c[0]; px[1] = c[1]; px[2] = c[2]; }
      else { px[0] = (u8)(px[0] + (((int)c[0] - px[0]) * (int)a >> 8)); px[1] = (u8)(px[1] + (((int)c[1] - px[1]) * (int)a >> 8)); px[2] = (u8)(px[2] + (((int)c[2] - px[2]) * (int)a >> 8)); }
    }
  }
  r->ne = 0;
}

/* ---- stroking: each segment a box, caps and joins round or square, all turned the same way so
   overlaps add up (the non-zero rule fills them once) */
static i64 poly_area(const i32 *x, const i32 *y, int n)
{
  i64 s = 0;
  int i;
  for (i = 0; i < n; i++) s += (i64)x[i] * y[(i + 1) % n] - (i64)x[(i + 1) % n] * y[i];
  return s;
}
static void poly_same_way(R *r, i32 *x, i32 *y, int n)
{
  if (poly_area(x, y, n) > 0) {
    int i;
    for (i = 0; i < n / 2; i++) { i32 t = x[i]; x[i] = x[n - 1 - i]; x[n - 1 - i] = t; t = y[i]; y[i] = y[n - 1 - i]; y[n - 1 - i] = t; }
  }
  poly_add(r, x, y, n);
}
/* a round dot (an octagon) of radius h at x, y (24.8) */
static void dot(R *r, i32 x, i32 y, i32 h)
{
  static const int ox[8] = { 256, 181, 0, -181, -256, -181, 0, 181 }, oy[8] = { 0, 181, 256, 181, 0, -181, -256, -181 };
  i32 px[8], py[8];
  int i;
  for (i = 0; i < 8; i++) { px[i] = x + (i32)((i64)ox[i] * h / 256); py[i] = y + (i32)((i64)oy[i] * h / 256); }
  poly_same_way(r, px, py, 8);
}
/* one stretch of line from a to b (24.8), h half its width, with the caps (cap) at both ends */
static void seg(R *r, i32 ax, i32 ay, i32 bx, i32 by, i32 h, int cap)
{
  i64 dx = bx - ax, dy = by - ay, l = isqrt64(dx * dx + dy * dy);
  i32 x[4], y[4], nx, ny, ex = 0, ey = 0;
  if (cap == 1) { dot(r, ax, ay, h); dot(r, bx, by, h); }
  if (l == 0) {
    if (cap == 2) { x[0] = ax - h; y[0] = ay - h; x[1] = ax + h; y[1] = ay - h; x[2] = ax + h; y[2] = ay + h; x[3] = ax - h; y[3] = ay + h; poly_same_way(r, x, y, 4); }
    return;
  }
  nx = (i32)(-dy * h / l); ny = (i32)(dx * h / l);
  if (cap == 2) { ex = (i32)(dx * h / l); ey = (i32)(dy * h / l); }
  x[0] = ax - ex + nx; y[0] = ay - ey + ny;
  x[1] = bx + ex + nx; y[1] = by + ey + ny;
  x[2] = bx + ex - nx; y[2] = by + ey - ny;
  x[3] = ax - ex - nx; y[3] = ay - ey - ny;
  poly_same_way(r, x, y, 4);
}
/* a segment cut into dashes: the pattern's place (which dash, how much of it is left, whether it is
   drawn) carries on from segment to segment */
typedef struct { const i64 *dash; int n, di, on; i64 left; } Dash;
static void dash_seg(R *r, i32 ax, i32 ay, i32 bx, i32 by, i32 h, int cap, Dash *ds)
{
  i64 dx = bx - ax, dy = by - ay, l = isqrt64(dx * dx + dy * dy), pos = 0;
  int guard = 0;
  if (l == 0) return;
#define AT(q) ax + (i32)(dx * (q) / l), ay + (i32)(dy * (q) / l)
  while (pos < l && guard++ < 100000) {
    i64 step = ds->left < l - pos ? ds->left : l - pos;
    if (ds->on && step > 0) seg(r, AT(pos), AT(pos + step), h, cap);
    pos += step; ds->left -= step;
    while (ds->left <= 0 && guard++ < 100000) {
      ds->di = (ds->di + 1) % ds->n; ds->left += ds->dash[ds->di]; ds->on = !ds->on;
      if (ds->on && ds->dash[ds->di] == 0 && cap) seg(r, AT(pos), AT(pos), h, cap);      /* a dot */
    }
  }
#undef AT
}
static void stroke_path(R *r)
{
  GState *g = &r->gs[r->sp];
  i64 sc = mat_scale(&g->ctm), tot = 0;
  i32 h = (i32)(mulfx(g->lw, sc) / 512);          /* half the width, 24.8 */
  i64 dash[8];
  int s, i, nd = g->ndash;
  if (h < 128) h = 128;                            /* at least a pixel wide */
  for (i = 0; i < nd; i++) { dash[i] = mulfx(g->dash[i], sc) / 256; if (dash[i] < 0) dash[i] = 0; tot += dash[i]; }   /* 24.8 */
  if (tot <= 0) nd = 0;
  for (s = 0; s < r->nsub; s++) {
    int a = r->sub[s], b = s + 1 < r->nsub ? r->sub[s + 1] : r->np, n = b - a, k;
    Dash ds;
    memset(&ds, 0, sizeof ds);
    if (n < 1) continue;
    if (nd) {
      /* where the pattern starts: the phase into it */
      i64 ph = (mulfx(g->phase, sc) / 256) % tot;
      ds.dash = dash; ds.n = nd; ds.di = 0; ds.on = 1; ds.left = dash[0];
      if (ph == 0 && dash[0] == 0 && g->cap) seg(r, r->px[a], r->py[a], r->px[a], r->py[a], h, g->cap);
      while (ph >= ds.left && ph > 0) { ph -= ds.left; ds.di = (ds.di + 1) % nd; ds.left = dash[ds.di]; ds.on = !ds.on; }
      ds.left -= ph;
      while (ds.left <= 0) { ds.di = (ds.di + 1) % nd; ds.left += dash[ds.di]; ds.on = !ds.on; }
    }
    if (n == 1) { if (g->cap) seg(r, r->px[a], r->py[a], r->px[a], r->py[a], h, g->cap); continue; }
    for (k = 0; k < n - 1 + (r->closed[s] ? 1 : 0); k++) {
      i32 ax = r->px[a + k], ay = r->py[a + k], bx = r->px[a + (k + 1) % n], by = r->py[a + (k + 1) % n];
      if (nd) { dash_seg(r, ax, ay, bx, by, h, g->cap, &ds); continue; }
      seg(r, ax, ay, bx, by, h, g->cap);
      if (k > 0 || r->closed[s]) {
        /* the join: a square round the corner, along the line coming in (a mitre at right angles) */
        int pk = (k + n - 1) % n;
        i64 dx = ax - r->px[a + pk], dy = ay - r->py[a + pk], l = isqrt64(dx * dx + dy * dy);
        if (l) seg(r, ax - (i32)(dx * h / l), ay - (i32)(dy * h / l), ax + (i32)(dx * h / l), ay + (i32)(dy * h / l), h, 0);
      }
    }
  }
  fill_edges(r, g->stroke);
}
static void fill_path(R *r)
{
  int s;
  for (s = 0; s < r->nsub; s++) {
    int a = r->sub[s], b = s + 1 < r->nsub ? r->sub[s + 1] : r->np;
    if (b - a >= 2) poly_add(r, r->px + a, r->py + a, b - a);
  }
  fill_edges(r, r->gs[r->sp].fill);
}
static void path_point(R *r, i64 x, i64 y, int newsub)
{
  i64 dx, dy;
  if (r->np >= MAXPTS || (newsub && r->nsub >= 512)) return;
  r->curx = x; r->cury = y;
  xform(&r->gs[r->sp].ctm, x, y, &dx, &dy);
  if (newsub || !r->nsub) { r->sub[r->nsub] = r->np; r->closed[r->nsub] = 0; r->nsub++; }
  r->px[r->np] = fdiv8(dx); r->py[r->np] = fdiv8(dy); r->np++;
}
/* the path's box clips what follows (W n) */
static void clip_path(R *r)
{
  GState *g = &r->gs[r->sp];
  i32 x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF;
  int i;
  for (i = 0; i < r->np; i++) {
    if (r->px[i] < x0) x0 = r->px[i];
    if (r->px[i] > x1) x1 = r->px[i];
    if (r->py[i] < y0) y0 = r->py[i];
    if (r->py[i] > y1) y1 = r->py[i];
  }
  if (!r->np) { g->cx1 = g->cx0; return; }
  x0 = (x0 + 128) >> 8; y0 = (y0 + 128) >> 8; x1 = (x1 + 128) >> 8; y1 = (y1 + 128) >> 8;
  if (x0 > g->cx0) g->cx0 = x0;
  if (y0 > g->cy0) g->cy0 = y0;
  if (x1 < g->cx1) g->cx1 = x1;
  if (y1 < g->cy1) g->cy1 = y1;
  if (g->cx1 < g->cx0) g->cx1 = g->cx0;
  if (g->cy1 < g->cy0) g->cy1 = g->cy0;
}

/* ---- pictures: the unit square through the matrix, each pixel inside it looked up */
static void picture(R *r, const Image *im)
{
  GState *g = &r->gs[r->sp];
  Mat *m = &g->ctm;
  i64 lu = isqrt64(mulfx(m->a, m->a) + mulfx(m->b, m->b)) , lv = isqrt64(mulfx(m->c, m->c) + mulfx(m->d, m->d)), det;
  i64 cx[4], cy[4], du_dx, du_dy, dv_dx, dv_dy;
  u32 iw, ih;
  i32 x0, x1, y0, y1, X, Y;
  u8 *rgb;
  int k;
  if (!im || !im->data) return;
  /* (lengths of the sides in pixels: sqrt of 16.16 squared gives 8.8) */
  iw = (u32)(lu >> 8) + 1; ih = (u32)(lv >> 8) + 1;
  if (im->pw && iw > im->pw) iw = im->pw;
  if (im->ph && ih > im->ph) ih = im->ph;
  if (iw > 4000) iw = 4000;
  if (ih > 4000) ih = 4000;
  det = mulfx(m->a, m->d) - mulfx(m->b, m->c);
  if (!det || !(rgb = img_decode(im->data, im->len, iw, ih))) return;
  xform(m, 0, 0, &cx[0], &cy[0]); xform(m, FX, 0, &cx[1], &cy[1]); xform(m, 0, FX, &cx[2], &cy[2]); xform(m, FX, FX, &cx[3], &cy[3]);
  x0 = y0 = 0x7FFFFFFF; x1 = y1 = -0x7FFFFFFF;
  for (k = 0; k < 4; k++) {
    i32 px = (i32)(cx[k] >> 16), py = (i32)(cy[k] >> 16);
    if (px < x0) x0 = px;
    if (px + 1 > x1) x1 = px + 1;
    if (py < y0) y0 = py;
    if (py + 1 > y1) y1 = py + 1;
  }
  if (x0 < g->cx0) x0 = g->cx0;
  if (y0 < g->cy0) y0 = g->cy0;
  if (x1 > g->cx1) x1 = g->cx1;
  if (y1 > g->cy1) y1 = g->cy1;
  /* u, v (32 bits of fraction) as the pixel moves: the inverse matrix */
  du_dx = m->d * ((i64)1 << 32) / det; du_dy = -m->c * ((i64)1 << 32) / det;
  dv_dx = -m->b * ((i64)1 << 32) / det; dv_dy = m->a * ((i64)1 << 32) / det;
  for (Y = y0; Y < y1; Y++) {
    i64 dxp = ((i64)x0 * FX + FX / 2) - m->e, dyp = ((i64)Y * FX + FX / 2) - m->f;
    i64 u = (dxp * du_dx + dyp * du_dy) / FX, v = (dxp * dv_dx + dyp * dv_dy) / FX;
    u8 *px = r->rgb + ((size_t)Y * r->W + x0) * 3;
    for (X = x0; X < x1; X++, px += 3, u += du_dx, v += dv_dx) {
      i64 col, rw;
      const u8 *s;
      if (u < 0 || v < 0 || u >= ((i64)1 << 32) || v >= ((i64)1 << 32)) continue;
      col = (u * iw) >> 32; rw = (i64)ih - 1 - ((v * ih) >> 32);
      if (rw < 0) rw = 0;
      s = rgb + ((size_t)rw * iw + (size_t)col) * 3;
      px[0] = s[0]; px[1] = s[1]; px[2] = s[2];
    }
  }
  free(rgb);
}

/* ---- text */
static const u16 cp1252_hi[32] = {
  0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
  0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
/* glyph g of font fi at ppem (26.6), drawn once and kept */
static TTBitmap *glyph(R *r, int fi, u16 g, i32 ppem)
{
  u32 h = ((u32)fi * 31 + g * 7 + (u32)ppem) % GCN;
  GCache *c = &r->gc[h];
  if (c->used && c->font == fi && c->g == g && c->ppem == ppem) return &c->bm;
  if (!r->ttok[fi]) r->ttok[fi] = ttr_open(&r->tt[fi], r->fonts[fi].face->path, r->fonts[fi].face->index) ? 1 : 2;
  if (r->ttok[fi] != 1) return NULL;
  if (c->used) { free(c->bm.cov); c->used = 0; }
  memset(&c->bm, 0, sizeof c->bm);
  if (!ttr_glyph(&r->tt[fi], g, ppem, &c->bm)) return NULL;
  c->font = fi; c->g = g; c->ppem = ppem; c->used = 1;
  return &c->bm;
}
/* a glyph's coverage laid on the page in colour c, its pen at device X, Y (16.16) for an upright
   size, else through the text rendering matrix m (em units to device) by looking each pixel up */
static void blend(R *r, i32 x, i32 y, const u8 *c, u32 a)
{
  GState *g = &r->gs[r->sp];
  u8 *px;
  if (x < g->cx0 || x >= g->cx1 || y < g->cy0 || y >= g->cy1 || !a) return;
  px = r->rgb + ((size_t)y * r->W + x) * 3;
  if (a >= 255) { px[0] = c[0]; px[1] = c[1]; px[2] = c[2]; return; }
  a += a >> 7;
  px[0] = (u8)(px[0] + (((int)c[0] - px[0]) * (int)a >> 8)); px[1] = (u8)(px[1] + (((int)c[1] - px[1]) * (int)a >> 8)); px[2] = (u8)(px[2] + (((int)c[2] - px[2]) * (int)a >> 8));
}
static void draw_glyph(R *r, int fi, u16 g, const Mat *m, i32 bold)
{
  GState *gs = &r->gs[r->sp];
  int upright = m->b == 0 && m->c == 0 && m->a > 0 && (m->a + m->d < m->a / 64 && m->a + m->d > -m->a / 64);
  i64 sc = upright ? m->a : mat_scale(m);
  i32 ppem = (i32)(sc >> 10), i, j, k;
  TTBitmap *bm;
  if (ppem < 16 || ppem > 64 * 2000 || !(bm = glyph(r, fi, g, ppem)) || !bm->w) return;
  if (upright) {
    /* whole pixels down; across, the glyph shared between two columns by the pen's fraction */
    i32 ox = (i32)(m->e >> 16) + bm->left, oy = (i32)((m->f + FX / 2) >> 16) - bm->top, fr = (i32)((m->e & 0xFFFF) >> 8);
    for (k = -bold; k <= bold; k++)
      for (j = 0; j < bm->h; j++) {
        const u8 *row = bm->cov + j * bm->w;
        for (i = 0; i <= bm->w; i++) {
          u32 a = ((i < bm->w ? row[i] : 0) * (u32)(256 - fr) + (i ? row[i - 1] : 0) * (u32)fr) >> 8;
          blend(r, ox + i + k, oy + j, gs->fill, a);
        }
      }
    return;
  }
  {
    /* the glyph's box through the matrix, and back from each pixel: em = inverse(m) (pixel - origin);
       bitmap column = em.x * ppem - left, row = top - em.y * ppem */
    i64 det = mulfx(m->a, m->d) - mulfx(m->b, m->c), ex[4], ey[4], pp = (i64)ppem * FX / 64;
    i64 iu_x, iu_y, iv_x, iv_y;
    i32 x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF, X, Y;
    i64 gx0 = (i64)bm->left * FX * FX / pp, gx1 = (i64)(bm->left + bm->w) * FX * FX / pp, gy0 = (i64)(bm->top - bm->h) * FX * FX / pp, gy1 = (i64)bm->top * FX * FX / pp;
    if (!det) return;
    xform(m, gx0, gy0, &ex[0], &ey[0]); xform(m, gx1, gy0, &ex[1], &ey[1]); xform(m, gx0, gy1, &ex[2], &ey[2]); xform(m, gx1, gy1, &ex[3], &ey[3]);
    for (k = 0; k < 4; k++) {
      i32 px = (i32)(ex[k] >> 16), py = (i32)(ey[k] >> 16);
      if (px < x0) x0 = px;
      if (px + 1 > x1) x1 = px + 1;
      if (py < y0) y0 = py;
      if (py + 1 > y1) y1 = py + 1;
    }
    if (x0 < gs->cx0) x0 = gs->cx0;
    if (y0 < gs->cy0) y0 = gs->cy0;
    if (x1 > gs->cx1) x1 = gs->cx1;
    if (y1 > gs->cy1) y1 = gs->cy1;
    /* em per device pixel (16.16) */
    iu_x = m->d * FX / det; iu_y = -m->c * FX / det; iv_x = -m->b * FX / det; iv_y = m->a * FX / det;
    for (Y = y0; Y < y1; Y++)
      for (X = x0; X < x1; X++) {
        i64 dx = (i64)X * FX + FX / 2 - m->e, dy = (i64)Y * FX + FX / 2 - m->f;
        i64 u = (dx * iu_x + dy * iu_y) / FX, v = (dx * iv_x + dy * iv_y) / FX;   /* em, 16.16 */
        i64 col = (u * pp / FX - (i64)bm->left * FX) >> 16, row = ((i64)bm->top * FX - v * pp / FX) >> 16;
        if (col < 0 || row < 0 || col >= bm->w || row >= bm->h) continue;
        for (k = -bold; k <= bold; k++) blend(r, X + k, Y, gs->fill, bm->cov[row * bm->w + col]);
      }
  }
}
/* Tj: each code's glyph at the text matrix, which then moves on by the glyph's advance */
static void show_text(R *r, const u8 *s, int n, int hex)
{
  GState *g = &r->gs[r->sp];
  const RFont *rf;
  Face *f;
  int i, step = hex && g->font >= 0 && g->font < r->nfonts && !r->fonts[g->font].simple ? 2 : 1;
  i32 bold = 0;
  if (g->font < 0 || g->font >= r->nfonts) return;
  rf = &r->fonts[g->font]; f = rf->face;
  if (!f->upm || !f->adv) return;
  if (g->tr == 2) bold = (i32)((mulfx(g->lw, mat_scale(&g->ctm)) / 2 + FX / 2) >> 16);
  if (bold > 2) bold = 2;
  for (i = 0; i + step <= n; i += step) {
    u16 gid;
    Mat trm, sz;
    i64 adv, tx;
    if (step == 2) gid = (u16)(s[i] << 8 | s[i + 1]);
    else {
      u32 cp = s[i] >= 0x80 && s[i] < 0xA0 ? cp1252_hi[s[i] - 0x80] : s[i];
      gid = ttf_glyph(f, cp);
    }
    if (gid >= f->nglyphs) gid = 0;
    sz.a = mulfx(g->fs, g->th); sz.b = 0; sz.c = 0; sz.d = g->fs; sz.e = 0; sz.f = 0;
    trm = r->tm; concat(&trm, &sz);       /* (sz x tm) */
    {
      Mat c = g->ctm;
      concat(&c, &trm);                   /* trm x ctm */
      if (gid) draw_glyph(r, g->font, gid, &c, bold);
    }
    adv = (i64)f->adv[gid] * FX / f->upm;
    tx = mulfx(mulfx(adv, g->fs) + g->tc, g->th);
    r->tm.e += mulfx(tx, r->tm.a); r->tm.f += mulfx(tx, r->tm.b);
  }
}

/* ---- the content stream */
typedef struct { const char *s, *end; } Lex;
static int is_delim(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0 || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '/' || c == '%'; }
static i64 parse_num(const char *s, const char *end, const char **after)
{
  int neg = 0;
  i64 ip = 0, fp = 0, div = 1;
  if (s < end && (*s == '-' || *s == '+')) { neg = *s == '-'; s++; }
  while (s < end && *s >= '0' && *s <= '9') { ip = ip * 10 + (*s - '0'); s++; }
  if (s < end && *s == '.') { s++; while (s < end && *s >= '0' && *s <= '9') { if (div < 1000000) { fp = fp * 10 + (*s - '0'); div *= 10; } s++; } }
  *after = s;
  ip = ip * FX + fp * FX / div;
  return neg ? -ip : ip;
}
static void colour3(u8 *c, const i64 *v)
{
  int k;
  for (k = 0; k < 3; k++) { i64 x = v[k] * 255 / FX; c[k] = (u8)(x < 0 ? 0 : x > 255 ? 255 : x); }
}
static void run(R *r, const char *s, size_t len)
{
  const char *end = s + len;
  i64 num[16];
  int nn = 0, nameint = -1, arrn = 0, inarr = 0, strn = 0, hex = 0;
  i64 arr[8];
  u8 str[1024];
  while (s < end) {
    int c = (u8)*s;
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') { s++; continue; }
    if (c == '%') { while (s < end && *s != '\n') s++; continue; }
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
      i64 v = parse_num(s, end, &s);
      if (inarr) { if (arrn < 8) arr[arrn++] = v; }
      else if (nn < 16) num[nn++] = v;
      continue;
    }
    if (c == '[') { inarr = 1; arrn = 0; s++; continue; }
    if (c == ']') { inarr = 0; s++; continue; }
    if (c == '/') {
      const char *a = ++s;
      while (s < end && !is_delim((u8)*s)) s++;
      nameint = (s > a + 1 && (a[0] == 'F' || a[0] == 'I')) ? atoi(a[0] == 'F' ? a + 1 : a + 2) : -1;
      continue;
    }
    if (c == '(') {
      int depth = 1;
      s++; strn = 0; hex = 0;
      while (s < end && depth) {
        int ch = (u8)*s++;
        if (ch == '\\' && s < end) {
          ch = (u8)*s++;
          if (ch >= '0' && ch <= '7') { int v = ch - '0', k; for (k = 0; k < 2 && s < end && *s >= '0' && *s <= '7'; k++) v = v * 8 + (*s++ - '0'); ch = v & 255; }
          else if (ch == 'n') ch = '\n'; else if (ch == 'r') ch = '\r'; else if (ch == 't') ch = '\t';
        } else if (ch == '(') depth++;
        else if (ch == ')' && !--depth) break;
        if (strn < (int)sizeof str) str[strn++] = (u8)ch;
      }
      continue;
    }
    if (c == '<') {
      int hi = -1;
      s++; strn = 0; hex = 1;
      while (s < end && *s != '>') {
        int ch = (u8)*s++, v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (v < 0) continue;
        if (hi < 0) hi = v; else { if (strn < (int)sizeof str) str[strn++] = (u8)(hi << 4 | v); hi = -1; }
      }
      if (s < end) s++;
      continue;
    }
    {
      /* an operator */
      char op[4];
      int k = 0;
      GState *g = &r->gs[r->sp];
      while (s < end && !is_delim((u8)*s) && k < 3) op[k++] = *s++;
      while (s < end && !is_delim((u8)*s)) s++;
      op[k] = 0;
      if (!strcmp(op, "q")) { if (r->sp < 15) { r->gs[r->sp + 1] = r->gs[r->sp]; r->sp++; } }
      else if (!strcmp(op, "Q")) { if (r->sp) r->sp--; }
      else if (!strcmp(op, "cm") && nn >= 6) { Mat m; m.a = num[0]; m.b = num[1]; m.c = num[2]; m.d = num[3]; m.e = num[4]; m.f = num[5]; concat(&g->ctm, &m); }
      else if (!strcmp(op, "rg") && nn >= 3) colour3(g->fill, num);
      else if (!strcmp(op, "RG") && nn >= 3) colour3(g->stroke, num);
      else if (!strcmp(op, "w") && nn >= 1) g->lw = num[0];
      else if (!strcmp(op, "J") && nn >= 1) g->cap = (int)(num[0] / FX);
      else if (!strcmp(op, "d")) { int i; g->ndash = arrn; for (i = 0; i < arrn; i++) g->dash[i] = arr[i]; g->phase = nn ? num[0] : 0; }
      else if (!strcmp(op, "m") && nn >= 2) path_point(r, num[0], num[1], 1);
      else if (!strcmp(op, "l") && nn >= 2) path_point(r, num[0], num[1], 0);
      else if (!strcmp(op, "h")) { if (r->nsub) r->closed[r->nsub - 1] = 1; }
      else if (!strcmp(op, "re") && nn >= 4) {
        path_point(r, num[0], num[1], 1); path_point(r, num[0] + num[2], num[1], 0);
        path_point(r, num[0] + num[2], num[1] + num[3], 0); path_point(r, num[0], num[1] + num[3], 0);
        if (r->nsub) r->closed[r->nsub - 1] = 1;
      }
      else if (!strcmp(op, "W")) r->clip_next = 1;
      else if (!strcmp(op, "f") || !strcmp(op, "S") || !strcmp(op, "B") || !strcmp(op, "n")) {
        if (op[0] == 'f' || op[0] == 'B') fill_path(r);
        if (op[0] == 'S' || op[0] == 'B') stroke_path(r);
        if (r->clip_next) clip_path(r);
        r->clip_next = 0; r->np = 0; r->nsub = 0; r->ne = 0;
      }
      else if (!strcmp(op, "BT")) { memset(&r->tm, 0, sizeof r->tm); r->tm.a = r->tm.d = FX; r->tlm = r->tm; }
      else if (!strcmp(op, "Tf") && nn >= 1) { g->font = nameint; g->fs = num[nn - 1]; }
      else if (!strcmp(op, "Tc") && nn >= 1) g->tc = num[0];
      else if (!strcmp(op, "Tz") && nn >= 1) g->th = num[0] / 100;
      else if (!strcmp(op, "Tr") && nn >= 1) g->tr = (int)(num[0] / FX);
      else if (!strcmp(op, "Td") && nn >= 2) {
        Mat t; t.a = FX; t.b = 0; t.c = 0; t.d = FX; t.e = num[0]; t.f = num[1];
        concat(&r->tlm, &t); r->tm = r->tlm;
      }
      else if (!strcmp(op, "Tm") && nn >= 6) { r->tm.a = num[0]; r->tm.b = num[1]; r->tm.c = num[2]; r->tm.d = num[3]; r->tm.e = num[4]; r->tm.f = num[5]; r->tlm = r->tm; }
      else if (!strcmp(op, "Tj")) show_text(r, str, strn, hex);
      else if (!strcmp(op, "Do")) { if (nameint >= 0 && nameint < r->nimgs) picture(r, r->imgs[nameint]); }
      nn = 0; arrn = 0;
    }
  }
}

/* see raster.h */
u8 *raster_page(const char *content, size_t len, i32 pw, i32 ph, int dpi, const RFont *fonts, int nfonts,
                const Image *const *imgs, int nimgs, u32 *w, u32 *h)
{
  R r;
  GState *g;
  int i;
  if (dpi < 18) dpi = 18;
  if (dpi > 600) dpi = 600;
  memset(&r, 0, sizeof r);
  r.W = (i32)(((i64)pw * dpi + 720) / 1440); r.H = (i32)(((i64)ph * dpi + 720) / 1440);
  if (r.W < 1) r.W = 1;
  if (r.H < 1) r.H = 1;
  r.fonts = fonts; r.nfonts = nfonts; r.imgs = imgs; r.nimgs = nimgs;
  r.rgb = (u8 *)malloc((size_t)r.W * r.H * 3);
  r.px = (i32 *)malloc(MAXPTS * sizeof(i32)); r.py = (i32 *)malloc(MAXPTS * sizeof(i32));
  r.acc = (u16 *)calloc((size_t)r.W + 2, sizeof(u16));
  r.tt = (TTRaster *)calloc(nfonts ? (size_t)nfonts : 1, sizeof(TTRaster));
  r.ttok = (u8 *)calloc(nfonts ? (size_t)nfonts : 1, 1);
  r.gc = (GCache *)calloc(GCN, sizeof(GCache));
  if (r.rgb && r.px && r.py && r.acc && r.tt && r.ttok && r.gc) {
    memset(r.rgb, 255, (size_t)r.W * r.H * 3);
    g = &r.gs[0];
    /* points to pixels, y down from the top */
    g->ctm.a = (i64)dpi * FX / 72; g->ctm.d = -g->ctm.a; g->ctm.f = (i64)r.H * FX;
    g->lw = FX; g->th = FX; g->font = -1;
    g->cx0 = 0; g->cy0 = 0; g->cx1 = r.W; g->cy1 = r.H;
    run(&r, content, len);
  } else { free(r.rgb); r.rgb = NULL; }
  for (i = 0; i < GCN && r.gc; i++) if (r.gc[i].used) free(r.gc[i].bm.cov);
  for (i = 0; i < nfonts && r.ttok; i++) if (r.ttok[i] == 1) ttr_close(&r.tt[i]);
  free(r.gc); free(r.tt); free(r.ttok); free(r.px); free(r.py); free(r.acc); free(r.e); free(r.xs); free(r.xd);
  *w = (u32)r.W; *h = (u32)r.H;
  return r.rgb;
}
