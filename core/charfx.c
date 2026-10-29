/* charfx.c - see charfx.h. WP9/WP10's core helpers: kept run properties, enclosed characters,
   WP10's numbering formats and Sort Text. Written for Quill from Word's documented behaviour. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "charfx.h"
#include "fonts.h"

/* ------------------------------------------------------------------ */
/* kept run properties: one table for the program, only added to */
#define MAX_RX 4000
static char **rx_tab; static int rx_n, rx_cap;

u16 rx_add(const char *xml, size_t n)
{
  int i;
  char *c;
  if (!xml || !n) return 0;
  for (i = 0; i < rx_n; i++) if (strlen(rx_tab[i]) == n && !memcmp(rx_tab[i], xml, n)) return (u16)(i + 1);
  if (rx_n >= MAX_RX) return 0;
  if (rx_n >= rx_cap) {
    int nc = rx_cap ? rx_cap * 2 : 16;
    char **nt = (char **)realloc(rx_tab, (size_t)nc * sizeof(char *));
    if (!nt) return 0;
    rx_tab = nt; rx_cap = nc;
  }
  if (!(c = str_ndup(xml, n))) return 0;
  rx_tab[rx_n++] = c;
  return (u16)rx_n;
}
const char *rx_text(u16 rx) { return rx && rx <= rx_n ? rx_tab[rx - 1] : ""; }
/* the end of the element starting at s[0] == '<' (just past its end tag or "/>"), counting nesting */
static const char *elem_end(const char *s)
{
  int depth = 0;
  while (*s) {
    if (s[0] == '<' && s[1] == '/') { const char *e = strchr(s, '>'); if (!e) return s + strlen(s); s = e + 1; if (--depth <= 0) return s; continue; }
    if (s[0] == '<') {
      const char *e = strchr(s, '>');
      if (!e) return s + strlen(s);
      if (e[-1] == '/') { s = e + 1; if (depth == 0) return s; continue; }
      depth++; s = e + 1; continue;
    }
    s++;
  }
  return s;
}
u16 rx_edit(u16 rx, const char *name, const char *add)
{
  const char *s = rx_text(rx);
  size_t nl = strlen(name);
  Buf b;
  u16 r;
  buf_init(&b);
  while (*s) {
    if (s[0] == '<' && !strncmp(s + 1, name, nl) && (s[1 + nl] == ' ' || s[1 + nl] == '/' || s[1 + nl] == '>')) { s = elem_end(s); continue; }
    if (s[0] == '<') { const char *e = elem_end(s); buf_add(&b, s, (size_t)(e - s)); s = e; continue; }
    s++;
  }
  if (add && *add) buf_adds(&b, add);
  r = b.oom ? rx : rx_add(b.data ? b.data : "", b.len);
  buf_free(&b);
  return r;
}

