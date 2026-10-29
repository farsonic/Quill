/*
 * compare.c - Compare and Combine (see compare.h).
 *
 * Each side's text (a story: the body, a header, a note) is cut into tokens: words, runs of white space,
 * single punctuation marks, objects (pictures, notes, equations, fields) and paragraph marks, or single
 * characters. A token carries a hash of what is compared (folded for case and white space as the options
 * say), its tracked-change state, and whether it takes part at all (comment marks, and with tables off the
 * text of tables, are only carried along: the revised document's kept, the original's dropped). The
 * sequences of the tokens that count are lined up (diff below); then the revised story is built again
 * from the script, with what only the original has copied over (its formatting, objects and notes made
 * the revised document's own) as deletions.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "compare.h"
#include "image.h"
#include "math.h"

int (*compare_fmt_hook)(Doc *d, int rid, const CharFmt *old, CharFmt *now);

void compare_defaults(CompareOpts *o)
{
  memset(o, 0, sizeof *o);
  o->formatting = o->casechg = o->whitespace = o->tables = o->headers = o->notes = o->fields = o->comments = 1;
  strcpy(o->author, "Author");
}

/* ------------------------------------------------------------------ tokens */
enum { TK_WORD, TK_SPACE, TK_PUNCT, TK_OBJ, TK_FIELD, TK_PARA, TK_MARK };
typedef struct { u32 para, off; u16 len; u8 kind, rev; u16 rid; u8 nodiff; u32 hash; } Tok;
typedef struct {
  Doc *d;
  Para **p; u32 np;
  Tok *t; u32 n, cap;
  u32 *base; u32 nbase;     /* the tokens that are compared (not insertions, not carried along) */
  u32 *bh;                  /* their hashes */
} Side;

static u32 fnv(u32 h, const void *p, size_t n)
{
  const u8 *b = (const u8 *)p;
  while (n--) { h ^= *b++; h *= 16777619UL; }
  return h;
}
static int is_space(u32 c) { return c == ' ' || c == '\t' || c == '\v' || c == 0xA0 || (c >= 0x2000 && c <= 0x200B) || c == 0x3000 || c == 0x202F; }
static int is_word(u32 c)
{
  if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
  if (c >= 0xE000 && c <= 0xF8FF) return 0;            /* (fields) */
  if (c >= 0xF0000) return 0;                          /* (objects) */
  if (c < 0xC0) return c == 0xAA || c == 0xB5 || c == 0xBA;
  if (c == 0xD7 || c == 0xF7) return 0;
  if (c >= 0x2000 && c <= 0x2BFF) return 0;            /* punctuation, symbols, arrows */
  if (c >= 0x3000 && c <= 0x303F) return 0;
  return 1;
}
static u32 fold(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if (c >= 0x100 && c < 0x180 && !(c & 1)) return c + 1;
  if (c >= 0x391 && c <= 0x3A9) return c + 32;
  if (c >= 0x410 && c <= 0x42F) return c + 32;
  return c;
}
/* an object's hash: what it is, and for a picture its size and first bytes */
static u32 obj_hash(Doc *d, const char *t)
{
  u32 h = 2166136261UL;
  if (IS_PIC(t)) {
    int k = pic_index(t);
    h = fnv(h, "pic", 3);
    if (k >= 0 && k < d->nimages) {
      const Image *im = &d->images[k];
      h = fnv(h, &im->kind, 1); h = fnv(h, &im->shape, 1);
      h = fnv(h, &im->len, sizeof im->len); h = fnv(h, &im->pw, sizeof im->pw); h = fnv(h, &im->ph, sizeof im->ph);
      if (im->data) h = fnv(h, im->data, im->len < 64 ? im->len : 64);
    }
  } else if (IS_NOTE(t)) h = fnv(h, "note", 4);
  else if (IS_ENOTE(t)) h = fnv(h, "enote", 5);
  else if (IS_MATH(t)) h = fnv(h, "math", 4);
  else h = fnv(h, t, 2);
  return h;
}
static int add_tok(Side *s, const Tok *t)
{
  if (s->n >= s->cap) {
    u32 nc = s->cap ? s->cap * 2 : 256;
    Tok *nt = (Tok *)realloc(s->t, nc * sizeof(Tok));
    if (!nt) return 0;
    s->t = nt; s->cap = nc;
  }
  s->t[s->n++] = *t;
  return 1;
}
/* the end of the stretch of text from off with the same tracked-change state (a token never crosses one) */
static u32 state_end(const Para *p, u32 off, u8 *rev, u16 *rid)
{
  u32 i;
  for (i = 0; i < p->nr; i++) {
    if (p->r[i].end <= off) continue;
    *rev = p->r[i].f.rev; *rid = p->r[i].f.rid;
    while (i + 1 < p->nr && p->r[i + 1].f.rev == *rev && p->r[i + 1].f.rid == *rid) i++;
    return p->r[i].end;
  }
  *rev = 0; *rid = 0;
  return p->len;
}
/* side s's tokens; accepted: as it reads with its tracked changes accepted (deletions left out, insertions
   plain), else with them */
