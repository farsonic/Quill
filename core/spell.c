/* spell.c - see spell.h */
#include <stdlib.h>
#include <string.h>
#include "spell.h"
#include "util.h"
#include "docx.h"     /* file_read */

/* letters: ASCII and the Latin-1 ones (as UTF-8 C3 xx), compared without case */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* does a letter start at t (n bytes left)? A-Z, a-z, or a Latin-1 letter in UTF-8 (C3 80-BF,
   leaving out the multiply and divide signs) */
static int is_letter(const u8 *t, u32 n)
{
  if (!n) return 0;
  if ((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z')) return 1;
  return *t == 0xC3 && n > 1 && t[1] >= 0x80 && t[1] <= 0xBF && t[1] != 0x97 && t[1] != 0xB7;
}
/* the same word ignoring case (ASCII and Latin-1 letters) */
static int wcmp(const char *a, u32 an, const char *b, u32 bn)
{
  u32 i;
  for (i = 0; i < an && i < bn; i++) {
    int x = (u8)a[i], y = (u8)b[i];
    if (i && (u8)a[i - 1] == 0xC3 && (u8)b[i - 1] == 0xC3) { if (x >= 0x80 && x <= 0x9E) x += 0x20; if (y >= 0x80 && y <= 0x9E) y += 0x20; }
    else { x = lower(x); y = lower(y); }
    if (x != y) return x - y;
  }
  return (int)an - (int)bn;
}
/* the list being sorted, for by_word: qsort's comparator has no context pointer */
static const char *sp_text;
/* compares two word offsets into sp_text, each word running to its newline */
static int by_word(const void *x, const void *y)
{
  const char *a = sp_text + *(const u32 *)x, *b = sp_text + *(const u32 *)y;
  return wcmp(a, (u32)strcspn(a, "\n"), b, (u32)strcspn(b, "\n"));
}
/* the words in order: nothing to do for a list that is in order already (Quill's own is), else a
   merge sort (a library qsort can take minutes on a 68k for a list almost in order) */
static void merge_words(u32 *w, u32 *tmp, u32 n)
{
  u32 h = n / 2, i = 0, j = h, k = 0;
  if (n < 2) return;
  merge_words(w, tmp, h);
  merge_words(w + h, tmp, n - h);
  if (by_word(&w[h - 1], &w[h]) <= 0) return;
  while (i < h && j < n) tmp[k++] = by_word(&w[j], &w[i]) < 0 ? w[j++] : w[i++];
  while (i < h) tmp[k++] = w[i++];
  memcpy(w, tmp, k * sizeof(u32));
}
/* checks first whether the list is already sorted; falls back to qsort only when there is no
   memory for the merge's scratch array */
static void sort_words(u32 *w, u32 n)
{
  u32 i, *tmp;
  for (i = 1; i < n && by_word(&w[i - 1], &w[i]) <= 0; i++) ;
  if (i >= n) return;
  if (!(tmp = (u32 *)malloc(n * sizeof(u32)))) { qsort(w, n, sizeof(u32), by_word); return; }
  merge_words(w, tmp, n);
  free(tmp);
}
/* takes a copy of the list (the caller keeps text), turns CR and CRLF line ends into newlines,
   skips blank lines and sorts the words case-insensitively for the binary search in known().
   Any previous contents of s are not freed: call spell_free first. 0 when out of memory */
int spell_load_mem(Spell *s, const char *text, size_t n)
{
  u32 i, k = 0, cap;
  memset(s, 0, sizeof *s);
  if (!(s->text = (char *)malloc(n + 2))) return 0;
  memcpy(s->text, text, n);
  s->text[n] = '\n'; s->text[n + 1] = 0;
  for (i = 0, cap = 1; i < n; i++) if (text[i] == '\n') cap++;
  if (!(s->word = (u32 *)malloc(cap * sizeof(u32)))) { free(s->text); s->text = NULL; return 0; }
  for (i = 0; i <= n; ) {
    u32 e = i;
    while (e < n && s->text[e] != '\n' && s->text[e] != '\r') e++;
    if (e > i && k < cap) s->word[k++] = i;
    s->text[e] = '\n';
    i = e + 1;
    while (i < n && (s->text[i] == '\n' || s->text[i] == '\r')) { s->text[i] = '\n'; i++; }
  }
  s->n = k;
  sp_text = s->text;
  sort_words(s->word, s->n);
  return 1;
}
/* the whole file into memory, then spell_load_mem; s is left empty on failure */
int spell_load(Spell *s, const char *path)
{
  size_t n;
  u8 *data = file_read(path, &n);
  int ok;
  memset(s, 0, sizeof *s);
  if (!data) return 0;
  ok = spell_load_mem(s, (const char *)data, n);
  free(data);
  return ok;
}
/* frees the list and the user's words; s is left empty (every word then counts as spelt right) */
void spell_free(Spell *s)
{
  u32 i;
  for (i = 0; i < s->nextra; i++) free(s->extra[i]);
  free(s->extra); free(s->text); free(s->word);
  memset(s, 0, sizeof *s);
}
/* a copy of word goes on the extra list, which is searched linearly; out of memory, it is
   quietly not added */
void spell_add(Spell *s, const char *word, u32 n)
{
  char *w;
  if (s->nextra >= s->extracap) {
    u32 nc = s->extracap ? s->extracap * 2 : 32;
    char **ne = (char **)realloc(s->extra, nc * sizeof(char *));
    if (!ne) return;
    s->extra = ne; s->extracap = nc;
  }
  if ((w = str_ndup(word, n))) s->extra[s->nextra++] = w;
}
/* in the list (exactly as it is there, but for case), or the user's */
static int known(const Spell *s, const char *w, u32 n)
{
  u32 lo = 0, hi = s->n, i;
  while (lo < hi) {
    u32 mid = (lo + hi) / 2;
    const char *e = s->text + s->word[mid];
    int c = wcmp(w, n, e, (u32)strcspn(e, "\n"));
    if (!c) return 1;
    if (c < 0) hi = mid; else lo = mid + 1;
  }
  for (i = 0; i < s->nextra; i++) if (!wcmp(w, n, s->extra[i], (u32)strlen(s->extra[i]))) return 1;
  return 0;
}
/* also accepts possessives ('s with a straight or curly apostrophe, and a trailing ') and a
   hyphenated word whose every part is right. Case is ignored throughout. With no list loaded
   everything is right, so a missing dictionary never underlines the whole document */
int spell_ok(const Spell *s, const char *word, u32 n)
{
  u32 i;
  if (!s->n || !n) return 1;
  for (i = 0; i < n; i++) if (word[i] >= '0' && word[i] <= '9') return 1;        /* "4th", "MP3" */
  if (known(s, word, n)) return 1;
  /* possessives: "Amiga's", "Amigas'" */
  if (n > 2 && word[n - 2] == '\'' && lower((u8)word[n - 1]) == 's' && known(s, word, n - 2)) return 1;
  if (n > 3 && (u8)word[n - 4] == 0xE2 && (u8)word[n - 3] == 0x80 && (u8)word[n - 2] == 0x99 && lower((u8)word[n - 1]) == 's' && known(s, word, n - 4)) return 1;
  if (n > 1 && word[n - 1] == '\'' && known(s, word, n - 1)) return 1;
  /* hyphenated: each part */
  for (i = 0; i < n; i++) if (word[i] == '-') break;
  if (i < n) {
    u32 a = 0;
    for (i = 0; i <= n; i++) if (i == n || word[i] == '-') { if (i > a && !spell_ok(s, word + a, i - a)) return 0; a = i + 1; }
    return 1;
  }
  return 0;
}
/* Suggestions are every word in the list one edit away (a letter changed, added, left out, or two
   swapped), ranked by how likely that slip is. People mostly double or undouble a letter
   ("speling", "Thiss"), swap two ("tset") or miss one out ("cheker"); hitting a neighbouring key or
   the wrong vowel comes next; the first letter is rarely wrong. Without that ranking the first
   candidate found wins, and "speling" would offer "spewing" before "spelling".
   Lower scores are better; ties keep the order they were found in. */
static const char *const kb_rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
/* a and b next to each other on a QWERTY keyboard (same row, or just above or below) */
static int kb_near(int a, int b)
{
  int ra = -1, rb = -1, ca = 0, cb = 0, r;
  for (r = 0; r < 3; r++) {
    const char *k = strchr(kb_rows[r], a), *m = strchr(kb_rows[r], b);
    if (k) { ra = r; ca = (int)(k - kb_rows[r]); }
    if (m) { rb = r; cb = (int)(m - kb_rows[r]); }
  }
  if (ra < 0 || rb < 0) return 0;
  if (ra == rb) return ca - cb == 1 || cb - ca == 1;
  return (ra - rb == 1 || rb - ra == 1) && ca - cb >= -1 && ca - cb <= 1;
}
static int is_vowel(int c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }
/* very common words: offered first when a slip could have been one of several words */
static const char *const common_words[] = {
  "the", "this", "that", "with", "have", "from", "they", "will", "what", "there", "their", "which", "when", "your", "about",
  "would", "could", "should", "were", "been", "them", "then", "than", "into", "some", "more", "also", "only", "other",
  "these", "those", "where", "after", "first", "because", "before", "through", "people", "thing", "think", "know", NULL };
static int is_common(const char *w, u32 n)
{
  int i;
  for (i = 0; common_words[i]; i++) if (strlen(common_words[i]) == n && !wcmp(common_words[i], n, w, n)) return 1;
  return 0;
}
#define SUG_MAX 64
typedef struct { char *w; int score, seq; } Sug;
static int sug_cmp(const void *x, const void *y)
{
  const Sug *a = (const Sug *)x, *b = (const Sug *)y;
  return a->score != b->score ? a->score - b->score : a->seq - b->seq;
}
/* up to max suggestions for word (n bytes), best first, each malloc'd into out (the caller frees
   them); returns how many. A capitalised word gets capitalised suggestions. Only a-z letters are
   tried, and words of 126 bytes or more get none */
int spell_suggest(const Spell *s, const char *word, u32 n, char **out, int max)
{
  char buf[128];
  Sug *cand;
  int got = 0, ncand = 0, c, k;
  u32 i;
  int cap = n && (word[0] >= 'A' && word[0] <= 'Z');
  if (!s->n || n + 2 >= sizeof buf || max <= 0) return 0;
  if (!(cand = (Sug *)malloc(SUG_MAX * sizeof *cand))) return 0;
  /* buf (len bytes) is a candidate; SCORE_ is the slip it would mean; AT_: where the edit was */
#define TRY(len, SCORE_, AT_) do { \
    int k_, dup_ = 0; \
    if ((len) > 0 && ncand < SUG_MAX && known(s, buf, (u32)(len))) { \
      for (k_ = 0; k_ < ncand; k_++) if (!wcmp(cand[k_].w, (u32)strlen(cand[k_].w), buf, (u32)(len))) dup_ = 1; \
      if (!dup_) { \
        int sc_ = (SCORE_) + ((AT_) == 0 ? 2 : 0) - (is_common(buf, (u32)(len)) ? 1 : 0); \
        if (cap && buf[0] >= 'a' && buf[0] <= 'z') buf[0] -= 32; \
        if ((cand[ncand].w = str_ndup(buf, (u32)(len)))) { cand[ncand].score = sc_; cand[ncand].seq = ncand; ncand++; } \
      } \
    } } while (0)
  for (i = 0; i < n; i++) {
    int wi = lower((u8)word[i]);
    /* two letters swapped */
    if (i + 1 < n) { memcpy(buf, word, n); buf[i] = word[i + 1]; buf[i + 1] = word[i]; TRY(n, 1, i); }
    /* one left out (at i): a doubled letter that wasn't doubled scores best */
    for (c = 'a'; c <= 'z'; c++) {
      int dbl = (i > 0 && lower((u8)word[i - 1]) == c) || wi == c;
      memcpy(buf, word, i); buf[i] = (char)c; memcpy(buf + i + 1, word + i, n - i);
      TRY(n + 1, dbl ? 0 : 2, i);
    }
    /* one too many (the letter at i): one of a doubled pair scores best */
    {
      int dbl = (i > 0 && lower((u8)word[i - 1]) == wi) || (i + 1 < n && lower((u8)word[i + 1]) == wi);
      memcpy(buf, word, i); memcpy(buf + i, word + i + 1, n - i - 1);
      TRY(n - 1, dbl ? 0 : 3, i);
    }
    /* one changed: a neighbouring key or another vowel is the likelier slip */
    for (c = 'a'; c <= 'z'; c++) {
      if (wi == c) continue;
      int sc = kb_near(wi, c) || (is_vowel(wi) && is_vowel(c)) ? 3 : 4;
      memcpy(buf, word, n); buf[i] = (char)c;
      TRY(n, sc, i);
    }
  }
  /* one left off the end */
  for (c = 'a'; c <= 'z'; c++) {
    int dbl = n && lower((u8)word[n - 1]) == c;
    memcpy(buf, word, n); buf[n] = (char)c;
    TRY(n + 1, dbl ? 0 : 2, n);
  }
#undef TRY
  qsort(cand, (size_t)ncand, sizeof *cand, sug_cmp);
  for (k = 0; k < ncand; k++) {
    if (got < max) out[got++] = cand[k].w;
    else free(cand[k].w);
  }
  free(cand);
  return got;
}
/* skips to the first letter at or after off; the word may also hold digits, and an apostrophe
   (straight or U+2019) or hyphen followed by a letter. Offsets are bytes into t */
int spell_word_at(const char *t, u32 len, u32 off, u32 *ws, u32 *we)
{
  const u8 *u = (const u8 *)t;
  u32 i = off;
  while (i < len && !is_letter(u + i, len - i)) i = (u32)utf8_next(t, len, i);
  if (i >= len) return 0;
  *ws = i;
  while (i < len) {
    if (is_letter(u + i, len - i) || (u[i] >= '0' && u[i] <= '9')) { i = (u32)utf8_next(t, len, i); continue; }
    /* an apostrophe or hyphen with letters on both sides is part of the word */
    if ((u[i] == '\'' || u[i] == '-') && i + 1 < len && is_letter(u + i + 1, len - i - 1)) { i++; continue; }
    if (u[i] == 0xE2 && i + 3 < len && u[i + 1] == 0x80 && u[i + 2] == 0x99 && is_letter(u + i + 3, len - i - 3)) { i += 3; continue; }
    break;
  }
  *we = i;
  return 1;
}
/* on success *a and *b are the start and end of the word. If p is inside a word, that word is
   skipped. The search goes round the whole document back to p's paragraph, which is then
   checked again from its start */
int spell_next(const Spell *s, Doc *d, Pos p, Pos *a, Pos *b)
{
  u32 pi = p.para, n, off = p.off;
  if (!s->n) return 0;
  for (n = 0; n <= d->np; n++) {
    Para *q = d->p[pi];
    u32 ws, we, k = off;
    /* not from the middle of a word */
    if (n == 0) while (k > 0 && k < q->len && spell_word_at(q->t, q->len, k - 1, &ws, &we) && ws < k && we > k) k = we;
    while (spell_word_at(q->t, q->len, k, &ws, &we)) {
      if (!spell_ok(s, q->t + ws, we - ws)) { a->para = b->para = pi; a->off = ws; b->off = we; return 1; }
      k = we;
    }
    pi = (pi + 1) % d->np; off = 0;
  }
  return 0;
}