/* ------------------------------------------------------------------ */
/* enclosed characters */
static const char *const en_sym[5] = { "", "\xE2\x97\x8B", "\xE2\x96\xA1", "\xE2\x96\xB3", "\xE2\x97\x87" };   /* ○ □ △ ◇ */
const char *enclose_symbol(int shape) { return shape > 0 && shape <= EN_DIAMOND ? en_sym[shape] : ""; }
/* the shape a symbol stands for (Word's own and their look-alikes), 0 none */
static int en_shape(const char *s, size_t n)
{
  static const struct { const char *u; int shape; } sy[] = {
    { "\xE2\x97\x8B", EN_CIRCLE }, { "\xE2\x97\xAF", EN_CIRCLE }, { "\xE3\x80\x87", EN_CIRCLE },    /* ○ ◯ 〇 */
    { "\xE2\x96\xA1", EN_SQUARE }, { "\xE2\x96\xA2", EN_SQUARE }, { "\xE2\x97\xBB", EN_SQUARE },    /* □ ▢ ◻ */
    { "\xE2\x96\xB3", EN_TRIANGLE }, { "\xE2\x96\xBD", EN_TRIANGLE },                             /* △ ▽ */
    { "\xE2\x97\x87", EN_DIAMOND }, { "\xE2\x97\x8A", EN_DIAMOND } };                             /* ◇ ◊ */
  size_t i;
  for (i = 0; i < sizeof sy / sizeof sy[0]; i++) if (strlen(sy[i].u) == n && !memcmp(sy[i].u, s, n)) return sy[i].shape;
  return 0;
}
int enclose_parse(const char *instr, int *shape, int *enlarge, char *text, size_t cap)
{
  const char *s = instr, *a, *comma, *close;
  while (*s == ' ') s++;
  if ((s[0] | 32) != 'e' || (s[1] | 32) != 'q' || s[2] != ' ') return 0;
  s += 3;
  while (*s == ' ') s++;
  if (strncmp(s, "\\o", 2)) return 0;
  s += 2;
  while (*s == ' ') s++;
  if (strncmp(s, "\\ac(", 4)) return 0;
  a = s + 4;
  if (!(comma = strchr(a, ',')) || !(close = strchr(comma, ')'))) return 0;
  /* the symbol is either side of the comma (Word puts it first) */
  if ((*shape = en_shape(a, (size_t)(comma - a)))) a = comma + 1;
  else if ((*shape = en_shape(comma + 1, (size_t)(close - comma - 1)))) close = comma;
  else return 0;
  if (cap) {
    size_t n = (size_t)(close - a);
    if (n >= cap) n = cap - 1;
    memcpy(text, a, n); text[n] = 0;
  }
  *enlarge = strstr(close, "enlarge") != NULL;
  return 1;
}
void enclose_code(char *out, size_t cap, int shape, int enlarge, const char *text)
{
  snprintf(out, cap, "eq \\o\\ac(%s,%s)%s", enclose_symbol(shape), text, enlarge ? " \\* enlarge" : "");
}

/* ------------------------------------------------------------------ */
/* WP10's numbering formats */
static const char *const ones[20] = { "", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Eleven", "Twelve",
                                      "Thirteen", "Fourteen", "Fifteen", "Sixteen", "Seventeen", "Eighteen", "Nineteen" };
static const char *const tens[10] = { "", "", "Twenty", "Thirty", "Forty", "Fifty", "Sixty", "Seventy", "Eighty", "Ninety" };
static const char *const ordones[20] = { "", "First", "Second", "Third", "Fourth", "Fifth", "Sixth", "Seventh", "Eighth", "Ninth", "Tenth",
                                         "Eleventh", "Twelfth", "Thirteenth", "Fourteenth", "Fifteenth", "Sixteenth", "Seventeenth", "Eighteenth",
                                         "Nineteenth" };
static const char *const ordtens[10] = { "", "", "Twentieth", "Thirtieth", "Fortieth", "Fiftieth", "Sixtieth", "Seventieth", "Eightieth", "Ninetieth" };
int nf_counter(char *out, u32 n, int fmt)
{
  switch (fmt) {
  case NF_DECIMAL_ZERO: sprintf(out, n < 10 ? "0%lu" : "%lu", (unsigned long)n); return 1;
  case NF_ORDINAL: {
    const char *suf = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    sprintf(out, "%lu%s", (unsigned long)n, suf);
    return 1; }
  case NF_CARDINAL_TEXT: case NF_ORDINAL_TEXT:
    /* words up to 99 (Word goes further; past 99 the number itself) */
    if (n == 0 || n > 99) { sprintf(out, "%lu", (unsigned long)n); return 1; }
    if (n < 20) strcpy(out, fmt == NF_ORDINAL_TEXT ? ordones[n] : ones[n]);
    else if (n % 10 == 0) strcpy(out, fmt == NF_ORDINAL_TEXT ? ordtens[n / 10] : tens[n / 10]);
    else sprintf(out, "%s-%s", tens[n / 10], fmt == NF_ORDINAL_TEXT ? ordones[n % 10] : ones[n % 10]);
    if (fmt == NF_ORDINAL_TEXT || n >= 20) { char *p = out + 1; for (; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32); }
    return 1;
  }
  return 0;
}
static const char *const nf_names[] = { "bullet", "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman", "none",
                                        "decimalZero", "ordinal", "cardinalText", "ordinalText" };
