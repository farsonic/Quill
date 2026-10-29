/* util.c - see util.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "util.h"

/* an empty buffer: no memory until the first add, so a Buf on the stack costs nothing */
void buf_init(Buf *b) { b->data = NULL; b->len = b->cap = 0; b->oom = 0; }
/* frees the data and leaves the buffer empty and usable again (the oom flag is cleared too) */
void buf_free(Buf *b) { free(b->data); buf_init(b); }
/* room for extra more bytes plus the terminating NUL, doubling from 256. Returns 1 if there is
   room, 0 when out of memory; once b->oom is set every later add is silently dropped, so a
   long run of writes needs only one check of b->oom at the end */
int buf_reserve(Buf *b, size_t extra)
{
  size_t want = b->len + extra + 1;
  char *nd;
  if (b->oom) return 0;
  if (want <= b->cap) return 1;
  {
    size_t nc = b->cap ? b->cap : 256;
    while (nc < want) nc *= 2;
    if (!(nd = (char *)realloc(b->data, nc))) { b->oom = 1; return 0; }
    b->data = nd; b->cap = nc;
  }
  return 1;
}
/* appends n bytes (which may include NULs) and keeps the data NUL-terminated; nothing on oom */
void buf_add(Buf *b, const void *p, size_t n)
{
  if (!buf_reserve(b, n)) return;
  if (n) memcpy(b->data + b->len, p, n);
  b->len += n;
  b->data[b->len] = 0;
}
/* one byte, and a C string without its NUL */
void buf_addc(Buf *b, int c) { char ch = (char)c; buf_add(b, &ch, 1); }
void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
/* appends printf-style text of any length; short results go through a stack buffer, which
   saves a second formatting pass in the common case */
void buf_printf(Buf *b, const char *fmt, ...)
{
  char tmp[512];
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((size_t)n < sizeof tmp) { buf_adds(b, tmp); return; }
  /* longer: formatted again straight into the buffer */
  if (!buf_reserve(b, (size_t)n + 1)) return;
  va_start(ap, fmt);
  vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
  va_end(ap);
  b->len += (size_t)n;
}
/* hands the data to the caller (free() it) and empties the buffer. Never NULL for an empty
   buffer, which gets a fresh "" - only NULL if even that one byte cannot be had */
char *buf_take(Buf *b)
{
  char *d = b->data;
  if (!d) { d = (char *)malloc(1); if (d) d[0] = 0; }
  buf_init(b);
  return d;
}

/* strict decoding: overlong forms, surrogates, truncated or bad continuation bytes all fall
   back to returning the first byte as a Latin-1 character with *used = 1, so text from old
   8-bit documents still round-trips. n == 0 gives 0 */
