/* mathlay.c - see math.h: equations laid out TeX-style (The TeXbook, appendix G, simplified) with
   Quill's own fonts. No OpenType MATH table is needed: the constants are fractions of the em (as
   OpenType's MATH tables give them for Cambria Math and TeX's fonts), the glyphs' real outline boxes
   are measured, and what the fonts lack is found in another of Quill's fonts, drawn from a glyph
   that looks like it (∇ is Δ upside down), or drawn as outlines here (∈ ⊂ ⟨ ⌊ ⇒ ...). Delimiters,
   radical signs, braces and arrows that grow are drawn as outlines too. All integer twips. */
#include <stdlib.h>
#include <string.h>
#include "math.h"
#include "ttraster.h"
#include "mathedit.h"     /* WP14b: numbered equations */

/* ------------------------------------------------------------------ */
/* the outline readers (one per face used), and glyph boxes measured once */
#define NMF 16
static struct { Face *f; TTRaster r; int ok; } mf[NMF];
static int nmf;
static TTRaster *raster(Face *f)
{
  int i;
  for (i = 0; i < nmf; i++) if (mf[i].f == f) return mf[i].ok ? &mf[i].r : NULL;
  if (nmf >= NMF) { ttr_close(&mf[0].r); memmove(mf, mf + 1, (NMF - 1) * sizeof mf[0]); nmf--; }
  memset(&mf[nmf], 0, sizeof mf[0]);
  mf[nmf].f = f;
  mf[nmf].ok = ttf_load(f) && ttr_open(&mf[nmf].r, f->path, f->index);
  nmf++;
  return mf[nmf - 1].ok ? &mf[nmf - 1].r : NULL;
}
#define NGM 1024
static struct { Face *f; u16 g; u8 ok, has; i16 x0, y0, x1, y1; } gm[NGM];
/* glyph g's outline box in font units (y up); 0 if it has none (a space) */
static int gbox(Face *f, u16 g, i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
  unsigned h = (unsigned)(((size_t)f >> 4) * 31 + g) % NGM;
  if (!gm[h].ok || gm[h].f != f || gm[h].g != g) {
    TTRaster *r = raster(f);
    i32 a = 0, b = 0, c = 0, d = 0;
    gm[h].f = f; gm[h].g = g; gm[h].ok = 1;
    gm[h].has = (u8)(r && ttr_bbox(r, g, &a, &b, &c, &d));
    gm[h].x0 = (i16)a; gm[h].y0 = (i16)b; gm[h].x1 = (i16)c; gm[h].y1 = (i16)d;
  }
  *x0 = gm[h].x0; *y0 = gm[h].y0; *x1 = gm[h].x1; *y1 = gm[h].y1;
  return gm[h].has;
}
void math_fonts_free(void)
{
  int i;
  for (i = 0; i < nmf; i++) if (mf[i].ok) ttr_close(&mf[i].r);
  nmf = 0;
  memset(gm, 0, sizeof gm);
}
struct TTRaster *math_raster(Face *f) { return raster(f); }
int math_glyph_segments(Face *f, u16 g, i32 *seg, int max)
{
  TTRaster *r = raster(f);
  return r ? ttr_segments(r, g, seg, max) : -1;
}

/* ------------------------------------------------------------------ */
/* filling outlines (the screen's): each pixel the outline covers at any of four sample rows through
   it gets drawn, so strokes thinner than a pixel never break up (equations' lines are thin) */
void math_fill(const i32 *seg, int nseg, int y0, int y1, void (*span)(void *ctx, int y, int x0, int x1), void *ctx)
{
  i32 xs[130], iv[2 * 260];
  int y, i, k;
  for (y = y0; y <= y1; y++) {
    int niv = 0, sub;
    for (sub = 0; sub < 4; sub++) {
      i32 sy = y * 16 + 2 + sub * 4;
      int n = 0, wind = 0;
      for (i = 0; i < nseg && n < 128; i++) {
        i32 ax = seg[4 * i], ay = seg[4 * i + 1], bx = seg[4 * i + 2], by = seg[4 * i + 3], x;
        int up = by > ay;
        if (ay == by || (sy < ay && sy < by) || (sy >= ay && sy >= by)) continue;
        x = ax + (i32)((long long)(bx - ax) * (sy - ay) / (by - ay));
        xs[n++] = x * 2 + up;
      }
      for (i = 1; i < n; i++) { i32 v = xs[i]; k = i; while (k > 0 && xs[k - 1] > v) { xs[k] = xs[k - 1]; k--; } xs[k] = v; }
      /* where the winding isn't 0: every pixel the stretch touches */
      for (i = 0; i + 1 < n && niv < 256; i++) {
        wind += xs[i] & 1 ? 1 : -1;
        if (wind) {
          i32 a = xs[i] >> 1, b = xs[i + 1] >> 1;
          iv[2 * niv] = a >= 0 ? a / 16 : -((-a + 15) / 16);
          iv[2 * niv + 1] = (b > 0 ? (b + 15) / 16 : -(-b / 16)) - 1;
          if (iv[2 * niv + 1] < iv[2 * niv]) iv[2 * niv + 1] = iv[2 * niv];
          niv++;
        }
      }
    }
    /* the stretches of the four rows merged */
    for (i = 1; i < niv; i++) {
      i32 a = iv[2 * i], b = iv[2 * i + 1];
      k = i;
      while (k > 0 && iv[2 * (k - 1)] > a) { iv[2 * k] = iv[2 * (k - 1)]; iv[2 * k + 1] = iv[2 * (k - 1) + 1]; k--; }
      iv[2 * k] = a; iv[2 * k + 1] = b;
    }
    for (i = 0; i < niv; ) {
      i32 a = iv[2 * i], b = iv[2 * i + 1];
      for (k = i + 1; k < niv && iv[2 * k] <= b + 1; k++) if (iv[2 * k + 1] > b) b = iv[2 * k + 1];
      span(ctx, y, (int)a, (int)b);
      i = k;
    }
  }
}

/* ------------------------------------------------------------------ */
/* the layout's lists */
void math_lay_free(MLay *l)
{
  if (!l) return;
  free(l->it); free(l->pt); free(l);
}
static MItem *add_item(MLay *l)
{
  MItem *it;
  if (l->n >= l->cap) {
    u32 nc = l->cap ? l->cap * 2 : 64;
    MItem *ni = (MItem *)realloc(l->it, nc * sizeof(MItem));
    if (!ni) { l->oom = 1; return NULL; }
    l->it = ni; l->cap = nc;
  }
  it = &l->it[l->n++];
  memset(it, 0, sizeof *it);
  return it;
}
/* a filled outline of n points (xy pairs, twips), in colour */
static void add_poly(MLay *l, const i32 *xy, int n, u32 color)
{
  MItem *it;
  if (n < 3) return;
  if (l->npt + 2 * n > l->ptcap) {
    u32 nc = l->ptcap ? l->ptcap * 2 : 256;
    i32 *np;
    while (nc < l->npt + 2 * n) nc *= 2;
    if (!(np = (i32 *)realloc(l->pt, nc * sizeof(i32)))) { l->oom = 1; return; }
    l->pt = np; l->ptcap = nc;
  }
  if (!(it = add_item(l))) return;
  it->kind = MI_POLY; it->w = (i32)l->npt; it->h = n; it->color = color;
  memcpy(l->pt + l->npt, xy, 2 * n * sizeof(i32));
  l->npt += 2 * n;
}
static void add_rect(MLay *l, i32 x, i32 y, i32 w, i32 h, u32 color)
{
  MItem *it;
  if (w <= 0 || h <= 0 || !(it = add_item(l))) return;
  it->kind = MI_RECT; it->x = x; it->y = y; it->w = w; it->h = h; it->color = color;
}
/* items [a, b) moved by dx, dy */
static void shift(MLay *l, u32 a, u32 b, i32 dx, i32 dy)
{
  u32 i;
  int k;
  if (!dx && !dy) return;
  for (i = a; i < b && i < l->n; i++) {
    MItem *it = &l->it[i];
    if (it->kind == MI_POLY) for (k = 0; k < it->h; k++) { l->pt[it->w + 2 * k] += dx; l->pt[it->w + 2 * k + 1] += dy; }
    else { it->x += dx; it->y += dy; }
  }
}
static i32 isqrt(long long v)
{
  long long r = 0, b = 1LL << 40;
  if (v <= 0) return 0;
  while (b > v) b >>= 2;
  while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return (i32)r;
}
/* a line from (x0, y0) to (x1, y1), w wide, its ends square (so lines meeting overlap) */
static void stroke(MLay *l, i32 x0, i32 y0, i32 x1, i32 y1, i32 w, u32 color)
{
  long long dx = x1 - x0, dy = y1 - y0;
  i32 len = isqrt(dx * dx + dy * dy), xy[8], nx, ny, ex, ey;
  if (w < 1) w = 1;
  if (!len) { dx = 1; dy = 0; len = 1; }
  nx = (i32)(-dy * w / 2 / len); ny = (i32)(dx * w / 2 / len);
  ex = (i32)(dx * w / 2 / len); ey = (i32)(dy * w / 2 / len);
  xy[0] = x0 - ex + nx; xy[1] = y0 - ey + ny; xy[2] = x1 + ex + nx; xy[3] = y1 + ey + ny;
  xy[4] = x1 + ex - nx; xy[5] = y1 + ey - ny; xy[6] = x0 - ex - nx; xy[7] = y0 - ey - ny;
  add_poly(l, xy, 4, color);
}
/* an elliptic arc (centre, radii; angles in degrees, counter-clockwise from 3 o'clock, y up as in
   the symbols' own space and turned to the page's y down here) as short lines */
static void arc(MLay *l, i32 cx, i32 cy, i32 rx, i32 ry, int a0, int a1, i32 w, u32 color)
{
  int steps = (a1 - a0) / 15, i;
  i32 px = 0, py = 0;
  if (steps < 2) steps = 2;
  for (i = 0; i <= steps; i++) {
    int a = a0 + (a1 - a0) * i / steps;
    i32 x = cx + (i32)((long long)rx * icos(a * 10) / TRIG_ONE), y = cy - (i32)((long long)ry * isin(a * 10) / TRIG_ONE);
    if (i) stroke(l, px, py, x, y, w, color);
    px = x; py = y;
  }
}
static void dot(MLay *l, i32 cx, i32 cy, i32 r, u32 color)
{
  i32 xy[24];
  int i;
  for (i = 0; i < 12; i++) { xy[2 * i] = cx + (i32)((long long)r * icos(i * 300) / TRIG_ONE); xy[2 * i + 1] = cy - (i32)((long long)r * isin(i * 300) / TRIG_ONE); }
  add_poly(l, xy, 12, color);
}
/* a quadratic curve's points (p0, control, p1), n segments, into xy (n + 1 points) */
static void quad_pts(i32 x0, i32 y0, i32 cx, i32 cy, i32 x1, i32 y1, int n, i32 *xy)
{
  int i;
  for (i = 0; i <= n; i++) {
    long long u = n - i, t = i, nn = (long long)n * n;
    xy[2 * i] = (i32)((u * u * x0 + 2 * u * t * cx + t * t * x1) / nn);
    xy[2 * i + 1] = (i32)((u * u * y0 + 2 * u * t * cy + t * t * y1) / nn);
  }
}

/* ------------------------------------------------------------------ */
/* the layout's state */
enum { C_ORD, C_OP, C_BIN, C_REL, C_OPEN, C_CLOSE, C_PUNCT, C_INNER, C_MARK };
typedef struct { i32 w, asc, desc, ic; u8 lc, rc; } Bx;
typedef struct { u8 lvl, cr; signed char sz; } St;            /* lvl: 0 display, 1 text, 2 script, 3 scriptscript; cr: cramped; sz: argSz steps */
typedef struct {
  FontSet *fs; MLay *L; Doc *d;
  MathPr mp;
  Face *serif[4], *sans[4], *mono, *carlito;
  Face *text;                 /* normal text's family (the paragraph's) */
  i32 base;                   /* the text's em, twips */
  u32 color;
  int nodes;                  /* a guard against monstrous input */
  i32 axis1000, xh1000;       /* the math axis and x-height, thousandths of an em */
  /* breaks a row records (equation arrays' & columns, display lines' m:brk) */
  int rec;
  struct { u32 item; i32 x, asc, desc; } brk[64]; int nbrk;
  i32 segasc, segdesc;
  /* WP14b: the numbered row of a numbered display equation (its # starts the number), the line's
     width (0 unknown) and the equation's justification (MPJC_) */
  MNode *eqno; i32 eqroom; int eqjc;
} Ctx;
static i32 em_of(Ctx *c, St s)
{
  static const int pct[5] = { 160, 137, 100, 73, 60 };
  int idx = (s.lvl <= 1 ? 0 : s.lvl - 1) - s.sz;
  if (idx < -2) idx = -2;
  if (idx > 2) idx = 2;
  return c->base * pct[idx + 2] / 100;
}
#define EM(v) ((i32)((long long)em * (v) / 1000))
static St st_sup(St s) { St r = s; r.lvl = (u8)(s.lvl <= 1 ? 2 : 3); return r; }
static St st_sub(St s) { St r = st_sup(s); r.cr = 1; return r; }
static St st_num(Ctx *c, St s) { St r = s; r.lvl = (u8)(s.lvl == 0 && !c->mp.smallfrac ? 1 : s.lvl <= 1 ? 2 : 3); return r; }
static St st_den(Ctx *c, St s) { St r = st_num(c, s); r.cr = 1; return r; }
static int face_index(Ctx *c, Face *f)
{
  MLay *l = c->L;
  int i;
  for (i = 0; i < l->nfaces; i++) if (l->faces[i] == f) return i;
  if (l->nfaces >= ML_FACES) return 0;
  l->faces[l->nfaces] = f;
  return l->nfaces++;
}
static Face *loaded(Face *f) { return f && ttf_load(f) ? f : NULL; }
/* the face and glyph that draw cp, trying pref, then Quill's other fonts, then every font there is
   (each code point looked for once) */
