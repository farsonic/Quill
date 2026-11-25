/* core/rowpack.c - rows of RGB pixels run-length packed (see rowpack.h) */
#include <stdlib.h>
#include <string.h>
#include "rowpack.h"

static int same_px(const u8 *a, const u8 *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

/* one row of w pixels packed into out (room for w * 3 + w / 128 + 1 bytes); its length */
static long pack_row(const u8 *p, u32 w, u8 *out)
{
  u32 i = 0, j;
  long n = 0;
  while (i < w) {
    for (j = i + 1; j < w && j - i < 129 && same_px(p + 3 * j, p + 3 * i); j++) ;
    if (j - i >= 2) {
      out[n] = (u8)(126 + (j - i));
      memcpy(out + n + 1, p + 3 * i, 3);
      n += 4;
    } else {
      for (j = i + 1; j < w && j - i < 128 && !(j + 1 < w && same_px(p + 3 * j, p + 3 * (j + 1))); j++) ;
      out[n] = (u8)(j - i - 1);
      memcpy(out + n + 1, p + 3 * i, (size_t)(3 * (j - i)));
      n += 1 + 3 * (long)(j - i);
    }
    i = j;
  }
  return n;
}

int rgb_pack(PackedRGB *p, const u8 *rgb, u32 w, u32 h)
{
  size_t cap = 65536, len = 0, worst = (size_t)w * 3 + w / 128 + 2;
  u8 *out = (u8 *)malloc(cap), *row = (u8 *)malloc(worst);
  u32 *at = (u32 *)malloc(((size_t)h + 1) * sizeof *at), y;
  memset(p, 0, sizeof *p);
  if (!out || !row || !at) goto fail;
  for (y = 0; y < h; y++) {
    size_t n = (size_t)pack_row(rgb + (size_t)y * w * 3, w, row);
    if (len + n > cap) {
      u8 *bigger;
      while (len + n > cap) cap *= 2;
      if (!(bigger = (u8 *)realloc(out, cap))) goto fail;
      out = bigger;
    }
    at[y] = (u32)len;
    memcpy(out + len, row, n);
    len += n;
  }
  at[h] = (u32)len;
  free(row);
  p->data = out; p->rowat = at; p->w = w; p->h = h;
  return 1;
fail:
  free(out); free(row); free(at);
  return 0;
}

void rgb_unpack_row(const PackedRGB *p, u32 y, u8 *out)
{
  const u8 *s = p->data + p->rowat[y], *e = p->data + p->rowat[y + 1];
  while (s < e) {
    unsigned n = *s++;
    if (n < 128) {
      memcpy(out, s, 3 * (n + 1));
      out += 3 * (n + 1); s += 3 * (n + 1);
    } else {
      unsigned k = n - 126;
      while (k--) { out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out += 3; }
      s += 3;
    }
  }
}

void rgb_unpack_span(const PackedRGB *p, u32 y, u32 x0, u32 x1, u8 *out)
{
  const u8 *s = p->data + p->rowat[y], *e = p->data + p->rowat[y + 1];
  u32 x = 0;
  while (s < e && x <= x1) {
    unsigned n = *s++, k, i;
    if (n < 128) {
      k = n + 1;
      for (i = 0; i < k; i++, x++)
        if (x >= x0 && x <= x1) { out[0] = s[3 * i]; out[1] = s[3 * i + 1]; out[2] = s[3 * i + 2]; out += 3; }
      s += 3 * k;
    } else {
      k = n - 126;
      if (x + k > x0)
        for (i = x < x0 ? x0 - x : 0; i < k && x + i <= x1; i++) { out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out += 3; }
      x += k;
      s += 3;
    }
  }
}

int rgb_same_row(const PackedRGB *a, const PackedRGB *b, u32 y)
{
  u32 la = a->rowat[y + 1] - a->rowat[y], lb = b->rowat[y + 1] - b->rowat[y];
  return la == lb && !memcmp(a->data + a->rowat[y], b->data + b->rowat[y], la);
}

size_t rgb_pack_size(const PackedRGB *p)
{
  return p->data ? p->rowat[p->h] + ((size_t)p->h + 1) * sizeof *p->rowat : 0;
}

void rgb_pack_free(PackedRGB *p)
{
  free(p->data); free(p->rowat);
  memset(p, 0, sizeof *p);
}
