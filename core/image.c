/* image.c - see image.h */
#include <stdlib.h>
#include <string.h>
#include "image.h"
#include "picfmt.h"
#include "zip.h"

/* both formats store numbers big-endian */
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }

/* ------------------------------------------------------------------ */
/* scaling down as rows arrive: each shown pixel is the average of the source pixels in it */
typedef struct {
  u32 sw, sh, ow, oh;
  u32 *acc;                 /* ow * 3 sums for the shown row being made */
  u32 *cols;                /* how many source columns fall in each shown column */
  u32 *xmap;                /* source column -> shown column */
  u32 cury, rows;           /* the shown row being made, and how many source rows it has */
  u8 *out;
} Scaler;
/* a scaler from sw x sh source pixels to ow x oh, which must be no bigger (img_decode enlarges
   afterwards). 0 when out of memory; call sc_free then, since some buffers may be allocated */
static int sc_init(Scaler *s, u32 sw, u32 sh, u32 ow, u32 oh)
{
  u32 x;
  memset(s, 0, sizeof *s);
  s->sw = sw; s->sh = sh; s->ow = ow; s->oh = oh;
  s->acc = (u32 *)calloc(ow * 3, sizeof(u32));
  s->cols = (u32 *)calloc(ow, sizeof(u32));
  s->xmap = (u32 *)malloc(sw * sizeof(u32));
  s->out = (u8 *)malloc((size_t)ow * oh * 3);
  if (!s->acc || !s->cols || !s->xmap || !s->out) return 0;
  for (x = 0; x < sw; x++) { s->xmap[x] = (u32)((unsigned long)x * ow / sw); s->cols[s->xmap[x]]++; }
  return 1;
}
/* the shown row being gathered, averaged and written out; the sums start again from zero */
static void sc_flush(Scaler *s)
{
  u32 x;
  u8 *o;
  if (!s->rows || s->cury >= s->oh) return;
  o = s->out + (size_t)s->cury * s->ow * 3;
  for (x = 0; x < s->ow; x++) {
    u32 n = s->cols[x] * s->rows;
    if (!n) n = 1;
    o[x * 3] = (u8)(s->acc[x * 3] / n); o[x * 3 + 1] = (u8)(s->acc[x * 3 + 1] / n); o[x * 3 + 2] = (u8)(s->acc[x * 3 + 2] / n);
  }
  memset(s->acc, 0, s->ow * 3 * sizeof(u32));
  s->rows = 0;
}
/* adds source row y (sw RGB pixels) into the shown row it falls in. Rows must come in order,
   top down: a row for a new shown row flushes the one before */
static void sc_row(Scaler *s, const u8 *rgb, u32 y)
{
  u32 ty = (u32)((unsigned long)y * s->oh / s->sh), x;
  if (ty != s->cury) { sc_flush(s); s->cury = ty; }
  for (x = 0; x < s->sw; x++) {
    u32 *a = s->acc + s->xmap[x] * 3;
    a[0] += rgb[x * 3]; a[1] += rgb[x * 3 + 1]; a[2] += rgb[x * 3 + 2];
  }
  s->rows++;
}
/* flushes the last row and hands over the picture (malloc'd, ow x oh RGB), freeing the rest */
static u8 *sc_finish(Scaler *s)
{
  u8 *o;
  sc_flush(s);
  free(s->acc); free(s->cols); free(s->xmap);
  o = s->out; s->out = NULL;
  return o;
}
/* gives up: frees everything, the output too */
static void sc_free(Scaler *s) { free(s->acc); free(s->cols); free(s->xmap); free(s->out); memset(s, 0, sizeof *s); }

/* ------------------------------------------------------------------ */
/* PNG */
typedef struct {
  u32 w, h;
  int depth, ctype, interlace, chans, bpp;    /* bpp: bits per pixel */
  u8 pal[256][4]; int npal;
  int trns; u16 key[3];                       /* a transparent colour (grey or RGB) */
  u8 *idat; u32 nidat;
} Png;
/* the signature and IHDR into p; 0 if not a PNG, an invalid colour type / depth pair, or over
   30000 pixels either way (which also keeps the row-size sums inside 32 bits) */
static int png_head(const u8 *d, u32 n, Png *p)
{
  static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  if (n < 33 || memcmp(d, sig, 8) || memcmp(d + 12, "IHDR", 4)) return 0;
  memset(p, 0, sizeof *p);
  p->w = be32(d + 16); p->h = be32(d + 20);
  p->depth = d[24]; p->ctype = d[25]; p->interlace = d[28];
  p->chans = p->ctype == 0 ? 1 : p->ctype == 2 ? 3 : p->ctype == 3 ? 1 : p->ctype == 4 ? 2 : p->ctype == 6 ? 4 : 0;
  if (!p->chans || !p->w || !p->h || p->w > 30000 || p->h > 30000) return 0;
  if (p->depth != 1 && p->depth != 2 && p->depth != 4 && p->depth != 8 && p->depth != 16) return 0;
  if ((p->ctype == 2 || p->ctype == 4 || p->ctype == 6) && p->depth < 8) return 0;
  if (p->ctype == 3 && p->depth > 8) return 0;
  p->bpp = p->chans * p->depth;
  return 1;
}
/* collects the palette and transparency, then joins every IDAT chunk into one malloc'd stream
   in p->idat (the caller frees it). CRCs are not checked. 0 if a chunk overruns the file or
   there is no image data */