#define NCP 256
static struct { u32 cp; Face *pref, *f; u16 g; } cpc[NCP];
static FontSet *cpc_fs;
static int find_glyph(Ctx *c, u32 cp, Face *pref, Face **pf, u16 *pg)
{
  unsigned h = (unsigned)(cp * 7 + ((size_t)pref >> 4)) % NCP;
  Face *cand[8];
  int n = 0, i;
  if (cpc_fs != c->fs) { memset(cpc, 0, sizeof cpc); cpc_fs = c->fs; }
  if (cpc[h].cp == cp && cpc[h].pref == pref && cp) { *pf = cpc[h].f; *pg = cpc[h].g; return cpc[h].f != NULL; }
  cand[n++] = pref; cand[n++] = c->serif[0]; cand[n++] = c->sans[0]; cand[n++] = c->carlito; cand[n++] = c->mono;
  *pf = NULL; *pg = 0;
  for (i = 0; i < n && !*pf; i++) if (loaded(cand[i]) && (*pg = ttf_glyph(cand[i], cp))) *pf = cand[i];
  for (i = 0; i < c->fs->nfaces && !*pf; i++) {
    Face *f = &c->fs->faces[i];
    if (f->installed && i > 40) break;                    /* (not hundreds of system fonts) */
    if (loaded(f) && (*pg = ttf_glyph(f, cp))) *pf = f;
  }
  cpc[h].cp = cp; cpc[h].pref = pref; cpc[h].f = *pf; cpc[h].g = *pg;
  return *pf != NULL;
}
/* a glyph drawn with its pen at (x, y) (y down), em twips to the em; its box in twips */
static void put_glyph(Ctx *c, Face *f, u16 g, u32 cp, i32 em, i32 x, i32 y, u32 color, int flags)
{
  MItem *it = add_item(c->L);
  if (!it) return;
  it->kind = MI_GLYPH; it->face = (u8)face_index(c, f); it->glyph = g; it->cp = cp; it->size = em;
  it->x = x; it->y = y; it->color = color; it->flags = (u8)flags;
}
/* a glyph's metrics at em: advance, ink box (y up from the baseline) */
static void glyph_met(Face *f, u16 g, i32 em, i32 *adv, i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
  i32 a, b, cc, d;
  u16 up = f->upm ? f->upm : 2048;
  *adv = f->nmetrics ? (i32)((long long)f->adv[g < f->nmetrics ? g : f->nmetrics - 1] * em / up) : em / 2;
  if (gbox(f, g, &a, &b, &cc, &d)) {
    *x0 = (i32)((long long)a * em / up); *y0 = (i32)((long long)b * em / up);
    *x1 = (i32)((long long)cc * em / up); *y1 = (i32)((long long)d * em / up);
  } else { *x0 = *y0 = *x1 = *y1 = 0; }
}

/* ------------------------------------------------------------------ */
/* symbols drawn as outlines (what the fonts lack): in thousandths of an em, y up from the baseline.
   Ops: 1 line x0 y0 x1 y1; 2 arc cx cy rx ry a0 a1; 3 dot cx cy r; 4 wave x0 x1 y amplitude; 0 end */
