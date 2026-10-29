/*
 * objfx.c - see objfx.h (WP17 Pictures, WP18 Shapes and text boxes): pictures' adjustments, the
 * raster that draws an object with its fill and effects, and the icon library's drawing. The .docx
 * side is in objdocx.c.
 */
#include <stdlib.h>
#include <string.h>
#include "objfx.h"
#include "layout.h"
#include "image.h"

#define CLAMP8(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (v))
#define LUMA(r, g, b) (((r) * 77 + (g) * 150 + (b) * 29) >> 8)

/* ------------------------------------------------------------------ blurring */
/* a box blur of radius r over w x h values of ch channels (stride ch), in place, across then down */
static void box_blur(u8 *p, i32 w, i32 h, int ch, i32 r)
{
  u8 *line;
  i32 n = w > h ? w : h, x, y, k;
  int c;
  if (r < 1 || w < 1 || h < 1 || !(line = (u8 *)malloc((size_t)n))) return;
  for (c = 0; c < ch; c++) {
    for (y = 0; y < h; y++) {
      u8 *row = p + (size_t)y * w * ch + c;
      long sum = 0;
      for (x = 0; x < w; x++) line[x] = row[x * ch];
      for (k = -r; k <= r; k++) sum += line[k < 0 ? 0 : k >= w ? w - 1 : k];
      for (x = 0; x < w; x++) {
        i32 a = x - r, b = x + r + 1;
        row[x * ch] = (u8)(sum / (2 * r + 1));
        sum += line[b >= w ? w - 1 : b] - line[a < 0 ? 0 : a];
      }
    }
    for (x = 0; x < w; x++) {
      u8 *col = p + (size_t)x * ch + c;
      long sum = 0;
      for (y = 0; y < h; y++) line[y] = col[(size_t)y * w * ch];
      for (k = -r; k <= r; k++) sum += line[k < 0 ? 0 : k >= h ? h - 1 : k];
      for (y = 0; y < h; y++) {
        i32 a = y - r, b = y + r + 1;
        col[(size_t)y * w * ch] = (u8)(sum / (2 * r + 1));
        sum += line[b >= h ? h - 1 : b] - line[a < 0 ? 0 : a];
      }
    }
  }
  free(line);
}
/* three box blurs of radius r/2 come close to a Gaussian of radius r */
static void soft_blur(u8 *p, i32 w, i32 h, int ch, i32 r)
{
  i32 q = r / 2 > 0 ? r / 2 : 1;
  if (r < 1) return;
  box_blur(p, w, h, ch, q); box_blur(p, w, h, ch, q); box_blur(p, w, h, ch, q);
}

