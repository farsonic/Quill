/* ttraster.c - see ttraster.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttraster.h"
#include "fonts.h"       /* sfnt_header */

/* big-endian reads, as everything in an sfnt file is */
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static i32 sbe16(const u8 *p) { i32 v = (i32)be16(p); return v >= 32768 ? v - 65536 : v; }

#define MAXTABLE (8UL * 1024 * 1024)
/* one table of the font into memory (malloc'd), *len its length; NULL if it is missing, runs
   past the file or is over MAXTABLE */
static u8 *table(FILE *fp, const u8 *dir, u32 ntab, const char *tag, u32 *len, long fsize)
{
  u32 i;
  for (i = 0; i < ntab; i++) {
    const u8 *e = dir + 16 * i;
    if (!memcmp(e, tag, 4)) {
      u32 off = be32(e + 8), n = be32(e + 12);
      u8 *b;
      if (off > (u32)fsize || n > (u32)fsize - off || n > MAXTABLE) return NULL;
      if (!(b = (u8 *)malloc(n ? n : 1))) return NULL;
      if (fseek(fp, (long)off, SEEK_SET) || fread(b, 1, n, fp) != n) { free(b); return NULL; }
      *len = n;
      return b;
    }
  }
  return NULL;
}
/* ------------------------------------------------------------------ */
/* CFF: INDEXes (a count, an offset size, count + 1 offsets from 1, the data) and DICTs */
/* the INDEX at off: its count, and where its data's offsets start; 0 if it runs past the table
   (CFF2's count is 32 bits, CFF's 16) */
static int cff_index(const TTRaster *r, u32 off, u32 *count, u32 *end)
{
  u32 n, os, last, i, cs = r->cff2 ? 4 : 2;
  if (off + cs > r->lcff) return 0;
  n = cs == 4 ? be32(r->cff + off) : be16(r->cff + off);
  if (!n) { *count = 0; *end = off + cs; return 1; }
  if (n > 1000000 || off + cs + 1 > r->lcff) return 0;
  os = r->cff[off + cs];
  if (os < 1 || os > 4 || off + cs + 1 + (n + 1) * os > r->lcff) return 0;
  for (i = 0, last = 0; i < os; i++) last = (last << 8) | r->cff[off + cs + 1 + n * os + i];
  *count = n;
  *end = off + cs + 1 + (n + 1) * os - 1 + last;
  return *end <= r->lcff;
}
/* entry i of the INDEX at off: where it starts and how long it is */
static int cff_entry(const TTRaster *r, u32 off, u32 i, u32 *at, u32 *len)
{
  u32 n, os, a = 0, b = 0, k, data, cs = r->cff2 ? 4 : 2;
  if (off + cs + 1 > r->lcff) return 0;
  n = cs == 4 ? be32(r->cff + off) : be16(r->cff + off);
  os = r->cff[off + cs];
  if (i >= n || os < 1 || os > 4 || off + cs + 1 + (n + 1) * os > r->lcff) return 0;
  for (k = 0; k < os; k++) { a = (a << 8) | r->cff[off + cs + 1 + i * os + k]; b = (b << 8) | r->cff[off + cs + 1 + (i + 1) * os + k]; }
  data = off + cs + 1 + (n + 1) * os - 1;
  if (b < a || data + b > r->lcff) return 0;
  *at = data + a; *len = b - a;
  return 1;
}
/* a DICT's operators: the operands (integers; reals as 0) before op (12 x as 1200 + x) */
static int cff_dict(const TTRaster *r, u32 at, u32 len, int op, i32 *v, int nv)
{
  const u8 *p = r->cff + at, *end = p + len;
  i32 st[48];
  int n = 0, i;
  if (at + len > r->lcff) return 0;
  while (p < end) {
    int b = *p++;
    if (b >= 32 && b <= 246) { if (n < 48) st[n++] = b - 139; }
    else if (b >= 247 && b <= 250) { if (p >= end) return 0; if (n < 48) st[n++] = (b - 247) * 256 + *p + 108; p++; }
    else if (b >= 251 && b <= 254) { if (p >= end) return 0; if (n < 48) st[n++] = -(b - 251) * 256 - *p - 108; p++; }
    else if (b == 28) { if (p + 2 > end) return 0; if (n < 48) st[n++] = sbe16(p); p += 2; }
    else if (b == 29) { if (p + 4 > end) return 0; if (n < 48) st[n++] = (i32)be32(p); p += 4; }
    else if (b == 30) { while (p < end && (*p & 15) != 15 && (*p >> 4) != 15) p++; if (p < end) p++; if (n < 48) st[n++] = 0; }
    else {
      int o = b;
      if (b == 12) { if (p >= end) return 0; o = 1200 + *p++; }
      if (o == op) { for (i = 0; i < nv && i < n; i++) v[i] = st[i]; return n; }
      n = 0;
    }
  }
  return 0;
}
/* CFF2 (variable fonts): the header, then the Top DICT, the global Subrs, the font DICTs; and the
   variation store, for how many deltas each blend has (of which the default design uses none) */
static int cff2_open(TTRaster *r)
{
  u32 hs, tl, top_at, n, end, fda, k, at, len;
  i32 v[2];
  if (r->lcff < 5) return 0;
  hs = r->cff[2]; tl = be16(r->cff + 3);
  top_at = hs;
  if (top_at + tl > r->lcff) return 0;
  r->gsub_off = top_at + tl;
  if (!cff_index(r, r->gsub_off, &n, &end)) return 0;
  if (!cff_dict(r, top_at, tl, 17, v, 1) || v[0] <= 0) return 0;
  r->cs_off = (u32)v[0];
  if (!cff_index(r, r->cs_off, &n, &end) || n < r->nglyphs) return 0;
  if (cff_dict(r, top_at, tl, 24, v, 1) && v[0] > 0 && (u32)v[0] + 2 + 8 <= r->lcff) {
    /* (a 16-bit length, then an ItemVariationStore: format, region list, data offsets) */
    u32 vs = (u32)v[0] + 2, nd = be16(r->cff + vs + 6), d;
    for (d = 0; d < nd && d < 16 && vs + 8 + 4 * d + 4 <= r->lcff; d++) {
      u32 o = vs + be32(r->cff + vs + 8 + 4 * d);
      r->vregions[d] = o + 6 <= r->lcff ? (int)be16(r->cff + o + 4) : 0;
    }
    r->nvdata = (int)d;
  }
  if (!cff_dict(r, top_at, tl, 1236, v, 1) || v[0] <= 0 || !cff_index(r, (u32)v[0], &n, &end)) return 0;
  fda = (u32)v[0];
  for (k = 0; k < n && k < 64; k++) {
    i32 pv[2];
    r->fd_lsub[k] = 0; r->fd_vsindex[k] = 0;
    if (cff_entry(r, fda, k, &at, &len) && cff_dict(r, at, len, 18, pv, 2) == 2 && pv[1] > 0 && (u32)pv[1] + (u32)pv[0] <= r->lcff) {
      if (cff_dict(r, (u32)pv[1], (u32)pv[0], 19, v, 1) && v[0] > 0) r->fd_lsub[k] = (u32)pv[1] + (u32)v[0];
      if (cff_dict(r, (u32)pv[1], (u32)pv[0], 22, v, 1)) r->fd_vsindex[k] = v[0];
    }
  }
  r->nfd = (int)k;
  if (cff_dict(r, top_at, tl, 1237, v, 1) && v[0] > 0 && (u32)v[0] < r->lcff) r->fdsel_off = (u32)v[0];
  else r->fdsel_off = 0;
  r->lsub_off = r->nfd ? r->fd_lsub[0] : 0;
  return 1;
}
/* finds the parts of a CFF table that drawing needs: the CharStrings, the global Subrs and the
   local Subrs (one set, or one per font DICT for a CID-keyed font). Hands CFF2 to cff2_open.
   0 for anything else, or charstrings other than Type 2 */