static const i16 v_in[] = { 2, 450, 260, 250, 250, 90, 270, 1, 450, 510, 620, 510, 1, 450, 10, 620, 10, 1, 210, 260, 620, 260, 0 };
static const i16 v_notin[] = { 2, 450, 260, 250, 250, 90, 270, 1, 450, 510, 620, 510, 1, 450, 10, 620, 10, 1, 210, 260, 620, 260, 1, 250, -120, 560, 640, 0 };
static const i16 v_subset[] = { 2, 400, 260, 250, 250, 90, 270, 1, 400, 510, 650, 510, 1, 400, 10, 650, 10, 0 };
static const i16 v_subseteq[] = { 2, 400, 340, 230, 230, 90, 270, 1, 400, 570, 650, 570, 1, 400, 110, 650, 110, 1, 150, -50, 650, -50, 0 };
static const i16 v_wedge[] = { 1, 110, 0, 350, 560, 1, 350, 560, 590, 0, 0 };
static const i16 v_vee[] = { 1, 110, 560, 350, 0, 1, 350, 0, 590, 560, 0 };
static const i16 v_implies[] = { 1, 80, 350, 600, 350, 1, 80, 150, 600, 150, 1, 460, 530, 740, 250, 1, 740, 250, 460, -30, 0 };
static const i16 v_impliedby[] = { 1, 220, 350, 740, 350, 1, 220, 150, 740, 150, 1, 360, 530, 80, 250, 1, 80, 250, 360, -30, 0 };
static const i16 v_langle[] = { 1, 300, 740, 90, 250, 1, 90, 250, 300, -240, 0 };
static const i16 v_rangle[] = { 1, 80, 740, 290, 250, 1, 290, 250, 80, -240, 0 };
static const i16 v_lceil[] = { 1, 130, 740, 130, -240, 1, 130, 740, 340, 740, 0 };
static const i16 v_rceil[] = { 1, 250, 740, 250, -240, 1, 40, 740, 250, 740, 0 };
static const i16 v_lfloor[] = { 1, 130, 740, 130, -240, 1, 130, -240, 340, -240, 0 };
static const i16 v_rfloor[] = { 1, 250, 740, 250, -240, 1, 40, -240, 250, -240, 0 };
static const i16 v_propto[] = { 2, 300, 250, 170, 150, 45, 315, 1, 420, 356, 640, 120, 1, 420, 144, 640, 380, 0 };
static const i16 v_perp[] = { 1, 380, 620, 380, 0, 1, 90, 0, 670, 0, 0 };
static const i16 v_top[] = { 1, 380, 620, 380, 0, 1, 90, 620, 670, 620, 0 };
static const i16 v_angle[] = { 1, 110, 0, 650, 0, 1, 110, 0, 560, 540, 0 };
static const i16 v_vdash[] = { 1, 110, 620, 110, -40, 1, 110, 290, 600, 290, 0 };
static const i16 v_models[] = { 1, 110, 620, 110, -40, 1, 110, 390, 600, 390, 1, 110, 180, 600, 180, 0 };
static const i16 v_therefore[] = { 3, 170, 60, 55, 3, 530, 60, 55, 3, 350, 440, 55, 0 };
static const i16 v_because[] = { 3, 170, 440, 55, 3, 530, 440, 55, 3, 350, 60, 55, 0 };
static const i16 v_sim[] = { 4, 100, 620, 250, 80, 0 };
static const i16 v_simeq[] = { 4, 100, 620, 360, 70, 1, 100, 140, 620, 140, 0 };
static const i16 v_cong[] = { 4, 100, 620, 460, 70, 1, 100, 270, 620, 270, 1, 100, 80, 620, 80, 0 };
static const i16 v_ll[] = { 1, 420, 520, 110, 250, 1, 110, 250, 420, -20, 1, 700, 520, 390, 250, 1, 390, 250, 700, -20, 0 };
static const i16 v_gg[] = { 1, 110, 520, 420, 250, 1, 420, 250, 110, -20, 1, 390, 520, 700, 250, 1, 700, 250, 390, -20, 0 };
static const i16 v_prec[] = { 2, 640, 0, 530, 250, 90, 180, 2, 640, 500, 530, 250, 180, 270, 0 };
static const i16 v_succ[] = { 2, 80, 0, 530, 250, 0, 90, 2, 80, 500, 530, 250, 270, 360, 0 };
static const i16 v_ast[] = { 1, 300, 440, 300, 60, 1, 135, 345, 465, 155, 1, 135, 155, 465, 345, 0 };
static const i16 v_oplus[] = { 2, 400, 250, 260, 260, 0, 360, 1, 140, 250, 660, 250, 1, 400, -10, 400, 510, 0 };
static const i16 v_ominus[] = { 2, 400, 250, 260, 260, 0, 360, 1, 140, 250, 660, 250, 0 };
static const i16 v_otimes[] = { 2, 400, 250, 260, 260, 0, 360, 1, 216, 66, 584, 434, 1, 216, 434, 584, 66, 0 };
static const i16 v_oslash[] = { 2, 400, 250, 260, 260, 0, 360, 1, 216, 66, 584, 434, 0 };
static const i16 v_odot[] = { 2, 400, 250, 260, 260, 0, 360, 3, 400, 250, 55, 0 };
static const i16 v_circ[] = { 2, 250, 250, 120, 120, 0, 360, 0 };
static const i16 v_mapsto[] = { 1, 80, 250, 680, 250, 1, 80, 110, 80, 390, 1, 540, 390, 700, 250, 1, 700, 250, 540, 110, 0 };
static const i16 v_longto[] = { 1, 80, 250, 960, 250, 1, 820, 390, 980, 250, 1, 980, 250, 820, 110, 0 };
static const i16 v_longimplies[] = { 1, 80, 350, 900, 350, 1, 80, 150, 900, 150, 1, 760, 530, 1040, 250, 1, 1040, 250, 760, -30, 0 };
static const i16 v_vdots[] = { 3, 200, 60, 55, 3, 200, 330, 55, 3, 200, 600, 55, 0 };
static const i16 v_ddots[] = { 3, 120, 600, 55, 3, 400, 330, 55, 3, 680, 60, 55, 0 };
static const i16 v_cdots[] = { 3, 170, 250, 55, 3, 470, 250, 55, 3, 770, 250, 55, 0 };
static const i16 v_cdot[] = { 3, 150, 250, 55, 0 };
static const i16 v_ratio[] = { 3, 150, 80, 55, 3, 150, 420, 55, 0 };
static const i16 v_parallel[] = { 1, 170, 740, 170, -240, 1, 360, 740, 360, -240, 0 };
static const i16 v_nmid[] = { 1, 200, 740, 200, -240, 1, 60, 120, 340, 400, 0 };
static const i16 v_setminus[] = { 1, 90, 700, 510, -150, 0 };
static const i16 v_diamond[] = { 1, 330, 520, 560, 250, 1, 560, 250, 330, -20, 1, 330, -20, 100, 250, 1, 100, 250, 330, 520, 0 };
static const i16 v_box[] = { 1, 100, 0, 650, 0, 1, 650, 0, 650, 550, 1, 650, 550, 100, 550, 1, 100, 550, 100, 0, 0 };
typedef struct { u32 cp; i16 adv; const i16 *ops; } VSym;
static const VSym vsyms[] = {
  { 0x2208, 720, v_in }, { 0x2209, 720, v_notin }, { 0x220A, 720, v_in }, { 0x2282, 760, v_subset }, { 0x2286, 760, v_subseteq },
  { 0x2227, 700, v_wedge }, { 0x2228, 700, v_vee }, { 0x22C0, 700, v_wedge }, { 0x22C1, 700, v_vee },
  { 0x21D2, 820, v_implies }, { 0x21D0, 820, v_impliedby }, { 0x27F9, 1120, v_longimplies }, { 0x27F6, 1060, v_longto }, { 0x21A6, 780, v_mapsto },
  { 0x27E8, 390, v_langle }, { 0x27E9, 390, v_rangle }, { 0x2329, 390, v_langle }, { 0x232A, 390, v_rangle },
  { 0x2308, 400, v_lceil }, { 0x2309, 330, v_rceil }, { 0x230A, 400, v_lfloor }, { 0x230B, 330, v_rfloor },
  { 0x221D, 720, v_propto }, { 0x22A5, 760, v_perp }, { 0x22A4, 760, v_top }, { 0x2220, 720, v_angle }, { 0x22A2, 700, v_vdash },
  { 0x22A8, 700, v_models }, { 0x2234, 700, v_therefore }, { 0x2235, 700, v_because }, { 0x223C, 720, v_sim }, { 0x2243, 720, v_simeq },
  { 0x2245, 720, v_cong }, { 0x226A, 810, v_ll }, { 0x226B, 810, v_gg }, { 0x227A, 720, v_prec }, { 0x227B, 720, v_succ },
  { 0x2217, 600, v_ast }, { 0x2295, 800, v_oplus }, { 0x2296, 800, v_ominus }, { 0x2297, 800, v_otimes }, { 0x2298, 800, v_oslash },
  { 0x2299, 800, v_odot }, { 0x2A01, 800, v_oplus }, { 0x2A02, 800, v_otimes }, { 0x2A00, 800, v_odot }, { 0x2218, 500, v_circ },
  { 0x22EE, 400, v_vdots }, { 0x22F1, 800, v_ddots }, { 0x22EF, 940, v_cdots }, { 0x22C5, 300, v_cdot }, { 0x2236, 300, v_ratio },
  { 0x2225, 530, v_parallel }, { 0x2224, 400, v_nmid }, { 0x2216, 600, v_setminus }, { 0x22C4, 660, v_diamond }, { 0x25A1, 750, v_box }
};
/* glyphs that stand in for others: the code point drawn, flipped, or repeated */
typedef struct { u32 cp, sub; u8 flags, rep; } Sub;
static const Sub subs[] = {
  { 0x2210, 0x220F, MIF_FLIPY, 1 }, { 0x222C, 0x222B, 0, 2 }, { 0x222D, 0x222B, 0, 3 }, { 0x2213, 0xB1, MIF_FLIPY, 1 },
  { 0x2207, 0x394, MIF_FLIPY, 1 }, { 0x2200, 'A', MIF_FLIPY, 1 }, { 0x2203, 'E', MIF_FLIPX, 1 }, { 0x2205, 0xD8, 0, 1 },
  { 0x2219, 0xB7, 0, 1 }, { 0x2223, '|', 0, 1 }, { 0x210F, 0x127, 0, 1 }, { 0x222A, 0x2229, MIF_FLIPY, 1 }, { 0x22C2, 0x2229, 0, 1 },
  { 0x22C3, 0x2229, MIF_FLIPY, 1 }, { 0x220B, 0x2208, MIF_FLIPX, 1 }, { 0x2283, 0x2282, MIF_FLIPX, 1 }, { 0x2287, 0x2286, MIF_FLIPX, 1 },
  { 0x2212, '-', 0, 1 }, { 0x2044, '/', 0, 1 }, { 0x2215, '/', 0, 1 }, { 0x2032, '\'', 0, 1 }, { 0x2192, '>', 0, 1 },
  { 0x2135, 'N', 0, 1 }, { 0x2118, 'P', 0, 1 }, { 0x2111, 'I', 0, 1 }, { 0x211C, 'R', 0, 1 }, { 0x2146, 'd', 0, 1 }, { 0x2147, 'e', 0, 1 },
  { 0x2148, 'i', 0, 1 }, { 0x2149, 'j', 0, 1 }, { 0x2145, 'D', 0, 1 }, { 0x2016, 0x2225, 0, 1 }, { 0x2502, '|', 0, 1 }, { 0x25E6, 0x2218, 0, 1 }
};
/* a symbol drawn from its ops at em, pen at x, y; its box; 0 if cp isn't one */
static int vsym(Ctx *c, u32 cp, i32 em, i32 x, i32 y, u32 color, int flags, Bx *b)
{
  const VSym *v = NULL;
  u32 i0 = c->L->n, i;
  size_t k;
  const i16 *o;
  i32 lw = EM(48), minx = 0x7FFFFFFF, maxx = -0x7FFFFFFF, miny = 0x7FFFFFFF, maxy = -0x7FFFFFFF;
  for (k = 0; k < sizeof vsyms / sizeof vsyms[0]; k++) if (vsyms[k].cp == cp) { v = &vsyms[k]; break; }
  if (!v) return 0;
  for (o = v->ops; *o; ) {
    switch (*o) {
    case 1: stroke(c->L, x + EM(o[1]), y - EM(o[2]), x + EM(o[3]), y - EM(o[4]), lw, color); o += 5; break;
    case 2: arc(c->L, x + EM(o[1]), y - EM(o[2]), EM(o[3]), EM(o[4]), o[5], o[6], lw, color); o += 7; break;
    case 3: dot(c->L, x + EM(o[1]), y - EM(o[2]), EM(o[3]), color); o += 4; break;
    case 4: {
      int s;
      i32 px = 0, py = 0;
      for (s = 0; s <= 16; s++) {
        i32 xx = x + EM(o[1] + (o[2] - o[1]) * s / 16), yy = y - EM(o[3] + (i32)((long long)o[4] * isin(s * 3600 / 16) / TRIG_ONE));
        if (s) stroke(c->L, px, py, xx, yy, lw, color);
        px = xx; py = yy;
      }
      o += 5;
      break; }
    default: o++; break;
    }
  }
  /* its box from what was drawn; flipped about its middle if asked */
  for (i = i0; i < c->L->n; i++) {
    MItem *it = &c->L->it[i];
    int j;
    for (j = 0; it->kind == MI_POLY && j < it->h; j++) {
      i32 px = c->L->pt[it->w + 2 * j], py = c->L->pt[it->w + 2 * j + 1];
      if (px < minx) minx = px;
      if (px > maxx) maxx = px;
      if (py < miny) miny = py;
      if (py > maxy) maxy = py;
    }
  }
  if (minx > maxx) { minx = maxx = x; miny = maxy = y; }
  if (flags) for (i = i0; i < c->L->n; i++) {
    MItem *it = &c->L->it[i];
    int j;
    for (j = 0; it->kind == MI_POLY && j < it->h; j++) {
      if (flags & MIF_FLIPX) c->L->pt[it->w + 2 * j] = x + EM(v->adv) - (c->L->pt[it->w + 2 * j] - x);
      if (flags & MIF_FLIPY) c->L->pt[it->w + 2 * j + 1] = miny + maxy - c->L->pt[it->w + 2 * j + 1];
    }
  }
  b->w = EM(v->adv); b->asc = y - miny; b->desc = maxy - y; b->ic = 0;
  if (b->desc < 0) b->desc = 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* characters */
/* the Mathematical Alphanumeric Symbols (U+1D400...) and letter-like ones as a plain letter and an
   alphabet (MSCR_) and style (bit 1 bold, bit 2 italic); else cp as it is */
static u32 plain_letter(u32 cp, int *scr, int *sty)
{
  static const u32 greek[58] = { 0x391, 0x392, 0x393, 0x394, 0x395, 0x396, 0x397, 0x398, 0x399, 0x39A, 0x39B, 0x39C, 0x39D, 0x39E, 0x39F,
    0x3A0, 0x3A1, 0x3F4, 0x3A3, 0x3A4, 0x3A5, 0x3A6, 0x3A7, 0x3A8, 0x3A9, 0x2207, 0x3B1, 0x3B2, 0x3B3, 0x3B4, 0x3B5, 0x3B6, 0x3B7, 0x3B8,
    0x3B9, 0x3BA, 0x3BB, 0x3BC, 0x3BD, 0x3BE, 0x3BF, 0x3C0, 0x3C1, 0x3C2, 0x3C3, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9, 0x2202, 0x3F5,
    0x3D1, 0x3F0, 0x3D5, 0x3F1, 0x3D6 };
  /* the Latin alphabets in order, each 52 letters: their alphabet and style */
  static const u8 lat[13][2] = { { MSCR_ROMAN, 1 }, { MSCR_ROMAN, 2 }, { MSCR_ROMAN, 3 }, { MSCR_SCRIPT, 0 }, { MSCR_SCRIPT, 1 },
    { MSCR_FRAKTUR, 0 }, { MSCR_DOUBLE, 0 }, { MSCR_FRAKTUR, 1 }, { MSCR_SANS, 0 }, { MSCR_SANS, 1 }, { MSCR_SANS, 2 }, { MSCR_SANS, 3 }, { MSCR_MONO, 0 } };
  static const u8 grk[5][2] = { { MSCR_ROMAN, 1 }, { MSCR_ROMAN, 2 }, { MSCR_ROMAN, 3 }, { MSCR_SANS, 1 }, { MSCR_SANS, 3 } };
  static const u8 dig[5][2] = { { MSCR_ROMAN, 1 }, { MSCR_DOUBLE, 0 }, { MSCR_SANS, 0 }, { MSCR_SANS, 1 }, { MSCR_MONO, 0 } };
  static const struct { u32 cp; u8 letter, scr, sty; } like[] = {
    { 0x2102, 'C', MSCR_DOUBLE, 0 }, { 0x210D, 'H', MSCR_DOUBLE, 0 }, { 0x2115, 'N', MSCR_DOUBLE, 0 }, { 0x2119, 'P', MSCR_DOUBLE, 0 },
    { 0x211A, 'Q', MSCR_DOUBLE, 0 }, { 0x211D, 'R', MSCR_DOUBLE, 0 }, { 0x2124, 'Z', MSCR_DOUBLE, 0 }, { 0x210E, 'h', MSCR_ROMAN, 2 },
    { 0x212C, 'B', MSCR_SCRIPT, 0 }, { 0x2130, 'E', MSCR_SCRIPT, 0 }, { 0x2131, 'F', MSCR_SCRIPT, 0 }, { 0x210B, 'H', MSCR_SCRIPT, 0 },
    { 0x2110, 'I', MSCR_SCRIPT, 0 }, { 0x2112, 'L', MSCR_SCRIPT, 0 }, { 0x2133, 'M', MSCR_SCRIPT, 0 }, { 0x211B, 'R', MSCR_SCRIPT, 0 },
    { 0x212F, 'e', MSCR_SCRIPT, 0 }, { 0x210A, 'g', MSCR_SCRIPT, 0 }, { 0x2134, 'o', MSCR_SCRIPT, 0 }, { 0x212D, 'C', MSCR_FRAKTUR, 0 },
    { 0x210C, 'H', MSCR_FRAKTUR, 0 }, { 0x2128, 'Z', MSCR_FRAKTUR, 0 } };
  size_t k;
  *scr = -1; *sty = -1;
  if (cp >= 0x1D400 && cp < 0x1D6A4) {
    u32 i = cp - 0x1D400, a = i / 52, j = i % 52;
    *scr = lat[a][0]; *sty = lat[a][1];
    return j < 26 ? 'A' + j : 'a' + j - 26;
  }
  if (cp == 0x1D6A4) { *sty = 2; return 0x131; }
  if (cp == 0x1D6A5) { *sty = 2; return 0x237; }
  if (cp >= 0x1D6A8 && cp < 0x1D7CA) {
    u32 i = cp - 0x1D6A8;
    *scr = grk[i / 58][0]; *sty = grk[i / 58][1];
    return greek[i % 58];
  }
  if (cp >= 0x1D7CE && cp <= 0x1D7FF) {
    u32 i = cp - 0x1D7CE;
    *scr = dig[i / 10][0]; *sty = dig[i / 10][1];
    return '0' + i % 10;
  }
  for (k = 0; k < sizeof like / sizeof like[0]; k++) if (like[k].cp == cp) { *scr = like[k].scr; *sty = like[k].sty; return like[k].letter; }
  return cp;
}
static int is_latin(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == 0x131 || c == 0x237; }
static int is_lgreek(u32 c) { return (c >= 0x3B1 && c <= 0x3C9) || c == 0x3D1 || c == 0x3D5 || c == 0x3D6 || c == 0x3F0 || c == 0x3F1 || c == 0x3F5 || c == 0x2202; }
static int cls_of(u32 c)
{
  if (c == '+' || c == 0x2212 || c == '-' || c == 0xB1 || c == 0x2213 || c == 0xD7 || c == 0xF7 || c == 0xB7 || c == 0x22C5 || c == 0x2217 || c == '*' ||
      c == 0x2218 || c == 0x2219 || c == 0x2229 || c == 0x222A || c == 0x2227 || c == 0x2228 || (c >= 0x2295 && c <= 0x2299) || c == 0x2216 || c == 0x2020 || c == 0x2021)
    return C_BIN;
  if (c == '=' || c == '<' || c == '>' || c == ':' || (c >= 0x2190 && c <= 0x21FF) || (c >= 0x2208 && c <= 0x220D) || c == 0x221D || c == 0x2223 || c == 0x2225 ||
      (c >= 0x2236 && c <= 0x22A9 && c != 0x2295 && c != 0x2296 && c != 0x2297 && c != 0x2298 && c != 0x2299) || (c >= 0x27F5 && c <= 0x27FF) || c == 0x2260)
    return C_REL;
  if (c == '(' || c == '[' || c == '{' || c == 0x27E8 || c == 0x2308 || c == 0x230A || c == 0x27E6) return C_OPEN;
  if (c == ')' || c == ']' || c == '}' || c == 0x27E9 || c == 0x2309 || c == 0x230B || c == 0x27E7 || c == '!') return C_CLOSE;
  if (c == ',' || c == ';') return C_PUNCT;
  if ((c >= 0x220F && c <= 0x2211) || (c >= 0x222B && c <= 0x2233) || (c >= 0x22C0 && c <= 0x22C3) || (c >= 0x2A00 && c <= 0x2A0C)) return C_OP;
  return C_ORD;
}
/* TeX's spacing between atoms, in mu (1/18 em); negative: only in display and text styles */
static const signed char space_tab[8][8] = {
  /*         Ord Op Bin Rel Open Close Punct Inner */
  /* Ord   */ { 0, 3, -4, -5, 0, 0, 0, -3 },
  /* Op    */ { 3, 3, 0, -5, 0, 0, 0, -3 },
  /* Bin   */ { -4, -4, 0, 0, -4, 0, 0, -4 },
  /* Rel   */ { -5, -5, 0, 0, -5, 0, 0, -5 },
  /* Open  */ { 0, 0, 0, 0, 0, 0, 0, 0 },
  /* Close */ { 0, 3, -4, -5, 0, 0, 0, -3 },
  /* Punct */ { -3, -3, 0, -3, -3, -3, -3, -3 },
  /* Inner */ { -3, 3, -4, -5, -3, 0, -3, -3 } };

/* one character of a math run at em in the run's alphabet and style: its atom's box (items at the origin) */
static Bx lay_char(Ctx *c, const MNode *run, u32 cp, i32 em)
{
  Bx b;
  int scr = run->pv[P_R_SCR] ? run->pv[P_R_SCR] - 1 : MSCR_ROMAN, sty = run->pv[P_R_STY] ? run->pv[P_R_STY] - 1 : -1, lscr, lsty;
  int bold, italic, flags = 0, rep = 1, k;
  u32 color = run->color != COLOR_AUTO ? run->color : c->color, draw;
  Face *pref, *f = NULL;
  u16 g = 0;
  i32 adv, x0, y0, x1, y1, i;
  size_t s;
  memset(&b, 0, sizeof b);
  b.lc = b.rc = (u8)cls_of(cp);
  draw = plain_letter(cp, &lscr, &lsty);
  if (lscr >= 0) scr = lscr;
  /* Word's default: Latin letters and lower-case Greek in italics, the rest upright */
  if (lsty >= 0) { bold = lsty & 1; italic = (lsty >> 1) & 1; }
  else if (sty >= 0) { bold = sty == MSTY_B || sty == MSTY_BI; italic = sty == MSTY_I || sty == MSTY_BI; }
  else { bold = 0; italic = is_latin(draw) || is_lgreek(draw); }
  if (scr == MSCR_SCRIPT) italic = 1;
  if (scr == MSCR_FRAKTUR) bold = 1;
  if (draw == '-') draw = 0x2212;
  if (draw == 0x2061 || draw == 0x2062 || draw == 0x2063 || draw == 0x2064 || draw == 0x200B) return b;   /* invisible operators */
  pref = scr == MSCR_SANS ? c->sans[bold + 2 * italic] : scr == MSCR_MONO ? c->mono : c->serif[bold + 2 * italic];
  if (!pref) pref = c->serif[0];
  if (!find_glyph(c, draw, pref, &f, &g) || draw == 0x2212) {
    /* a stand-in glyph, or the symbol drawn */
    for (s = 0; s < sizeof subs / sizeof subs[0]; s++) if (subs[s].cp == draw) break;
    if (draw != 0x2212 || !f) {
      if (s < sizeof subs / sizeof subs[0] && !find_glyph(c, subs[s].sub, pref, &f, &g)) {
        Bx vb;
        if (vsym(c, subs[s].sub, em, 0, 0, color, subs[s].flags, &vb)) { vb.lc = b.lc; vb.rc = b.rc; return vb; }
      }
      if (s < sizeof subs / sizeof subs[0] && f) { flags = subs[s].flags; rep = subs[s].rep; }
      else if (vsym(c, draw, em, 0, 0, color, 0, &b)) { b.lc = b.rc = (u8)cls_of(cp); return b; }
      else f = NULL;
    }
  }
  if (!f) {
    /* nothing draws it: an empty box the size of a letter */
    i32 w = EM(500), t = EM(40);
    add_rect(c->L, EM(60), -EM(680), w, t, color); add_rect(c->L, EM(60), -t, w, t, color);
    add_rect(c->L, EM(60), -EM(680), t, EM(680), color); add_rect(c->L, EM(60) + w - t, -EM(680), t, EM(680), color);
    b.w = EM(620); b.asc = EM(680);
    return b;
  }
  glyph_met(f, g, em, &adv, &x0, &y0, &x1, &y1);
  for (k = 0; k < rep; k++) {
    i32 px = k * (adv * 6 / 10);
    put_glyph(c, f, g, draw, em, px, 0, color, flags);
    if (scr == MSCR_DOUBLE) put_glyph(c, f, g, draw, em, px + EM(50), 0, color, flags);    /* (double-struck: drawn twice) */
  }
  i = (rep - 1) * (adv * 6 / 10);
  b.w = adv + i + (scr == MSCR_DOUBLE ? EM(50) : 0);
  b.asc = y1 > 0 ? y1 : 0; b.desc = y0 < 0 ? -y0 : 0;
  b.ic = italic && x1 > adv ? x1 - adv : 0;
  if (draw == 0x2032 || draw == 0x2033 || draw == 0x2034) b.lc = b.rc = C_ORD;
  return b;
}
/* normal text (m:nor) in the paragraph's font or the run's own */
static Bx lay_text(Ctx *c, const MNode *run, const char *t, size_t n, i32 em)
{
  Bx b;
  Face *pref = NULL, *f;
  u16 g;
  u32 color = run->color != COLOR_AUTO ? run->color : c->color;
  size_t i = 0;
  memset(&b, 0, sizeof b);
  if (run->font[0] && !strstr(run->font, "Math")) pref = fonts_face(c->fs, run->font, run->wb, run->wi);
  else if (run->font[0]) pref = c->serif[run->wb + 2 * run->wi];          /* (Cambria Math's normal text: Quill's math font, upright) */
  if (!pref && c->text) pref = fonts_face(c->fs, c->text->family, run->wb, run->wi);
  if (!pref) pref = c->serif[run->wb + 2 * run->wi];
  while (i < n) {
    int used;
    u32 cp = utf8_get(t + i, n - i, &used);
    i32 adv, x0, y0, x1, y1;
    i += (size_t)used;
    if (!find_glyph(c, cp, pref, &f, &g)) { b.w += EM(500); continue; }
    glyph_met(f, g, em, &adv, &x0, &y0, &x1, &y1);
    put_glyph(c, f, g, cp, em, b.w, 0, color, 0);
    b.w += adv;
    if (y1 > b.asc) b.asc = y1;
    if (-y0 > b.desc) b.desc = -y0;
  }
  b.lc = b.rc = C_ORD;
  return b;
}

/* ------------------------------------------------------------------ */
/* growing shapes: delimiters, radical signs, braces, arrows, accents across a wide base */
/* a delimiter cp h tall (its top at y0, y down), at x; its width */
static i32 delim(Ctx *c, u32 cp, i32 em, i32 x, i32 top, i32 h, u32 color)
{
  i32 t = EM(50), thick = t + h / 40, w, xy[2 * 2 * 17 + 4], mid = top + h / 2, bot = top + h;
  Face *f;
  u16 g;
  int i, n = 16;
  if (thick > EM(110)) thick = EM(110);
  if (!cp) return 0;
  /* small enough for the glyph */
  if (cp != 0x2502 && find_glyph(c, cp, c->serif[0], &f, &g)) {
    i32 adv, x0, y0, x1, y1;
    glyph_met(f, g, em, &adv, &x0, &y0, &x1, &y1);
    if (h <= (y1 - y0) * 115 / 100 && y1 > y0) {
      i32 dy = (y1 + y0) / 2;                                  /* its middle on the middle asked for */
      put_glyph(c, f, g, cp, em, x, mid + dy, color, 0);
      return adv;
    }
  }
  w = EM(320) + h / 14;
  if (w > EM(700)) w = EM(700);
  switch (cp) {
  case '(': case ')': {
    /* a crescent: the outer curve down, the inner one back up */
    i32 bulge = w * 7 / 10, xo = x + w - EM(60), cx = xo - bulge * 2 + EM(30);
    quad_pts(xo, top, cx, mid, xo, bot, n, xy);
    quad_pts(xo + t / 2, bot, cx + thick * 2, mid, xo + t / 2, top, n, xy + 2 * (n + 1));
    if (cp == ')') for (i = 0; i < 2 * (n + 1); i++) xy[2 * i] = x + w - (xy[2 * i] - x);
    add_poly(c->L, xy, 2 * (n + 1), color);
    return w;
  }
  case '[': case ']': case 0x27E6: case 0x27E7: {
    i32 sx = cp == '[' || cp == 0x27E6 ? x + EM(100) : x + w - EM(100) - thick, arm = EM(220), dir = cp == '[' || cp == 0x27E6 ? 1 : -1;
    add_rect(c->L, sx, top, thick, h, color);
    add_rect(c->L, dir > 0 ? sx : sx + thick - arm, top, arm, t, color);
    add_rect(c->L, dir > 0 ? sx : sx + thick - arm, bot - t, arm, t, color);
    if (cp == 0x27E6 || cp == 0x27E7) add_rect(c->L, sx + dir * EM(120) + (dir > 0 ? 0 : thick - t), top, t, h, color);
    return w;
  }
  case 0x2308: case 0x2309: case 0x230A: case 0x230B: {
    int left = cp == 0x2308 || cp == 0x230A, ceil = cp == 0x2308 || cp == 0x2309;
    i32 sx = left ? x + EM(100) : x + w - EM(100) - thick, arm = EM(220);
    add_rect(c->L, sx, top, thick, h, color);
    add_rect(c->L, left ? sx : sx + thick - arm, ceil ? top : bot - t, arm, t, color);
    return w;
  }
  case '{': case '}': {
    /* a brace: two curls with the point in the middle */
    i32 x0 = x + EM(80), x1 = x + w - EM(80), xm = (x0 + x1) / 2, q = h / 4;
    i32 pts[2 * 9];
    pts[0] = x1; pts[1] = top;
    pts[2] = xm; pts[3] = top + EM(60);
    pts[4] = xm; pts[5] = top + q;
    pts[6] = xm; pts[7] = mid - EM(80);
    pts[8] = x0; pts[9] = mid;
    pts[10] = xm; pts[11] = mid + EM(80);
    pts[12] = xm; pts[13] = bot - q;
    pts[14] = xm; pts[15] = bot - EM(60);
    pts[16] = x1; pts[17] = bot;
    if (cp == '}') for (i = 0; i < 9; i++) pts[2 * i] = x + w - (pts[2 * i] - x);
    {
      i32 seg[2 * 9];
      int k;
      /* the curls as curves, the straight parts straight */
      quad_pts(pts[0], pts[1], pts[2], pts[1], pts[2], pts[3], 4, seg);
      for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
      stroke(c->L, pts[2], pts[3], pts[6], pts[7], thick, color);
      quad_pts(pts[6], pts[7], pts[6], pts[9], pts[8], pts[9], 4, seg);
      for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
      quad_pts(pts[8], pts[9], pts[10], pts[9], pts[10], pts[11], 4, seg);
      for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
      stroke(c->L, pts[10], pts[11], pts[14], pts[15], thick, color);
      quad_pts(pts[14], pts[15], pts[14], pts[17], pts[16], pts[17], 4, seg);
      for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
    }
    return w;
  }
  case 0x27E8: case 0x27E9: case 0x2329: case 0x232A: {
    i32 a = x + EM(80), b = x + w - EM(80);
    if (cp == 0x27E8 || cp == 0x2329) { stroke(c->L, b, top, a, mid, t, color); stroke(c->L, a, mid, b, bot, t, color); }
    else { stroke(c->L, a, top, b, mid, t, color); stroke(c->L, b, mid, a, bot, t, color); }
    return w;
  }
  case '|': case 0x2502: case 0x2223:
    add_rect(c->L, x + EM(120), top, t, h, color);
    return EM(120) * 2 + t;
  case 0x2016: case 0x2225:
    add_rect(c->L, x + EM(120), top, t, h, color); add_rect(c->L, x + EM(300), top, t, h, color);
    return EM(420) + t;
  case '/': stroke(c->L, x + w - EM(60), top, x + EM(60), bot, t, color); return w;
  case '\\': stroke(c->L, x + EM(60), top, x + w - EM(60), bot, t, color); return w;
  }
  /* anything else: its glyph scaled up to the height */
  if (find_glyph(c, cp, c->serif[0], &f, &g)) {
    i32 adv, x0, y0, x1, y1, e2;
    glyph_met(f, g, em, &adv, &x0, &y0, &x1, &y1);
    e2 = y1 > y0 ? (i32)((long long)em * h / (y1 - y0)) : em;
    glyph_met(f, g, e2, &adv, &x0, &y0, &x1, &y1);
    put_glyph(c, f, g, cp, e2, x, mid + (y1 + y0) / 2, color, 0);
    return adv;
  }
  return 0;
}

/* a horizontal brace, parenthesis or bracket (⏞ ⏟ ⏜ ⏝ ⎴ ⎵) or arrow across w at x, its top at y; its height */
static i32 hshape(Ctx *c, u32 cp, i32 em, i32 x, i32 y, i32 w, u32 color)
{
  i32 t = EM(45), h = EM(250), mid = x + w / 2, seg[2 * 5];
  int k, down = cp == 0x23DF || cp == 0x23DD || cp == 0x23B5;    /* opening down (under the base) */
  i32 yo = down ? y : y + h, yi = down ? y + h : y;             /* the ends' side, the point's side */
  switch (cp) {
  case 0x23DE: case 0x23DF: {
    i32 ym = (y + y + h) / 2;
    quad_pts(x, yo, x, ym, x + EM(150), ym, 4, seg);
    for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
    stroke(c->L, x + EM(150), ym, mid - EM(150), ym, t * 3 / 2, color);
    quad_pts(mid - EM(150), ym, mid, ym, mid, yi, 4, seg);
    for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
    quad_pts(mid, yi, mid, ym, mid + EM(150), ym, 4, seg);
    for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
    stroke(c->L, mid + EM(150), ym, x + w - EM(150), ym, t * 3 / 2, color);
    quad_pts(x + w - EM(150), ym, x + w, ym, x + w, yo, 4, seg);
    for (k = 0; k < 4; k++) stroke(c->L, seg[2 * k], seg[2 * k + 1], seg[2 * k + 2], seg[2 * k + 3], t, color);
    return h;
  }
  case 0x23DC: case 0x23DD: {
    i32 xy[2 * 17];
    quad_pts(x, yo, mid, yi + (yi - yo), x + w, yo, 16, xy);
    for (k = 0; k < 16; k++) stroke(c->L, xy[2 * k], xy[2 * k + 1], xy[2 * k + 2], xy[2 * k + 3], t, color);
    return h;
  }
  case 0x23B4: case 0x23B5:
    add_rect(c->L, x, down ? yi - t : yi, w, t, color);
    add_rect(c->L, x, y, t, h, color); add_rect(c->L, x + w - t, y, t, h, color);
    return h;
  case 0x2190: case 0x2192: case 0x2194: case 0x20D6: case 0x20D7: case 0x20E1: case 0x21D0: case 0x21D2: case 0x21D4: {
    int left = cp == 0x2190 || cp == 0x2194 || cp == 0x20D6 || cp == 0x20E1 || cp == 0x21D0 || cp == 0x21D4;
    int right = cp != 0x2190 && cp != 0x20D6 && cp != 0x21D0;
    int dbl = cp == 0x21D0 || cp == 0x21D2 || cp == 0x21D4;
    i32 ym = y + EM(120), hd = EM(110);
    h = EM(240);
    if (dbl) { stroke(c->L, x, ym - EM(50), x + w, ym - EM(50), t, color); stroke(c->L, x, ym + EM(50), x + w, ym + EM(50), t, color); hd = EM(150); }
    else stroke(c->L, x, ym, x + w, ym, t, color);
    if (right) { stroke(c->L, x + w - hd, ym - hd, x + w, ym, t, color); stroke(c->L, x + w, ym, x + w - hd, ym + hd, t, color); }
    if (left) { stroke(c->L, x + hd, ym - hd, x, ym, t, color); stroke(c->L, x, ym, x + hd, ym + hd, t, color); }
    return h;
  }
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* the tree */
static Bx lay_row(Ctx *c, MNode *row, St st);
static Bx lay_node(Ctx *c, MNode *n, St st);
static Bx empty_box(void) { Bx b; memset(&b, 0, sizeof b); return b; }
static void place(MNode *n, i32 x, i32 y, const Bx *b) { n->x = x; n->y = y; n->w = b->w; n->asc = b->asc; n->desc = b->desc; }
/* a row laid out apart from the items before it: [*a, *e) its items, at its own origin */
static Bx sub_row(Ctx *c, MNode *row, St st, u32 *a, u32 *e)
{
  Bx b;
  int rec = c->rec;
  c->rec = 0;
  *a = c->L->n;
  b = row ? lay_row(c, row, st) : empty_box();
  *e = c->L->n;
  c->rec = rec;
  return b;
}
/* a row's own argSz on top of the style */
static St with_sz(MNode *row, St st)
{
  if (row && row->hasargsz) { int z = st.sz + row->argsz; st.sz = (signed char)(z < -2 ? -2 : z > 2 ? 2 : z); }
  return st;
}
/* scripts on a base box (already drawn at the origin): sup and sub rows (either NULL), as TeX
   places them; char_base: a single character (no drop). Returns the whole box */
static Bx scripts(Ctx *c, Bx base, MNode *sub, MNode *sup, St st, int char_base, int pre)
{
  i32 em = em_of(c, st), u = 0, v = 0, xh = EM(c->xh1000), w;
  St ss = st_sup(st), sb = st_sub(st);
  i32 ems = em_of(c, ss);
  u32 pa = 0, pe = 0, ba = 0, be = 0;
  Bx p = empty_box(), q = empty_box(), r = base;
  if (sup) p = sub_row(c, sup, with_sz(sup, ss), &pa, &pe);
  if (sub) q = sub_row(c, sub, with_sz(sub, sb), &ba, &be);
  if (!char_base) { u = base.asc - ems * 386 / 1000; v = base.desc + ems * 50 / 1000; }
  if (sup) {
    i32 m = st.cr ? EM(289) : st.lvl == 0 ? EM(413) : EM(363);
    if (u < m) u = m;
    if (u < p.desc + xh / 4) u = p.desc + xh / 4;
  }
  if (sub && !sup) {
    if (v < EM(150)) v = EM(150);
    if (v < q.asc - xh * 4 / 5) v = q.asc - xh * 4 / 5;
  } else if (sub) {
    i32 gap;
    if (v < EM(247)) v = EM(247);
    gap = (u - p.desc) - (q.asc - v);
    if (gap < EM(200)) {
      v += EM(200) - gap;
      /* and the superscript's bottom up to four fifths of the x-height */
      gap = xh * 4 / 5 - (u - p.desc);
      if (gap > 0) { u += gap; v -= gap; }
    }
  }
  w = (sup ? p.w + (pre ? 0 : base.ic) : 0) > (sub ? q.w : 0) ? p.w + (pre ? 0 : base.ic) : q.w;
  if (pre) {
    /* the scripts on the left, right-aligned against the base (moved right by their width) */
    shift(c->L, 0, 0, 0, 0);
    if (sup) { shift(c->L, pa, pe, w - p.w, -u); place(sup, w - p.w, -u, &p); }
    if (sub) { shift(c->L, ba, be, w - q.w, v); place(sub, w - q.w, v, &q); }
  } else {
    if (sup) { shift(c->L, pa, pe, base.w + base.ic, -u); place(sup, base.w + base.ic, -u, &p); }
    if (sub) { shift(c->L, ba, be, base.w, v); place(sub, base.w, v, &q); }
  }
  r.w = base.w + w + EM(40);
  if (sup && u + p.asc > r.asc) r.asc = u + p.asc;
  if (sub && v + q.desc > r.desc) r.desc = v + q.desc;
  r.ic = 0;
  return r;
}
static int is_char_row(const MNode *row)
{
  if (!row || row->nkid != 1 || row->kid[0]->kind != MK_RUN || !row->kid[0]->text) return 0;
  return utf8_next(row->kid[0]->text, strlen(row->kid[0]->text), 0) == strlen(row->kid[0]->text);
}
static int is_integral(u32 c) { return (c >= 0x222B && c <= 0x2233); }
static Bx lay_frac(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), t = EM(50), axis = EM(c->axis1000), pad = EM(70), u, v, W;
  int type = n->pv[P_F_TYPE] ? n->pv[P_F_TYPE] - 1 : MF_BAR;
  u32 na, ne, da, de;
  Bx a, b, r = empty_box();
  if (type == MF_LIN) {
    /* num/den in a line */
    Bx s;
    u32 sa = 0;
    a = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &na, &ne);
    sa = c->L->n;
    { MNode slash; memset(&slash, 0, sizeof slash); slash.color = COLOR_AUTO; s = lay_char(c, &slash, '/', em); }
    shift(c->L, sa, c->L->n, a.w, 0);
    b = sub_row(c, n->kid[1], with_sz(n->kid[1], st), &da, &de);
    shift(c->L, da, de, a.w + s.w, 0);
    place(n->kid[0], 0, 0, &a); place(n->kid[1], a.w + s.w, 0, &b);
    r.w = a.w + s.w + b.w;
    r.asc = a.asc > b.asc ? a.asc : b.asc; if (s.asc > r.asc) r.asc = s.asc;
    r.desc = a.desc > b.desc ? a.desc : b.desc; if (s.desc > r.desc) r.desc = s.desc;
    r.lc = r.rc = C_ORD;
    return r;
  }
  a = sub_row(c, n->kid[0], with_sz(n->kid[0], type == MF_SKW ? st_sup(st) : st_num(c, st)), &na, &ne);
  b = sub_row(c, n->kid[1], with_sz(n->kid[1], type == MF_SKW ? st_sub(st) : st_den(c, st)), &da, &de);
  if (type == MF_SKW) {
    /* skewed: the numerator up, the denominator down, a slash between */
    i32 hgap = EM(120), up = axis + a.desc + EM(30), down = b.asc - axis + EM(30), top, bot, sw;
    top = -(up + a.asc); bot = down + b.desc;
    sw = (bot - top) * 3 / 10;
    shift(c->L, na, ne, 0, -up); place(n->kid[0], 0, -up, &a);
    stroke(c->L, a.w + hgap / 2 + sw, top, a.w + hgap / 2, bot, t, c->color);
    shift(c->L, da, de, a.w + hgap + sw, down); place(n->kid[1], a.w + hgap + sw, down, &b);
    r.w = a.w + hgap + sw + b.w; r.asc = -top; r.desc = bot;
    r.lc = r.rc = C_INNER;
    return r;
  }
  if (st.lvl == 0) { u = EM(677); v = EM(686); } else { u = type == MF_NOBAR ? EM(444) : EM(394); v = EM(345); }
  if (type == MF_NOBAR) {
    i32 gmin = st.lvl == 0 ? 7 * t : 3 * t, gap = (u - a.desc) - (b.asc - v);
    if (gap < gmin) { u += (gmin - gap) / 2; v += (gmin - gap + 1) / 2; }
  } else {
    i32 gmin = st.lvl == 0 ? 3 * t : t;
    if (u - a.desc - (axis + t / 2) < gmin) u = gmin + axis + t / 2 + a.desc;
    if ((axis - t / 2) - (b.asc - v) < gmin) v = gmin + b.asc - axis + t / 2;
  }
  W = (a.w > b.w ? a.w : b.w) + 2 * pad;
  shift(c->L, na, ne, (W - a.w) / 2, -u); place(n->kid[0], (W - a.w) / 2, -u, &a);
  shift(c->L, da, de, (W - b.w) / 2, v); place(n->kid[1], (W - b.w) / 2, v, &b);
  if (type == MF_BAR) add_rect(c->L, EM(20), -axis - t / 2, W - EM(40), t, c->color);
  r.w = W; r.asc = u + a.asc; r.desc = v + b.desc;
  if (r.asc < axis + t) r.asc = axis + t;
  r.lc = r.rc = C_INNER;
  return r;
}
static Bx lay_rad(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), t = EM(50), xh = EM(c->xh1000), clr, top, bot, h, sw, x0 = 0;
  u32 ia, ie, da = 0, de = 0;
  int hasdeg = n->pv[P_RAD_DEGHIDE] != MB_ON && n->kid[0];      /* (WP14b: an empty degree shown, as its placeholder) */
  St ist = st; Bx in, dg = empty_box(), r = empty_box();
  ist.cr = 1;
  in = sub_row(c, n->kid[1], with_sz(n->kid[1], ist), &ia, &ie);
  if (in.asc < xh) in.asc = xh;
  clr = st.lvl == 0 ? t + xh / 4 : t + t / 4;
  top = -(in.asc + clr + t);                   /* the bar's top */
  bot = in.desc + EM(40);
  h = bot - top;
  sw = EM(560) + h / 12;
  if (sw > EM(900)) sw = EM(900);
  if (hasdeg) {
    St ds = st; ds.lvl = 3;
    dg = sub_row(c, n->kid[0], with_sz(n->kid[0], ds), &da, &de);
    if (dg.w > sw * 6 / 10) x0 = dg.w - sw * 6 / 10;
  }
  {
    /* the sign: a short rise, a thick fall, a long rise to the bar, the bar over the base */
    i32 yt = bot - h * 45 / 100, a = x0 + sw * 8 / 100, b = x0 + sw * 28 / 100, cc = x0 + sw * 55 / 100, d = x0 + sw;
    stroke(c->L, a, yt + EM(30), b, yt - EM(20), t, c->color);
    stroke(c->L, b, yt - EM(20), cc, bot, t * 2, c->color);
    stroke(c->L, cc, bot, d, top + t / 2, t, c->color);
    add_rect(c->L, d - t / 2, top, in.w + EM(120) + t / 2, t, c->color);
  }
  shift(c->L, ia, ie, x0 + sw + EM(60), 0); place(n->kid[1], x0 + sw + EM(60), 0, &in);
  if (hasdeg) {
    i32 dy = bot - h * 6 / 10 - dg.desc;
    shift(c->L, da, de, x0 + sw * 6 / 10 - dg.w, dy); place(n->kid[0], x0 + sw * 6 / 10 - dg.w, dy, &dg);
    if (-dy + dg.asc > r.asc) r.asc = -dy + dg.asc;
  }
  r.w = x0 + sw + in.w + EM(150);
  if (-top + t > r.asc) r.asc = -top + t;
  r.desc = bot > in.desc ? bot : in.desc;
  r.lc = r.rc = C_ORD;
  return r;
}
static Bx lay_nary(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), axis = EM(c->axis1000), target, adv, x0, y0, x1, y1, e2, sh;
  u32 cp = n->ch[0] == MCH_UNSET ? 0x222B : n->ch[0], oa, oe, ea, ee;
  int integral = is_integral(cp), limloc, subhide = n->pv[P_N_SUBHIDE] == MB_ON, suphide = n->pv[P_N_SUPHIDE] == MB_ON;
  Face *f;
  u16 g;
  Bx op = empty_box(), e, r;
  MNode stub;
  St es = st;
  /* the operand first: a growing operator matches it */
  e = sub_row(c, n->kid[2], with_sz(n->kid[2], es), &ea, &ee);
  if (n->pv[P_N_LIMLOC]) limloc = n->pv[P_N_LIMLOC] - 1;
  else limloc = (integral ? c->mp.intsub : c->mp.narysub) ? MLIM_SUBSUP : MLIM_UNDOVR;
  if (st.lvl > 0) limloc = MLIM_SUBSUP;           /* in a line: limits to the side, as Word */
  target = st.lvl == 0 ? (integral ? EM(2200) : EM(1450)) : (integral ? EM(1250) : EM(1000));
  if (n->pv[P_N_GROW] == MB_ON && e.asc + e.desc > target) target = e.asc + e.desc;
  oa = c->L->n;
  memset(&stub, 0, sizeof stub);
  stub.color = COLOR_AUTO; stub.pv[P_R_STY] = MSTY_P + 1;
  if (find_glyph(c, cp, c->serif[0], &f, &g) && (glyph_met(f, g, em, &adv, &x0, &y0, &x1, &y1), y1 > y0)) {
    e2 = (i32)((long long)em * target / (y1 - y0));
    if (e2 < em) e2 = em;
    glyph_met(f, g, e2, &adv, &x0, &y0, &x1, &y1);
    sh = axis + (y1 + y0) / 2;                    /* its middle on the axis */
    put_glyph(c, f, g, cp, e2, 0, sh - 0, c->color, 0);
    c->L->it[c->L->n - 1].y = (y1 + y0) / 2 - axis;
    op.w = adv; op.asc = y1 - ((y1 + y0) / 2 - axis); op.desc = -(y0 - ((y1 + y0) / 2 - axis));
    op.ic = x1 > adv ? x1 - adv : 0;
  } else {
    /* (a stand-in: ∬ as two ∫...) at the size wanted */
    i32 big = st.lvl == 0 ? em * 3 / 2 : em;
    op = lay_char(c, &stub, cp, big);
    shift(c->L, oa, c->L->n, 0, (op.desc - op.asc) / 2 - axis + (op.asc - op.desc) / 2 + 0);
  }
  oe = c->L->n;
  if (limloc == MLIM_SUBSUP) {
    r = scripts(c, op, subhide ? NULL : n->kid[0], suphide ? NULL : n->kid[1], st, 0, 0);
    if (integral) r.w -= EM(40);
  } else {
    /* limits above and below, centred */
    u32 pa = 0, pe = 0, ba = 0, be = 0;
    Bx p = empty_box(), q = empty_box();
    i32 W = op.w, up = 0, down = 0;
    if (!suphide) p = sub_row(c, n->kid[1], with_sz(n->kid[1], st_sup(st)), &pa, &pe);
    if (!subhide) q = sub_row(c, n->kid[0], with_sz(n->kid[0], st_sub(st)), &ba, &be);
    if (!suphide && p.w > W) W = p.w;
    if (!subhide && q.w > W) W = q.w;
    shift(c->L, oa, oe, (W - op.w) / 2, 0);
    r = op; r.w = W;
    if (!suphide) {
      up = op.asc + EM(111) + p.desc;
      if (up - p.desc < op.asc + EM(200) - p.desc && up < op.asc + EM(200)) up = op.asc + EM(200) > up ? up : up;
      shift(c->L, pa, pe, (W - p.w) / 2, -up); place(n->kid[1], (W - p.w) / 2, -up, &p);
      r.asc = up + p.asc + EM(60);
    }
    if (!subhide) {
      down = op.desc + EM(167) + q.asc;
      if (down < op.desc + EM(600) - q.desc && down < op.desc + q.asc + EM(300)) down = op.desc + q.asc + EM(130);
      shift(c->L, ba, be, (W - q.w) / 2, down); place(n->kid[0], (W - q.w) / 2, down, &q);
      r.desc = down + q.desc + EM(60);
    }
  }
  /* the operand after a thin space */
  {
    i32 gap = EM(167);
    shift(c->L, ea, ee, r.w + gap, 0); place(n->kid[2], r.w + gap, 0, &e);
    /* (its items came first: they stay where they are in the list, which doesn't matter for drawing) */
    r.w += gap + e.w;
    if (e.asc > r.asc) r.asc = e.asc;
    if (e.desc > r.desc) r.desc = e.desc;
  }
  r.lc = C_OP; r.rc = e.w ? e.rc : C_OP; r.ic = 0;
  return r;
}
static Bx lay_delim(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), axis = EM(c->axis1000), asc = 0, desc = 0, h, top, x = 0, pad = EM(30);
  u32 beg = n->ch[0] == MCH_UNSET ? '(' : n->ch[0], end = n->ch[2] == MCH_UNSET ? ')' : n->ch[2], sep = n->ch[1] == MCH_UNSET ? 0x2502 : n->ch[1];
  int grow = n->pv[P_D_GROW] != MB_OFF, match = n->pv[P_D_SHP] == MSHP_MATCH + 1;
  u16 i;
  u32 *ra, *re;
  Bx *bx, r = empty_box();
  if (!n->nkid) return r;
  ra = (u32 *)malloc(n->nkid * 2 * sizeof(u32));
  bx = (Bx *)malloc(n->nkid * sizeof(Bx));
  if (!ra || !bx) { free(ra); free(bx); c->L->oom = 1; return r; }
  re = ra + n->nkid;
  for (i = 0; i < n->nkid; i++) {
    bx[i] = sub_row(c, n->kid[i], with_sz(n->kid[i], st), &ra[i], &re[i]);
    if (bx[i].asc > asc) asc = bx[i].asc;
    if (bx[i].desc > desc) desc = bx[i].desc;
  }
  if (asc < EM(c->xh1000)) asc = EM(c->xh1000);
  /* the delimiters' height: round the axis enough to cover both sides (or just the contents) */
  if (!grow) { h = EM(1000); top = -axis - h / 2; }
  else if (match) { h = asc + desc + EM(60); top = -asc - EM(30); }
  else {
    i32 half = asc - axis > desc + axis ? asc - axis : desc + axis;
    h = 2 * half + EM(80);
    if (h < EM(1000)) h = EM(1000);
    top = -axis - h / 2;
  }
  x = delim(c, beg, em, 0, top, h, c->color);
  x += pad;
  for (i = 0; i < n->nkid; i++) {
    if (i) { x += pad; x += delim(c, sep, em, x, top, h, c->color); x += pad; }
    shift(c->L, ra[i], re[i], x, 0); place(n->kid[i], x, 0, &bx[i]);
    x += bx[i].w;
  }
  x += pad;
  x += delim(c, end, em, x, top, h, c->color);
  free(ra); free(bx);
  r.w = x; r.asc = -top; r.desc = top + h;
  if (asc > r.asc) r.asc = asc;
  if (desc > r.desc) r.desc = desc;
  r.lc = beg ? C_OPEN : C_ORD; r.rc = end ? C_CLOSE : C_ORD;
  return r;
}
/* the spacing glyph an accent's combining character is drawn with (0: drawn as a shape) */
static u32 acc_glyph(u32 cp)
{
  switch (cp) {
  case 0x300: return '`'; case 0x301: return 0xB4; case 0x302: return 0x2C6; case 0x303: return 0x2DC; case 0x304: return 0xAF;
  case 0x306: return 0x2D8; case 0x307: return 0x2D9; case 0x308: return 0xA8; case 0x30A: return 0x2DA; case 0x30C: return 0x2C7;
  case 0x305: case 0x20D7: case 0x20D6: case 0x20E1: return 0;
  }
  return cp;
}
static Bx lay_acc(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), xh = EM(c->xh1000), t = EM(45), dy;
  u32 cp = n->ch[0] == MCH_UNSET ? 0x302 : n->ch[0], g = acc_glyph(cp), ba, be;
  St bs = st;
  Bx b, a, r;
  MNode stub;
  bs.cr = 1;
  b = sub_row(c, n->kid[0], with_sz(n->kid[0], bs), &ba, &be);
  place(n->kid[0], 0, 0, &b);
  r = b;
  dy = b.asc > xh ? b.asc - xh : 0;               /* a glyph accent is drawn for the x-height: raised for taller bases */
  memset(&stub, 0, sizeof stub);
  stub.color = COLOR_AUTO; stub.pv[P_R_STY] = MSTY_P + 1;
  if (g && (b.w < EM(700) || (cp != 0x302 && cp != 0x303 && cp != 0x304))) {
    u32 aa = c->L->n;
    i32 skew = is_char_row(n->kid[0]) ? b.ic / 2 + EM(30) * (b.ic > 0) : 0;
    a = lay_char(c, &stub, g, em);
    shift(c->L, aa, c->L->n, (b.w - a.w) / 2 + skew, -dy);
    if (a.asc + dy > r.asc) r.asc = a.asc + dy;
  } else {
    /* across the whole base: a line, a wide hat or tilde, an arrow */
    i32 y = -(b.asc + EM(80)), w = b.w;
    if (cp == 0x305 || cp == 0x304 || cp == 0xAF) { add_rect(c->L, 0, y - t, w, t, c->color); r.asc = -y + t + EM(40); }
    else if (cp == 0x302) {
      i32 hh = EM(180);
      stroke(c->L, 0, y, w / 2, y - hh, t, c->color); stroke(c->L, w / 2, y - hh, w, y, t, c->color);
      r.asc = -y + hh + t;
    } else if (cp == 0x303) {
      int s;
      i32 px = 0, py = 0;
      for (s = 0; s <= 16; s++) {
        i32 xx = w * s / 16, yy = y - EM(90) - (i32)((long long)EM(70) * isin(s * 3600 / 16) / TRIG_ONE);
        if (s) stroke(c->L, px, py, xx, yy, t, c->color);
        px = xx; py = yy;
      }
      r.asc = -y + EM(160) + t;
    } else {
      i32 hh = hshape(c, cp, em, 0, y - EM(240), w, c->color);
      r.asc = -y + hh;
    }
  }
  r.ic = 0;
  return r;
}
static Bx lay_bar(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), t = EM(50);
  u32 ba, be;
  Bx b = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &ba, &be), r = b;
  place(n->kid[0], 0, 0, &b);
  if (n->pv[P_BAR_POS] == MPOS_TOP + 1) { add_rect(c->L, 0, -(b.asc + 3 * t + t), b.w, t, c->color); r.asc = b.asc + 5 * t; }
  else { add_rect(c->L, 0, b.desc + 3 * t, b.w, t, c->color); r.desc = b.desc + 5 * t; }
  r.ic = 0;
  return r;
}
static Bx lay_groupchr(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), gap = EM(80);
  u32 cp = n->ch[0] == MCH_UNSET ? 0x23DF : n->ch[0], ba, be;
  int top = n->pv[P_G_POS] ? n->pv[P_G_POS] - 1 == MPOS_TOP : 0;
  Bx b = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &ba, &be), r = b;
  i32 h, y = top ? -(b.asc + gap + EM(250)) : b.desc + gap;
  place(n->kid[0], 0, 0, &b);
  h = hshape(c, cp, em, 0, y, b.w, c->color);
  if (!h) {
    /* another character: its glyph centred over or under the base */
    MNode stub;
    u32 aa = c->L->n;
    Bx a;
    memset(&stub, 0, sizeof stub); stub.color = COLOR_AUTO; stub.pv[P_R_STY] = MSTY_P + 1;
    a = lay_char(c, &stub, cp, em);
    shift(c->L, aa, c->L->n, (b.w - a.w) / 2, top ? -(b.asc + gap + a.desc) : b.desc + gap + a.asc);
    h = a.asc + a.desc;
    y = top ? -(b.asc + gap + h) : b.desc + gap;
  }
  if (top) r.asc = -y + EM(30); else r.desc = y + h + EM(30);
  if (r.w < EM(300)) r.w = EM(300);
  return r;
}
static Bx lay_lim(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), gap = EM(80);
  u32 ba, be, la, le;
  int low = n->kind == MK_LIMLOW;
  Bx b = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &ba, &be), l, r;
  i32 W, y;
  l = sub_row(c, n->kid[1], with_sz(n->kid[1], low ? st_sub(st) : st_sup(st)), &la, &le);
  W = b.w > l.w ? b.w : l.w;
  shift(c->L, ba, be, (W - b.w) / 2, 0); place(n->kid[0], (W - b.w) / 2, 0, &b);
  y = low ? b.desc + gap + l.asc : -(b.asc + gap + l.desc);
  shift(c->L, la, le, (W - l.w) / 2, y); place(n->kid[1], (W - l.w) / 2, y, &l);
  r = b; r.w = W; r.ic = 0;
  if (low) r.desc = y + l.desc; else r.asc = -y + l.asc;
  return r;
}
/* a block of rows (a matrix's, an equation array's) of total height ht placed round the axis, or with
   its first or last row on the baseline */