static int tokenize(Side *s, const CompareOpts *o, int accepted)
{
  u32 pi, i;
  for (pi = 0; pi < s->np; pi++) {
    Para *p = s->p[pi];
    u32 off = 0;
    int intab = in_table(&p->pf) && !o->tables;
    Tok t;
    while (off < p->len) {
      u8 rev; u16 rid;
      u32 end = state_end(p, off, &rev, &rid);
      if (accepted && rev == REV_DEL) { off = end; continue; }
      if (accepted) rev = 0, rid = 0;
      while (off < end) {
        int used;
        u32 c = utf8_get(p->t + off, end - off, &used), h = 2166136261UL;
        u32 e = off + (u32)used;
        memset(&t, 0, sizeof t);
        t.para = pi; t.off = off; t.rev = rev; t.rid = rid;
        if (c >= 0xF0000) {
          t.kind = IS_MARK(p->t + off) ? TK_MARK : TK_OBJ;
          t.hash = obj_hash(s->d, p->t + off);
        } else if (c >= 0xE000 && c <= 0xE002) {
          t.kind = TK_FIELD;
          t.hash = o->fields ? fnv(h, p->t + off, (size_t)used) : fnv(h, "fld", 3);
        } else if (o->chars || !(is_word(c) || is_space(c))) {
          u32 f = o->casechg ? c : fold(c);
          t.kind = is_space(c) ? TK_SPACE : is_word(c) ? TK_WORD : TK_PUNCT;
          t.hash = t.kind == TK_SPACE && !o->whitespace ? fnv(h, " ", 1) : fnv(h, &f, sizeof f);
        } else {
          int sp = is_space(c);
          u32 f = o->casechg ? c : fold(c);
          t.kind = sp ? TK_SPACE : TK_WORD;
          h = fnv(h, &f, sizeof f);
          while (e < end && e - off < 4000) {
            u32 c2 = utf8_get(p->t + e, end - e, &used);
            if (sp ? !is_space(c2) : !is_word(c2)) break;
            f = o->casechg ? c2 : fold(c2);
            h = fnv(h, &f, sizeof f);
            e += (u32)used;
          }
          t.hash = sp && !o->whitespace ? fnv(2166136261UL, " ", 1) : h;
        }
        t.hash = fnv(t.hash, &t.kind, 1);
        t.len = (u16)(e - off);
        t.nodiff = (u8)(t.kind == TK_MARK || intab);
        if (!add_tok(s, &t)) return 0;
        off = e;
      }
    }
    memset(&t, 0, sizeof t);
    t.para = pi; t.off = p->len; t.kind = TK_PARA; t.hash = fnv(2166136261UL, "\n", 1) ^ 0x5bd1e995UL;
    t.nodiff = (u8)intab;
    if (!add_tok(s, &t)) return 0;
  }
  s->base = (u32 *)malloc(sizeof(u32) * (s->n + 1));
  s->bh = (u32 *)malloc(sizeof(u32) * (s->n + 1));
  if (!s->base || !s->bh) return 0;
  for (i = 0; i < s->n; i++)
    if (!s->t[i].nodiff && s->t[i].rev != REV_INS) { s->base[s->nbase] = i; s->bh[s->nbase++] = s->t[i].hash; }
  return 1;
}
static void side_free(Side *s) { free(s->t); free(s->base); free(s->bh); memset(s, 0, sizeof *s); }

