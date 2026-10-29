/* fonts.c - see fonts.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fonts.h"
#include "docx.h"      /* file_read */

/* ------------------------------------------------------------------ */
/* TrueType: big-endian tables; every offset is checked against the file */
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static i32 sbe16(const u8 *p) { i32 v = (i32)be16(p); return v >= 32768 ? v - 65536 : v; }

/* read one table into memory (malloc'd); *len gets its length */
/* read one table into memory (malloc'd); *len gets its length. NULL if the tag is missing,
   lies outside the file, or is over 4MB (no table Quill reads is anywhere near that) */
static u8 *read_table(FILE *fp, const u8 *dir, int ntab, const char *tag, u32 *len, long fsize)
{
  int i;
  for (i = 0; i < ntab; i++) {
    const u8 *e = dir + 16 * i;
    if (!memcmp(e, tag, 4)) {
      u32 off = be32(e + 8), n = be32(e + 12);
      u8 *b;
      if (off > (u32)fsize || n > (u32)fsize - off || n > 4UL * 1024 * 1024) return NULL;
      if (!(b = (u8 *)malloc(n ? n : 1))) return NULL;
      if (fseek(fp, (long)off, SEEK_SET) || fread(b, 1, n, fp) != n) { free(b); return NULL; }
      *len = n;
      return b;
    }
  }
  return NULL;
}
/* fp is a FILE * (void * keeps stdio out of fonts.h). For a .ttc, follows the collection's
   offset table to face index; a plain file only has face 0. Accepts TrueType (0x00010000
   or Apple's 'true') and OpenType CFF ('OTTO') */
int sfnt_header(void *fpv, int index, u8 hdr[12], int *cff, long fsize)
{
  FILE *fp = (FILE *)fpv;
  u8 h[12], o[4];
  if (fseek(fp, 0, SEEK_SET) || fread(h, 1, 12, fp) != 12) return 0;
  if (!memcmp(h, "ttcf", 4)) {
    u32 n = be32(h + 8), off;
    if (index < 0 || (u32)index >= n || n > 1000) return 0;
    if (fseek(fp, 12 + 4L * index, SEEK_SET) || fread(o, 1, 4, fp) != 4) return 0;
    off = be32(o);
    if ((long)off > fsize - 12 || fseek(fp, (long)off, SEEK_SET) || fread(h, 1, 12, fp) != 12) return 0;
  } else if (index) return 0;
  if (be32(h) == 0x00010000UL || !memcmp(h, "true", 4)) *cff = 0;
  else if (!memcmp(h, "OTTO", 4)) *cff = 1;
  else return 0;
  memcpy(hdr, h, 12);
  return 1;
}
/* a collection's count is capped at 255, since Face.index is a u8 */
int fonts_count(const char *path)
{
  FILE *fp = fopen(path, "rb");
  u8 h[12];
  int n = 0, cff;
  long fsize;
  if (!fp) return 0;
  fseek(fp, 0, SEEK_END); fsize = ftell(fp);
  if (fseek(fp, 0, SEEK_SET) == 0 && fread(h, 1, 12, fp) == 12) {
    if (!memcmp(h, "ttcf", 4)) { n = (int)be32(h + 8); if (n > 255) n = 255; }
    else n = sfnt_header(fp, 0, h, &cff, fsize);
  }
  fclose(fp);
  return n;
}
/* sfnt tables start on 4-byte boundaries */
static u32 align4(u32 v) { return (v + 3) & ~3UL; }
/* for a .ttc face, builds a standalone sfnt: its header, a table directory with the offsets
   rewritten, and each table copied in padded to 4 bytes. Checksums are copied as they are
   (they cover only each table's own bytes, so they stay right); head's checkSumAdjustment
   is not recomputed */