/* ------------------------------------------------------------------ adjustments */
int objfx_adjusted(const ObjFx *fx)
{
  return fx->bright || fx->contrast || fx->sharp || fx->cmode || fx->sat || fx->temp || fx->art;
}
/* the edge strength (Sobel on the brightness) of each pixel, 0-255 */
static u8 *edges(const u8 *rgb, i32 w, i32 h)
{
  u8 *g = (u8 *)malloc((size_t)w * h), *e = (u8 *)malloc((size_t)w * h);
  i32 x, y;
  if (!g || !e) { free(g); free(e); return NULL; }
  for (x = 0; x < w * h; x++) g[x] = (u8)LUMA(rgb[x * 3], rgb[x * 3 + 1], rgb[x * 3 + 2]);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      i32 xm = x ? x - 1 : 0, xp = x + 1 < w ? x + 1 : x, ym = y ? y - 1 : 0, yp = y + 1 < h ? y + 1 : y;
#define G(X, Y) ((int)g[(size_t)(Y) * w + (X)])
      int gx = G(xp, ym) + 2 * G(xp, y) + G(xp, yp) - G(xm, ym) - 2 * G(xm, y) - G(xm, yp);
      int gy = G(xm, yp) + 2 * G(x, yp) + G(xp, yp) - G(xm, ym) - 2 * G(x, ym) - G(xp, ym);
#undef G
      int m = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
      e[(size_t)y * w + x] = (u8)(m / 4 > 255 ? 255 : m / 4);
    }
  free(g);
  return e;
}
static void artistic(u8 *rgb, i32 w, i32 h, int art)
{
  i32 n = w * h, i, s = w < h ? w : h;
  u8 *e;
  switch (art) {
  case ART_BLUR: soft_blur(rgb, w, h, 3, s / 40 > 1 ? s / 40 : 1); break;
  case ART_PENCIL: case ART_LINES: case ART_PHOTOCOPY: case ART_GLOWEDGES:
    if (art == ART_PHOTOCOPY) box_blur(rgb, w, h, 3, 1);
    if (!(e = edges(rgb, w, h))) break;
    for (i = 0; i < n; i++) {
      u8 *p = rgb + i * 3;
      int v, m = e[i];
      if (art == ART_PENCIL) v = 255 - CLAMP8(m * 3);
      else if (art == ART_LINES) v = m > 24 ? CLAMP8(200 - m * 2) : 250;
      else if (art == ART_PHOTOCOPY) v = LUMA(p[0], p[1], p[2]) < 90 || m > 40 ? 30 : 245;
      else { p[0] = (u8)(p[0] * m / 128 > 255 ? 255 : p[0] * m / 128); p[1] = (u8)(p[1] * m / 128 > 255 ? 255 : p[1] * m / 128); p[2] = (u8)(p[2] * m / 128 > 255 ? 255 : p[2] * m / 128); continue; }
      p[0] = p[1] = p[2] = (u8)v;
    }
    free(e);
    break;
  case ART_CUTOUT:
    box_blur(rgb, w, h, 3, s / 100 > 1 ? s / 100 : 1);
    for (i = 0; i < n * 3; i++) rgb[i] = (u8)((rgb[i] / 64) * 85);
    break;
  case ART_GRAIN: {
    u32 seed = 12345;
    for (i = 0; i < n; i++) {
      int d;
      seed = seed * 1103515245UL + 12345;
      d = (int)((seed >> 16) % 49) - 24;
      rgb[i * 3] = (u8)CLAMP8(rgb[i * 3] + d); rgb[i * 3 + 1] = (u8)CLAMP8(rgb[i * 3 + 1] + d); rgb[i * 3 + 2] = (u8)CLAMP8(rgb[i * 3 + 2] + d);
    }
    break; }
  case ART_MOSAIC: {
    i32 b = s / 30 > 2 ? s / 30 : 2, bx, by, x, y;
    for (by = 0; by < h; by += b)
      for (bx = 0; bx < w; bx += b) {
        long sum[3] = { 0, 0, 0 }, c = 0;
        for (y = by; y < by + b && y < h; y++) for (x = bx; x < bx + b && x < w; x++) { u8 *p = rgb + ((size_t)y * w + x) * 3; sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; c++; }
        for (y = by; y < by + b && y < h; y++) for (x = bx; x < bx + b && x < w; x++) { u8 *p = rgb + ((size_t)y * w + x) * 3; p[0] = (u8)(sum[0] / c); p[1] = (u8)(sum[1] / c); p[2] = (u8)(sum[2] / c); }
      }
    break; }
  }
}
/* one channel of a duotone: brightness g (0-255) between the dark colour's and the light one's */
static int duo(int g, u32 dark, u32 light, int sh) { int a = (int)((dark >> sh) & 255), b = (int)((light >> sh) & 255); return a + (b - a) * g / 255; }
/* a value through a:lum's brightness and contrast (per cents) */
static int lum(int v, int bright, int contrast)
{
  if (contrast > 0) v = (v - 128) * 100 / (100 - (contrast > 95 ? 95 : contrast)) + 128;
  else if (contrast < 0) v = (v - 128) * (100 + contrast) / 100 + 128;
  return CLAMP8(v + bright * 255 / 100);
}
void objfx_adjust(u8 *rgb, u32 w, u32 h, const ObjFx *fx)
{
  u32 i, n = w * h;
  int bright = fx->bright, contrast = fx->contrast;
  if (!objfx_adjusted(fx) || !rgb) return;
  if (fx->art) artistic(rgb, (i32)w, (i32)h, fx->art);
  if (fx->sharp) {
    u8 *b = (u8 *)malloc((size_t)n * 3);
    if (b) {
      memcpy(b, rgb, (size_t)n * 3);
      box_blur(b, (i32)w, (i32)h, 3, 1);
      for (i = 0; i < n * 3; i++) {
        int v = rgb[i], bl = b[i];
        rgb[i] = (u8)(fx->sharp > 0 ? CLAMP8(v + (v - bl) * fx->sharp / 40) : v + (bl - v) * -fx->sharp / 100);
      }
      free(b);
    }
  }
  if (fx->cmode == PCM_WASHOUT) { bright += 70; contrast -= 70; }
  for (i = 0; i < n; i++) {
    u8 *p = rgb + i * 3;
    int r = p[0], g = p[1], b = p[2], y;
    if (fx->temp) { int t = (6500 - (int)fx->temp) / 40; r = CLAMP8(r + t); b = CLAMP8(b - t); }
    if (fx->sat) {
      y = LUMA(r, g, b);
      r = CLAMP8(y + (r - y) * (int)fx->sat / 100); g = CLAMP8(y + (g - y) * (int)fx->sat / 100); b = CLAMP8(y + (b - y) * (int)fx->sat / 100);
    }
    y = LUMA(r, g, b);
    switch (fx->cmode) {
    case PCM_GREY: r = g = b = y; break;
    case PCM_SEPIA: r = duo(y, 0x2B1D0E, 0xF2E2C6, 16); g = duo(y, 0x2B1D0E, 0xF2E2C6, 8); b = duo(y, 0x2B1D0E, 0xF2E2C6, 0); break;
    case PCM_BW: r = g = b = y >= 128 ? 255 : 0; break;
    case PCM_DARK: r = duo(y, 0, fx->recolor, 16); g = duo(y, 0, fx->recolor, 8); b = duo(y, 0, fx->recolor, 0); break;
    case PCM_LIGHT: r = duo(y, fx->recolor, 0xFFFFFF, 16); g = duo(y, fx->recolor, 0xFFFFFF, 8); b = duo(y, fx->recolor, 0xFFFFFF, 0); break;
    }
    if (bright || contrast) { r = lum(r, bright, contrast); g = lum(g, bright, contrast); b = lum(b, bright, contrast); }
    p[0] = (u8)r; p[1] = (u8)g; p[2] = (u8)b;
  }
}
u32 objfx_corner_colour(const Image *im)
{
  u8 *rgb = im->data ? img_decode(im->data, im->len, 16, 16) : NULL;
  u32 c = 0xFFFFFF;
  int k, s[3];
  static const int at[4] = { 0, 15, 15 * 16, 255 };
  if (!rgb) return c;
  for (k = 0; k < 3; k++) s[k] = (rgb[at[0] * 3 + k] + rgb[at[1] * 3 + k] + rgb[at[2] * 3 + k] + rgb[at[3] * 3 + k]) / 4;
  free(rgb);
  return ((u32)s[0] << 16) | ((u32)s[1] << 8) | (u32)s[2];
}
int objfx_rich(const Image *im)
{
  const ObjFx *f = &im->fx;
  if (f->shadow || f->glow || f->soft) return 1;
  if (im->kind == OBJ_PICTURE) return objfx_adjusted(f) || f->alpha || f->bgkey || (im->shape != SH_RECT && im->shape < SHX_END);
  if (im->kind == OBJ_SHAPE || im->kind == OBJ_TEXTBOX)
    return f->filltype == FT_GRAD || (f->filltype == FT_PIC && im->data) || f->falpha || im->shape == SHX_ICON;
  return 0;
}
i32 objfx_margin(const Image *im)
{
  const ObjFx *f = &im->fx;
  i32 m = 0;
  if (f->shadow) m = f->shdist + f->shblur;
  if (f->glow > m) m = f->glow;
  return m;
}