/* ------------------------------------------------------------------ the diff: patience, with an exact LCS for small stretches */
enum { OP_EQ, OP_DEL, OP_INS };
typedef struct { u8 op; u32 i, j; } Op;
typedef struct { Op *o; u32 n, cap; int oom; } Ops;
static void op_add(Ops *s, int op, u32 i, u32 j)
{
  if (s->oom) return;
  if (s->n >= s->cap) {
    u32 nc = s->cap ? s->cap * 2 : 1024;
    Op *no = (Op *)realloc(s->o, nc * sizeof(Op));
    if (!no) { s->oom = 1; return; }
    s->o = no; s->cap = nc;
  }
  s->o[s->n].op = (u8)op; s->o[s->n].i = i; s->o[s->n].j = j; s->n++;
}
#define DP_MAX 250000UL
static void lcs(const u32 *a, u32 a0, u32 a1, const u32 *b, u32 b0, u32 b1, Ops *out)
{
  u32 n = a1 - a0, m = b1 - b0, i, j, w = m + 1;
  u16 *L = (u16 *)calloc((size_t)(n + 1) * w, sizeof(u16));
  if (!L) { for (i = a0; i < a1; i++) op_add(out, OP_DEL, i, 0); for (j = b0; j < b1; j++) op_add(out, OP_INS, 0, j); return; }
  for (i = n; i-- > 0;)
    for (j = m; j-- > 0;)
      L[i * w + j] = a[a0 + i] == b[b0 + j] ? (u16)(L[(i + 1) * w + j + 1] + 1)
                     : L[(i + 1) * w + j] >= L[i * w + j + 1] ? L[(i + 1) * w + j] : L[i * w + j + 1];
  i = j = 0;
  while (i < n && j < m) {
    if (a[a0 + i] == b[b0 + j]) { op_add(out, OP_EQ, a0 + i, b0 + j); i++; j++; }
    else if (L[(i + 1) * w + j] >= L[i * w + j + 1]) { op_add(out, OP_DEL, a0 + i, 0); i++; }
    else { op_add(out, OP_INS, 0, b0 + j); j++; }
  }
  for (; i < n; i++) op_add(out, OP_DEL, a0 + i, 0);
  for (; j < m; j++) op_add(out, OP_INS, 0, b0 + j);
  free(L);
}
typedef struct { u32 h, pos; u8 side; } HE;
static int he_cmp(const void *x, const void *y)
{
  const HE *a = (const HE *)x, *b = (const HE *)y;
  if (a->h != b->h) return a->h < b->h ? -1 : 1;
  if (a->side != b->side) return a->side - b->side;
  return a->pos < b->pos ? -1 : a->pos > b->pos;
}
typedef struct { u32 i, j; } Pair;
static int pair_cmp(const void *x, const void *y) { const Pair *a = (const Pair *)x, *b = (const Pair *)y; return a->i < b->i ? -1 : a->i > b->i; }
static void diff(const u32 *a, u32 a0, u32 a1, const u32 *b, u32 b0, u32 b1, Ops *out, int depth);
/* the anchors of a..b: tokens found once in each, the longest run of them in the same order */
static int patience(const u32 *a, u32 a0, u32 a1, const u32 *b, u32 b0, u32 b1, Ops *out, int depth)
{
  u32 na = a1 - a0, nb = b1 - b0, n = na + nb, i, k, np = 0, nl = 0;
  HE *he = (HE *)malloc(sizeof(HE) * n);
  Pair *pr = NULL;
  u32 *tails = NULL, *prev = NULL;
  int found = 0;
  if (!he) return 0;
  for (i = 0; i < na; i++) { he[i].h = a[a0 + i]; he[i].pos = a0 + i; he[i].side = 0; }
  for (i = 0; i < nb; i++) { he[na + i].h = b[b0 + i]; he[na + i].pos = b0 + i; he[na + i].side = 1; }
  qsort(he, n, sizeof(HE), he_cmp);
  if (!(pr = (Pair *)malloc(sizeof(Pair) * (n / 2 + 1)))) { free(he); return 0; }
  for (i = 0; i < n; i = k) {
    for (k = i + 1; k < n && he[k].h == he[i].h; k++) ;
    if (k - i == 2 && he[i].side == 0 && he[i + 1].side == 1) { pr[np].i = he[i].pos; pr[np].j = he[i + 1].pos; np++; }
  }
  free(he);
  if (!np) { free(pr); return 0; }
  qsort(pr, np, sizeof(Pair), pair_cmp);
  /* the longest increasing run of j (patience sorting, with links back) */
  tails = (u32 *)malloc(sizeof(u32) * np); prev = (u32 *)malloc(sizeof(u32) * np);
  if (tails && prev) {
    u32 *chain;
    for (i = 0; i < np; i++) {
      u32 lo = 0, hi = nl;
      while (lo < hi) { u32 mid = (lo + hi) / 2; if (pr[tails[mid]].j < pr[i].j) lo = mid + 1; else hi = mid; }
      prev[i] = lo ? tails[lo - 1] : (u32)-1;
      tails[lo] = i;
      if (lo == nl) nl++;
    }
    if ((chain = (u32 *)malloc(sizeof(u32) * nl))) {
      u32 c = tails[nl - 1], pa = a0, pb = b0;
      for (k = nl; k-- > 0;) { chain[k] = c; c = prev[c]; }
      for (k = 0; k < nl; k++) {
        Pair *q = &pr[chain[k]];
        diff(a, pa, q->i, b, pb, q->j, out, depth + 1);
        op_add(out, OP_EQ, q->i, q->j);
        pa = q->i + 1; pb = q->j + 1;
      }
      diff(a, pa, a1, b, pb, b1, out, depth + 1);
      free(chain);
      found = 1;
    }
  }
  free(tails); free(prev); free(pr);
  return found;
}
static void diff(const u32 *a, u32 a0, u32 a1, const u32 *b, u32 b0, u32 b1, Ops *out, int depth)
{
  u32 sa, sb, i;
  while (a0 < a1 && b0 < b1 && a[a0] == b[b0]) op_add(out, OP_EQ, a0++, b0++);
  sa = a1; sb = b1;
  while (sa > a0 && sb > b0 && a[sa - 1] == b[sb - 1]) { sa--; sb--; }
  if (a0 == sa) for (i = b0; i < sb; i++) op_add(out, OP_INS, 0, i);
  else if (b0 == sb) for (i = a0; i < sa; i++) op_add(out, OP_DEL, i, 0);
  else if ((sa - a0) < 65535 && (sb - b0) < 65535 && (u32)(sa - a0) * (sb - b0) <= DP_MAX) lcs(a, a0, sa, b, b0, sb, out);
  else if (depth > 40 || !patience(a, a0, sa, b, b0, sb, out, depth)) {
    for (i = a0; i < sa; i++) op_add(out, OP_DEL, i, 0);
    for (i = b0; i < sb; i++) op_add(out, OP_INS, 0, i);
  }
  for (i = 0; sa + i < a1; i++) op_add(out, OP_EQ, sa + i, sb + i);
}
/* a base token's weight for the clean-up: its characters (a paragraph mark one) */
static u32 weight(const Side *S, u32 bi) { const Tok *t = &S->t[S->base[bi]]; return t->kind == TK_PARA ? 1 : t->len; }
/* tidied for people (diff-match-patch's "semantic clean-up", simply): a stretch of equal text no longer than
   the changes on either side of it becomes part of them; then in each stretch of changes the deletions come
   before the insertions, as Word shows them */
