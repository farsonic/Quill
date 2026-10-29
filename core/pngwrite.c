/*
 * pngwrite.c - pages as pictures (WP29, see pdf.h): PNG, deflated with core/zip.c's own deflate,
 * and IFF ILBM for Amiga programs (24 bit planes, each row of each plane packed with ByteRun1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "zip.h"
#include "docx.h"

static void be32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24) & 255); buf_addc(b, (int)(v >> 16) & 255); buf_addc(b, (int)(v >> 8) & 255); buf_addc(b, (int)v & 255); }
static void be16(Buf *b, u32 v) { buf_addc(b, (int)(v >> 8) & 255); buf_addc(b, (int)v & 255); }
/* a PNG chunk: length, type, data, CRC of type and data */
static void chunk(Buf *b, const char *type, const u8 *data, u32 n)
{
  u32 crc;
  be32(b, n);
  buf_add(b, type, 4);
  if (n) buf_add(b, data, n);
  crc = crc32_update(0, (const u8 *)type, 4);
  crc = crc32_update(crc, data, n);
  be32(b, crc);
}
/* see pdf.h. Each row is filtered (none, Sub or Up: whichever leaves the smallest sum of bytes,
   the usual guess at what deflates best), then the whole image deflated in the zlib wrapper */
int png_encode(Buf *out, const u8 *rgb, u32 w, u32 h, int dpi)
{
  static const u8 sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
  size_t stride = (size_t)w * 3, rowlen = stride + 1, clen = 0, i;
  u8 *f = (u8 *)malloc(rowlen * h), *c, hdr[13], phys[9], *z;
  u32 y, a = 1, bsum = 0, ppm = (u32)((dpi * 10000L + 127) / 254);
  if (!f) return 0;
  for (y = 0; y < h; y++) {
    const u8 *row = rgb + y * stride, *up = y ? row - stride : NULL;
    u8 *o = f + y * rowlen;
    u32 s0 = 0, s1 = 0, s2 = 0, x;
    for (x = 0; x < stride; x++) {
      u8 v1 = (u8)(row[x] - (x >= 3 ? row[x - 3] : 0)), v2 = (u8)(row[x] - (up ? up[x] : 0));
      s0 += row[x] < 128 ? row[x] : 256 - row[x];
      s1 += v1 < 128 ? v1 : 256 - v1;
      s2 += v2 < 128 ? v2 : 256 - v2;
    }
    if (s1 < s0 && s1 <= s2) { o[0] = 1; for (x = 0; x < stride; x++) o[1 + x] = (u8)(row[x] - (x >= 3 ? row[x - 3] : 0)); }
    else if (s2 < s0) { o[0] = 2; for (x = 0; x < stride; x++) o[1 + x] = (u8)(row[x] - up[x]); }
    else { o[0] = 0; memcpy(o + 1, row, stride); }
  }
  c = zdeflate(f, rowlen * h, &clen);
  if (!c) { free(f); return 0; }
  for (i = 0; i < rowlen * h; i++) { a = (a + f[i]) % 65521; bsum = (bsum + a) % 65521; }
  free(f);
  if (!(z = (u8 *)malloc(clen + 6))) { free(c); return 0; }
  z[0] = 0x78; z[1] = 0x01;
  memcpy(z + 2, c, clen);
  z[clen + 2] = (u8)(bsum >> 8); z[clen + 3] = (u8)bsum; z[clen + 4] = (u8)(a >> 8); z[clen + 5] = (u8)a;
  free(c);
  buf_add(out, sig, 8);
  hdr[0] = (u8)(w >> 24); hdr[1] = (u8)(w >> 16); hdr[2] = (u8)(w >> 8); hdr[3] = (u8)w;
  hdr[4] = (u8)(h >> 24); hdr[5] = (u8)(h >> 16); hdr[6] = (u8)(h >> 8); hdr[7] = (u8)h;
  hdr[8] = 8; hdr[9] = 2; hdr[10] = 0; hdr[11] = 0; hdr[12] = 0;      /* 8 bits, RGB, deflate, filters, not interlaced */
  chunk(out, "IHDR", hdr, 13);
  phys[0] = phys[4] = (u8)(ppm >> 24); phys[1] = phys[5] = (u8)(ppm >> 16); phys[2] = phys[6] = (u8)(ppm >> 8); phys[3] = phys[7] = (u8)ppm; phys[8] = 1;
  chunk(out, "pHYs", phys, 9);
  chunk(out, "IDAT", z, (u32)(clen + 6));
  chunk(out, "IEND", NULL, 0);
  free(z);
  return !out->oom;
}
/* ByteRun1: runs of three or more equal bytes as (1 - n, byte), the rest literally (n - 1, bytes) */
static void byterun1(Buf *b, const u8 *s, u32 n)
{
  u32 i = 0;
  while (i < n) {
    u32 r = 1;
    while (i + r < n && r < 128 && s[i + r] == s[i]) r++;
    if (r >= 3) { buf_addc(b, (int)(257 - r) & 255); buf_addc(b, s[i]); i += r; continue; }
    {
      u32 j = i, k;
      while (j < n && j - i < 128) {
        if (j + 2 < n && s[j] == s[j + 1] && s[j] == s[j + 2]) break;
        j++;
      }
      buf_addc(b, (int)(j - i - 1));
      for (k = i; k < j; k++) buf_addc(b, s[k]);
      i = j;
    }
  }
}
/* see pdf.h: FORM ILBM with BMHD (24 planes, ByteRun1) and BODY: each row as 24 plane rows, red's
   eight bits lowest first, then green's, then blue's (the deep ILBM Amiga programs read) */