static i32 block_top(Ctx *c, St st, int jc, i32 ht, i32 firstasc, i32 lastdesc)
{
  i32 em = em_of(c, st), axis = EM(c->axis1000);
  if (jc == MJC_TOP) return -firstasc;
  if (jc == MJC_BOT) return lastdesc - ht;
  return -axis - ht / 2;
}
static Bx lay_matrix(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), rgap = EM(300), cgap = EM(1000), ht = 0, y, *cw, *ra, *rd, *cx, W = 0;
  int R = n->rows, C = n->cols, i, j, jc = n->pv[P_M_BASEJC] ? n->pv[P_M_BASEJC] - 1 : MJC_CENTER;
  u32 *ia;
  Bx *bx, r = empty_box();
  if (!R || !C || n->nkid < (u16)(R * C)) return r;
  if (n->iv[1] == 3 && n->iv[4] != MI_UNSET) cgap = n->iv[4];        /* cGpRule exactly: twips */
  else if (n->iv[1] == 1) cgap = EM(1500); else if (n->iv[1] == 2) cgap = EM(2000);
  if (n->iv[3] != MI_UNSET && n->iv[3] > 0) cgap = n->iv[3];          /* cSp: twips */
  if (n->iv[0] == 1) rgap = EM(550); else if (n->iv[0] == 2) rgap = EM(800); else if (n->iv[0] == 3 && n->iv[2] != MI_UNSET) rgap = n->iv[2];
  cw = (i32 *)calloc((size_t)(2 * C + 2 * R), sizeof(i32));
  ia = (u32 *)malloc((size_t)R * C * 2 * sizeof(u32));
  bx = (Bx *)malloc((size_t)R * C * sizeof(Bx));
  if (!cw || !ia || !bx) { free(cw); free(ia); free(bx); c->L->oom = 1; return r; }
  cx = cw + C; ra = cx + C; rd = ra + R;
  for (i = 0; i < R; i++) for (j = 0; j < C; j++) {
    int k = i * C + j;
    bx[k] = sub_row(c, n->kid[k], with_sz(n->kid[k], st), &ia[2 * k], &ia[2 * k + 1]);
    if (bx[k].w > cw[j]) cw[j] = bx[k].w;
    if (bx[k].asc > ra[i]) ra[i] = bx[k].asc;
    if (bx[k].desc > rd[i]) rd[i] = bx[k].desc;
  }
  for (i = 0; i < R; i++) { if (ra[i] < EM(c->xh1000)) ra[i] = EM(c->xh1000); ht += ra[i] + rd[i] + (i ? rgap : 0); }
  for (j = 0; j < C; j++) { cx[j] = W + (j ? cgap : 0); W = cx[j] + cw[j]; }
  y = block_top(c, st, jc, ht, ra[0], rd[R - 1]);
  for (i = 0; i < R; i++) {
    y += ra[i];
    for (j = 0; j < C; j++) {
      int k = i * C + j, col = 0, m, jcol = MCJ_CENTER;
      i32 x;
      /* the column's alignment (m:mcs groups) */
      for (m = 0; m < n->nmcs; m++) { if (j < col + n->mcs[m][0]) { jcol = n->mcs[m][1] ? n->mcs[m][1] - 1 : MCJ_CENTER; break; } col += n->mcs[m][0]; }
      x = jcol == MCJ_LEFT ? cx[j] : jcol == MCJ_RIGHT ? cx[j] + cw[j] - bx[k].w : cx[j] + (cw[j] - bx[k].w) / 2;
      shift(c->L, ia[2 * k], ia[2 * k + 1], x, y); place(n->kid[k], x, y, &bx[k]);
    }
    y += rd[i] + rgap;
  }
  r.w = W; r.asc = -block_top(c, st, jc, ht, ra[0], rd[R - 1]); r.desc = ht - r.asc;
  r.lc = r.rc = C_ORD;
  free(cw); free(ia); free(bx);
  return r;
}
static Bx lay_eqarr(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), rgap = EM(300), ht = 0, y, cw[33], *ra, *rd, W = 0;
  int R = n->nkid, i, j, k, jc = n->pv[P_EQ_BASEJC] ? n->pv[P_EQ_BASEJC] - 1 : MJC_CENTER;
  /* each row: its items, and its & breaks (column starts) */
  typedef struct { u32 a, e; Bx b; int nc; u32 ci[33]; i32 cx[33]; } Row;
  Row *rw;
  void *sv;
  int svn;
  Bx r = empty_box();
  if (!R) return r;
  if (n->iv[0] == 1) rgap = EM(550); else if (n->iv[0] == 2) rgap = EM(800); else if (n->iv[0] == 3 && n->iv[1] != MI_UNSET) rgap = n->iv[1];
  rw = (Row *)calloc((size_t)R, sizeof(Row));
  ra = (i32 *)calloc((size_t)2 * R, sizeof(i32));
  sv = (void *)malloc(sizeof c->brk);
  if (!rw || !ra || !sv) { free(rw); free(ra); free(sv); c->L->oom = 1; return r; }
  /* (the breaks an enclosing display line recorded so far, kept) */
  memcpy(sv, c->brk, sizeof c->brk); svn = c->nbrk;
  rd = ra + R;
  memset(cw, 0, sizeof cw);
  for (i = 0; i < R; i++) {
    int rec = c->rec, nb;
    rw[i].a = c->L->n;
    c->rec = 1; c->nbrk = 0;
    rw[i].b = lay_row(c, n->kid[i], with_sz(n->kid[i], st));
    nb = c->nbrk; c->rec = rec;
    rw[i].e = c->L->n;
    /* columns: [start, first break), [break, next)... */
    rw[i].nc = nb + 1 > 32 ? 32 : nb + 1;
    rw[i].ci[0] = rw[i].a; rw[i].cx[0] = 0;
    for (k = 1; k < rw[i].nc; k++) { rw[i].ci[k] = c->brk[k - 1].item; rw[i].cx[k] = c->brk[k - 1].x; }
    rw[i].ci[rw[i].nc] = rw[i].e; rw[i].cx[rw[i].nc] = rw[i].b.w;
    for (k = 0; k < rw[i].nc; k++) if (rw[i].cx[k + 1] - rw[i].cx[k] > cw[k]) cw[k] = rw[i].cx[k + 1] - rw[i].cx[k];
    ra[i] = rw[i].b.asc > EM(c->xh1000) ? rw[i].b.asc : EM(c->xh1000); rd[i] = rw[i].b.desc;
    ht += ra[i] + rd[i] + (i ? rgap : 0);
  }
  for (j = 0; j < 32 && cw[j]; j++) W += cw[j];
  y = block_top(c, st, jc, ht, ra[0], rd[R - 1]);
  for (i = 0; i < R; i++) {
    i32 x = 0;
    y += ra[i];
    /* the columns aligned in pairs, right then left (TeX's align): the & points line up */
    for (k = 0; k < rw[i].nc; k++) {
      i32 cwid = rw[i].cx[k + 1] - rw[i].cx[k], at = k % 2 == 0 ? x + cw[k] - cwid : x;
      shift(c->L, rw[i].ci[k], rw[i].ci[k + 1], at - rw[i].cx[k], y);
      if (k == 0) place(n->kid[i], at, y, &rw[i].b);
      x += cw[k];
    }
    y += rd[i] + rgap;
  }
  r.w = W > 0 ? W : 0; r.asc = -block_top(c, st, jc, ht, ra[0], rd[R - 1]); r.desc = ht - r.asc;
  r.lc = r.rc = C_ORD;
  memcpy(c->brk, sv, sizeof c->brk); c->nbrk = svn;
  free(rw); free(ra); free(sv);
  return r;
}
static Bx lay_borderbox(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), t = EM(45), pad = EM(100), w, top, bot;
  u32 ba, be;
  Bx b = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &ba, &be), r;
  shift(c->L, ba, be, pad + t, 0); place(n->kid[0], pad + t, 0, &b);
  w = b.w + 2 * (pad + t); top = -(b.asc + pad + t); bot = b.desc + pad + t;
  if (n->pv[P_BB_HIDETOP] != MB_ON) add_rect(c->L, 0, top, w, t, c->color);
  if (n->pv[P_BB_HIDEBOT] != MB_ON) add_rect(c->L, 0, bot - t, w, t, c->color);
  if (n->pv[P_BB_HIDELEFT] != MB_ON) add_rect(c->L, 0, top, t, bot - top, c->color);
  if (n->pv[P_BB_HIDERIGHT] != MB_ON) add_rect(c->L, w - t, top, t, bot - top, c->color);
  if (n->pv[P_BB_STRIKEH] == MB_ON) add_rect(c->L, 0, -EM(c->axis1000) - t / 2, w, t, c->color);
  if (n->pv[P_BB_STRIKEV] == MB_ON) add_rect(c->L, w / 2 - t / 2, top, t, bot - top, c->color);
  if (n->pv[P_BB_STRIKEBLTR] == MB_ON) stroke(c->L, 0, bot, w, top, t, c->color);
  if (n->pv[P_BB_STRIKETLBR] == MB_ON) stroke(c->L, 0, top, w, bot, t, c->color);
  r = b; r.w = w; r.asc = -top; r.desc = bot; r.ic = 0;
  return r;
}
static Bx lay_phant(Ctx *c, MNode *n, St st)
{
  u32 ba, be;
  Bx b = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &ba, &be);
  place(n->kid[0], 0, 0, &b);
  if (n->pv[P_PH_SHOW] == MB_OFF) c->L->n = ba;      /* hidden: its room kept, nothing drawn */
  if (n->pv[P_PH_ZEROWID] == MB_ON) b.w = 0;
  if (n->pv[P_PH_ZEROASC] == MB_ON) b.asc = 0;
  if (n->pv[P_PH_ZERODESC] == MB_ON) b.desc = 0;
  if (n->pv[P_PH_TRANSP] != MB_ON) b.lc = b.rc = C_ORD;
  return b;
}
static Bx lay_func(Ctx *c, MNode *n, St st)
{
  i32 em = em_of(c, st), gap;
  u32 na, ne, ea, ee;
  Bx a, e, r;
  a = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &na, &ne);
  e = sub_row(c, n->kid[1], with_sz(n->kid[1], st), &ea, &ee);
  place(n->kid[0], 0, 0, &a);
  gap = !e.w || e.lc == C_OPEN ? 0 : EM(167);       /* "sin x", but "sin(x)" */
  shift(c->L, ea, ee, a.w + gap, 0); place(n->kid[1], a.w + gap, 0, &e);
  r.w = a.w + gap + e.w; r.asc = a.asc > e.asc ? a.asc : e.asc; r.desc = a.desc > e.desc ? a.desc : e.desc;
  r.lc = C_OP; r.rc = e.w ? e.rc : C_OP; r.ic = 0;
  return r;
}
static Bx lay_node(Ctx *c, MNode *n, St st)
{
  Bx b = empty_box();
  if (++c->nodes > 20000) return b;
  switch (n->kind) {
  case MK_ROW: return lay_row(c, n, st);
  case MK_RAW: return n->nkid ? lay_row(c, n, st) : b;
  case MK_FRAC: return lay_frac(c, n, st);
  case MK_SSUB: case MK_SSUP: case MK_SSUBSUP: {
    u32 ba, be;
    MNode *base = n->kid[0];
    b = sub_row(c, base, with_sz(base, st), &ba, &be);
    place(base, 0, 0, &b);
    return scripts(c, b, n->kind == MK_SSUP ? NULL : n->kid[1], n->kind == MK_SSUB ? NULL : n->kid[n->kind == MK_SSUP ? 1 : 2], st, is_char_row(base), 0);
  }
  case MK_SPRE: {
    u32 ba, be;
    Bx base, r;
    base = sub_row(c, n->kid[2], with_sz(n->kid[2], st), &ba, &be);
    r = scripts(c, base, n->kid[0], n->kid[1], st, is_char_row(n->kid[2]), 1);
    /* the base after the scripts */
    shift(c->L, ba, be, r.w - base.w, 0);
    place(n->kid[2], r.w - base.w, 0, &base);
    return r;
  }
  case MK_RAD: return lay_rad(c, n, st);
  case MK_NARY: return lay_nary(c, n, st);
  case MK_DELIM: return lay_delim(c, n, st);
  case MK_FUNC: return lay_func(c, n, st);
  case MK_ACC: return lay_acc(c, n, st);
  case MK_BAR: return lay_bar(c, n, st);
  case MK_GROUPCHR: return lay_groupchr(c, n, st);
  case MK_LIMLOW: case MK_LIMUPP: return lay_lim(c, n, st);
  case MK_MATRIX: return lay_matrix(c, n, st);
  case MK_EQARR: return lay_eqarr(c, n, st);
  case MK_BOX: {
    u32 ba, be;
    b = sub_row(c, n->kid[0], with_sz(n->kid[0], st), &ba, &be);
    place(n->kid[0], 0, 0, &b);
    if (n->pv[P_BOX_OPEMU] != MB_ON) { b.lc = b.lc == C_BIN || b.lc == C_REL ? b.lc : C_ORD; }
    return b;
  }
  case MK_BORDERBOX: return lay_borderbox(c, n, st);
  case MK_PHANT: return lay_phant(c, n, st);
  }
  return b;
}
/* a row: its elements as atoms (a run gives one per character), TeX's spaces between them */
typedef struct { u32 a, e; Bx b; MNode *n; u8 mark; i32 x; } Atom;
static Atom *more_atoms(Ctx *c, Atom *at, int na, int *cap)
{
  Atom *n;
  if (na < *cap) return at;
  *cap = *cap ? *cap * 2 : 16;
  if (!(n = (Atom *)realloc(at, (size_t)*cap * sizeof(Atom)))) { free(at); c->L->oom = 1; return NULL; }
  return n;
}
static Bx lay_row(Ctx *c, MNode *row, St st)
{
  Atom *at = NULL;
  int na = 0, cap = 0, i, rec = c->rec, hash = 0;
  i32 em = em_of(c, st), x = 0;
  u16 k;
  Bx r = empty_box();
  c->rec = 0;
  for (k = 0; k < row->nkid; k++) {
    MNode *n = row->kid[k];
    if (n->kind == MK_RUN) {
      const char *t = n->text ? n->text : "";
      size_t len = strlen(t), o = 0;
      /* a run's own size (w:sz) against the text's */
      i32 rem = n->size && c->base > 0 && n->size * 10 != c->base ? (i32)((long long)em * n->size * 10 / c->base) : em;
      if (n->pv[P_R_BRK] == MB_ON && rec) {
        /* a line break in a display equation: a mark */
        if (!(at = more_atoms(c, at, na, &cap))) return r;
        memset(&at[na], 0, sizeof at[na]); at[na].a = at[na].e = c->L->n; at[na].mark = 2; na++;
      }
      if (n->pv[P_R_NOR] == MB_ON) {
        if (!(at = more_atoms(c, at, na, &cap))) return r;
        memset(&at[na], 0, sizeof at[na]);
        at[na].a = c->L->n; at[na].b = lay_text(c, n, t, len, rem); at[na].e = c->L->n; at[na].n = n; na++;
        continue;
      }
      while (o < len) {
        int used;
        u32 cp = utf8_get(t + o, len - o, &used);
        if (!(at = more_atoms(c, at, na, &cap))) return r;
        memset(&at[na], 0, sizeof at[na]);
        at[na].a = c->L->n;
        if (cp == '&' && rec) at[na].mark = 1;                  /* an equation array's alignment point */
        else if (cp == '#' && c->eqno == row && !hash) { at[na].mark = 3; hash = na + 1; }   /* WP14b: an equation number follows */
        else at[na].b = lay_char(c, n, cp, rem);
        at[na].e = c->L->n; at[na].n = o == 0 ? n : NULL;
        o += (size_t)used;
        na++;
      }
      if (!len) {
        /* an empty run: still somewhere (an editor's caret) */
        if (!(at = more_atoms(c, at, na, &cap))) return r;
        memset(&at[na], 0, sizeof at[na]);
        at[na].a = at[na].e = c->L->n; at[na].n = n; at[na].b.lc = at[na].b.rc = C_ORD; na++;
      }
      continue;
    }
    if (!(at = more_atoms(c, at, na, &cap))) return r;
    memset(&at[na], 0, sizeof at[na]);
    at[na].a = c->L->n; at[na].b = lay_node(c, n, st); at[na].e = c->L->n; at[na].n = n;
    na++;
  }
  /* binary operators with nothing to combine become ordinary */
  for (i = 0; i < na; i++) {
    int p = i - 1, q = i + 1;
    if (at[i].mark || at[i].b.lc != C_BIN) continue;
    while (p >= 0 && at[p].mark) p--;
    while (q < na && at[q].mark) q++;
    if (p < 0 || at[p].b.rc == C_BIN || at[p].b.rc == C_OP || at[p].b.rc == C_REL || at[p].b.rc == C_OPEN || at[p].b.rc == C_PUNCT ||
        q >= na || at[q].b.lc == C_REL || at[q].b.lc == C_CLOSE || at[q].b.lc == C_PUNCT)
      at[i].b.lc = at[i].b.rc = C_ORD;
  }
  /* placed side by side */
  c->segasc = c->segdesc = 0;
  for (i = 0; i < na; i++) {
    if (at[i].mark) {
      if (at[i].n) { at[i].n->x = x; at[i].n->y = 0; }      /* (a run starting with the mark: still somewhere) */
      if (at[i].mark == 3) { at[i].x = x; continue; }
      if (rec && c->nbrk < 64) { c->brk[c->nbrk].item = at[i].a; c->brk[c->nbrk].x = x; c->brk[c->nbrk].asc = c->segasc; c->brk[c->nbrk].desc = c->segdesc; c->nbrk++; }
      at[i].x = x;
      c->segasc = c->segdesc = 0;
      continue;
    }
    {
      int p = i - 1;
      while (p >= 0 && at[p].mark) p--;
      if (p >= 0 && (at[p].b.w || at[p].b.asc) && (at[i].b.w || at[i].b.asc)) {
        int sp = space_tab[at[p].b.rc & 7][at[i].b.lc & 7];
        if (sp < 0) sp = st.lvl <= 1 ? -sp : 0;
        x += em * sp / 18;
      }
    }
    shift(c->L, at[i].a, at[i].e, x, 0);
    at[i].x = x;
    if (at[i].n) {
      at[i].n->x = x; at[i].n->y = 0;
      at[i].n->w = at[i].b.w; at[i].n->asc = at[i].b.asc; at[i].n->desc = at[i].b.desc;
    }
    x += at[i].b.w;
    if (at[i].b.asc > r.asc) r.asc = at[i].b.asc;
    if (at[i].b.desc > r.desc) r.desc = at[i].b.desc;
    if (at[i].b.asc > c->segasc) c->segasc = at[i].b.asc;
    if (at[i].b.desc > c->segdesc) c->segdesc = at[i].b.desc;
  }
  if (hash) {
    /* WP14b: a numbered equation: the equation as justified in the line, its number at the right */
    int hk = hash - 1;
    i32 xh = at[hk].x, numw = x - xh, gap = EM(600), room = c->eqroom, mx, nx;
    if (room > 0 && room >= xh + gap + numw) {
      nx = room - numw;
      mx = c->eqjc == MPJC_LEFT ? 0 : c->eqjc == MPJC_RIGHT ? nx - gap - xh : (room - xh) / 2;
      if (mx + xh > nx - gap) mx = nx - gap - xh;
      if (mx < 0) mx = 0;
    } else { mx = 0; nx = xh + gap; }
    for (i = 0; i < na; i++) {
      i32 dx = i < hk ? mx : nx - xh;
      shift(c->L, at[i].a, at[i].e, dx, 0);
      at[i].x += dx;
      if (at[i].n) at[i].n->x += dx;
    }
    x = nx + numw;
  }
  /* runs: from their first character to the end of their last */
  for (i = 0; i < na; i++) if (at[i].n && at[i].n->kind == MK_RUN) {
    int j = i + 1;
    MNode *n = at[i].n;
    while (j < na && !at[j].n && !at[j].mark) j++;
    n->w = at[j - 1].x + at[j - 1].b.w - at[i].x;
    for (k = 0; (int)k < j - i; k++) {
      if (at[i + k].b.asc > n->asc) n->asc = at[i + k].b.asc;
      if (at[i + k].b.desc > n->desc) n->desc = at[i + k].b.desc;
    }
    /* WP14b: each character's x (the editor's caret); a normal-text run is one atom */
    if (n->pv[P_R_NOR] != MB_ON && n->text) {
      int nc = j - i, q;
      if (n->ncx < nc + 1) { free(n->cx); n->ncx = 0; if ((n->cx = (i32 *)malloc((size_t)(nc + 1) * sizeof(i32)))) n->ncx = (u16)(nc + 1); }
      if (n->cx && n->ncx >= nc + 1) {
        for (q = 0; q < nc; q++) n->cx[q] = at[i + q].x - at[i].x;
        n->cx[nc] = n->w;
        n->ncx = (u16)(nc + 1);
      }
    } else if (n->cx) { free(n->cx); n->cx = NULL; n->ncx = 0; }
  }
  if (na) {
    r.lc = 0xFF; r.rc = 0xFF;
    for (i = 0; i < na && r.lc == 0xFF; i++) if (!at[i].mark) r.lc = at[i].b.lc;
    for (i = na - 1; i >= 0 && r.rc == 0xFF; i--) if (!at[i].mark) { r.rc = at[i].b.rc; r.ic = at[i].b.ic; }
    if (r.lc == 0xFF) r.lc = r.rc = C_ORD;
  }
  r.w = x;
  free(at);
  c->rec = rec;
  if (!r.w && !r.asc && !r.desc) {
    /* WP14b: an empty argument (or equation) keeps room for its placeholder, a letter's size */
    for (k = 0; k < row->nkid && row->kid[k]->kind == MK_RUN && (!row->kid[k]->text || !row->kid[k]->text[0]); k++) ;
    if (k == row->nkid) { r.w = em * 55 / 100; r.asc = em * 68 / 100; r.desc = em * 8 / 100; r.lc = r.rc = C_ORD; c->segasc = r.asc; c->segdesc = r.desc; }
  }
  row->w = r.w; row->asc = r.asc; row->desc = r.desc;
  return r;
}