static void tidy(Ops *s, const Side *O, const Side *R)
{
  u32 i, k, j, pass;
  Op *tmp;
  for (pass = 0; pass < 3; pass++) {
    int changed = 0;
    for (i = 0; i < s->n; i = k) {
      u32 eq = 0, bd = 0, bi = 0, ad = 0, ai = 0, extra, m;
      int para = 0, words = 0;
      for (k = i; k < s->n && s->o[k].op == OP_EQ; k++) {
        u8 kind = O->t[O->base[s->o[k].i]].kind;
        eq += weight(O, s->o[k].i);
        if (kind == TK_PARA) para = 1;
        if (kind == TK_WORD) words = 1;
      }
      if (k == i) { k++; continue; }
      if (i == 0 || k >= s->n || para) continue;
      for (j = i; j-- > 0 && s->o[j].op != OP_EQ;) { if (s->o[j].op == OP_DEL) bd += weight(O, s->o[j].i); else bi += weight(R, s->o[j].j); }
      for (j = k; j < s->n && s->o[j].op != OP_EQ; j++) { if (s->o[j].op == OP_DEL) ad += weight(O, s->o[j].i); else ai += weight(R, s->o[j].j); }
      if (words) eq *= 2;                      /* (a word in common is worth keeping: only half the changes' size) */
      if (eq > (bd > bi ? bd : bi) || eq > (ad > ai ? ad : ai)) continue;
      /* each equal token becomes a deletion here and an insertion after */
      extra = k - i;
      if (s->n + extra > s->cap) {
        Op *no = (Op *)realloc(s->o, (s->n + extra) * sizeof(Op));
        if (!no) return;
        s->o = no; s->cap = s->n + extra;
      }
      memmove(s->o + k + extra, s->o + k, (s->n - k) * sizeof(Op));
      for (m = 0; m < extra; m++) {
        u32 rj = s->o[i + m].j;
        s->o[i + m].op = OP_DEL;
        s->o[k + m].op = OP_INS; s->o[k + m].i = 0; s->o[k + m].j = rj;
      }
      s->n += extra;
      k += extra;
      changed = 1;
    }
    if (!changed) break;
  }
  if (!(tmp = (Op *)malloc(sizeof(Op) * (s->n + 1)))) return;
  for (i = 0; i < s->n; i = k) {
    u32 o = 0;
    for (k = i; k < s->n && s->o[k].op != OP_EQ; k++) ;
    if (k == i) { k++; continue; }
    for (j = i; j < k; j++) if (s->o[j].op == OP_DEL) tmp[o++] = s->o[j];
    for (j = i; j < k; j++) if (s->o[j].op == OP_INS) tmp[o++] = s->o[j];
    memcpy(s->o + i, tmp, (k - i) * sizeof(Op));
  }
  free(tmp);
}

/* ------------------------------------------------------------------ lining up paragraphs first */
/* each paragraph's base tokens: first index into base, how many, and the hash of them all */
typedef struct { u32 first, n, hash; } PRange;
static PRange *para_ranges(const Side *S, u32 *np)
{
  PRange *r = (PRange *)malloc(sizeof(PRange) * (S->nbase + 1));
  u32 i, n = 0;
  if (!r) return NULL;
  for (i = 0; i < S->nbase; i++) {
    if (!n || S->t[S->base[i - 1]].kind == TK_PARA) { r[n].first = i; r[n].n = 0; r[n].hash = 2166136261UL; n++; }
    r[n - 1].n++;
    r[n - 1].hash = (r[n - 1].hash ^ S->bh[i]) * 16777619UL;
  }
  *np = n;
  return r;
}
/* how alike two paragraphs are: their words in common (each counted once a side, by hash), 0-100 */
static int alike(const Side *O, const PRange *a, const Side *R, const PRange *b)
{
  u32 i, j, common = 0, wa = 0, wb = 0;
  for (i = 0; i < a->n; i++) if (O->t[O->base[a->first + i]].kind == TK_WORD) wa++;
  for (j = 0; j < b->n; j++) if (R->t[R->base[b->first + j]].kind == TK_WORD) wb++;
  if (!wa || !wb) return a->n == b->n && a->n <= 2 ? 60 : 0;      /* (empty paragraphs are alike) */
  if (wa > 400 || wb > 400) return 0;
  for (i = 0; i < a->n; i++) {
    u32 h = O->bh[a->first + i];
    if (O->t[O->base[a->first + i]].kind != TK_WORD) continue;
    for (j = 0; j < b->n; j++) if (R->bh[b->first + j] == h && R->t[R->base[b->first + j]].kind == TK_WORD) { common++; break; }
  }
  return (int)(200 * common / (wa + wb));
}
static void emit_range(Ops *s, int op, const PRange *p)
{
  u32 k;
  for (k = 0; k < p->n; k++) op_add(s, op, op == OP_DEL ? p->first + k : 0, op == OP_INS ? p->first + k : 0);
}
/* paragraphs a0..a1 of O and b0..b1 of R, none equal: those alike paired (an LCS by likeness) and compared
   word by word, the others deleted or inserted whole */
