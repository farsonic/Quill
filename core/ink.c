/*
 * ink.c - WP20: freeform shapes and ink (see ink.h). This file belongs to WP20.
 *
 * Ink to Shape follows the approach of WordCraft's recogniser (crates/engine/src/cmd/ink_shape.rs,
 * MIT OR Apache-2.0, (c) the WordCraft authors): an open stroke near the chord between its ends is
 * a line; a shaft with a head drawn on at its far end is an arrow; a closed stroke is an ellipse when
 * its points lie on the ellipse inscribed in its bounds, else the polygon of its corners. Written
 * here afresh in integer C.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ink.h"

/* ------------------------------------------------------------------ small arithmetic */
static i32 isqrt64(long long v)
{
  long long r, x;
  if (v <= 0) return 0;
  r = 1; x = v;
  while (x > 3) { x >>= 2; r <<= 1; }             /* a first guess near the root */
  for (;;) {
    long long n = (r + v / r) / 2;
    if (n >= r && n - r <= 1) break;
    if (n < r && r - n <= 1) { r = n; break; }
    r = n;
  }
  while (r * r > v) r--;
  while ((r + 1) * (r + 1) <= v) r++;
  return (i32)r;
}
static i32 dist(i32 ax, i32 ay, i32 bx, i32 by) { long long dx = (long long)bx - ax, dy = (long long)by - ay; return isqrt64(dx * dx + dy * dy); }
/* the distance from p to the segment a-b */
static i32 seg_dist(i32 ax, i32 ay, i32 bx, i32 by, i32 px, i32 py)
{
  long long dx = (long long)bx - ax, dy = (long long)by - ay, l2 = dx * dx + dy * dy, t;
  if (!l2) return dist(ax, ay, px, py);
  t = ((long long)(px - ax) * dx + (long long)(py - ay) * dy);
  if (t <= 0) return dist(ax, ay, px, py);
  if (t >= l2) return dist(bx, by, px, py);
  /* the perpendicular: |cross| / length */
  {
    long long c = dx * (py - ay) - dy * (px - ax);
    if (c < 0) c = -c;
    return (i32)(c / isqrt64(l2));
  }
}
static i32 imax(i32 a, i32 b) { return a > b ? a : b; }
static i32 imin(i32 a, i32 b) { return a < b ? a : b; }
static i32 iabs(i32 a) { return a < 0 ? -a : a; }

/* ------------------------------------------------------------------ freeforms */
Freeform *ff_new(i32 w, i32 h)
{
  Freeform *f = (Freeform *)calloc(1, sizeof(Freeform));
  if (!f) return NULL;
  f->refs = 1; f->w = w > 0 ? w : 1; f->h = h > 0 ? h : 1;
  return f;
}
void ff_ref(Freeform *f) { if (f) f->refs++; }
void ff_unref(Freeform *f)
{
  u32 k;
  if (!f || --f->refs) return;
  for (k = 0; k < f->np; k++) free(f->p[k].xy);
  free(f->p); free(f->raw); free(f->rawpart); free(f);
}
int ff_add_path(Freeform *f, const i32 *xy, u32 n, int closed)
{
  FfPath *np;
  if (!f || !n) return 1;
  if (f->np >= FF_MAXPATHS) return 1;
  if (n > FF_MAXPTS) n = FF_MAXPTS;
  if (!(np = (FfPath *)realloc(f->p, (f->np + 1) * sizeof(FfPath)))) return 0;
  f->p = np;
  if (!(np[f->np].xy = (i32 *)malloc(n * 2 * sizeof(i32)))) return 0;
  memcpy(np[f->np].xy, xy, n * 2 * sizeof(i32));
  np[f->np].n = n; np[f->np].closed = (u8)(closed != 0);
  f->np++;
  return 1;
}
Freeform *ff_copy(const Freeform *f)
{
  Freeform *c;
  u32 k;
  if (!f || !(c = ff_new(f->w, f->h))) return NULL;
  c->tool = f->tool; c->alpha = f->alpha; c->order = f->order;
  for (k = 0; k < f->np; k++) if (!ff_add_path(c, f->p[k].xy, f->p[k].n, f->p[k].closed)) { ff_unref(c); return NULL; }
  if ((f->raw && !(c->raw = str_dup(f->raw))) || (f->rawpart && !(c->rawpart = str_dup(f->rawpart)))) { ff_unref(c); return NULL; }
  return c;
}
void ff_point(const Freeform *f, i32 cx, i32 cy, u32 k, u32 i, i32 *u, i32 *v)
{
  const i32 *p = f->p[k].xy + 2 * i;
  *u = (i32)((long long)p[0] * cx / f->w);
  *v = (i32)((long long)p[1] * cy / f->h);
}
int ff_is_ink(const Image *im) { return im && im->ff && im->ff->tool != INK_NONE; }
static u32 max_order(const Image *im, int n)
{
  u32 m = 0;
  int i;
  for (i = 0; i < n; i++) {
    if (im[i].ff && im[i].ff->order > m) m = im[i].ff->order;
    if (im[i].nkids) { u32 k = max_order(im[i].kids, im[i].nkids); if (k > m) m = k; }
  }
  return m;
}
u32 ink_next_order(const Doc *d) { return max_order(d->images, d->nimages) + 1; }