/* ------------------------------------------------------------------ */
/* the whole equation */
const MLay *math_layout(FontSet *fs, Doc *d, int k, const CharFmt *f)
{
  static MLay none;
  MathEq *e = doc_math(d, k);
  MLay *L;
  Ctx c;
  St st;
  u16 i;
  i32 y = 0, W = 0, em, firstasc = -1, lastdesc = 0;
  /* the lines: each one's items and width (display equations' lines may be broken further by m:brk) */
  struct { u32 a, e; i32 w; } ln[96];
  int nln = 0, j;
  u32 ln_i0;
  if (!e) { memset(&none, 0, sizeof none); return &none; }
  if (e->lay && e->laysize == f->size && e->laycolor == f->color && e->layver == e->ver && e->layfs == (void *)fs && e->laydisp == e->display && e->layfont == f->font &&
      e->layroom == e->room)
    return e->lay;
  math_lay_free(e->lay); e->lay = NULL;
  if (!(L = (MLay *)calloc(1, sizeof(MLay)))) return NULL;
  memset(&c, 0, sizeof c);
  c.fs = fs; c.L = L; c.d = d;
  math_settings(d, &c.mp);
  for (j = 0; j < 4; j++) {
    c.serif[j] = loaded(fonts_face(fs, "Liberation Serif", j & 1, j >> 1));
    c.sans[j] = loaded(fonts_face(fs, "Liberation Sans", j & 1, j >> 1));
  }
  c.mono = loaded(fonts_face(fs, "Liberation Mono", 0, 0));
  c.carlito = loaded(fonts_face(fs, "Carlito", 0, 0));
  c.text = f->font < d->nfonts ? fonts_face(fs, d->fonts[f->font], 0, 0) : NULL;
  c.base = f->size * 10;
  if (f->flags & (CF_SUPER | CF_SUB)) c.base = c.base * 2 / 3;
  if (c.base < 40) c.base = 240;
  c.color = f->color;
  /* the math axis (the middle of a plus) and the x-height, from the font */
  c.axis1000 = 250; c.xh1000 = 450;
  if (c.serif[0]) {
    i32 adv, x0, y0, x1, y1;
    u16 g = ttf_glyph(c.serif[0], '+');
    if (g) { glyph_met(c.serif[0], g, 1000, &adv, &x0, &y0, &x1, &y1); if (y1 > y0) c.axis1000 = (y0 + y1) / 2; }
    if ((g = ttf_glyph(c.serif[0], 'x'))) { glyph_met(c.serif[0], g, 1000, &adv, &x0, &y0, &x1, &y1); if (y1 > 0) c.xh1000 = y1; }
  }
  st.lvl = (u8)(e->display ? 0 : 1); st.cr = 0; st.sz = 0;
  em = c.base;
  if (math_numbered(e)) {           /* WP14b: a numbered display equation spans the line */
    c.eqno = e->line[0]->kid[0]->kid[0]; c.eqroom = e->room;
    c.eqjc = e->jc ? e->jc - 1 : c.mp.defjc ? c.mp.defjc - 1 : MPJC_CENTERGROUP;
  }
  for (i = 0; i < e->nline; i++) {
    Bx b;
    int nb, s;
    c.rec = e->display; c.nbrk = 0;
    ln_i0 = L->n;
    b = lay_row(&c, e->line[i], st);
    nb = e->display ? c.nbrk : 0;
    /* the row's pieces between its breaks, one below the other (later ones indented) */
    for (s = 0; s <= nb && nln < 96; s++) {
      u32 a = s ? c.brk[s - 1].item : ln_i0, z = s < nb ? c.brk[s].item : L->n;
      i32 x0 = s ? c.brk[s - 1].x : 0, x1 = s < nb ? c.brk[s].x : b.w;
      i32 asc = s < nb ? c.brk[s].asc : c.segasc, desc = s < nb ? c.brk[s].desc : c.segdesc, ind = s ? c.mp.wrapind : 0;
      if (firstasc < 0) { firstasc = asc; y = 0; }
      else y += lastdesc + EM(300) + asc;
      shift(L, a, z, ind - x0, y);
      if (!s) { e->line[i]->x = 0; e->line[i]->y = y; }
      ln[nln].a = a; ln[nln].e = z; ln[nln].w = x1 - x0 + ind;
      if (ln[nln].w > W) W = ln[nln].w;
      nln++;
      lastdesc = desc;
    }
  }
  /* a display equation's lines placed as its justification says */
  if (e->display && nln > 1) {
    int jc = e->jc ? e->jc - 1 : c.mp.defjc ? c.mp.defjc - 1 : MPJC_CENTERGROUP;
    for (j = 0; j < nln; j++) {
      i32 dx = jc == MPJC_LEFT ? 0 : jc == MPJC_RIGHT ? W - ln[j].w : (W - ln[j].w) / 2;
      shift(L, ln[j].a, ln[j].e, dx, 0);
    }
  }
  L->w = W; L->asc = firstasc > 0 ? firstasc : 0; L->desc = y + lastdesc;
  if (L->desc < 0) L->desc = 0;
  e->lay = L; e->laysize = f->size; e->laycolor = f->color; e->layver = e->ver; e->layfs = (void *)fs; e->laydisp = e->display; e->layfont = f->font;
  e->layroom = e->room;
  return L;
}