static int cff_open(TTRaster *r)
{
  u32 hs, n, top_at, top_len, end, v2;
  i32 v[2];
  if (r->lcff >= 1 && r->cff[0] == 2) { r->cff2 = 1; return cff2_open(r); }
  if (r->lcff < 4 || r->cff[0] != 1) return 0;
  hs = r->cff[2];
  /* the Name INDEX, the Top DICT INDEX (its first DICT), the String INDEX, the global Subrs */
  if (!cff_index(r, hs, &n, &end) || !cff_entry(r, end, 0, &top_at, &top_len)) return 0;
  if (!cff_index(r, end, &n, &v2) || !cff_index(r, v2, &n, &end)) return 0;
  r->gsub_off = end;
  if (!cff_index(r, end, &n, &v2)) return 0;
  if (cff_dict(r, top_at, top_len, 1206, v, 1) && v[0] != 2) return 0;          /* charstrings of type 2 only */
  if (!cff_dict(r, top_at, top_len, 17, v, 1) || v[0] <= 0) return 0;
  r->cs_off = (u32)v[0];
  if (!cff_index(r, r->cs_off, &n, &end) || n < r->nglyphs) return 0;
  if (cff_dict(r, top_at, top_len, 1236, v, 1) && v[0] > 0) {
    /* CID-keyed: a font DICT for each group of glyphs, each with its own Private DICT */
    u32 fda = (u32)v[0], nfd, k, at, len;
    i32 sel[1];
    if (!cff_dict(r, top_at, top_len, 1237, sel, 1) || sel[0] <= 0 || !cff_index(r, fda, &nfd, &end)) return 0;
    r->fdsel_off = (u32)sel[0];
    if (r->fdsel_off >= r->lcff) return 0;
    for (k = 0; k < nfd && k < 64; k++) {
      i32 pv[2];
      r->fd_lsub[k] = 0;
      if (cff_entry(r, fda, k, &at, &len) && cff_dict(r, at, len, 18, pv, 2) == 2 && pv[1] > 0 &&
          cff_dict(r, (u32)pv[1], (u32)pv[0], 19, v, 1) && v[0] > 0)
        r->fd_lsub[k] = (u32)pv[1] + (u32)v[0];
    }
    r->nfd = (int)k;
  } else {
    i32 pv[2];
    if (cff_dict(r, top_at, top_len, 18, pv, 2) == 2 && pv[1] > 0 && (u32)pv[1] + (u32)pv[0] <= r->lcff &&
        cff_dict(r, (u32)pv[1], (u32)pv[0], 19, v, 1) && v[0] > 0)
      r->lsub_off = (u32)pv[1] + (u32)v[0];
  }
  return 1;
}

/* opens a face for drawing: 1 if it can be, with the file left open in r until ttr_close (big
   TrueType fonts read their glyphs from it). On failure everything is released and r is empty */
int ttr_open(TTRaster *r, const char *path, int index)
{
  FILE *fp;
  u8 hdr[12], *dir = NULL, *head = NULL, *maxp = NULL;
  u32 ntab, lh = 0, lm = 0;
  long fsize;
  int ok = 0, cff = 0;
  memset(r, 0, sizeof *r);
  if (!(fp = fopen(path, "rb"))) return 0;
  fseek(fp, 0, SEEK_END); fsize = ftell(fp); fseek(fp, 0, SEEK_SET);
  if (!sfnt_header(fp, index, hdr, &cff, fsize)) goto done;
  ntab = be16(hdr + 4);
  if (!ntab || ntab > 64 || !(dir = (u8 *)malloc(16 * ntab)) || fread(dir, 16, ntab, fp) != ntab) goto done;
  head = table(fp, dir, ntab, "head", &lh, fsize);
  maxp = table(fp, dir, ntab, "maxp", &lm, fsize);
  if (!head || lh < 54 || !maxp || lm < 6) goto done;
  r->upm = (u16)be16(head + 18);
  r->longloca = sbe16(head + 50) != 0;
  r->nglyphs = (u16)be16(maxp + 4);
  if (r->upm < 16) goto done;
  if (cff) {
    if (!(r->cff = table(fp, dir, ntab, "CFF ", &r->lcff, fsize)) && !(r->cff = table(fp, dir, ntab, "CFF2", &r->lcff, fsize))) goto done;
    if (!cff_open(r)) goto done;
  } else {
    u32 i;
    r->loca = table(fp, dir, ntab, "loca", &r->lloca, fsize);
    for (i = 0; i < ntab && memcmp(dir + 16 * i, "glyf", 4); i++) ;
    if (i < ntab) { r->glyfoff = be32(dir + 16 * i + 8); r->lglyf = be32(dir + 16 * i + 12); }
    if (i == ntab || r->glyfoff > (u32)fsize || r->lglyf > (u32)fsize - r->glyfoff) goto done;
    if (!r->loca || r->lloca < (u32)(r->nglyphs + 1) * (r->longloca ? 4 : 2)) goto done;
    /* a font of usual size: its glyphs read at once (a seek and a read for each is slow on a floppy
       or an emulated disk); a big one (Asian scripts) glyph by glyph as they are wanted */
    if (r->lglyf <= 1536UL * 1024 && (r->glyf = (u8 *)malloc(r->lglyf ? r->lglyf : 1)) &&
        (fseek(fp, (long)r->glyfoff, SEEK_SET) || fread(r->glyf, 1, r->lglyf, fp) != r->lglyf)) { free(r->glyf); r->glyf = NULL; }
  }
  ok = 1;
done:
  free(dir); free(head); free(maxp);
  r->fp = fp;
  if (!ok) ttr_close(r);
  return ok;
}
/* closes the file and frees the tables and work buffers; safe on an empty (zeroed) raster */
void ttr_close(TTRaster *r)
{
  int i;
  if (r->fp) fclose((FILE *)r->fp);
  free(r->loca); free(r->glyf); free(r->cff); free(r->xs); free(r->ys); free(r->on);
  free(r->lines); free(r->ends); free(r->cross);
  for (i = 0; i < 10; i++) free(r->gbuf[i]);
  memset(r, 0, sizeof *r);
}
/* a glyph's bytes, read into the buffer for this level of a composite */
static const u8 *glyph_bytes(TTRaster *r, u32 off, u32 len, int depth)
{
  if (r->glyf) return off + len <= r->lglyf ? r->glyf + off : NULL;     /* (the whole table in memory) */
  if (len > 65536 || depth < 0 || depth > 9) return NULL;
  if (len > r->gcap[depth]) {
    u8 *nb = (u8 *)realloc(r->gbuf[depth], len);
    if (!nb) return NULL;
    r->gbuf[depth] = nb; r->gcap[depth] = len;
  }
  if (fseek((FILE *)r->fp, (long)(r->glyfoff + off), SEEK_SET) || fread(r->gbuf[depth], 1, len, (FILE *)r->fp) != len) return NULL;
  return r->gbuf[depth];
}

/* ------------------------------------------------------------------ */
/* outlines: the points of a glyph (and of the glyphs a composite one is made of), in font units */
typedef struct Contours { u32 *ends; u32 n, cap; } Contours;
/* room for more points in the work arrays (kept from glyph to glyph); 0 if out of memory or a
   glyph would pass 20000 points, which only a damaged one does */
