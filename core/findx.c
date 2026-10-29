/* findx.c - WP12: Word's Advanced Find and Replace, Go To, Select Similar Formatting, column
   selection and Toggle Unicode Character (see findx.h). Written from Word's documented behaviour;
   WordCraft's find_pattern.rs was read for the list of codes it supports, none of its code is used.

   The matcher is a small backtracking one over the document read as a stream of characters with a
   paragraph mark between paragraphs. Every pattern item takes one character or none (groups and
   word edges), and only single items repeat, so the backtracking is kept on an explicit stack (no
   recursion: 68k stacks are small) and a step budget stops a pathological pattern */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "findx.h"
#include "table.h"

/* pseudo characters of the stream */
#define CP_MARK 0x110000UL              /* a paragraph mark */
#define CP_END  0x110001UL              /* past the last paragraph */

enum { N_CHAR, N_ANY, N_DIGIT, N_LETTER, N_WS, N_CLASS, N_MARK, N_KIND, N_FILL, N_WSTART, N_WEND, N_GOPEN, N_GCLOSE };
enum { MK_ANY, MK_PAGE, MK_COLUMN, MK_SECTION };
#define REP_INF 0xFFFF

/* ------------------------------------------------------------------ */
/* the stream */
static u32 sc(Doc *d, Pos p)
{
  Para *q;
  int u;
  if (p.para >= d->np) return CP_END;
  q = d->p[p.para];
  if (p.off < q->len) return utf8_get(q->t + p.off, q->len - p.off, &u);
  return p.para + 1 < d->np ? CP_MARK : CP_END;
}
static Pos snext(Doc *d, Pos p)
{
  Para *q = d->p[p.para];
  if (p.off < q->len) p.off = (u32)utf8_next(q->t, q->len, p.off);
  else { p.para++; p.off = 0; }
  return p;
}
static Pos sprev(Doc *d, Pos p)
{
  if (p.off) p.off = (u32)utf8_prev(d->p[p.para]->t, p.off);
  else if (p.para) { p.para--; p.off = d->p[p.para]->len; }
  return p;
}
static int at_start(Pos p) { return !p.para && !p.off; }
/* as doc.c's words: letters, digits, _ and ' (not spaces, punctuation, fields or objects) */
static int wordc(u32 c)
{
  if (c > 127) return c < 0xF0000 && c != 0xA0 && !(c >= 0x2000 && c <= 0x206F) && !(c >= 0xE000 && c <= 0xE0FF) && c != 0xD7 && c != 0xF7 && !(c >= 0xA1 && c <= 0xBF);
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '\'';
}
static int letter(u32 c)
{
  if (c < 128) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  return wordc(c) && c != 0xB5 && !(c >= 0x660 && c <= 0x669);
}
static int spacec(u32 c) { return c == ' ' || c == '\t' || c == 0xA0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F; }
static int punctc(u32 c)
{
  if (c < 128) return c > ' ' && c < 127 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
  return (c >= 0x2010 && c <= 0x2027) || c == 0xA1 || c == 0xBF || c == 0xAB || c == 0xBB;
}
/* folded for finding regardless of case (as doc.c: ASCII, Latin-1, Latin Extended-A in pairs) */
static u32 fold(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if (c >= 0x100 && c < 0x180 && !(c & 1)) return c + 1;
  return c;
}
static int ieq(const char *a, const char *b)
{
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return 0;
  }
  return *a == *b;
}

/* ------------------------------------------------------------------ */
/* formatting */
void fx_format_none(FxFormat *f)
{
  memset(f, 0, sizeof *f);
  f->color = FXC_ANY; f->hl = -1; f->align = -1;
}
int fx_format_empty(const FxFormat *f)
{
  return !f->set && !f->clear && !f->fxset && !f->fxclear && !f->font[0] && !f->size && f->color == FXC_ANY && f->hl < 0 &&
         !f->pstyle[0] && !f->cstyle[0] && f->align < 0;
}
static int para_ok(Doc *d, const FxFormat *f, const Para *q)
{
  if (f->pstyle[0] && (q->pf.style >= d->nstyles || !ieq(d->styles[q->pf.style].name, f->pstyle))) return 0;
  if (f->align >= 0 && q->pf.align != (u8)f->align) return 0;
  return 1;
}
static int char_ok(Doc *d, const FxFormat *f, const CharFmt *c)
{
  if ((c->flags & f->set) != f->set || (c->flags & f->clear)) return 0;
  if ((c->fx & f->fxset) != f->fxset || (c->fx & f->fxclear)) return 0;
  if (f->font[0] && (c->font >= d->nfonts || !ieq(d->fonts[c->font], f->font))) return 0;
  if (f->size && c->size != f->size) return 0;
  if (f->color != FXC_ANY && c->color != f->color) return 0;
  if (f->hl >= 0 && (c->hl != 0) != (f->hl != 0)) return 0;
  if (f->cstyle[0] && (!c->cstyle || c->cstyle >= d->nstyles || !ieq(d->styles[c->cstyle].name, f->cstyle))) return 0;
  return 1;
}
/* the character (or mark) at p has the pattern's formatting */
static int fmt_at(Doc *d, const FxFormat *f, Pos p)
{
  Para *q = d->p[p.para];
  if (!para_ok(d, f, q)) return 0;
  if (p.off >= q->len) return 1;              /* a paragraph mark: its paragraph's formatting only */
  return char_ok(d, f, para_fmt_at(q, p.off));
}