static void pair_region(const Side *O, const PRange *pa, u32 a0, u32 a1, const Side *R, const PRange *pb, u32 b0, u32 b1, Ops *s)
{
  u32 n = a1 - a0, m = b1 - b0, i, j, w = m + 1;
  u16 *L;
  u8 *A;
  if (!n || !m || n * m > 40000 || !(L = (u16 *)calloc((size_t)(n + 1) * w, sizeof(u16)))) {
    for (i = a0; i < a1; i++) emit_range(s, OP_DEL, &pa[i]);
    for (j = b0; j < b1; j++) emit_range(s, OP_INS, &pb[j]);
    return;
  }
  if (!(A = (u8 *)malloc((size_t)n * m))) { free(L); for (i = a0; i < a1; i++) emit_range(s, OP_DEL, &pa[i]); for (j = b0; j < b1; j++) emit_range(s, OP_INS, &pb[j]); return; }
  for (i = 0; i < n; i++) for (j = 0; j < m; j++) A[i * m + j] = (u8)(alike(O, &pa[a0 + i], R, &pb[b0 + j]) >= 50);
  for (i = n; i-- > 0;)
    for (j = m; j-- > 0;)
      L[i * w + j] = A[i * m + j] ? (u16)(L[(i + 1) * w + j + 1] + 1) : L[(i + 1) * w + j] >= L[i * w + j + 1] ? L[(i + 1) * w + j] : L[i * w + j + 1];
  i = j = 0;
  while (i < n && j < m) {
    if (A[i * m + j] && L[i * w + j] == L[(i + 1) * w + j + 1] + 1) {
      const PRange *x = &pa[a0 + i], *y = &pb[b0 + j];
      Ops sub;
      u32 k;
      memset(&sub, 0, sizeof sub);
      diff(O->bh, x->first, x->first + x->n, R->bh, y->first, y->first + y->n, &sub, 0);
      if (sub.oom) s->oom = 1;
      tidy(&sub, O, R);
      for (k = 0; k < sub.n; k++) op_add(s, sub.o[k].op, sub.o[k].i, sub.o[k].j);
      free(sub.o);
      i++; j++;
    } else if (L[(i + 1) * w + j] >= L[i * w + j + 1]) emit_range(s, OP_DEL, &pa[a0 + i++]);
    else emit_range(s, OP_INS, &pb[b0 + j++]);
  }
  for (; i < n; i++) emit_range(s, OP_DEL, &pa[a0 + i]);
  for (; j < m; j++) emit_range(s, OP_INS, &pb[b0 + j]);
  free(L); free(A);
}
/* the whole script: paragraphs lined up by their hashes, then the stretches between paired by likeness */
static void align(const Side *O, const Side *R, Ops *s)
{
  u32 na, nb, i, ia = 0, ib = 0;
  PRange *pa = para_ranges(O, &na), *pb = para_ranges(R, &nb);
  u32 *ha = NULL, *hb = NULL;
  Ops ps;
  memset(&ps, 0, sizeof ps);
  if (!pa || !pb || !(ha = (u32 *)malloc(sizeof(u32) * (na + 1))) || !(hb = (u32 *)malloc(sizeof(u32) * (nb + 1)))) {
    diff(O->bh, 0, O->nbase, R->bh, 0, R->nbase, s, 0);
    tidy(s, O, R);
    free(pa); free(pb); free(ha);
    return;
  }
  for (i = 0; i < na; i++) ha[i] = pa[i].hash;
  for (i = 0; i < nb; i++) hb[i] = pb[i].hash;
  diff(ha, 0, na, hb, 0, nb, &ps, 0);
  if (ps.oom) s->oom = 1;
  for (i = 0; i <= ps.n && !s->oom; i++) {
    if (i < ps.n && ps.o[i].op != OP_EQ) continue;
    /* the stretch of unequal paragraphs before this equal pair (or the end) */
    {
      u32 ea = i < ps.n ? ps.o[i].i : na, eb = i < ps.n ? ps.o[i].j : nb;
      if (ea > ia || eb > ib) pair_region(O, pa, ia, ea, R, pb, ib, eb, s);
      if (i < ps.n) {
        const PRange *x = &pa[ea], *y = &pb[eb];
        u32 k;
        if (x->n == y->n) for (k = 0; k < x->n; k++) op_add(s, OP_EQ, x->first + k, y->first + k);
        else pair_region(O, pa, ea, ea + 1, R, pb, eb, eb + 1, s);
        ia = ea + 1; ib = eb + 1;
      }
    }
  }
  free(ps.o); free(pa); free(pb); free(ha); free(hb);
}

/* ------------------------------------------------------------------ building the revised story again */
typedef struct {
  Doc *o, *r;
  const CompareOpts *opt;
  int label;                /* the label's Doc.revs index in r */
  Para **out; u32 nout, cap;
  Para *cur;
  int oom, changes, lastmark;
} Build;