u8 *fonts_face_file(const Face *f, size_t *len)
{
  FILE *fp;
  u8 hdr[12], *dir = NULL, *out = NULL, first[4];
  u32 ntab, i, total, at;
  long fsize;
  int cff;
  if (!(fp = fopen(f->path, "rb"))) return NULL;
  if (fread(first, 1, 4, fp) != 4) { fclose(fp); return NULL; }
  if (memcmp(first, "ttcf", 4)) { fclose(fp); return file_read(f->path, len); }       /* a face of its own already */
  fseek(fp, 0, SEEK_END); fsize = ftell(fp);
  if (!sfnt_header(fp, f->index, hdr, &cff, fsize)) goto done;
  ntab = be16(hdr + 4);
  if (!ntab || ntab > 64 || !(dir = (u8 *)malloc(16 * ntab)) || fread(dir, 16, ntab, fp) != ntab) goto done;
  for (i = 0, total = 12 + 16 * ntab; i < ntab; i++) {
    u32 off = be32(dir + 16 * i + 8), n = be32(dir + 16 * i + 12);
    if ((long)off > fsize || (long)n > fsize - (long)off) goto done;
    total += align4(n);
  }
  if (!(out = (u8 *)calloc(total, 1))) goto done;
  memcpy(out, hdr, 12);
  for (i = 0, at = 12 + 16 * ntab; i < ntab; i++) {
    u8 *e = out + 12 + 16 * i;
    u32 off = be32(dir + 16 * i + 8), n = be32(dir + 16 * i + 12);
    memcpy(e, dir + 16 * i, 8);                              /* the tag and its checksum */
    e[8] = (u8)(at >> 24); e[9] = (u8)(at >> 16); e[10] = (u8)(at >> 8); e[11] = (u8)at;
    memcpy(e + 12, dir + 16 * i + 12, 4);
    if (fseek(fp, (long)off, SEEK_SET) || fread(out + at, 1, n, fp) != n) { free(out); out = NULL; goto done; }
    at += align4(n);
  }
  *len = total;
done:
  free(dir);
  fclose(fp);
  return out;
}
/* fsType 2 is "restricted licence" (no embedding at all), bit 0x200 is "bitmap embedding only" */
int fonts_embeddable(const Face *f)
{
  /* (and not CFF2: PDF readers don't take variable fonts) */
  return (f->fstype & 0x000F) != 0x0002 && !(f->fstype & 0x0200) && f->cff != 2;
}
/* frees what ttf_load allocated; the face's name, path and flags stay, so it can load again */
static void face_drop(Face *f)
{
  free(f->adv); free(f->seg_end); free(f->seg_start); free(f->seg_off); free(f->seg_delta); free(f->glyphs);
  f->adv = NULL; f->seg_end = f->seg_start = f->seg_off = NULL; f->seg_delta = NULL; f->glyphs = NULL;
}
/* the format 4 subtable (Windows, Unicode BMP) of a cmap */
/* the format 4 subtable (Windows, Unicode BMP) of a cmap: Windows Unicode preferred, then a
   Unicode-platform one, then Windows Symbol. Copies the segment arrays into f (host order).
   0 if there is none or it is malformed; a partial allocation is left for face_drop to free */
static int load_cmap(Face *f, const u8 *c, u32 n)
{
  u32 i, nsub, best = 0;
  const u8 *t;
  u32 tl, segx2, k;
  if (n < 4) return 0;
  nsub = be16(c + 2);
  for (i = 0; i < nsub && 4 + 8 * i + 8 <= n; i++) {
    const u8 *e = c + 4 + 8 * i;
    u32 pid = be16(e), eid = be16(e + 2), off = be32(e + 4);
    if (off + 4 > n || be16(c + off) != 4) continue;
    if (pid == 3 && eid == 1) { best = off; break; }
    if (pid == 0 && !best) best = off;
    if (pid == 3 && eid == 0 && !best) best = off;            /* symbol fonts */
  }
  if (!best) return 0;
  t = c + best;
  tl = be16(t + 2);
  if (best + tl > n || tl < 16) tl = n - best;
  segx2 = be16(t + 6);
  if (segx2 & 1 || 16 + 4 * segx2 > tl) return 0;
  f->nseg = (u16)(segx2 / 2);
  f->seg_end = (u16 *)malloc(segx2); f->seg_start = (u16 *)malloc(segx2);
  f->seg_delta = (i16 *)malloc(segx2); f->seg_off = (u16 *)malloc(segx2);
  if (!f->seg_end || !f->seg_start || !f->seg_delta || !f->seg_off) return 0;
  for (k = 0; k < f->nseg; k++) {
    f->seg_end[k] = (u16)be16(t + 14 + 2 * k);
    f->seg_start[k] = (u16)be16(t + 16 + segx2 + 2 * k);
    f->seg_delta[k] = (i16)sbe16(t + 16 + 2 * segx2 + 2 * k);
    f->seg_off[k] = (u16)be16(t + 16 + 3 * segx2 + 2 * k);
  }
  /* the glyph id array follows the range offsets; an offset counts from its own place, so the
     index into the glyph array is k + off/2 + (cp - start) - nseg */
  {
    u32 from = 16 + 4 * segx2, cnt = tl > from ? (tl - from) / 2 : 0;
    f->nglyphids = cnt;
    if (!(f->glyphs = (u16 *)malloc((cnt ? cnt : 1) * 2))) return 0;
    for (k = 0; k < cnt; k++) f->glyphs[k] = (u16)be16(t + from + 2 * k);
  }
  return 1;
}
/* the glyph id for a BMP code point by binary search of the format 4 segments; 0 (.notdef)
   for anything unmapped or beyond U+FFFF. Deltas wrap modulo 65536 as the spec requires */
