/* hyph.c - see hyph.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hyph.h"

#define HBITS 15
#define HSIZE (1u << HBITS)
#define MAXPAT 32                       /* letters in a pattern, and in a word looked at */
static char *text;                      /* the patterns, NUL-terminated lines */
static u32 *table;                      /* offset + 1 of a pattern's line, by the hash of its letters */
static int leftmin = 2, rightmin = 3, maxlen;

/* FNV-1a over n bytes; the table is indexed by the low HBITS bits */
static u32 hash(const char *s, u32 n)
{
  u32 h = 2166136261u, i;
  for (i = 0; i < n; i++) h = (h ^ (u8)s[i]) * 16777619u;
  return h;
}
/* a pattern line's letters (digits left out); their count */
static u32 letters(const char *line, char *out)
{
  u32 n = 0;
  for (; *line; line++) if (*line < '0' || *line > '9') { if (n < MAXPAT) out[n] = *line; n++; }
  return n;
}
/* drops the loaded patterns; hyph_word then finds no breaks until the next load */
void hyph_free(void)
{
  free(text); free(table);
  text = NULL; table = NULL; maxlen = 0;
}
/* 1 once a pattern file has been loaded */
int hyph_ready(void) { return table != NULL; }
/* replaces any loaded patterns with those in data (copied, so the caller keeps its buffer).
   The first line is the file's character set and is skipped, as are lines with non-ASCII
   letters, spaces or slashes (non-standard patterns); LEFTHYPHENMIN / RIGHTHYPHENMIN set the
   shortest pieces allowed. Patterns are found by open addressing; a repeated set of letters
   keeps its first line. Returns 0 only when out of memory */
int hyph_load_mem(const char *data, size_t n)
{
  char *p, *end;
  hyph_free();
  if (!(text = (char *)malloc(n + 1)) || !(table = (u32 *)calloc(HSIZE, sizeof(u32)))) { hyph_free(); return 0; }
  memcpy(text, data, n); text[n] = 0;
  for (p = text, end = text + n; p < end; ) {
    char *nl = memchr(p, '\n', (size_t)(end - p)), key[MAXPAT];
    u32 k, h, i;
    int ascii = 1;
    if (!nl) nl = end;
    *nl = 0;
    if (nl > p && nl[-1] == '\r') nl[-1] = 0;
    if (!strncmp(p, "LEFTHYPHENMIN ", 14)) leftmin = atoi(p + 14);
    else if (!strncmp(p, "RIGHTHYPHENMIN ", 15)) rightmin = atoi(p + 15);
    for (i = 0; p[i]; i++) if ((u8)p[i] > 126 || p[i] == ' ' || p[i] == '/') ascii = 0;
    if (ascii && p[0] && p != text && (k = letters(p, key)) > 0 && k <= MAXPAT) {
      for (h = hash(key, k) & (HSIZE - 1), i = 0; table[h] && i < HSIZE; h = (h + 1) & (HSIZE - 1), i++) ;
      if (!table[h]) table[h] = (u32)(p - text) + 1;
      if ((int)k > maxlen) maxlen = (int)k;
    }
    p = nl + 1;
  }
  if (leftmin < 1) leftmin = 1;
  if (rightmin < 1) rightmin = 1;
  return 1;
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
static const char *find(const char *s, u32 n)
{
  u32 h, i;
  char key[MAXPAT];
  for (h = hash(s, n) & (HSIZE - 1), i = 0; table[h] && i < HSIZE; h = (h + 1) & (HSIZE - 1), i++) {
    const char *line = text + table[h] - 1;
    if (letters(line, key) == n && !memcmp(key, s, n)) return line;
  }
  return NULL;
}
/* Liang's method: the word is wrapped in dots, every pattern matching any substring raises the
   digits between letters, and odd values mark breaks. Words outside the left/right minimums or
   longer than MAXPAT get none, and no break goes next to an apostrophe */
int hyph_word(const char *w, u32 n, u8 *brk)
{
  char s[MAXPAT + 2];
  u8 v[MAXPAT + 3];
  u32 i, j, len = n + 2;
  int count = 0;
  memset(brk, 0, n + 1);
  if (!table || n < (u32)(leftmin + rightmin) || n > MAXPAT) return 0;
  s[0] = '.';
  for (i = 0; i < n; i++) {
    char c = w[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (!((c >= 'a' && c <= 'z') || c == '\'')) return 0;
    s[i + 1] = c;
  }
  s[n + 1] = '.';
  memset(v, 0, sizeof v);
  for (i = 0; i < len; i++)
    for (j = i + 1; j <= len && (int)(j - i) <= maxlen; j++) {
      const char *pat = find(s + i, j - i), *q;
      u32 at = i;                     /* the digit before letter 'at' of s */
      if (!pat) continue;
      for (q = pat; *q; q++) {
        if (*q >= '0' && *q <= '9') { if ((u8)(*q - '0') > v[at]) v[at] = (u8)(*q - '0'); }
        else at++;
      }
    }
  /* v[k]: between s[k - 1] and s[k], so before w[k - 1] */
  for (i = (u32)leftmin; i + (u32)rightmin <= n; i++)
    if ((v[i + 1] & 1) && w[i - 1] != '\'' && w[i] != '\'') { brk[i] = 1; count++; }
  return count;
}