static int grow(TTRaster *r, u32 more)
{
  u32 need = r->npts + more, nc;
  i32 *nx, *ny;
  u8 *no;
  if (need <= r->cappts) return 1;
  if (need > 20000) return 0;
  nc = need + need / 2 + 64;
  nx = (i32 *)realloc(r->xs, nc * sizeof(i32)); if (!nx) return 0; r->xs = nx;
  ny = (i32 *)realloc(r->ys, nc * sizeof(i32)); if (!ny) return 0; r->ys = ny;
  no = (u8 *)realloc(r->on, nc); if (!no) return 0; r->on = no;
  r->cappts = nc;
  return 1;
}
/* records where a contour ends (an index into the points); at most 4096 contours a glyph */
static int add_end(Contours *c, u32 end)
{
  if (c->n >= c->cap) {
    u32 nc = c->cap ? c->cap * 2 : 16, *ne;
    if (nc > 4096 || !(ne = (u32 *)realloc(c->ends, nc * sizeof(u32)))) return 0;
    c->ends = ne; c->cap = nc;
  }
  c->ends[c->n++] = end;
  return 1;
}
/* glyph g's points, transformed by (a b; c d) (2.14 fixed) and moved by (dx, dy) */
static int outline(TTRaster *r, Contours *cs, u16 g, i32 a, i32 b, i32 c, i32 d, i32 dx, i32 dy, int depth)
{
  u32 o0, o1, len;
  const u8 *p, *end;
  i32 nc;
  if (g >= r->nglyphs || depth > 8) return 0;
  if (r->longloca) { o0 = be32(r->loca + 4 * g); o1 = be32(r->loca + 4 * g + 4); }
  else { o0 = 2 * be16(r->loca + 2 * g); o1 = 2 * be16(r->loca + 2 * g + 2); }
  if (o1 <= o0) return 1;                                     /* nothing to draw */
  if (o1 > r->lglyf) return 0;
  len = o1 - o0;
  if (len < 10 || !(p = glyph_bytes(r, o0, len, depth))) return 0;
  end = p + len;
  nc = sbe16(p);
  if (nc >= 0) {
    u32 i, n, base = r->npts, ninstr, k;
    const u8 *q = p + 10;
    i32 v;
    if (q + 2 * nc + 2 > end) return 0;
    n = nc ? be16(q + 2 * (nc - 1)) + 1 : 0;
    if (!grow(r, n)) return 0;
    for (i = 0; i < (u32)nc; i++) {
      u32 e = be16(q + 2 * i);
      if (e >= n || (i && e < be16(q + 2 * (i - 1)))) return 0;
      if (!add_end(cs, base + e)) return 0;
    }
    q += 2 * nc;
    ninstr = be16(q); q += 2 + ninstr;
    if (q > end) return 0;
    /* flags (with repeats), then x and y, each short (1 byte and a sign) or long, or the same */
    for (i = 0; i < n; ) {
      u8 f;
      if (q >= end) return 0;
      f = *q++;
      r->on[base + i++] = f;
      if (f & 8) {
        if (q >= end) return 0;
        for (k = *q++; k && i < n; k--) r->on[base + i++] = f;
      }
    }
    for (i = 0, v = 0; i < n; i++) {
      u8 f = r->on[base + i];
      if (f & 2) { if (q >= end) return 0; v += f & 16 ? *q : -(i32)*q; q++; }
      else if (!(f & 16)) { if (q + 2 > end) return 0; v += sbe16(q); q += 2; }
      r->xs[base + i] = v;
    }
    for (i = 0, v = 0; i < n; i++) {
      u8 f = r->on[base + i];
      if (f & 4) { if (q >= end) return 0; v += f & 32 ? *q : -(i32)*q; q++; }
      else if (!(f & 32)) { if (q + 2 > end) return 0; v += sbe16(q); q += 2; }
      r->ys[base + i] = v;
    }
    for (i = 0; i < n; i++) {
      i32 x = r->xs[base + i], y = r->ys[base + i];
      r->xs[base + i] = ((a * x + c * y) >> 14) + dx;
      r->ys[base + i] = ((b * x + d * y) >> 14) + dy;
      r->on[base + i] &= 1;
    }
    r->npts += n;
    return 1;
  } else {
    /* a composite: other glyphs, each moved (and maybe scaled) */
    const u8 *q = p + 10;
    u32 flags;
    do {
      i32 e1, e2, ta = 1 << 14, tb = 0, tc = 0, td = 1 << 14;
      u16 sub;
      if (q + 4 > end) return 0;
      flags = be16(q); sub = (u16)be16(q + 2); q += 4;
      if (flags & 1) { if (q + 4 > end) return 0; e1 = sbe16(q); e2 = sbe16(q + 2); q += 4; }
      else { if (q + 2 > end) return 0; e1 = (i32)(signed char)q[0]; e2 = (i32)(signed char)q[1]; q += 2; }
      if (!(flags & 2)) e1 = e2 = 0;                          /* matched points: not followed */
      if (flags & 8) { if (q + 2 > end) return 0; ta = td = sbe16(q); q += 2; }
      else if (flags & 0x40) { if (q + 4 > end) return 0; ta = sbe16(q); td = sbe16(q + 2); q += 4; }
      else if (flags & 0x80) { if (q + 8 > end) return 0; ta = sbe16(q); tb = sbe16(q + 2); tc = sbe16(q + 4); td = sbe16(q + 6); q += 8; }
      /* the part's transform, then the composite's own */
      {
        i32 na = (a * ta + c * tb) >> 14, nb = (b * ta + d * tb) >> 14;
        i32 ncc = (a * tc + c * td) >> 14, nd = (b * tc + d * td) >> 14;
        i32 ndx = ((a * e1 + c * e2) >> 14) + dx, ndy = ((b * e1 + d * e2) >> 14) + dy;
        if (!outline(r, cs, sub, na, nb, ncc, nd, ndx, ndy, depth + 1)) return 0;
      }
    } while (flags & 0x20);
    return 1;
  }
}

/* ------------------------------------------------------------------ */
/* filling: the outline as lines (26.6 pixels, y down), crossed by 4 sample rows a pixel */
typedef struct Line { i32 x0, y0, x1, y1; int curve; } Line;     /* curve: a piece of a curve (never a stem's edge) */
typedef struct Lines { Line *l; u32 n, cap; } Lines;
/* adds a line (26.6 pixels) to the outline, growing the array; zero-length ones are left out.
   0 when out of memory or past 200000 lines */