/* ------------------------------------------------------------------ coverage masks */
/* a polygon (n points, pixels x 16) filled into mask (w x h, coverage 0-255) by the even-odd rule,
   4 rows of samples a pixel and exact coverage across: op 0 adds (max), 1 cuts out */
static void mask_poly(u8 *mask, i32 w, i32 h, const long *xy, int n, int op)
{
  long *xs, y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF;
  int *acc;
  i32 y, x;
  int i, j, m, k;
  if (n < 3) return;
  xs = (long *)malloc((size_t)n * sizeof(long));
  acc = (int *)malloc((size_t)(w + 2) * sizeof(int));
  if (!xs || !acc) { free(xs); free(acc); return; }
  for (i = 0; i < n; i++) { if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1]; if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1]; }
  y0 >>= 4; y1 = (y1 >> 4) + 1;
  if (y0 < 0) y0 = 0;
  if (y1 > h) y1 = h;
  for (y = (i32)y0; y < y1; y++) {
    int any = 0;
    memset(acc, 0, (size_t)(w + 2) * sizeof(int));
    for (k = 0; k < 4; k++) {
      long sy = (long)y * 16 + k * 4 + 2;
      for (i = 0, m = 0; i < n; i++) {
        long ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
        if ((ay <= sy) != (by <= sy)) xs[m++] = ax + (sy - ay) * (bx - ax) / (by - ay);
      }
      for (i = 1; i < m; i++) for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) { long t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
      for (i = 0; i + 1 < m; i += 2) {
        long a = xs[i] < 0 ? 0 : xs[i], b = xs[i + 1] > (long)w * 16 ? (long)w * 16 : xs[i + 1];
        long pa, pb;
        if (b <= a) continue;
        pa = a >> 4; pb = b >> 4;
        any = 1;
        if (pa == pb) { acc[pa] += (int)(b - a); continue; }
        acc[pa] += (int)(16 - (a & 15));
        for (x = (i32)pa + 1; x < pb; x++) acc[x] += 16;
        if (pb < w) acc[pb] += (int)(b & 15);
      }
    }
    if (!any) continue;
    for (x = 0; x < w; x++) {
      int c = acc[x] * 255 / 64;
      u8 *p = mask + (size_t)y * w + x;
      if (c > 255) c = 255;
      if (op) *p = (u8)(*p * (255 - c) / 255);
      else if (c > *p) *p = (u8)c;
    }
  }
  free(xs); free(acc);
}
/* the square root of v, rounded down */
static long isqrt(long v)
{
  long r = 0, b = 1L << 30;
  if (v <= 0) return 0;
  while (b > v) b >>= 2;
  while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}