/* ------------------------------------------------------------------ drawing with the mouse */
/* Ramer-Douglas-Peucker: keep[i] set for the points [a, b] needs within tol (iterative) */
static void rdp(const i32 *p, u32 n, i32 tol, u8 *keep)
{
  u32 *st, sp = 0;
  if (n == 0) return;
  memset(keep, 0, n);
  keep[0] = keep[n - 1] = 1;
  if (n < 3 || !(st = (u32 *)malloc(n * 2 * sizeof(u32)))) { u32 i; for (i = 0; i < n; i++) keep[i] = 1; return; }
  st[sp++] = 0; st[sp++] = n - 1;
  while (sp) {
    u32 b = st[--sp], a = st[--sp], i, far = a;
    i32 best = -1;
    for (i = a + 1; i < b; i++) {
      i32 d = seg_dist(p[2 * a], p[2 * a + 1], p[2 * b], p[2 * b + 1], p[2 * i], p[2 * i + 1]);
      if (d > best) { best = d; far = i; }
    }
    if (best > tol && far > a && far < b) {
      keep[far] = 1;
      st[sp++] = a; st[sp++] = far;
      st[sp++] = far; st[sp++] = b;
    }
  }
  free(st);
}
u32 ink_smooth(const i32 *in, u32 n, i32 *out, u32 cap, i32 tol)
{
  i32 *a, *b;
  u8 *keep;
  u32 i, m = 0, k = 0, it;
  if (!n || cap < 2) return 0;
  if (!(a = (i32 *)malloc(n * 2 * sizeof(i32))) || !(keep = (u8 *)malloc(n))) { free(a); a = NULL; }
  if (!a) { u32 c = n < cap ? n : cap; memcpy(out, in, c * 2 * sizeof(i32)); return c; }
  /* the same point twice in a row only once */
  for (i = 0; i < n; i++) if (!m || in[2 * i] != a[2 * m - 2] || in[2 * i + 1] != a[2 * m - 1]) { a[2 * m] = in[2 * i]; a[2 * m + 1] = in[2 * i + 1]; m++; }
  if (m < 3) {
    for (i = 0; i < m && i < cap; i++) { out[2 * i] = a[2 * i]; out[2 * i + 1] = a[2 * i + 1]; }
    if (m == 1) { out[2] = out[0]; out[3] = out[1]; i = 2; }     /* a dot: a stroke of no length */
    free(a); free(keep);
    return i;
  }
  /* the mouse's jitter averaged out (1 2 3 2 1), the ends kept where they are */
  if (!(b = (i32 *)malloc(m * 2 * sizeof(i32)))) { b = a; }
  else {
    for (i = 0; i < m; i++) {
      int c, j;
      long sx = 0, sy = 0, sw = 0;
      static const int wt[5] = { 1, 2, 3, 2, 1 };
      if (i == 0 || i == m - 1) { b[2 * i] = a[2 * i]; b[2 * i + 1] = a[2 * i + 1]; continue; }
      for (j = -2; j <= 2; j++) {
        long q = (long)i + j;
        if (q < 0 || q >= (long)m) continue;
        c = wt[j + 2];
        sx += (long)a[2 * q] * c; sy += (long)a[2 * q + 1] * c; sw += c;
      }
      b[2 * i] = (i32)(sx / sw); b[2 * i + 1] = (i32)(sy / sw);
    }
  }
  /* points that add nothing dropped */
  rdp(b, m, tol, keep);
  for (i = 0; i < m; i++) if (keep[i]) { a[2 * k] = b[2 * i]; a[2 * k + 1] = b[2 * i + 1]; k++; }
  if (b != a) free(b);
  free(keep);
  /* the corners rounded: Chaikin's cutting, twice, the ends kept */
  for (it = 0; it < 2 && k >= 3 && k * 2 <= FF_MAXPTS; it++) {
    i32 *c = (i32 *)malloc((k * 2) * 2 * sizeof(i32));
    u32 j = 0;
    if (!c) break;
    c[j++] = a[0]; c[j++] = a[1];
    for (i = 0; i + 1 < k; i++) {
      i32 px = a[2 * i], py = a[2 * i + 1], qx = a[2 * i + 2], qy = a[2 * i + 3];
      if (i > 0) { c[j++] = (3 * px + qx) / 4; c[j++] = (3 * py + qy) / 4; }
      if (i + 2 < k) { c[j++] = (px + 3 * qx) / 4; c[j++] = (py + 3 * qy) / 4; }
    }
    c[j++] = a[2 * k - 2]; c[j++] = a[2 * k - 1];
    free(a); a = c; k = j / 2;
  }
  if (k > cap) {
    /* too many: every so many, the ends kept */
    for (i = 0; i < cap; i++) { u32 s = (u32)((long long)i * (k - 1) / (cap - 1)); out[2 * i] = a[2 * s]; out[2 * i + 1] = a[2 * s + 1]; }
    k = cap;
  } else memcpy(out, a, k * 2 * sizeof(i32));
  free(a);
  return k;
}
int ink_make(Image *im, int tool, u32 color, int width, const i32 *pts, u32 n, i32 *x, i32 *y)
{
  i32 x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF, pad, *rel;
  Freeform *f;
  u32 i;
  if (!n) return 0;
  if (width < 1) width = 1;
  if (width > 255) width = 255;
  for (i = 0; i < n; i++) {
    x0 = imin(x0, pts[2 * i]); x1 = imax(x1, pts[2 * i]);
    y0 = imin(y0, pts[2 * i + 1]); y1 = imax(y1, pts[2 * i + 1]);
  }
  pad = width * 5 / 4 + 10;                       /* half the line (eighths of a point: 2.5 twips each), and a little */
  if (!(rel = (i32 *)malloc(n * 2 * sizeof(i32)))) return 0;
  for (i = 0; i < n; i++) { rel[2 * i] = pts[2 * i] - x0 + pad; rel[2 * i + 1] = pts[2 * i + 1] - y0 + pad; }
  img_defaults(im);
  im->kind = OBJ_SHAPE; im->shape = SH_FREEFORM;
  im->cx = x1 - x0 + 2 * pad; im->cy = y1 - y0 + 2 * pad;
  im->wrap = tool == INK_HIGHLIGHTER ? WRAP_BEHIND : WRAP_FRONT;
  im->dl = im->dr = im->dt = im->db = 0;
  im->lstyle = PB_SINGLE; im->lwidth = (u8)width; im->lcolor = color & 0xFFFFFF; im->fill = COLOR_AUTO;
  if (!(f = ff_new(im->cx, im->cy)) || !ff_add_path(f, rel, n, 0)) { free(rel); ff_unref(f); return 0; }
  free(rel);
  f->tool = (u8)tool;
  f->alpha = (u8)(tool == INK_HIGHLIGHTER ? 50 : 0);
  im->ff = f;
  *x = x0 - pad; *y = y0 - pad;
  return 1;
}

/* ------------------------------------------------------------------ Ink to Shape */
/* the corners of a closed stroke: the path simplified, then corners that are hardly corners (nearly
   straight on) or crowd another merged away, round the loop. How many (at most max) */
