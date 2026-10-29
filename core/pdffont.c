/*
 * pdffont.c - font subsets for PDF export (WP29, see pdf.h): a font file cut down to the glyphs a
 * document uses, so a PDF carries kilobytes of each font instead of the whole file.
 *
 * TrueType outlines: a new glyf table holding only the glyphs used (and the parts composite glyphs
 * are built from), with a long loca pointing every other glyph at nothing. Glyph ids stay as they
 * were, so the text can name glyphs by their ids (Identity-H, /CIDToGIDMap /Identity). The tables a
 * PDF reader needs are kept (head, hhea, hmtx, maxp, cvt, fpgm, prep); cmap, names, kerning and the
 * OpenType layout tables are left out, as the PDF says which glyph to draw itself.
 *
 * CFF outlines: the CharStrings INDEX is written again with every glyph not used reduced to a lone
 * endchar, in the same place (it is never longer), the rest of its old room zeroed. Every other
 * offset in the CFF stays right, and the zeros cost next to nothing once the stream is deflated.
 * The subroutines are kept whole. The OpenType wrapper keeps CFF, head, hhea, hmtx, maxp, OS/2,
 * cmap, name and post.
 *
 * Fonts are hostile input like documents: every offset is checked against the file's length.
 */
#include <stdlib.h>
#include <string.h>
#include "pdf.h"

static u32 rd16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void wr16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void wr32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

/* a table of an sfnt: its tag, where it is, how long; 0 if absent or out of bounds */
static int sfnt_table(const u8 *f, size_t len, const char *tag, u32 *off, u32 *n)
{
  u32 nt, i;
  if (len < 12) return 0;
  nt = rd16(f + 4);
  if (12 + 16 * (size_t)nt > len) return 0;
  for (i = 0; i < nt; i++) {
    const u8 *e = f + 12 + 16 * i;
    if (!memcmp(e, tag, 4)) {
      u32 o = rd32(e + 8), l = rd32(e + 12);
      if (o > len || l > len - o) return 0;
      *off = o; *n = l;
      return 1;
    }
  }
  return 0;
}

typedef struct { char tag[5]; const u8 *data; u32 len; } STab;
/* an sfnt file of the tables given (sorted by tag here), with their checksums and head's
   checkSumAdjustment; malloc'd, NULL if out of memory */