u16 ttf_glyph(Face *f, u32 cp)
{
  u32 lo = 0, hi = f->nseg;
  if (cp > 0xFFFF || !f->nseg) return 0;
  while (lo < hi) {                          /* the first segment whose end >= cp */
    u32 mid = (lo + hi) / 2;
    if (f->seg_end[mid] < cp) lo = mid + 1; else hi = mid;
  }
  if (lo >= f->nseg || f->seg_start[lo] > cp) return 0;
  if (!f->seg_off[lo]) return (u16)((cp + (u32)(u16)f->seg_delta[lo]) & 0xFFFF);
  {
    u32 idx = lo + f->seg_off[lo] / 2 + (cp - f->seg_start[lo]);
    u16 g;
    if (idx < f->nseg) return 0;
    idx -= f->nseg;
    if (idx >= f->nglyphids) return 0;
    g = f->glyphs[idx];
    return g ? (u16)((g + (u32)(u16)f->seg_delta[lo]) & 0xFFFF) : 0;
  }
}
/* loads the metrics and cmap on first use. Remembers a failure in f->failed so a damaged file
   is tried only once; returns 1 if the face is ready. Advances are clamped to 4 em so that
   width sums of a long line cannot overflow 32 bits */
int ttf_load(Face *f)
{
  FILE *fp;
  u8 hdr[12], *dir = NULL, *head = NULL, *hhea = NULL, *os2 = NULL, *hmtx = NULL, *cmap = NULL, *maxp = NULL;
  u32 ntab, lh, lhh, lo, lm, lc, lmx;
  long fsize;
  int ok = 0, i;
  if (f->loaded) return 1;
  if (f->failed) return 0;
  f->failed = 1;
  if (!(fp = fopen(f->path, "rb"))) return 0;
  fseek(fp, 0, SEEK_END); fsize = ftell(fp); fseek(fp, 0, SEEK_SET);
  {
    int cff;
    if (!sfnt_header(fp, f->index, hdr, &cff, fsize)) goto done;
    f->cff = (u8)cff;
  }
  ntab = be16(hdr + 4);
  if (!ntab || ntab > 64 || !(dir = (u8 *)malloc(16 * ntab)) || fread(dir, 16, ntab, fp) != ntab) goto done;
  if (f->cff) {
    /* CFF2 (a variable font) rather than CFF */
    u32 t;
    for (t = 0; t < ntab; t++) if (!memcmp(dir + 16 * t, "CFF2", 4)) f->cff = 2;
    for (t = 0; t < ntab; t++) if (!memcmp(dir + 16 * t, "CFF ", 4)) f->cff = 1;
  }
  head = read_table(fp, dir, (int)ntab, "head", &lh, fsize);
  hhea = read_table(fp, dir, (int)ntab, "hhea", &lhh, fsize);
  os2 = read_table(fp, dir, (int)ntab, "OS/2", &lo, fsize);
  hmtx = read_table(fp, dir, (int)ntab, "hmtx", &lm, fsize);
  cmap = read_table(fp, dir, (int)ntab, "cmap", &lc, fsize);
  maxp = read_table(fp, dir, (int)ntab, "maxp", &lmx, fsize);
  if (!head || lh < 54 || !hhea || lhh < 36 || !hmtx || !cmap || !maxp || lmx < 6) goto done;
  f->upm = (u16)be16(head + 18);
  if (f->upm < 16) goto done;
  f->nglyphs = (u16)be16(maxp + 4);
  f->nmetrics = (u16)be16(hhea + 34);
  if (!f->nmetrics || f->nmetrics > f->nglyphs || 4UL * f->nmetrics > lm) goto done;
  {
    /* Windows' metrics: the line is winAscent + winDescent plus whatever line gap hhea asks for
       beyond that (GDI's external leading). Word sets single spacing this way */
    i32 ha = sbe16(hhea + 4), hd = -sbe16(hhea + 6), hg = sbe16(hhea + 8);
    i32 wa = ha, wd = hd;
    if (os2 && lo >= 78) { wa = (i32)be16(os2 + 74); wd = (i32)be16(os2 + 76); }
    if (os2 && lo >= 10) f->fstype = (u16)be16(os2 + 8);
    f->ascent = wa; f->descent = wd;
    f->linegap = hg - ((wa + wd) - (ha + hd));
    if (f->linegap < 0) f->linegap = 0;
    /* Caladea has Cambria's widths but taller line metrics: use Cambria's (1946 + 455 of 2048) */
    if (str_ieq(f->family, "Caladea")) { f->ascent = (1946 * (i32)f->upm + 1024) / 2048; f->descent = (455 * (i32)f->upm + 1024) / 2048; f->linegap = 0; }
  }
  if (!(f->adv = (u16 *)malloc(2UL * f->nglyphs))) goto done;
  for (i = 0; i < f->nglyphs; i++) {
    u32 a = be16(hmtx + 4 * (i < f->nmetrics ? i : f->nmetrics - 1));
    f->adv[i] = (u16)(a > 4 * (u32)f->upm ? 4 * (u32)f->upm : a);     /* keeps the width sums in 32 bits */
  }
  if (!load_cmap(f, cmap, lc)) goto done;
  for (i = 0; i < 256; i++) f->latin_glyph[i] = ttf_glyph(f, (u32)i);
  ok = 1;
done:
  free(dir); free(head); free(hhea); free(os2); free(hmtx); free(cmap); free(maxp);
  fclose(fp);
  if (ok) { f->loaded = 1; f->failed = 0; }
  else face_drop(f);
  return ok;
}