static int add_seg(Lines *ls, i32 x0, i32 y0, i32 x1, i32 y1, int curve)
{
  if (y0 == y1 && x0 == x1) return 1;                  /* (horizontal ones kept: a stem's edges; the filling passes them by) */
  if (ls->n >= ls->cap) {
    u32 nc = ls->cap ? ls->cap * 2 : 128;
    Line *nl;
    if (nc > 200000 || !(nl = (Line *)realloc(ls->l, nc * sizeof(Line)))) return 0;
    ls->l = nl; ls->cap = nc;
  }
  ls->l[ls->n].x0 = x0; ls->l[ls->n].y0 = y0; ls->l[ls->n].x1 = x1; ls->l[ls->n].y1 = y1; ls->l[ls->n].curve = curve;
  ls->n++;
  return 1;
}
/* a straight edge of the outline, one stem detection may use */
static int add_line(Lines *ls, i32 x0, i32 y0, i32 x1, i32 y1) { return add_seg(ls, x0, y0, x1, y1, 0); }
/* the absolute value of an i32 */
static i32 iabs(i32 v) { return v < 0 ? -v : v; }
/* whole pixels below v (26.6), rounding towards minus infinity as C's division doesn't */
static i32 floor64(i32 v) { return v >= 0 ? v / 64 : -((-v + 63) / 64); }
/* a quadratic curve as lines: more of them the more it bends */
static int add_curve(Lines *ls, i32 x0, i32 y0, i32 cx, i32 cy, i32 x1, i32 y1)
{
  i32 dd = iabs(x0 - 2 * cx + x1) + iabs(y0 - 2 * cy + y1), n = 1, i, px = x0, py = y0;
  while (n < 16 && dd > 24 * n * n) n++;
  for (i = 1; i <= n; i++) {
    /* B(t) = (1-t)^2 p0 + 2t(1-t) c + t^2 p1, t = i/n */
    i32 u = n - i, nx, ny, nn = n * n;
    nx = (u * u * x0 + 2 * i * u * cx + i * i * x1) / nn;
    ny = (u * u * y0 + 2 * i * u * cy + i * i * y1) / nn;
    if (!add_seg(ls, px, py, nx, ny, 1)) return 0;
    px = nx; py = ny;
  }
  return 1;
}
/* a line's top (its smaller y, since y runs down), and qsort's order by it */
static i32 top(const Line *l) { return l->y0 < l->y1 ? l->y0 : l->y1; }
static int by_top(const void *a, const void *b)
{
  i32 x = top((const Line *)a), y = top((const Line *)b);
  return x < y ? -1 : x > y;
}
/* ------------------------------------------------------------------ */
/* CFF glyphs: Type 2 charstrings run into lines (cubic curves flattened), in 26.6 pixels */
typedef struct CS {
  TTRaster *r;
  Lines *ls;
  i32 ppem64, mult;
  i32 st[513]; int n;                 /* the stack: 16.16 fixed point, font units (CFF2's can be 513 deep) */
  int vsindex;                        /* CFF2: which variation data the blends use */
  i32 x, y, sx, sy;                   /* the pen, and where its contour began (16.16) */
  int open, nstems, width_done, depth;
  u32 lsub;                           /* this glyph's local Subrs INDEX (0: none) */
} CS;
/* 16.16 font units to 26.6 pixels (y downwards for the y's): by mult (ppem64 / upm, 16.16), in
   32 bits - the whole units, then the fraction's top 12 bits (no 64-bit sums on a 68k) */
static i32 cs_px(const CS *c, i32 v)
{
  i32 ip = v >= 0 ? v / 65536 : -((-v + 65535) / 65536), fp = v - ip * 65536;
  if (c->mult > 524287 || ip > 4095 || ip < -4095)                /* (big sizes: 64 bits) */
    return (i32)(((long long)v * c->ppem64) / ((long long)c->r->upm << 16));
  return (ip * c->mult + ((fp >> 4) * c->mult) / 4096) / 65536;
}
/* a straight line from the pen to (x, y), 16.16 font units; the pen moves there */
static int cs_line(CS *c, i32 x, i32 y)
{
  int ok = add_line(c->ls, cs_px(c, c->x), -cs_px(c, c->y), cs_px(c, x), -cs_px(c, y));
  c->x = x; c->y = y;
  return ok;
}
/* a cubic Bezier from the pen through two control points to (x3, y3), flattened into up to 16
   lines depending on how much it bends (measured in pixels, so small sizes get few) */