int ilbm_encode(Buf *out, const u8 *rgb, u32 w, u32 h, int dpi)
{
  u32 rb = ((w + 15) / 16) * 2, y, x, pl;
  u8 *plane = (u8 *)malloc(rb);
  size_t form, body, end;
  if (!plane) return 0;
  buf_adds(out, "FORM"); form = out->len; be32(out, 0); buf_adds(out, "ILBM");
  buf_adds(out, "BMHD"); be32(out, 20);
  be16(out, w); be16(out, h); be16(out, 0); be16(out, 0);
  buf_addc(out, 24); buf_addc(out, 0); buf_addc(out, 1); buf_addc(out, 0);       /* planes, no mask, ByteRun1, pad */
  be16(out, 0); buf_addc(out, 1); buf_addc(out, 1);                            /* transparent colour, square pixels */
  be16(out, w); be16(out, h);
  /* the resolution, as the DPI chunk some programs read */
  buf_adds(out, "DPI "); be32(out, 4); be16(out, (u32)dpi); be16(out, (u32)dpi);
  buf_adds(out, "BODY"); body = out->len; be32(out, 0);
  for (y = 0; y < h; y++) {
    const u8 *row = rgb + (size_t)y * w * 3;
    for (pl = 0; pl < 24; pl++) {
      int ch = (int)(pl / 8), bit = (int)(pl % 8);
      memset(plane, 0, rb);
      for (x = 0; x < w; x++) if (row[x * 3 + ch] >> bit & 1) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
      byterun1(out, plane, rb);
    }
  }
  free(plane);
  if (out->oom) return 0;
  end = out->len;
  if ((end - body - 4) & 1) buf_addc(out, 0);
  {
    u32 bl = (u32)(end - body - 4), fl = (u32)(out->len - form - 4);
    out->data[body] = (char)(bl >> 24); out->data[body + 1] = (char)(bl >> 16); out->data[body + 2] = (char)(bl >> 8); out->data[body + 3] = (char)bl;
    out->data[form] = (char)(fl >> 24); out->data[form + 1] = (char)(fl >> 16); out->data[form + 2] = (char)(fl >> 8); out->data[form + 3] = (char)fl;
  }
  return !out->oom;
}
/* see pdf.h */
void page_image_name(const char *path, u32 page, char *out, size_t cap)
{
  const char *dot = strrchr(path, '.'), *sep = path, *q;
  char num[16];
  size_t stem;
  for (q = path; *q; q++) if (*q == '/' || *q == ':') sep = q + 1;
  if (!dot || dot < sep) dot = path + strlen(path);
  stem = (size_t)(dot - path);
  sprintf(num, "-%lu", (unsigned long)page);
  if (stem + strlen(num) + strlen(dot) + 1 > cap) { if (cap) { strncpy(out, path, cap - 1); out[cap - 1] = 0; } return; }
  memcpy(out, path, stem);
  strcpy(out + stem, num);
  strcat(out, dot);
}
/* see pdf.h */
int page_images_save(Layout *L, const char *path, const PdfOpts *o)
{
  u32 first, last, k;
  int n = 0;
  if (!o) o = &pdf_options;
  if (!lay_update(L) || !L->npages) return -1;
  first = o->first ? o->first - 1 : 0;
  last = o->last && o->last <= L->npages ? o->last - 1 : L->npages - 1;
  if (first > last) first = last;
  for (k = first; k <= last; k++) {
    u32 w, h;
    u8 *rgb = pdf_page_rgb(L, k, o->dpi ? o->dpi : 150, &w, &h);
    Buf b;
    char name[512];
    int ok;
    if (!rgb) return -1;
    buf_init(&b);
    ok = o->format == PIMG_ILBM ? ilbm_encode(&b, rgb, w, h, o->dpi) : png_encode(&b, rgb, w, h, o->dpi);
    free(rgb);
    if (first == last) { strncpy(name, path, sizeof name - 1); name[sizeof name - 1] = 0; }
    else page_image_name(path, k + 1, name, sizeof name);
    ok = ok && file_write(name, b.data, b.len);
    buf_free(&b);
    if (!ok) return -1;
    n++;
  }
  return n;
}