/* a name record as UTF-8 (UTF-16BE for Windows' records, else bytes) */
static void name_text(const u8 *p, u32 n, int utf16, char *out, int size)
{
  int k = 0;
  u32 i;
  for (i = 0; i + (utf16 ? 1 : 0) < n && k < size - 4; i += utf16 ? 2 : 1) {
    u32 c = utf16 ? be16(p + i) : p[i];
    if (c < 32 || (c >= 0xD800 && c < 0xE000)) c = '?';
    if (c < 0x80) out[k++] = (char)c;
    else if (c < 0x800) { out[k++] = (char)(0xC0 | (c >> 6)); out[k++] = (char)(0x80 | (c & 63)); }
    else { out[k++] = (char)(0xE0 | (c >> 12)); out[k++] = (char)(0x80 | ((c >> 6) & 63)); out[k++] = (char)(0x80 | (c & 63)); }
  }
  while (k > 0 && out[k - 1] == ' ') k--;
  out[k] = 0;
}
/* bold and italic come from OS/2 fsSelection, or head's macStyle when there is no OS/2.
   Used when scanning FONTS: for the user's own fonts. family is written as UTF-8, at most
   famsize - 1 bytes; returns 0 if the file is not a font or has no family name */
int fonts_probe(const char *path, int index, char *family, int famsize, int *bold, int *italic)
{
  FILE *fp;
  u8 hdr[12], *dir = NULL, *name = NULL, *os2 = NULL, *head = NULL;
  u32 ntab, ln = 0, lo = 0, lh = 0, i;
  long fsize;
  int ok = 0, score = -1;
  family[0] = 0; *bold = *italic = 0;
  if (!(fp = fopen(path, "rb"))) return 0;
  fseek(fp, 0, SEEK_END); fsize = ftell(fp); fseek(fp, 0, SEEK_SET);
  {
    int cff;
    if (!sfnt_header(fp, index, hdr, &cff, fsize)) goto done;
  }
  ntab = be16(hdr + 4);
  if (!ntab || ntab > 64 || !(dir = (u8 *)malloc(16 * ntab)) || fread(dir, 16, ntab, fp) != ntab) goto done;
  name = read_table(fp, dir, (int)ntab, "name", &ln, fsize);
  os2 = read_table(fp, dir, (int)ntab, "OS/2", &lo, fsize);
  head = read_table(fp, dir, (int)ntab, "head", &lh, fsize);
  if (!name || ln < 6 || !head || lh < 54) goto done;
  {
    /* name ID 1 (the family as Word knows it): Windows' English record first, then any Windows
       one, then a Macintosh one */
    u32 cnt = be16(name + 2), strs = be16(name + 4);
    for (i = 0; i < cnt && 6 + 12 * i + 12 <= ln; i++) {
      const u8 *r = name + 6 + 12 * i;
      u32 pid = be16(r), eid = be16(r + 2), lang = be16(r + 4), nid = be16(r + 6), len = be16(r + 8), off = be16(r + 10);
      int sc;
      if (nid != 1 || strs + off + len > ln) continue;
      sc = pid == 3 && (eid == 1 || eid == 0) ? (lang == 0x409 ? 3 : 2) : pid == 1 && eid == 0 ? 1 : 0;
      if (sc > score) { score = sc; name_text(name + strs + off, len, pid == 3 || pid == 0, family, famsize); }
    }
  }
  if (!family[0]) goto done;
  if (os2 && lo >= 64) { u32 sel = be16(os2 + 62); *italic = (sel & 1) != 0; *bold = (sel & 32) != 0; }
  else { u32 mac = be16(head + 44); *bold = (mac & 1) != 0; *italic = (mac & 2) != 0; }
  ok = 1;
done:
  free(dir); free(name); free(os2); free(head);
  fclose(fp);
  return ok;
}