static int corners(const i32 *p, u32 n, i32 diag, i32 *c, int max)
{
  u8 *keep = (u8 *)malloc(n);
  int k = 0, changed = 1, i;
  i32 tmp[2 * 64];
  u32 j;
  if (!keep) return 0;
  rdp(p, n, imax(diag * 6 / 100, 2), keep);
  for (j = 0; j < n && k < 64; j++) if (keep[j]) { tmp[2 * k] = p[2 * j]; tmp[2 * k + 1] = p[2 * j + 1]; k++; }
  free(keep);
  /* the last back where the first is: one corner */
  if (k > 1 && dist(tmp[0], tmp[1], tmp[2 * k - 2], tmp[2 * k - 1]) < diag / 6) k--;
  while (changed && k > 2) {
    changed = 0;
    for (i = 0; i < k && k > 2; i++) {
      int a = (i + k - 1) % k, b = (i + 1) % k;
      int t1 = iatan2(tmp[2 * i + 1] - tmp[2 * a + 1], tmp[2 * i] - tmp[2 * a]);
      int t2 = iatan2(tmp[2 * b + 1] - tmp[2 * i + 1], tmp[2 * b] - tmp[2 * i]);
      int turn = (t2 - t1 + 3600 + 1800) % 3600 - 1800;
      if (iabs(turn) < 300 || dist(tmp[2 * i], tmp[2 * i + 1], tmp[2 * b], tmp[2 * b + 1]) < diag / 8) {
        if (iabs(turn) >= 300) {
          /* two corners close together: one between them */
          tmp[2 * b] = (tmp[2 * b] + tmp[2 * i]) / 2; tmp[2 * b + 1] = (tmp[2 * b + 1] + tmp[2 * i + 1]) / 2;
        }
        memmove(tmp + 2 * i, tmp + 2 * i + 2, (size_t)(k - i - 1) * 2 * sizeof(i32));
        k--; changed = 1;
        break;
      }
    }
  }
  if (k > max) return k;
  memcpy(c, tmp, (size_t)k * 2 * sizeof(i32));
  return k;
}
/* the farthest any point is from the closed polygon c (k corners) */
static i32 poly_off(const i32 *c, int k, const i32 *p, u32 n)
{
  i32 worst = 0;
  u32 j;
  int i;
  for (j = 0; j < n; j++) {
    i32 best = 0x7FFFFFFF;
    for (i = 0; i < k; i++) { int b = (i + 1) % k; best = imin(best, seg_dist(c[2 * i], c[2 * i + 1], c[2 * b], c[2 * b + 1], p[2 * j], p[2 * j + 1])); }
    worst = imax(worst, best);
  }
  return worst;
}
/* how far (thousandths) the points are on average from the ellipse inscribed in x0, y0, w, h (rms) */
static i32 ellipse_rms(const i32 *p, u32 n, i32 x0, i32 y0, i32 w, i32 h)
{
  long long s = 0;
  u32 i;
  i32 rx = w / 2, ry = h / 2;
  if (rx < 1 || ry < 1 || !n) return 100000;
  for (i = 0; i < n; i++) {
    long long dx = ((long long)p[2 * i] - x0 - rx) * 1000 / rx, dy = ((long long)p[2 * i + 1] - y0 - ry) * 1000 / ry;
    i32 r = isqrt64(dx * dx + dy * dy) - 1000;
    s += (long long)r * r;
  }
  return isqrt64(s / n);
}
static void snap_square(InkShape *r)
{
  i32 m = imax(r->w, r->h);
  if (iabs(r->w - r->h) * 10 < m) { r->x -= (m - r->w) / 2; r->y -= (m - r->h) / 2; r->w = r->h = m; }
}
/* the head of an arrow drawn shaft first: the shaft from the start to the tip (the point farthest
   from the start) stays near its chord; after the tip the pen goes out to both sides of it and
   back. 1 if p (n points) is one */