static int cs_curve(CS *c, i32 x1, i32 y1, i32 x2, i32 y2, i32 x3, i32 y3)
{
  i32 p0x = cs_px(c, c->x), p0y = -cs_px(c, c->y), p1x = cs_px(c, x1), p1y = -cs_px(c, y1);
  i32 p2x = cs_px(c, x2), p2y = -cs_px(c, y2), p3x = cs_px(c, x3), p3y = -cs_px(c, y3);
  i32 dd = iabs(p0x - 2 * p1x + p2x) + iabs(p0y - 2 * p1y + p2y) + iabs(p1x - 2 * p2x + p3x) + iabs(p1y - 2 * p2y + p3y);
  i32 n = 1, i, px = p0x, py = p0y;
  while (n < 16 && dd > 24 * n * n) n++;
  for (i = 1; i <= n; i++) {
    i32 u = n - i, n3 = n * n * n, nx, ny;
    nx = (u * u * u * p0x + 3 * u * u * i * p1x + 3 * u * i * i * p2x + i * i * i * p3x) / n3;
    ny = (u * u * u * p0y + 3 * u * u * i * p1y + 3 * u * i * i * p2y + i * i * i * p3y) / n3;
    if (!add_seg(c->ls, px, py, nx, ny, 1)) return 0;
    px = nx; py = ny;
  }
  c->x = x3; c->y = y3;
  return 1;
}
/* a contour closed: a line back to its start, the pen staying where it was (moves are from it) */
static int cs_close(CS *c)
{
  if (c->open && (c->x != c->sx || c->y != c->sy) &&
      !add_line(c->ls, cs_px(c, c->x), -cs_px(c, c->y), cs_px(c, c->sx), -cs_px(c, c->sy))) return 0;
  c->open = 0;
  return 1;
}
/* moveto: closes the contour being drawn and starts another dx, dy from the pen */
static int cs_move(CS *c, i32 dx, i32 dy)
{
  if (!cs_close(c)) return 0;
  c->x += dx; c->y += dy; c->sx = c->x; c->sy = c->y; c->open = 1;
  return 1;
}
/* the Type 2 subroutine bias for an INDEX of n entries (the spec's fixed thresholds) */
static u32 cs_bias(u32 n) { return n < 1240 ? 107 : n < 33900 ? 1131 : 32768; }
/* the charstring at at (len bytes); 2 at endchar, 1 at its end or a return, 0 if damaged */
static int cs_run(CS *c, u32 at, u32 len)
{
  const u8 *p = c->r->cff + at, *end = p + len;
  i32 *s = c->st;
  if (++c->depth > 10) return 0;
  while (p < end) {
    int b = *p++, i, k;
    if (b >= 32 || b == 28) {
      i32 v;
      if (b == 28) { if (p + 2 > end) return 0; v = sbe16(p) * 65536; p += 2; }
      else if (b <= 246) v = (b - 139) * 65536;
      else if (b <= 250) { if (p >= end) return 0; v = ((b - 247) * 256 + *p++ + 108) * 65536; }
      else if (b <= 254) { if (p >= end) return 0; v = (-(b - 251) * 256 - *p++ - 108) * 65536; }
      else { if (p + 4 > end) return 0; v = (i32)be32(p); p += 4; }
      if (c->n >= (c->r->cff2 ? 513 : 48)) return 0;
      s[c->n++] = v;
      continue;
    }
    /* the first operator that clears the stack may have the glyph's width first: dropped */
#define WIDTH(even) do { if (!c->width_done) { c->width_done = 1; if ((c->n & 1) != (even)) { memmove(s, s + 1, (size_t)--c->n * sizeof(i32)); } } } while (0)
    switch (b) {
    case 1: case 3: case 18: case 23:                         /* stems: counted for the masks */
      WIDTH(0); c->nstems += c->n / 2; c->n = 0; break;
    case 19: case 20:                                         /* hintmask, cntrmask: skipped */
      WIDTH(0); c->nstems += c->n / 2; c->n = 0;
      p += (c->nstems + 7) / 8;
      break;
    case 21: WIDTH(0); if (c->n < 2 || !cs_move(c, s[c->n - 2], s[c->n - 1])) return 0; c->n = 0; break;
    case 22: WIDTH(1); if (c->n < 1 || !cs_move(c, s[c->n - 1], 0)) return 0; c->n = 0; break;
    case 4: WIDTH(1); if (c->n < 1 || !cs_move(c, 0, s[c->n - 1])) return 0; c->n = 0; break;
    case 5:                                                   /* rlineto */
      for (i = 0; i + 1 < c->n; i += 2) if (!cs_line(c, c->x + s[i], c->y + s[i + 1])) return 0;
      c->n = 0; break;
    case 6: case 7:                                           /* hlineto, vlineto: alternating */
      for (i = 0, k = b == 6; i < c->n; i++, k = !k)
        if (!(k ? cs_line(c, c->x + s[i], c->y) : cs_line(c, c->x, c->y + s[i]))) return 0;
      c->n = 0; break;
    case 8: case 24:                                          /* rrcurveto, rcurveline */
      for (i = 0; i + 5 < c->n; i += 6) {
        i32 x1 = c->x + s[i], y1 = c->y + s[i + 1], x2 = x1 + s[i + 2], y2 = y1 + s[i + 3];
        if (!cs_curve(c, x1, y1, x2, y2, x2 + s[i + 4], y2 + s[i + 5])) return 0;
      }
      if (b == 24 && i + 1 < c->n && !cs_line(c, c->x + s[i], c->y + s[i + 1])) return 0;
      c->n = 0; break;
    case 25:                                                  /* rlinecurve */
      for (i = 0; i + 7 < c->n; i += 2) if (!cs_line(c, c->x + s[i], c->y + s[i + 1])) return 0;
      if (i + 5 < c->n) {
        i32 x1 = c->x + s[i], y1 = c->y + s[i + 1], x2 = x1 + s[i + 2], y2 = y1 + s[i + 3];
        if (!cs_curve(c, x1, y1, x2, y2, x2 + s[i + 4], y2 + s[i + 5])) return 0;
      }
      c->n = 0; break;
    case 26: case 27: {                                       /* vvcurveto, hhcurveto */
      i32 d1 = 0;
      i = 0;
      if (c->n & 1) d1 = s[i++];
      for (; i + 3 < c->n; i += 4, d1 = 0) {
        i32 x1, y1, x2, y2;
        if (b == 26) { x1 = c->x + d1; y1 = c->y + s[i]; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; if (!cs_curve(c, x1, y1, x2, y2, x2, y2 + s[i + 3])) return 0; }
        else { x1 = c->x + s[i]; y1 = c->y + d1; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; if (!cs_curve(c, x1, y1, x2, y2, x2 + s[i + 3], y2)) return 0; }
      }
      c->n = 0; break; }
    case 30: case 31: {                                       /* vhcurveto, hvcurveto: alternating */
      int h = b == 31;
      for (i = 0; i + 3 < c->n; i += 4, h = !h) {
        i32 last = (i + 5 == c->n) ? s[i + 4] : 0, x1, y1, x2, y2;
        if (h) { x1 = c->x + s[i]; y1 = c->y; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; if (!cs_curve(c, x1, y1, x2, y2, x2 + last, y2 + s[i + 3])) return 0; }
        else { x1 = c->x; y1 = c->y + s[i]; x2 = x1 + s[i + 1]; y2 = y1 + s[i + 2]; if (!cs_curve(c, x1, y1, x2, y2, x2 + s[i + 3], y2 + last)) return 0; }
      }
      c->n = 0; break; }
    case 10: case 29: {                                       /* callsubr, callgsubr */
      u32 idx = b == 10 ? c->lsub : c->r->gsub_off, cnt, e, sat, slen;
      i32 num;
      int rr;
      if (!c->n || (b == 10 && !idx) || !cff_index(c->r, idx, &cnt, &e)) return 0;
      num = s[--c->n] / 65536 + (i32)cs_bias(cnt);
      if (num < 0 || !cff_entry(c->r, idx, (u32)num, &sat, &slen)) return 0;
      if (!(rr = cs_run(c, sat, slen))) return 0;
      c->depth--;
      if (rr == 2) return 2;
      break; }
    case 11: return 1;                                        /* return */
    case 15: if (c->n) c->vsindex = s[c->n - 1] / 65536; c->n = 0; break;          /* vsindex (CFF2) */
    case 16: {                                                /* blend (CFF2): the default design's values kept */
      i32 nb, k = c->vsindex >= 0 && c->vsindex < c->r->nvdata ? c->r->vregions[c->vsindex] : 0;
      if (!c->n) return 0;
      nb = s[--c->n] / 65536;
      if (nb < 0 || nb * (k + 1) > c->n) return 0;
      c->n -= nb * k;
      break; }
    case 14: WIDTH(0); c->n = 0; return cs_close(c) ? 2 : 0;  /* endchar (an accent made of two glyphs isn't) */
    case 12: {
      int e2;
      if (p >= end) return 0;
      e2 = *p++;
      if (e2 == 35 && c->n >= 12) {                           /* flex: two curves */
        i32 x1 = c->x + s[0], y1 = c->y + s[1], x2 = x1 + s[2], y2 = y1 + s[3], x3 = x2 + s[4], y3 = y2 + s[5];
        i32 x4 = x3 + s[6], y4 = y3 + s[7], x5 = x4 + s[8], y5 = y4 + s[9], x6 = x5 + s[10], y6 = y5 + s[11];
        if (!cs_curve(c, x1, y1, x2, y2, x3, y3) || !cs_curve(c, x4, y4, x5, y5, x6, y6)) return 0;
      } else if (e2 == 34 && c->n >= 7) {                     /* hflex */
        i32 y0 = c->y, x1 = c->x + s[0], x2 = x1 + s[1], y2 = y0 + s[2], x3 = x2 + s[3];
        i32 x4 = x3 + s[4], x5 = x4 + s[5], x6 = x5 + s[6];
        if (!cs_curve(c, x1, y0, x2, y2, x3, y2) || !cs_curve(c, x4, y2, x5, y0, x6, y0)) return 0;
      } else if (e2 == 36 && c->n >= 9) {                     /* hflex1 */
        i32 y0 = c->y, x1 = c->x + s[0], y1 = y0 + s[1], x2 = x1 + s[2], y2 = y1 + s[3], x3 = x2 + s[4];
        i32 x4 = x3 + s[5], x5 = x4 + s[6], y5 = y2 + s[7], x6 = x5 + s[8];
        if (!cs_curve(c, x1, y1, x2, y2, x3, y2) || !cs_curve(c, x4, y2, x5, y5, x6, y0)) return 0;
      } else if (e2 == 37 && c->n >= 11) {                    /* flex1: the last point by the larger move */
        i32 x0 = c->x, y0 = c->y, x1 = x0 + s[0], y1 = y0 + s[1], x2 = x1 + s[2], y2 = y1 + s[3], x3 = x2 + s[4], y3 = y2 + s[5];
        i32 x4 = x3 + s[6], y4 = y3 + s[7], x5 = x4 + s[8], y5 = y4 + s[9], dx = x5 - x0, dy = y5 - y0, x6, y6;
        if (iabs(dx) > iabs(dy)) { x6 = x5 + s[10]; y6 = y0; } else { x6 = x0; y6 = y5 + s[10]; }
        if (!cs_curve(c, x1, y1, x2, y2, x3, y3) || !cs_curve(c, x4, y4, x5, y5, x6, y6)) return 0;
      }
      c->n = 0;                                               /* (arithmetic and the rest: not needed for outlines) */
      break; }
    default: c->n = 0; break;
    }
#undef WIDTH
  }
  return 1;
}
/* glyph g of a CFF font as lines */
static int cff_glyph(TTRaster *r, u16 g, i32 ppem64, Lines *ls)
{
  CS c;
  u32 at, len;
  memset(&c, 0, sizeof c);
  c.r = r; c.ls = ls; c.ppem64 = ppem64;
  c.mult = (i32)(((long long)ppem64 << 16) / r->upm);
  c.lsub = r->lsub_off;
  c.width_done = r->cff2;                                     /* (CFF2's charstrings carry no width) */
  c.vsindex = r->cff2 && r->nfd ? r->fd_vsindex[0] : 0;
  if (r->fdsel_off) {
    /* CID-keyed: the font DICT this glyph belongs to (FDSelect format 0 or 3) */
    const u8 *f = r->cff + r->fdsel_off;
    u32 fd = 0, left = r->lcff - r->fdsel_off;
    if (f[0] == 0 && left > 1u + g) fd = f[1 + g];
    else if (f[0] == 3 && left >= 5) {
      u32 nr = be16(f + 1), i;
      for (i = 0; i < nr && 3 + 3 * i + 5 <= left; i++)
        if (g >= be16(f + 3 + 3 * i) && g < be16(f + 3 + 3 * (i + 1))) { fd = f[3 + 3 * i + 2]; break; }
    }
    else if (f[0] == 4 && left >= 5) {                        /* (CFF2: 32-bit ranges, 16-bit FDs) */
      u32 nr = be32(f + 1), i;
      for (i = 0; i < nr && 5 + 6 * i + 10 <= left; i++)
        if (g >= be32(f + 5 + 6 * i) && g < be32(f + 5 + 6 * (i + 1))) { fd = be16(f + 5 + 6 * i + 4); break; }
    }
    c.lsub = (int)fd < r->nfd ? r->fd_lsub[fd] : 0;
    if (r->cff2 && (int)fd < r->nfd) c.vsindex = r->fd_vsindex[fd];
  }
  if (!cff_entry(r, r->cs_off, g, &at, &len)) return 0;
  if (!cs_run(&c, at, len)) return 0;
  return cs_close(&c);
}