/* a round dot of radius rad (pixels x 16) at x, y */
static void mask_dot(u8 *mask, i32 w, i32 h, long x, long y, long rad, int op)
{
  long p[2 * 16];
  int i;
  for (i = 0; i < 16; i++) { p[2 * i] = x + rad * icos(i * 225) / TRIG_ONE; p[2 * i + 1] = y + rad * isin(i * 225) / TRIG_ONE; }
  mask_poly(mask, w, h, p, 16, op);
}
/* a line lw pixels x 16 wide from a to b (pixels x 16), with round ends when caps */
static void mask_line(u8 *mask, i32 w, i32 h, long ax, long ay, long bx, long by, long lw, int caps, int op)
{
  long dx = bx - ax, dy = by - ay, len = isqrt(dx * dx + dy * dy), nx, ny, q[8];
  if (lw < 8) lw = 8;
  if (caps) { mask_dot(mask, w, h, ax, ay, lw / 2, op); mask_dot(mask, w, h, bx, by, lw / 2, op); }
  if (!len) return;
  nx = -dy * (lw / 2) / len; ny = dx * (lw / 2) / len;
  q[0] = ax + nx; q[1] = ay + ny; q[2] = bx + nx; q[3] = by + ny; q[4] = bx - nx; q[5] = by - ny; q[6] = ax - nx; q[7] = ay - ny;
  mask_poly(mask, w, h, q, 4, op);
}

/* ------------------------------------------------------------------ icons */
static long inum(const char **p)
{
  long v = 0;
  int neg = 0;
  while (**p == ' ') (*p)++;
  if (**p == '-') { neg = 1; (*p)++; }
  while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
  return neg ? -v : v;
}
void objicon_mask(int i, u8 *mask, i32 w, i32 h)
{
  const char *p = objicon_src(i);
  int op = 0;
  long lw = (long)(w < h ? w : h) * 16 / 16 * 11 / 10;   /* a stroke is 1.1 of the 16 units, pixels x 16 */
#define SX(v) ((v) * (long)w * 16 / 16 + (long)w * 16 / 32)
#define SY(v) ((v) * (long)h * 16 / 16 + (long)h * 16 / 32)
  while (*p) {
    char c = *p++;
    long a, b, e, f, xy[2 * 40];
    int n, k;
    switch (c) {
    case 'c': op = *p == 'w'; if (*p) p++; break;
    case 'L': a = inum(&p); b = inum(&p); e = inum(&p); f = inum(&p); mask_line(mask, w, h, SX(a), SY(b), SX(e), SY(f), lw, 1, op); break;
    case 'R': a = inum(&p); b = inum(&p); e = inum(&p); f = inum(&p);
      mask_line(mask, w, h, SX(a), SY(b), SX(e), SY(b), lw, 1, op); mask_line(mask, w, h, SX(e), SY(b), SX(e), SY(f), lw, 1, op);
      mask_line(mask, w, h, SX(e), SY(f), SX(a), SY(f), lw, 1, op); mask_line(mask, w, h, SX(a), SY(f), SX(a), SY(b), lw, 1, op);
      break;
    case 'F': a = inum(&p); b = inum(&p); e = inum(&p); f = inum(&p);
      xy[0] = SX(a) - lw / 2; xy[1] = SY(b) - lw / 2; xy[2] = SX(e) + lw / 2; xy[3] = xy[1]; xy[4] = xy[2]; xy[5] = SY(f) + lw / 2; xy[6] = xy[0]; xy[7] = xy[5];
      mask_poly(mask, w, h, xy, 4, op);
      break;
    case 'O': case 'E': {
      long r;
      a = inum(&p); b = inum(&p); e = inum(&p);
      r = e * (long)(w < h ? w : h) * 16 / 16;
      for (k = 0; k < 32; k++) { xy[2 * k] = SX(a) + (r + (c == 'E' ? lw / 2 : 0)) * w / (w < h ? w : h) * icos(k * 1125 / 10) / TRIG_ONE; xy[2 * k + 1] = SY(b) + (r + (c == 'E' ? lw / 2 : 0)) * h / (w < h ? w : h) * isin(k * 1125 / 10) / TRIG_ONE; }
      if (c == 'E') mask_poly(mask, w, h, xy, 32, op);
      else for (k = 0; k < 32; k++) mask_line(mask, w, h, xy[2 * k], xy[2 * k + 1], xy[2 * ((k + 1) & 31)], xy[2 * ((k + 1) & 31) + 1], lw, 1, op);
      break; }
    case 'P': case 'Q':
      n = (int)inum(&p);
      for (k = 0; k < n; k++) { a = inum(&p); b = inum(&p); if (k < 40) { xy[2 * k] = SX(a); xy[2 * k + 1] = SY(b); } }
      if (n > 40) n = 40;
      if (c == 'Q') mask_poly(mask, w, h, xy, n, op);
      else for (k = 0; k + 1 < n; k++) mask_line(mask, w, h, xy[2 * k], xy[2 * k + 1], xy[2 * k + 2], xy[2 * k + 3], lw, 1, op);
      break;
    default: break;
    }
  }
#undef SX
#undef SY
}
/* DrawingML for one icon command's path: lines stroked, filled shapes filled (in 1600ths: 100 a unit,
   a unit's half added so strokes sit on the pixel's centre) */