/* one of o's formats made r's: fonts, character styles and links found again by name */
static CharFmt o_fmt(Build *b, const CharFmt *src)
{
  CharFmt f = *src;
  f.font = (u8)(src->font < b->o->nfonts ? doc_font(b->r, b->o->fonts[src->font]) : 0);
  if (src->cstyle && src->cstyle < b->o->nstyles) {
    int k = doc_style(b->r, b->o->styles[src->cstyle].id);
    f.cstyle = (u8)(k > 0 && b->r->styles[k].type == ST_CHAR ? k : 0);
  }
  f.link = (u16)(src->link ? doc_link(b->r, doc_link_url(b->o, src->link)) : 0);
  f.bmark = 0; f.perm = 0;
  return f;
}
static int same_look(Build *b, const CharFmt *of, const CharFmt *rf)
{
  const char *ofn = of->font < b->o->nfonts ? b->o->fonts[of->font] : "", *rfn = rf->font < b->r->nfonts ? b->r->fonts[rf->font] : "";
  const char *ocs = of->cstyle && of->cstyle < b->o->nstyles ? b->o->styles[of->cstyle].id : "";
  const char *rcs = rf->cstyle && rf->cstyle < b->r->nstyles ? b->r->styles[rf->cstyle].id : "";
  return of->flags == rf->flags && str_ieq(ofn, rfn) && of->size == rf->size && of->color == rf->color && of->hl == rf->hl &&
         !strcmp(ocs, rcs) && of->fx == rf->fx && of->ul == rf->ul && of->spacing == rf->spacing && of->scale == rf->scale &&
         of->position == rf->position && of->fx2 == rf->fx2 && of->ulcolor == rf->ulcolor && of->shade == rf->shade && of->rx == rf->rx;
}
static Para *cur_para(Build *b, const CharFmt *f)
{
  if (!b->cur) {
    ParaFmt pf;
    memset(&pf, 0, sizeof pf);
    if (!(b->cur = para_new(&pf, f))) b->oom = 1;
  }
  return b->cur;
}
static void put(Build *b, const char *s, u32 n, const CharFmt *f)
{
  Para *p = cur_para(b, f);
  if (p && !para_append(p, s, n, f)) b->oom = 1;
}
/* the paragraph being built ends: its formatting is rp's (the revised paragraph whose mark this is) */
static void close_para(Build *b, const Para *rp)
{
  Para *p = cur_para(b, &rp->r[rp->nr - 1].f);
  if (!p) return;
  p->pf = rp->pf;
  if (!p->len) p->r[0].f = rp->r[0].f;
  para_normalise(p);
  if (b->nout >= b->cap) {
    u32 nc = b->cap ? b->cap * 2 : 64;
    Para **np = (Para **)realloc(b->out, nc * sizeof(Para *));
    if (!np) { b->oom = 1; return; }
    b->out = np; b->cap = nc;
  }
  b->out[b->nout++] = p;
  b->cur = NULL;
}
static int rid_of(Build *b, Doc *d, int rid)
{
  if (rid < 0 || rid >= d->nrevs) return b->label;
  return doc_add_rev(b->r, d->revs[rid].author, d->revs[rid].date);
}
static void count(Build *b, int mark) { if (mark && mark != b->lastmark) b->changes++; b->lastmark = mark; }
/* a paragraph of o's made r's (for notes copied over), its text deleted (rev) by rid */
static Para *o_para(Build *b, const Para *src, int rev, int rid)
{
  ParaFmt pf = src->pf;
  CharFmt f0 = o_fmt(b, &src->r[0].f);
  Para *p;
  u32 i, start = 0;
  int st = src->pf.style < b->o->nstyles ? doc_style(b->r, b->o->styles[src->pf.style].id) : 0;
  pf.style = (u8)(st > 0 && b->r->styles[st].type == ST_PARA ? st : 0);
  pf.list = 0; pf.sect = 0;
  memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer);
  f0.rev = (u8)rev; f0.rid = (u16)rid;
  if (!(p = para_new(&pf, &f0))) return NULL;
  for (i = 0; i < src->nr; start = src->r[i].end, i++) {
    CharFmt f = o_fmt(b, &src->r[i].f);
    u32 k;
    f.rev = (u8)rev; f.rid = (u16)rid;
    for (k = start; k < src->r[i].end;) {
      int used;
      u32 c = utf8_get(src->t + k, src->r[i].end - k, &used);
      if (c < 0xF0000 && !para_append(p, src->t + k, (u32)used, &f)) { para_free(p); return NULL; }   /* (objects in notes left out) */
      k += (u32)used;
    }
  }
  para_normalise(p);
  return p;
}
/* one of o's objects copied into r: the reference to put in r's text (4 bytes and a NUL), 0 if it can't be */
static int o_object(Build *b, const char *t, char *ref, int rev, int rid)
{
  Doc *o = b->o, *r = b->r;
  int k = note_index(t), nk;
  if (IS_PIC(t)) {
    Image im;
    k = pic_index(t);
    if (k < 0 || k >= o->nimages || !img_clone(&im, &o->images[k], 1)) return 0;
    if ((nk = doc_take_object(r, &im)) < 0) { img_release(&im); return 0; }
    pic_utf8(ref, nk);
    return 1;
  }
  if (IS_NOTE(t) || IS_ENOTE(t)) {
    int en = IS_ENOTE(t) != 0;
    Story *src;
    u32 i;
    if (k < 0 || k >= (en ? o->nenotes : o->nnotes)) return 0;
    src = en ? &o->enotes[k] : &o->notes[k];
    if ((nk = en ? doc_add_endnote(r) : doc_add_note(r)) < 0) return 0;
    {
      Story *dst = en ? &r->enotes[nk] : &r->notes[nk];
      Para **pp = (Para **)calloc(src->np ? src->np : 1, sizeof(Para *));
      if (!pp) return 0;
      for (i = 0; i < src->np; i++) if (!(pp[i] = o_para(b, src->p[i], rev, rid))) break;
      if (i < src->np) { while (i--) para_free(pp[i]); free(pp); return 0; }
      story_free(dst);
      dst->p = pp; dst->np = src->np;
    }
    if (en) enote_utf8(ref, nk); else note_utf8(ref, nk);
    return 1;
  }
  if (IS_MATH(t)) {
    MathEq *e = doc_math(o, math_index(t)), *c;
    if (!e || !(c = math_clone(e)) || (nk = doc_add_math(r, c)) < 0) return 0;
    math_utf8(ref, nk);
    return 1;
  }
  return 0;                                    /* (comment marks: the original's comments aren't brought over) */
}
/* a token of the original, in r as a deletion (rev REV_DEL) or insertion by rid */
static void emit_o(Build *b, const Side *O, const Tok *t, int rev, int rid)
{
  const Para *p = O->p[t->para];
  count(b, rev ? rid * 4 + rev : 0);
  if (t->kind == TK_PARA) {
    /* a paragraph mark only the original has: a line break in its place */
    CharFmt f = o_fmt(b, &p->r[p->nr - 1].f);
    f.rev = (u8)rev; f.rid = (u16)rid;
    put(b, "\v", 1, &f);
    return;
  }
  if (t->kind == TK_OBJ || t->kind == TK_MARK) {
    char ref[8];
    CharFmt f = o_fmt(b, para_fmt_at((Para *)p, t->off));
    f.rev = (u8)rev; f.rid = (u16)rid;
    if (o_object(b, p->t + t->off, ref, rev, rid)) put(b, ref, 4, &f);
    return;
  }
  {
    u32 k = t->off, e = t->off + t->len, i;
    for (i = 0; i < p->nr && k < e; i++) {
      u32 re = p->r[i].end < e ? p->r[i].end : e;
      CharFmt f;
      if (re <= k) continue;
      f = o_fmt(b, &p->r[i].f);
      f.rev = (u8)rev; f.rid = (u16)rid;
      put(b, p->t + k, re - k, &f);
      k = re;
    }
  }
}
/* the same token in both whose look differs (piece by piece, by the character at the same place) */
static int look_differs(Build *b, const Side *O, const Tok *ot, const Side *R, const Tok *rt)
{
  const Para *p = R->p[rt->para], *op = O->p[ot->para];
  u32 k = rt->off, e = rt->off + rt->len, i;
  if (!b->opt->formatting || rt->kind == TK_PARA || ot->rev || rt->rev) return 0;
  for (i = 0; i < p->nr && k < e; i++) {
    u32 re = p->r[i].end < e ? p->r[i].end : e;
    if (re <= k) continue;
    if (!same_look(b, para_fmt_at((Para *)op, ot->off + (ot->len == rt->len ? k - rt->off : 0)), &p->r[i].f)) return 1;
    k = re;
  }
  return 0;
}
/* a token of the revised document, marked rev by rid (rev -1: as it is); o (may be NULL) its counterpart
   in the original, for the formatting check */