const char *nf_name(int fmt) { return fmt >= 0 && fmt < (int)(sizeof nf_names / sizeof nf_names[0]) ? nf_names[fmt] : "decimal"; }
int nf_from_name(const char *name)
{
  int i;
  for (i = 0; i < (int)(sizeof nf_names / sizeof nf_names[0]); i++) if (!strcmp(name, nf_names[i])) return i;
  return -1;
}

/* ------------------------------------------------------------------ */
/* Sort Text */
/* a Latin-1 letter without its accent, in lower case (the order Word sorts in: accents after the plain letter) */
static u32 fold(u32 c, int *accent)
{
  static const char base[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";   /* U+00E0..U+00FF */
  *accent = 0;
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c += 32;
  if (c >= 0xE0 && c <= 0xFF && c != 0xF7) { *accent = (int)c; return (u32)(u8)base[c - 0xE0]; }
  return c;
}
/* field k (1 up; 0 the whole text) of s[0..n) split at sep: where it starts and how long */
static void field_of(const char *s, u32 n, int k, char sep, u32 *a, u32 *len)
{
  u32 i = 0, st = 0;
  int f = 1;
  if (k <= 0) { *a = 0; *len = n; return; }
  for (i = 0; i <= n; i++) {
    if (i == n || s[i] == sep) {
      if (f == k) { *a = st; *len = i - st; return; }
      f++; st = i + 1;
    }
  }
  *a = n; *len = 0;
}
long sort_number(const char *s, u32 n, int *found)
{
  u32 i = 0;
  long v = 0, frac = 0, scale = 1000;
  int neg = 0, any = 0;
  while (i < n && !(s[i] >= '0' && s[i] <= '9')) i++;
  if (i < n && i > 0 && s[i - 1] == '-') neg = 1;
  while (i < n) {
    if (s[i] >= '0' && s[i] <= '9') { if (v < 100000000L) v = v * 10 + (s[i] - '0'); any = 1; i++; }
    else if (s[i] == ',' && i + 3 < n && s[i + 1] >= '0' && s[i + 1] <= '9' && s[i + 2] >= '0' && s[i + 2] <= '9' && s[i + 3] >= '0' && s[i + 3] <= '9' &&
             (i + 4 >= n || !(s[i + 4] >= '0' && s[i + 4] <= '9'))) i++;              /* thousands */
    else break;
  }
  if (i + 1 < n && (s[i] == '.' || s[i] == ',') && s[i + 1] >= '0' && s[i + 1] <= '9') {
    i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') { if (scale > 1) { scale /= 10; frac += (s[i] - '0') * scale; } i++; }
  }
  *found = any;
  if (v > 2000000L) v = 2000000L;
  v = v * 1000 + frac;
  return neg ? -v : v;
}
/* month names (English, and their first three letters) */
static int month_of(const char *s, u32 n)
{
  static const char *const m[12] = { "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december" };
  int k;
  u32 i;
  char w[12];
  if (n < 3 || n > 9) return 0;
  for (i = 0; i < n; i++) w[i] = (char)(s[i] | 32);
  w[n] = 0;
  for (k = 0; k < 12; k++) if (!strncmp(m[k], w, n) && (n == 3 || n == strlen(m[k]))) return k + 1;
  return 0;
}
long sort_date(const char *s, u32 n)
{
  long num[3];
  int nn = 0, month = 0, mpos = -1, iso = 0;
  u32 i = 0;
  long y, m, d;
  while (i < n && nn < 3) {
    if (s[i] >= '0' && s[i] <= '9') {
      long v = 0;
      u32 st = i;
      while (i < n && s[i] >= '0' && s[i] <= '9' && v < 100000) v = v * 10 + (s[i++] - '0');
      if (nn == 0 && i - st == 4) iso = 1;
      num[nn++] = v;
    } else if ((s[i] | 32) >= 'a' && (s[i] | 32) <= 'z') {
      u32 st = i;
      while (i < n && (s[i] | 32) >= 'a' && (s[i] | 32) <= 'z') i++;
      if (!month && (month = month_of(s + st, i - st))) mpos = nn;
    } else i++;
  }
  if (month) {
    if (nn < 2) return 0;
    m = month;
    if (mpos == 0) { d = num[0]; y = num[1]; }       /* March 12, 2024 */
    else { d = num[0]; y = num[1]; }                 /* 12 March 2024 */
  } else {
    if (nn < 3) return 0;
    if (iso) { y = num[0]; m = num[1]; d = num[2]; }
    else { d = num[0]; m = num[1]; y = num[2]; }     /* day first, as in Britain */
  }
  if (y < 100) y += y < 50 ? 2000 : 1900;
  if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
  return y * 372 + m * 31 + d;
}
/* one paragraph's keys, worked out once */
typedef struct { char *t[3]; u32 tn[3]; long v[3]; int has[3]; Para *p; } SortItem;
static const SortOpt *so;
/* the paragraph's text without objects' and fields' placeholders */
static char *plain_text(const Para *p, u32 *n)
{
  char *o = (char *)malloc(p->len + 1);
  u32 i = 0, k = 0;
  if (!o) return NULL;
  while (i < p->len) {
    int used;
    u32 c = utf8_get(p->t + i, p->len - i, &used);
    if (c < 0xE000 || (c > 0xF8FF && c < 0xF0000)) { memcpy(o + k, p->t + i, (size_t)used); k += (u32)used; }
    i += (u32)used;
  }
  o[k] = 0; *n = k;
  return o;
}
static int cmp_text(const char *a, u32 an, const char *b, u32 bn, int casesens)
{
  u32 i = 0, j = 0;
  int tie = 0;
  while (i < an && j < bn) {
    int ua, ub, ea, eb;
    u32 ca = utf8_get(a + i, an - i, &ua), cb = utf8_get(b + j, bn - j, &ub), fa = fold(ca, &ea), fb = fold(cb, &eb);
    if (fa != fb) return fa < fb ? -1 : 1;
    if (!tie && ea != eb) tie = ea < eb ? -1 : 1;
    if (!tie && casesens && ca != cb) tie = ca < cb ? -1 : 1;     /* capitals first */
    i += (u32)ua; j += (u32)ub;
  }
  if (i < an || j < bn) return i < an ? 1 : -1;
  return tie;
}
static int cmp_items(const SortItem *x, const SortItem *y)
{
  int k;
  for (k = 0; k < so->nkeys; k++) {
    int c = 0;
    if (so->key[k].type == SORT_TEXT) {
      /* empty ones first */
      if (!x->tn[k] || !y->tn[k]) c = (x->tn[k] != 0) - (y->tn[k] != 0);
      else c = cmp_text(x->t[k], x->tn[k], y->t[k], y->tn[k], so->casesens);
    } else {
      if (x->has[k] != y->has[k]) c = x->has[k] - y->has[k];      /* those without a number (date) first */
      else c = x->v[k] < y->v[k] ? -1 : x->v[k] > y->v[k];
    }
    if (c) return so->key[k].desc ? -c : c;
  }
  return 0;
}
/* a stable merge sort of a[0..n) using tmp */
static void msort(SortItem **a, SortItem **tmp, u32 n)
{
  u32 h = n / 2, i, j, k;
  if (n < 2) return;
  msort(a, tmp, h); msort(a + h, tmp, n - h);
  for (i = 0, j = h, k = 0; i < h && j < n; ) tmp[k++] = cmp_items(a[j], a[i]) < 0 ? a[j++] : a[i++];
  while (i < h) tmp[k++] = a[i++];
  while (j < n) tmp[k++] = a[j++];
  memcpy(a, tmp, n * sizeof *a);
}
int doc_sort_text(Doc *d, u32 pa, u32 pb, const SortOpt *o)
{
  SortItem *it;
  SortItem **ord, **tmp;
  Para **nw;
  u32 n, i, first;
  int k, ok = 0;
  Pos where;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pb >= d->np || !o || o->nkeys < 1 || o->nkeys > 3) return 0;
  for (i = pa; i <= pb; i++) if (d->p[i]->pf.cell.table) return 0;
  first = pa + (o->header ? 1 : 0);
  if (first >= pb) return 0;
  n = pb - first + 1;
  so = o;
  it = (SortItem *)calloc(n, sizeof *it);
  ord = (SortItem **)malloc(n * sizeof *ord);
  tmp = (SortItem **)malloc(n * sizeof *tmp);
  nw = (Para **)calloc(n, sizeof *nw);
  if (!it || !ord || !tmp || !nw) goto done;
  for (i = 0; i < n; i++) {
    u32 tn;
    char *t = plain_text(d->p[first + i], &tn);
    if (!t) goto done;
    it[i].p = d->p[first + i];
    for (k = 0; k < o->nkeys; k++) {
      u32 a, len;
      field_of(t, tn, o->key[k].field, o->sep ? o->sep : '\t', &a, &len);
      while (len && t[a] == ' ') { a++; len--; }
      while (len && t[a + len - 1] == ' ') len--;
      if (o->key[k].type == SORT_TEXT) { it[i].t[k] = str_ndup(t + a, len); it[i].tn[k] = it[i].t[k] ? len : 0; }
      else if (o->key[k].type == SORT_NUMBER) it[i].v[k] = sort_number(t + a, len, &it[i].has[k]);
      else { it[i].v[k] = sort_date(t + a, len); it[i].has[k] = it[i].v[k] != 0; }
    }
    free(t);
    ord[i] = &it[i];
  }
  msort(ord, tmp, n);
  for (i = 0; i < n; i++) if (!(nw[i] = para_copy(ord[i]->p))) goto done;
  where.para = first; where.off = 0;
  ok = doc_replace(d, first, n, nw, n, where);
  free(nw); nw = NULL;
done:
  if (nw) { for (i = 0; i < n; i++) para_free(nw[i]); free(nw); }
  if (it) for (i = 0; i < n; i++) for (k = 0; k < 3; k++) free(it[i].t[k]);
  free(it); free(ord); free(tmp);
  return ok;
}