/* ------------------------------------------------------------------ */
/* the font set */
/* only records the face: nothing is read until layout first needs it (ttf_load) */
int fonts_add(FontSet *fs, const char *path, const char *family, int bold, int italic)
{
  Face *f;
  if (fs->nfaces >= (int)(sizeof fs->faces / sizeof fs->faces[0])) return -1;
  f = &fs->faces[fs->nfaces];
  memset(f, 0, sizeof *f);
  strncpy(f->family, family, sizeof f->family - 1);
  strncpy(f->path, path, sizeof f->path - 1);
  f->bold = (u8)(bold != 0); f->italic = (u8)(italic != 0);
  return fs->nfaces++;
}
/* reads dir/fonts.list (dir may end in '/' or an Amiga ':' or neither). Returns how many faces
   were listed; 0 if the list is missing. The files themselves are not opened yet */
int fonts_init(FontSet *fs, const char *dir)
{
  char path[300], line[300];
  FILE *fp;
  memset(fs, 0, sizeof *fs);
  strncpy(fs->dir, dir, sizeof fs->dir - 1);
  sprintf(path, "%.250s%sfonts.list", dir, (*dir && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != ':') ? "/" : "");
  if (!(fp = fopen(path, "r"))) return 0;
  while (fgets(line, sizeof line, fp)) {
    char *fld[4], *s = line;
    int n = 0;
    char *nl = strpbrk(line, "\r\n");
    if (nl) *nl = 0;
    if (line[0] == '#' || !line[0]) continue;
    while (n < 4) { fld[n++] = s; s = strchr(s, ';'); if (!s) break; *s++ = 0; }
    if (n < 2) continue;
    sprintf(path, "%.200s%s%.90s", dir, (*dir && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != ':') ? "/" : "", fld[0]);
    fonts_add(fs, path, fld[1], n > 2 && fld[2][0] == '1', n > 3 && fld[3][0] == '1');
  }
  fclose(fp);
  return fs->nfaces;
}
/* frees the size cache and every face's tables; fs can be filled again with fonts_add */
void fonts_free(FontSet *fs)
{
  int i;
  while (fs->sizes) { FaceSize *n = fs->sizes->next; free(fs->sizes); fs->sizes = n; }
  for (i = 0; i < fs->nfaces; i++) face_drop(&fs->faces[i]);
  fs->nfaces = 0;
}
/* is any face of this family (ignoring ASCII case) in the set? */
static int has_family(FontSet *fs, const char *family)
{
  int i;
  for (i = 0; i < fs->nfaces; i++) if (str_ieq(fs->faces[i].family, family)) return 1;
  return 0;
}
/* does hay contain needle, ignoring case? needle must be lower case already */
static int contains(const char *hay, const char *needle)
{
  size_t n = strlen(needle);
  for (; *hay; hay++) {
    size_t k;
    for (k = 0; k < n; k++) {
      int a = (u8)hay[k], b = (u8)needle[k];
      if (a >= 'A' && a <= 'Z') a += 32;
      if (a != b) break;
    }
    if (k == n) return 1;
  }
  return 0;
}
/* a family we have is used as it is; otherwise a fixed map from Microsoft's fonts to their
   metric-compatible stand-ins, then a guess from the name (mono, serif or sans), and finally
   the first face listed. The result points into fs, the static map or family itself */