static int arrow_head(const i32 *p, u32 n)
{
  u32 ti = 0, i;
  i32 best = -1, shaft, off = 0, left = 0, right = 0, reach = 0;
  long long ux, uy;
  for (i = 0; i < n; i++) { i32 d = dist(p[0], p[1], p[2 * i], p[2 * i + 1]); if (d > best) { best = d; ti = i; } }
  shaft = best;
  if (shaft <= 0 || ti < 2 || ti + 2 >= n) return 0;
  for (i = 0; i <= ti; i++) off = imax(off, seg_dist(p[0], p[1], p[2 * ti], p[2 * ti + 1], p[2 * i], p[2 * i + 1]));
  if (off > shaft / 10 + 20) return 0;
  ux = (long long)p[2 * ti] - p[0]; uy = (long long)p[2 * ti + 1] - p[1];
  for (i = ti; i < n; i++) {
    long long vx = (long long)p[2 * i] - p[2 * ti], vy = (long long)p[2 * i + 1] - p[2 * ti + 1];
    long long cr = (ux * vy - uy * vx) / shaft, back = -(ux * vx + uy * vy) / shaft;   /* across the shaft, and back along it */
    reach = imax(reach, dist(p[2 * ti], p[2 * ti + 1], p[2 * i], p[2 * i + 1]));
    if (back < 0 && -back > shaft / 10) return 0;       /* past the tip: not a head */
    if (cr > right) right = (i32)cr;
    if (-cr > left) left = (i32)-cr;
  }
  /* barbs both sides, of like size, 5-50 % of the shaft */
  if (reach < shaft / 20 || reach > shaft / 2) return 0;
  if (left < reach / 4 || right < reach / 4) return 0;
  if (left > 3 * right || right > 3 * left) return 0;
  return 1;
}
int ink_recognise(const i32 *pts, u32 n, InkShape *r)
{
  i32 x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF, w, h, size, diag, gap, rms;
  long long len = 0;
  u32 i;
  i32 c[2 * 16];
  int k;
  memset(r, 0, sizeof *r);
  if (n < 2) return 0;
  for (i = 0; i < n; i++) {
    x0 = imin(x0, pts[2 * i]); x1 = imax(x1, pts[2 * i]); y0 = imin(y0, pts[2 * i + 1]); y1 = imax(y1, pts[2 * i + 1]);
    if (i) len += dist(pts[2 * i - 2], pts[2 * i - 1], pts[2 * i], pts[2 * i + 1]);
  }
  w = x1 - x0; h = y1 - y0; size = imax(w, h); diag = dist(0, 0, w, h);
  if (size < 120) return 0;                       /* a tick or a dot stays ink */
  gap = dist(pts[0], pts[1], pts[2 * n - 2], pts[2 * n - 1]);
  /* a line: the stroke hardly leaves the chord between its ends, and doesn't double back */
  if ((long long)gap * 100 > len * 85) {
    i32 off = 0;
    for (i = 0; i < n; i++) off = imax(off, seg_dist(pts[0], pts[1], pts[2 * n - 2], pts[2 * n - 1], pts[2 * i], pts[2 * i + 1]));
    if (off <= imax(gap * 6 / 100, 30)) {
      i32 ax = pts[0], ay = pts[1], bx = pts[2 * n - 2], by = pts[2 * n - 1];
      r->shape = SH_LINE;
      /* nearly level or upright: made so */
      if (iabs(by - ay) * 10 < iabs(bx - ax)) by = ay;
      else if (iabs(bx - ax) * 10 < iabs(by - ay)) bx = ax;
      r->x = imin(ax, bx); r->y = imin(ay, by); r->w = iabs(bx - ax); r->h = iabs(by - ay);
      if ((long long)(bx - ax) * (by - ay) < 0) r->flip = 2;
      return 1;
    }
  }
  /* an arrow: drawn shaft first, or head first (then read backwards) */
  {
    i32 *rev = (i32 *)malloc(n * 2 * sizeof(i32));
    int fw = arrow_head(pts, n), bw = 0;
    if (rev && !fw) { for (i = 0; i < n; i++) { rev[2 * i] = pts[2 * (n - 1 - i)]; rev[2 * i + 1] = pts[2 * (n - 1 - i) + 1]; } bw = arrow_head(rev, n); }
    if (fw || bw) {
      const i32 *p = fw ? pts : rev;
      u32 ti = 0;
      i32 best = -1, ax = p[0], ay = p[1], bx, by;
      for (i = 0; i < n; i++) { i32 d = dist(p[0], p[1], p[2 * i], p[2 * i + 1]); if (d > best) { best = d; ti = i; } }
      bx = p[2 * ti]; by = p[2 * ti + 1];
      if (iabs(by - ay) * 10 < iabs(bx - ax)) by = ay;
      else if (iabs(bx - ax) * 10 < iabs(by - ay)) bx = ax;
      r->shape = SH_ARROW;
      r->x = imin(ax, bx); r->y = imin(ay, by); r->w = iabs(bx - ax); r->h = iabs(by - ay);
      r->flip = (u8)((bx < ax ? 1 : 0) | (by < ay ? 2 : 0));     /* the head at the tip */
      free(rev);
      return 1;
    }
    free(rev);
  }
  /* the rest are closed: the stroke comes back near where it began, having gone round */
  if (gap * 5 > size || len < 2 * (long long)size) return 0;
  r->x = x0; r->y = y0; r->w = w; r->h = h;
  rms = ellipse_rms(pts, n, x0, y0, w, h);
  if (w * 5 > size && h * 5 > size && rms < 60) { r->shape = SH_ELLIPSE; snap_square(r); return 1; }
  k = corners(pts, n, diag, c, 8);
  if (k >= 3 && k <= 8 && poly_off(c, k, pts, n) <= diag * 9 / 100) {
    i32 tw = imax(w / 5, 1), th = imax(h / 5, 1);
    if (k == 4) {
      int up = 1, mid = 1;
      for (i = 0; i < 4; i++) {
        int b = (int)(i + 1) % 4, a = iatan2(c[2 * b + 1] - c[2 * i + 1], c[2 * b] - c[2 * i]) % 900;
        if (a > 200 && a < 700) up = 0;
      }
      if (up) { r->shape = SH_RECT; snap_square(r); return 1; }
      /* a diamond: a corner near the middle of each side */
      for (i = 0; i < 4; i++) {
        i32 px = c[2 * i], py = c[2 * i + 1];
        int ok = (iabs(px - (x0 + w / 2)) < tw && (iabs(py - y0) < th || iabs(py - y1) < th)) ||
                 (iabs(py - (y0 + h / 2)) < th && (iabs(px - x0) < tw || iabs(px - x1) < tw));
        if (!ok) mid = 0;
      }
      if (mid) { r->shape = SH_DIAMOND; return 1; }
    }
    if (k == 3) {
      /* upright: the apex near the middle of the top (or bottom), the others at the far corners */
      int ap, ok;
      for (ap = 0; ap < 3; ap++) {
        i32 axx = c[2 * ap], ayy = c[2 * ap + 1];
        int o1 = (ap + 1) % 3, o2 = (ap + 2) % 3, top = iabs(ayy - y0) < th, bot = iabs(ayy - y1) < th;
        if (iabs(axx - (x0 + w / 2)) >= tw || (!top && !bot)) continue;
        ok = iabs(c[2 * o1 + 1] - (top ? y1 : y0)) < th && iabs(c[2 * o2 + 1] - (top ? y1 : y0)) < th &&
             iabs(iabs(c[2 * o1] - c[2 * o2]) - w) < tw;
        if (ok) { r->shape = SH_TRIANGLE; r->flip = (u8)(top ? 0 : 2); return 1; }
      }
      /* a right triangle: its corners at three of the box's */
      {
        int at[4] = { 0, 0, 0, 0 }, q, miss = -1, cnt = 0;      /* top left, top right, bottom right, bottom left */
        for (i = 0; i < 3; i++) {
          int l = iabs(c[2 * i] - x0) < tw, rr = iabs(c[2 * i] - x1) < tw, t = iabs(c[2 * i + 1] - y0) < th, b = iabs(c[2 * i + 1] - y1) < th;
          if (l && t) at[0] = 1; else if (rr && t) at[1] = 1; else if (rr && b) at[2] = 1; else if (l && b) at[3] = 1;
        }
        for (q = 0; q < 4; q++) { if (at[q]) cnt++; else miss = q; }
        if (cnt == 3) {
          static const u8 flips[4] = { 3, 0, 2, 1 };      /* the corner missing: bottom-left's right angle mirrored there */
          r->shape = SH_RTRIANGLE; r->flip = flips[miss];
          return 1;
        }
      }
    }
    r->shape = SH_FREEFORM;
    r->npoly = k;
    for (i = 0; i < (u32)k; i++) { r->poly[2 * i] = c[2 * i] - x0; r->poly[2 * i + 1] = c[2 * i + 1] - y0; }
    return 1;
  }
  /* a wobbly round stroke is an ellipse all the same */
  if (w * 5 > size && h * 5 > size && rms < 110) { r->shape = SH_ELLIPSE; snap_square(r); return 1; }
  return 0;
}
const char *ink_shape_name(int shape)
{
  switch (shape) {
  case SH_LINE: return "Line";
  case SH_ARROW: return "Arrow";
  case SH_RECT: return "Rectangle";
  case SH_ELLIPSE: return "Oval";
  case SH_TRIANGLE: return "Isosceles Triangle";
  case SH_RTRIANGLE: return "Right Triangle";
  case SH_DIAMOND: return "Diamond";
  case SH_FREEFORM: return "Freeform";
  }
  return "Shape";
}
int ink_to_shape(const Image *im, Image *out, i32 *dx, i32 *dy)
{
  const Freeform *f = im->ff;
  InkShape r;
  i32 *pts;
  u32 n = 0, k, i, tot = 0;
  if (!f || (f->tool != INK_PEN && f->tool != INK_PENCIL) || !f->np || im->rot || im->flip) return 0;
  for (k = 0; k < f->np; k++) tot += f->p[k].n;
  if (!(pts = (i32 *)malloc(tot * 2 * sizeof(i32)))) return 0;
  /* the strokes one after another (a shape is one stroke; a second only if it carries on from the first) */
  for (k = 0; k < f->np; k++) for (i = 0; i < f->p[k].n; i++) { ff_point(f, im->cx, im->cy, k, i, &pts[2 * n], &pts[2 * n + 1]); n++; }
  if (!ink_recognise(pts, n, &r)) { free(pts); return 0; }
  free(pts);
  if (!img_clone(out, im, 0)) return 0;
  ff_unref(out->ff); out->ff = NULL;
  out->kind = OBJ_SHAPE; out->shape = (u8)r.shape; out->flip = r.flip; out->fill = COLOR_AUTO;
  out->wrap = WRAP_FRONT;
  out->cx = imax(r.w, 1); out->cy = imax(r.h, 1);
  if (r.shape == SH_FREEFORM) {
    Freeform *nf = ff_new(out->cx, out->cy);
    if (!nf || !ff_add_path(nf, r.poly, (u32)r.npoly, 1)) { ff_unref(nf); img_release(out); return 0; }
    out->ff = nf;
  }
  *dx = r.x; *dy = r.y;
  return 1;
}