/* glyph g's outline as lines (26.6 pixels at ppem64, y downwards); 0 on a damaged glyph */
static int glyph_lines(TTRaster *r, u16 g, i32 ppem64, Lines *ls)
{
  Contours cs;
  u32 i, k, start;
  int ok = 0;
  cs.ends = r->ends; cs.n = 0; cs.cap = r->capends;
  r->npts = 0;
  if (r->cff) {
    if (g >= r->nglyphs || !cff_glyph(r, g, ppem64, ls)) goto fail;
  } else {
  if (!outline(r, &cs, g, 1 << 14, 0, 0, 1 << 14, 0, 0, 0)) goto fail;
  if (!r->npts) { ok = 1; goto fail; }
  /* font units to 26.6 pixels, y downwards */
  for (i = 0; i < r->npts; i++) {
    r->xs[i] = r->xs[i] * ppem64 / r->upm;                    /* (units under 32768, ppem64 under 25600: 32 bits) */
    r->ys[i] = -(r->ys[i] * ppem64 / r->upm);
  }
  /* each contour as lines and curves: two points off the curve in a row have one on it between */
  for (k = 0, start = 0; k < cs.n; start = cs.ends[k] + 1, k++) {
    u32 e = cs.ends[k], n = e - start + 1, j, first;
    i32 sx, sy, px, py, cx = 0, cy = 0;
    int have_c = 0;
    if (n < 2) continue;
    /* start on a point on the curve (or between two off it) */
    for (first = 0; first < n && !r->on[start + first]; first++) ;
    if (first < n) { sx = r->xs[start + first]; sy = r->ys[start + first]; }
    else { first = 0; sx = (r->xs[start] + r->xs[start + 1]) / 2; sy = (r->ys[start] + r->ys[start + 1]) / 2; }
    px = sx; py = sy;
    for (j = 1; j <= n; j++) {
      u32 q = start + (first + j) % n;
      i32 x = r->xs[q], y = r->ys[q];
      if (r->on[q]) {
        if (have_c) { if (!add_curve(ls, px, py, cx, cy, x, y)) goto fail; have_c = 0; }
        else if (!add_line(ls, px, py, x, y)) goto fail;
        px = x; py = y;
      } else if (have_c) {
        i32 mx = (cx + x) / 2, my = (cy + y) / 2;
        if (!add_curve(ls, px, py, cx, cy, mx, my)) goto fail;
        px = mx; py = my; cx = x; cy = y;
      } else { cx = x; cy = y; have_c = 1; }
    }
    if (have_c) { if (!add_curve(ls, px, py, cx, cy, sx, sy)) goto fail; }
    else if (px != sx || py != sy) { if (!add_line(ls, px, py, sx, sy)) goto fail; }
  }
  }                                                          /* (TrueType outlines) */
  ok = 1;
fail:
  r->ends = cs.ends; r->capends = cs.cap;
  return ok;
}

/* ------------------------------------------------------------------ */
/* hinting: the lines moved before they are filled, so the alignment zones and the straight stems
   fall on whole pixels: control points (where an edge is, where it goes) on each axis, the other
   points following between them, in proportion */
/* v (26.6) to the nearest whole pixel */
static i32 round64(i32 v) { return floor64(v + 32) * 64; }
typedef struct { i32 from, to, slope; } CP;             /* slope: from the one before, 12 bits of fraction */
/* glyph g's lowest and highest points, font units, y up; 0 if it has none */
static int extent(TTRaster *r, u16 g, i32 *lo, i32 *hi)
{
  Lines ls;
  u32 i;
  int ok;
  ls.l = (Line *)r->lines; ls.n = 0; ls.cap = r->caplines;
  ok = g && glyph_lines(r, g, r->upm, &ls) && ls.n;          /* (at upm pixels to the em: 26.6 units are font units) */
  if (ok) {
    *lo = *hi = -ls.l[0].y0;
    for (i = 0; i < ls.n; i++) {
      i32 a = -ls.l[i].y0, b = -ls.l[i].y1;
      *lo = a < *lo ? a : *lo; *lo = b < *lo ? b : *lo;
      *hi = a > *hi ? a : *hi; *hi = b > *hi ? b : *hi;
    }
  }
  r->lines = ls.l; r->caplines = ls.cap;
  return ok;
}
/* measures up to five zones from sample letters: x-height, capitals, ascenders, the baseline
   and descenders. A zone is left out if the font lacks its letters or it nearly matches one
   already found; hinting stays off if none is found (a symbol font, say) */
