/*
 * field.c - fields (WP1): see field.h. Records, reading codes and switches, Word's formats
 * (\@ date pictures, \* number and case formats, \# number pictures), evaluating, finding fields in
 * the text, the edits (insert, update, unlink, lock, edit code), how they are shown, and the document
 * properties fields use. Written from Word's documented behaviour (ECMA-376 part 1, 17.16).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "field.h"
#include "layout.h"
#include "xml.h"
#include "info.h"

/* ------------------------------------------------------------------ */
/* the marks and the records */
void fmark_utf8(char *out, int k, int end) { out[utf8_put(out, (end ? 0x10C000u : 0x108000u) + (u32)k)] = 0; }
int fmark_index(const char *t) { int used; return (int)(utf8_get(t, 4, &used) & 0x3FFF); }

int field_shading = FS_SELECTED;
void (*field_clock)(FieldTime *t);

FieldInfo *field_info(Doc *d)
{
  if (!d->finfo) d->finfo = (FieldInfo *)calloc(1, sizeof(FieldInfo));
  return d->finfo;
}
void field_free_all(Doc *d)
{
  int i;
  for (i = 0; i < d->nfields; i++) { free(d->fields[i].code); free(d->fields[i].ffdata); }
  free(d->fields); d->fields = NULL; d->nfields = 0;
  if (d->finfo) { free(d->finfo->toccode); free(d->finfo); d->finfo = NULL; }
}
/* a new record copying proto's settings (proto NULL: none) with this code */
static int new_rec(Doc *d, const char *code, const Field *proto)
{
  Field *f;
  if (d->nfields >= FIELD_MAX) return -1;
  if (!(d->nfields & 15)) {
    Field *nf = (Field *)realloc(d->fields, (size_t)(d->nfields + 16) * sizeof(Field));
    if (!nf) return -1;
    d->fields = nf;
  }
  f = &d->fields[d->nfields];
  memset(f, 0, sizeof *f);
  if (proto) { *f = *proto; f->ffdata = NULL; f->showcode = 0; }
  if (!(f->code = str_dup(code ? code : ""))) return -1;
  if (proto && proto->ffdata && !(f->ffdata = str_dup(proto->ffdata))) { free(f->code); return -1; }
  return d->nfields++;
}
int field_new(Doc *d, const char *code, int locked)
{
  int k = new_rec(d, code, NULL);
  if (k >= 0) d->fields[k].locked = (u8)(locked != 0);
  return k;
}
const Field *field_get(const Doc *d, int k) { return k >= 0 && k < d->nfields ? &d->fields[k] : NULL; }

/* ------------------------------------------------------------------ */
/* reading codes: tokens are words, "quoted strings" (\" and \\ inside), switches (\x) and nested
   fields (FLD_OPEN ... FLD_CLOSE, kept inside the token they are in) */
enum { TK_WORD, TK_QUOTED, TK_SWITCH };
static int is_space(const char *s) { return *s == ' ' || *s == '\t' || ((u8)s[0] == 0xC2 && (u8)s[1] == 0xA0); }
static int open_quote(const char *s) { return *s == '"' || ((u8)s[0] == 0xE2 && (u8)s[1] == 0x80 && ((u8)s[2] == 0x9C || (u8)s[2] == 0x9E)); }
static int close_quote(const char *s) { return *s == '"' ? 1 : ((u8)s[0] == 0xE2 && (u8)s[1] == 0x80 && (u8)s[2] == 0x9D) ? 3 : 0; }
/* past a nested field starting at s (FLD_OPEN) */
static const char *skip_nest(const char *s)
{
  int depth = 0;
  for (; *s; s++) {
    if (*s == FLD_OPEN) depth++;
    else if (*s == FLD_CLOSE && --depth == 0) return s + 1;
  }
  return s;
}
/* the next token from s into out (may be NULL); its kind in *kind; NULL at the end */
static const char *token(const char *s, char *out, size_t cap, int *kind)
{
  size_t n = 0;
#define PUT(c) do { if (out && n + 1 < cap) out[n++] = (char)(c); } while (0)
  while (*s && is_space(s)) s += (u8)*s == 0xC2 ? 2 : 1;
  if (!*s) { if (out && cap) out[0] = 0; return NULL; }
  if (*s == '\\' && s[1]) {
    *kind = TK_SWITCH;
    PUT('\\'); PUT(s[1]);
    s += 2;
    if (out && cap) out[n] = 0;
    return s;
  }
  if (open_quote(s)) {
    *kind = TK_QUOTED;
    s += *s == '"' ? 1 : 3;
    while (*s) {
      int cq = close_quote(s);
      if (cq) { s += cq; break; }
      if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) { PUT(s[1]); s += 2; continue; }
      if (*s == FLD_OPEN) { const char *e = skip_nest(s); while (s < e) { PUT(*s); s++; } continue; }
      PUT(*s); s++;
    }
    if (out && cap) out[n] = 0;
    return s;
  }
  *kind = TK_WORD;
  while (*s && !is_space(s)) {
    if (*s == FLD_OPEN) { const char *e = skip_nest(s); while (s < e) { PUT(*s); s++; } continue; }
    PUT(*s); s++;
  }
  if (out && cap) out[n] = 0;
  return s;
#undef PUT
}
static int all_digits(const char *s) { if (!*s) return 0; for (; *s; s++) if (*s < '0' || *s > '9') return 0; return 1; }
/* whether a letter switch takes the token after it as its value (Word: \@ \# \* always; a letter
   switch when that token is quoted or a number) */