static void emit_r(Build *b, const Side *R, const Tok *t, int rev, int rid, const Side *O, const Tok *ot)
{
  const Para *p = R->p[t->para];
  if (t->kind == TK_PARA) { close_para(b, p); return; }
  count(b, rev > 0 ? rid * 4 + rev : 0);
  {
    u32 k = t->off, e = t->off + t->len, i;
    for (i = 0; i < p->nr && k < e; i++) {
      u32 re = p->r[i].end < e ? p->r[i].end : e;
      CharFmt f;
      if (re <= k) continue;
      f = p->r[i].f;
      if (rev >= 0) { f.rev = (u8)rev; f.rid = (u16)rid; }
      if (ot && rev == 0 && b->opt->formatting) {
        /* the same text: its look then and now (by the character at the same place, or the first) */
        const Para *op = O->p[ot->para];
        u32 at = ot->off + (ot->len == t->len ? k - t->off : 0);
        const CharFmt *of = para_fmt_at((Para *)op, at);
        if (!same_look(b, of, &f)) {
          CharFmt old = o_fmt(b, of);
          if (compare_fmt_hook && compare_fmt_hook(b->r, b->label, &old, &f)) count(b, b->label * 4 + 3);
          else {
            /* (no tracked formatting changes: shown as the old look deleted and the new one inserted) */
            u32 ok0 = ot->len == t->len ? at : ot->off, ok1 = ot->len == t->len ? at + (re - k) : ot->off + ot->len;
            if (ot->len == t->len || k == t->off) {
              Tok piece = *ot;
              piece.off = ok0; piece.len = (u16)(ok1 - ok0);
              emit_o(b, O, &piece, REV_DEL, b->label);
            }
            f.rev = REV_INS; f.rid = (u16)b->label;
            count(b, b->label * 4 + REV_INS);
          }
        }
      }
      put(b, p->t + k, re - k, &f);
      k = re;
    }
  }
}
/* the tokens of a side from *at up to k that aren't compared: insertions (marked by their author) and
   tokens carried along (the revised document's kept as they are, the original's dropped) */