static int png_chunks(const u8 *d, u32 n, Png *p)
{
  u32 o = 8, total = 0;
  /* first pass: sizes, palette and transparency */
  while (o + 12 <= n) {
    u32 len = be32(d + o);
    const u8 *t = d + o + 4, *c = d + o + 8;
    if (len > n - o - 12) return 0;
    if (!memcmp(t, "PLTE", 4)) {
      u32 i;
      p->npal = (int)(len / 3 > 256 ? 256 : len / 3);
      for (i = 0; i < (u32)p->npal; i++) { p->pal[i][0] = c[i * 3]; p->pal[i][1] = c[i * 3 + 1]; p->pal[i][2] = c[i * 3 + 2]; p->pal[i][3] = 255; }
    } else if (!memcmp(t, "tRNS", 4)) {
      u32 i;
      if (p->ctype == 3) for (i = 0; i < len && i < 256; i++) p->pal[i][3] = c[i];
      else if (p->ctype == 0 && len >= 2) { p->trns = 1; p->key[0] = (u16)be16(c); }
      else if (p->ctype == 2 && len >= 6) { p->trns = 1; p->key[0] = (u16)be16(c); p->key[1] = (u16)be16(c + 2); p->key[2] = (u16)be16(c + 4); }
    } else if (!memcmp(t, "IDAT", 4)) total += len;
    else if (!memcmp(t, "IEND", 4)) break;
    o += len + 12;
  }
  if (!total || !(p->idat = (u8 *)malloc(total))) return 0;
  for (o = 8; o + 12 <= n; ) {
    u32 len = be32(d + o);
    if (!memcmp(d + o + 4, "IDAT", 4)) { memcpy(p->idat + p->nidat, d + o + 8, len); p->nidat += len; }
    if (!memcmp(d + o + 4, "IEND", 4)) break;
    o += len + 12;
  }
  return 1;
}
/* PNG's Paeth predictor: whichever of left, above and above-left is nearest a + b - c */
static int paeth(int a, int b, int c)
{
  int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
/* undo the filters of h rows of rb bytes (each after its filter byte), in place */
static int unfilter(u8 *raw, u32 rb, u32 h, int fb)
{
  u32 y, x;
  u8 *prev = NULL;
  for (y = 0; y < h; y++) {
    u8 *r = raw + (size_t)y * (rb + 1), f = r[0], *c = r + 1;
    for (x = 0; x < rb; x++) {
      int a = x >= (u32)fb ? c[x - fb] : 0, b = prev ? prev[x] : 0, cc = prev && x >= (u32)fb ? prev[x - fb] : 0;
      switch (f) {
      case 0: break;
      case 1: c[x] = (u8)(c[x] + a); break;
      case 2: c[x] = (u8)(c[x] + b); break;
      case 3: c[x] = (u8)(c[x] + ((a + b) >> 1)); break;
      case 4: c[x] = (u8)(c[x] + paeth(a, b, cc)); break;
      default: return 0;
      }
    }
    prev = c;
  }
  return 1;
}
/* row of w pixels (after the filter byte) to RGB over white */
static void png_rgb(const Png *p, const u8 *row, u32 w, u8 *out)
{
  u32 x;
  for (x = 0; x < w; x++) {
    u32 r, g, b, a = 255;
    if (p->depth < 8) {
      u32 bit = x * (u32)p->depth, v = (row[bit >> 3] >> (8 - p->depth - (bit & 7))) & ((1u << p->depth) - 1);
      if (p->ctype == 3) { r = p->pal[v][0]; g = p->pal[v][1]; b = p->pal[v][2]; a = p->pal[v][3]; }
      else { if (p->trns && v == p->key[0]) a = 0; r = g = b = v * 255 / ((1u << p->depth) - 1); }
    } else {
      int s = p->depth / 8;                                /* bytes per sample; 16-bit: the high byte */
      const u8 *q = row + (size_t)x * p->chans * s;
      switch (p->ctype) {
      case 0: r = g = b = q[0]; if (p->trns && (s == 2 ? be16(q) : q[0]) == p->key[0]) a = 0; break;
      case 2: r = q[0]; g = q[s]; b = q[2 * s];
              if (p->trns && (s == 2 ? be16(q) == p->key[0] && be16(q + 2) == p->key[1] && be16(q + 4) == p->key[2]
                                     : q[0] == p->key[0] && q[1] == p->key[1] && q[2] == p->key[2])) a = 0;
              break;
      case 3: r = p->pal[q[0]][0]; g = p->pal[q[0]][1]; b = p->pal[q[0]][2]; a = p->pal[q[0]][3]; break;
      case 4: r = g = b = q[0]; a = q[s]; break;
      default: r = q[0]; g = q[s]; b = q[2 * s]; a = q[3 * s]; break;
      }
    }
    if (a < 255) { r = (r * a + 255 * (255 - a)) / 255; g = (g * a + 255 * (255 - a)) / 255; b = (b * a + 255 * (255 - a)) / 255; }
    out[x * 3] = (u8)r; out[x * 3 + 1] = (u8)g; out[x * 3 + 2] = (u8)b;
  }
}
/* inflates all the image data at once (refusing over 256MB of it), then unfilters and feeds
   the rows to the scaler. An interlaced picture is built whole at full size first, since its
   passes don't arrive top down. 0 for a damaged or unsupported file */
static int png_decode(const u8 *d, u32 n, Scaler *sc)
{
  static const u8 a7[7][4] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
  Png p;
  u8 *raw = NULL, *line = NULL, *full = NULL;
  unsigned long size = 0;
  u32 y;
  int fb, pass, ok = 0;
  if (!png_head(d, n, &p) || !png_chunks(d, n, &p) || p.nidat < 6) goto done;
  fb = p.bpp >= 8 ? p.bpp / 8 : 1;
  if (!p.interlace) size = (unsigned long)p.h * (((unsigned long)p.w * p.bpp + 7) / 8 + 1);
  else for (pass = 0; pass < 7; pass++) {
    u32 pw = (p.w - a7[pass][0] + a7[pass][2] - 1) / a7[pass][2], ph = (p.h - a7[pass][1] + a7[pass][3] - 1) / a7[pass][3];
    if (p.w > a7[pass][0] && p.h > a7[pass][1]) size += (unsigned long)ph * (((unsigned long)pw * p.bpp + 7) / 8 + 1);
  }
  if (size > 256UL * 1024 * 1024 || !(raw = (u8 *)malloc(size + 1))) goto done;
  /* the zlib stream: a 2-byte header around deflate data */
  if (zinflate(p.idat + 2, p.nidat - 2, raw, size) != (long)size) goto done;
  if (!(line = (u8 *)malloc((size_t)p.w * 3))) goto done;
  if (!p.interlace) {
    u32 rb = (p.w * (u32)p.bpp + 7) / 8;
    if (!unfilter(raw, rb, p.h, fb)) goto done;
    for (y = 0; y < p.h; y++) { png_rgb(&p, raw + (size_t)y * (rb + 1) + 1, p.w, line); sc_row(sc, line, y); }
  } else {
    /* interlaced: the seven passes into a whole picture first */
    u8 *r = raw;
    if (!(full = (u8 *)malloc((size_t)p.w * p.h * 3))) goto done;
    memset(full, 255, (size_t)p.w * p.h * 3);
    for (pass = 0; pass < 7; pass++) {
      u32 sx = a7[pass][0], sy = a7[pass][1], dx = a7[pass][2], dy = a7[pass][3], pw, ph, rb, x;
      u8 *prow;
      if (p.w <= sx || p.h <= sy) continue;
      pw = (p.w - sx + dx - 1) / dx; ph = (p.h - sy + dy - 1) / dy; rb = (pw * (u32)p.bpp + 7) / 8;
      if (!unfilter(r, rb, ph, fb)) goto done;
      if (!(prow = (u8 *)malloc((size_t)pw * 3))) goto done;
      for (y = 0; y < ph; y++) {
        png_rgb(&p, r + (size_t)y * (rb + 1) + 1, pw, prow);
        for (x = 0; x < pw; x++) memcpy(full + ((size_t)(sy + y * dy) * p.w + sx + x * dx) * 3, prow + x * 3, 3);
      }
      free(prow);
      r += (size_t)ph * (rb + 1);
    }
    for (y = 0; y < p.h; y++) sc_row(sc, full + (size_t)y * p.w * 3, y);
  }
  ok = 1;
done:
  free(p.idat); free(raw); free(line); free(full);
  return ok;
}

/* ------------------------------------------------------------------ */
/* JPEG (baseline and progressive) */
static const u8 zz[80] = {                 /* zigzag -> natural order (padded for damaged data) */
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63 };
typedef struct {
  u8 vals[256];
  i32 maxcode[18], valptr[17], mincode[17];
  u16 look[256];                            /* 8-bit lookahead: length << 8 | value, 0 if longer */
  int ok;
} Huff;
typedef struct { int id, h, v, tq, td, ta, pred; u32 bw, bh; u8 *plane; i16 *coef; u32 cbw, cbh; } Comp;
typedef struct {
  const u8 *d; u32 n, pos;
  u32 acc; int nbits, marker;
  i32 q[4][64];                             /* dequantising tables with the AAN scale, natural order */
  Huff dc[4], ac[4];
  Comp c[4]; int nc;
  u32 w, h; int hmax, vmax, progressive, restart;
  int ns, sc[4], ss, se, ah, al;            /* the scan: its components and (progressive) its part */
  int adobe, transform; u32 eobrun;
} Jpeg;
/* the canonical Huffman table from a DHT segment's 16 code-length counts and values, as in the
   JPEG spec's Annex C, plus a lookup table for codes of 8 bits or fewer */
static void build_huff(Huff *t, const u8 *bits, const u8 *vals, int nv)
{
  int l, i, k = 0, code = 0;
  memset(t, 0, sizeof *t);
  memcpy(t->vals, vals, (size_t)nv);
  for (l = 1; l <= 16; l++) {
    t->valptr[l] = k; t->mincode[l] = code;
    code += bits[l - 1]; k += bits[l - 1];
    t->maxcode[l] = bits[l - 1] ? code - 1 : -1;
    if (l <= 8) for (i = t->mincode[l]; i <= t->maxcode[l]; i++) {
      int lo = (i << (8 - l)), j;
      for (j = 0; j < (1 << (8 - l)); j++) t->look[lo + j] = (u16)((l << 8) | vals[t->valptr[l] + i - t->mincode[l]]);
    }
    code <<= 1;
  }
  t->maxcode[17] = 0x7FFFFFFF;
  t->ok = 1;
}
/* tops the bit accumulator up to more than 24 bits, removing the 0 stuffed after each FF byte.
   At a marker (or the end of the data) it feeds zeros instead, so damaged data runs out quietly
   rather than reading past the scan */
static void fill(Jpeg *j)
{
  while (j->nbits <= 24) {
    u32 b = 0;
    if (!j->marker && j->pos < j->n) {
      b = j->d[j->pos++];
      if (b == 0xFF) {
        u32 c = j->pos < j->n ? j->d[j->pos] : 0;
        if (c == 0) j->pos++;
        else { j->marker = (int)c; j->pos--; b = 0; }   /* a marker: zeros from here */
      }
    }
    j->acc |= b << (24 - j->nbits);
    j->nbits += 8;
  }
}
/* the next n bits (n at most 16) as an unsigned number */
static int getbits(Jpeg *j, int n)
{
  int v;
  if (!n) return 0;
  fill(j);
  v = (int)(j->acc >> (32 - n));
  j->acc <<= n; j->nbits -= n;
  return v;
}
/* one Huffman-coded symbol: the 8-bit lookup first, then bit by bit for longer codes */
static int decode(Jpeg *j, Huff *t)
{
  int l, code;
  u16 lk;
  fill(j);
  lk = t->look[j->acc >> 24];
  if (lk) { l = lk >> 8; j->acc <<= l; j->nbits -= l; return lk & 255; }
  code = getbits(j, 8);
  for (l = 9; l <= 16; l++) {
    code = (code << 1) | getbits(j, 1);
    if (code <= t->maxcode[l]) return t->vals[t->valptr[l] + code - t->mincode[l]];
  }
  return 0;                                 /* damaged: carry on with something */
}
/* the JPEG spec's EXTEND: s raw bits to a signed value (codes below half the range are negative) */
static int extend(int v, int s) { return s && v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }

#define F1_414 362
#define F1_847 473
#define F1_082 277
#define F2_613 669
#define MUL(a, b) (((a) * (b)) >> 8)
/* the AAN inverse DCT (as IJG's jidctfst): coefficients already dequantised with 2 fraction bits */
static void idct(i32 *in, u8 *out, u32 stride)
{
  i32 t0, t1, t2, t3, t4, t5, t6, t7, t10, t11, t12, t13, z5, z10, z11, z12, z13, ws[64];
  int i;
  for (i = 0; i < 8; i++) {
    i32 *s = in + i, *w = ws + i;
    if (!s[8] && !s[16] && !s[24] && !s[32] && !s[40] && !s[48] && !s[56]) {
      w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = s[0];
      continue;
    }
    t0 = s[0]; t1 = s[16]; t2 = s[32]; t3 = s[48];
    t10 = t0 + t2; t11 = t0 - t2; t13 = t1 + t3; t12 = MUL(t1 - t3, F1_414) - t13;
    t0 = t10 + t13; t3 = t10 - t13; t1 = t11 + t12; t2 = t11 - t12;
    t4 = s[8]; t5 = s[24]; t6 = s[40]; t7 = s[56];
    z13 = t6 + t5; z10 = t6 - t5; z11 = t4 + t7; z12 = t4 - t7;
    t7 = z11 + z13; t11 = MUL(z11 - z13, F1_414);
    z5 = MUL(z10 + z12, F1_847); t10 = MUL(z12, F1_082) - z5; t12 = MUL(z10, -F2_613) + z5;
    t6 = t12 - t7; t5 = t11 - t6; t4 = t10 + t5;
    w[0] = t0 + t7; w[56] = t0 - t7; w[8] = t1 + t6; w[48] = t1 - t6;
    w[16] = t2 + t5; w[40] = t2 - t5; w[32] = t3 + t4; w[24] = t3 - t4;
  }
  for (i = 0; i < 8; i++) {
    i32 *w = ws + i * 8, v[8];
    u8 *o = out + (size_t)i * stride;
    int k;
    t10 = w[0] + w[4]; t11 = w[0] - w[4]; t13 = w[2] + w[6]; t12 = MUL(w[2] - w[6], F1_414) - t13;
    t0 = t10 + t13; t3 = t10 - t13; t1 = t11 + t12; t2 = t11 - t12;
    z13 = w[5] + w[3]; z10 = w[5] - w[3]; z11 = w[1] + w[7]; z12 = w[1] - w[7];
    t7 = z11 + z13; t11 = MUL(z11 - z13, F1_414);
    z5 = MUL(z10 + z12, F1_847); t10 = MUL(z12, F1_082) - z5; t12 = MUL(z10, -F2_613) + z5;
    t6 = t12 - t7; t5 = t11 - t6; t4 = t10 + t5;
    v[0] = t0 + t7; v[7] = t0 - t7; v[1] = t1 + t6; v[6] = t1 - t6; v[2] = t2 + t5; v[5] = t2 - t5; v[4] = t3 + t4; v[3] = t3 - t4;
    for (k = 0; k < 8; k++) {
      i32 x = (v[k] >> 5) + 128;            /* 2 fraction bits and the 1/8 of the transform */
      o[k] = (u8)(x < 0 ? 0 : x > 255 ? 255 : x);
    }
  }
}
/* the markers up to the next scan's data (from o); the scan's position, 0 at the end or if not a JPEG we know */
static u32 jpeg_markers(Jpeg *j, u32 o)
{
  static const i32 aan[8] = { 16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520 };
  const u8 *d = j->d;
  u32 n = j->n;
  while (o + 4 <= n) {
    u32 m, len;
    if (d[o] != 0xFF) { o++; continue; }
    m = d[o + 1];
    if (m == 0xFF) { o++; continue; }
    if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0x00) { o += 2; continue; }
    if (m == 0xD9) return 0;
    len = be16(d + o + 2);
    if (len < 2 || o + 2 + len > n) return 0;
    if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
      const u8 *s = d + o + 4;
      int i;
      if (len < 8 || j->w) return 0;
      j->progressive = m == 0xC2;
      j->h = be16(s + 1); j->w = be16(s + 3); j->nc = s[5];
      if (s[0] != 8 || !j->w || !j->h || (j->nc != 1 && j->nc != 3 && j->nc != 4) || len < 8 + 3 * (u32)j->nc) return 0;
      for (i = 0; i < j->nc; i++) {
        Comp *c = &j->c[i];
        c->id = s[6 + i * 3]; c->h = s[7 + i * 3] >> 4; c->v = s[7 + i * 3] & 15; c->tq = s[8 + i * 3] & 3;
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) return 0;
        if (c->h > j->hmax) j->hmax = c->h;
        if (c->v > j->vmax) j->vmax = c->v;
      }
    } else if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) return 0;   /* lossless, arithmetic... */
    else if (m == 0xDB) {
      u32 p = o + 4;
      while (p < o + 2 + len) {
        int pq = d[p] >> 4, tq = d[p] & 3, k;
        p++;
        if (p + (pq ? 128u : 64u) > o + 2 + len) return 0;
        for (k = 0; k < 64; k++) {
          i32 q = pq ? (i32)be16(d + p + k * 2) : d[p + k];
          int nat = zz[k];
          j->q[tq][nat] = (q * ((aan[nat >> 3] * aan[nat & 7]) >> 14)) >> 12;
        }
        p += pq ? 128 : 64;
      }
    } else if (m == 0xC4) {
      u32 p = o + 4;
      while (p + 17 <= o + 2 + len) {
        int tc = d[p] >> 4, th = d[p] & 3, k, nv = 0;
        for (k = 0; k < 16; k++) nv += d[p + 1 + k];
        if (nv > 256 || p + 17 + (u32)nv > o + 2 + len) return 0;
        build_huff(tc ? &j->ac[th] : &j->dc[th], d + p + 1, d + p + 17, nv);
        p += 17 + (u32)nv;
      }
    } else if (m == 0xDD) j->restart = (int)be16(d + o + 4);
    else if (m == 0xEE && len >= 14 && !memcmp(d + o + 4, "Adobe", 5)) { j->adobe = 1; j->transform = d[o + 15]; }
    else if (m == 0xDA) {
      const u8 *s = d + o + 4;
      int ns = s[0], i, k;
      if (!j->w || ns < 1 || ns > 4 || len < 6 + 2 * (u32)ns) return 0;
      j->ns = ns;
      for (i = 0; i < ns; i++) {
        for (k = 0; k < j->nc && j->c[k].id != s[1 + i * 2]; k++) ;
        if (k == j->nc) return 0;
        j->sc[i] = k;
        j->c[k].td = s[2 + i * 2] >> 4 & 3; j->c[k].ta = s[2 + i * 2] & 3;
      }
      j->ss = s[1 + ns * 2]; j->se = s[2 + ns * 2]; j->ah = s[3 + ns * 2] >> 4; j->al = s[3 + ns * 2] & 15;
      if (!j->progressive) { j->ss = 0; j->se = 63; j->ah = j->al = 0; }
      if (j->ss > 63 || j->se > 63 || j->ss > j->se || (j->ss == 0 && j->se != 0 && j->progressive) || j->al > 13) return 0;
      return o + 2 + len;
    }
    o += 2 + len;
  }
  return 0;
}
/* starts a decode: checks the SOI marker and reads the tables and frame up to the first scan.
   The offset of that scan's data, or 0 if this is not a JPEG we can decode */
