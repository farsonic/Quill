/* hyph.c - see hyph.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hyph.h"
#include "proflang.h"   /* (WP31) */

#define MAXPAT 64                       /* bytes in a pattern's letters, and in a word looked at */
typedef struct {
  u16 lang;                             /* 0: the default set */
  int state;                            /* 0 not asked yet, 1 none, 2 loaded */
  char *text;                           /* the patterns, NUL-terminated lines (UTF-8) */
  u32 *table, mask;                     /* offset + 1 of a pattern's line, by the hash of its letters */
  int leftmin, rightmin, maxlen;
} HSet;
#define MAXSETS 24
static HSet sets[MAXSETS];
static int nsets;
static HyphLoader loader;

/* FNV-1a over n bytes */
static u32 hash(const char *s, u32 n)
{
  u32 h = 2166136261u, i;
  for (i = 0; i < n; i++) h = (h ^ (u8)s[i]) * 16777619u;
  return h;
}
/* a pattern line's letters (digits left out); their count in bytes */
static u32 letters(const char *line, char *out)
{
  u32 n = 0;
  for (; *line; line++) if (*line < '0' || *line > '9') { if (n < MAXPAT) out[n] = *line; n++; }
  return n;
}
static void set_free(HSet *s)
{
  free(s->text); free(s->table);
  s->text = NULL; s->table = NULL; s->maxlen = 0; s->state = 0;
}
/* drops every set; hyph_word then finds no breaks until the next load */
void hyph_free(void)
{
  int i;
  for (i = 0; i < nsets; i++) set_free(&sets[i]);
  nsets = 0;
}
void hyph_set_loader(HyphLoader fn) { loader = fn; }
static HSet *set_for(u16 lang, int make)
{
  int i;
  for (i = 0; i < nsets; i++) if (sets[i].lang == lang) return &sets[i];
  if (!make || nsets >= MAXSETS) return NULL;
  memset(&sets[nsets], 0, sizeof(HSet));
  sets[nsets].lang = lang;
  return &sets[nsets++];
}
/* 1 once a pattern file has been loaded (or a loader may give one) */
int hyph_ready(void)
{
  HSet *d = set_for(0, 0);
  return (d && d->state == 2) || loader != NULL;
}
/* Latin-1 bytes made UTF-8 (for pattern files that say ISO8859-1 on their first line); malloc'd */
static char *latin1_utf8(const char *s, size_t n, size_t *outn)
{
  char *o = (char *)malloc(n * 2 + 1);
  size_t i, k = 0;
  if (!o) return NULL;
  for (i = 0; i < n; i++) k += (size_t)utf8_put(o + k, (u8)s[i]);
  o[k] = 0; *outn = k;
  return o;
}
/* the patterns in data (copied) into set s. The first line is the file's character set (ISO8859-1 is
   made UTF-8) and is skipped, as are lines with spaces or slashes (Hyphen's non-standard patterns);
   LEFTHYPHENMIN / RIGHTHYPHENMIN set the shortest pieces allowed (in letters). Patterns are found by
   open addressing in a table twice the number of lines; a repeated set of letters keeps its first
   line. Returns 0 only when out of memory */
static int set_load(HSet *s, const char *data, size_t n)
{
  char *p, *end;
  u32 lines = 1, size = 1024;
  size_t i;
  set_free(s);
  s->leftmin = 2; s->rightmin = 3;
  if (n > 8 && (!strncmp(data, "ISO8859-1", 9) || !strncmp(data, "ISO-8859-1", 10) || !strncmp(data, "ISO8859-15", 10))) {
    if (!(s->text = latin1_utf8(data, n, &n))) return 0;
  } else {
    if (!(s->text = (char *)malloc(n + 1))) return 0;
    memcpy(s->text, data, n); s->text[n] = 0;
  }
  for (i = 0; i < n; i++) if (s->text[i] == '\n') lines++;
  while (size < lines * 2) size <<= 1;
  if (!(s->table = (u32 *)calloc(size, sizeof(u32)))) { set_free(s); return 0; }
  s->mask = size - 1;
  for (p = s->text, end = s->text + n; p < end; ) {
    char *nl = memchr(p, '\n', (size_t)(end - p)), key[MAXPAT];
    u32 k, h, j;
    int ok = 1;
    if (!nl) nl = end;
    *nl = 0;
    if (nl > p && nl[-1] == '\r') nl[-1] = 0;
    if (!strncmp(p, "LEFTHYPHENMIN ", 14)) s->leftmin = atoi(p + 14);
    else if (!strncmp(p, "RIGHTHYPHENMIN ", 15)) s->rightmin = atoi(p + 15);
    for (j = 0; p[j]; j++) if (p[j] == ' ' || p[j] == '/' || (p[j] >= 'A' && p[j] <= 'Z')) ok = 0;
    if (ok && p[0] && p != s->text && (k = letters(p, key)) > 0 && k <= MAXPAT) {
      for (h = hash(key, k) & s->mask, j = 0; s->table[h] && j <= s->mask; h = (h + 1) & s->mask, j++) ;
      if (!s->table[h]) s->table[h] = (u32)(p - s->text) + 1;
      if ((int)k > s->maxlen) s->maxlen = (int)k;
    }
    p = nl + 1;
  }
  if (s->leftmin < 1) s->leftmin = 1;
  if (s->rightmin < 1) s->rightmin = 1;
  s->state = 2;
  return 1;
}
int hyph_load_mem(const char *data, size_t n)
{
  HSet *s = set_for(0, 1);
  return s && set_load(s, data, n);
}
int hyph_load_lang(u16 lang, const char *data, size_t n)
{
  HSet *s = set_for(lang, 1);
  return s && set_load(s, data, n);
}
/* reads a whole .dic file and loads it with hyph_load_mem; 0 if missing, empty or unreadable */
int hyph_load(const char *path)
{
  FILE *f = fopen(path, "rb");
  long n;
  char *buf;
  int ok;
  if (!f) return 0;
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n <= 0 || !(buf = (char *)malloc((size_t)n))) { fclose(f); return 0; }
  ok = fread(buf, 1, (size_t)n, f) == (size_t)n && hyph_load_mem(buf, (size_t)n);
  free(buf);
  fclose(f);
  return ok;
}
/* the pattern for these letters, or NULL */
static const char *find(const HSet *s, const char *w, u32 n)
{
  u32 h, i;
  char key[MAXPAT];
  for (h = hash(w, n) & s->mask, i = 0; s->table[h] && i <= s->mask; h = (h + 1) & s->mask, i++) {
    const char *line = s->text + s->table[h] - 1;
    if (letters(line, key) == n && !memcmp(key, w, n)) return line;
  }
  return NULL;
}
/* a letter's small form (ASCII, Latin-1 and Latin Extended-A capitals, Greek and Cyrillic) */
static u32 small(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
    return (c & 1) ? c : c + 1;
  }
  if (c >= 0x391 && c <= 0x3AB) return c + 32;
  if (c >= 0x410 && c <= 0x42F) return c + 32;
  if (c >= 0x400 && c <= 0x40F) return c + 80;
  return c;
}
/* Liang's method on s's patterns: the word (made small, UTF-8) is wrapped in dots, every pattern
   matching any substring raises the digits between letters, and odd values mark breaks. The left
   and right minimums count letters; words longer than MAXPAT bytes get none, and no break goes next
   to an apostrophe */