/* ------------------------------------------------------------------ hitting, rubbing out, the lasso */
int ff_hit(const Image *im, i32 u, i32 v, i32 r)
{
  const Freeform *f = im->ff;
  u32 k, i;
  if (!f) return 0;
  r += im->lwidth * 5 / 4;
  for (k = 0; k < f->np; k++) {
    i32 pu, pv, qu, qv;
    if (f->p[k].n == 1) { ff_point(f, im->cx, im->cy, k, 0, &pu, &pv); if (dist(pu, pv, u, v) <= r) return 1; continue; }
    for (i = 0; i + 1 < f->p[k].n + (f->p[k].closed ? 1 : 0); i++) {
      ff_point(f, im->cx, im->cy, k, i, &pu, &pv);
      ff_point(f, im->cx, im->cy, k, (i + 1) % f->p[k].n, &qu, &qv);
      if (seg_dist(pu, pv, qu, qv, u, v) <= r) return 1;
    }
  }
  return 0;
}
Freeform *ff_erase(const Image *im, i32 u, i32 v, i32 r, int *hit)
{
  const Freeform *f = im->ff;
  Freeform *nf;
  i32 *run;
  u32 k, i, m;
  *hit = 0;
  if (!f || !(nf = ff_new(f->w, f->h))) return NULL;
  nf->tool = f->tool; nf->alpha = f->alpha; nf->order = f->order;
  r += im->lwidth * 5 / 4;
  for (k = 0; k < f->np; k++) {
    const FfPath *pp = &f->p[k];
    u32 cap, j, whole = 1;
    /* a segment passing near the eraser cut too: each first split into pieces no longer than r / 2 */
    for (i = 0, cap = 1; i + 1 < pp->n; i++) {
      i32 au, av, bu, bv, l;
      ff_point(f, im->cx, im->cy, k, i, &au, &av); ff_point(f, im->cx, im->cy, k, i + 1, &bu, &bv);
      l = dist(au, av, bu, bv);
      cap += (u32)(seg_dist(au, av, bu, bv, u, v) <= r ? l / imax(r / 2, 1) + 1 : 1);
    }
    if (!(run = (i32 *)malloc(cap * 2 * sizeof(i32)))) { ff_unref(nf); return NULL; }
    m = 0;
    for (i = 0; i < pp->n; i++) {
      u32 steps = 1;
      if (i + 1 < pp->n) {
        i32 au, av, bu, bv;
        ff_point(f, im->cx, im->cy, k, i, &au, &av); ff_point(f, im->cx, im->cy, k, i + 1, &bu, &bv);
        if (seg_dist(au, av, bu, bv, u, v) <= r) steps = (u32)(dist(au, av, bu, bv) / imax(r / 2, 1) + 1);
      }
      for (j = 0; j < steps && (j == 0 || i + 1 < pp->n); j++) {
        i32 px = pp->xy[2 * i], py = pp->xy[2 * i + 1], pu, pv;
        if (j) { px += (i32)((long long)(pp->xy[2 * i + 2] - px) * j / steps); py += (i32)((long long)(pp->xy[2 * i + 3] - py) * j / steps); }
        pu = (i32)((long long)px * im->cx / f->w); pv = (i32)((long long)py * im->cy / f->h);
        if (dist(pu, pv, u, v) <= r) {
          *hit = 1; whole = 0;
          if (m >= 2) ff_add_path(nf, run, m, 0);
          m = 0;
          continue;
        }
        if (m < cap) { run[2 * m] = px; run[2 * m + 1] = py; m++; }
      }
    }
    if (whole) ff_add_path(nf, pp->xy, pp->n, pp->closed);
    else if (m >= 2) ff_add_path(nf, run, m, 0);
    free(run);
  }
  if (!nf->np) { ff_unref(nf); return NULL; }
  return nf;
}
int poly_inside(const i32 *poly, u32 n, i32 x, i32 y)
{
  u32 i, j;
  int in = 0;
  for (i = 0, j = n ? n - 1 : 0; i < n; j = i++) {
    i32 xi = poly[2 * i], yi = poly[2 * i + 1], xj = poly[2 * j], yj = poly[2 * j + 1];
    if ((yi > y) != (yj > y) && (long long)x < (long long)(xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}
int ink_in_lasso(const Image *im, i32 bx, i32 by, const i32 *poly, u32 n)
{
  const Freeform *f = im->ff;
  u32 k, i, tot = 0, ins = 0;
  if (n < 3) return 0;
  if (!f || !f->np) {
    i32 cxp = bx + obj_w(im) / 2, cyp = by + obj_h(im) / 2;
    return poly_inside(poly, n, cxp, cyp);
  }
  for (k = 0; k < f->np; k++)
    for (i = 0; i < f->p[k].n; i++) {
      i32 u, v, X, Y;
      ff_point(f, im->cx, im->cy, k, i, &u, &v);
      obj_point(im, u, v, &X, &Y);
      tot++;
      if (poly_inside(poly, n, bx + X, by + Y)) ins++;
    }
  return tot && ins * 10 >= tot * 6;
}

/* ------------------------------------------------------------------ drawing canvas */
int canvas_make(Image *im, i32 cx, i32 cy)
{
  Image bg;
  img_defaults(im);
  im->kind = OBJ_GROUP; im->canvas = CANVAS_GROUP;
  im->cx = im->chw = cx; im->cy = im->chh = cy;
  im->dl = im->dr = 0;
  img_defaults(&bg);
  bg.kind = OBJ_SHAPE; bg.shape = SH_RECT; bg.canvas = CANVAS_BG;
  bg.cx = cx; bg.cy = cy; bg.fill = COLOR_AUTO; bg.lstyle = PB_NONE;
  if (!(im->kids = (Image *)malloc(sizeof(Image)))) return 0;
  im->kids[0] = bg; im->nkids = 1;
  return 1;
}
int canvas_add(Image *c, const Image *kid, i32 x, i32 y)
{
  Image *nk;
  if (!(nk = (Image *)realloc(c->kids, (c->nkids + 1) * sizeof(Image)))) return 0;
  c->kids = nk;
  if (!img_clone(&nk[c->nkids], kid, 0)) return 0;
  nk[c->nkids].x = x; nk[c->nkids].y = y;
  nk[c->nkids].wrap = WRAP_FRONT;
  c->nkids++;
  return 1;
}

/* ------------------------------------------------------------------ .docx */
/* a coordinate of a path: a number, or one of the names for the path's sides; 0 if it can't say */
static int coord(const char *v, i32 pw, i32 ph, i32 *out)
{
  char *end;
  long n;
  if (!v || !*v) return 0;
  n = strtol(v, &end, 10);
  if (end != v && !*end) { *out = (i32)(n > 2000000000L ? 2000000000L : n < -2000000000L ? -2000000000L : n); return 1; }
  if (!strcmp(v, "l") || !strcmp(v, "t")) { *out = 0; return 1; }
  if (!strcmp(v, "r") || !strcmp(v, "w")) { *out = pw; return 1; }
  if (!strcmp(v, "b") || !strcmp(v, "h")) { *out = ph; return 1; }
  if (!strcmp(v, "hc")) { *out = pw / 2; return 1; }
  if (!strcmp(v, "vc")) { *out = ph / 2; return 1; }
  return 0;
}
typedef struct { i32 *xy; u32 n, cap; int bad; } PtRun;
static void run_add(PtRun *r, i32 x, i32 y)
{
  if (r->n >= FF_MAXPTS) return;
  if (r->n == r->cap) {
    u32 c = r->cap ? r->cap * 2 : 64;
    i32 *n = (i32 *)realloc(r->xy, c * 2 * sizeof(i32));
    if (!n) { r->bad = 1; return; }
    r->xy = n; r->cap = c;
  }
  r->xy[2 * r->n] = x; r->xy[2 * r->n + 1] = y; r->n++;
}
/* a subpath done: into f in its space (scaled from the path's pw x ph) */
static void run_flush(Freeform *f, PtRun *r, int closed, i32 pw, i32 ph)
{
  u32 i;
  if (r->n >= 1 && !r->bad) {
    for (i = 0; i < r->n; i++) {
      r->xy[2 * i] = (i32)((long long)r->xy[2 * i] * f->w / (pw > 0 ? pw : 1));
      r->xy[2 * i + 1] = (i32)((long long)r->xy[2 * i + 1] * f->h / (ph > 0 ? ph : 1));
    }
    if (r->n == 1) run_add(r, r->xy[0], r->xy[1]);
    ff_add_path(f, r->xy, r->n, closed);
  }
  r->n = 0; r->bad = 0;
}
Freeform *ff_read_custgeom(Xml *x, i32 ew, i32 eh)
{
  int d0 = x->depth;
  Freeform *f = ff_new(ew > 0 ? ew : 914400, eh > 0 ? eh : 914400);
  PtRun r;
  i32 pw = 0, ph = 0, curx = 0, cury = 0;
  int inpath = 0, nofill = 0, seg = 0;    /* seg: 1 moveTo 2 lnTo 3 cubic 4 quad 5 arc */
  i32 cp[2 * 4];
  int ncp = 0;
  memset(&r, 0, sizeof r);
  if (!f) { xml_skip(x); return NULL; }
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_END) {
      if (!strcmp(n, "path") && inpath) { run_flush(f, &r, !nofill && r.n > 2 && r.xy && r.xy[0] == r.xy[2 * r.n - 2] && r.xy[1] == r.xy[2 * r.n - 1], pw, ph); inpath = 0; }
      else if (seg && (!strcmp(n, "moveTo") || !strcmp(n, "lnTo") || !strcmp(n, "cubicBezTo") || !strcmp(n, "quadBezTo"))) {
        if (seg == 1 && ncp >= 1) { run_flush(f, &r, 0, pw, ph); curx = cp[0]; cury = cp[1]; run_add(&r, curx, cury); }
        else if (seg == 2 && ncp >= 1) { curx = cp[0]; cury = cp[1]; run_add(&r, curx, cury); }
        else if ((seg == 3 && ncp >= 3) || (seg == 4 && ncp >= 2)) {
          /* the curve in eight straight pieces (de Casteljau) */
          int s, m = seg == 3 ? 4 : 3, j, l;
          i32 q[8];
          if (!r.n) run_add(&r, curx, cury);
          for (s = 1; s <= 8; s++) {
            q[0] = curx; q[1] = cury;
            for (j = 0; j < m - 1; j++) { q[2 * j + 2] = cp[2 * j]; q[2 * j + 3] = cp[2 * j + 1]; }
            for (l = m - 1; l > 0; l--)
              for (j = 0; j < l; j++) {
                q[2 * j] = q[2 * j] + (i32)(((long long)q[2 * j + 2] - q[2 * j]) * s / 8);
                q[2 * j + 1] = q[2 * j + 1] + (i32)(((long long)q[2 * j + 3] - q[2 * j + 1]) * s / 8);
              }
            run_add(&r, q[0], q[1]);
          }
          curx = cp[2 * (m - 2)]; cury = cp[2 * (m - 2) + 1];
        }
        seg = 0;
      }
      continue;
    }
    if (x->type != XT_START) continue;
    if (!strcmp(n, "path")) {
      const char *fl = xml_attr(x, "fill");
      pw = 0; ph = 0;
      if (!coord(xml_attr(x, "w"), 0, 0, &pw) || pw <= 0) pw = f->w;
      if (!coord(xml_attr(x, "h"), 0, 0, &ph) || ph <= 0) ph = f->h;
      nofill = fl && !strcmp(fl, "none");
      inpath = 1; r.n = 0; curx = cury = 0;
    }
    else if (!inpath) continue;
    else if (!strcmp(n, "moveTo")) { seg = 1; ncp = 0; }
    else if (!strcmp(n, "lnTo")) { seg = 2; ncp = 0; }
    else if (!strcmp(n, "cubicBezTo")) { seg = 3; ncp = 0; }
    else if (!strcmp(n, "quadBezTo")) { seg = 4; ncp = 0; }
    else if (!strcmp(n, "pt") && seg && ncp < 4) {
      i32 px, py;
      if (coord(xml_attr(x, "x"), pw, ph, &px) && coord(xml_attr(x, "y"), pw, ph, &py)) { cp[2 * ncp] = px; cp[2 * ncp + 1] = py; ncp++; }
      else r.bad = 1;
    }
    else if (!strcmp(n, "arcTo")) {
      /* an arc of the ellipse wR x hR from stAng through swAng (60000ths of a degree) */
      i32 wr, hr, st, sw;
      if (coord(xml_attr(x, "wR"), pw, ph, &wr) && coord(xml_attr(x, "hR"), pw, ph, &hr) && coord(xml_attr(x, "stAng"), pw, ph, &st) && coord(xml_attr(x, "swAng"), pw, ph, &sw)) {
        int a0 = (int)((st / 6000) % 3600), sweep = (int)(sw / 6000), s, steps;
        long ocx = curx - (long)wr * icos(a0) / TRIG_ONE, ocy = cury - (long)hr * isin(a0) / TRIG_ONE;
        if (!r.n) run_add(&r, curx, cury);
        steps = iabs(sweep) / 100 + 2;
        for (s = 1; s <= steps; s++) {
          int a = (a0 + sweep * s / steps + 3600 * 4) % 3600;
          curx = (i32)(ocx + (long)wr * icos(a) / TRIG_ONE); cury = (i32)(ocy + (long)hr * isin(a) / TRIG_ONE);
          run_add(&r, curx, cury);
        }
      }
    }
    else if (!strcmp(n, "close")) { run_flush(f, &r, !nofill, pw, ph); }
  }
  free(r.xy);
  if (!f->np) { ff_unref(f); return NULL; }
  return f;
}
void ff_write_custgeom(Buf *o, const Freeform *f)
{
  u32 k, i;
  buf_adds(o, "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"l\" t=\"t\" r=\"r\" b=\"b\"/><a:pathLst>");
  for (k = 0; f && k < f->np; k++) {
    const FfPath *p = &f->p[k];
    buf_printf(o, "<a:path w=\"%ld\" h=\"%ld\"%s>", (long)f->w, (long)f->h, p->closed ? "" : " fill=\"none\"");
    for (i = 0; i < p->n; i++)
      buf_printf(o, "<a:%s><a:pt x=\"%ld\" y=\"%ld\"/></a:%s>", i ? "lnTo" : "moveTo", (long)p->xy[2 * i], (long)p->xy[2 * i + 1], i ? "lnTo" : "moveTo");
    if (p->closed) buf_adds(o, "<a:close/>");
    buf_adds(o, "</a:path>");
  }
  buf_adds(o, "</a:pathLst></a:custGeom>");
}
static const char *const tool_names[4] = { "", "Pen", "Pencil", "Highlighter" };
const char *ink_name(const Freeform *f, u32 id, char *out, size_t cap)
{
  if (!f || f->tool == INK_NONE || f->tool > INK_HIGHLIGHTER) return NULL;
  if (f->order) snprintf(out, cap, "Ink %s %lu #%lu", tool_names[f->tool], (unsigned long)id, (unsigned long)f->order);
  else snprintf(out, cap, "Ink %s %lu", tool_names[f->tool], (unsigned long)id);
  return out;
}
void ink_parse_name(const char *name, u8 *tool, u32 *order)
{
  const char *h;
  int k;
  *tool = INK_NONE; *order = 0;
  if (!name || strncmp(name, "Ink ", 4)) return;
  *tool = INK_PEN;                                /* "Ink 3" (Ledger's, Word's own names): a pen */
  for (k = 1; k <= INK_HIGHLIGHTER; k++) {
    size_t l = strlen(tool_names[k]);
    if (!strncmp(name + 4, tool_names[k], l) && (name[4 + l] == ' ' || !name[4 + l])) *tool = (u8)k;
  }
  if ((h = strchr(name, '#'))) *order = (u32)strtoul(h + 1, NULL, 10);
}
/* ---- InkML: the traces of Word's ink part */
typedef struct { long pos[2], vel[2]; int mode[2]; } Chan;
/* one value of a trace at *s: its kind (0 explicit, 1 first difference ', 2 second ") stays with
   the channel until another is given. Returns 0 at the end of the point or trace */
static int trace_value(const char **s, int *mode, long *v, int *sep)
{
  const char *p = *s;
  char *end;
  long whole;
  *sep = 0;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (*p == ',') { *sep = 1; *s = p + 1; return 0; }
  if (!*p) { *s = p; return 0; }
  if (*p == '!') { *mode = 0; p++; }
  else if (*p == '\'') { *mode = 1; p++; }
  else if (*p == '"') { *mode = 2; p++; }
  if (*p == '*' || *p == '?') { *v = 0; *s = p + 1; return 1; }
  whole = strtol(p, &end, 10);
  if (end == p && *p != '.') { *s = p + 1; *v = 0; return 1; }
  if (*end == '.') { end++; while (*end >= '0' && *end <= '9') end++; }   /* (fractions dropped: Office's ink is in whole units) */
  if (*end == 'e' || *end == 'E') { end++; if (*end == '-' || *end == '+') end++; while (*end >= '0' && *end <= '9') end++; }
  *v = whole; *s = end;
  return 1;
}
int ff_from_inkml(Freeform *f, const char *xmls, size_t n, i32 cx, i32 cy, u32 *color, int *width)
{
  Xml x;
  i32 **tr = NULL;
  u32 *tn = NULL, ntr = 0, k, i;
  long x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF;
  int inbrush = 0, gotbrush = 0;
  xml_init(&x, xmls, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "brush")) inbrush = !gotbrush;
    else if (x.type == XT_END && !strcmp(x.name, "brush")) { if (inbrush) gotbrush = 1; inbrush = 0; }
    else if (x.type == XT_START && inbrush && !strcmp(x.name, "brushProperty")) {
      const char *nm = xml_attr(&x, "name"), *v = xml_attr(&x, "value"), *u = xml_attr(&x, "units");
      if (nm && v && !strcmp(nm, "color") && *v == '#' && color) *color = (u32)strtoul(v + 1, NULL, 16) & 0xFFFFFF;
      else if (nm && v && !strcmp(nm, "width") && width) {
        /* in cm (or mm): eighths of a point */
        long milli = 0;
        const char *p = v;
        while (*p >= '0' && *p <= '9') milli = milli * 10 + (*p++ - '0');
        milli *= 1000;
        if (*p == '.') { long sc = 100; p++; while (*p >= '0' && *p <= '9' && sc) { milli += (*p++ - '0') * sc; sc /= 10; } }
        if (u && !strcmp(u, "mm")) milli /= 10;
        *width = (int)(milli * 8 * 72 / 2540);   /* thousandths of a cm to eighths of a point */
        if (*width < 1) *width = 1;
        if (*width > 255) *width = 255;
      }
    }
    else if (x.type == XT_START && !strcmp(x.name, "trace")) {
      Buf t;
      const char *s;
      Chan c;
      i32 *pts = NULL;
      u32 np = 0, cap = 0;
      int ch = 0;
      int d1 = x.depth;
      buf_init(&t);
      while (xml_next(&x) != XT_EOF) {
        if (x.type == XT_END && x.depth < d1) break;
        if (x.type == XT_TEXT) buf_add(&t, x.text.data, x.text.len);
      }
      buf_addc(&t, 0);
      memset(&c, 0, sizeof c);
      s = t.data ? t.data : "";
      for (;;) {
        long v;
        int sep, other = 0, more = trace_value(&s, ch < 2 ? &c.mode[ch] : &other, &v, &sep);
        if (!more) {
          if (ch >= 2 && np < FF_MAXPTS) {
            if (np == cap) { u32 nc = cap ? cap * 2 : 64; i32 *pp = (i32 *)realloc(pts, nc * 2 * sizeof(i32)); if (!pp) break; pts = pp; cap = nc; }
            pts[2 * np] = (i32)c.pos[0]; pts[2 * np + 1] = (i32)c.pos[1]; np++;
          }
          ch = 0;
          if (!sep) break;
          continue;
        }
        if (ch < 2) {
          if (c.mode[ch] == 0) { c.pos[ch] = v; c.vel[ch] = 0; }
          else if (c.mode[ch] == 1) { c.vel[ch] = v; c.pos[ch] += v; }
          else { c.vel[ch] += v; c.pos[ch] += c.vel[ch]; }
        }
        ch++;
      }
      buf_free(&t);
      if (np) {
        i32 **a = (i32 **)realloc(tr, (ntr + 1) * sizeof(i32 *));
        u32 *b = a ? (u32 *)realloc(tn, (ntr + 1) * sizeof(u32)) : NULL;
        if (a) tr = a;
        if (b) { tn = b; tr[ntr] = pts; tn[ntr] = np; ntr++; pts = NULL; }
        for (i = 0; b && i < np; i++) {
          i32 px = tr[ntr - 1][2 * i], py = tr[ntr - 1][2 * i + 1];
          if (px < x0) x0 = px;
          if (px > x1) x1 = px;
          if (py < y0) y0 = py;
          if (py > y1) y1 = py;
        }
      }
      free(pts);
    }
  }
  xml_free(&x);
  /* stretched over the object, keeping their shape */
  if (ntr) {
    long sw = x1 - x0 > 0 ? x1 - x0 : 1, sh = y1 - y0 > 0 ? y1 - y0 : 1;
    i32 pad = width && *width ? *width * 5 / 4 : 20;
    long aw = cx - 2 * pad > 1 ? cx - 2 * pad : 1, ah = cy - 2 * pad > 1 ? cy - 2 * pad : 1;
    long num = aw * sh < ah * sw ? aw : ah, den = aw * sh < ah * sw ? sw : sh;   /* the smaller scale */
    f->w = cx > 0 ? cx : 1; f->h = cy > 0 ? cy : 1;
    for (k = 0; k < ntr; k++) {
      for (i = 0; i < tn[k]; i++) {
        tr[k][2 * i] = (i32)(pad + (long long)(tr[k][2 * i] - x0) * num / den + (aw - sw * num / den) / 2);
        tr[k][2 * i + 1] = (i32)(pad + (long long)(tr[k][2 * i + 1] - y0) * num / den + (ah - sh * num / den) / 2);
      }
      ff_add_path(f, tr[k], tn[k], 0);
    }
  }
  for (k = 0; k < ntr; k++) free(tr[k]);
  free(tr); free(tn);
  return ntr > 0;
}
/* ---- PDF */
static void pdf_rgb(Buf *b, u32 c, const char *op)
{
  int k;
  for (k = 2; k >= 0; k--) { int v = (int)((c >> (8 * k)) & 255) * 1000 / 255; buf_printf(b, "%d.%03d ", v / 1000, v % 1000); }
  buf_printf(b, "%s ", op);
}
static void pdf_fix(Buf *b, i32 v)
{
  long a = v < 0 ? -(long)v : v, w = a / TRIG_ONE, f = (a % TRIG_ONE) * 10000 / TRIG_ONE;
  buf_printf(b, "%s%ld.%04ld ", v < 0 ? "-" : "", w, f);
}
void ff_pdf(Buf *b, const ObjPart *pt, i32 cx, i32 cy)
{
  const Image *im = pt->im;
  const Freeform *f = im->ff;
  i32 w = pt->cx, h = pt->cy, lw = im->lwidth * 5 / 2, c = icos(pt->rot), sn = isin(pt->rot);
  int outline = im->lstyle != PB_NONE && im->lwidth, anyfill = 0;
  u32 k, i;
  if (!f || !f->np) return;
  if (lw < 5) lw = 5;
  buf_printf(b, "q 1 0 0 1 %ld %ld cm ", (long)cx, (long)cy);
  pdf_fix(b, c); pdf_fix(b, -sn); pdf_fix(b, sn); pdf_fix(b, c);
  buf_printf(b, "0 0 cm %d 0 0 %d 0 0 cm 1 J 1 j\n", pt->flip & 1 ? -1 : 1, pt->flip & 2 ? -1 : 1);
  /* the closed paths filled, then every path's line */
  for (k = 0; k < f->np; k++) if (f->p[k].closed && im->fill != COLOR_AUTO && f->tool == INK_NONE) anyfill = 1;
  if (anyfill) {
    pdf_rgb(b, im->fill, "rg");
    for (k = 0; k < f->np; k++) {
      if (!f->p[k].closed) continue;
      for (i = 0; i < f->p[k].n; i++) {
        i32 u, v;
        ff_point(f, w, h, k, i, &u, &v);
        buf_printf(b, "%ld %ld %s ", (long)(u - w / 2), (long)(h / 2 - v), i ? "l" : "m");
      }
      buf_adds(b, "h ");
    }
    buf_adds(b, "f\n");
  }
  if (outline) {
    pdf_rgb(b, im->lcolor, "RG");
    buf_printf(b, "%ld w ", (long)lw);
    for (k = 0; k < f->np; k++) {
      for (i = 0; i < f->p[k].n; i++) {
        i32 u, v;
        ff_point(f, w, h, k, i, &u, &v);
        buf_printf(b, "%ld %ld %s ", (long)(u - w / 2), (long)(h / 2 - v), i ? "l" : "m");
      }
      if (f->p[k].closed) buf_adds(b, "h ");
    }
    buf_adds(b, "S\n");
  }
  buf_adds(b, "Q\n");
}