#define IU(v) ((long)(v) * 100 + 50)
void objicon_custgeom(Buf *o, int i)
{
  const char *p = objicon_src(i);
  int cut = 0;
  buf_adds(o, "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"0\" t=\"0\" r=\"r\" b=\"b\"/><a:pathLst>");
  while (*p) {
    char c = *p++;
    long a, b, e, f;
    int n, k;
    switch (c) {
    case 'c': cut = *p == 'w'; if (*p) p++; break;
    case 'L': case 'R':
      a = inum(&p); b = inum(&p); e = inum(&p); f = inum(&p);
      if (cut) break;
      buf_printf(o, "<a:path w=\"1600\" h=\"1600\" fill=\"none\"><a:moveTo><a:pt x=\"%ld\" y=\"%ld\"/></a:moveTo>", IU(a), IU(b));
      if (c == 'L') buf_printf(o, "<a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo>", IU(e), IU(f));
      else buf_printf(o, "<a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:close/>",
                      IU(e), IU(b), IU(e), IU(f), IU(a), IU(f));
      buf_adds(o, "</a:path>");
      break;
    case 'F':
      a = inum(&p); b = inum(&p); e = inum(&p); f = inum(&p);
      buf_printf(o, "<a:path w=\"1600\" h=\"1600\" stroke=\"%d\"%s><a:moveTo><a:pt x=\"%ld\" y=\"%ld\"/></a:moveTo><a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo>"
                    "<a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo><a:close/></a:path>",
                 0, cut ? " fill=\"lighten\"" : "", IU(a) - 50, IU(b) - 50, IU(e) + 50, IU(b) - 50, IU(e) + 50, IU(f) + 50, IU(a) - 50, IU(f) + 50);
      break;
    case 'O': case 'E':
      a = inum(&p); b = inum(&p); e = inum(&p);
      if (cut && c == 'O') break;
      buf_printf(o, "<a:path w=\"1600\" h=\"1600\"%s><a:moveTo><a:pt x=\"%ld\" y=\"%ld\"/></a:moveTo>"
                    "<a:arcTo wR=\"%ld\" hR=\"%ld\" stAng=\"0\" swAng=\"21600000\"/><a:close/></a:path>",
                 c == 'O' ? " fill=\"none\"" : cut ? " stroke=\"0\" fill=\"lighten\"" : " stroke=\"0\"", IU(a) + e * 100 + (c == 'E' ? 50 : 0), IU(b), e * 100 + (c == 'E' ? 50 : 0), e * 100 + (c == 'E' ? 50 : 0));
      break;
    case 'P': case 'Q':
      n = (int)inum(&p);
      buf_printf(o, "<a:path w=\"1600\" h=\"1600\"%s>", c == 'P' ? " fill=\"none\"" : cut ? " stroke=\"0\" fill=\"lighten\"" : " stroke=\"0\"");
      for (k = 0; k < n; k++) {
        a = inum(&p); b = inum(&p);
        buf_printf(o, k ? "<a:lnTo><a:pt x=\"%ld\" y=\"%ld\"/></a:lnTo>" : "<a:moveTo><a:pt x=\"%ld\" y=\"%ld\"/></a:moveTo>", IU(a), IU(b));
      }
      if (c == 'Q') buf_adds(o, "<a:close/>");
      buf_adds(o, "</a:path>");
      break;
    default: break;
    }
  }
  buf_adds(o, "</a:pathLst></a:custGeom>");
}
#undef IU