static int word_breaks(const HSet *s, const char *w, u32 n, u8 *brk)
{
  char t[MAXPAT + 2];
  u8 v[MAXPAT + 3];
  u32 i, j, len, chars = 0, at = 0, k;
  int count = 0;
  memset(brk, 0, n + 1);
  if (!s || s->state != 2 || n > MAXPAT - 2) return 0;
  /* the small letters, with the same byte offsets (a small letter is as long as its capital in the
     ranges small() covers, but for a few: those words are left alone) */
  t[0] = '.'; len = 1;
  for (i = 0; i < n; ) {
    int used;
    u32 c = utf8_get(w + i, n - i, &used), lc = small(c);
    char e[4];
    int el = utf8_put(e, lc);
    if (el != used) return 0;
    if (c == '\'' || c == 0x2019) { /* allowed */ }
    else if (c < 0x80 && !((lc >= 'a' && lc <= 'z'))) return 0;
    else if (c >= 0x80 && c < 0xC0) return 0;
    else if (c >= 0x2000 && c <= 0x206F) return 0;          /* (dashes, quotes: not letters) */
    memcpy(t + len, e, (size_t)el); len += (u32)el;
    i += (u32)used; chars++;
  }
  if (chars < (u32)(s->leftmin + s->rightmin)) return 0;
  t[len++] = '.';
  memset(v, 0, sizeof v);
  for (i = 0; i < len; i++)
    for (j = i + 1; j <= len && (int)(j - i) <= s->maxlen; j++) {
      const char *pat = find(s, t + i, j - i), *q;
      u32 p = i;                     /* the digit before byte p of t */
      if (!pat) continue;
      for (q = pat; *q; q++) {
        if (*q >= '0' && *q <= '9') { if ((u8)(*q - '0') > v[p]) v[p] = (u8)(*q - '0'); }
        else p++;
      }
    }
  /* v[k]: before t[k], so before w[k - 1]; only at a character's start, leftmin letters in and
     rightmin from the end */
  for (i = 0, k = 0; i < n; i = (u32)utf8_next(w, n, i), k++) {
    if (k < (u32)s->leftmin || chars - k < (u32)s->rightmin) continue;
    if (!(v[i + 1] & 1)) continue;
    if (w[i - 1] == '\'' || w[i] == '\'') continue;
    if (i >= 3 && (u8)w[i - 3] == 0xE2 && (u8)w[i - 2] == 0x80 && (u8)w[i - 1] == 0x99) continue;
    if ((u8)w[i] == 0xE2) continue;
    brk[i] = 1; count++;
  }
  (void)at;
  return count;
}
int hyph_word(const char *w, u32 n, u8 *brk)
{
  return word_breaks(set_for(0, 0), w, n, brk);
}
int hyph_word_lang(u16 lang, const char *w, u32 n, u8 *brk)
{
  HSet *s;
  if (!lang) lang = plang_default;
  if (!(s = set_for(lang, 0)) && loader && (s = set_for(lang, 1))) {
    size_t len;
    char *data = loader(lang, &len);
    s->state = 1;
    if (data) { set_load(s, data, len); free(data); }
  }
  if (s && s->state == 2) return word_breaks(s, w, n, brk);
  /* no set of its own: English (or no language) has the default set; other languages none */
  if (plang_primary(lang, "en")) return hyph_word(w, n, brk);
  memset(brk, 0, n + 1);
  return 0;
}