u32 utf8_get(const char *s, size_t n, int *used)
{
  const u8 *p = (const u8 *)s;
  u32 c;
  int k, i;
  if (!n) { *used = 1; return 0; }
  c = p[0];
  if (c < 0x80) { *used = 1; return c; }
  if (c >= 0xC2 && c <= 0xDF) { k = 1; c &= 0x1F; }
  else if (c >= 0xE0 && c <= 0xEF) { k = 2; c &= 0x0F; }
  else if (c >= 0xF0 && c <= 0xF4) { k = 3; c &= 0x07; }
  else { *used = 1; return p[0]; }
  if ((size_t)k >= n) { *used = 1; return p[0]; }
  for (i = 1; i <= k; i++) {
    if ((p[i] & 0xC0) != 0x80) { *used = 1; return p[0]; }
    c = (c << 6) | (p[i] & 0x3F);
  }
  if ((k == 2 && c < 0x800) || (k == 3 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) { *used = 1; return p[0]; }
  *used = k + 1;
  return c;
}
/* writes 1-4 bytes at out (no NUL) and returns the count; values above U+10FFFF become U+FFFD.
   Surrogates are passed through unchecked */
int utf8_put(char *out, u32 c)
{
  u8 *o = (u8 *)out;
  if (c < 0x80) { o[0] = (u8)c; return 1; }
  if (c < 0x800) { o[0] = (u8)(0xC0 | (c >> 6)); o[1] = (u8)(0x80 | (c & 0x3F)); return 2; }
  if (c < 0x10000) { o[0] = (u8)(0xE0 | (c >> 12)); o[1] = (u8)(0x80 | ((c >> 6) & 0x3F)); o[2] = (u8)(0x80 | (c & 0x3F)); return 3; }
  if (c > 0x10FFFF) c = 0xFFFD;
  o[0] = (u8)(0xF0 | (c >> 18)); o[1] = (u8)(0x80 | ((c >> 12) & 0x3F)); o[2] = (u8)(0x80 | ((c >> 6) & 0x3F)); o[3] = (u8)(0x80 | (c & 0x3F));
  return 4;
}
/* the offset just past the character at off, never beyond len */
size_t utf8_next(const char *s, size_t len, size_t off)
{
  int used;
  if (off >= len) return len;
  utf8_get(s + off, len - off, &used);
  return off + used;
}
/* the start of the character ending at off; if the bytes before off do not form one valid
   sequence (a stray continuation byte), it steps back a single byte, matching utf8_get */
size_t utf8_prev(const char *s, size_t off)
{
  size_t o = off;
  if (!o) return 0;
  /* step back over continuation bytes, at most three, then check that they decode as one */
  do o--; while (o > 0 && off - o < 4 && ((u8)s[o] & 0xC0) == 0x80);
  {
    int used;
    utf8_get(s + o, off - o, &used);
    if (o + used != off) return off - 1;
  }
  return o;
}
/* Latin-1 for one code point: itself below 0x100, an ASCII look-alike for common typographic
   marks, 0x85 for an ellipsis (callers turn it into "..."), or -1 if nothing fits */
int latin1_of(u32 c)
{
  if (c < 0x100) return (int)c;
  switch (c) {
  case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
  case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
  case 0x2013: case 0x2014: case 0x2212: case 0x2010: case 0x2011: return '-';
  case 0x2026: return 0x85;          /* no ellipsis in Latin-1: callers expand it */
  case 0x2022: case 0x25CF: case 0x2219: return 0xB7;
  case 0x20AC: return 'E';
  case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: return ' ';
  case 0x2122: return 'T';
  }
  return -1;
}
/* converts n bytes of UTF-8 into out for Amiga fonts and returns the bytes written. Output stops
   early rather than overflow (it keeps 3 bytes spare for an expanded ellipsis) and is
   NUL-terminated when there is room */
size_t utf8_to_latin1(const char *s, size_t n, char *out, size_t outcap)
{
  size_t i = 0, o = 0;
  while (i < n && o + 3 < outcap) {
    int used, l;
    u32 c = utf8_get(s + i, n - i, &used);
    i += used;
    if (c >= 0xF0000) continue;           /* Quill's references (pictures, notes, comment marks): nothing to show */
    l = latin1_of(c);
    if (l == 0x85) { out[o++] = '.'; out[o++] = '.'; out[o++] = '.'; continue; }
    out[o++] = (char)(l < 0 ? '?' : l);
  }
  if (o < outcap) out[o] = 0;
  return o;
}

/* malloc'd copies of a string, or of its first n bytes (always NUL-terminated); NULL when
   out of memory, and the caller frees */
char *str_dup(const char *s) { return str_ndup(s, strlen(s)); }
char *str_ndup(const char *s, size_t n)
{
  char *d = (char *)malloc(n + 1);
  if (!d) return NULL;
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}
/* 1 if equal ignoring ASCII case (only A-Z fold, so Latin-1 letters must match exactly), else 0 */
int str_ieq(const char *a, const char *b)
{
  for (;; a++, b++) {
    int x = (u8)*a, y = (u8)*b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return 0;
    if (!x) return 1;
  }
}

/* ------------------------------------------------------------------ */
/* angles */
static const u16 sin_deg[91] = {
  0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406, 3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138,
  6402, 6664, 6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087, 10311, 10531, 10749, 10963, 11174,
  11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466,
  14598, 14726, 14849, 14968, 15082, 15191, 15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182,
  16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382, 16384 };
/* sine for 0-900 tenths of a degree, times TRIG_ONE: the table, between its degrees in a
   straight line */
static i32 sin_q(int t)
{
  int d = t / 10, f = t % 10;
  return d >= 90 ? TRIG_ONE : sin_deg[d] + (i32)(sin_deg[d + 1] - sin_deg[d]) * f / 10;
}
/* sine of t tenths of a degree, times TRIG_ONE; any angle, negative ones too, folded into the
   first quadrant */
i32 isin(int t)
{
  t %= 3600;
  if (t < 0) t += 3600;
  if (t <= 900) return sin_q(t);
  if (t <= 1800) return sin_q(1800 - t);
  if (t <= 2700) return -sin_q(t - 1800);
  return -sin_q(3600 - t);
}
/* cosine of t tenths of a degree, times TRIG_ONE */
i32 icos(int t) { return isin(t + 900); }
/* direction of (x, y) in tenths of a degree, 0-3599, clockwise from 3 o'clock with y down;
   found by bisecting the sine table rather than with floating point. (0, 0) gives 0 */
int iatan2(i32 y, i32 x)
{
  long ax = x < 0 ? -(long)x : x, ay = y < 0 ? -(long)y : y;
  int lo = 0, hi = 900, a;
  if (!ax && !ay) return 0;
  /* the first quadrant's angle whose tangent is ay / ax: sin * ax against cos * ay, halving */
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if ((long long)sin_q(mid) * ax < (long long)sin_q(900 - mid) * ay) lo = mid + 1; else hi = mid;
  }
  a = lo;
  if (x < 0) a = 1800 - a;
  if (y < 0) a = 3600 - a;
  return a % 3600;
}