/* ------------------------------------------------------------------ */
/* WP10's special characters in the layout, WP9's enclosed boxes */
int wp10_special(const char *s, u32 n)
{
  u32 i;
  for (i = 0; i + 1 < n; i++) {
    if ((u8)s[i] == 0xC2 && (u8)s[i + 1] == 0xAD) return 1;
    if (i + 2 < n && (u8)s[i] == 0xE2 && (u8)s[i + 1] == 0x80 && (u8)s[i + 2] == 0x91) return 1;
  }
  return 0;
}
u32 wp10_plain(const char *s, u32 n, char *out)
{
  u32 i = 0, k = 0;
  while (i < n) {
    if (i + 1 < n && (u8)s[i] == 0xC2 && (u8)s[i + 1] == 0xAD) { i += 2; continue; }
    if (i + 2 < n && (u8)s[i] == 0xE2 && (u8)s[i + 1] == 0x80 && (u8)s[i + 2] == 0x91) { out[k++] = '-'; i += 3; continue; }
    out[k++] = s[i++];
  }
  return k;
}
i32 wp9_enclosed_w(i32 w, const CharFmt *f)
{
  i32 em = (i32)f->size * 10 * QFINE;
  if (f->fx2 & FX2_ENLARGE) return (w > em * 3 / 2 ? w : em * 3 / 2) + em / 6;
  return (w * 7 / 10 > em ? w * 7 / 10 : em) + em / 6;
}