const char *fonts_substitute(FontSet *fs, const char *family)
{
  static const char *const map[][2] = {
    { "Times New Roman", "Liberation Serif" }, { "Times", "Liberation Serif" },
    { "Arial", "Liberation Sans" }, { "Helvetica", "Liberation Sans" }, { "Arial Nova", "Liberation Sans" },
    { "Courier New", "Liberation Mono" }, { "Courier", "Liberation Mono" },
    { "Calibri", "Carlito" }, { "Calibri Light", "Carlito" }, { "Aptos", "Carlito" },
    { "Cambria", "Caladea" }, { "Georgia", "Liberation Serif" }, { "Garamond", "Liberation Serif" },
    { "Verdana", "Liberation Sans" }, { "Tahoma", "Liberation Sans" }, { "Segoe UI", "Liberation Sans" },
    { "Consolas", "Liberation Mono" }, { "Lucida Console", "Liberation Mono" },
  };
  size_t i;
  if (has_family(fs, family)) return family;
  for (i = 0; i < sizeof map / sizeof map[0]; i++)
    if (str_ieq(map[i][0], family) && has_family(fs, map[i][1])) return map[i][1];
  /* by its look */
  if ((contains(family, "mono") || contains(family, "courier") || contains(family, "code") || contains(family, "consol")) && has_family(fs, "Liberation Mono")) return "Liberation Mono";
  if ((contains(family, "serif") && !contains(family, "sans")) || contains(family, "times") || contains(family, "roman") ||
      contains(family, "book") || contains(family, "garamond") || contains(family, "georgia")) {
    if (has_family(fs, "Liberation Serif")) return "Liberation Serif";
  }
  if (has_family(fs, "Liberation Sans")) return "Liberation Sans";
  return fs->nfaces ? fs->faces[0].family : family;
}
/* the closest face of a family that hasn't failed to load: matching boldness counts for more
   than matching italics, so bold text in a family without bold italic stays bold */
static Face *pick(FontSet *fs, const char *family, int bold, int italic)
{
  int i, best = -1, bs = -1;
  for (i = 0; i < fs->nfaces; i++) {
    Face *f = &fs->faces[i];
    int sc;
    if (!str_ieq(f->family, family) || f->failed) continue;
    sc = (f->bold == (bold != 0)) * 2 + (f->italic == (italic != 0));
    if (sc > bs) { bs = sc; best = i; }
  }
  return best >= 0 ? &fs->faces[best] : NULL;
}
/* if every face of the family is damaged, falls back to any face that loads at all, so text
   always has some metrics */