static void flush(Build *b, const Side *S, u32 *at, u32 k, int isr)
{
  for (; *at < k; (*at)++) {
    const Tok *t = &S->t[*at];
    if (t->nodiff) { if (isr) emit_r(b, S, t, t->rev ? -1 : 0, 0, NULL, NULL); continue; }
    if (t->rev != REV_INS) continue;                   /* (a compared token: the script's) */
    if (isr) emit_r(b, S, t, REV_INS, rid_of(b, b->r, t->rid), NULL, NULL);
    else emit_o(b, S, t, REV_INS, rid_of(b, b->o, t->rid));
  }
}
/* the story rp/rnp (the revised side's, replaced) built again from op/onp and itself */
static int merge_story(Doc *o, Doc *r, const CompareOpts *opt, Para **op, u32 onp, Para ***rp, u32 *rnp, int accepted, int label)
{
  Side O, R;
  Ops s;
  Build b;
  u32 i, io = 0, ir = 0;
  int ok = 0;
  memset(&O, 0, sizeof O); memset(&R, 0, sizeof R); memset(&s, 0, sizeof s); memset(&b, 0, sizeof b);
  if (!*rnp) return 0;                              /* (nothing to build on) */
  O.d = o; O.p = op; O.np = onp;
  R.d = r; R.p = *rp; R.np = *rnp;
  b.o = o; b.r = r; b.opt = opt; b.label = label;
  if (!tokenize(&O, opt, accepted) || !tokenize(&R, opt, accepted)) goto done;
  align(&O, &R, &s);
  if (s.oom) goto done;
  for (i = 0; i < s.n && !b.oom; i++) {
    const Op *x = &s.o[i];
    if (x->op == OP_EQ && !compare_fmt_hook && look_differs(&b, &O, &O.t[O.base[x->i]], &R, &R.t[R.base[x->j]])) {
      /* text whose look changed, and no tracked formatting changes: the stretch of it (white space between
         taken along) shown as deleted in its old look, then inserted in its new one */
      u32 j = i, last = i, k;
      while (j + 1 < s.n && s.o[j + 1].op == OP_EQ) {
        const Tok *o2 = &O.t[O.base[s.o[j + 1].i]], *r2 = &R.t[R.base[s.o[j + 1].j]];
        if (r2->kind == TK_PARA) break;
        if (look_differs(&b, &O, o2, &R, r2)) last = j + 1;
        else if (r2->kind != TK_SPACE) break;
        j++;
      }
      for (k = i; k <= last; k++) {
        flush(&b, &O, &io, O.base[s.o[k].i], 0);
        emit_o(&b, &O, &O.t[O.base[s.o[k].i]], REV_DEL, label);
        io = O.base[s.o[k].i] + 1;
      }
      for (k = i; k <= last; k++) {
        flush(&b, &R, &ir, R.base[s.o[k].j], 1);
        emit_r(&b, &R, &R.t[R.base[s.o[k].j]], REV_INS, label, NULL, NULL);
        ir = R.base[s.o[k].j] + 1;
      }
      i = last;
    } else if (x->op == OP_EQ) {
      const Tok *ot = &O.t[O.base[x->i]], *rt = &R.t[R.base[x->j]];
      flush(&b, &O, &io, O.base[x->i], 0);
      flush(&b, &R, &ir, R.base[x->j], 1);
      if (ot->rev == REV_DEL && rt->kind != TK_PARA) emit_r(&b, &R, rt, REV_DEL, rid_of(&b, o, ot->rid), NULL, NULL);
      else if (rt->rev == REV_DEL) emit_r(&b, &R, rt, REV_DEL, rid_of(&b, r, rt->rid), NULL, NULL);
      else emit_r(&b, &R, rt, 0, 0, &O, ot);
      io = O.base[x->i] + 1; ir = R.base[x->j] + 1;
    } else if (x->op == OP_DEL) {
      const Tok *ot = &O.t[O.base[x->i]];
      flush(&b, &O, &io, O.base[x->i], 0);
      emit_o(&b, &O, ot, REV_DEL, ot->rev == REV_DEL ? rid_of(&b, o, ot->rid) : label);
      io = O.base[x->i] + 1;
    } else {
      const Tok *rt = &R.t[R.base[x->j]];
      flush(&b, &R, &ir, R.base[x->j], 1);
      if (rt->kind == TK_PARA) emit_r(&b, &R, rt, 0, 0, NULL, NULL);       /* (a new paragraph: its break can't be marked) */
      else if (rt->rev != REV_DEL) emit_r(&b, &R, rt, REV_INS, label, NULL, NULL);
      ir = R.base[x->j] + 1;
    }
  }
  flush(&b, &O, &io, O.n, 0);
  flush(&b, &R, &ir, R.n, 1);
  if (b.cur) {
    /* text after the revised side's last paragraph mark (what only the original had at its end) */
    if (b.nout) {
      Para *last = b.out[b.nout - 1];
      u32 k, start = 0;
      for (k = 0; k < b.cur->nr && !b.oom; start = b.cur->r[k].end, k++)
        if (!para_append(last, b.cur->t + start, b.cur->r[k].end - start, &b.cur->r[k].f)) b.oom = 1;
      para_normalise(last);
      para_free(b.cur);
    } else close_para(&b, R.p[R.np - 1]);
    b.cur = NULL;
  }
  if (b.oom || !b.nout) goto done;
  /* the new paragraphs take the old ones' place */
  for (i = 0; i < *rnp; i++) para_free((*rp)[i]);
  free(*rp);
  *rp = b.out; *rnp = b.nout;
  b.out = NULL; b.nout = 0;
  ok = 1;
done:
  for (i = 0; i < b.nout; i++) para_free(b.out[i]);
  free(b.out);
  if (b.cur) para_free(b.cur);
  free(s.o);
  side_free(&O); side_free(&R);
  return ok ? b.changes : -1;
}
/* every story compared or combined */
static int run(Doc *o, Doc *r, const CompareOpts *opt, int accepted)
{
  int label, n, total = 0, i, rnotes = r->nnotes, renotes = r->nenotes;
  u32 np;
  doc_story_end(o, 1); doc_story_end(r, 1);
  label = doc_add_rev(r, opt->author[0] ? opt->author : "Author", opt->date);
  np = r->np;
  if ((n = merge_story(o, r, opt, o->p, o->np, &r->p, &np, accepted, label)) < 0) return -1;
  r->np = np; r->pcap = np;
  total += n;
  if (opt->headers)
    for (i = 0; i < HF_N; i++) {
      if (o->hdr[i].np && r->hdr[i].np && (n = merge_story(o, r, opt, o->hdr[i].p, o->hdr[i].np, &r->hdr[i].p, &r->hdr[i].np, accepted, label)) > 0) total += n;
      if (o->ftr[i].np && r->ftr[i].np && (n = merge_story(o, r, opt, o->ftr[i].p, o->ftr[i].np, &r->ftr[i].p, &r->ftr[i].np, accepted, label)) > 0) total += n;
    }
  if (opt->notes) {
    /* notes by their number (the revised document's own notes; those copied over are deletions already) */
    int nn = rnotes < o->nnotes ? rnotes : o->nnotes, ne = renotes < o->nenotes ? renotes : o->nenotes;
    for (i = 0; i < nn; i++)
      if (o->notes[i].np && r->notes[i].np && (n = merge_story(o, r, opt, o->notes[i].p, o->notes[i].np, &r->notes[i].p, &r->notes[i].np, accepted, label)) > 0) total += n;
    for (i = 0; i < ne; i++)
      if (o->enotes[i].np && r->enotes[i].np && (n = merge_story(o, r, opt, o->enotes[i].p, o->enotes[i].np, &r->enotes[i].p, &r->enotes[i].np, accepted, label)) > 0) total += n;
  }
  r->ver++; r->dirty = 1;
  doc_clear_undo(r);
  return total;
}
int doc_compare(Doc *orig, Doc *rev, const CompareOpts *o) { return run(orig, rev, o, 1); }
int doc_combine(Doc *orig, Doc *rev, const CompareOpts *o) { return run(orig, rev, o, 0); }