void ttr_hint(TTRaster *r, const u16 *latin)
{
  /* each zone by its flat letters (their tops, or bottoms) and its round ones (overshooting) */
  static const struct { const char *flat, *round; int top; } zones[] = {
    { "xzvw", "oe", 1 }, { "HEIT", "OC", 1 }, { "dhkl", "", 1 }, { "xzHI", "oO", 0 }, { "pq", "", 0 },
  };
  int z, k;
  r->nz = 0;
  for (z = 0; z < (int)(sizeof zones / sizeof zones[0]); z++) {
    i32 flat = 0, over, lo, hi;
    int nf = 0, j;
    const char *c;
    for (c = zones[z].flat; *c; c++)
      if (extent(r, latin[(u8)*c], &lo, &hi)) { i32 v = zones[z].top ? hi : lo; flat = nf ? (zones[z].top ? (v > flat ? v : flat) : (v < flat ? v : flat)) : v; nf++; }
    if (!nf) continue;
    over = flat;
    for (c = zones[z].round; *c; c++)
      if (extent(r, latin[(u8)*c], &lo, &hi)) { i32 v = zones[z].top ? hi : lo; if (zones[z].top ? v > over : v < over) over = v; }
    if (iabs(over - flat) > r->upm / 20) over = flat;          /* (not an overshoot: some other shape) */
    for (j = 0; j < r->nz && iabs(r->zf[j] - flat) > r->upm / 50; j++) ;
    if (j < r->nz) continue;                                   /* (as good as one there already) */
    r->zf[r->nz] = flat; r->zo[r->nz] = over; r->nz++;
  }
  (void)k;
  r->hint = r->nz > 0;
}
/* control points ordered by where they are now */
static int cp_cmp(const void *a, const void *b)
{
  i32 x = ((const CP *)a)->from, y = ((const CP *)b)->from;
  return x < y ? -1 : x > y;
}
/* in order; one that would go back past the one before it is dropped */
static int cp_tidy(CP *cp, int n)
{
  int i, k = 0, j;
  for (i = 1; i < n; i++) {                                    /* (a few: sorted by insertion) */
    CP t = cp[i];
    for (j = i; j > 0 && cp_cmp(&cp[j - 1], &t) > 0; j--) cp[j] = cp[j - 1];
    cp[j] = t;
  }
  for (i = 0; i < n; i++) {
    if (k && (cp[i].from == cp[k - 1].from || cp[i].to < cp[k - 1].to)) continue;
    cp[k++] = cp[i];
  }
  return k;
}
/* for map_axis: how far each stretch between control points is stretched or squeezed */
static void cp_slopes(CP *cp, int n)
{
  int i;
  for (i = 1; i < n; i++) cp[i].slope = (cp[i].to - cp[i - 1].to) * 4096 / (cp[i].from - cp[i - 1].from);    /* (moves under 8000 pixels: 32 bits) */
}
/* where coordinate v moves to: outside the control points it shifts with the nearest one,
   between two it is placed in proportion, and on one it goes exactly where that one goes */
static i32 map_axis(const CP *cp, int n, i32 v)
{
  int i;
  i32 d;
  if (!n) return v;
  if (v <= cp[0].from) return v + cp[0].to - cp[0].from;
  if (v >= cp[n - 1].from) return v + cp[n - 1].to - cp[n - 1].from;
  for (i = 1; i < n && cp[i].from < v; i++) ;
  if (cp[i].from == v) return cp[i].to;                       /* (on one: exactly where it goes) */
  d = v - cp[i - 1].from;
  /* (32 bits do, but for huge sizes or steep moves) */
  if (d < 32768 && cp[i].slope < 65536 && cp[i].slope > -65536) {
    i32 m = d * cp[i].slope + 2048;                           /* (rounded: floor of m / 4096) */
    return cp[i - 1].to + (m >= 0 ? m / 4096 : -((-m + 4095) / 4096));
  }
  return cp[i - 1].to + (i32)(((long long)d * cp[i].slope) / 4096);
}
/* where a control point already is near v (within half a pixel), or -1 */
static int cp_near(const CP *cp, int n, i32 v)
{
  int i;
  for (i = 0; i < n; i++) if (iabs(cp[i].from - v) < 32) return i;
  return -1;
}
/* straight stems across an axis: long, straight runs of the outline (edges), paired with the
   nearest opposite one; each stem's first edge onto a pixel, its width rounded (one pixel at least) */
typedef struct { i32 pos, lo, hi; long len; int dir; } Edge;      /* lo, hi: how far it runs */
/* finds the stems on one axis (vertical: the x positions of upright stems) and adds their edges
   to the control points cp (n so far, room for max); returns the new count. Stems wider than a
   quarter em are not stems, and edges already held by a zone keep their place */
static int stems(const Lines *ls, int vertical, i32 ppem64, CP *cp, int n, int max)
{
  Edge e[64];
  int ne = 0, j, k;
  u32 i;
  for (i = 0; i < ls->n; i++) {
    const Line *l = &ls->l[i];
    i32 along = vertical ? l->y1 - l->y0 : l->x1 - l->x0, across = vertical ? l->x1 - l->x0 : l->y1 - l->y0, pos, len = iabs(along);
    int dir = along > 0 ? 1 : -1;
    i32 a0 = vertical ? l->y0 : l->x0, a1 = vertical ? l->y1 : l->x1, lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
    if (l->curve || len < 32 || iabs(across) * 8 > len) continue;
    pos = vertical ? (l->x0 + l->x1) / 2 : (l->y0 + l->y1) / 2;
    for (j = 0; j < ne && !(e[j].dir == dir && iabs(e[j].pos - pos) <= 8 && lo <= e[j].hi + 16 && hi >= e[j].lo - 16); j++) ;
    if (j < ne) {
      e[j].pos = (i32)((e[j].pos * (long long)e[j].len + pos * (long long)len) / (e[j].len + len)); e[j].len += len;
      e[j].lo = lo < e[j].lo ? lo : e[j].lo; e[j].hi = hi > e[j].hi ? hi : e[j].hi;
    }
    else if (ne < 64) { e[ne].pos = pos; e[ne].len = len; e[ne].dir = dir; e[ne].lo = lo; e[ne].hi = hi; ne++; }
  }
  for (;;) {
    int bi = -1, bj = -1, ka, kb;
    i32 bd = ppem64 / 4 + 1, a, b, w, nw;
    for (j = 0; j < ne; j++)
      if (e[j].dir && e[j].len >= 48)
        for (k = 0; k < ne; k++)
          if (e[k].dir == -e[j].dir && e[k].len >= 48 && e[k].pos > e[j].pos && e[k].pos - e[j].pos < bd &&
              (e[k].hi < e[j].hi ? e[k].hi : e[j].hi) - (e[k].lo > e[j].lo ? e[k].lo : e[j].lo) > 32) { bd = e[k].pos - e[j].pos; bi = j; bj = k; }   /* (facing each other) */
    if (bi < 0 || n + 2 > max) break;
    a = e[bi].pos; b = e[bj].pos; w = b - a;
    nw = round64(w); if (nw < 64) nw = 64;
    ka = cp_near(cp, n, a); kb = cp_near(cp, n, b);
    /* an edge on a zone stays with the zone; the other goes the stem's width from it */
    if (ka >= 0 && kb < 0) { cp[n].from = b; cp[n].to = cp[ka].to + nw; n++; }
    else if (kb >= 0 && ka < 0) { cp[n].from = a; cp[n].to = cp[kb].to - nw; n++; }
    else if (ka < 0 && kb < 0) { cp[n].from = a; cp[n].to = round64(a); cp[n + 1].from = b; cp[n + 1].to = round64(a) + nw; n += 2; }
    e[bi].dir = e[bj].dir = 0;                                 /* (paired) */
  }
  return n;
}
/* moves a glyph's lines (26.6 pixels) so zones and stems land on whole pixels: the zones and
   horizontal stems give the y control points, the upright stems the x ones */