static u32 jpeg_head(Jpeg *j, const u8 *d, u32 n)
{
  memset(j, 0, sizeof *j);
  j->d = d; j->n = n; j->transform = -1;
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;
  return jpeg_markers(j, 2);
}
/* MCU sizes, with a single component's sampling taken as 1 x 1 */
static void jpeg_geometry(Jpeg *j, u32 *mcux, u32 *mcuy)
{
  if (j->nc == 1) { j->c[0].h = j->c[0].v = 1; j->hmax = j->vmax = 1; }
  *mcux = (j->w + 8 * j->hmax - 1) / (8 * j->hmax); *mcuy = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
}
/* a colour channel limited to 0-255 */
static u8 clamp8(i32 v) { return (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }
/* row y of the strip (planes one row of MCUs high) to RGB */
static void jpeg_rgb(const Jpeg *j, u32 y, u8 *rgb)
{
  const u8 *pl[4];
  u32 x;
  int i, k;
  for (i = 0; i < j->nc; i++) pl[i] = j->c[i].plane + (size_t)(y * j->c[i].v / j->vmax) * j->c[i].bw;
  for (x = 0; x < j->w; x++) {
    i32 v[4];
    u8 *o = rgb + x * 3;
    for (i = 0; i < j->nc; i++) v[i] = pl[i][x * j->c[i].h / j->hmax];
    if (j->nc == 1) { o[0] = o[1] = o[2] = (u8)v[0]; continue; }
    if (j->transform != 0 || j->nc == 4) {
      /* YCbCr, unless an Adobe marker says the three are RGB or the four plain CMYK */
      if (j->nc == 3 || j->transform == 2) {
        i32 Y = v[0], B = v[1] - 128, R = v[2] - 128;
        v[0] = clamp8(Y + ((91881 * R) >> 16)); v[1] = clamp8(Y - ((22554 * B + 46802 * R) >> 16)); v[2] = clamp8(Y + ((116130 * B) >> 16));
        if (j->nc == 4) for (k = 0; k < 3; k++) v[k] = 255 - v[k];      /* YCCK: the conversion gives CMY */
      }
    }
    if (j->nc == 4) {
      /* Adobe's (the usual) CMYK is stored inverted */
      if (!j->adobe) for (k = 0; k < 4; k++) v[k] = 255 - v[k];
      for (k = 0; k < 3; k++) v[k] = (v[k] * v[3] + 127) / 255;
    }
    o[0] = (u8)v[0]; o[1] = (u8)v[1]; o[2] = (u8)v[2];
  }
}
/* at a restart interval: skips to just past the next RSTn marker and resets the bit reader,
   the DC predictions and the progressive end-of-band run */
static void jpeg_restart(Jpeg *j)
{
  int i;
  j->acc = 0; j->nbits = 0; j->marker = 0;
  while (j->pos + 1 < j->n && !(j->d[j->pos] == 0xFF && j->d[j->pos + 1] >= 0xD0 && j->d[j->pos + 1] <= 0xD7)) j->pos++;
  if (j->pos + 1 < j->n) j->pos += 2;
  for (i = 0; i < j->nc; i++) j->c[i].pred = 0;
  j->eobrun = 0;
}
/* baseline with all the components in one scan: decoded a strip at a time, never whole */
static int jpeg_stream(Jpeg *j, u32 start, Scaler *sc)
{
  u32 mx, my, mcux, mcuy, y;
  int i, ok = 0, todo;
  u8 *rgb = NULL;
  i32 blk[64];
  for (i = 0; i < j->nc; i++) if (!j->dc[j->c[i].td].ok || !j->ac[j->c[i].ta].ok) return 0;
  jpeg_geometry(j, &mcux, &mcuy);
  for (i = 0; i < j->nc; i++) {
    Comp *c = &j->c[i];
    c->bw = mcux * c->h * 8; c->bh = c->v * 8;
    if (!(c->plane = (u8 *)malloc((size_t)c->bw * c->bh))) goto done;
  }
  if (!(rgb = (u8 *)malloc((size_t)j->w * 3))) goto done;
  j->pos = start;
  todo = j->restart;
  for (my = 0; my < mcuy; my++) {
    u32 rows;
    for (mx = 0; mx < mcux; mx++) {
      if (j->restart && !todo) { jpeg_restart(j); todo = j->restart; }
      for (i = 0; i < j->nc; i++) {
        Comp *c = &j->c[i];
        int bx, by;
        for (by = 0; by < c->v; by++)
          for (bx = 0; bx < c->h; bx++) {
            int t, k;
            i32 *q = j->q[c->tq];
            memset(blk, 0, sizeof blk);
            t = decode(j, &j->dc[c->td]);
            c->pred += extend(getbits(j, t), t);
            blk[0] = c->pred * q[0];
            for (k = 1; k < 64; ) {
              int rs = decode(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
              if (!s) { if (r != 15) break; k += 16; continue; }
              k += r;
              if (k > 63) break;
              blk[zz[k]] = extend(getbits(j, s), s) * q[zz[k]];
              k++;
            }
            idct(blk, c->plane + (size_t)(by * 8) * c->bw + (mx * c->h + bx) * 8, c->bw);
          }
      }
      if (j->restart) todo--;
    }
    /* the strip's rows to RGB (chroma sampled up by repeating) */
    rows = (u32)j->vmax * 8;
    for (y = 0; y < rows && my * rows + y < j->h; y++) { jpeg_rgb(j, y, rgb); sc_row(sc, rgb, my * rows + y); }
  }
  ok = 1;
done:
  for (i = 0; i < j->nc; i++) { free(j->c[i].plane); j->c[i].plane = NULL; }
  free(rgb);
  return ok;
}
/* one block of a scan into its coefficients (as they are in the file, not dequantised) */
static void jpeg_block(Jpeg *j, Comp *c, i16 *b)
{
  int k, t;
  if (!j->progressive) {
    t = decode(j, &j->dc[c->td]);
    c->pred += extend(getbits(j, t), t);
    b[0] = (i16)c->pred;
    for (k = 1; k < 64; ) {
      int rs = decode(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
      if (!s) { if (r != 15) break; k += 16; continue; }
      k += r;
      if (k > 63) break;
      b[zz[k]] = (i16)extend(getbits(j, s), s);
      k++;
    }
  } else if (j->ss == 0) {                   /* DC: first, then a bit at a time */
    if (!j->ah) {
      t = decode(j, &j->dc[c->td]);
      c->pred += extend(getbits(j, t), t);
      b[0] = (i16)(c->pred * (1 << j->al));
    } else if (getbits(j, 1)) b[0] |= (i16)(1 << j->al);
  } else if (!j->ah) {                       /* AC, first */
    if (j->eobrun) { j->eobrun--; return; }
    for (k = j->ss; k <= j->se; ) {
      int rs = decode(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
      if (!s) {
        if (r < 15) { j->eobrun = (1u << r) - 1; if (r) j->eobrun += (u32)getbits(j, r); break; }
        k += 16; continue;
      }
      k += r;
      if (k > 63) break;
      b[zz[k]] = (i16)(extend(getbits(j, s), s) * (1 << j->al));
      k++;
    }
  } else {                                   /* AC, refining (as IJG's decode_mcu_AC_refine) */
    int p1 = 1 << j->al, m1 = -p1;
    k = j->ss;
    if (!j->eobrun) {
      for (; k <= j->se; k++) {
        int rs = decode(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
        if (s) s = getbits(j, 1) ? p1 : m1;
        else if (r != 15) { j->eobrun = 1u << r; if (r) j->eobrun += (u32)getbits(j, r); break; }
        do {
          i16 *z = &b[zz[k]];
          if (*z) { if (getbits(j, 1) && !(*z & p1)) *z = (i16)(*z + (*z >= 0 ? p1 : m1)); }
          else if (--r < 0) break;
          k++;
        } while (k <= j->se);
        if (s && k <= 63) b[zz[k]] = (i16)s;
      }
    }
    if (j->eobrun) {
      for (; k <= j->se; k++) {
        i16 *z = &b[zz[k]];
        if (*z && getbits(j, 1) && !(*z & p1)) *z = (i16)(*z + (*z >= 0 ? p1 : m1));
      }
      j->eobrun--;
    }
  }
}
/* progressive, or components in separate scans: every scan into the coefficients, then out a strip at a time */
static int jpeg_coefs(Jpeg *j, u32 start, Scaler *sc)
{
  u32 mcux, mcuy, mx, my, y, pos = start;
  int i, ok = 0;
  u8 *rgb = NULL;
  i32 blk[64];
  jpeg_geometry(j, &mcux, &mcuy);
  for (i = 0; i < j->nc; i++) {
    Comp *c = &j->c[i];
    c->cbw = mcux * (u32)c->h; c->cbh = mcuy * (u32)c->v;
    if (!(c->coef = (i16 *)calloc((size_t)c->cbw * c->cbh, 64 * sizeof(i16)))) goto done;
    c->bw = c->cbw * 8; c->bh = (u32)c->v * 8;
    if (!(c->plane = (u8 *)malloc((size_t)c->bw * c->bh))) goto done;
  }
  if (!(rgb = (u8 *)malloc((size_t)j->w * 3))) goto done;
  while (pos) {
    int todo = j->restart;
    for (i = 0; i < j->ns; i++) {
      Comp *c = &j->c[j->sc[i]];
      if ((!j->progressive || j->ss == 0) && !j->ah && !j->dc[c->td].ok) goto done;
      if ((!j->progressive || j->ss > 0) && !j->ac[c->ta].ok) goto done;
      c->pred = 0;
    }
    j->pos = pos; j->acc = 0; j->nbits = 0; j->marker = 0; j->eobrun = 0;
    if (j->ns == 1) {
      /* one component: its blocks in order, over its own size (not the MCUs') */
      Comp *c = &j->c[j->sc[0]];
      u32 bw = ((j->w * (u32)c->h + j->hmax - 1) / j->hmax + 7) / 8, bh = ((j->h * (u32)c->v + j->vmax - 1) / j->vmax + 7) / 8;
      for (my = 0; my < bh; my++)
        for (mx = 0; mx < bw; mx++) {
          if (j->restart && !todo) { jpeg_restart(j); todo = j->restart; }
          jpeg_block(j, c, c->coef + ((size_t)my * c->cbw + mx) * 64);
          if (j->restart) todo--;
        }
    } else {
      for (my = 0; my < mcuy; my++)
        for (mx = 0; mx < mcux; mx++) {
          if (j->restart && !todo) { jpeg_restart(j); todo = j->restart; }
          for (i = 0; i < j->ns; i++) {
            Comp *c = &j->c[j->sc[i]];
            int bx, by;
            for (by = 0; by < c->v; by++)
              for (bx = 0; bx < c->h; bx++)
                jpeg_block(j, c, c->coef + ((size_t)(my * c->v + by) * c->cbw + mx * c->h + bx) * 64);
          }
          if (j->restart) todo--;
        }
    }
    /* on to the next marker that isn't a restart, and the tables and scan after it */
    pos = j->pos;
    while (pos + 1 < j->n && !(j->d[pos] == 0xFF && j->d[pos + 1] && !(j->d[pos + 1] >= 0xD0 && j->d[pos + 1] <= 0xD7))) pos++;
    pos = jpeg_markers(j, pos);
  }
  for (my = 0; my < mcuy; my++) {
    u32 rows = (u32)j->vmax * 8;
    for (i = 0; i < j->nc; i++) {
      Comp *c = &j->c[i];
      i32 *q = j->q[c->tq];
      u32 bx, by;
      int k;
      for (by = 0; by < (u32)c->v; by++)
        for (bx = 0; bx < c->cbw; bx++) {
          const i16 *b = c->coef + ((size_t)(my * c->v + by) * c->cbw + bx) * 64;
          for (k = 0; k < 64; k++) blk[k] = b[k] * q[k];
          idct(blk, c->plane + (size_t)(by * 8) * c->bw + bx * 8, c->bw);
        }
    }
    for (y = 0; y < rows && my * rows + y < j->h; y++) { jpeg_rgb(j, y, rgb); sc_row(sc, rgb, my * rows + y); }
  }
  ok = 1;
done:
  for (i = 0; i < j->nc; i++) { free(j->c[i].plane); free(j->c[i].coef); j->c[i].plane = NULL; j->c[i].coef = NULL; }
  free(rgb);
  return ok;
}
/* picks the strip-at-a-time path when it can (baseline, every component in the first scan),
   else the one that keeps every coefficient */
static int jpeg_decode(const u8 *d, u32 n, Scaler *sc)
{
  Jpeg j;
  u32 start = jpeg_head(&j, d, n);
  if (!start) return 0;
  return !j.progressive && j.ns == j.nc ? jpeg_stream(&j, start, sc) : jpeg_coefs(&j, start, sc);
}

/* WP17: GIF, BMP and ILBM decoded whole by picfmt.c, then laid over white into the scaler row by row */
static int other_decode(const u8 *d, u32 n, Scaler *sc)
{
  Rgba im;
  u8 *row;
  i32 x, y;
  if (!picfmt_rgba(d, n, &im)) return 0;
  if ((u32)im.w != sc->sw || (u32)im.h != sc->sh || !(row = (u8 *)malloc((size_t)im.w * 3))) { rgba_free(&im); return 0; }
  for (y = 0; y < im.h; y++) {
    const u8 *s = im.px + (size_t)y * im.w * 4;
    for (x = 0; x < im.w; x++, s += 4) {
      int a = s[3];
      row[x * 3] = (u8)((s[0] * a + 255 * (255 - a)) / 255); row[x * 3 + 1] = (u8)((s[1] * a + 255 * (255 - a)) / 255); row[x * 3 + 2] = (u8)((s[2] * a + 255 * (255 - a)) / 255);
    }
    sc_row(sc, row, (u32)y);
  }
  free(row); rgba_free(&im);
  return 1;
}
/* ------------------------------------------------------------------ */
/* reads only the headers, so it is cheap enough to call when a document loads. For JPEG it
   accepts any frame type (even ones img_decode cannot decode); see img_jpeg_components */
int img_info(const u8 *d, u32 n, u32 *w, u32 *h)
{
  Png p;
  Jpeg j;
  *w = *h = 0;
  if (png_head(d, n, &p)) { *w = p.w; *h = p.h; return IMG_PNG; }
  if (n > 4 && d[0] == 0xFF && d[1] == 0xD8) {
    /* the frame header may come after the scan's tables: look for it by itself */
    u32 o = 2;
    while (o + 9 < n) {
      u32 m, len;
      if (d[o] != 0xFF) { o++; continue; }
      m = d[o + 1];
      if (m == 0xFF || m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { o += m == 0xFF ? 1 : 2; continue; }
      len = be16(d + o + 2);
      if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) { *h = be16(d + o + 5); *w = be16(d + o + 7); return *w && *h ? IMG_JPEG : IMG_NONE; }
      if (m == 0xDA || m == 0xD9) break;
      o += 2 + len;
    }
    (void)j;
  }
  return picfmt_info(d, n, w, h);     /* WP17: GIF, BMP, ILBM */
}
/* the component count is 0 when the frame header can't be found */
int img_jpeg_components(const u8 *d, u32 n, int *decodable)
{
  Jpeg j;
  u32 s = jpeg_head(&j, d, n);
  *decodable = s != 0;
  return j.nc;
}
/* decodes straight to the smaller of the shown and natural sizes, then repeats pixels if the
   picture is shown larger than it is. Sizes over 8000 pixels are refused. The caller frees */
u8 *img_decode(const u8 *d, u32 n, u32 ow, u32 oh)
{
  u32 w, h, tw, th, x, y;
  int t = img_info(d, n, &w, &h), ok;
  Scaler sc;
  u8 *small, *big;
  if (t == IMG_NONE || !ow || !oh || ow > 8000 || oh > 8000) return NULL;
  tw = ow < w ? ow : w; th = oh < h ? oh : h;
  if (!sc_init(&sc, w, h, tw, th)) { sc_free(&sc); return NULL; }
  ok = t == IMG_PNG ? png_decode(d, n, &sc) : t == IMG_JPEG ? jpeg_decode(d, n, &sc) : other_decode(d, n, &sc);
  if (!ok) { sc_free(&sc); return NULL; }
  small = sc_finish(&sc);
  if (tw == ow && th == oh) return small;
  /* shown bigger than it is: each pixel repeated */
  if (!(big = (u8 *)malloc((size_t)ow * oh * 3))) { free(small); return NULL; }
  for (y = 0; y < oh; y++)
    for (x = 0; x < ow; x++) memcpy(big + ((size_t)y * ow + x) * 3, small + ((size_t)(y * th / oh) * tw + x * tw / ow) * 3, 3);
  free(small);
  return big;
}

/* ------------------------------------------------------------------ */
/* outlines */
#define OL 48                    /* the picture looked at this many pixels each way */
/* works on a 48 x 48 thumbnail: per row, the leftmost and rightmost pixels that differ clearly
   from the background, joined into one polygon down the left side and back up the right. The
   units match Word's wrap polygons (21600 across); the caller frees *out */
int img_outline(const u8 *d, u32 n, i32 **out)
{
  u8 *rgb = img_decode(d, n, OL, OL);
  int lft[OL], rgt[OL], y, x, k, m = 0, bg[3], top = -1, bot = -1;
  i32 *pts;
  *out = NULL;
  if (!rgb) return 0;
  /* the background: the average of the four corners' colours */
  for (k = 0; k < 3; k++) bg[k] = (rgb[k] + rgb[(OL - 1) * 3 + k] + rgb[(OL - 1) * OL * 3 + k] + rgb[(OL * OL - 1) * 3 + k]) / 4;
  for (y = 0; y < OL; y++) {
    lft[y] = -1; rgt[y] = -1;
    for (x = 0; x < OL; x++) {
      const u8 *c = rgb + (y * OL + x) * 3;
      int dd = 0;
      for (k = 0; k < 3; k++) dd += c[k] > bg[k] ? c[k] - bg[k] : bg[k] - c[k];
      if (dd > 48) { if (lft[y] < 0) lft[y] = x; rgt[y] = x; }
    }
    if (lft[y] >= 0) { if (top < 0) top = y; bot = y; }
  }
  free(rgb);
  if (top < 0) return 0;
  /* down the left edge, up the right, a little outside what shows; rows with nothing take the row before's */
  if (!(pts = (i32 *)malloc((size_t)(bot - top + 1) * 4 * 2 * sizeof(i32)))) return 0;
  for (y = top; y <= bot; y++) {
    int l = lft[y] >= 0 ? lft[y] : lft[y - 1], yy = y == top ? y : y + 1;
    if (lft[y] < 0) lft[y] = lft[y - 1], rgt[y] = rgt[y - 1];
    if (y == top) { pts[2 * m] = (i32)l * 21600 / OL; pts[2 * m + 1] = (i32)y * 21600 / OL; m++; }
    pts[2 * m] = (i32)l * 21600 / OL; pts[2 * m + 1] = (i32)yy * 21600 / OL; m++;
  }
  pts[2 * m] = (i32)lft[bot] * 21600 / OL; pts[2 * m + 1] = (i32)(bot + 1) * 21600 / OL; m++;
  pts[2 * m] = (i32)(rgt[bot] + 1) * 21600 / OL; pts[2 * m + 1] = (i32)(bot + 1) * 21600 / OL; m++;
  for (y = bot; y >= top; y--) { pts[2 * m] = (i32)(rgt[y] + 1) * 21600 / OL; pts[2 * m + 1] = (i32)y * 21600 / OL; m++; }
  /* points in a straight line with their neighbours left out */
  {
    int j = 0;
    for (k = 0; k < m; k++) {
      i32 ax, ay, bx, by, cx, cy;
      if (j >= 2) {
        ax = pts[2 * (j - 2)]; ay = pts[2 * (j - 2) + 1]; bx = pts[2 * (j - 1)]; by = pts[2 * (j - 1) + 1]; cx = pts[2 * k]; cy = pts[2 * k + 1];
        if ((long)(bx - ax) * (cy - ay) == (long)(by - ay) * (cx - ax)) j--;
      }
      pts[2 * j] = pts[2 * k]; pts[2 * j + 1] = pts[2 * k + 1]; j++;
    }
    m = j;
  }
  if (m < 3) { free(pts); return 0; }
  *out = pts;
  return m;
}