static u8 *sfnt_build(STab *t, int n, u32 version, size_t *outlen)
{
  size_t total = 12 + 16 * (size_t)n, at;
  int i, j, sel = 0, headi = -1;
  u8 *o;
  u32 sum = 0;
  for (i = 1; i < n; i++)             /* by tag (insertion sort: a dozen at most) */
    for (j = i; j > 0 && memcmp(t[j - 1].tag, t[j].tag, 4) > 0; j--) { STab x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
  for (i = 0; i < n; i++) total += (t[i].len + 3) & ~3u;
  if (!(o = (u8 *)calloc(total, 1))) return NULL;
  wr32(o, version); wr16(o + 4, (u32)n);
  while ((2 << sel) <= n) sel++;
  wr16(o + 6, (u32)(16 << sel)); wr16(o + 8, (u32)sel); wr16(o + 10, (u32)(n * 16 - (16 << sel)));
  at = 12 + 16 * (size_t)n;
  for (i = 0; i < n; i++) {
    u8 *e = o + 12 + 16 * i;
    u32 cs = 0, k;
    memcpy(o + at, t[i].data, t[i].len);
    if (!memcmp(t[i].tag, "head", 4) && t[i].len >= 12) { wr32(o + at + 8, 0); headi = (int)at; }
    for (k = 0; k < ((t[i].len + 3) & ~3u); k += 4) cs += rd32(o + at + k);
    memcpy(e, t[i].tag, 4); wr32(e + 4, cs); wr32(e + 8, (u32)at); wr32(e + 12, t[i].len);
    at += (t[i].len + 3) & ~3u;
  }
  for (at = 0; at < total; at += 4) sum += rd32(o + at);
  if (headi >= 0) wr32(o + headi + 8, 0xB1B0AFBAUL - sum);
  *outlen = total;
  return o;
}

#define USED(u, g) ((u)[(g) >> 3] & (1 << ((g) & 7)))
#define SETUSED(u, g) ((u)[(g) >> 3] |= (u8)(1 << ((g) & 7)))

/* TrueType: see the top of the file */
static u8 *subset_glyf(const u8 *f, size_t len, const u8 *used0, u32 ng, size_t *outlen)
{
  u32 ho, hl, lo, ll, go, gl, mo, ml, o, l, g, nglyphs, total = 0;
  int longloca, changed, i, n = 0;
  u8 *used = NULL, *glyf = NULL, *loca = NULL, *head = NULL, *res = NULL;
  STab t[12];
  static const char *const keep[] = { "hhea", "hmtx", "cvt ", "fpgm", "prep" };
  if (!sfnt_table(f, len, "head", &ho, &hl) || hl < 54 || !sfnt_table(f, len, "loca", &lo, &ll) || !sfnt_table(f, len, "glyf", &go, &gl) ||
      !sfnt_table(f, len, "maxp", &mo, &ml) || ml < 6)
    return NULL;
  nglyphs = rd16(f + mo + 4);
  longloca = rd16(f + ho + 50) != 0;
  if ((longloca ? 4 : 2) * ((size_t)nglyphs + 1) > ll) return NULL;
  if (!(used = (u8 *)calloc((nglyphs + 8) / 8, 1))) return NULL;
  for (g = 0; g < nglyphs && g < ng; g++) if (USED(used0, g)) SETUSED(used, g);
  SETUSED(used, 0);                                  /* .notdef, always */
#define GOFF(k) (longloca ? rd32(f + lo + 4 * (k)) : rd16(f + lo + 2 * (k)) * 2)
  /* the glyphs composite glyphs are made of (and theirs: until nothing new turns up) */
  do {
    changed = 0;
    for (g = 0; g < nglyphs; g++) {
      u32 a = GOFF(g), b = GOFF(g + 1), p;
      if (!USED(used, g) || b <= a || b > gl || b - a < 10 || (i16)rd16(f + go + a) >= 0) continue;
      p = a + 10;
      for (;;) {
        u32 flags, sub;
        if (p + 4 > b) break;
        flags = rd16(f + go + p); sub = rd16(f + go + p + 2);
        if (sub < nglyphs && !USED(used, sub)) { SETUSED(used, sub); changed = 1; }
        p += 4 + (flags & 1 ? 4 : 2) + (flags & 8 ? 2 : flags & 0x40 ? 4 : flags & 0x80 ? 8 : 0);
        if (!(flags & 0x20)) break;
      }
    }
  } while (changed);
  for (g = 0; g < nglyphs; g++) {
    u32 a = GOFF(g), b = GOFF(g + 1);
    if (USED(used, g) && b > a && b <= gl) total += (b - a + 3) & ~3u;
  }
  if (!(glyf = (u8 *)calloc(total ? total : 4, 1)) || !(loca = (u8 *)malloc(4 * ((size_t)nglyphs + 1))) || !(head = (u8 *)malloc(hl))) goto done;
  total = 0;
  for (g = 0; g < nglyphs; g++) {
    u32 a = GOFF(g), b = GOFF(g + 1);
    wr32(loca + 4 * g, total);
    if (USED(used, g) && b > a && b <= gl) { memcpy(glyf + total, f + go + a, b - a); total += (b - a + 3) & ~3u; }
  }
  wr32(loca + 4 * nglyphs, total);
#undef GOFF
  memcpy(head, f + ho, hl);
  wr16(head + 50, 1);                                /* long offsets */
  strcpy(t[n].tag, "head"); t[n].data = head; t[n].len = hl; n++;
  strcpy(t[n].tag, "loca"); t[n].data = loca; t[n].len = 4 * (nglyphs + 1); n++;
  strcpy(t[n].tag, "glyf"); t[n].data = glyf; t[n].len = total; n++;
  strcpy(t[n].tag, "maxp"); t[n].data = f + mo; t[n].len = ml; n++;
  for (i = 0; i < (int)(sizeof keep / sizeof keep[0]); i++)
    if (sfnt_table(f, len, keep[i], &o, &l)) { memcpy(t[n].tag, keep[i], 5); t[n].data = f + o; t[n].len = l; n++; }
  res = sfnt_build(t, n, 0x00010000UL, outlen);
done:
  free(used); free(glyf); free(loca); free(head);
  return res;
}

/* ---- CFF */
/* the end of the INDEX at off (count, offSize, offsets, data); 0 if damaged. With data/offs set:
   where its data starts (minus one, as the offsets count from 1) and its offset array */
static u32 cff_index(const u8 *c, u32 n, u32 off, u32 *count, u32 *osz, u32 *offs, u32 *data)
{
  u32 cnt, os, last, k;
  if (off + 2 > n) return 0;
  cnt = rd16(c + off);
  if (count) *count = cnt;
  if (!cnt) return off + 2;
  if (off + 3 > n) return 0;
  os = c[off + 2];
  if (os < 1 || os > 4 || off + 3 + (cnt + 1) * os > n) return 0;
  for (last = 0, k = 0; k < os; k++) last = last << 8 | c[off + 3 + cnt * os + k];
  if (osz) *osz = os;
  if (offs) *offs = off + 3;
  if (data) *data = off + 3 + (cnt + 1) * os - 1;
  if (off + 3 + (cnt + 1) * os - 1 + last > n) return 0;
  return off + 3 + (cnt + 1) * os - 1 + last;
}
static u32 cff_off(const u8 *c, u32 offs, u32 os, u32 k)
{
  u32 v = 0, i;
  for (i = 0; i < os; i++) v = v << 8 | c[offs + k * os + i];
  return v;
}
/* whether a DICT has an operator (op, or 12 op2 as 0x0C00 | op2), and in *val its last operand
   (-1 if it has none or it isn't an integer) */
static int cff_dict_op(const u8 *c, u32 a, u32 b, u32 op, long *val)
{
  long stack[48];
  int sp = 0;
  while (a < b) {
    u32 v = c[a];
    if (v <= 21) {
      u32 o = v;
      if (v == 12) { if (a + 1 >= b) break; o = 0x0C00 | c[a + 1]; a++; }
      a++;
      if (o == op) { *val = sp ? stack[sp - 1] : -1; return 1; }
      sp = 0;
      continue;
    }
    if (sp == 48) sp = 0;
    if (v == 28) { if (a + 3 > b) break; stack[sp++] = (i16)rd16(c + a + 1); a += 3; }
    else if (v == 29) { if (a + 5 > b) break; stack[sp++] = (long)(i32)rd32(c + a + 1); a += 5; }
    else if (v == 30) { a++; while (a < b && (c[a] & 15) != 15 && (c[a] >> 4) != 15) a++; a++; stack[sp++] = 0; }
    else if (v >= 32 && v <= 246) { stack[sp++] = (long)v - 139; a++; }
    else if (v >= 247 && v <= 250) { if (a + 2 > b) break; stack[sp++] = (long)(v - 247) * 256 + c[a + 1] + 108; a += 2; }
    else if (v >= 251 && v <= 254) { if (a + 2 > b) break; stack[sp++] = -(long)(v - 251) * 256 - c[a + 1] - 108; a += 2; }
    else a++;
  }
  *val = -1;
  return 0;
}
static long cff_dict_int(const u8 *c, u32 a, u32 b, u32 op)
{
  long v;
  cff_dict_op(c, a, b, op, &v);
  return v;
}
/* the CFF table's Top DICT (its first) and the CharStrings INDEX's place; 0 if damaged */
static int cff_top(const u8 *c, u32 n, u32 *ta, u32 *tb, u32 *cs)
{
  u32 p, cnt, os, offs, data;
  long v;
  if (n < 4) return 0;
  p = c[2];
  if (!(p = cff_index(c, n, p, NULL, NULL, NULL, NULL))) return 0;           /* Name INDEX */
  if (!cff_index(c, n, p, &cnt, &os, &offs, &data) || !cnt) return 0;      /* Top DICT INDEX */
  *ta = data + cff_off(c, offs, os, 0); *tb = data + cff_off(c, offs, os, 1);
  if (*ta > *tb || *tb > n) return 0;
  v = cff_dict_int(c, *ta, *tb, 17);
  if (v <= 0 || (u32)v >= n) return 0;
  *cs = (u32)v;
  return 1;
}

/* see pdf.h */
int cff_cid_map(const u8 *f, size_t len, u16 **gid2cid, u32 *n)
{
  u32 o, l, ta, tb, cs, cnt, k;
  const u8 *c;
  long cso;
  u16 *m;
  *gid2cid = NULL; *n = 0;
  if (!sfnt_table(f, len, "CFF ", &o, &l)) return 0;
  c = f + o;
  /* CID-keyed: its Top DICT has ROS (12 30) */
  if (!cff_top(c, l, &ta, &tb, &cs) || !cff_dict_op(c, ta, tb, 0x0C1E, &cso)) return 0;
  if (!cff_index(c, l, cs, &cnt, NULL, NULL, NULL) || !cnt) return 0;
  cso = cff_dict_int(c, ta, tb, 15);                 /* charset */
  if (!(m = (u16 *)calloc(cnt, sizeof(u16)))) return 0;
  if (cso > 2 && (u32)cso < l) {
    u32 p = (u32)cso + 1, g = 1;
    int fmt = c[cso];
    if (fmt == 0) { for (; g < cnt && p + 2 <= l; g++, p += 2) m[g] = (u16)rd16(c + p); }
    else if (fmt == 1 || fmt == 2) {
      while (g < cnt && p + (fmt == 1 ? 3u : 4u) <= l) {
        u32 first = rd16(c + p), left = fmt == 1 ? c[p + 2] : rd16(c + p + 2), i;
        for (i = 0; i <= left && g < cnt; i++) m[g++] = (u16)(first + i);
        p += fmt == 1 ? 3 : 4;
      }
    }
  } else for (k = 0; k < cnt; k++) m[k] = (u16)k;
  *gid2cid = m; *n = cnt;
  return 1;
}

/* CFF: see the top of the file */
static u8 *subset_cff(const u8 *f, size_t len, const u8 *used, u32 ng, size_t *outlen)
{
  u32 o, l, ta, tb, cs, cnt, os, offs, data, end, k, at, newlen;
  u8 *cff = NULL, *idx = NULL, *res = NULL;
  STab t[12];
  int n = 0, i;
  static const char *const keep[] = { "head", "hhea", "hmtx", "maxp", "OS/2", "cmap", "name", "post" };
  if (!sfnt_table(f, len, "CFF ", &o, &l)) return NULL;
  if (!cff_top(f + o, l, &ta, &tb, &cs) || !(end = cff_index(f + o, l, cs, &cnt, &os, &offs, &data)) || !cnt) return NULL;
  if (!(cff = (u8 *)malloc(l)) || !(idx = (u8 *)malloc(end - cs))) goto done;
  memcpy(cff, f + o, l);
  /* the new INDEX: same count and offset size, glyphs not used an endchar (14) each */
  wr16(idx, cnt); idx[2] = (u8)os;
  at = 3 + (cnt + 1) * os;
  for (k = 0; k <= cnt; k++) {
    u32 a, b, v = at - 3 - (cnt + 1) * os + 1, j;
    for (j = 0; j < os; j++) idx[3 + k * os + j] = (u8)(v >> (8 * (os - 1 - j)));
    if (k == cnt) break;
    a = data + cff_off(f + o, offs, os, k); b = data + cff_off(f + o, offs, os, k + 1);
    if (b < a || b > end) goto done;
    if (b == a) continue;
    if (k == 0 || (k < ng && USED(used, k))) { if (at + (b - a) > end - cs) goto done; memcpy(idx + at, f + o + a, b - a); at += b - a; }
    else { if (at + 1 > end - cs) goto done; idx[at++] = 14; }
  }
  newlen = at;
  memcpy(cff + cs, idx, newlen);
  memset(cff + cs + newlen, 0, end - cs - newlen);
  strcpy(t[n].tag, "CFF "); t[n].data = cff; t[n].len = l; n++;
  for (i = 0; i < (int)(sizeof keep / sizeof keep[0]); i++) {
    u32 to, tl;
    if (sfnt_table(f, len, keep[i], &to, &tl)) { memcpy(t[n].tag, keep[i], 5); t[n].data = f + to; t[n].len = tl; n++; }
  }
  res = sfnt_build(t, n, rd32(f), outlen);
done:
  free(cff); free(idx);
  return res;
}

/* see pdf.h */
u8 *font_subset(const u8 *f, size_t len, const u8 *used, u32 nglyphs, size_t *outlen)
{
  u32 o, l;
  if (len < 12) return NULL;
  if (sfnt_table(f, len, "CFF ", &o, &l)) return subset_cff(f, len, used, nglyphs, outlen);
  return subset_glyf(f, len, used, nglyphs, outlen);
}
