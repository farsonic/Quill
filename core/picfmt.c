/*
 * picfmt.c - see picfmt.h. The readers and writers here are Easel's (core/gif.c, bmp.c, ilbm.c,
 * png.c, jpegenc.c of Easel 1.0, the Amiga paint program by Quill's author), copied with their
 * comments; only the glue (Rgba, the error messages, no progress bar) and the JPEG encoder's
 * integer DCT (Quill's core has no floating point) are Quill's. WP17.
 */
#include <stdlib.h>
#include <string.h>
#include "picfmt.h"
#include "zip.h"

#define MAXDIM 16384
#define CODEC_MAXPIXELS (16L * 1024 * 1024)
#define CAMG_EHB 0x80
#define CAMG_HAM 0x800
static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static void put32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24)); buf_addc(b, (int)((v >> 16) & 255)); buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void codec_err(char *err, size_t cap, const char *msg) { if (err && cap) { strncpy(err, msg, cap - 1); err[cap - 1] = 0; } }
int rgba_alloc(Rgba *im, i32 w, i32 h)
{
  im->w = im->h = 0; im->px = NULL;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) return 0;
  if (!(im->px = (u8 *)calloc((size_t)w * h, 4))) return 0;
  im->w = w; im->h = h;
  return 1;
}
void rgba_free(Rgba *im) { free(im->px); im->px = NULL; im->w = im->h = 0; }

/* ---- GIF (Easel core/gif.c) */

/* step over a run of data sub-blocks (each a length byte and that many bytes, ended by a 0);
   returns the offset after the terminator, or 0 if the data runs out */
static size_t skip_blocks(const u8 *d, size_t n, size_t o)
{
  while (o < n && d[o]) o += (size_t)d[o] + 1;
  return o < n ? o + 1 : 0;
}

/* decode LZW codes (variable width, least significant bit first, from mcs + 1 to 12 bits) from
   s[0..n) into npx indexes. Returns how many pixels were written; damaged codes end it early */
static long lzw(const u8 *s, size_t n, int mcs, u8 *out, long npx)
{
  u16 *prefix = (u16 *)malloc(sizeof(u16) * 4096);
  u8 *suffix = (u8 *)malloc(4096), *stack = (u8 *)malloc(4097);
  int clear = 1 << mcs, eoi = clear + 1, next = clear + 2, size = mcs + 1, old = -1, first = 0, i;
  long o = 0;
  size_t bitpos = 0, nbits = n * 8;
  if (!prefix || !suffix || !stack) { free(prefix); free(suffix); free(stack); return 0; }
  for (i = 0; i < clear; i++) { suffix[i] = (u8)i; prefix[i] = 0xFFFF; }
  while (o < npx && bitpos + (size_t)size <= nbits) {
    int code = 0, k, sp = 0, c;
    for (k = 0; k < size; k++, bitpos++) code |= ((s[bitpos >> 3] >> (bitpos & 7)) & 1) << k;
    if (code == clear) { next = clear + 2; size = mcs + 1; old = -1; continue; }
    if (code == eoi) break;
    if (old < 0) {                                       /* the first code after a clear: a plain index */
      if (code >= clear) break;
      out[o++] = (u8)code; old = first = code;
      continue;
    }
    if (code < next) c = code;
    else if (code == next) { stack[sp++] = (u8)first; c = old; }   /* the string being defined: old + its first byte */
    else break;
    while (c >= clear && sp < 4096) { stack[sp++] = suffix[c]; c = prefix[c]; }
    if (c >= clear) break;
    stack[sp++] = (u8)c;
    first = c;
    while (sp && o < npx) out[o++] = stack[--sp];
    if (next < 4096) {
      prefix[next] = (u16)old; suffix[next] = (u8)first; next++;
      if (next == (1 << size) && size < 12) size++;
    }
    old = code;
  }
  free(prefix); free(suffix); free(stack);
  return o;
}

/* header and logical screen, the global table, then blocks up to the first image: extensions are
   skipped except a Graphic Control one (for its transparent colour). The image's sub-blocks are
   joined and decompressed; every pixel must be there */
