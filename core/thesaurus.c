/* thesaurus.c - see thesaurus.h (WP8). Written from MyThes's documented .dat format */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thesaurus.h"

struct Thes {
  FILE *f;                     /* the file (or mem) */
  char *mem;
  long size;
  int latin1;                  /* the file is ISO8859-1 (else UTF-8) */
};

/* the line starting at or after byte off (the first whole one): into buf; its start in *at; 0 at the end */
static int line_from(Thes *t, long off, int whole, char *buf, size_t cap, long *at)
{
  long p = off;
  size_t k = 0;
  if (t->mem) {
    if (!whole) { while (p < t->size && t->mem[p] != '\n') p++; p++; }
    if (p >= t->size) return 0;
    *at = p;
    while (p < t->size && t->mem[p] != '\n' && k + 1 < cap) buf[k++] = t->mem[p++];
    while (k && buf[k - 1] == '\r') k--;
    buf[k] = 0;
    return 1;
  }
  if (fseek(t->f, off, SEEK_SET)) return 0;
  if (!whole) { int c; while ((c = fgetc(t->f)) != EOF && c != '\n') p++; if (c == EOF) return 0; p++; }
  *at = p;
  if (!fgets(buf, (int)cap, t->f)) return 0;
  k = strlen(buf);
  while (k && (buf[k - 1] == '\n' || buf[k - 1] == '\r')) buf[--k] = 0;
  return 1;
}
static Thes *finish_open(Thes *t)
{
  char line[64];
  long at;
  if (!line_from(t, 0, 1, line, sizeof line, &at)) { thes_close(t); return NULL; }
  t->latin1 = !strncmp(line, "ISO8859-1", 9) || !strncmp(line, "ISO-8859-1", 10) || !strncmp(line, "ISO8859-15", 10);
  return t;
}
Thes *thes_open(const char *path)
{
  Thes *t = (Thes *)calloc(1, sizeof(Thes));
  if (!t) return NULL;
  if (!(t->f = fopen(path, "rb"))) { free(t); return NULL; }
  fseek(t->f, 0, SEEK_END); t->size = ftell(t->f);
  return finish_open(t);
}
Thes *thes_open_mem(const char *data, size_t n)
{
  Thes *t = (Thes *)calloc(1, sizeof(Thes));
  if (!t || !(t->mem = (char *)malloc(n + 1))) { free(t); return NULL; }
  memcpy(t->mem, data, n); t->mem[n] = 0; t->size = (long)n;
  return finish_open(t);
}
void thes_close(Thes *t)
{
  if (!t) return;
  if (t->f) fclose(t->f);
  free(t->mem); free(t);
}
/* compares a word with a headword line's word (up to its '|'), bytes made small (ASCII) */
static int hcmp(const char *w, const char *line)
{
  for (;; w++, line++) {
    int a = (u8)*w, b = *line == '|' ? 0 : (u8)*line;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b || !a) return a - b;
  }
}
/* a headword line: "word|n" (not a meaning, which starts with '(' or '|') */
static int is_head(const char *line) { const char *b = strrchr(line, '|'); return line[0] && line[0] != '(' && b && b[1] >= '0' && b[1] <= '9'; }
/* binary search over the file's bytes for w's entry: its headword line's start, -1 if none */
static long find(Thes *t, const char *w)
{
  long lo = 0, hi = t->size, at;
  char line[512];
  int guard = 0;
  while (lo < hi && guard++ < 64) {
    long mid = lo + (hi - lo) / 2, p = mid, hat = -1;
    int first = 1;
    /* the first headword at or after mid */
    while (line_from(t, p, first && p == 0, line, sizeof line, &at)) {
      first = 0;
      if (at >= hi) break;
      if (is_head(line) && at > 0) { hat = at; break; }
      p = at;
    }
    if (hat < 0) { hi = mid; continue; }
    {
      int c = hcmp(w, line);
      if (!c) return hat;
      if (c < 0) hi = mid; else lo = hat + 1;
    }
  }
  return -1;
}
static int read_entry(Thes *t, long hat, ThesResult *r)
{
  char line[1024];
  long at, p;
  int nm, i;
  Buf b;
  size_t offs[THES_MEANINGS][THES_TERMS], poffs[THES_MEANINGS];
  if (!line_from(t, hat, 1, line, sizeof line, &at)) return 0;
  { char *bar = strrchr(line, '|'); nm = bar ? atoi(bar + 1) : 0; if (bar) *bar = 0; }
  strncpy(r->head, line, sizeof r->head - 1); r->head[sizeof r->head - 1] = 0;
  buf_init(&b);
  p = at;
  for (i = 0; i < nm; i++) {
    char *s, *e;
    if (!line_from(t, p, 0, line, sizeof line, &at)) break;
    p = at;
    if (r->n >= THES_MEANINGS) continue;
    s = line;
    /* the part of speech, then the terms */
    e = strchr(s, '|');
    if (e) *e = 0;
    poffs[r->n] = b.len; buf_add(&b, s, strlen(s)); buf_addc(&b, 0);
    r->m[r->n].nterms = 0;
    while (e && r->m[r->n].nterms < THES_TERMS) {
      s = e + 1;
      e = strchr(s, '|');
      if (e) *e = 0;
      if (!*s) continue;
      offs[r->n][r->m[r->n].nterms++] = b.len; buf_add(&b, s, strlen(s)); buf_addc(&b, 0);
    }
    r->n++;
  }
  if (b.oom) { buf_free(&b); r->n = 0; return 0; }
  r->buf = buf_take(&b);
  for (i = 0; i < r->n; i++) {
    int k;
    const char *pos = r->buf + poffs[i];
    /* "(noun)" shown as "noun" */
    if (*pos == '(') pos++;
    strncpy(r->m[i].pos, pos, sizeof r->m[i].pos - 1); r->m[i].pos[sizeof r->m[i].pos - 1] = 0;
    { char *c = strchr(r->m[i].pos, ')'); if (c) *c = 0; }
    for (k = 0; k < r->m[i].nterms; k++) r->m[i].terms[k] = r->buf + offs[i][k];
  }
  return r->n > 0;
}
/* a Latin-1 file's text made UTF-8 in place (cap bytes of room) */
static void up8(char *s, size_t cap)
{
  char tmp[1024];
  size_t i, k = 0;
  for (i = 0; s[i] && k + 3 < sizeof tmp; i++) k += (size_t)utf8_put(tmp + k, (u8)s[i]);
  tmp[k] = 0;
  strncpy(s, tmp, cap - 1); s[cap - 1] = 0;
}
int thes_lookup(Thes *t, const char *word, u32 n, ThesResult *r)
{
  char w[64], v[72];
  long hat = -1;
  int k;
  memset(r, 0, sizeof *r);
  if (!t || !n || n >= sizeof w) return 0;
  memcpy(w, word, n); w[n] = 0;
  /* the typographic apostrophe as the straight one */
  { char *q; while ((q = strstr(w, "\xE2\x80\x99"))) { *q = '\''; memmove(q + 1, q + 3, strlen(q + 3) + 1); } }
  if (t->latin1) { utf8_to_latin1(w, strlen(w), v, sizeof v); strcpy(w, v); }
  hat = find(t, w);
  if (hat < 0) {
    /* a base form: cars car, boxes box, cities city, walked walk / baked bake, walking walk / baking bake */
    static const char *const ends[][2] = { { "ies", "y" }, { "es", "" }, { "s", "" }, { "ied", "y" }, { "ed", "" }, { "ed", "e" },
                                           { "ing", "" }, { "ing", "e" }, { "er", "" }, { "est", "" } };
    size_t l = strlen(w);
    for (k = 0; k < (int)(sizeof ends / sizeof ends[0]) && hat < 0; k++) {
      size_t el = strlen(ends[k][0]);
      if (l <= el + 2 || strcmp(w + l - el, ends[k][0])) continue;
      memcpy(v, w, l - el); strcpy(v + l - el, ends[k][1]);
      hat = find(t, v);
      /* doubled consonants: stopped stop, running run */
      if (hat < 0 && l - el >= 3 && v[l - el - 1] == v[l - el - 2] && !ends[k][1][0]) { v[l - el - 1] = 0; hat = find(t, v); }
    }
  }
  if (hat < 0 || !read_entry(t, hat, r)) { thes_result_free(r); return 0; }
  if (t->latin1) {
    /* the strings made UTF-8: copied into a new buffer */
    Buf b;
    int i, j;
    size_t offs[THES_MEANINGS][THES_TERMS];
    up8(r->head, sizeof r->head);
    buf_init(&b);
    for (i = 0; i < r->n; i++) for (j = 0; j < r->m[i].nterms; j++) {
      char s[256];
      strncpy(s, r->m[i].terms[j], sizeof s - 1); s[sizeof s - 1] = 0;
      up8(s, sizeof s);
      offs[i][j] = b.len; buf_add(&b, s, strlen(s)); buf_addc(&b, 0);
    }
    if (b.oom) { buf_free(&b); thes_result_free(r); return 0; }
    free(r->buf); r->buf = buf_take(&b);
    for (i = 0; i < r->n; i++) for (j = 0; j < r->m[i].nterms; j++) r->m[i].terms[j] = r->buf + offs[i][j];
  }
  return 1;
}
void thes_result_free(ThesResult *r)
{
  free(r->buf);
  memset(r, 0, sizeof *r);
}