static int takes_value(char sw, int nextkind, const char *next)
{
  if (sw == '@' || sw == '#' || sw == '*') return nextkind != TK_SWITCH;
  return nextkind == TK_QUOTED || (nextkind == TK_WORD && all_digits(next));
}
static char upc(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
void field_name(const char *code, char *out, size_t cap)
{
  int kind;
  char *p;
  while (*code && is_space(code)) code++;
  if (*code == '=') { snprintf(out, cap, "="); return; }
  if (!token(code, out, cap, &kind) || kind != TK_WORD) { if (cap) out[0] = 0; return; }
  for (p = out; *p; p++) *p = upc(*p);
}
int field_switch(const char *code, char c, int which, char *out, size_t cap)
{
  char t[512], nx[512];
  int kind, nk;
  const char *s = code, *q;
  if (cap) out[0] = 0;
  while ((s = token(s, t, sizeof t, &kind))) {
    if (kind != TK_SWITCH || upc(t[1]) != upc(c)) continue;
    if (which-- > 0) continue;
    q = token(s, nx, sizeof nx, &nk);
    if (q && takes_value(c, nk, nx)) snprintf(out, cap, "%s", nx);
    return 1;
  }
  return 0;
}
int field_arg(const char *code, int n, char *out, size_t cap)
{
  char t[1024];
  int kind, nk;
  const char *s = code, *q;
  if (cap) out[0] = 0;
  if (!(s = token(s, NULL, 0, &kind))) return 0;      /* the name */
  { const char *c0 = code; while (*c0 && is_space(c0)) c0++; if (*c0 == '=') s = c0 + 1; }
  while ((s = token(s, t, sizeof t, &kind))) {
    if (kind == TK_SWITCH) {
      char sw = t[1], nx[1024];
      q = token(s, nx, sizeof nx, &nk);
      if (q && takes_value(sw, nk, nx)) s = q;
      continue;
    }
    if (n-- == 0) { snprintf(out, cap, "%s", t); return 1; }
  }
  return 0;
}
void field_code_text(const char *code, char *out, size_t cap)
{
  size_t n = 0;
  int skip = 0;               /* inside a nested field's result (left out) */
  for (; *code && n + 1 < cap; code++) {
    if (*code == FLD_OPEN) { if (!skip) out[n++] = '{'; else skip++; continue; }
    if (*code == FLD_SEP) { if (!skip) skip = 1; continue; }
    if (*code == FLD_CLOSE) { if (skip > 1) skip--; else { skip = 0; out[n++] = '}'; } continue; }
    if (!skip) out[n++] = *code;
  }
  if (cap) out[n] = 0;
}
char *field_code_parse(const char *typed)
{
  Buf b;
  buf_init(&b);
  for (; *typed; typed++) {
    if (*typed == '{') buf_addc(&b, FLD_OPEN);
    else if (*typed == '}') { buf_addc(&b, FLD_SEP); buf_addc(&b, FLD_CLOSE); }
    else if ((u8)*typed >= 32 || *typed == '\t') buf_addc(&b, *typed);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return b.data ? buf_take(&b) : str_dup("");
}
int field_kind(const char *code)
{
  char n[32];
  field_name(code, n, sizeof n);
  if (!strcmp(n, "PAGE")) return FK_PAGE;
  if (!strcmp(n, "NUMPAGES")) return FK_NUMPAGES;
  if (!strcmp(n, "SECTION")) return FK_SECTION;
  if (!strcmp(n, "SECTIONPAGES")) return FK_SECTIONPAGES;
  if (!strcmp(n, "PAGEREF")) return FK_PAGEREF;
  if (!strcmp(n, "HYPERLINK")) return FC_HYPERLINK;
  if (!strcmp(n, "REF")) return FC_REF;
  if (!strcmp(n, "TOC")) return FC_TOC;
  return FC_PLAIN;
}

void field_set_filename(Doc *d, const char *path)
{
  FieldInfo *fi = field_info(d);
  if (fi && path) snprintf(fi->filename, sizeof fi->filename, "%s", path);
}
int field_is_bare(const char *code)
{
  char t[512];
  int kind, n = 0, k = field_kind(code);
  const char *s = code;
  if (k != FK_PAGE && k != FK_NUMPAGES && k != FK_PAGEREF && k != FC_HYPERLINK) return 0;
  while ((s = token(s, t, sizeof t, &kind))) {
    if (++n == 1) continue;
    if (kind == TK_SWITCH && t[1] == '*') { s = token(s, t, sizeof t, &kind); if (!s || !str_ieq(t, "MERGEFORMAT")) return 0; continue; }
    if (k == FK_PAGEREF && n == 2 && kind != TK_SWITCH) continue;               /* its bookmark */
    if (k == FK_PAGEREF && kind == TK_SWITCH && (t[1] == 'h' || t[1] == 'H')) continue;
    if (k == FC_HYPERLINK && n == 2 && kind == TK_QUOTED) continue;             /* its address */
    if (k == FC_HYPERLINK && kind == TK_SWITCH && t[1] == 'l') { s = token(s, t, sizeof t, &kind); if (!s || kind != TK_QUOTED) return 0; continue; }
    return 0;
  }
  return k != FK_PAGEREF || n >= 2;
}

/* ------------------------------------------------------------------ */
/* dates and times */
static const char *const en_months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const en_months3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const en_days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const en_days3[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char *const *field_months = en_months, *const *field_months3 = en_months3, *const *field_days = en_days, *const *field_days3 = en_days3;
/* days since 1970-01-01 of a date, and back (H. Hinnant's algorithms, in integers) */
static long days_from_civil(long y, int m, int d)
{
  long era, yoe, doy, doe;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}
static void civil_from_days(long z, FieldTime *t)
{
  long era, doe, yoe, y, doy, mp;
  z += 719468L;
  era = (z >= 0 ? z : z - 146096) / 146097;
  doe = z - era * 146097;
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  t->day = (int)(doy - (153 * mp + 2) / 5 + 1);
  t->month = (int)(mp < 10 ? mp + 3 : mp - 9);
  t->year = (int)(y + (t->month <= 2));
}
static void set_wday(FieldTime *t)
{
  long z = days_from_civil(t->year, t->month, t->day);
  t->wday = (int)(((z % 7) + 11) % 7);       /* 1970-01-01 was a Thursday */
}
void field_time_now(FieldTime *t)
{
  time_t s;
  long days, rest;
  if (field_clock) { field_clock(t); set_wday(t); return; }
  s = time(NULL);
  days = (long)(s / 86400); rest = (long)(s % 86400);
  civil_from_days(days, t);
  t->hour = (int)(rest / 3600); t->min = (int)(rest / 60 % 60); t->sec = (int)(rest % 60);
  set_wday(t);
}
static int num(const char **s, int digits)
{
  int v = 0, i;
  for (i = 0; i < digits; i++) { if (**s < '0' || **s > '9') return -1; v = v * 10 + (**s - '0'); (*s)++; }
  return v;
}
int field_parse_time(const char *w, FieldTime *t)
{
  const char *s = w;
  memset(t, 0, sizeof *t);
  if (!w || (t->year = num(&s, 4)) < 0 || *s++ != '-' || (t->month = num(&s, 2)) < 1 || t->month > 12 || *s++ != '-' || (t->day = num(&s, 2)) < 1 || t->day > 31) return 0;
  if (*s == 'T' || *s == ' ') {
    s++;
    if ((t->hour = num(&s, 2)) < 0 || *s++ != ':' || (t->min = num(&s, 2)) < 0) { t->hour = t->min = 0; }
    else if (*s == ':') { s++; if ((t->sec = num(&s, 2)) < 0) t->sec = 0; }
  }
  set_wday(t);
  return 1;
}
void field_time_w3c(const FieldTime *t, char *out)
{
  sprintf(out, "%04d-%02d-%02dT%02d:%02d:%02dZ", t->year % 10000, t->month % 100, t->day % 100, t->hour % 100, t->min % 100, t->sec % 100);
}
/* the first n bytes of s into out (cap bytes), NUL-ended (not "%.*s": vbcc prints all of s for 0) */
static void copy_n(char *out, size_t cap, const char *s, size_t n)
{
  if (!cap) return;
  if (n > cap - 1) n = cap - 1;
  memcpy(out, s, n);
  out[n] = 0;
}
/* append s to out (n bytes used of cap) */
static void put(char *out, size_t cap, size_t *n, const char *s)
{
  while (*s && *n + 1 < cap) out[(*n)++] = *s++;
  out[*n < cap ? *n : cap - 1] = 0;
}
static int ieq_n(const char *a, const char *b, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++) if (upc(a[i]) != upc(b[i]) || !a[i]) return 0;
  return 1;
}
void field_format_date(const char *pic, const FieldTime *t, char *out, size_t cap)
{
  size_t n = 0;
  char b[32];
  if (!cap) return;
  out[0] = 0;
  while (*pic && n + 1 < cap) {
    char c = *pic;
    int run = 1;
    if (c == '\'') {                                  /* 'literal text' */
      pic++;
      while (*pic && *pic != '\'' && n + 1 < cap) out[n++] = *pic++;
      if (*pic) pic++;
      out[n] = 0;
      continue;
    }
    if (ieq_n(pic, "AM/PM", 5)) { put(out, cap, &n, t->hour < 12 ? (pic[0] == 'a' ? "am" : "AM") : (pic[0] == 'a' ? "pm" : "PM")); pic += 5; continue; }
    if (ieq_n(pic, "A/P", 3)) { put(out, cap, &n, t->hour < 12 ? (pic[0] == 'a' ? "a" : "A") : (pic[0] == 'a' ? "p" : "P")); pic += 3; continue; }
    while (pic[run] == c) run++;
    b[0] = 0;
    switch (c) {
    case 'd': case 'D':
      if (run >= 4) snprintf(b, sizeof b, "%s", field_days[t->wday % 7]);
      else if (run == 3) snprintf(b, sizeof b, "%s", field_days3[t->wday % 7]);
      else sprintf(b, run == 2 ? "%02d" : "%d", t->day);
      break;
    case 'M':
      if (run >= 4) snprintf(b, sizeof b, "%s", field_months[(t->month + 11) % 12]);
      else if (run == 3) snprintf(b, sizeof b, "%s", field_months3[(t->month + 11) % 12]);
      else sprintf(b, run == 2 ? "%02d" : "%d", t->month);
      break;
    case 'y': case 'Y':
      if (run >= 3) sprintf(b, "%04d", t->year); else sprintf(b, "%02d", t->year % 100);
      break;
    case 'h': sprintf(b, run >= 2 ? "%02d" : "%d", t->hour % 12 ? t->hour % 12 : 12); break;
    case 'H': sprintf(b, run >= 2 ? "%02d" : "%d", t->hour); break;
    case 'm': sprintf(b, run >= 2 ? "%02d" : "%d", t->min); break;
    case 's': case 'S': sprintf(b, run >= 2 ? "%02d" : "%d", t->sec); break;
    default: { int i; for (i = 0; i < run && n + 1 < cap; i++) out[n++] = c; out[n] = 0; } pic += run; continue;
    }
    put(out, cap, &n, b);
    pic += run;
  }
  out[n] = 0;
}

const char *const field_date_pics[] = { "d MMMM yyyy", "dddd, d MMMM yyyy", "d/M/yyyy", "dd/MM/yyyy", "M/d/yyyy", "yyyy-MM-dd", "d-MMM-yy", "d.M.yyyy",
  "MMMM yyyy", "MMM-yy", "d/M/yyyy HH:mm", "M/d/yyyy h:mm am/pm", "HH:mm", "HH:mm:ss", "h:mm am/pm", "h:mm:ss am/pm" };
const int field_ndate_pics = (int)(sizeof field_date_pics / sizeof field_date_pics[0]);
const char *const field_formats[] = { "", "Upper", "Lower", "FirstCap", "Caps", "Arabic", "alphabetic", "ALPHABETIC", "roman", "ROMAN", "Ordinal", "CardText", "OrdText", "Hex" };
const int field_nformats = (int)(sizeof field_formats / sizeof field_formats[0]);

/* ------------------------------------------------------------------ */
/* numbers */
static void roman(long v, char *out, int upper)
{
  static const char *const r[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
  static const long rv[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  int i;
  char *p;
  out[0] = 0;
  if (v <= 0 || v > 32767) { sprintf(out, "%ld", v); return; }
  for (i = 0; i < 13; i++) while (v >= rv[i]) { strcat(out, r[i]); v -= rv[i]; }
  if (upper) for (p = out; *p; p++) *p = upc(*p);
}
static const char *const ones[20] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
  "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
static const char *const tens[10] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };
static void words_below_1000(long v, char *out, size_t cap)
{
  size_t n = strlen(out);
  char b[40];
  if (v >= 100) { snprintf(b, sizeof b, "%s hundred", ones[v / 100]); put(out, cap, &n, b); v %= 100; if (v) put(out, cap, &n, " "); }
  if (v >= 20) { put(out, cap, &n, tens[v / 10]); if (v % 10) { put(out, cap, &n, "-"); put(out, cap, &n, ones[v % 10]); } }
  else if (v > 0) put(out, cap, &n, ones[v]);
}
/* Word's CardText: "one hundred twenty-three" */
static void card_text(long v, char *out, size_t cap)
{
  out[0] = 0;
  if (v < 0) v = -v;
  if (v == 0) { snprintf(out, cap, "zero"); return; }
  if (v >= 1000000L) { words_below_1000(v / 1000000L, out, cap); { size_t n = strlen(out); put(out, cap, &n, " million"); if (v % 1000000L) put(out, cap, &n, " "); } v %= 1000000L; }
  if (v >= 1000) { words_below_1000(v / 1000, out, cap); { size_t n = strlen(out); put(out, cap, &n, " thousand"); if (v % 1000) put(out, cap, &n, " "); } v %= 1000; }
  words_below_1000(v, out, cap);
}
/* Word's OrdText: the last word made ordinal ("twenty-first") */
static void ord_text(long v, char *out, size_t cap)
{
  static const char *const irr[][2] = { { "one", "first" }, { "two", "second" }, { "three", "third" }, { "five", "fifth" },
    { "eight", "eighth" }, { "nine", "ninth" }, { "twelve", "twelfth" } };
  char *last;
  size_t i, n;
  card_text(v, out, cap);
  last = out + strlen(out);
  while (last > out && last[-1] != ' ' && last[-1] != '-') last--;
  for (i = 0; i < sizeof irr / sizeof irr[0]; i++)
    if (!strcmp(last, irr[i][0])) { *last = 0; n = strlen(out); put(out, cap, &n, irr[i][1]); return; }
  n = strlen(out);
  if (n && out[n - 1] == 'y') { out[n - 1] = 0; n--; put(out, cap, &n, "ieth"); }
  else put(out, cap, &n, "th");
}
void field_number_picture(const char *pic, long v, char *out, size_t cap)
{
  char sec[256], digits[32], tmp[256];
  const char *s = pic, *e;
  int nsec = 0, which = 0, i, neg = v < 0, hasminus = 0, grouping = 0, first = -1, last = -1, dot = -1, len;
  unsigned long av = neg ? (unsigned long)(-v) : (unsigned long)v;
  size_t n = 0;
  /* the section: positive;negative;zero */
  {
    const char *secs[3];
    int q = 0;
    secs[0] = s; nsec = 1;
    for (e = s; *e; e++) {
      if (*e == '\'') q = !q;
      else if (*e == ';' && !q && nsec < 3) secs[nsec++] = e + 1;
    }
    which = neg && nsec >= 2 ? 1 : (v == 0 && nsec >= 3) ? 2 : 0;
    s = secs[which];
    for (e = s, q = 0; *e && (q || *e != ';'); e++) if (*e == '\'') q = !q;
    copy_n(sec, sizeof sec, s, (size_t)(e - s));
  }
  if (which) neg = 0;                                  /* (the section says how a negative number looks) */
  len = (int)strlen(sec);
  {
    int q = 0;
    for (i = 0; i < len; i++) {
      char c = sec[i];
      if (c == '\'') { q = !q; continue; }
      if (q) continue;
      if (c == '.' && dot < 0) { dot = i; continue; }
      if ((c == '0' || c == '#' || c == 'x') && dot < 0) { if (first < 0) first = i; last = i; }
      if (c == ',' && dot < 0 && first >= 0) grouping = 1;
      if (c == '-') hasminus = 1;
    }
  }
  sprintf(digits, "%lu", av);
  /* the integer part over its placeholders */
  tmp[0] = 0;
  if (first >= 0) {
    int ndig = (int)strlen(digits), di = ndig, k;
    char rev[128];
    int r = 0, x = 0, zeros = 0;
    for (k = first; k <= last; k++) if (sec[k] == 'x') x = 1;
    if (grouping) {
      for (k = first; k <= last; k++) if (sec[k] == '0') zeros++;
      {
        char padded[64];
        int p0 = zeros > ndig ? zeros - ndig : 0, j, outn = 0, tot;
        for (j = 0; j < p0 && j < 40; j++) padded[j] = '0';
        snprintf(padded + j, sizeof padded - (size_t)j, "%s", digits);
        tot = (int)strlen(padded);
        for (j = 0; j < tot && outn + 2 < (int)sizeof tmp; j++) {
          if (j && (tot - j) % 3 == 0) tmp[outn++] = ',';
          tmp[outn++] = padded[j];
        }
        tmp[outn] = 0;
      }
    } else {
      for (k = last; k >= first && r < (int)sizeof rev - 2; k--) {
        char c = sec[k];
        if (c != '0' && c != '#' && c != 'x') { if (c != '\'' ) rev[r++] = c; continue; }
        if (di > 0) rev[r++] = digits[--di];
        else if (c == '0') rev[r++] = '0';
        else if (c == '#') rev[r++] = ' ';
      }
      if (!x) while (di > 0 && r < (int)sizeof rev - 1) rev[r++] = digits[--di];     /* digits beyond the placeholders */
      for (k = 0; k < r; k++) tmp[k] = rev[r - 1 - k];
      tmp[r] = 0;
    }
  }
  /* put together: what is before the placeholders, the number, the rest (decimals as zeros) */
  if (neg && !hasminus) put(out, cap, &n, "-");
  {
    int q = 0;
    for (i = 0; i < len; i++) {
      char c = sec[i], b2[2];
      if (c == '\'') { q = !q; continue; }
      if (q) { b2[0] = c; b2[1] = 0; put(out, cap, &n, b2); continue; }
      if (first >= 0 && i == first) { put(out, cap, &n, tmp); i = last; continue; }
      if (i == dot) {
        int k, any = 0;
        for (k = dot + 1; k < len && sec[k] != '\''; k++) if (sec[k] == '0') any = 1;
        if (any) put(out, cap, &n, ".");
        for (k = dot + 1; k < len && (sec[k] == '0' || sec[k] == '#' || sec[k] == 'x'); k++) if (sec[k] == '0') put(out, cap, &n, "0");
        i = k - 1;
        continue;
      }
      if (c == '-') { put(out, cap, &n, neg ? "-" : " "); continue; }
      if (c == '+') { put(out, cap, &n, neg ? "-" : "+"); continue; }
      b2[0] = c; b2[1] = 0;
      put(out, cap, &n, b2);
    }
  }
  if (first < 0 && n == 0) snprintf(out, cap, "%ld", v);
}
int field_format_number(const char *code, long v, char *out, size_t cap)
{
  char sw[128];
  int i;
  if (field_switch(code, '#', 0, sw, sizeof sw)) { field_number_picture(sw, v, out, cap); return 1; }
  for (i = 0; field_switch(code, '*', i, sw, sizeof sw); i++) {
    if (str_ieq(sw, "Arabic")) { snprintf(out, cap, "%ld", v); return 1; }
    if (str_ieq(sw, "ArabicDash")) { snprintf(out, cap, "- %ld -", v); return 1; }
    if (str_ieq(sw, "roman")) { char r[64]; roman(v, r, sw[0] == 'R'); snprintf(out, cap, "%s", r); return 1; }
    if (str_ieq(sw, "alphabetic")) {
      long k = v > 0 ? v : 1, cnt = (k - 1) / 26 + 1, j;
      char c = (char)((sw[0] == 'A' ? 'A' : 'a') + (k - 1) % 26);
      size_t m = 0;
      for (j = 0; j < cnt && m + 1 < cap; j++) out[m++] = c;
      out[m] = 0;
      return 1;
    }
    if (str_ieq(sw, "Ordinal")) {
      long a = v < 0 ? -v : v;
      const char *suf = (a % 100 >= 11 && a % 100 <= 13) ? "th" : a % 10 == 1 ? "st" : a % 10 == 2 ? "nd" : a % 10 == 3 ? "rd" : "th";
      snprintf(out, cap, "%ld%s", v, suf);
      return 1;
    }
    if (str_ieq(sw, "CardText")) { card_text(v, out, cap); return 1; }
    if (str_ieq(sw, "OrdText")) { ord_text(v, out, cap); return 1; }
    if (str_ieq(sw, "Hex")) { snprintf(out, cap, "%lX", (unsigned long)v); return 1; }
    if (str_ieq(sw, "DollarText")) { size_t n; card_text(v, out, cap); n = strlen(out); put(out, cap, &n, " and 00/100"); return 1; }
  }
  snprintf(out, cap, "%ld", v);
  return 0;
}
/* a letter's capital or small form, ASCII and Latin-1 (UTF-8 C3 xx) */
static void case_at(char *s, int up)
{
  if (up && *s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
  else if (!up && *s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
  else if ((u8)s[0] == 0xC3) {
    u8 c = (u8)s[1];
    if (up && c >= 0xA0 && c <= 0xBE && c != 0xB7) s[1] = (char)(c - 0x20);
    else if (!up && c >= 0x80 && c <= 0x9E && c != 0x97) s[1] = (char)(c + 0x20);
  }
}
static int letter_at(const char *s) { return (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || ((u8)s[0] == 0xC3 && (u8)s[1] >= 0x80); }
void field_format_case(const char *code, char *s, size_t cap)
{
  char sw[64];
  int i;
  (void)cap;
  for (i = 0; field_switch(code, '*', i, sw, sizeof sw); i++) {
    char *p;
    int start = 1;
    if (str_ieq(sw, "Upper")) for (p = s; *p; p++) case_at(p, 1);
    else if (str_ieq(sw, "Lower")) for (p = s; *p; p++) case_at(p, 0);
    else if (str_ieq(sw, "FirstCap")) { for (p = s; *p; p++) if (letter_at(p)) { case_at(p, 1); break; } }
    else if (str_ieq(sw, "Caps"))
      for (p = s; *p; p++) {
        if (letter_at(p)) { if (start) case_at(p, 1); start = 0; if ((u8)*p == 0xC3) p++; }
        else if (*p == ' ' || *p == '-' || *p == '\t') start = 1;
      }
  }
}

/* ------------------------------------------------------------------ */
/* document properties */
const char *const field_prop_names[] = { "Author", "Category", "Comments", "Company", "Keywords", "Manager", "Status", "Subject", "Title",
  "LastSavedBy", "Template", "RevisionNumber", "CreateTime", "LastSaveTime", "LastPrinted", "TotalEditingTime" };
const int field_nprop_names = (int)(sizeof field_prop_names / sizeof field_prop_names[0]);
/* app.xml's TotalTime (minutes), kept by WP24 among its children; -1 if none */
static long edit_minutes(Doc *d)
{
  int i;
  if (!d->info) return -1;
  for (i = 0; i < d->info->napp; i++)
    if (d->info->app[i].name && !strcmp(d->info->app[i].name, "TotalTime") && d->info->app[i].raw) {
      const char *r = strchr(d->info->app[i].raw, '>');
      return r ? atol(r + 1) : -1;
    }
  return -1;
}
int field_prop(Doc *d, const char *name, char *out, size_t cap)
{
  static const struct { const char *name; int ip; } map[] = {
    { "Subject", IP_SUBJECT }, { "Keywords", IP_KEYWORDS }, { "Comments", IP_DESCRIPTION }, { "Description", IP_DESCRIPTION },
    { "Category", IP_CATEGORY }, { "Status", IP_STATUS }, { "Company", IP_COMPANY }, { "Manager", IP_MANAGER },
    { "LastSavedBy", IP_LASTBY }, { "Template", IP_TEMPLATE }, { "RevisionNumber", IP_REVISION }, { "CreateTime", IP_CREATED },
    { "LastSaveTime", IP_MODIFIED }, { "LastPrinted", IP_LASTPRINTED }, { "HyperlinkBase", IP_HLINKBASE }, { "Language", IP_LANGUAGE } };
  const char *v = NULL;
  char b[32];
  size_t i;
  if (cap) out[0] = 0;
  if (str_ieq(name, "Title")) v = d->title;
  else if (str_ieq(name, "Author")) v = d->author;
  else if (str_ieq(name, "TotalEditingTime")) { long m = edit_minutes(d); if (m >= 0) { sprintf(b, "%ld", m); v = b; } }
  else {
    for (i = 0; i < sizeof map / sizeof map[0] && !v; i++) if (str_ieq(name, map[i].name)) v = info_str(d, map[i].ip);
    if (!v && d->info) { int k = info_cust_find(d, name); if (k >= 0) v = d->info->cust[k].value; }
  }
  if (!v || !v[0]) return 0;
  snprintf(out, cap, "%s", v);
  return 1;
}

/* ------------------------------------------------------------------ */
/* finding fields in an array of paragraphs (the body, or a story) */
typedef struct { Para **p; u32 np; } Paras;
static Paras body(Doc *d) { Paras s; s.p = d->p; s.np = d->np; return s; }
/* the next mark in q at or after off: its offset, or q->len */
static u32 next_mark(const Para *q, u32 off)
{
  while (off + 4 <= q->len) {
    const char *f = (const char *)memchr(q->t + off, 0xF4, q->len - off);
    if (!f) break;
    off = (u32)(f - q->t);
    if (off + 4 <= q->len && IS_FMARK(f)) return off;
    off++;
  }
  return q->len;
}
static int has_marks(const Para *q) { return next_mark(q, 0) < q->len; }
/* field k's end mark from its start mark at s */
static int find_end(Paras s, Pos st, Pos *e)
{
  u32 pi;
  int k = fmark_index(s.p[st.para]->t + st.off), depth = 0;
  for (pi = st.para; pi < s.np; pi++) {
    Para *q = s.p[pi];
    u32 o = pi == st.para ? st.off : 0;
    for (o = next_mark(q, o); o < q->len; o = next_mark(q, o + 4)) {
      if (fmark_index(q->t + o) != k) continue;
      if (IS_FSTART(q->t + o)) depth++;
      else if (--depth == 0) { e->para = pi; e->off = o; return 1; }
    }
  }
  return 0;
}
int field_end(Doc *d, Pos s, Pos *e)
{
  if (s.para >= d->np || s.off + 4 > d->p[s.para]->len || !IS_FSTART(d->p[s.para]->t + s.off)) return 0;
  return find_end(body(d), s, e);
}
/* the marks of q before lim: their offsets into m (the last max of them, in order); how many */
static int marks_before(const Para *q, u32 lim, u32 *m, int max)
{
  int n = 0;
  u32 o;
  for (o = next_mark(q, 0); o < q->len && o + 4 <= lim; o = next_mark(q, o + 4)) {
    if (n == max) { memmove(m, m + 1, (size_t)(max - 1) * sizeof(u32)); n--; }
    m[n++] = o;
  }
  return n;
}
/* the innermost field round p in s: going back from p, an end mark closes its field (seen), a start
   mark of a field not closed so is the one */
static int find_at(Doc *d, Paras s, Pos p, Pos *ps, Pos *pe)
{
  u32 pi, lim, m[256];
  u16 *seen;
  int k = -1;
  Pos st, en;
  if (p.para >= s.np || !d->nfields) return -1;
  {
    Para *q = s.p[p.para];
    if (p.off > q->len) p.off = q->len;
    if (p.off + 4 <= q->len && IS_FSTART(q->t + p.off)) { st = p; k = fmark_index(q->t + p.off); goto found; }
    if (p.off >= 4 && IS_FEND(q->t + p.off - 4)) p.off -= 4;          /* just after an end mark: that field */
  }
  if (!(seen = (u16 *)calloc((size_t)d->nfields, sizeof(u16)))) return -1;
  lim = p.para > 300 ? p.para - 300 : 0;
  for (pi = p.para + 1; pi-- > lim && k < 0;) {
    Para *q = s.p[pi];
    int nm = marks_before(q, pi == p.para ? p.off : q->len, m, 256);
    while (nm-- > 0 && k < 0) {
      const char *t = q->t + m[nm];
      int j = fmark_index(t);
      if (j >= d->nfields) continue;
      if (IS_FEND(t)) seen[j]++;
      else if (seen[j]) seen[j]--;
      else { k = j; st.para = pi; st.off = m[nm]; }
    }
  }
  free(seen);
  if (k < 0) return -1;
found:
  if (!find_end(s, st, &en)) return -1;
  if (pos_cmp(p, en) > 0) return -1;
  if (ps) *ps = st;
  if (pe) *pe = en;
  return k;
}
int field_at(Doc *d, Pos p, Pos *s, Pos *e) { return find_at(d, body(d), p, s, e); }
int field_next(Doc *d, Pos p, int dir, Pos *s, Pos *e)
{
  Paras b = body(d);
  u32 pi;
  Pos st, en;
  if (!d->nfields || p.para >= b.np) return -1;
  if (dir > 0) {
    /* the first start mark at or after p */
    for (pi = p.para; pi < b.np; pi++) {
      Para *q = b.p[pi];
      u32 o;
      for (o = next_mark(q, pi == p.para ? p.off : 0); o < q->len; o = next_mark(q, o + 4)) {
        if (!IS_FSTART(q->t + o)) continue;
        st.para = pi; st.off = o;
        if (!find_end(b, st, &en)) continue;
        if (s) *s = st;
        if (e) *e = en;
        return fmark_index(q->t + o);
      }
    }
    return -1;
  }
  /* the last start mark before p (not the one just before it: a field selected from its start) */
  pi = p.para + 1;
  while (pi-- > 0) {
    Para *q = b.p[pi];
    u32 m[256];
    int nm = marks_before(q, pi == p.para ? (p.off >= 4 ? p.off - 4 : 0) : q->len, m, 256);
    while (nm-- > 0) {
      if (!IS_FSTART(q->t + m[nm])) continue;
      st.para = pi; st.off = m[nm];
      if (!find_end(b, st, &en)) continue;
      if (s) *s = st;
      if (e) *e = en;
      return fmark_index(q->t + m[nm]);
    }
  }
  return -1;
}
/* the fields whose start marks lie in a..b of s, and the one round a */
static int list_in(Doc *d, Paras s, Pos a, Pos b, Pos *at, int max)
{
  int n = 0;
  u32 pi;
  Pos st;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (find_at(d, s, a, &st, NULL) >= 0 && pos_cmp(st, a) < 0 && n < max) at[n++] = st;
  for (pi = a.para; pi <= b.para && pi < s.np; pi++) {
    Para *q = s.p[pi];
    u32 o;
    for (o = next_mark(q, pi == a.para ? a.off : 0); o < q->len; o = next_mark(q, o + 4)) {
      if (pi == b.para && o > b.off) break;
      if (!IS_FSTART(q->t + o)) continue;
      if (n < max) { at[n].para = pi; at[n].off = o; }
      n++;
    }
  }
  return n;
}
int field_list(Doc *d, Pos a, Pos b, Pos *at, int max) { return list_in(d, body(d), a, b, at, max); }

/* ------------------------------------------------------------------ */
/* evaluating */
void field_env(FieldEnv *e) { memset(e, 0, sizeof *e); field_time_now(&e->now); }
static int eval_flat(Doc *d, Paras s, const char *code, Pos at, const FieldEnv *e, char *out, size_t cap, long seqv);
static int eval_nested;          /* evaluating a field inside another's code */
/* code with each nested field replaced by its result (worked out, or the one it has) */
static char *resolve(Doc *d, Paras s, const char *code, Pos at, const FieldEnv *e)
{
  Buf b;
  buf_init(&b);
  while (*code) {
    if (*code == FLD_OPEN) {
      const char *end = skip_nest(code), *sep = code + 1;
      int depth = 0;
      char *inner, *flat, res[512];
      /* its code runs to its own separator (at its own depth) */
      while (sep < end) { if (*sep == FLD_OPEN) depth++; else if (*sep == FLD_CLOSE) depth--; else if (*sep == FLD_SEP && depth == 0) break; sep++; }
      inner = str_ndup(code + 1, (size_t)(sep - code - 1));
      flat = inner ? resolve(d, s, inner, at, e) : NULL;
      int got;
      eval_nested++;
      got = flat && eval_flat(d, s, flat, at, e, res, sizeof res, -1) == FE_TEXT;
      eval_nested--;
      if (got) buf_adds(&b, res);
      else if (sep < end) {
        /* its own result, without nested fields' codes */
        const char *r = sep + 1;
        int dep = 0, skip = 0;
        for (; r < end - 1; r++) {
          if (*r == FLD_OPEN) { dep++; skip = 1; continue; }
          if (*r == FLD_SEP && dep) { skip = 0; continue; }
          if (*r == FLD_CLOSE && dep) { dep--; continue; }
          if (!skip || !dep) buf_addc(&b, *r);
        }
      }
      free(inner); free(flat);
      code = end;
      continue;
    }
    buf_addc(&b, *code++);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return b.data ? buf_take(&b) : str_dup("");
}
/* a number result with the code's formats */
static int number_result(const char *code, long v, char *out, size_t cap)
{
  field_format_number(code, v, out, cap);
  field_format_case(code, out, cap);
  return FE_TEXT;
}
/* a date result: the \@ picture, or def */
static int date_result(const char *code, const FieldTime *t, const char *def, char *out, size_t cap)
{
  char pic[128];
  if (!field_switch(code, '@', 0, pic, sizeof pic) || !pic[0]) snprintf(pic, sizeof pic, "%s", def);
  field_format_date(pic, t, out, cap);
  field_format_case(code, out, cap);
  return FE_TEXT;
}
static int text_result(const char *code, const char *v, char *out, size_t cap)
{
  snprintf(out, cap, "%s", v);
  field_format_case(code, out, cap);
  return FE_TEXT;
}
/* the heading level of a paragraph's style (Heading 1-9), 0 if none */
static int heading_level(Doc *d, const Para *q)
{
  const Style *st = q->pf.style < d->nstyles ? &d->styles[q->pf.style] : NULL;
  const char *nm = st ? st->name : "";
  if (ieq_n(nm, "heading ", 8) && nm[8] >= '1' && nm[8] <= '9' && !nm[9]) return nm[8] - '0';
  if (st && ieq_n(st->id, "Heading", 7) && st->id[7] >= '1' && st->id[7] <= '9' && !st->id[8]) return st->id[7] - '0';
  return 0;
}
/* SEQ counters: one per identifier; minhead: the highest heading (smallest level) met since its last
   field (0 none), for \s n, which starts the numbering again after a heading of level n or above */
typedef struct { char id[48]; long v; int minhead; } SeqCounter;
typedef struct { SeqCounter c[32]; int n; } SeqState;
static SeqCounter *seq_slot(SeqState *ss, const char *id)
{
  int i;
  for (i = 0; i < ss->n; i++) if (str_ieq(ss->c[i].id, id)) return &ss->c[i];
  if (ss->n >= 32) return NULL;
  memset(&ss->c[ss->n], 0, sizeof ss->c[0]);
  snprintf(ss->c[ss->n].id, sizeof ss->c[ss->n].id, "%s", id);
  return &ss->c[ss->n++];
}
static void seq_heading(SeqState *ss, int hl)
{
  int i;
  for (i = 0; i < ss->n; i++) if (!ss->c[i].minhead || hl < ss->c[i].minhead) ss->c[i].minhead = hl;
}
/* a SEQ field met in order: its number (and the counter moved on) */
static long seq_step(SeqState *ss, const char *code)
{
  char id[48], v[32];
  SeqCounter *c;
  if (!field_arg(code, 0, id, sizeof id) || !(c = seq_slot(ss, id))) return 0;
  if (field_switch(code, 's', 0, v, sizeof v) && atoi(v) > 0 && c->minhead && c->minhead <= atoi(v)) c->v = 0;
  c->minhead = 0;
  if (field_switch(code, 'r', 0, v, sizeof v) && v[0]) c->v = atol(v);
  else if (field_switch(code, 'c', 0, v, sizeof v)) ;
  else c->v++;
  return c->v;
}
/* the SEQ fields of s in order up to (not including) stop (NULL: all): each one's number to cb */
static void seq_walk(Doc *d, Paras s, SeqState *ss, const Pos *stop, void (*cb)(void *, Pos, long), void *arg)
{
  u32 pi;
  for (pi = 0; pi < s.np && (!stop || pi <= stop->para); pi++) {
    Para *q = s.p[pi];
    u32 o;
    int hl = heading_level(d, q);
    if (hl) seq_heading(ss, hl);
    for (o = next_mark(q, 0); o < q->len; o = next_mark(q, o + 4)) {
      int k;
      char nm[8];
      Pos at;
      if (stop && pi == stop->para && o >= stop->off) break;
      if (!IS_FSTART(q->t + o) || (k = fmark_index(q->t + o)) >= d->nfields) continue;
      field_name(d->fields[k].code, nm, sizeof nm);
      if (strcmp(nm, "SEQ")) continue;
      at.para = pi; at.off = o;
      { long v = seq_step(ss, d->fields[k].code); if (cb) cb(arg, at, v); }
    }
  }
}
/* the SEQ number a field with flat at at would have */
static long seq_value(Doc *d, Paras s, Pos at, const char *flat)
{
  SeqState ss;
  memset(&ss, 0, sizeof ss);
  seq_walk(d, s, &ss, &at, NULL, NULL);
  return seq_step(&ss, flat);
}
/* the text of a bookmark (its first paragraph's stretch; marks and objects left out) */
static int bookmark_text(Doc *d, const char *name, char *out, size_t cap)
{
  Pos a, b;
  u32 o, n = 0;
  Para *q;
  if (!doc_bmark_find(d, name, &a, &b)) return 0;
  q = d->p[a.para];
  for (o = a.off; o < (a.para == b.para ? b.off : q->len) && n + 1 < cap; o++) {
    u8 c = (u8)q->t[o];
    if ((c == 0xF3 || c == 0xF4) && o + 4 <= q->len) { o += 3; continue; }
    if (c == 0xEE && o + 3 <= q->len && IS_FIELD(q->t + o)) { o += 2; continue; }
    if (c == '\v' || c == '\t') c = ' ';
    out[n++] = (char)c;
  }
  out[n] = 0;
  return 1;
}
/* the non-space characters of the body (NUMCHARS) */
static long count_chars(Doc *d)
{
  long n = 0;
  u32 pi, o;
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    for (o = 0; o < q->len; o++) {
      u8 c = (u8)q->t[o];
      if ((c & 0xC0) == 0x80) continue;
      if ((c == 0xF3 || c == 0xF4) && o + 4 <= q->len) { o += 3; continue; }
      if (c != ' ' && c != '\t' && c != '\v') n++;
    }
  }
  return n;
}
/* a number from a string, if it is one (IF's comparisons) */
static int as_number(const char *s, long *v)
{
  char *e;
  while (*s == ' ') s++;
  if (!*s) return 0;
  *v = strtol(s, &e, 10);
  while (*e == ' ') e++;
  return *e == 0;
}
/* Word's wildcards in IF's = and <>: ? one character, * any run */
static int wild(const char *p, const char *s)
{
  if (!*p) return !*s;
  if (*p == '*') { for (;;) { if (wild(p + 1, s)) return 1; if (!*s) return 0; s++; } }
  if (!*s) return 0;
  if (*p == '?' || upc(*p) == upc(*s)) return wild(p + 1, s + 1);
  return 0;
}
static int eval_if(const char *flat, char *out, size_t cap)
{
  char a[256], op[8], b[256], t[512], f[512];
  int c, r;
  long x, y;
  if (!field_arg(flat, 0, a, sizeof a) || !field_arg(flat, 1, op, sizeof op) || !field_arg(flat, 2, b, sizeof b)) return FE_KEEP;
  field_arg(flat, 3, t, sizeof t); field_arg(flat, 4, f, sizeof f);
  if (as_number(a, &x) && as_number(b, &y)) c = x < y ? -1 : x > y;
  else c = strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0;
  if (!strcmp(op, "=")) r = strchr(b, '*') || strchr(b, '?') ? wild(b, a) : c == 0;
  else if (!strcmp(op, "<>")) r = strchr(b, '*') || strchr(b, '?') ? !wild(b, a) : c != 0;
  else if (!strcmp(op, "<")) r = c < 0;
  else if (!strcmp(op, ">")) r = c > 0;
  else if (!strcmp(op, "<=")) r = c <= 0;
  else if (!strcmp(op, ">=")) r = c >= 0;
  else return FE_KEEP;
  return text_result(flat, r ? t : f, out, cap);
}
/* the default pictures (Word's are the system's short date and time) */
#define DEF_DATE "d MMMM yyyy"
#define DEF_TIME "HH:mm"
#define DEF_DATETIME "d MMMM yyyy HH:mm"
static int eval_flat(Doc *d, Paras s, const char *flat, Pos at, const FieldEnv *e, char *out, size_t cap, long seqv)
{
  char nm[32], a0[256];
  FieldInfo *fi = d->finfo;
  FieldTime t;
  long em;
  if (cap) out[0] = 0;
  field_name(flat, nm, sizeof nm);
  if (!nm[0]) return FE_KEEP;
  if (!strcmp(nm, "DATE")) return e ? date_result(flat, &e->now, DEF_DATE, out, cap) : FE_KEEP;
  if (!strcmp(nm, "TIME")) return e ? date_result(flat, &e->now, DEF_TIME, out, cap) : FE_KEEP;
  if (!strcmp(nm, "CREATEDATE") || !strcmp(nm, "SAVEDATE") || !strcmp(nm, "PRINTDATE")) {
    const char *w = info_str(d, nm[0] == 'C' ? IP_CREATED : nm[1] == 'A' ? IP_MODIFIED : IP_LASTPRINTED);
    if (!w || !field_parse_time(w, &t)) return FE_KEEP;
    return date_result(flat, &t, DEF_DATETIME, out, cap);
  }
  if (!strcmp(nm, "PAGE")) { snprintf(out, cap, "%s", FIELD_PAGE); return FE_PLACE; }
  if (!strcmp(nm, "SECTION")) { snprintf(out, cap, "%s", FIELD_PAGE); return FE_PLACE; }
  if (!strcmp(nm, "NUMPAGES") || !strcmp(nm, "SECTIONPAGES")) { snprintf(out, cap, "%s", FIELD_PAGES); return FE_PLACE; }
  if (!strcmp(nm, "PAGEREF")) { if (!field_arg(flat, 0, a0, sizeof a0)) return FE_KEEP; snprintf(out, cap, "%s", FIELD_PAGEREF); return FE_PLACE; }
  if (!strcmp(nm, "NUMWORDS")) return number_result(flat, (long)doc_count_words(d), out, cap);
  if (!strcmp(nm, "NUMCHARS")) return number_result(flat, count_chars(d), out, cap);
  if (!strcmp(nm, "SEQ")) {
    if (field_switch(flat, 'h', 0, a0, sizeof a0)) return FE_TEXT;     /* hidden: no result */
    return number_result(flat, seqv >= 0 ? seqv : seq_value(d, s, at, flat), out, cap);
  }
  if (!strcmp(nm, "REF")) {
    if (!field_arg(flat, 0, a0, sizeof a0) || field_switch(flat, 'n', 0, NULL, 0) || field_switch(flat, 'r', 0, NULL, 0) || field_switch(flat, 'w', 0, NULL, 0)) return FE_KEEP;
    { char v[512]; if (!bookmark_text(d, a0, v, sizeof v)) return FE_KEEP; return text_result(flat, v, out, cap); }
  }
  if (!strcmp(nm, "MERGEFIELD")) {
    char v[512], bf[128], af[128];
    if (!field_arg(flat, 0, a0, sizeof a0)) return FE_KEEP;
    if (e && e->merge && e->merge(e->ctx, a0, v, sizeof v)) {
      if (v[0] && field_switch(flat, 'b', 0, bf, sizeof bf)) { char w[700]; snprintf(w, sizeof w, "%s%s", bf, v); memcpy(v, w, sizeof v - 1); v[sizeof v - 1] = 0; }
      if (v[0] && field_switch(flat, 'f', 0, af, sizeof af)) { size_t n = strlen(v); put(v, sizeof v, &n, af); }
      return text_result(flat, v, out, cap);
    }
    if (eval_nested) return FE_KEEP;                          /* (in another's code: the value it had) */
    snprintf(out, cap, "\xC2\xAB%s\xC2\xBB", a0);            /* «name», as Word shows a merge field without data */
    return FE_TEXT;
  }
  if (!strcmp(nm, "IF")) return eval_if(flat, out, cap);
  if (!strcmp(nm, "QUOTE")) { if (!field_arg(flat, 0, a0, sizeof a0)) return FE_KEEP; return text_result(flat, a0, out, cap); }
  if (!strcmp(nm, "SYMBOL")) {
    char fnt[64];
    long cp;
    if (!field_arg(flat, 0, a0, sizeof a0)) return FE_KEEP;
    cp = strtol(a0, NULL, 0);
    if (field_switch(flat, 'f', 0, fnt, sizeof fnt) && fnt[0] && !str_ieq(fnt, "Times New Roman") && !str_ieq(fnt, "Arial")) return FE_KEEP;
    if (cp < 32 || cp > 0x10FFFF || cap < 5) return FE_KEEP;
    out[utf8_put(out, (u32)cp)] = 0;
    return FE_TEXT;
  }
  if (!strcmp(nm, "USERNAME") || !strcmp(nm, "USERINITIALS")) {
    const char *u = e ? (nm[4] == 'N' ? e->user : e->initials) : NULL;
    if (!u || !u[0]) {
      if (nm[4] == 'N') u = d->author;
      else { static char ini[16]; const char *p = d->author; int n = 0; while (*p && n < 8) { if (p == d->author || p[-1] == ' ') ini[n++] = upc(*p); p++; } ini[n] = 0; u = ini; }
    }
    if (!u[0]) return FE_KEEP;
    return text_result(flat, u, out, cap);
  }
  if (!strcmp(nm, "FILENAME")) {
    const char *f = e && e->filename && e->filename[0] ? e->filename : fi ? fi->filename : "";
    const char *base = f, *p;
    if (!f[0]) return FE_KEEP;
    if (!field_switch(flat, 'p', 0, NULL, 0)) for (p = f; *p; p++) if (*p == '/' || *p == ':' || *p == '\\') base = p + 1;
    return text_result(flat, base, out, cap);
  }
  if (!strcmp(nm, "EDITTIME")) return (em = edit_minutes(d)) >= 0 ? number_result(flat, em, out, cap) : FE_KEEP;
  if (!strcmp(nm, "REVNUM")) return info_str(d, IP_REVISION)[0] ? number_result(flat, atol(info_str(d, IP_REVISION)), out, cap) : FE_KEEP;
  {
    /* the document's properties, by their own fields, DOCPROPERTY or INFO */
    static const char *const map[][2] = { { "AUTHOR", "Author" }, { "TITLE", "Title" }, { "SUBJECT", "Subject" }, { "KEYWORDS", "Keywords" },
      { "COMMENTS", "Comments" }, { "LASTSAVEDBY", "LastSavedBy" }, { "TEMPLATE", "Template" } };
    const char *prop = NULL;
    char pn[64], v[512];
    size_t i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++) if (!strcmp(nm, map[i][0])) prop = map[i][1];
    if (!prop && (!strcmp(nm, "DOCPROPERTY") || !strcmp(nm, "INFO")) && field_arg(flat, 0, pn, sizeof pn)) {
      prop = pn;
      for (i = 0; i < sizeof map / sizeof map[0]; i++) if (str_ieq(pn, map[i][0])) prop = map[i][1];
      if (str_ieq(pn, "CreateDate")) prop = "CreateTime";
      else if (str_ieq(pn, "SaveDate")) prop = "LastSaveTime";
      else if (str_ieq(pn, "PrintDate")) prop = "LastPrinted";
      else if (str_ieq(pn, "NumWords") || str_ieq(pn, "Words")) return number_result(flat, (long)doc_count_words(d), out, cap);
      else if (str_ieq(pn, "NumChars") || str_ieq(pn, "Characters")) return number_result(flat, count_chars(d), out, cap);
      else if ((str_ieq(pn, "NumPages") || str_ieq(pn, "Pages")) && e && e->L) return number_result(flat, (long)e->L->npages, out, cap);
    }
    if (prop) {
      if (!field_prop(d, prop, v, sizeof v)) return FE_KEEP;
      if ((str_ieq(prop, "CreateTime") || str_ieq(prop, "LastSaveTime") || str_ieq(prop, "LastPrinted")) && field_parse_time(v, &t))
        return date_result(flat, &t, DEF_DATETIME, out, cap);
      { long nv; if ((field_switch(flat, '#', 0, NULL, 0) || field_switch(flat, '*', 0, NULL, 0)) && as_number(v, &nv)) return number_result(flat, nv, out, cap); }
      return text_result(flat, v, out, cap);
    }
  }
  return FE_KEEP;
}
int field_eval_code(Doc *d, const char *code, Pos at, const FieldEnv *e, char *out, size_t cap)
{
  Paras s = body(d);
  char *flat = resolve(d, s, code, at, e);
  int r;
  if (!flat) return FE_KEEP;
  r = eval_flat(d, s, flat, at, e, out, cap, -1);
  free(flat);
  return r;
}
int field_eval(Doc *d, int k, Pos at, const FieldEnv *e, char *out, size_t cap)
{
  if (k < 0 || k >= d->nfields) return FE_KEEP;
  return field_eval_code(d, d->fields[k].code, at, e, out, cap);
}

/* ------------------------------------------------------------------ */
/* edits */
/* a copy of p with bytes a..b replaced by s (n bytes) formatted f; NULL if out of memory */
static Para *splice(const Para *p, u32 a, u32 b, const char *s, u32 n, const CharFmt *f)
{
  Para *q = para_new(&p->pf, &p->r[0].f);
  u32 r, start = 0;
  if (!q) return NULL;
  for (r = 0; r < p->nr; r++) {
    u32 e = p->r[r].end, hi = e < a ? e : a;
    if (hi > start && !para_append(q, p->t + start, hi - start, &p->r[r].f)) goto oom;
    start = e;
  }
  if (n && !para_append(q, s, n, f)) goto oom;
  for (r = 0, start = 0; r < p->nr; r++) {
    u32 e = p->r[r].end, lo = start > b ? start : b;
    if (e > lo && !para_append(q, p->t + lo, e - lo, &p->r[r].f)) goto oom;
    start = e;
  }
  para_normalise(q);
  return q;
oom:
  para_free(q);
  return NULL;
}
/* paragraph pi of s becomes q: in the body through doc_replace (undoable), in a story directly */
static int put_para(Doc *d, Paras *s, int undo, u32 pi, Para *q)
{
  if (!q) return 0;
  if (undo) {
    Pos w;
    w.para = pi; w.off = 0;
    if (!doc_replace(d, pi, 1, &q, 1, w)) return 0;
    s->p = d->p;
    return 1;
  }
  para_free(s->p[pi]);
  s->p[pi] = q;
  d->ver++; d->dirty = 1;
  return 1;
}
/* the formatting for a field's result: its first character's (or the start mark's) */
static CharFmt result_fmt(const Para *q, u32 s, u32 e)
{
  CharFmt f = *para_fmt_at((Para *)q, s + 4 < e ? s + 4 : s);
  return f;
}
/* the text a field's result should have, and its link (REF and PAGEREF: their bookmark's) */
static int new_result(Doc *d, Paras s, int k, Pos at, const FieldEnv *e, long seqv, char *out, size_t cap, u16 *link)
{
  char *flat = resolve(d, s, d->fields[k].code, at, e), bm[200], url[210];
  int r, kind;
  *link = 0;
  if (!flat) return FE_KEEP;
  r = eval_flat(d, s, flat, at, e, out, cap, seqv);
  kind = field_kind(flat);
  if (r != FE_KEEP && (kind == FC_REF || kind == FK_PAGEREF) && field_arg(flat, 0, bm, sizeof bm)) {
    snprintf(url, sizeof url, "#%s", bm);
    *link = (u16)doc_link(d, url);
  }
  free(flat);
  { char *p; for (p = out; *p; p++) if (*p == '\n' || *p == '\r') *p = ' '; }
  return r;
}
/* the SEQ numbers of the fields at[0..n) of s (counted over all of s in order); -1 for other fields */
typedef struct { const Pos *at; int n; long *v; int j; } SeqOut;
static void seq_out(void *arg, Pos p, long v)
{
  SeqOut *o = (SeqOut *)arg;
  while (o->j < o->n && pos_cmp(o->at[o->j], p) < 0) o->j++;
  if (o->j < o->n && !pos_cmp(o->at[o->j], p)) o->v[o->j] = v;
}
static void seq_numbers(Doc *d, Paras s, const Pos *at, int n, long *v)
{
  SeqState ss;
  SeqOut so;
  int i, any = 0;
  for (i = 0; i < n; i++) {
    Para *q = s.p[at[i].para];
    int k = fmark_index(q->t + at[i].off);
    char nm[8];
    v[i] = -1;
    if (k < d->nfields) { field_name(d->fields[k].code, nm, sizeof nm); if (!strcmp(nm, "SEQ")) any = 1; }
  }
  if (!any) return;
  memset(&ss, 0, sizeof ss);
  so.at = at; so.n = n; so.v = v; so.j = 0;
  seq_walk(d, s, &ss, NULL, seq_out, &so);
}
#define MAXLIST 4096
/* update the fields of s round and in a..b */
static int update_in(Doc *d, Paras *s, int undo, Pos a, Pos b, const FieldEnv *e)
{
  Pos *at;
  long *seq;
  int n, i, done = 0;
  if (!d->nfields || !s->np) return 0;
  if (!(at = (Pos *)malloc(MAXLIST * sizeof(Pos)))) return 0;
  n = list_in(d, *s, a, b, at, MAXLIST);
  if (n > MAXLIST) n = MAXLIST;
  if (!(seq = (long *)malloc((size_t)(n ? n : 1) * sizeof(long)))) { free(at); return 0; }
  seq_numbers(d, *s, at, n, seq);
  for (i = n; i-- > 0;) {
    Para *q = s->p[at[i].para];
    int k, r;
    Pos en;
    char res[1024];
    u16 link;
    CharFmt f;
    if (at[i].off + 4 > q->len || !IS_FSTART(q->t + at[i].off)) continue;
    k = fmark_index(q->t + at[i].off);
    if (k >= d->nfields || d->fields[k].locked || !find_end(*s, at[i], &en) || en.para != at[i].para) continue;
    r = new_result(d, *s, k, at[i], e, seq[i], res, sizeof res, &link);
    if (r == FE_KEEP) continue;
    if (en.off - at[i].off - 4 == strlen(res) && !memcmp(q->t + at[i].off + 4, res, strlen(res))) continue;
    f = result_fmt(q, at[i].off, en.off);
    if (link) f.link = link;
    if (d->fields[k].dirty) d->fields[k].dirty = 0;
    if (put_para(d, s, undo, at[i].para, splice(q, at[i].off + 4, en.off, res, (u32)strlen(res), &f))) done++;
  }
  free(seq); free(at);
  return done;
}
int field_update(Doc *d, Pos a, Pos b, const FieldEnv *e)
{
  Paras s = body(d);
  int n;
  doc_begin(d, a);
  n = update_in(d, &s, 1, a, b, e);
  doc_end(d, a);
  return n;
}
/* every story of the document (not the paragraphs being edited in the body's place): fn on each */
static int each_story(Doc *d, int (*fn)(Doc *, Paras *, void *), void *arg)
{
  int i, n = 0;
  Paras s;
#define STORY(st) do { s.p = (st)->p; s.np = (st)->np; if (s.np) { n += fn(d, &s, arg); (st)->p = s.p; } } while (0)
  for (i = 0; i < HF_N; i++) { STORY(&d->hdr[i]); STORY(&d->ftr[i]); }
  for (i = 0; i < d->nsects; i++) if (d->sects[i].hf) { int j; for (j = 0; j < 2 * HF_N; j++) STORY(&d->sects[i].hf[j]); }
  for (i = 0; i < d->nnotes; i++) STORY(&d->notes[i]);
  for (i = 0; i < d->nenotes; i++) STORY(&d->enotes[i]);
  for (i = 0; i < d->ncomments; i++) STORY(&d->comments[i].text);
  for (i = 0; i < d->nimages; i++) if (d->images[i].text && !d->images[i].shared) STORY(d->images[i].text);
#undef STORY
  return n;
}
static int update_story(Doc *d, Paras *s, void *arg)
{
  Pos a, b;
  a.para = 0; a.off = 0; b.para = s->np - 1; b.off = s->p[s->np - 1]->len;
  return update_in(d, s, 0, a, b, (const FieldEnv *)arg);
}
int field_update_all(Doc *d, const FieldEnv *e)
{
  int n = field_update(d, doc_start(d), doc_end_pos(d), e);
  if (!d->edit.kind) n += each_story(d, update_story, (void *)e);
  return n;
}
/* the number a page placeholder stands for at pos (L laid out), into out */
static int place_number(Doc *d, const FieldEnv *e, const Para *q, u32 off, Pos pos, char *out, size_t cap)
{
  const char *code = NULL;
  int kind = field_placeholder(d, q, off, &code);
  u32 page;
  i32 x, y, h;
  long v, first = d->pgstart > 1 ? d->pgstart : 1;
  if (!e || !e->L) return 0;
  lay_caret(e->L, pos, &page, &x, &y, &h);
  if ((u8)q->t[off + 2] == 0x82 || kind == FK_PAGEREF) {
    const char *u = doc_link_url(d, para_fmt_at((Para *)q, off)->link);
    int pg = lay_bmark_page(e->L, u[0] == '#' ? u + 1 : u);
    if (pg <= 0) return 0;
    v = pg - 1 + first;
  } else if (kind == FK_SECTION || kind == FK_SECTIONPAGES) {
    u32 pi, sec = 1, s0 = 0, s1;
    u32 pa, pb;
    for (pi = 0; pi < pos.para && pi < d->np; pi++) if (d->p[pi]->pf.sect) { sec++; s0 = pi + 1; }
    if (kind == FK_SECTION) v = (long)sec;
    else {
      Pos ps, pe;
      s1 = doc_sect_end(d, pos.para);
      ps.para = s0; ps.off = 0; pe.para = s1; pe.off = d->p[s1]->len;
      lay_caret(e->L, ps, &pa, &x, &y, &h); lay_caret(e->L, pe, &pb, &x, &y, &h);
      v = (long)(pb - pa + 1);
    }
  } else if ((u8)q->t[off + 2] == 0x81) v = (long)e->L->npages;
  else v = (long)page + first;
  if (code) number_result(code, v, out, cap); else snprintf(out, cap, "%ld", v);
  return 1;
}
int field_unlink(Doc *d, Pos a, Pos b, const FieldEnv *e)
{
  Pos *at;
  Paras s = body(d);
  int n, i, done = 0;
  if (!d->nfields) return 0;
  if (!(at = (Pos *)malloc(MAXLIST * sizeof(Pos)))) return 0;
  n = list_in(d, s, a, b, at, MAXLIST);
  if (n > MAXLIST) n = MAXLIST;
  doc_begin(d, a);
  for (i = n; i-- > 0;) {
    Pos en;
    Para *q = d->p[at[i].para];
    u32 o, lim;
    if (at[i].off + 4 > q->len || !IS_FSTART(q->t + at[i].off) || !find_end(s, at[i], &en)) continue;
    /* the end mark, the placeholders in the result (in the start's paragraph), the start mark */
    q = d->p[en.para];
    put_para(d, &s, 1, en.para, splice(q, en.off, en.off + 4, "", 0, NULL));
    q = d->p[at[i].para];
    lim = en.para == at[i].para ? en.off : q->len;
    for (o = lim; o-- > at[i].off + 4;) {
      char num[64];
      Pos pp;
      if (o + 3 > q->len || !IS_FIELD(q->t + o)) continue;
      pp.para = at[i].para; pp.off = o;
      if (place_number(d, e, q, o, pp, num, sizeof num)) {
        CharFmt f = *para_fmt_at(q, o);
        f.link = 0;
        if (put_para(d, &s, 1, at[i].para, splice(q, o, o + 3, num, (u32)strlen(num), &f))) q = d->p[at[i].para];
      }
    }
    if (put_para(d, &s, 1, at[i].para, splice(q, at[i].off, at[i].off + 4, "", 0, NULL))) done++;
  }
  doc_end(d, a);
  free(at);
  return done;
}
/* field at s (its end at en) gets record nk: both marks replaced */
static int swap_marks(Doc *d, Paras *s, Pos st, Pos en, int nk)
{
  char m[8];
  Para *q = s->p[en.para];
  CharFmt f = *para_fmt_at(q, en.off);
  fmark_utf8(m, nk, 1);
  if (!put_para(d, s, 1, en.para, splice(q, en.off, en.off + 4, m, 4, &f))) return 0;
  q = s->p[st.para];
  f = *para_fmt_at(q, st.off);
  fmark_utf8(m, nk, 0);
  return put_para(d, s, 1, st.para, splice(q, st.off, st.off + 4, m, 4, &f));
}
int field_lock(Doc *d, Pos a, Pos b, int lock)
{
  Pos *at;
  Paras s = body(d);
  int n, i, done = 0;
  if (!d->nfields) return 0;
  if (!(at = (Pos *)malloc(MAXLIST * sizeof(Pos)))) return 0;
  n = list_in(d, s, a, b, at, MAXLIST);
  if (n > MAXLIST) n = MAXLIST;
  doc_begin(d, a);
  for (i = n; i-- > 0;) {
    Pos en;
    int k, nk;
    Para *q = d->p[at[i].para];
    if (at[i].off + 4 > q->len || !IS_FSTART(q->t + at[i].off) || !find_end(s, at[i], &en)) continue;
    k = fmark_index(q->t + at[i].off);
    if (k >= d->nfields || d->fields[k].locked == (lock != 0)) continue;
    if ((nk = new_rec(d, d->fields[k].code, &d->fields[k])) < 0) break;
    d->fields[nk].locked = (u8)(lock != 0);
    if (swap_marks(d, &s, at[i], en, nk)) done++;
  }
  doc_end(d, a);
  free(at);
  return done;
}
int field_set_code(Doc *d, Pos st, const char *code)
{
  Paras s = body(d);
  Pos en;
  int k, nk, r;
  if (!field_end(d, st, &en)) return 0;
  k = fmark_index(d->p[st.para]->t + st.off);
  if (k >= d->nfields || (nk = new_rec(d, code, &d->fields[k])) < 0) return 0;
  d->fields[nk].nosep = 0;
  doc_begin(d, st);
  r = swap_marks(d, &s, st, en, nk);
  doc_end(d, st);
  return r;
}
int field_insert_result(Doc *d, Pos *at, const char *code, const char *result, const FieldEnv *e, const CharFmt *f)
{
  char res[1024], m[8];
  CharFmt cf, rf;
  int k, r = FE_TEXT;
  u16 link = 0;
  Pos p;
  Paras s = body(d);
  *at = pos_clamp(d, *at);
  cf = f ? *f : *para_fmt_before(d->p[at->para], at->off);
  cf.link = 0;
  if ((k = field_new(d, code, 0)) < 0) return -1;
  if (result) snprintf(res, sizeof res, "%s", result);
  else if ((r = new_result(d, s, k, *at, e, -1, res, sizeof res, &link)) == FE_KEEP) res[0] = 0;
  { char *c; for (c = res; *c; c++) if (*c == '\n' || *c == '\r') *c = ' '; }
  rf = cf; rf.link = link;
  p = *at;
  doc_begin(d, p);
  fmark_utf8(m, k, 0);
  if (!doc_insert(d, at, m, 4, &cf)) { doc_end(d, p); return -1; }
  if (res[0]) doc_insert(d, at, res, (u32)strlen(res), &rf);
  fmark_utf8(m, k, 1);
  doc_insert(d, at, m, 4, &cf);
  doc_end(d, *at);
  if (!res[0] && r == FE_KEEP) d->fields[k].dirty = 1;
  return k;
}
int field_insert(Doc *d, Pos *at, const char *code, const FieldEnv *e, const CharFmt *f)
{
  return field_insert_result(d, at, code, NULL, e, f);
}

/* ------------------------------------------------------------------ */
/* how fields are shown */
static int touch_story(Doc *d, Paras *s, void *arg)
{
  u32 i;
  (void)d; (void)arg;
  for (i = 0; i < s->np; i++) if (has_marks(s->p[i])) s->p[i]->ver++;
  return 0;
}
void field_show_codes(Doc *d, int on)
{
  Paras s = body(d);
  int i;
  d->fieldcodes = (u8)(on != 0);
  for (i = 0; i < d->nfields; i++) d->fields[i].showcode = 0;
  touch_story(d, &s, NULL);
  if (d->edit.kind) { Paras b; b.p = d->edit.p; b.np = d->edit.np; touch_story(d, &b, NULL); }
  each_story(d, touch_story, NULL);
  d->ver++;
}
int field_toggle(Doc *d, Pos a, Pos b)
{
  Pos at[256];
  int n = field_list(d, a, b, at, 256), i;
  if (n > 256) n = 256;
  for (i = 0; i < n; i++) {
    Para *q = d->p[at[i].para];
    int k = fmark_index(q->t + at[i].off);
    if (k < d->nfields) { d->fields[k].showcode ^= 1; q->ver++; }
  }
  if (n) d->ver++;
  return n;
}
int field_codes_any(const Doc *d)
{
  int i;
  if (d->fieldcodes) return 2;
  for (i = 0; i < d->nfields; i++) if (d->fields[i].showcode) return 1;
  return 0;
}
int field_code_shown(const Doc *d, int k)
{
  if (k < 0 || k >= d->nfields) return 0;
  return (d->fieldcodes != 0) != (d->fields[k].showcode != 0);
}
void field_code_display(const Doc *d, int k, char *out, size_t cap)
{
  char c[512];
  const char *s = c, *e;
  size_t n;
  if (cap < 8) { if (cap) out[0] = 0; return; }
  field_code_text(k >= 0 && k < d->nfields ? d->fields[k].code : "", c, sizeof c);
  while (*s == ' ') s++;
  e = s + strlen(s);
  while (e > s && e[-1] == ' ') e--;
  n = (size_t)(e - s);
  if (n + 5 > cap) {
    /* cut short on a character boundary */
    size_t m = cap - 8;
    while (m > 0 && ((u8)s[m] & 0xC0) == 0x80) m--;
    snprintf(out, cap, "{ ");
    copy_n(out + 2, cap - 2, s, m);
    strcat(out, "... }");
  } else { snprintf(out, cap, "{ "); copy_n(out + 2, cap - 2, s, n); strcat(out, " }"); }
}
u32 field_end_in_para(const Para *p, u32 off)
{
  int k = fmark_index(p->t + off), depth = 0;
  u32 o;
  for (o = next_mark(p, off); o < p->len; o = next_mark(p, o + 4)) {
    if (fmark_index(p->t + o) != k) continue;
    if (IS_FSTART(p->t + o)) depth++;
    else if (--depth == 0) return o;
  }
  return p->len;
}
int field_placeholder(const Doc *d, const Para *p, u32 off, const char **code)
{
  u32 o = off;
  int depth = 0;
  if (code) *code = NULL;
  if (!d->nfields) return FK_NONE;
  while (o >= 4) {
    o = (u32)utf8_prev(p->t, o);
    if (o + 4 > p->len || !IS_FMARK(p->t + o)) continue;
    if (IS_FEND(p->t + o)) depth++;
    else if (depth) depth--;
    else {
      int k = fmark_index(p->t + o), kind;
      if (k >= d->nfields) return FK_NONE;
      kind = field_kind(d->fields[k].code);
      if (kind > FK_PAGEREF) return FK_NONE;
      if (code) *code = d->fields[k].code;
      return kind;
    }
  }
  return FK_NONE;
}
int field_depth_before(Doc *d, u32 para)
{
  static const Doc *cd;
  static u32 cver, cpara;
  static int cdepth;
  u32 pi, lim;
  int depth = 0;
  u16 *closed;
  if (!d->nfields || para == 0 || para > d->np) return 0;
  if (cd == d && cver == d->ver && cpara == para) return cdepth;
  if (!(closed = (u16 *)calloc((size_t)d->nfields, sizeof(u16)))) return 0;
  lim = para > 300 ? para - 300 : 0;
  for (pi = para; pi-- > lim;) {
    Para *q = d->p[pi];
    u32 m[256];
    int nm = marks_before(q, q->len, m, 256);
    while (nm-- > 0) {
      int k = fmark_index(q->t + m[nm]);
      if (k >= d->nfields) continue;
      if (IS_FEND(q->t + m[nm])) closed[k]++;
      else if (closed[k]) closed[k]--;
      else depth++;
    }
  }
  free(closed);
  cd = d; cver = d->ver; cpara = para; cdepth = depth;
  return depth;
}

/* ------------------------------------------------------------------ */
/* saving: marks without a proper partner */
typedef struct { const char **o; u32 n, cap; struct { int k; const char *t; } st[256]; int nst; } Orph;
static void orph_add(Orph *r, const char *t)
{
  if (r->n >= r->cap) {
    u32 nc = r->cap ? r->cap * 2 : 16;
    const char **no = (const char **)realloc((void *)r->o, nc * sizeof(char *));
    if (!no) return;
    r->o = no; r->cap = nc;
  }
  r->o[r->n++] = t;
}
static void orph_story(Orph *r, Para **pp, u32 np)
{
  u32 pi;
  r->nst = 0;
  for (pi = 0; pi < np; pi++) {
    Para *q = pp[pi];
    u32 o;
    for (o = next_mark(q, 0); o < q->len; o = next_mark(q, o + 4)) {
      const char *t = q->t + o;
      int k = fmark_index(t), j;
      if (IS_FSTART(t)) {
        if (r->nst < 256) { r->st[r->nst].k = k; r->st[r->nst].t = t; r->nst++; }
        else orph_add(r, t);
        continue;
      }
      for (j = r->nst; j-- > 0;) if (r->st[j].k == k) break;
      if (j < 0) { orph_add(r, t); continue; }
      while (r->nst > j + 1) orph_add(r, r->st[--r->nst].t);
      r->nst = j;
    }
  }
  while (r->nst > 0) orph_add(r, r->st[--r->nst].t);
}
static int orph_each(Doc *d, Paras *s, void *arg) { (void)d; orph_story((Orph *)arg, s->p, s->np); return 0; }
static int ptr_cmp(const void *a, const void *b)
{
  uintptr_t x = (uintptr_t)*(const char *const *)a, y = (uintptr_t)*(const char *const *)b;
  return x < y ? -1 : x > y;
}
const char **field_orphans(Doc *d, u32 *n)
{
  Orph r;
  *n = 0;
  if (!d->nfields) return NULL;
  memset(&r, 0, sizeof r);
  orph_story(&r, d->p, d->np);
  if (d->edit.kind) orph_story(&r, d->edit.p, d->edit.np);
  each_story(d, orph_each, &r);
  if (!r.n) { free((void *)r.o); return NULL; }
  qsort((void *)r.o, r.n, sizeof(char *), ptr_cmp);
  *n = r.n;
  return r.o;
}
int field_is_orphan(const char *const *o, u32 n, const char *t)
{
  u32 lo = 0, hi = n;
  while (lo < hi) {
    u32 m = (lo + hi) / 2;
    if (o[m] == t) return 1;
    if ((uintptr_t)o[m] < (uintptr_t)t) lo = m + 1; else hi = m;
  }
  return 0;
}