int gif_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  const u8 *gct = NULL, *ct;
  int ngct = 0, nct, trans = -1, mcs, inter, ok = 0;
  i32 sw, sh, left, top, iw, ih, cw, ch, y, x;
  size_t o, e, k, total = 0;
  u8 *data = NULL, *idx = NULL;
  const char *msg = "Damaged GIF file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 13 || (memcmp(d, "GIF87a", 6) && memcmp(d, "GIF89a", 6))) { codec_err(err, errcap, "Not a GIF file"); return 0; }
  sw = (i32)le16(d + 6); sh = (i32)le16(d + 8);
  o = 13;
  if (d[10] & 0x80) {
    ngct = 2 << (d[10] & 7);
    gct = d + 13;
    o += (size_t)ngct * 3;
    if (o > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  }
  for (;;) {
    if (o >= n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
    if (d[o] == 0x21) {                                  /* extension */
      if (o + 2 > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
      if (d[o + 1] == 0xF9 && o + 7 < n && d[o + 2] >= 4) trans = (d[o + 3] & 1) ? d[o + 6] : -1;
      if (!(o = skip_blocks(d, n, o + 2))) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
    } else if (d[o] == 0x2C) break;                      /* an image */
    else if (d[o] == 0x3B) { codec_err(err, errcap, "The GIF file has no picture"); return 0; }
    else { codec_err(err, errcap, msg); return 0; }
  }
  if (o + 10 > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  left = (i32)le16(d + o + 1); top = (i32)le16(d + o + 3); iw = (i32)le16(d + o + 5); ih = (i32)le16(d + o + 7);
  inter = d[o + 9] & 0x40;
  ct = gct; nct = ngct;
  if (d[o + 9] & 0x80) { nct = 2 << (d[o + 9] & 7); ct = d + o + 10; o += (size_t)nct * 3; }
  o += 10;
  if (o + 1 > n) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  mcs = d[o++];
  if (mcs < 1 || mcs > 11) { codec_err(err, errcap, msg); return 0; }
  /* the canvas: the logical screen, grown to hold the frame if the screen is too small */
  cw = left + iw > sw ? left + iw : sw; ch = top + ih > sh ? top + ih : sh;
  if (iw < 1 || ih < 1 || cw > MAXDIM || ch > MAXDIM || (long)cw * ch > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad GIF picture size"); return 0; }
  /* the compressed data's length, then the data joined up */
  for (e = o; e < n && d[e]; e += (size_t)d[e] + 1) total += d[e];
  if (e >= n && !total) { codec_err(err, errcap, "Truncated GIF file"); return 0; }
  if (!(data = (u8 *)malloc(total + 1)) || !(idx = (u8 *)malloc((size_t)iw * ih))) { msg = "Out of memory"; goto done; }
  for (e = o, k = 0; e < n && d[e]; e += (size_t)d[e] + 1) {
    size_t l = d[e];
    if (e + 1 + l > n) l = n - e - 1;
    memcpy(data + k, d + e + 1, l); k += l;
  }
  if (lzw(data, k, mcs, idx, (long)iw * ih) < (long)iw * ih) { msg = "Truncated GIF file"; goto done; }
  if (!rgba_alloc(out, cw, ch)) { msg = "Out of memory"; goto done; }
  {
    /* interlaced rows come in four passes: every 8th from 0, every 8th from 4, every 4th from 2, every 2nd from 1 */
    static const u8 pstart[4] = { 0, 4, 2, 1 }, pstep[4] = { 8, 8, 4, 2 };
    int pass = 0;
    i32 ry = 0;
    for (y = 0; y < ih; y++) {
      const u8 *s = idx + (size_t)y * iw;
      u8 *row;
      if (inter) {
        while (ry >= ih && pass < 3) { pass++; ry = pstart[pass]; }
        row = out->px + ((size_t)(top + ry) * cw + left) * 4;
        ry += pstep[pass];
      } else row = out->px + ((size_t)(top + y) * cw + left) * 4;
      for (x = 0; x < iw; x++, row += 4) {
        int v = s[x];
        if (v == trans) continue;
        if (ct && v < nct) { row[0] = ct[v * 3]; row[1] = ct[v * 3 + 1]; row[2] = ct[v * 3 + 2]; }
        else if (!ct) row[0] = row[1] = row[2] = (u8)v;
        row[3] = 255;
      }
    }
  }
  ok = 1;
done:
  if (!ok) { rgba_free(out); codec_err(err, errcap, msg); }
  free(data); free(idx);
  return ok;
}

/* ---- BMP (Easel core/bmp.c) */

/* a channel described by a bitfield mask: where it starts and how wide it is (8 bits at most are
   kept: wider fields lose their low bits) */
typedef struct { u32 mask; int shift, bits; } Field;
static void field(Field *f, u32 mask)
{
  f->mask = mask; f->shift = 0; f->bits = 0;
  if (!mask) return;
  while (!(mask & 1)) { mask >>= 1; f->shift++; }
  while (mask & 1) { mask >>= 1; f->bits++; }
}
/* the channel's value in a pixel, scaled to 0-255; dflt if the mask is empty */
static int field_get(const Field *f, u32 px, int dflt)
{
  u32 v, mx;
  int b = f->bits;
  if (!b) return dflt;
  v = (px & f->mask) >> f->shift;
  if (b > 8) { v >>= b - 8; b = 8; }
  mx = (1UL << b) - 1;
  return (int)(v * 255 / mx);
}

/* RLE8 (rle4 0) or RLE4 into idx (w * h indexes, rows as stored: bottom up). Runs and absolute
   stretches past the picture are clipped; 0 if the data stops before the end-of-picture code
   with rows still to come */
static int unrle(const u8 *s, size_t n, int rle4, u8 *idx, i32 w, i32 h)
{
  size_t i = 0;
  i32 x = 0, y = 0;
  while (i + 1 < n) {
    int c = s[i], v = s[i + 1];
    i += 2;
    if (c) {                                             /* a run of c pixels */
      int k;
      for (k = 0; k < c; k++, x++)
        if (x < w && y < h) idx[(long)y * w + x] = (u8)(rle4 ? (k & 1 ? v & 15 : v >> 4) : v);
    } else if (v == 0) { x = 0; y++; }                   /* end of line */
    else if (v == 1) return 1;                           /* end of picture */
    else if (v == 2) {                                   /* move right and up */
      if (i + 2 > n) return 0;
      x += s[i]; y += s[i + 1]; i += 2;
    } else {                                             /* v pixels as they are, padded to 16 bits */
      size_t bytes = rle4 ? ((size_t)v + 1) / 2 : (size_t)v;
      int k;
      if (i + bytes > n) return 0;
      for (k = 0; k < v; k++, x++)
        if (x < w && y < h) idx[(long)y * w + x] = (u8)(rle4 ? (k & 1 ? s[i + k / 2] & 15 : s[i + k / 2] >> 4) : s[i + k]);
      i += bytes + (bytes & 1);
    }
    if (y >= h) return 1;
  }
  return y >= h - 1;
}

/* the file header, then the info header (12 bytes for OS/2, 40 and up for Windows: masks come in
   the V2+ header or, for a 40-byte one, just after it), the palette, the pixels */
int bmp_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 off, hs, comp = 0, bpp, ncol, palsz = 4, i, stride;
  i32 w, h, x, y;
  int topdown = 0, usealpha = 0;
  u8 pal[256][3];
  size_t palpos;
  Field fr, fg, fb, fa;
  u8 *idx = NULL;
  out->w = out->h = 0; out->px = NULL;
  if (n < 26 || d[0] != 'B' || d[1] != 'M') { codec_err(err, errcap, "Not a BMP file"); return 0; }
  off = le32(d + 10); hs = le32(d + 14);
  if (hs == 12) {
    w = (i32)le16(d + 18); h = (i32)le16(d + 20); bpp = le16(d + 24); palsz = 3;
  } else if (hs >= 40 && hs <= 1024 && n >= 54) {
    u32 uh = le32(d + 22);
    w = (i32)le32(d + 18);
    if (uh == 0x80000000UL) { codec_err(err, errcap, "Damaged BMP file"); return 0; }
    if (uh & 0x80000000UL) { h = (i32)(0 - uh); topdown = 1; } else h = (i32)uh;
    bpp = le16(d + 28); comp = le32(d + 30);
  } else { codec_err(err, errcap, "Unsupported BMP header"); return 0; }
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad BMP picture size"); return 0; }
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { codec_err(err, errcap, "Unsupported BMP depth"); return 0; }
  if (comp == 6) comp = 3;                               /* BI_ALPHABITFIELDS: bitfields with alpha */
  if (!(comp == 0 || (comp == 1 && bpp == 8) || (comp == 2 && bpp == 4) || (comp == 3 && (bpp == 16 || bpp == 32)))) {
    codec_err(err, errcap, "Unsupported BMP compression"); return 0;
  }
  /* channel masks: the defaults, or the header's */
  palpos = 14 + hs;
  if (bpp == 16) { field(&fr, 0x7C00); field(&fg, 0x03E0); field(&fb, 0x001F); }
  else { field(&fr, 0xFF0000UL); field(&fg, 0xFF00); field(&fb, 0xFF); }
  field(&fa, 0);
  if (comp == 3) {
    size_t mp = 54;
    u32 am = 0;
    if (hs == 40) { palpos += le32(d + 30) == 6 ? 16 : 12; if (le32(d + 30) == 6 && n >= 70) am = le32(d + 66); }
    else if (hs >= 56) am = le32(d + 66);
    if (n < mp + 12) { codec_err(err, errcap, "Truncated BMP file"); return 0; }
    field(&fr, le32(d + mp)); field(&fg, le32(d + mp + 4)); field(&fb, le32(d + mp + 8));
    if (am && n >= 70) { field(&fa, am); usealpha = 1; }
  } else if (hs >= 56 && bpp == 32 && n >= 70 && le32(d + 66)) {
    /* a V3+ header may give an alpha mask even for plain 32-bit */
    field(&fa, le32(d + 66)); usealpha = 1;
  }
  /* the palette (whatever of it the file really holds) */
  memset(pal, 0, sizeof pal);
  ncol = bpp <= 8 ? (hs >= 40 && le32(d + 46) ? le32(d + 46) : 1u << bpp) : 0;
  if (ncol > 256) ncol = 256;
  for (i = 0; i < ncol && palpos + (i + 1) * palsz <= n; i++) {
    const u8 *p = d + palpos + i * palsz;
    pal[i][0] = p[2]; pal[i][1] = p[1]; pal[i][2] = p[0];
  }
  if (off >= n) { codec_err(err, errcap, "Truncated BMP file"); return 0; }
  if (!rgba_alloc(out, w, h)) { codec_err(err, errcap, "Out of memory"); return 0; }
  if (comp == 1 || comp == 2) {
    if (!(idx = (u8 *)calloc((size_t)w * h, 1))) { rgba_free(out); codec_err(err, errcap, "Out of memory"); return 0; }
    if (!unrle(d + off, n - off, comp == 2, idx, w, h)) { free(idx); rgba_free(out); codec_err(err, errcap, "Truncated BMP file"); return 0; }
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        u8 *o = out->px + ((long)(h - 1 - y) * w + x) * 4, *c = pal[idx[(long)y * w + x]];
        o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255;
      }
    free(idx);
    return 1;
  }
  /* uncompressed: every row must be there (the last one's padding may be missing) */
  stride = (((u32)w * bpp + 31) / 32) * 4;
  if ((unsigned long)(n - off) < (unsigned long)stride * (h - 1) + ((u32)w * bpp + 7) / 8) {
    rgba_free(out); codec_err(err, errcap, "Truncated BMP file"); return 0;
  }
  /* plain 32-bit: the fourth byte is alpha only if some pixel has it non-zero */
  if (bpp == 32 && comp == 0 && !usealpha) {
    for (y = 0; y < h && !usealpha; y++)
      for (x = 0; x < w; x++) if (d[off + (size_t)y * stride + (size_t)x * 4 + 3]) { usealpha = 1; break; }
    if (usealpha) field(&fa, 0xFF000000UL);
  }
  for (y = 0; y < h; y++) {
    const u8 *r = d + off + (size_t)y * stride;
    u8 *o = out->px + (size_t)(topdown ? y : h - 1 - y) * w * 4;
    for (x = 0; x < w; x++, o += 4) {
      if (bpp <= 8) {
        u32 bit = (u32)x * bpp, v = (r[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1u << bpp) - 1);
        o[0] = pal[v][0]; o[1] = pal[v][1]; o[2] = pal[v][2]; o[3] = 255;
      } else if (bpp == 24) {
        o[0] = r[x * 3 + 2]; o[1] = r[x * 3 + 1]; o[2] = r[x * 3]; o[3] = 255;
      } else {
        u32 v = bpp == 16 ? le16(r + x * 2) : le32(r + x * 4);
        o[0] = (u8)field_get(&fr, v, 0); o[1] = (u8)field_get(&fg, v, 0); o[2] = (u8)field_get(&fb, v, 0);
        o[3] = (u8)(usealpha ? field_get(&fa, v, 255) : 255);
      }
    }
  }
  return 1;
}


/* ---- IFF ILBM (Easel core/ilbm.c) */

/* ------------------------------------------------------------------ reading */
/* ByteRun1 from s[0..n) into out until it holds size bytes: n >= 0 copies the next n + 1 bytes,
   -127..-1 repeats the next byte 1 - n times, -128 is a no-op. A run may cross rows (some writers
   do that). 0 if the data runs out first */
static int unpack(const u8 *s, size_t n, u8 *out, size_t size)
{
  size_t i = 0, o = 0;
  while (o < size) {
    int c;
    size_t k;
    if (i >= n) return 0;
    c = s[i++];
    if (c < 128) {
      k = (size_t)c + 1;
      if (i + k > n) return 0;
      if (k > size - o) k = size - o;
      memcpy(out + o, s + i, k);
      o += k; i += (size_t)c + 1;
    } else if (c > 128) {
      if (i >= n) return 0;
      k = 257 - (size_t)c;
      if (k > size - o) k = size - o;
      memset(out + o, s[i++], k);
      o += k;
    }
  }
  return 1;
}

/* walks the FORM's chunks (a chunk running past the end of the file is cut short, which the BODY
   decoding then notices), unpacks the whole BODY, then turns each row's planes into pixels */
int ilbm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  size_t end, o, rowsize, rowbytes = 0, size;
  const u8 *body = NULL;
  size_t bodylen = 0;
  u8 pal[256][3], *buf = NULL;
  u32 camg = 0, *vals = NULL;
  int pbm, haveh = 0, ncmap = 0, np = 0, masking = 0, comp = 0, tcol = 0, ham = 0, ehb = 0, deep, nplanes, ok = 0, i;
  i32 w = 0, h = 0, x, y;
  const char *msg = "Damaged ILBM file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 12 || memcmp(d, "FORM", 4) || (memcmp(d + 8, "ILBM", 4) && memcmp(d + 8, "PBM ", 4))) { codec_err(err, errcap, "Not an IFF ILBM file"); return 0; }
  pbm = !memcmp(d + 8, "PBM ", 4);
  end = be32(d + 4);
  end = end > n - 8 ? n : end + 8;
  for (o = 12; o + 8 <= end; ) {
    size_t len = be32(d + o + 4), avail = end - o - 8;
    const u8 *c = d + o + 8;
    if (len > avail) len = avail;
    if (!memcmp(d + o, "BMHD", 4) && len >= 20) {
      w = (i32)be16(c); h = (i32)be16(c + 2); np = c[8]; masking = c[9]; comp = c[10]; tcol = (int)be16(c + 12);
      haveh = 1;
    } else if (!memcmp(d + o, "CMAP", 4)) {
      ncmap = (int)(len / 3 > 256 ? 256 : len / 3);
      for (i = 0; i < ncmap; i++) { pal[i][0] = c[i * 3]; pal[i][1] = c[i * 3 + 1]; pal[i][2] = c[i * 3 + 2]; }
    } else if (!memcmp(d + o, "CAMG", 4) && len >= 4) camg = be32(c);
    else if (!memcmp(d + o, "BODY", 4)) { body = c; bodylen = len; break; }
    o += 8 + len + (len & 1);
  }
  if (!haveh) { codec_err(err, errcap, "The ILBM file has no BMHD header"); return 0; }
  if (!body) { codec_err(err, errcap, "The ILBM file has no BODY"); return 0; }
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad ILBM picture size"); return 0; }
  deep = !pbm && (np == 24 || np == 32);
  if (!deep && (np < 1 || np > 8)) { codec_err(err, errcap, "Unsupported number of ILBM bitplanes"); return 0; }
  if (comp > 1) { codec_err(err, errcap, "Unsupported ILBM compression"); return 0; }
  ham = !pbm && !deep && (camg & CAMG_HAM) && (np == 6 || np == 8);
  ehb = !pbm && !ham && np == 6 && ((camg & CAMG_EHB) || (ncmap > 0 && ncmap <= 32));
  /* without a CMAP: a grey ramp */
  for (i = ncmap; i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = 0;
  if (!ncmap && !deep) for (i = 0; i < (1 << np) && i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = (u8)(i * 255 / ((1 << np) - 1));
  nplanes = np + (masking == 1 && !pbm);                 /* the mask plane follows the colour planes */
  if (pbm) rowsize = (size_t)w + (w & 1);
  else { rowbytes = (size_t)((w + 15) >> 4) << 1; rowsize = rowbytes * nplanes; }
  size = rowsize * (size_t)h;
  /* ByteRun1 makes at most 128 bytes from 2, so a short BODY can't claim a huge picture */
  if ((comp == 0 && bodylen < size) || (comp == 1 && bodylen < size / 64)) { codec_err(err, errcap, "Truncated ILBM file"); return 0; }
  if (!(buf = (u8 *)malloc(size)) || !(vals = (u32 *)malloc(sizeof(u32) * (size_t)w))) { msg = "Out of memory"; goto done; }
  if (comp == 0) memcpy(buf, body, size);
  else if (!unpack(body, bodylen, buf, size)) { msg = "Truncated ILBM file"; goto done; }
  if (!rgba_alloc(out, w, h)) { msg = "Out of memory"; goto done; }
  for (y = 0; y < h; y++) {
    const u8 *row = buf + (size_t)y * rowsize;
    u8 *px = out->px + (size_t)y * w * 4, cr, cg, cb;
    int p;
    if (pbm) for (x = 0; x < w; x++) vals[x] = row[x];
    else {
      memset(vals, 0, sizeof(u32) * (size_t)w);
      for (p = 0; p < np; p++) {
        const u8 *pl = row + (size_t)p * rowbytes;
        for (x = 0; x < w; x++) vals[x] |= (u32)((pl[x >> 3] >> (7 - (x & 7))) & 1) << p;
      }
    }
    cr = pal[0][0]; cg = pal[0][1]; cb = pal[0][2];      /* HAM starts each row from colour 0 */
    for (x = 0; x < w; x++, px += 4) {
      u32 v = vals[x];
      px[3] = 255;
      if (deep) {
        px[0] = (u8)v; px[1] = (u8)(v >> 8); px[2] = (u8)(v >> 16);
        if (np == 32) px[3] = (u8)(v >> 24);
        continue;
      }
      if (ham) {
        int hb = np - 2, ctl = (int)(v >> hb);
        u32 dv = v & ((1u << hb) - 1), m = dv << (8 - hb);
        m |= m >> hb;                                    /* HAM6 4 bits -> x17; HAM8 6 bits -> top bits repeated */
        if (ctl == 0) { cr = pal[dv][0]; cg = pal[dv][1]; cb = pal[dv][2]; }
        else if (ctl == 1) cb = (u8)m;
        else if (ctl == 2) cr = (u8)m;
        else cg = (u8)m;
        px[0] = cr; px[1] = cg; px[2] = cb;
        continue;
      }
      if (ehb && v >= 32) { px[0] = (u8)(pal[v - 32][0] >> 1); px[1] = (u8)(pal[v - 32][1] >> 1); px[2] = (u8)(pal[v - 32][2] >> 1); }
      else { px[0] = pal[v & 255][0]; px[1] = pal[v & 255][1]; px[2] = pal[v & 255][2]; }
      if (masking == 2 && (int)v == tcol) px[3] = 0;
    }
  }
  ok = 1;
done:
  if (!ok) { rgba_free(out); codec_err(err, errcap, msg); }
  free(buf); free(vals);
  return ok;
}

/* ------------------------------------------------------------------ quantizing */
/* pixel i's RGB (the picture is taken as opaque: callers flatten it first) */

/* ---- PNG writing (Easel core/png.c) */
static int paeth(int a, int b, int c)
{
  int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
/* undo the filters of h rows of rb bytes (each after its filter byte) in place; fb is the bytes
   per pixel (at least 1). 0 for an unknown filter type */

/* Adler-32 as zlib defines it, the sums reduced every 5552 bytes so they never overflow 32 bits */
static u32 adler32(u32 a, const u8 *p, size_t n)
{
  u32 s1 = a & 0xFFFF, s2 = a >> 16;
  while (n) {
    size_t k = n < 5552 ? n : 5552;
    n -= k;
    while (k--) { s1 += *p++; s2 += s1; }
    s1 %= 65521; s2 %= 65521;
  }
  return (s2 << 16) | s1;
}
/* zip.c's raw deflate inside a zlib header (32 KB window, no dictionary) and Adler-32 trailer */
static u8 *zlib_compress(const u8 *in, size_t n, size_t *outlen)
{
  size_t dl;
  u8 *def = zdeflate(in, n, &dl), *o;
  u32 a;
  if (!def) return NULL;
  if (!(o = (u8 *)malloc(dl + 6))) { free(def); return NULL; }
  o[0] = 0x78; o[1] = 0x01;                 /* (0x78 << 8 | 0x01) is a multiple of 31, as required */
  memcpy(o + 2, def, dl);
  free(def);
  a = adler32(1, in, n);
  o[dl + 2] = (u8)(a >> 24); o[dl + 3] = (u8)(a >> 16); o[dl + 4] = (u8)(a >> 8); o[dl + 5] = (u8)a;
  *outlen = dl + 6;
  return o;
}

/* one chunk: length, type, data and the CRC of type and data */
static void chunk(Buf *b, const char *type, const u8 *data, u32 len)
{
  u32 crc = crc32_update(0, (const u8 *)type, 4);
  put32(b, len);
  buf_add(b, type, 4);
  if (len) { buf_add(b, data, len); crc = crc32_update(crc, data, len); }
  put32(b, crc);
}
/* the filtered byte for filter f at position x of a row (cur, prev the raw rows, fb bytes a pixel) */
static u8 filt(int f, const u8 *cur, const u8 *prev, u32 x, int fb)
{
  int a = x >= (u32)fb ? cur[x - fb] : 0, b = prev ? prev[x] : 0, c = prev && x >= (u32)fb ? prev[x - fb] : 0;
  switch (f) {
  case 1: return (u8)(cur[x] - a);
  case 2: return (u8)(cur[x] - b);
  case 3: return (u8)(cur[x] - ((a + b) >> 1));
  case 4: return (u8)(cur[x] - paeth(a, b, c));
  }
  return cur[x];
}

/* RGB if every alpha is 255 (a quarter smaller), else RGBA; each row gets whichever of the five
   filters gives the smallest sum of its bytes taken as signed, then the whole lot is deflated */
u8 *png_encode(const Rgba *im, int dpi, size_t *len)
{
  long i, n = (long)im->w * im->h;
  int ch = 3, fb, f, best;
  u32 rb, x, y;
  u8 *filtered = NULL, *rows = NULL, *z = NULL, hdr[13];
  size_t zlen;
  Buf b;
  for (i = 0; i < n; i++) if (im->px[i * 4 + 3] != 255) { ch = 4; break; }
  fb = ch;
  rb = (u32)im->w * ch;
  if (!(filtered = (u8 *)malloc((size_t)im->h * (rb + 1))) || !(rows = (u8 *)malloc((size_t)rb * 2))) { free(filtered); free(rows); return NULL; }
  for (y = 0; y < (u32)im->h; y++) {
    u8 *cur = rows + (y & 1) * rb, *prev = y ? rows + ((y - 1) & 1) * rb : NULL, *o = filtered + (size_t)y * (rb + 1);
    const u8 *s = im->px + (size_t)y * im->w * 4;
    unsigned long sum, bestsum = 0;
    if (ch == 4) memcpy(cur, s, rb);
    else for (x = 0; x < (u32)im->w; x++) { cur[x * 3] = s[x * 4]; cur[x * 3 + 1] = s[x * 4 + 1]; cur[x * 3 + 2] = s[x * 4 + 2]; }
    for (best = 0, f = 0; f < 5; f++) {
      for (sum = 0, x = 0; x < rb; x++) { int v = filt(f, cur, prev, x, fb); sum += (unsigned long)(v < 128 ? v : 256 - v); }
      if (!f || sum < bestsum) { bestsum = sum; best = f; }
    }
    o[0] = (u8)best;
    for (x = 0; x < rb; x++) o[x + 1] = filt(best, cur, prev, x, fb);
  }
  free(rows);
  z = zlib_compress(filtered, (size_t)im->h * (rb + 1), &zlen);
  free(filtered);
  if (!z) return NULL;
  buf_init(&b);
  buf_add(&b, "\211PNG\r\n\032\n", 8);
  hdr[0] = (u8)(im->w >> 24); hdr[1] = (u8)(im->w >> 16); hdr[2] = (u8)(im->w >> 8); hdr[3] = (u8)im->w;
  hdr[4] = (u8)(im->h >> 24); hdr[5] = (u8)(im->h >> 16); hdr[6] = (u8)(im->h >> 8); hdr[7] = (u8)im->h;
  hdr[8] = 8; hdr[9] = (u8)(ch == 4 ? 6 : 2); hdr[10] = hdr[11] = hdr[12] = 0;
  chunk(&b, "IHDR", hdr, 13);
  if (dpi > 0) {
    u8 phys[9];
    u32 ppm = (u32)(((unsigned long)dpi * 10000UL + 127) / 254);
    phys[0] = phys[4] = (u8)(ppm >> 24); phys[1] = phys[5] = (u8)(ppm >> 16); phys[2] = phys[6] = (u8)(ppm >> 8); phys[3] = phys[7] = (u8)ppm;
    phys[8] = 1;                                  /* metres */
    chunk(&b, "pHYs", phys, 9);
  }
  chunk(&b, "IDAT", z, (u32)zlen);
  free(z);
  chunk(&b, "IEND", NULL, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}


/* ---- JPEG writing (Easel core/jpegenc.c) */
/* ------------------------------------------------------------------ tables */
static const u8 zigzag[64] = {        /* the natural position of each coefficient in zigzag order */
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const u8 std_lum_q[64] = {     /* Annex K.1, natural order */
  16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const u8 std_chr_q[64] = {
  17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };
/* Annex K.3: code counts per length 1-16, then the symbols */
static const u8 dc_lum_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const u8 dc_chr_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const u8 dc_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const u8 ac_lum_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const u8 ac_lum_vals[162] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
static const u8 ac_chr_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const u8 ac_chr_vals[162] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

typedef struct { u16 code[256]; u8 size[256]; } HuffEnc;
/* the canonical codes for a table given as counts per length and symbols (Annex C) */
static void huff_build(HuffEnc *h, const u8 *bits, const u8 *vals)
{
  int len, i, k = 0;
  u32 code = 0;
  memset(h, 0, sizeof *h);
  for (len = 1; len <= 16; len++) {
    for (i = 0; i < bits[len - 1]; i++, k++) { h->code[vals[k]] = (u16)code++; h->size[vals[k]] = (u8)len; }
    code <<= 1;
  }
}

/* ------------------------------------------------------------------ encoding */
/* (Quill: integer arithmetic throughout, as the rest of the core: the slow but accurate integer DCT
   of the JPEG standard's reference (as IJG's jfdctint.c) instead of Easel's floating-point one) */
typedef struct {
  Buf o;
  u32 bits; int nbits;                /* bits waiting to make a byte, most significant first */
  HuffEnc dc[2], ac[2];
  int q[2][64];                       /* the quantizers times 8 (the DCT's output is 8 times too big), natural order */
  int pred[3];                        /* the last DC of each component */
} Enc;

/* append the low n bits of v; every 0xFF byte in the entropy data is followed by a stuffed 0 */
static void putbits(Enc *e, u32 v, int n)
{
  e->bits = (e->bits << n) | (v & ((1UL << n) - 1));
  e->nbits += n;
  while (e->nbits >= 8) {
    int c = (int)((e->bits >> (e->nbits - 8)) & 255);
    buf_addc(&e->o, c);
    if (c == 255) buf_addc(&e->o, 0);
    e->nbits -= 8;
  }
}
#define CB 13
#define P1 2
#define DESC(x, n) (((x) + (1L << ((n) - 1))) >> (n))
/* the forward DCT of an 8 x 8 block in place (Loeffler, Ligtenberg and Moschytz; the output 8 times too big) */
static void fdct(long *d)
{
  long t0, t1, t2, t3, t4, t5, t6, t7, t10, t11, t12, t13, z1, z2, z3, z4, z5, *p;
  int i;
  for (i = 0; i < 16; i++) {
    int s = i < 8 ? 1 : 8, col = i >= 8;
    p = i < 8 ? d + i * 8 : d + (i - 8);
    t0 = p[0] + p[7 * s]; t7 = p[0] - p[7 * s]; t1 = p[s] + p[6 * s]; t6 = p[s] - p[6 * s];
    t2 = p[2 * s] + p[5 * s]; t5 = p[2 * s] - p[5 * s]; t3 = p[3 * s] + p[4 * s]; t4 = p[3 * s] - p[4 * s];
    t10 = t0 + t3; t13 = t0 - t3; t11 = t1 + t2; t12 = t1 - t2;
    if (!col) { p[0] = (t10 + t11) * (1 << P1); p[4 * s] = (t10 - t11) * (1 << P1); }
    else { p[0] = DESC(t10 + t11, P1); p[4 * s] = DESC(t10 - t11, P1); }
    z1 = (t12 + t13) * 4433;
    p[2 * s] = DESC(z1 + t13 * 6270, col ? CB + P1 : CB - P1);
    p[6 * s] = DESC(z1 - t12 * 15137, col ? CB + P1 : CB - P1);
    z1 = t4 + t7; z2 = t5 + t6; z3 = t4 + t6; z4 = t5 + t7;
    z5 = (z3 + z4) * 9633;
    t4 *= 2446; t5 *= 16819; t6 *= 25172; t7 *= 12299;
    z1 *= -7373; z2 *= -20995; z3 *= -16069; z4 *= -3196;
    z3 += z5; z4 += z5;
    p[7 * s] = DESC(t4 + z1 + z3, col ? CB + P1 : CB - P1);
    p[5 * s] = DESC(t5 + z2 + z4, col ? CB + P1 : CB - P1);
    p[3 * s] = DESC(t6 + z2 + z3, col ? CB + P1 : CB - P1);
    p[s] = DESC(t7 + z1 + z4, col ? CB + P1 : CB - P1);
  }
}
/* the size category of a coefficient (the bits its magnitude needs) */
static int category(int v)
{
  int n = 0;
  if (v < 0) v = -v;
  while (v) { n++; v >>= 1; }
  return n;
}
/* transform, quantize and Huffman-code one block of level-shifted samples for component c
   (table t: 0 luminance, 1 chrominance) */
static void block(Enc *e, long *blk, int c, int t)
{
  int q[64], k, run, diff, cat;
  fdct(blk);
  for (k = 0; k < 64; k++) {
    long v = blk[zigzag[k]], d = e->q[t][zigzag[k]];
    q[k] = (int)(v < 0 ? -((-v + d / 2) / d) : (v + d / 2) / d);
  }
  diff = q[0] - e->pred[c]; e->pred[c] = q[0];
  cat = category(diff);
  putbits(e, e->dc[t].code[cat], e->dc[t].size[cat]);
  if (cat) putbits(e, (u32)(diff < 0 ? diff - 1 : diff), cat);
  for (run = 0, k = 1; k < 64; k++) {
    int v = q[k], sym;
    if (!v) { run++; continue; }
    while (run > 15) { putbits(e, e->ac[t].code[0xF0], e->ac[t].size[0xF0]); run -= 16; }
    cat = category(v);
    sym = (run << 4) | cat;
    putbits(e, e->ac[t].code[sym], e->ac[t].size[sym]);
    putbits(e, (u32)(v < 0 ? v - 1 : v), cat);
    run = 0;
  }
  if (run) putbits(e, e->ac[t].code[0], e->ac[t].size[0]);
}
static void put16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
/* a DHT table: class/id, counts, symbols */
static void dht(Buf *b, int tc, const u8 *bits, const u8 *vals)
{
  int i, n = 0;
  buf_addc(b, tc);
  for (i = 0; i < 16; i++) { buf_addc(b, bits[i]); n += bits[i]; }
  buf_add(b, vals, (size_t)n);
}

/* the picture's samples over white, as Y, Cb and Cr minus 128, for an mcu x mcu square at x0, y0
   (edges repeated past the picture's right and bottom) */
static void mcu_samples(const Rgba *im, i32 x0, i32 y0, int mcu, long *Y, long *Cb, long *Cr)
{
  int x, y;
  for (y = 0; y < mcu; y++) {
    i32 sy = y0 + y < im->h ? y0 + y : im->h - 1;
    for (x = 0; x < mcu; x++) {
      i32 sx = x0 + x < im->w ? x0 + x : im->w - 1;
      const u8 *p = im->px + ((long)sy * im->w + sx) * 4;
      long r = p[0], g = p[1], b = p[2], a = p[3];
      if (a != 255) { r = (r * a + 255 * (255 - a)) / 255; g = (g * a + 255 * (255 - a)) / 255; b = (b * a + 255 * (255 - a)) / 255; }
      Y[y * mcu + x] = ((19595 * r + 38470 * g + 7471 * b + 32768) >> 16) - 128;
      Cb[y * mcu + x] = (-11059 * r - 21709 * g + 32768 * b + 32768) >> 16;
      Cr[y * mcu + x] = (32768 * r - 27439 * g - 5329 * b + 32768) >> 16;
    }
  }
}

/* the markers (SOI, APP0 JFIF, DQT, SOF0, DHT, SOS), the entropy-coded macroblocks, EOI */
u8 *jpeg_encode(const Rgba *im, int quality, int dpi, size_t *len)
{
  Enc *e;
  u8 qt[2][64];
  int sub = quality < 90, mcu = sub ? 16 : 8, i, t, scale;
  i32 mx, my;
  long *Y, *Cb, *Cr, blk[64];
  u8 *res;
  if (!im->px || im->w < 1 || im->h < 1 || im->w > 65535 || im->h > 65535) return NULL;
  if (!(e = (Enc *)calloc(1, sizeof *e))) return NULL;
  if (!(Y = (long *)malloc(sizeof(long) * 256 * 3))) { free(e); return NULL; }
  Cb = Y + 256; Cr = Y + 512;
  /* IJG quality scaling, clamped to baseline's 8-bit quantizers */
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;
  scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
  for (t = 0; t < 2; t++)
    for (i = 0; i < 64; i++) {
      long v = ((long)(t ? std_chr_q[i] : std_lum_q[i]) * scale + 50) / 100;
      qt[t][i] = (u8)(v < 1 ? 1 : v > 255 ? 255 : v);
      e->q[t][i] = qt[t][i] * 8;
    }
  huff_build(&e->dc[0], dc_lum_bits, dc_vals); huff_build(&e->ac[0], ac_lum_bits, ac_lum_vals);
  huff_build(&e->dc[1], dc_chr_bits, dc_vals); huff_build(&e->ac[1], ac_chr_bits, ac_chr_vals);
  buf_init(&e->o);
  /* SOI, JFIF 1.01 with the density in dots per inch (or just square pixels) */
  buf_add(&e->o, "\377\330\377\340\000\020JFIF\000\001\001", 13);
  buf_addc(&e->o, dpi > 0 ? 1 : 0);
  put16(&e->o, (u32)(dpi > 0 && dpi < 65536 ? dpi : 1)); put16(&e->o, (u32)(dpi > 0 && dpi < 65536 ? dpi : 1));
  buf_addc(&e->o, 0); buf_addc(&e->o, 0);
  /* both quantization tables, in zigzag order */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xDB); put16(&e->o, 2 + 65 * 2);
  for (t = 0; t < 2; t++) { buf_addc(&e->o, t); for (i = 0; i < 64; i++) buf_addc(&e->o, qt[t][zigzag[i]]); }
  /* the frame: 8-bit, three components; Y sampled 2x2 for 4:2:0 */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xC0); put16(&e->o, 17); buf_addc(&e->o, 8);
  put16(&e->o, (u32)im->h); put16(&e->o, (u32)im->w); buf_addc(&e->o, 3);
  buf_addc(&e->o, 1); buf_addc(&e->o, sub ? 0x22 : 0x11); buf_addc(&e->o, 0);
  buf_addc(&e->o, 2); buf_addc(&e->o, 0x11); buf_addc(&e->o, 1);
  buf_addc(&e->o, 3); buf_addc(&e->o, 0x11); buf_addc(&e->o, 1);
  /* the four Huffman tables */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xC4); put16(&e->o, 2 + 4 * 17 + 12 * 2 + 162 * 2);
  dht(&e->o, 0x00, dc_lum_bits, dc_vals); dht(&e->o, 0x10, ac_lum_bits, ac_lum_vals);
  dht(&e->o, 0x01, dc_chr_bits, dc_vals); dht(&e->o, 0x11, ac_chr_bits, ac_chr_vals);
  /* the scan: all three components, the whole spectrum */
  buf_add(&e->o, "\377\332\000\014\003\001\000\002\021\003\021\000\077\000", 14);
  for (my = 0; my < im->h; my += mcu)
    for (mx = 0; mx < im->w; mx += mcu) {
      int bx, by, x, y;
      mcu_samples(im, mx, my, mcu, Y, Cb, Cr);
      for (by = 0; by < mcu; by += 8)
        for (bx = 0; bx < mcu; bx += 8) {
          for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) blk[y * 8 + x] = Y[(by + y) * mcu + bx + x];
          block(e, blk, 0, 0);
        }
      for (t = 0; t < 2; t++) {
        long *c = t ? Cr : Cb;
        if (sub) for (y = 0; y < 8; y++) for (x = 0; x < 8; x++)
          blk[y * 8 + x] = (c[(2 * y) * 16 + 2 * x] + c[(2 * y) * 16 + 2 * x + 1] + c[(2 * y + 1) * 16 + 2 * x] + c[(2 * y + 1) * 16 + 2 * x + 1] + 2) >> 2;
        else memcpy(blk, c, sizeof blk);
        block(e, blk, 1 + t, 1);
      }
    }
  if (e->nbits) putbits(e, 0x7F, 8 - e->nbits);       /* pad the last byte with 1 bits */
  buf_addc(&e->o, 0xFF); buf_addc(&e->o, 0xD9);
  free(Y);
  if (e->o.oom) { buf_free(&e->o); free(e); return NULL; }
  *len = e->o.len;
  res = (u8 *)buf_take(&e->o);
  free(e);
  return res;
}

/* ---- Quill's own: which of the three a file is, its size from its header, and decoding it */
int picfmt_info(const u8 *d, u32 n, u32 *w, u32 *h)
{
  *w = *h = 0;
  if (n >= 13 && (!memcmp(d, "GIF87a", 6) || !memcmp(d, "GIF89a", 6))) { *w = le16(d + 6); *h = le16(d + 8); return *w && *h ? IMG_GIF : IMG_NONE; }
  if (n >= 26 && d[0] == 'B' && d[1] == 'M') {
    u32 hs = le32(d + 14);
    if (hs == 12) { *w = le16(d + 18); *h = le16(d + 20); }
    else if (hs >= 40 && n >= 54) { u32 uh = le32(d + 22); *w = le32(d + 18); *h = uh & 0x80000000UL ? 0 - uh : uh; }
    return *w && *h && *w <= MAXDIM && *h <= MAXDIM ? IMG_BMP : IMG_NONE;
  }
  if (n >= 12 && !memcmp(d, "FORM", 4) && (!memcmp(d + 8, "ILBM", 4) || !memcmp(d + 8, "PBM ", 4))) {
    u32 o = 12;
    while (o + 8 <= n) {
      u32 len = be32(d + o + 4);
      if (!memcmp(d + o, "BMHD", 4) && o + 12 <= n) { *w = be16(d + o + 8); *h = be16(d + o + 10); return *w && *h ? IMG_ILBM : IMG_NONE; }
      if (!memcmp(d + o, "BODY", 4) || len > n) break;
      o += 8 + len + (len & 1);
    }
  }
  return IMG_NONE;
}
int picfmt_rgba(const u8 *d, u32 n, Rgba *out)
{
  u32 w, h;
  switch (picfmt_info(d, n, &w, &h)) {
  case IMG_GIF: return gif_read(d, n, out, NULL, 0);
  case IMG_BMP: return bmp_read(d, n, out, NULL, 0);
  case IMG_ILBM: return ilbm_read(d, n, out, NULL, 0);
  }
  out->w = out->h = 0; out->px = NULL;
  return 0;
}