/* ------------------------------------------------------------------ the raster */
void objraster_free(ObjRaster *r) { free(r->px); r->px = NULL; r->w = r->h = 0; }
/* src (straight RGBA colour c with coverage a, 0-255) laid over dst (straight RGBA) */
static void over(u8 *d, int r, int g, int b, int a)
{
  int da = d[3], oa;
  if (a <= 0) return;
  if (a >= 255 || !da) { d[0] = (u8)r; d[1] = (u8)g; d[2] = (u8)b; d[3] = (u8)(a > 255 ? 255 : a); return; }
  oa = a + da * (255 - a) / 255;
  d[0] = (u8)((r * a + d[0] * da * (255 - a) / 255) / oa);
  d[1] = (u8)((g * a + d[1] * da * (255 - a) / 255) / oa);
  d[2] = (u8)((b * a + d[2] * da * (255 - a) / 255) / oa);
  d[3] = (u8)oa;
}
/* the colour at t (0-1000) along a gradient's stops */
static u32 grad_at(const ObjFx *f, long t)
{
  int n = f->ngs >= 2 && f->ngs <= 4 ? f->ngs : 2, k;
  u32 a, b;
  long pa, pb;
  if (f->ngs < 2) return f->gcol[0];
  if (t <= f->gpos[0] * 10L) return f->gcol[0];
  for (k = 0; k + 1 < n; k++) {
    pa = f->gpos[k] * 10L; pb = f->gpos[k + 1] * 10L;
    if (t <= pb) {
      int s = pb > pa ? (int)((t - pa) * 256 / (pb - pa)) : 256, c, o = 0;
      a = f->gcol[k]; b = f->gcol[k + 1];
      for (c = 16; c >= 0; c -= 8) o |= (((int)((a >> c) & 255) * (256 - s) + (int)((b >> c) & 255) * s) >> 8) << c;
      return (u32)o;
    }
  }
  return f->gcol[n - 1];
}
/* a picture's part that shows (cropped) at uw x uh pixels, as RGB; NULL if it can't be decoded */
u8 *objfx_picture_rgb(const Image *im, i32 uw, i32 uh, int crop)
{
  long cl = crop ? im->crop[0] : 0, ct = crop ? im->crop[1] : 0, cr = crop ? im->crop[2] : 0, cb = crop ? im->crop[3] : 0;
  long fw, fh, x0, y0;
  u8 *full, *out;
  i32 y;
  if (!im->data) return NULL;
  if (cl < 0) cl = 0;
  if (ct < 0) ct = 0;
  if (cr < 0) cr = 0;
  if (cb < 0) cb = 0;
  if (cl + cr >= 99000) cl = cr = 0;
  if (ct + cb >= 99000) ct = cb = 0;
  fw = (long)uw * 100000L / (100000L - cl - cr); fh = (long)uh * 100000L / (100000L - ct - cb);
  if (fw < uw) fw = uw;
  if (fh < uh) fh = uh;
  if (fw * fh > 6000000L) return NULL;
  if (!(full = img_decode(im->data, im->len, (u32)fw, (u32)fh))) return NULL;
  if (fw == uw && fh == uh) return full;
  x0 = fw * cl / 100000L; y0 = fh * ct / 100000L;
  if (x0 + uw > fw) x0 = fw - uw;
  if (y0 + uh > fh) y0 = fh - uh;
  if (!(out = (u8 *)malloc((size_t)uw * uh * 3))) { free(full); return NULL; }
  for (y = 0; y < uh; y++) memcpy(out + (size_t)y * uw * 3, full + ((size_t)(y + y0) * fw + x0) * 3, (size_t)uw * 3);
  free(full);
  return out;
}
#define SC(v) ((long)(v) * num / den)
int objfx_render(const Image *im, long num, long den, ObjRaster *r)
{
  const ObjFx *f = &im->fx;
  i32 uw = (i32)SC(im->cx), uh = (i32)SC(im->cy), m, cw, ch, x, y;
  long lw = im->lstyle != PB_NONE && im->lwidth ? SC((long)im->lwidth * 5 / 2) * 16 : 0;
  int shape = im->kind == OBJ_TEXTBOX ? SH_RECT : im->shape, closed = 1, i;
  u8 *cv = NULL, *mask = NULL, *line = NULL, *tmp = NULL, *fill = NULL;
  i32 pts[2 * SHAPE_MAXPTS];
  long xy[2 * SHAPE_MAXPTS];
  u32 np;
  memset(r, 0, sizeof *r);
  if (uw < 1) uw = 1;
  if (uh < 1) uh = 1;
  m = (i32)SC(objfx_margin(im) + (im->lstyle != PB_NONE ? im->lwidth * 3 : 0)) + 2;
  cw = uw + 2 * m; ch = uh + 2 * m;
  if ((long)cw * ch > 4000000L) return 0;
  cv = (u8 *)calloc((size_t)cw * ch, 4);
  mask = (u8 *)calloc((size_t)cw * ch, 1);
  line = (u8 *)calloc((size_t)cw * ch, 1);
  if (!cv || !mask || !line) goto fail;
  /* the shape's coverage (an icon's drawing), its outline's */
  if (shape == SHX_ICON) {
    u8 *ic = (u8 *)calloc((size_t)uw * uh, 1);
    if (!ic) goto fail;
    objicon_mask(f->icon ? f->icon - 1 : 0, ic, uw, uh);
    for (y = 0; y < uh; y++) memcpy(mask + (size_t)(y + m) * cw + m, ic + (size_t)y * uw, (size_t)uw);
    free(ic);
    np = 0;
  } else {
    np = shape_points(shape, im->cx, im->cy, pts, SHAPE_MAXPTS, &closed);
    for (i = 0; i < (int)np; i++) { xy[2 * i] = SC((long)pts[2 * i] * 16) + m * 16L; xy[2 * i + 1] = SC((long)pts[2 * i + 1] * 16) + m * 16L; }
    if (closed) mask_poly(mask, cw, ch, xy, (int)np, 0);
    if (lw) {
      for (i = 0; i + 1 < (int)np + closed; i++) mask_line(line, cw, ch, xy[2 * i], xy[2 * i + 1], xy[2 * ((i + 1) % np)], xy[2 * ((i + 1) % np) + 1], lw, 1, 0);
      if (im->kind == OBJ_SHAPE && shape == SH_ARROW) {
        i32 ah[6];
        long hx[6];
        shape_arrowhead(im->cx, im->cy, im->lwidth * 5 / 2, ah);
        for (i = 0; i < 3; i++) { hx[2 * i] = SC((long)ah[2 * i] * 16) + m * 16L; hx[2 * i + 1] = SC((long)ah[2 * i + 1] * 16) + m * 16L; }
        mask_poly(line, cw, ch, hx, 3, 0);
      }
    }
  }
  /* soft edges: the coverage fades over their size inside the edge */
  if (f->soft && (tmp = (u8 *)malloc((size_t)cw * ch))) {
    memcpy(tmp, mask, (size_t)cw * ch);
    soft_blur(tmp, cw, ch, 1, (i32)SC(f->soft));
    for (i = 0; i < cw * ch; i++) { int v = (tmp[i] - 128) * 2; mask[i] = (u8)(mask[i] * CLAMP8(v) / 255); line[i] = (u8)(line[i] * CLAMP8(v) / 255); }
    free(tmp); tmp = NULL;
  }
  /* glow and shadow behind it, from the shape and outline's coverage */
  if ((f->glow || f->shadow) && (tmp = (u8 *)malloc((size_t)cw * ch))) {
    for (i = 0; i < cw * ch; i++) tmp[i] = mask[i] > line[i] ? mask[i] : line[i];
    if (im->kind == OBJ_PICTURE || (im->kind != OBJ_GROUP && (f->filltype == FT_PIC || im->fill != COLOR_AUTO || f->filltype == FT_GRAD || shape == SHX_ICON))) ;
    else for (i = 0; i < cw * ch; i++) if (!line[i]) tmp[i] = 0;    /* (no fill: only the outline casts) */
    if (f->glow) {
      u8 *g = (u8 *)malloc((size_t)cw * ch);
      if (g) {
        int op = 255 - f->glalpha * 255 / 100;
        memcpy(g, tmp, (size_t)cw * ch);
        soft_blur(g, cw, ch, 1, (i32)SC(f->glow));
        for (i = 0; i < cw * ch; i++) { int a = g[i] * 2 > 255 ? 255 : g[i] * 2; over(cv + i * 4, (int)(f->glcolor >> 16) & 255, (int)(f->glcolor >> 8) & 255, (int)f->glcolor & 255, a * op / 255); }
        free(g);
      }
    }
    if (f->shadow) {
      /* the shadow's offset on the page, turned back into the object's own space */
      long dx = (long)f->shdist * icos(f->shdir) / TRIG_ONE, dy = (long)f->shdist * isin(f->shdir) / TRIG_ONE, c = icos(im->rot), s = isin(im->rot);
      long ux = (dx * c + dy * s) / TRIG_ONE, uy = (dy * c - dx * s) / TRIG_ONE;
      int op = 255 - f->shalpha * 255 / 100;
      i32 ox, oy;
      if (im->flip & 1) ux = -ux;
      if (im->flip & 2) uy = -uy;
      ox = (i32)SC(ux); oy = (i32)SC(uy);
      if (f->shblur) soft_blur(tmp, cw, ch, 1, (i32)SC(f->shblur));
      for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
          i32 sx = x - ox, sy = y - oy;
          if (sx < 0 || sy < 0 || sx >= cw || sy >= ch || !tmp[(size_t)sy * cw + sx]) continue;
          over(cv + ((size_t)y * cw + x) * 4, (int)(f->shcolor >> 16) & 255, (int)(f->shcolor >> 8) & 255, (int)f->shcolor & 255, tmp[(size_t)sy * cw + sx] * op / 255);
        }
    }
    free(tmp); tmp = NULL;
  }
  /* the fill: the picture, a picture fill, a gradient or a colour */
  if (im->kind == OBJ_PICTURE || (f->filltype == FT_PIC && im->data)) {
    u8 *alpha = NULL;
    fill = objfx_picture_rgb(im, uw, uh, im->kind == OBJ_PICTURE);
    if (fill && (f->bgkey || f->alpha || f->falpha) && (alpha = (u8 *)malloc((size_t)uw * uh))) {
      int op = 255 - (im->kind == OBJ_PICTURE ? f->alpha : f->falpha) * 255 / 100, tol = f->bgtol * 3 + 8;
      for (i = 0; i < uw * uh; i++) {
        int a = op;
        if (f->bgkey) {
          u8 *p = fill + i * 3;
          int d = abs(p[0] - (int)((f->bgcolor >> 16) & 255)) + abs(p[1] - (int)((f->bgcolor >> 8) & 255)) + abs(p[2] - (int)(f->bgcolor & 255));
          if (d <= tol) a = 0;
          else if (d < tol * 2) a = a * (d - tol) / tol;
        }
        alpha[i] = (u8)a;
      }
    }
    if (fill) objfx_adjust(fill, (u32)uw, (u32)uh, f);
    for (y = 0; fill && y < uh; y++)
      for (x = 0; x < uw; x++) {
        size_t k = (size_t)(y + m) * cw + x + m;
        const u8 *p = fill + ((size_t)y * uw + x) * 3;
        int a = mask[k];
        if (alpha) a = a * alpha[(size_t)y * uw + x] / 255;
        over(cv + k * 4, p[0], p[1], p[2], a);
      }
    if (!fill && im->kind == OBJ_PICTURE)       /* can't be shown: grey */
      for (y = 0; y < uh; y++) for (x = 0; x < uw; x++) { size_t k = (size_t)(y + m) * cw + x + m; over(cv + k * 4, 200, 200, 200, mask[k]); }
    free(alpha);
  } else if (f->filltype == FT_GRAD || im->fill != COLOR_AUTO) {
    int op = 255 - f->falpha * 255 / 100;
    long c = icos(f->gangle), s = isin(f->gangle), span = ((long)uw * (c < 0 ? -c : c) + (long)uh * (s < 0 ? -s : s)) / TRIG_ONE;
    long diag = isqrt((long)uw * uw + (long)uh * uh) / 2;
    u32 col = im->fill == COLOR_AUTO ? 0 : im->fill;
    if (span < 1) span = 1;
    if (diag < 1) diag = 1;
    for (y = 0; y < uh; y++)
      for (x = 0; x < uw; x++) {
        size_t k = (size_t)(y + m) * cw + x + m;
        long dx = 2 * x + 1 - uw, dy = 2 * y + 1 - uh, t;     /* (doubled, from the centre) */
        if (!mask[k]) continue;
        if (f->filltype == FT_GRAD) {
          if (f->gtype == GT_RADIAL) t = isqrt(dx * dx + dy * dy) * 500 / diag;
          else if (f->gtype == GT_RECT) { long a = labs(dx) * 1000 / (uw > 0 ? uw : 1), b = labs(dy) * 1000 / (uh > 0 ? uh : 1); t = a > b ? a : b; }
          else t = 500 + (dx * c + dy * s) / TRIG_ONE * 500 / span;
          col = grad_at(f, t < 0 ? 0 : t > 1000 ? 1000 : t);
        }
        over(cv + k * 4, (int)(col >> 16) & 255, (int)(col >> 8) & 255, (int)col & 255, mask[k] * op / 255);
      }
  }
  free(fill); fill = NULL;
  /* the outline over it */
  if (lw) {
    u32 lc = im->lcolor;
    for (i = 0; i < cw * ch; i++) if (line[i]) over(cv + i * 4, (int)(lc >> 16) & 255, (int)(lc >> 8) & 255, (int)lc & 255, line[i]);
  }
  free(mask); free(line); mask = line = NULL;
  /* turned and flipped into the raster */
  {
    Image t = *im;
    long c = icos(im->rot), s = isin(im->rot), W, H;
    i32 bw, bh;
    t.cx = cw; t.cy = ch;
    W = obj_w(&t); H = obj_h(&t);
    bw = (i32)SC(obj_w(im)); bh = (i32)SC(obj_h(im));
    if (!im->rot && !im->flip) { r->px = cv; r->w = cw; r->h = ch; r->ox = (bw - cw) / 2; r->oy = (bh - ch) / 2; return 1; }
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W * H > 4000000L || !(r->px = (u8 *)calloc((size_t)W * H, 4))) goto fail;
    r->w = (i32)W; r->h = (i32)H; r->ox = (bw - (i32)W) / 2; r->oy = (bh - (i32)H) / 2;
    for (y = 0; y < H; y++) {
      long dy = 2 * y + 1 - H;
      for (x = 0; x < W; x++) {
        long dx = 2 * x + 1 - W, u = (dx * c + dy * s) / TRIG_ONE, v = (dy * c - dx * s) / TRIG_ONE;   /* (doubled) */
        i32 su, sv;
        if (im->flip & 1) u = -u;
        if (im->flip & 2) v = -v;
        su = (i32)((u + cw) >> 1); sv = (i32)((v + ch) >> 1);
        if (su < 0 || sv < 0 || su >= cw || sv >= ch) continue;
        memcpy(r->px + ((size_t)y * W + x) * 4, cv + ((size_t)sv * cw + su) * 4, 4);
      }
    }
    free(cv);
  }
  return 1;
fail:
  free(cv); free(mask); free(line); free(tmp); free(fill);
  memset(r, 0, sizeof *r);
  return 0;
}
#undef SC