static void hint_lines(TTRaster *r, Lines *ls, i32 ppem64)
{
  CP cx[72], cy[72];
  int nx = 0, ny = 0, z;
  u32 i;
  for (z = 0; z < r->nz; z++) {
    /* a zone onto a pixel row; its overshoot with it while that is under a pixel, else its own */
    i32 f = -(r->zf[z] * ppem64 / r->upm), o = -(r->zo[z] * ppem64 / r->upm), sf = round64(f);     /* (units * ppem64 well within 32 bits) */
    cy[ny].from = f; cy[ny].to = sf; ny++;
    if (o != f) { cy[ny].from = o; cy[ny].to = iabs(o - f) < 40 ? sf : sf + round64(o - f); ny++; }
  }
  ny = stems(ls, 0, ppem64, cy, ny, 72);
  nx = stems(ls, 1, ppem64, cx, nx, 72);
  ny = cp_tidy(cy, ny); nx = cp_tidy(cx, nx);
  cp_slopes(cy, ny); cp_slopes(cx, nx);
  for (i = 0; i < ls->n; i++) {
    Line *l = &ls->l[i];
    l->x0 = map_axis(cx, nx, l->x0); l->x1 = map_axis(cx, nx, l->x1);
    l->y0 = map_axis(cy, ny, l->y0); l->y1 = map_axis(cy, ny, l->y1);
  }
}

/* renders with a non-zero winding rule, 16 samples a pixel (4 rows of 4), coverage scaled to
   0-255. Sizes from 1 to 400 pixels; hinting (when on) only up to 64. A glyph with no outline
   gives a 0 x 0 bitmap and success. bm->cov is malloc'd for the caller to free */
int ttr_glyph(TTRaster *r, u16 g, i32 ppem64, TTBitmap *bm)
{
  Lines ls;
  i32 *cross = NULL;
  u32 i, *active, nact = 0, next = 0;
  i32 minx = 0, miny = 0, maxx = 0, maxy = 0;
  int ok = 0;
  memset(bm, 0, sizeof *bm);
  if (!r->fp || ppem64 < 64 || ppem64 > 64 * 400) return 0;
  ls.l = (Line *)r->lines; ls.n = 0; ls.cap = r->caplines;
  if (!glyph_lines(r, g, ppem64, &ls)) goto done;
  if (r->hint && ppem64 <= 64 * 64 && ls.n) hint_lines(r, &ls, ppem64);     /* (bigger, it isn't needed) */
  if (!ls.n) { ok = 1; goto done; }
  minx = maxx = ls.l[0].x0; miny = maxy = ls.l[0].y0;
  for (i = 0; i < ls.n; i++) {
    Line *l = &ls.l[i];
    minx = l->x0 < minx ? l->x0 : minx; minx = l->x1 < minx ? l->x1 : minx;
    maxx = l->x0 > maxx ? l->x0 : maxx; maxx = l->x1 > maxx ? l->x1 : maxx;
    miny = l->y0 < miny ? l->y0 : miny; miny = l->y1 < miny ? l->y1 : miny;
    maxy = l->y0 > maxy ? l->y0 : maxy; maxy = l->y1 > maxy ? l->y1 : maxy;
  }
  {
    i32 px0 = floor64(minx), py0 = floor64(miny), px1 = -floor64(-maxx), py1 = -floor64(-maxy);
    int w = (int)(px1 - px0), h = (int)(py1 - py0), sub;
    if (w <= 0 || h <= 0) { ok = 1; goto done; }
    if (w > 1024 || h > 1024) goto done;
    bm->w = w; bm->h = h; bm->left = (int)px0; bm->top = (int)-py0;
    if (!(bm->cov = (u8 *)calloc((size_t)w * h, 1))) goto done;
    if (ls.n * 2 > r->capcross) {                              /* (crossings, then the active lines) */
      i32 *nc = (i32 *)realloc(r->cross, ls.n * 2 * sizeof(i32));
      if (!nc) goto done;
      r->cross = nc; r->capcross = ls.n * 2;
    }
    cross = r->cross;
    /* the lines by their tops, so each sample row looks only at those crossing it */
    qsort(ls.l, ls.n, sizeof(Line), by_top);
    active = (u32 *)cross + ls.n;
    /* four sample rows a pixel; on each, the outline's crossings, and between them (where the
       winding isn't 0) four samples a pixel across */
    for (sub = 0; sub < h * 4; sub++) {
      i32 y = py0 * 64 + sub * 16 + 8;
      u32 nc = 0, c, keep = 0;
      u8 *row = bm->cov + (size_t)(sub / 4) * w;
      while (next < ls.n && top(&ls.l[next]) <= y) active[nact++] = next++;
      for (c = 0; c < nact; c++) {
        Line *l = &ls.l[active[c]];
        i32 ya = top(l), yb = l->y0 < l->y1 ? l->y1 : l->y0, x, dy, dx;
        if (y >= yb) continue;                                 /* passed: dropped */
        active[keep++] = active[c];
        if (y < ya) continue;
        dy = y - l->y0; dx = l->x1 - l->x0;
        if (dy < 46000 && dy > -46000 && dx < 46000 && dx > -46000) x = l->x0 + dy * dx / (l->y1 - l->y0);
        else x = l->x0 + (i32)(((long long)dy * dx) / (l->y1 - l->y0));
        /* the crossing, with its direction in the lowest bit (x is never left of px0) */
        {
          i32 v = (x - px0 * 64) * 2 + (l->y1 > l->y0);
          u32 k = nc++;
          while (k > 0 && cross[k - 1] > v) { cross[k] = cross[k - 1]; k--; }     /* in order as they come */
          cross[k] = v;
        }
      }
      nact = keep;
      if (nc < 2) continue;
      {
        int wind = 0;
        for (c = 0; c + 1 < nc; c++) {
          wind += cross[c] & 1 ? 1 : -1;
          if (wind) {
            i32 xa = cross[c] / 2, xb = cross[c + 1] / 2, s;
            /* the samples whose centres (every 16 units, from 8) lie in [xa, xb) */
            i32 s0 = (xa + 7) / 16, s1 = (xb + 7) / 16;
            if (s0 < 0) s0 = 0;
            if (s1 > w * 4) s1 = w * 4;
            for (s = s0; s < s1; s++) row[s >> 2] = (u8)(row[s >> 2] + 1);
          }
        }
      }
    }
    for (i = 0; i < (u32)(w * h); i++) bm->cov[i] = (u8)(bm->cov[i] >= 16 ? 255 : bm->cov[i] * 16);
  }
  ok = 1;
done:
  r->lines = ls.l; r->caplines = ls.cap;
  if (!ok) { free(bm->cov); memset(bm, 0, sizeof *bm); }
  return ok;
}

/* works along rows, then down columns: in each run of partly covered pixels with none at thr
   yet, the strongest becomes 255, provided the run holds three quarters of thr in all */
void ttr_dropout(TTBitmap *bm, int thr)
{
  int pass, a, b, na, nb;
  if (!bm->cov) return;
  for (pass = 0; pass < 2; pass++) {
    na = pass ? bm->w : bm->h; nb = pass ? bm->h : bm->w;
    for (a = 0; a < na; a++) {
      for (b = 0; b < nb; ) {
        int strong = 0, sum = 0, best = b, e;
        u8 *px;
        if (!bm->cov[pass ? b * bm->w + a : a * bm->w + b]) { b++; continue; }
        /* a run of covered pixels: if none of them is set, its strongest one is */
        for (e = b; e < nb && *(px = &bm->cov[pass ? e * bm->w + a : a * bm->w + e]); e++) {
          if (*px >= thr) strong = 1;
          sum += *px;
          if (*px > bm->cov[pass ? best * bm->w + a : a * bm->w + best]) best = e;
        }
        if (!strong && sum >= thr * 3 / 4) bm->cov[pass ? best * bm->w + a : a * bm->w + best] = 255;
        b = e;
      }
    }
  }
}