Face *fonts_face(FontSet *fs, const char *family, int bold, int italic)
{
  Face *f;
  const char *fam = fonts_substitute(fs, family);
  int i;
  f = pick(fs, fam, bold, italic);
  while (f && !ttf_load(f)) f = pick(fs, fam, bold, italic);       /* a damaged file: the next best */
  for (i = 0; i < fs->nfaces && !f; i++) if (ttf_load(&fs->faces[i])) f = &fs->faces[i];
  return f;
}
/* qsort order for family names: ASCII case ignored */
static int by_family(const void *a, const void *b)
{
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  for (; *x && *y; x++, y++) {
    int p = (u8)*x, q = (u8)*y;
    if (p >= 'A' && p <= 'Z') p += 32;
    if (q >= 'A' && q <= 'Z') q += 32;
    if (p != q) return p - q;
  }
  return (u8)*x - (u8)*y;
}
/* for the font name list; stops collecting at max distinct families */
int fonts_families(FontSet *fs, const char **out, int max)
{
  int i, k, n = 0;
  for (i = 0; i < fs->nfaces && n < max; i++) {
    for (k = 0; k < n && !str_ieq(out[k], fs->faces[i].family); k++) ;
    if (k == n) out[n++] = fs->faces[i].family;
  }
  qsort(out, (size_t)n, sizeof *out, by_family);
  return n;
}
/* size 0 means 12 point, and sizes are capped at 1638 points. Results are cached in fs (and
   moved to the front on each hit); they live until fonts_free, so callers may keep the pointer */
FaceSize *fonts_size(FontSet *fs, const char *family, int bold, int italic, u16 size)
{
  FaceSize *z, *prev = NULL;
  Face *f = fonts_face(fs, family, bold, italic);
  int i;
  if (!f) return NULL;
  if (!size) size = 24;
  if (size > 3276) size = 3276;
  for (z = fs->sizes; z; prev = z, z = z->next)
    if (z->face == f && z->size == size) {
      if (prev) { prev->next = z->next; z->next = fs->sizes; fs->sizes = z; }   /* most recent first */
      return z;
    }
  if (!(z = (FaceSize *)malloc(sizeof *z))) return NULL;
  z->face = f; z->size = size;
  /* twips = units * halfpoints * 10 / upm; fine = 16 times that. Products stay below 2^32:
     advances < 65536 but in practice < 4096, sizes <= 3276 half-points */
  for (i = 0; i < 256; i++) {
    u32 a = f->latin_glyph[i] < f->nglyphs ? f->adv[f->latin_glyph[i]] : 0;
    z->fine[i] = (a * size * 10UL * QFINE + f->upm / 2) / f->upm;
  }
  z->ascent = (f->ascent * (i32)size * 10 + f->upm / 2) / f->upm;
  z->descent = (f->descent * (i32)size * 10 + f->upm / 2) / f->upm;
  z->height = ((f->ascent + f->descent + f->linegap) * (i32)size * 10 + f->upm / 2) / f->upm;
  z->next = fs->sizes; fs->sizes = z;
  return z;
}
/* Latin-1 from the size's table; anything else through the cmap. A character the face lacks
   is measured as '?', the character the Amiga draws in its place */
u32 fonts_fine(FaceSize *z, u32 cp)
{
  Face *f = z->face;
  u16 g;
  if (cp < 256) return z->fine[cp];
  g = ttf_glyph(f, cp);
  if (!g && cp != 0xFFFD) return z->fine['?'];
  return g < f->nglyphs ? (f->adv[g] * (u32)z->size * 10UL * QFINE + f->upm / 2) / f->upm : 0;
}
/* the sum of the advances, without kerning; Quill's reference characters (U+F0000 up) are
   measured like any other code point */
u32 fonts_text(FaceSize *z, const char *s, u32 n)
{
  u32 i = 0, w = 0;
  while (i < n) {
    u8 c = (u8)s[i];
    if (c < 0x80) { w += z->fine[c]; i++; }
    else {
      int used;
      u32 cp = utf8_get(s + i, n - i, &used);
      w += fonts_fine(z, cp);
      i += used;
    }
  }
  return w;
}