/* ------------------------------------------------------------------ */
/* compiling */
static int add_node(FxPattern *p, int type, u32 c)
{
  FxNode *n;
  if (p->nn >= FX_MAXNODE) { strcpy(p->err, "The Find what text is too long."); return -1; }
  n = &p->n[p->nn];
  memset(n, 0, sizeof *n);
  n->type = (u8)type; n->c = c; n->min = n->max = 1;
  if (type == N_WSTART || type == N_WEND || type == N_GOPEN || type == N_GCLOSE) n->min = n->max = 0;
  return p->nn++;
}
/* Windows-1252's 128-159 */
static u32 ansi(u32 n)
{
  static const unsigned short high[32] = {
    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
  return n >= 128 && n < 160 ? high[n - 128] : n;
}
/* a numeric code after ^ at s (13, 0151, u8212): its character (CP_MARK for 13, 12 a page break
   '\f', 14 a column break 0x0E, 11 '\v'), *used how many bytes it took; 0 if none */
static u32 numeric_code(const char *s, int *used)
{
  int i = 0, st = 0, k = 0, max = 4;
  u32 n = 0;
  if (s[0] == 'u') { st = i = 1; max = 5; }
  while (k < max && s[i] >= '0' && s[i] <= '9') { n = n * 10 + (u32)(s[i] - '0'); i++; k++; }
  if (!k) return 0;
  if (!st && s[0] == '0') n = ansi(n);
  *used = i;
  switch (n) {
  case 13: return CP_MARK;
  case 11: return '\v';
  case 12: return '\f';
  case 14: return 0x0E;
  case 30: return 0x2011;
  case 31: return 0xAD;
  case 9: return '\t';
  }
  if (n < 32 || n == 127 || n > 0x10FFFF || (n >= 0xD800 && n < 0xE000)) return 0;
  return n;
}
/* a ^ code at s (after the caret) as a node; how many bytes it took (0: not a code) */
static int caret_code(FxPattern *p, const char *s)
{
  int k = -1, used = 0;
  u32 c;
  switch (*s) {
  case 'p': k = add_node(p, N_MARK, MK_ANY); break;
  case 'm': k = add_node(p, N_MARK, MK_PAGE); break;
  case 'n': k = add_node(p, N_MARK, MK_COLUMN); break;
  case 'b': k = add_node(p, N_MARK, MK_SECTION); break;
  case 't': k = add_node(p, N_CHAR, '\t'); break;
  case 'l': k = add_node(p, N_CHAR, '\v'); break;
  case '?': k = add_node(p, N_ANY, 0); break;
  case '#': k = add_node(p, N_DIGIT, 0); break;
  case '$': k = add_node(p, N_LETTER, 0); break;
  case 'w': if ((k = add_node(p, N_WS, 0)) >= 0) p->n[k].max = REP_INF; break;
  case '^': k = add_node(p, N_CHAR, '^'); break;
  case '+': k = add_node(p, N_CHAR, 0x2014); break;
  case '=': k = add_node(p, N_CHAR, 0x2013); break;
  case 's': k = add_node(p, N_CHAR, 0xA0); break;
  case '~': k = add_node(p, N_CHAR, 0x2011); break;
  case '-': k = add_node(p, N_CHAR, 0xAD); break;
  case '%': k = add_node(p, N_CHAR, 0xA7); break;
  case 'v': k = add_node(p, N_CHAR, 0xB6); break;
  case 'g': case 'f': case 'e': case 'a': case 'd': k = add_node(p, N_KIND, (u32)*s); break;
  default:
    if (!(c = numeric_code(s, &used))) return 0;
    if (c == CP_MARK) k = add_node(p, N_MARK, MK_ANY);
    else if (c == '\f') k = add_node(p, N_MARK, MK_PAGE);
    else if (c == 0x0E) k = add_node(p, N_MARK, MK_COLUMN);
    else k = add_node(p, N_CHAR, c);
    return k < 0 ? -1 : used;
  }
  return k < 0 ? -1 : 1;
}
static int add_range(FxPattern *p, u32 lo, u32 hi)
{
  if (p->nsets >= FX_MAXSET) { strcpy(p->err, "Too many characters in [ ]."); return 0; }
  p->set[p->nsets][0] = lo; p->set[p->nsets][1] = hi; p->nsets++;
  return 1;
}
/* [abc], [a-z], [!x] at s (after the [): the class node; bytes up to and past the ] (0 on error) */
static int wild_class(FxPattern *p, const char *s)
{
  int i = 0, k, neg = 0, used;
  u32 c, hi;
  if (s[0] == '!') { neg = 1; i = 1; }
  if ((k = add_node(p, N_CLASS, 0)) < 0) return 0;
  p->n[k].neg = (u8)neg; p->n[k].set0 = (u16)p->nsets;
  if (s[i] == ']') { strcpy(p->err, "[ ] needs at least one character."); return 0; }
  while (s[i] && s[i] != ']') {
    if (s[i] == '\\' && s[i + 1]) i++;
    c = utf8_get(s + i, strlen(s + i), &used); i += used;
    hi = c;
    if (s[i] == '-' && s[i + 1] && s[i + 1] != ']') {
      i++;
      if (s[i] == '\\' && s[i + 1]) i++;
      hi = utf8_get(s + i, strlen(s + i), &used); i += used;
      if (hi < c) { strcpy(p->err, "A range in [ ] must be in ascending order."); return 0; }
    }
    if (!add_range(p, c, hi)) return 0;
  }
  if (s[i] != ']') { strcpy(p->err, "A [ needs a closing ]."); return 0; }
  p->n[k].nset = (u16)(p->nsets - p->n[k].set0);
  return i + 1;
}
/* {n}, {n,}, {n,m} after the item before (s after the {); bytes past the } (0 on error) */
static int wild_count(FxPattern *p, const char *s)
{
  int i = 0;
  long a = 0, b = -1, any = 0;
  FxNode *n;
  if (!p->nn || p->n[p->nn - 1].max != 1 || p->n[p->nn - 1].min != 1) { strcpy(p->err, "A count { } must follow a character or [ ]."); return 0; }
  while (s[i] == ' ') i++;
  while (s[i] >= '0' && s[i] <= '9') { a = a * 10 + (s[i++] - '0'); any = 1; if (a > 1000) break; }
  if (s[i] == ',' || s[i] == ';') {
    i++;
    if (s[i] >= '0' && s[i] <= '9') { b = 0; while (s[i] >= '0' && s[i] <= '9') { b = b * 10 + (s[i++] - '0'); if (b > 1000) break; } }
    else b = REP_INF;
  } else b = a;
  if (s[i] != '}' || (!any && b == REP_INF) || a > 1000 || (b != REP_INF && (b > 1000 || b < a)) || b == 0) {
    strcpy(p->err, "This count { } is not valid."); return 0;
  }
  n = &p->n[p->nn - 1];
  n->min = (u16)a; n->max = (u16)b;
  return i + 1;
}
int fx_compile(FxPattern *p, const char *what, unsigned flags, const FxFormat *fmt)
{
  const char *s = what;
  int used, last_atom = 0, depth = 0, gstack[10];
  memset(p, 0, sizeof *p);
  p->flags = flags;
  if (fmt) { p->fmt = *fmt; p->hasfmt = !fx_format_empty(fmt); }
  else fx_format_none(&p->fmt);
  if (!*what) {
    if (!p->hasfmt) { strcpy(p->err, "There is nothing to find."); return 0; }
    p->empty = 1;
    return 1;
  }
  while (*s) {
    u32 c;
    if (flags & FX_WILD) {
      switch (*s) {
      case '?': if (add_node(p, N_ANY, 0) < 0) return 0; s++; last_atom = 1; continue;
      case '*': { int k = add_node(p, N_ANY, 0); if (k < 0) return 0; p->n[k].min = 0; p->n[k].max = REP_INF; p->n[k].lazy = 1; s++; last_atom = 0; continue; }
      case '@':
        if (!last_atom) { strcpy(p->err, "@ must follow a character or [ ]."); return 0; }
        p->n[p->nn - 1].min = 1; p->n[p->nn - 1].max = REP_INF; s++; last_atom = 0; continue;
      case '<': if (add_node(p, N_WSTART, 0) < 0) return 0; s++; last_atom = 0; continue;
      case '>': if (add_node(p, N_WEND, 0) < 0) return 0; s++; last_atom = 0; continue;
      case '(': {
        int k;
        if (p->ngroups >= 9 || depth >= 9) { strcpy(p->err, "Too many groups ( )."); return 0; }
        if ((k = add_node(p, N_GOPEN, 0)) < 0) return 0;
        p->n[k].grp = (u8)++p->ngroups; gstack[depth++] = p->ngroups; s++; last_atom = 0; continue; }
      case ')': {
        int k;
        if (!depth) { strcpy(p->err, "A ) has no ( before it."); return 0; }
        if ((k = add_node(p, N_GCLOSE, 0)) < 0) return 0;
        p->n[k].grp = (u8)gstack[--depth]; s++; last_atom = 0; continue; }
      case '{': if (!(used = wild_count(p, s + 1))) return 0; s += 1 + used; last_atom = 0; continue;
      case '[': if (!(used = wild_class(p, s + 1))) return 0; s += 1 + used; last_atom = 1; continue;
      case '\\':
        if (s[1] >= '1' && s[1] <= '9') { strcpy(p->err, "\\1 to \\9 belong in Replace with, not in Find what."); return 0; }
        if (s[1]) s++;
        break;
      }
    }
    if (*s == '^' && s[1]) {
      used = caret_code(p, s + 1);
      if (used < 0) return 0;
      if (used > 0) { s += 1 + used; last_atom = p->n[p->nn - 1].max == 1; continue; }
    }
    c = utf8_get(s, strlen(s), &used);
    s += used;
    if (!(flags & FX_WILD) && (((flags & FX_NOPUNCT) && punctc(c)) || ((flags & FX_NOSPACE) && spacec(c)))) continue;
    if (add_node(p, N_CHAR, c) < 0) return 0;
    last_atom = 1;
  }
  if (depth) { strcpy(p->err, "A ( needs a closing )."); return 0; }
  if (!(flags & FX_WILD) && (flags & (FX_NOPUNCT | FX_NOSPACE)) && p->nn > 1) {
    /* skipped punctuation or white space may come between any two items */
    int i, n0 = p->nn;
    u32 kind = ((flags & FX_NOPUNCT) ? 1 : 0) | ((flags & FX_NOSPACE) ? 2 : 0);
    if (2 * n0 - 1 > FX_MAXNODE) { strcpy(p->err, "The Find what text is too long."); return 0; }
    for (i = n0 - 1; i > 0; i--) { p->n[2 * i] = p->n[i]; memset(&p->n[2 * i - 1], 0, sizeof(FxNode)); p->n[2 * i - 1].type = N_FILL; p->n[2 * i - 1].c = kind; p->n[2 * i - 1].max = REP_INF; }
    p->nn = 2 * n0 - 1;
  }
  if (!p->nn) { strcpy(p->err, "There is nothing to find."); return 0; }
  return 1;
}

/* ------------------------------------------------------------------ */
/* matching */
static int class_has(const FxPattern *p, const FxNode *n, u32 c)
{
  int i;
  for (i = n->set0; i < n->set0 + n->nset; i++) if (c >= p->set[i][0] && c <= p->set[i][1]) return 1;
  return 0;
}
/* does item n take the character c at pos? */
static int atom_ok(Doc *d, const FxPattern *p, const FxNode *n, Pos pos, u32 c)
{
  if (c == CP_END) return 0;
  switch (n->type) {
  case N_CHAR:
    if (c == CP_MARK) return 0;
    return (p->flags & (FX_CASE | FX_WILD)) ? c == n->c : fold(c) == fold(n->c);
  case N_ANY: return c != CP_MARK;
  case N_DIGIT: return c >= '0' && c <= '9';
  case N_LETTER: return letter(c);
  case N_WS: return spacec(c);
  case N_CLASS: return c != CP_MARK && (class_has(p, n, c) ? !n->neg : n->neg);
  case N_FILL: return c != CP_MARK && (((n->c & 1) && punctc(c)) || ((n->c & 2) && spacec(c)));
  case N_KIND:
    switch (n->c) {
    case 'g': return c >= 0xF0000 && c < 0xF4000;
    case 'f': return c >= 0xF4000 && c < 0xF8000;
    case 'a': return c >= 0xF8000 && c < 0x100000;
    case 'e': return c >= 0x100000 && c < 0x104000;
    case 'd': return c >= 0xE000 && c <= 0xE0FF;
    }
    return 0;
  case N_MARK: {
    const Para *q, *nx;
    if (c != CP_MARK) return 0;
    q = d->p[pos.para]; nx = d->p[pos.para + 1];
    if (!same_cell(&q->pf, &nx->pf)) return 0;       /* (a cell's end is not a paragraph mark to take) */
    switch (n->c) {
    case MK_PAGE: return nx->pf.pagebreak;
    case MK_COLUMN: return nx->pf.colbreak;
    case MK_SECTION: return q->pf.sect != 0;
    }
    return 1; }
  }
  return 0;
}
typedef struct { short ni, kind; int cnt; Pos start, cur; } Bt;
static Bt bt[FX_MAXNODE + 8];
static Pos gs[10], ge[10];
static int gset[10];
static long budget;
#define BUDGET 3000000L
static int word_before(Doc *d, Pos p) { return !at_start(p) && wordc(sc(d, sprev(d, p))); }
/* the whole match's own conditions: not empty, whole words, prefix, suffix, formatting */
static int final_ok(Doc *d, const FxPattern *p, Pos a, Pos b)
{
  Pos q;
  if (!pos_cmp(a, b)) return 0;
  if (!(p->flags & FX_WILD)) {
    if ((p->flags & (FX_WORD | FX_PREFIX)) && word_before(d, a) && wordc(sc(d, a))) return 0;
    if ((p->flags & (FX_WORD | FX_SUFFIX)) && wordc(sc(d, b)) && word_before(d, b)) return 0;
  }
  if (p->hasfmt)
    for (q = a; pos_cmp(q, b) < 0; q = snext(d, q)) if (!fmt_at(d, &p->fmt, q)) return 0;
  return 1;
}
/* 1 matched (end set), 0 not, -1 out of budget */
static int match(Doc *d, const FxPattern *p, Pos at, Pos *end)
{
  int sp = 0, ni = 0, k;
  Pos pos = at;
  memset(gset, 0, sizeof gset);
  for (;;) {
    const FxNode *nd;
    if (--budget < 0) return -1;
    if (ni == p->nn) {
      if (final_ok(d, p, at, pos)) { *end = pos; return 1; }
      goto back;
    }
    nd = &p->n[ni];
    switch (nd->type) {
    case N_GOPEN: case N_GCLOSE: {
      Bt *f = &bt[sp++];
      f->kind = nd->type == N_GOPEN ? 2 : 3; f->cnt = nd->grp; f->ni = (short)gset[nd->grp];
      f->start = nd->type == N_GOPEN ? gs[nd->grp] : ge[nd->grp];
      if (nd->type == N_GOPEN) { gs[nd->grp] = pos; gset[nd->grp] = 0; } else { ge[nd->grp] = pos; gset[nd->grp] = 1; }
      ni++; continue; }
    case N_WSTART:
      if (!wordc(sc(d, pos)) || word_before(d, pos)) goto back;
      ni++; continue;
    case N_WEND:
      if (wordc(sc(d, pos)) || !word_before(d, pos)) goto back;
      ni++; continue;
    }
    if (nd->min == 1 && nd->max == 1) {
      if (!atom_ok(d, p, nd, pos, sc(d, pos))) goto back;
      pos = snext(d, pos); ni++; continue;
    }
    if (nd->lazy) {
      Pos cur = pos;
      for (k = 0; k < nd->min; k++) { if (!atom_ok(d, p, nd, cur, sc(d, cur))) goto back; cur = snext(d, cur); }
      bt[sp].kind = 1; bt[sp].ni = (short)ni; bt[sp].cnt = nd->min; bt[sp].cur = cur; sp++;
      pos = cur; ni++; continue;
    } else {
      Pos cur = pos;
      k = 0;
      while (k < nd->max && atom_ok(d, p, nd, cur, sc(d, cur))) { cur = snext(d, cur); k++; if (--budget < 0) return -1; }
      if (k < nd->min) goto back;
      bt[sp].kind = 0; bt[sp].ni = (short)ni; bt[sp].cnt = k; bt[sp].start = pos; sp++;
      pos = cur; ni++; continue;
    }
back:
    for (;;) {
      Bt *f;
      if (!sp) return 0;
      f = &bt[--sp];
      if (f->kind == 2) { gs[f->cnt] = f->start; gset[f->cnt] = f->ni; continue; }
      if (f->kind == 3) { ge[f->cnt] = f->start; gset[f->cnt] = f->ni; continue; }
      nd = &p->n[f->ni];
      if (f->kind == 0) {
        if (f->cnt > nd->min) {
          f->cnt--;
          for (pos = f->start, k = 0; k < f->cnt; k++) pos = snext(d, pos);
          ni = f->ni + 1; sp++;
          break;
        }
        continue;
      }
      if (f->cnt < nd->max && atom_ok(d, p, nd, f->cur, sc(d, f->cur))) {
        f->cnt++; f->cur = snext(d, f->cur); pos = f->cur; ni = f->ni + 1; sp++;
        break;
      }
    }
    if (--budget < 0) return -1;
  }
}
int fx_match_at(Doc *d, const FxPattern *p, Pos a, Pos *end, Pos *groups)
{
  int r, g;
  Pos e;
  if (p->empty || a.para >= d->np) return 0;
  budget = BUDGET;
  r = match(d, p, a, &e);
  if (r != 1) return 0;
  if (end) *end = e;
  if (groups) {
    groups[0] = a; groups[1] = e;
    for (g = 1; g < 10; g++) {
      if (g <= p->ngroups && gset[g]) { groups[2 * g] = gs[g]; groups[2 * g + 1] = ge[g]; }
      else groups[2 * g] = groups[2 * g + 1] = a;
    }
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* finding */
/* a quick look at the first item (most positions fail there) */
static int may_start(Doc *d, const FxPattern *p, Pos pos)
{
  const FxNode *n = &p->n[0];
  if (n->type >= N_WSTART || n->min == 0) return 1;
  return atom_ok(d, p, n, pos, sc(d, pos));
}
/* formatting alone: the stretch of formatted characters (in one paragraph) from pos on, before hi */
static int fmt_stretch(Doc *d, const FxPattern *p, Pos pos, Pos hi, Pos *a, Pos *b)
{
  while (pos_cmp(pos, hi) < 0 && sc(d, pos) != CP_END) {
    if (pos.off < d->p[pos.para]->len && fmt_at(d, &p->fmt, pos)) {
      Pos e = pos;
      while (pos_cmp(e, hi) < 0 && e.off < d->p[e.para]->len && fmt_at(d, &p->fmt, e)) e = snext(d, e);
      *a = pos; *b = e;
      return 1;
    }
    pos = snext(d, pos);
  }
  return 0;
}
/* the first match starting in [lo, hi) and ending by hi */
static int find_fwd(Doc *d, const FxPattern *p, Pos lo, Pos hi, Pos *a, Pos *b)
{
  Pos pos = lo, e;
  if (p->empty) return fmt_stretch(d, p, lo, hi, a, b);
  while (pos_cmp(pos, hi) < 0) {
    u32 c = sc(d, pos);
    if (c == CP_END) break;
    if (may_start(d, p, pos)) {
      int r = match(d, p, pos, &e);
      if (r < 0) return 0;
      if (r && pos_cmp(e, hi) <= 0) { *a = pos; *b = e; return 1; }
    }
    pos = snext(d, pos);
  }
  return 0;
}
/* the last match starting in [lo, hi) and ending by hi */
static int find_back(Doc *d, const FxPattern *p, Pos lo, Pos hi, Pos *a, Pos *b)
{
  Pos pos = hi, e;
  while (pos_cmp(pos, lo) > 0) {
    pos = sprev(d, pos);
    if (p->empty) {
      if (pos.off < d->p[pos.para]->len && fmt_at(d, &p->fmt, pos)) {
        Pos s = pos, t = snext(d, pos);
        while (pos_cmp(s, lo) > 0 && s.off > 0) { Pos q = sprev(d, s); if (!fmt_at(d, &p->fmt, q)) break; s = q; }
        *a = s; *b = t;
        return 1;
      }
      continue;
    }
    if (may_start(d, p, pos)) {
      int r = match(d, p, pos, &e);
      if (r < 0) return 0;
      if (r && pos_cmp(e, hi) <= 0) { *a = pos; *b = e; return 1; }
    }
  }
  return 0;
}
int fx_find(Doc *d, const FxPattern *p, Pos from, const Pos *range, Pos *a, Pos *b)
{
  Pos lo = range ? range[0] : doc_start(d), hi = range ? range[1] : doc_end_pos(d);
  if (!d->np) return 0;
  from = pos_clamp(d, from);
  if (pos_cmp(from, lo) < 0) from = lo;
  if (pos_cmp(from, hi) > 0) from = hi;
  budget = BUDGET;
  if (p->flags & FX_BACK) {
    if (find_back(d, p, lo, from, a, b)) return 1;
    return !(p->flags & FX_NOWRAP) && find_back(d, p, from, hi, a, b);
  }
  if (find_fwd(d, p, from, hi, a, b)) return 1;
  return !(p->flags & FX_NOWRAP) && find_fwd(d, p, lo, from, a, b);
}
u32 fx_find_all(Doc *d, const FxPattern *p, const Pos *range, Pos *out, u32 max)
{
  Pos lo = range ? range[0] : doc_start(d), hi = range ? range[1] : doc_end_pos(d), a, b;
  u32 n = 0;
  budget = BUDGET;
  while (n < max && find_fwd(d, p, lo, hi, &a, &b)) {
    out[2 * n] = a; out[2 * n + 1] = b; n++;
    lo = pos_cmp(b, a) > 0 ? b : snext(d, a);
  }
  return n;
}

/* ------------------------------------------------------------------ */
/* replacing */
/* Replace with read (groups: the match's, 10 pairs): UTF-8 with '\f' for a page break and 0x0E for
   a column break */
static void build_with(Doc *d, const FxPattern *p, const char *with, const char *clip, const Pos *groups, Buf *o)
{
  const char *s = with;
  char u[8];
  while (*s) {
    if (*s == '^' && s[1]) {
      int used = 0;
      u32 c = 0;
      s++;
      switch (*s) {
      case '&': case 'c': {
        char *t;
        u32 n;
        if (*s == 'c') { if (clip) buf_adds(o, clip); s++; continue; }
        if ((t = doc_text(d, groups[0], groups[1], &n))) { buf_add(o, t, n); free(t); }
        s++; continue; }
      case 'p': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'l': c = '\v'; break;
      case 'm': c = '\f'; break;
      case 'n': c = 0x0E; break;
      case '^': c = '^'; break;
      case '+': c = 0x2014; break;
      case '=': c = 0x2013; break;
      case 's': c = 0xA0; break;
      case '~': c = 0x2011; break;
      case '-': c = 0xAD; break;
      case '%': c = 0xA7; break;
      case 'v': c = 0xB6; break;
      default:
        if ((c = numeric_code(s, &used))) { if (c == CP_MARK) c = '\n'; s += used; buf_add(o, u, (size_t)utf8_put(u, c)); continue; }
        buf_addc(o, '^');
        continue;
      }
      s++;
      buf_add(o, u, (size_t)utf8_put(u, c));
      continue;
    }
    if ((p->flags & FX_WILD) && *s == '\\' && s[1]) {
      if (s[1] >= '1' && s[1] <= '9') {
        int g = s[1] - '0';
        char *t;
        u32 n;
        if (g <= p->ngroups && pos_cmp(groups[2 * g], groups[2 * g + 1]) && (t = doc_text(d, groups[2 * g], groups[2 * g + 1], &n))) { buf_add(o, t, n); free(t); }
        s += 2; continue;
      }
      s++;
    }
    buf_addc(o, *s++);
  }
}
void fx_apply_format(Doc *d, Pos a, Pos b, const FxFormat *rf)
{
  CharFmt v;
  unsigned mask;
  int k;
  if (!rf || !pos_cmp(a, b)) return;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (rf->pstyle[0] && (k = doc_style_by_name(d, rf->pstyle)) >= 0 && d->styles[k].type == ST_PARA)
    doc_apply_style(d, a.para, b.off == 0 && b.para > a.para ? b.para - 1 : b.para, k);
  if (rf->align >= 0) {
    ParaFmt pf;
    memset(&pf, 0, sizeof pf); pf.align = (u8)rf->align;
    doc_para_format(d, a.para, b.off == 0 && b.para > a.para ? b.para - 1 : b.para, PM_ALIGN, &pf);
  }
  memset(&v, 0, sizeof v);
  mask = (rf->set | rf->clear) & 0x7F;
  v.flags = (u8)rf->set;
  if ((rf->fxset | rf->fxclear) & FX_SMALLCAPS) mask |= CM_SMALLCAPS;
  if ((rf->fxset | rf->fxclear) & FX_DSTRIKE) mask |= CM_DSTRIKE;
  v.fx = rf->fxset;
  if (rf->font[0]) { mask |= CM_FONT; v.font = (u8)doc_font(d, rf->font); }
  if (rf->size) { mask |= CM_SIZE; v.size = rf->size; }
  if (rf->color != FXC_ANY) { mask |= CM_COLOR; v.color = rf->color; }
  if (rf->hl >= 0) { mask |= CM_HL; v.hl = rf->hl ? HL_ON | 0xFFFF00UL : 0; }
  if (mask) doc_format(d, a, b, mask, &v);
  if (rf->cstyle[0] && (k = doc_style_by_name(d, rf->cstyle)) >= 0 && d->styles[k].type == ST_CHAR) doc_char_style(d, a, b, k);
}
int fx_replace(Doc *d, const FxPattern *p, Pos a, Pos b, const char *with, const char *clip, const FxFormat *rf, Pos *end)
{
  Pos groups[20], at;
  Buf o;
  CharFmt f;
  Para *q;
  size_t i, s0;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (a.para >= d->np) return 0;
  if (rf && fx_format_empty(rf)) rf = NULL;
  if (!strcmp(with, "^&") || (!*with && rf)) {
    doc_begin(d, a);
    fx_apply_format(d, a, b, rf);
    doc_end(d, b);
    if (end) *end = b;
    return 1;
  }
  if (p->empty || !fx_match_at(d, p, a, NULL, groups)) { groups[0] = a; groups[1] = b; memset(groups + 2, 0, sizeof(Pos) * 18); }
  groups[1] = b;
  buf_init(&o);
  build_with(d, p, with, clip, groups, &o);
  if (o.oom) { buf_free(&o); return 0; }
  q = d->p[a.para];
  f = a.off < q->len ? *para_fmt_at(q, a.off) : *para_fmt_before(q, a.off);
  doc_begin(d, a);
  doc_delete(d, a, b);
  at = a;
  for (i = s0 = 0; i <= o.len; i++) {
    if (i < o.len && o.data[i] != '\f' && o.data[i] != 0x0E) continue;
    if (i > s0) doc_insert(d, &at, o.data + s0, (u32)(i - s0), &f);
    if (i < o.len) {
      ParaFmt pf;
      doc_insert(d, &at, "\n", 1, &f);
      memset(&pf, 0, sizeof pf);
      if (o.data[i] == '\f') pf.pagebreak = 1; else pf.colbreak = 1;
      doc_para_format(d, at.para, at.para, PM_BREAK, &pf);
    }
    s0 = i + 1;
  }
  fx_apply_format(d, a, at, rf);
  doc_end(d, at);
  buf_free(&o);
  if (end) *end = at;
  return 1;
}
u32 fx_replace_all(Doc *d, const FxPattern *p, const Pos *range, const char *with, const char *clip, const FxFormat *rf)
{
  Pos lo = range ? range[0] : doc_start(d), hi = range ? range[1] : doc_end_pos(d), a, b, e;
  u32 hpe, hoe, count = 0;
  hi = pos_clamp(d, hi);
  hpe = d->np - hi.para; hoe = d->p[hi.para]->len - hi.off;     /* the range's end, counted from the end (edits come before it) */
  doc_begin(d, lo);
  budget = BUDGET;
  while (count < 100000 && find_fwd(d, p, lo, hi, &a, &b)) {
    if (!fx_replace(d, p, a, b, with, clip, rf, &e)) break;
    count++;
    if (hpe > d->np) break;
    hi.para = d->np - hpe;
    hi.off = d->p[hi.para]->len >= hoe ? d->p[hi.para]->len - hoe : 0;
    lo = e;
    if (!pos_cmp(e, a) && sc(d, e) != CP_END) lo = snext(d, e);       /* (an empty replacement of formatting alone: on past it) */
    if (p->empty && !pos_cmp(lo, b)) lo = b;
    budget = BUDGET;
  }
  doc_end(d, lo);
  if (count) doc_undo_kind(d, UK_REPLACEALL);
  return count;
}

/* ------------------------------------------------------------------ */
/* Go To */
static int heading_para(Doc *d, const Para *q)
{
  const char *n;
  if (q->pf.style >= d->nstyles) return 0;
  n = d->styles[q->pf.style].name;
  return (n[0] == 'h' || n[0] == 'H') && !strncmp(n + 1, "eading ", 7) && n[8] >= '1' && n[8] <= '9';
}
static void put(Pos *a, Pos *b, u32 n, u32 max, Pos pa, Pos pb)
{
  if (n < max) { if (a) a[n] = pa; if (b) b[n] = pb; }
}
u32 fx_targets(Doc *d, Layout *L, int kind, Pos *a, Pos *b, u32 max)
{
  u32 n = 0, i;
  Pos pa, pb;
  if (!a) max = 0;
  switch (kind) {
  case GT_PAGE:
    if (!L) return 0;
    for (i = 0; i < L->npages; i++) {
      u32 j, e;
      lay_page_lines(L, i, &j, &e);
      for (; j < e; j++) {
        const Placed *pc = &L->placed[j];
        if (pc->page != i || pc->para >= L->npl || pc->para >= d->np || !L->pl[pc->para].laid || pc->line >= L->pl[pc->para].nlines) continue;
        pa.para = pc->para; pa.off = L->pl[pc->para].lines[pc->line].start;
        put(a, b, n, max, pa, pa); n++;
        break;
      }
    }
    return n;
  case GT_LINE:
    if (!L) return 0;
    for (i = 0; i < L->nplaced; i++) {
      const Placed *pc = &L->placed[i];
      const ParaLay *pl;
      if (pc->para >= L->npl || pc->para >= d->np) continue;
      pl = &L->pl[pc->para];
      if (!pl->laid || pc->line >= pl->nlines) continue;
      pa.para = pb.para = pc->para; pa.off = pl->lines[pc->line].start;
      pb.off = pc->line + 1 < pl->nlines ? pl->lines[pc->line + 1].start : d->p[pc->para]->len;
      put(a, b, n, max, pa, pb); n++;
    }
    return n;
  case GT_SECTION:
    pa.para = pa.off = 0;
    put(a, b, n, max, pa, pa); n++;
    for (i = 0; i + 1 < d->np; i++) if (d->p[i]->pf.sect) { pa.para = i + 1; pa.off = 0; put(a, b, n, max, pa, pa); n++; }
    return n;
  case GT_BOOKMARK: {
    int k;
    for (k = 1; k <= d->nbmarks; k++) {
      const char *nm = doc_bmark_name(d, k);
      u32 j;
      if (!nm[0] || nm[0] == '_' || !doc_bmark_find(d, nm, &pa, &pb)) continue;
      /* in order of position */
      j = n < max ? n : max;
      while (j > 0 && a && pos_cmp(a[j - 1], pa) > 0) { if (j < max) { a[j] = a[j - 1]; if (b) b[j] = b[j - 1]; } j--; }
      if (j < max) { a[j] = pa; if (b) b[j] = pb; }
      n++;
    }
    return n; }
  case GT_TABLE: {
    u16 cur = 0;
    for (i = 0; i < d->np; i++) {
      u16 t = top_cell(&d->p[i]->pf)->table;
      if (t && t != cur) {
        u32 j = i;
        while (j + 1 < d->np && top_cell(&d->p[j + 1]->pf)->table == t) j++;
        pa.para = i; pa.off = 0; pb.para = j; pb.off = d->p[j]->len;
        put(a, b, n, max, pa, pa); n++;
      }
      cur = t;
    }
    return n; }
  case GT_HEADING:
    for (i = 0; i < d->np; i++)
      if (heading_para(d, d->p[i])) { pa.para = pb.para = i; pa.off = 0; pb.off = d->p[i]->len; put(a, b, n, max, pa, pa); n++; }
    return n;
  }
  /* the rest are characters in the text */
  for (i = 0; i < d->np; i++) {
    Para *q = d->p[i];
    u32 o = 0;
    int prevlink = 0;
    while (o < q->len) {
      int used, hit = 0;
      u32 c = utf8_get(q->t + o, q->len - o, &used);
      pa.para = pb.para = i; pa.off = o; pb.off = o + (u32)used;
      switch (kind) {
      case GT_COMMENT:
        if (c >= 0xF8000 && c < 0xFC000) { hit = 1; if (!doc_comment_mark(d, (int)(c - 0xF8000), 1, &pb)) pb = pa; else pb.off += 4; }
        break;
      case GT_FOOTNOTE: hit = c >= 0xF4000 && c < 0xF8000; pb = pa; break;
      case GT_ENDNOTE: hit = c >= 0x100000 && c < 0x104000; pb = pa; break;
      case GT_GRAPHIC: case GT_OBJECT:
        if (c >= 0xF0000 && c < 0xF4000 && (int)(c - 0xF0000) < d->nimages) {
          int pic = d->images[c - 0xF0000].kind == OBJ_PICTURE;
          hit = kind == GT_GRAPHIC ? pic : !pic;
        }
        break;
      case GT_FIELD: {
        int lk = para_fmt_at(q, o)->link;
        if (c >= 0xE000 && c <= 0xE0FF) hit = 1;
        else if (lk && lk != prevlink) { hit = 1; doc_link_at(d, pa, NULL, &pb); }
        prevlink = lk;
        break; }
      }
      if (hit) { put(a, b, n, max, pa, pb); n++; }
      o += (u32)used;
    }
  }
  return n;
}
static int number(const char *s, long *v)
{
  long n = 0;
  if (!*s) return 0;
  for (; *s; s++) { if (*s < '0' || *s > '9') return 0; n = n * 10 + (*s - '0'); if (n > 100000000L) return 0; }
  *v = n;
  return 1;
}
int fx_goto(Doc *d, Layout *L, int kind, const char *spec, int dir, Pos cur, Pos *a, Pos *b)
{
  Pos *ta, *tb;
  u32 n, i;
  long ci = -1, want = -1, v;
  char sp[128];
  size_t k = 0;
  int container = kind == GT_PAGE || kind == GT_SECTION || kind == GT_LINE || kind == GT_TABLE;
  while (*spec == ' ') spec++;
  while (spec[k] && k < sizeof sp - 1) { sp[k] = spec[k]; k++; }
  while (k && sp[k - 1] == ' ') k--;
  sp[k] = 0;
  if (dir) sp[0] = 0;
  if (kind == GT_BOOKMARK && sp[0] && !number(sp, &v)) return doc_bmark_find(d, sp, a, b);
  n = fx_targets(d, L, kind, NULL, NULL, 0);
  if (!n) return 0;
  ta = (Pos *)malloc(n * sizeof(Pos)); tb = (Pos *)malloc(n * sizeof(Pos));
  if (!ta || !tb) { free(ta); free(tb); return 0; }
  fx_targets(d, L, kind, ta, tb, n);
  for (i = 0; i < n; i++) if (pos_cmp(ta[i], cur) <= 0) ci = (long)i;
  if (kind == GT_COMMENT && sp[0] && sp[0] != '+' && sp[0] != '-' && !number(sp, &v)) {
    /* the next comment by that reviewer (wrapping round) */
    for (i = 1; i <= n && want < 0; i++) {
      long j = (ci + (long)i) % (long)n;
      const Para *q = d->p[ta[j].para];
      int used;
      int k2 = (int)(utf8_get(q->t + ta[j].off, q->len - ta[j].off, &used) - 0xF8000UL);
      if (k2 < d->ncomments && ieq(d->comments[k2].author, sp)) want = j;
    }
  } else if (sp[0] == '+' || sp[0] == '-') {
    if (!number(sp + 1, &v)) v = 1;
    want = sp[0] == '+' ? ci + v : ci - v;
    if (sp[0] == '+' && ci < 0) want = v - 1;
  } else if (sp[0]) {
    if (number(sp, &v) && v >= 1) want = v - 1;
  } else if (dir < 0) {
    if (container) want = ci - 1;
    else for (i = 0; i < n; i++) if (pos_cmp(ta[i], cur) < 0) want = (long)i;
  } else {
    for (i = 0; i < n; i++) if (pos_cmp(ta[i], cur) > 0) { want = (long)i; break; }
  }
  if (want < 0 || want >= (long)n) { free(ta); free(tb); return 0; }
  *a = ta[want];
  *b = (kind == GT_BOOKMARK || kind == GT_COMMENT || kind == GT_GRAPHIC || kind == GT_OBJECT || kind == GT_FIELD) ? tb[want] : ta[want];
  free(ta); free(tb);
  return 1;
}

/* ------------------------------------------------------------------ */
/* Select Similar Formatting */
static int same_look(const CharFmt *a, const CharFmt *b)
{
  return a->flags == b->flags && a->font == b->font && a->size == b->size && a->color == b->color && a->hl == b->hl &&
         a->cstyle == b->cstyle && a->fx == b->fx && (!(a->flags & CF_UNDERLINE) || a->ul == b->ul) && a->spacing == b->spacing &&
         a->scale == b->scale && a->position == b->position;
}
u32 fx_similar(Doc *d, Pos at, Pos *out, u32 max)
{
  CharFmt t;
  Para *q;
  u32 i, n = 0;
  int open = 0;
  if (!d->np) return 0;
  at = pos_clamp(d, at);
  q = d->p[at.para];
  t = at.off < q->len ? *para_fmt_at(q, at.off) : *para_fmt_before(q, at.off);
  for (i = 0; i < d->np; i++) {
    u32 r, s = 0;
    q = d->p[i];
    open = 0;
    for (r = 0; r < q->nr; r++) {
      if (q->r[r].end <= s) continue;
      if (same_look(&q->r[r].f, &t)) {
        if (!open && n < max) { out[2 * n].para = i; out[2 * n].off = s; }
        if (n < max) { out[2 * n + 1].para = i; out[2 * n + 1].off = q->r[r].end; }
        if (!open) open = 1;
      } else if (open) { n++; open = 0; }
      s = q->r[r].end;
    }
    if (open) n++;
  }
  return n < max ? n : max;
}

/* ------------------------------------------------------------------ */
/* column selection */
/* the placed line a position is on, or -1 */
static long placed_of(Layout *L, Pos p)
{
  const ParaLay *pl;
  u32 li = 0;
  if (p.para >= L->npl) return -1;
  pl = &L->pl[p.para];
  if (!pl->laid || !pl->nlines) return -1;
  while (li + 1 < pl->nlines && pl->lines[li + 1].start <= p.off) li++;
  if (pl->placed0 + li >= L->nplaced || L->placed[pl->placed0 + li].para != p.para) return -1;
  return (long)(pl->placed0 + li);
}
u32 fx_block(Layout *L, Pos anchor, Pos caret, Pos *out, u32 max)
{
  u32 pg, n = 0;
  i32 xa, xc, y, h, x0, x1;
  long ia, ic, i;
  if (!L || !L->nplaced) return 0;
  ia = placed_of(L, anchor); ic = placed_of(L, caret);
  if (ia < 0 || ic < 0) return 0;
  lay_caret(L, anchor, &pg, &xa, &y, &h);
  lay_caret(L, caret, &pg, &xc, &y, &h);
  x0 = xa < xc ? xa : xc; x1 = xa < xc ? xc : xa;
  if (ia > ic) { long t = ia; ia = ic; ic = t; }
  for (i = ia; i <= ic && n < max; i++) {
    const Placed *pc = &L->placed[i];
    const ParaLay *pl = &L->pl[pc->para];
    i32 my;
    Pos s, e;
    if (pc->line >= pl->nlines) continue;
    my = pc->y + pl->lines[pc->line].height / 2;
    s = lay_hit(L, pc->page, x0, my);
    e = lay_hit(L, pc->page, x1, my);
    if (s.para != pc->para || e.para != pc->para || pos_cmp(s, e) >= 0) continue;
    out[2 * n] = s; out[2 * n + 1] = e; n++;
  }
  return n;
}
char *fx_block_text(Doc *d, const Pos *r, u32 n, u32 *len)
{
  Buf o;
  u32 i;
  buf_init(&o);
  for (i = 0; i < n; i++) {
    u32 k;
    char *t = doc_text(d, r[2 * i], r[2 * i + 1], &k);
    if (i) buf_addc(&o, '\n');
    if (t) { buf_add(&o, t, k); free(t); }
  }
  buf_addc(&o, 0);
  if (o.oom) { buf_free(&o); return NULL; }
  if (len) *len = (u32)o.len - 1;
  return o.data;
}

/* ------------------------------------------------------------------ */
/* Toggle Unicode Character */
static int hexd(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* a character Alt+X may make or show: not a control character, break, field or object */
static int codeable(u32 c) { return c >= 0x20 && c != 0x7F && !(c >= 0x80 && c < 0xA0) && c <= 0x10FFFF && !(c >= 0xD800 && c < 0xE000) && !(c >= 0xE000 && c <= 0xE0FF) && c < 0xF0000; }
/* the code in s[0..n) (hex digits only, at most six) as a character, or 0 */
static u32 hex_char(const char *s, u32 n)
{
  u32 v = 0, i;
  if (!n || n > 6) return 0;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (!hexd(c)) return 0;
    v = v * 16 + (u32)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
  }
  return codeable(v) ? v : 0;
}
int fx_toggle_unicode(Doc *d, Pos a, Pos b, Pos *caret)
{
  Para *q;
  u32 s = 0, c = 0;
  char out[16];
  int n = 0, used;
  CharFmt f;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (a.para != b.para || a.para >= d->np) return 0;
  q = d->p[a.para];
  if (pos_cmp(a, b)) {
    u32 k = a.off;
    if (b.off - k > 2 && (q->t[k] == 'U' || q->t[k] == 'u') && q->t[k + 1] == '+') k += 2;
    if ((c = hex_char(q->t + k, b.off - k))) n = utf8_put(out, c);
    else {
      c = utf8_get(q->t + a.off, q->len - a.off, &used);
      if (a.off + (u32)used != b.off || !codeable(c)) return 0;
      sprintf(out, "%04lX", (unsigned long)c); n = (int)strlen(out);
    }
    s = a.off;
  } else {
    u32 i, k = 0;
    int found = 0;
    /* the hex digits just before the caret (six at most); the longest that names a character wins */
    while (k < 6 && k < b.off && hexd(q->t[b.off - 1 - k])) k++;
    for (i = k; i >= 1 && !found; i--)
      if ((c = hex_char(q->t + b.off - i, i))) {
        s = b.off - i;
        if (s >= 2 && (q->t[s - 1] == '+') && (q->t[s - 2] == 'U' || q->t[s - 2] == 'u')) s -= 2;
        n = utf8_put(out, c); found = 1;
      }
    if (!found) {
      if (!b.off) return 0;
      s = (u32)utf8_prev(q->t, b.off);
      c = utf8_get(q->t + s, q->len - s, &used);
      if (!codeable(c)) return 0;
      /* after hex digits the code is written U+xxxx, so it can be turned back */
      if (s > 0 && hexd(q->t[s - 1])) sprintf(out, "U+%04lX", (unsigned long)c); else sprintf(out, "%04lX", (unsigned long)c);
      n = (int)strlen(out);
    }
  }
  f = *para_fmt_at(q, s);
  a.off = s;
  doc_begin(d, a);
  doc_delete(d, a, b);
  doc_insert(d, &a, out, (u32)n, &f);
  doc_end(d, a);
  if (caret) *caret = a;
  return 1;
}
