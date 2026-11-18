/* mathml.c - see mathml.h: MathML in and out of Quill's equation model (WP28b; this file belongs to
   that package). Written from the MathML 3 specification's presentation markup (chapter 3) and the
   mapping between MathML and OMML that Word and LibreOffice use. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mathml.h"

/* ------------------------------------------------------------------ */
/* reading */

/* what a child element was, for the structures that look at their arguments */
typedef struct {
  MNode *row;               /* its content, as a row */
  u32 op;                   /* it was a single operator (an mo, or a row of just one): the character, else 0 */
  char name[16];            /* the element's own name */
  char word[16];            /* it was a single identifier (mi) of a few letters: them ("lim"), else "" */
} Arg;

static void conv(Xml *x, MNode *row, Arg *info);
static int is_funcword(const char *w);
static int is_nary(u32 c);
static void cpy(char *dst, size_t cap, const char *src)
{
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = 0;
}

/* a token's text, read to its END, trimmed */
static void token_text(Xml *x, Buf *t)
{
  int d0 = x->depth;
  size_t a, b;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_TEXT) buf_add(t, x->text.data, x->text.len);
    else if (x->type == XT_START && !strcmp(x->name, "mglyph")) { const char *alt = xml_attr(x, "alt"); if (alt) buf_adds(t, alt); }
  }
  if (!t->data) return;
  for (a = 0; a < t->len && (t->data[a] == ' ' || t->data[a] == '\n' || t->data[a] == '\r' || t->data[a] == '\t'); a++) ;
  for (b = t->len; b > a && (t->data[b - 1] == ' ' || t->data[b - 1] == '\n' || t->data[b - 1] == '\r' || t->data[b - 1] == '\t'); b--) ;
  memmove(t->data, t->data + a, b - a);
  t->len = b - a; t->data[t->len] = 0;
}

static int is_nary(u32 c)
{
  return (c >= 0x220F && c <= 0x2211) || (c >= 0x222B && c <= 0x2233) || (c >= 0x22C0 && c <= 0x22C3) || (c >= 0x2A00 && c <= 0x2A0C);
}
static u32 first_cp(const char *s)
{
  int used;
  return s && *s ? utf8_get(s, strlen(s), &used) : 0;
}
static int one_cp(const char *s)
{
  int used;
  if (!s || !*s) return 0;
  utf8_get(s, strlen(s), &used);
  return s[used] == 0;
}

/* a run of text added to row: joined to the run before when both are plain math tokens */
static void add_run(MNode *row, const char *s, size_t n, int normal, int upright, int join)
{
  MNode *r;
  if (!n) return;
  if (join && row->nkid) {
    MNode *p = row->kid[row->nkid - 1];
    if (p->kind == MK_RUN && !p->pv[P_R_NOR] && !p->pv[P_R_STY] && p->text) {
      size_t m = strlen(p->text);
      char *t = (char *)realloc(p->text, m + n + 1);
      if (!t) return;
      memcpy(t + m, s, n); t[m + n] = 0;
      p->text = t;
      return;
    }
  }
  if (!(r = mn_run(s, n))) return;
  if (normal) r->pv[P_R_NOR] = MB_ON;
  else if (upright) r->pv[P_R_STY] = MSTY_P + 1;
  mn_insert(row, -1, r);
}

/* one child element (x at its START) as an argument: its content in a row of its own */
static void arg_read(Xml *x, Arg *a)
{
  memset(a, 0, sizeof *a);
  a->row = mn_new(MK_ROW);
  cpy(a->name, sizeof a->name, x->name);
  if (!a->row) { xml_skip(x); return; }
  conv(x, a->row, a);
}
/* the children of the element x is in (just after its START), as arguments; how many (at most max;
   more are dropped) */
static int args_read(Xml *x, Arg *a, int max)
{
  int d0 = x->depth, n = 0;
  if (x->pending_end) { xml_next(x); return 0; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (n < max) arg_read(x, &a[n++]);
    else xml_skip(x);
  }
  return n;
}
static void args_free(Arg *a, int n) { int i; for (i = 0; i < n; i++) mn_free(a[i].row); }
/* argument k of structure s replaced by row (taken; an empty row when NULL) */
static void set_arg(MNode *s, int k, MNode *row)
{
  if (k >= s->nkid) { mn_free(row); return; }
  if (!row) return;
  mn_free(s->kid[k]);
  s->kid[k] = row;
}
/* the contents of row src moved to the end of row dst (src freed) */
static void row_append(MNode *dst, MNode *src)
{
  u16 i;
  if (!src) return;
  for (i = 0; i < src->nkid; i++) {
    MNode *k = src->kid[i];
    if (k->kind == MK_RUN && !k->pv[P_R_NOR] && !k->pv[P_R_STY] && k->text) { add_run(dst, k->text, strlen(k->text), 0, 0, 1); mn_free(k); }
    else mn_insert(dst, -1, k);
  }
  src->nkid = 0;
  mn_free(src);
}

/* a delimiter structure from a fenced row's inside: separators (sep, 0 none) split it into arguments */
static MNode *make_delim(u32 open, u32 close, MNode *inside, u32 sep)
{
  MNode *d = mn_struct(MK_DELIM, 1, 0), *cur;
  u16 i;
  if (!d) { mn_free(inside); return NULL; }
  d->ch[0] = open == '(' ? MCH_UNSET : open;
  d->ch[2] = close == ')' ? MCH_UNSET : close;
  if (sep && sep != '|') d->ch[1] = sep;
  cur = d->kid[0];
  if (!inside) return d;
  for (i = 0; i < inside->nkid; i++) {
    MNode *k = inside->kid[i];
    if (sep && k->kind == MK_RUN && !k->pv[P_R_NOR] && k->text && strchr(k->text, (int)(sep < 128 ? sep : 1)) && sep < 128) {
      /* a run holding separators: split it */
      const char *t = k->text, *s;
      while ((s = strchr(t, (int)sep))) {
        add_run(cur, t, (size_t)(s - t), 0, 0, 1);
        if (!mn_insert(d, -1, mn_new(MK_ROW))) break;
        cur = d->kid[d->nkid - 1];
        t = s + 1;
      }
      add_run(cur, t, strlen(t), 0, 0, 1);
      mn_free(k);
      continue;
    }
    mn_insert(cur, -1, k);
  }
  inside->nkid = 0;
  mn_free(inside);
  return d;
}

/* function application (U+2061) in a row: the name before it and the argument after it made a
   function structure */
static void find_funcs(MNode *row)
{
  u16 i;
  for (i = 0; i < row->nkid; i++) {
    MNode *k = row->kid[i], *f, *name;
    size_t n;
    char *s;
    if (k->kind != MK_RUN || !k->text || !(s = strstr(k->text, "\xE2\x81\xA1"))) continue;
    n = (size_t)(s - k->text);
    if (!(f = mn_struct(MK_FUNC, 2, 0))) return;
    /* the name: the run's text before the mark, else the element before */
    if (n) {
      name = mn_run(k->text, n);
      if (name) { name->pv[P_R_STY] = k->pv[P_R_STY] ? k->pv[P_R_STY] : (u8)(MSTY_P + 1); mn_insert(f->kid[0], -1, name); }
    } else if (i > 0) {
      mn_insert(f->kid[0], -1, mn_remove(row, i - 1));
      i--;
    }
    /* what follows the mark in the run, or the next element, is the argument */
    if (s[3]) {
      MNode *rest = mn_run(s + 3, strlen(s + 3));
      if (rest) mn_insert(f->kid[1], -1, rest);
      mn_free(mn_remove(row, i));
    } else {
      mn_free(mn_remove(row, i));
      if (i < row->nkid) mn_insert(f->kid[1], -1, mn_remove(row, i));
    }
    mn_insert(row, i, f);
  }
}

/* a row of children (mrow, math, mstyle...): read into row; a row that is a fence ( ... ) becomes a
   delimiter */
static void row_children(Xml *x, MNode *row, Arg *info)
{
  Arg *a = (Arg *)malloc(64 * sizeof(Arg));     /* (on the heap: equations nest, the Amiga's stack is small) */
  int n, i, from = 0, to, delim = 0;
  MNode *inside;
  if (!a) { xml_skip(x); return; }
  n = args_read(x, a, 64);
  if (info && n == 1) { info->op = a[0].op; snprintf(info->word, sizeof info->word, "%s", a[0].word); }
  /* a fence: an opening and closing operator round the rest (or one of them, the other none) */
  to = n;
  {
    int op0 = n >= 2 && a[0].op && (a[0].op == '(' || a[0].op == '[' || a[0].op == '{' || a[0].op == '|' || a[0].op == 0x2016 || a[0].op == 0x27E8 ||
                                    a[0].op == 0x2329 || a[0].op == 0x230A || a[0].op == 0x2308 || a[0].op == 0x27E6 || !strcmp(a[0].name, "fence"));
    int op1 = n >= 2 && a[n - 1].op && (a[n - 1].op == ')' || a[n - 1].op == ']' || a[n - 1].op == '}' || a[n - 1].op == '|' || a[n - 1].op == 0x2016 ||
                                        a[n - 1].op == 0x27E9 || a[n - 1].op == 0x232A || a[n - 1].op == 0x230B || a[n - 1].op == 0x2309 || a[n - 1].op == 0x27E7 ||
                                        !strcmp(a[n - 1].name, "fence"));
    if (op0 && op1) { from = 1; to = n - 1; delim = 3; }
    else if (op1 && !strcmp(a[n - 1].name, "fence")) { from = 0; to = n - 1; delim = 2; }
    else if (op0 && !strcmp(a[0].name, "fence")) { from = 1; to = n; delim = 1; }
  }
  inside = delim ? mn_new(MK_ROW) : row;
  if (!inside) { args_free(a, n); free(a); return; }
  for (i = from; i < to; i++) {
    MNode *r = a[i].row;
    if (r && r->nkid == 1 && r->kid[0]->kind == MK_NARY && !r->kid[0]->kid[2]->nkid && i + 1 < to) {
      /* an n-ary operator's argument is what follows it */
      set_arg(r->kid[0], 2, a[i + 1].row); a[i + 1].row = NULL;
      row_append(inside, r); a[i].row = NULL;
      i++;
      continue;
    }
    if (r && i + 1 < to && a[i].op && is_nary(a[i].op) && r->nkid == 1 && r->kid[0]->kind == MK_RUN) {
      /* an n-ary operator with no limits: its argument is what follows */
      MNode *s = mn_struct(MK_NARY, 3, 0);
      if (s) {
        if (a[i].op != 0x222B) s->ch[0] = a[i].op;
        s->pv[P_N_SUBHIDE] = MB_ON; s->pv[P_N_SUPHIDE] = MB_ON;
        set_arg(s, 2, a[i + 1].row); a[i + 1].row = NULL;
        mn_insert(inside, -1, s);
      }
      mn_free(r); a[i].row = NULL;
      i++;
      continue;
    }
    if (r && i + 1 < to && a[i + 1].op != 0x2061 &&
        ((a[i].word[0] && is_funcword(a[i].word) && r->nkid == 1) ||
         (r->nkid == 1 && (r->kid[0]->kind == MK_LIMLOW || r->kid[0]->kind == MK_LIMUPP) && r->kid[0]->nkid && r->kid[0]->kid[0]->nkid == 1 &&
          r->kid[0]->kid[0]->kid[0]->kind == MK_RUN && r->kid[0]->kid[0]->kid[0]->text && is_funcword(r->kid[0]->kid[0]->kid[0]->text)))) {
      /* a function's name (sin, lim under its limit) and its argument */
      MNode *f = mn_struct(MK_FUNC, 2, 0);
      if (f) {
        set_arg(f, 0, r); a[i].row = NULL;
        set_arg(f, 1, a[i + 1].row); a[i + 1].row = NULL;
        mn_insert(inside, -1, f);
      }
      i++;
      continue;
    }
    if (r && i + 1 < to && a[i + 1].op == 0x2061) {
      /* function application: the name before it, the argument after */
      MNode *f = mn_struct(MK_FUNC, 2, 0);
      if (f) {
        set_arg(f, 0, r); a[i].row = NULL;
        if (i + 2 < to) { set_arg(f, 1, a[i + 2].row); a[i + 2].row = NULL; }
        mn_insert(inside, -1, f);
      }
      mn_free(a[i + 1].row); a[i + 1].row = NULL;
      i += 2;
      continue;
    }
    row_append(inside, r); a[i].row = NULL;
  }
  find_funcs(inside);
  if (delim) {
    u32 o = delim & 1 ? (a[0].op == 0xFFFF ? 0 : a[0].op) : 0, c = delim & 2 ? (a[n - 1].op == 0xFFFF ? 0 : a[n - 1].op) : 0;
    MNode *d = make_delim(o, c, inside, 0);
    if (d) mn_insert(row, -1, d);
    if (delim & 1) { mn_free(a[0].row); a[0].row = NULL; }
    if (delim & 2) { mn_free(a[n - 1].row); a[n - 1].row = NULL; }
    if (info) info->op = 0;
  }
  args_free(a, n);
  free(a);
}

static int is_funcword(const char *w)
{
  static const char *const words[] = { "sin", "cos", "tan", "cot", "sec", "csc", "sinh", "cosh", "tanh", "coth", "arcsin", "arccos", "arctan",
                                       "log", "ln", "lg", "exp", "lim", "max", "min", "det", "arg", "deg", "dim", "gcd", "hom", "inf", "ker",
                                       "sup", "Pr", "liminf", "limsup", NULL };
  int i;
  for (i = 0; words[i]; i++) if (!strcmp(w, words[i])) return 1;
  return 0;
}
static int is_limword(const char *w)
{
  static const char *const words[] = { "lim", "max", "min", "sup", "inf", "liminf", "limsup", "arg max", "arg min", NULL };
  int i;
  for (i = 0; words[i]; i++) if (!strcmp(w, words[i])) return 1;
  return 0;
}
static int is_accent(u32 c)
{
  return (c >= 0x300 && c <= 0x36F) || c == '^' || c == '~' || c == 0x2D9 || c == 0xA8 || c == 0x2C6 || c == 0x2DC || c == 0x2192 ||
         c == 0x20D7 || c == 0x2190 || c == 0x2194 || c == 0x20D6 || c == 0x20E1 || c == 0x2D8 || c == 0x2C7 || c == 0xB4 || c == '`' || c == 0x2032;
}
/* the combining accent OMML uses for a spacing one */
static u32 combining(u32 c)
{
  switch (c) {
  case '^': case 0x2C6: return 0x302;
  case '~': case 0x2DC: return 0x303;
  case 0x2D9: return 0x307;
  case 0xA8: return 0x308;
  case 0x2192: return 0x20D7;
  case 0x2190: return 0x20D6;
  case 0x2194: return 0x20E1;
  case 0x2D8: return 0x306;
  case 0x2C7: return 0x30C;
  case 0xB4: return 0x301;
  case '`': return 0x300;
  case 0xAF: case 0x203E: return 0x305;
  case 0x304: return 0x305;
  default: return c;
  }
}
static int is_overbar(u32 c) { return c == 0xAF || c == 0x203E || c == 0x305 || c == 0x2015 || c == '_' || c == 0x332 || c == 0x2212 || c == '-'; }
static int is_brace(u32 c) { return c == 0x23DE || c == 0x23DF || c == 0xFE37 || c == 0xFE38 || c == 0x23B4 || c == 0x23B5 || c == 0x23DC || c == 0x23DD; }

/* scripts on a base: n-ary operators, limits, accents, bars, group characters, else scripts */
static void scripts(MNode *row, Arg *a, int n, const char *el)
{
  int under = !strcmp(el, "munder") || !strcmp(el, "munderover"), over = !strcmp(el, "mover") || !strcmp(el, "munderover");
  int sub = !strcmp(el, "msub") || !strcmp(el, "msubsup"), sup = !strcmp(el, "msup") || !strcmp(el, "msubsup");
  int lo = -1, hi = -1;
  MNode *s = NULL;
  if (n >= 2 && !(sub && sup) && !(under && over) && a[1].row && !a[1].row->nkid) n = 1;      /* (an empty limit or script: the base alone) */
  if (n < 2) { int i; for (i = 0; i < n; i++) { row_append(row, a[i].row); a[i].row = NULL; } return; }
  if (under || sub) lo = 1;
  if (over && !under) hi = 1;
  if (over && under) hi = 2;
  if (sup && !sub) hi = 1;
  if (sup && sub) hi = 2;
  if (hi >= n) hi = -1;
  if (lo >= n) lo = -1;
  if (a[0].op && is_nary(a[0].op)) {
    s = mn_struct(MK_NARY, 3, 0);
    if (!s) return;
    if (a[0].op != 0x222B) s->ch[0] = a[0].op;
    s->pv[P_N_LIMLOC] = (u8)((under || over) ? MLIM_UNDOVR + 1 : MLIM_SUBSUP + 1);
    if (lo >= 0) { set_arg(s, 0, a[lo].row); a[lo].row = NULL; } else s->pv[P_N_SUBHIDE] = MB_ON;
    if (hi >= 0) { set_arg(s, 1, a[hi].row); a[hi].row = NULL; } else s->pv[P_N_SUPHIDE] = MB_ON;
    mn_free(a[0].row); a[0].row = NULL;
    mn_insert(row, -1, s);          /* (its argument follows it: row_children puts it in) */
    return;
  }
  if ((under || over) && a[0].word[0] && is_limword(a[0].word) && (under ^ over)) {
    s = mn_struct(under ? MK_LIMLOW : MK_LIMUPP, 2, 0);
    if (!s) return;
    set_arg(s, 0, a[0].row); set_arg(s, 1, a[1].row); a[0].row = a[1].row = NULL;
    mn_insert(row, -1, s);
    return;
  }
  if (over && !under && a[1].op && is_overbar(a[1].op)) {
    s = mn_struct(MK_BAR, 1, 0);
    if (!s) return;
    s->pv[P_BAR_POS] = MPOS_TOP + 1;
    set_arg(s, 0, a[0].row); a[0].row = NULL;
    mn_insert(row, -1, s);
    return;
  }
  if (under && !over && a[1].op && is_overbar(a[1].op)) {
    s = mn_struct(MK_BAR, 1, 0);
    if (!s) return;
    set_arg(s, 0, a[0].row); a[0].row = NULL;
    mn_insert(row, -1, s);
    return;
  }
  if ((over ^ under) && a[1].op && is_brace(a[1].op)) {
    s = mn_struct(MK_GROUPCHR, 1, 0);
    if (!s) return;
    if (a[1].op != 0x23DF) s->ch[0] = a[1].op;
    if (over) { s->pv[P_G_POS] = MPOS_TOP + 1; s->pv[P_G_VERTJC] = MPOS_BOT + 1; }
    set_arg(s, 0, a[0].row); a[0].row = NULL;
    mn_insert(row, -1, s);
    return;
  }
  if (over && !under && a[1].op && is_accent(a[1].op)) {
    u32 c = combining(a[1].op);
    s = mn_struct(MK_ACC, 1, 0);
    if (!s) return;
    if (c != 0x302) s->ch[0] = c;
    set_arg(s, 0, a[0].row); a[0].row = NULL;
    mn_insert(row, -1, s);
    return;
  }
  if (under || over) {
    /* a lower or upper limit (both: the upper round the lower) */
    MNode *in = NULL;
    if (under && lo >= 0) {
      in = mn_struct(MK_LIMLOW, 2, 0);
      if (!in) return;
      set_arg(in, 0, a[0].row); set_arg(in, 1, a[lo].row); a[0].row = a[lo].row = NULL;
    }
    if (over && hi >= 0) {
      s = mn_struct(MK_LIMUPP, 2, 0);
      if (!s) { mn_free(in); return; }
      if (in) mn_insert(s->kid[0], -1, in); else { set_arg(s, 0, a[0].row); a[0].row = NULL; }
      set_arg(s, 1, a[hi].row); a[hi].row = NULL;
      in = s;
    }
    if (in) mn_insert(row, -1, in);
    return;
  }
  s = mn_struct(sub && sup ? MK_SSUBSUP : sub ? MK_SSUB : MK_SSUP, 3, 0);
  if (!s) return;
  set_arg(s, 0, a[0].row); a[0].row = NULL;
  if (lo >= 0) { set_arg(s, 1, a[lo].row); a[lo].row = NULL; }
  if (hi >= 0) { set_arg(s, sub ? 2 : 1, a[hi].row); a[hi].row = NULL; }
  mn_insert(row, -1, s);
}

/* one element (x at its START) converted and added to row; info (may be NULL) learns what it was */
static void conv(Xml *x, MNode *row, Arg *info)
{
  char el[32];
  cpy(el, sizeof el, x->name);
  if (!strcmp(el, "mi") || !strcmp(el, "mn") || !strcmp(el, "mo") || !strcmp(el, "mtext") || !strcmp(el, "ms")) {
    const char *mv = xml_attr(x, "mathvariant"), *fence = xml_attr(x, "fence");
    int upright = 0, normal = !strcmp(el, "mtext") || !strcmp(el, "ms");
    Buf t;
    buf_init(&t);
    if (!strcmp(el, "mi") && mv && !strcmp(mv, "normal")) upright = 1;
    if (fence && !strcmp(fence, "true") && info) snprintf(info->name, sizeof info->name, "fence");
    token_text(x, &t);
    if (t.data && t.len) {
      int letters = 1;
      size_t k;
      for (k = 0; k < t.len; k++) if (!((t.data[k] >= 'a' && t.data[k] <= 'z') || (t.data[k] >= 'A' && t.data[k] <= 'Z'))) letters = 0;
      /* a word in an mi (sin, lim) is upright, as OMML's function names */
      if (!strcmp(el, "mi") && t.len > 1 && letters && !mv) upright = 1;
      if (info) {
        if (!strcmp(el, "mo") && one_cp(t.data)) info->op = first_cp(t.data);
        if (!strcmp(el, "mi") && letters && t.len < sizeof info->word) memcpy(info->word, t.data, t.len + 1);
      }
      /* a lone mi letter isn't joined to a word before it (the run would read as one word) */
      add_run(row, t.data, t.len, normal, upright, !normal && !upright);
    } else if (info && !strcmp(el, "mo")) info->op = 0xFFFF;      /* (an empty fence: none) */
    buf_free(&t);
    return;
  }
  if (!strcmp(el, "mspace") || !strcmp(el, "annotation") || !strcmp(el, "annotation-xml") || !strcmp(el, "malignmark") || !strcmp(el, "maligngroup") ||
      !strcmp(el, "mprescripts") || !strcmp(el, "none")) {
    if (!x->pending_end) xml_skip(x); else xml_next(x);
    return;
  }
  if (!strcmp(el, "semantics")) {
    /* its first child is the presentation; annotations follow */
    int d0 = x->depth, first = 1;
    if (x->pending_end) { xml_next(x); return; }
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (first && strcmp(x->name, "annotation") && strcmp(x->name, "annotation-xml")) { conv(x, row, info); first = 0; }
      else xml_skip(x);
    }
    return;
  }
  if (!strcmp(el, "mfrac")) {
    Arg a[2];
    const char *bev = xml_attr(x, "bevelled"), *lt = xml_attr(x, "linethickness"), *qk = xml_attr(x, "kind");
    int ty = qk && !strcmp(qk, "lin") ? MF_LIN + 1 : bev && !strcmp(bev, "true") ? MF_SKW + 1 : lt && (!strcmp(lt, "0") || !strcmp(lt, "0pt") || !strcmp(lt, "0px") || !strcmp(lt, "0em")) ? MF_NOBAR + 1 : 0;
    int n = args_read(x, a, 2);
    MNode *f = mn_struct(MK_FRAC, 2, 0);
    if (f) {
      f->pv[P_F_TYPE] = (u8)ty;
      if (n > 0) { set_arg(f, 0, a[0].row); a[0].row = NULL; }
      if (n > 1) { set_arg(f, 1, a[1].row); a[1].row = NULL; }
      mn_insert(row, -1, f);
    }
    args_free(a, n);
    return;
  }
  if (!strcmp(el, "msqrt") || !strcmp(el, "mroot")) {
    MNode *r = mn_struct(MK_RAD, 2, 0);
    if (!r) { xml_skip(x); return; }
    if (!strcmp(el, "msqrt")) {
      MNode *in = mn_new(MK_ROW);
      if (in) { row_children(x, in, NULL); set_arg(r, 1, in); } else xml_skip(x);
      r->pv[P_RAD_DEGHIDE] = MB_ON;
    } else {
      Arg a[2];
      int n = args_read(x, a, 2);
      if (n > 0) { set_arg(r, 1, a[0].row); a[0].row = NULL; }
      if (n > 1) { set_arg(r, 0, a[1].row); a[1].row = NULL; }
      args_free(a, n);
    }
    mn_insert(row, -1, r);
    return;
  }
  if (!strcmp(el, "msub") || !strcmp(el, "msup") || !strcmp(el, "msubsup") || !strcmp(el, "munder") || !strcmp(el, "mover") || !strcmp(el, "munderover")) {
    Arg a[3];
    int n = args_read(x, a, 3);
    scripts(row, a, n, el);
    args_free(a, n);
    return;
  }
  if (!strcmp(el, "mmultiscripts")) {
    /* base, then post sub/sup pairs, then mprescripts and pre pairs: prescripts make an sPre */
    Arg a[8];
    int d0 = x->depth, n = 0, pre = -1;
    if (!x->pending_end) while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "mprescripts")) { pre = n; if (!x->pending_end) xml_skip(x); else xml_next(x); continue; }
      if (n < 8) arg_read(x, &a[n++]); else xml_skip(x);
    } else xml_next(x);
    if (pre >= 1 && pre + 2 <= n) {
      MNode *s = mn_struct(MK_SPRE, 3, 0);
      if (s) {
        set_arg(s, 0, a[pre].row); set_arg(s, 1, a[pre + 1].row); set_arg(s, 2, a[0].row);
        a[pre].row = a[pre + 1].row = a[0].row = NULL;
        mn_insert(row, -1, s);
      }
    } else if (n >= 3) {
      Arg b[3];
      b[0] = a[0]; b[1] = a[1]; b[2] = a[2];
      a[0].row = a[1].row = a[2].row = NULL;
      if (b[1].row && !b[1].row->nkid) scripts(row, (b[1] = b[2], b), 2, "msup");
      else if (b[2].row && !b[2].row->nkid) scripts(row, b, 2, "msub");
      else scripts(row, b, 3, "msubsup");
      args_free(b, 3);
    } else if (n >= 1) { row_append(row, a[0].row); a[0].row = NULL; }
    args_free(a, n);
    return;
  }
  if (!strcmp(el, "mfenced")) {
    const char *op = xml_attr(x, "open"), *cl = xml_attr(x, "close"), *sp = xml_attr(x, "separators");
    u32 o = op ? first_cp(op) : '(', c = cl ? first_cp(cl) : ')', s = sp ? first_cp(sp) : ',';
    Arg *a = (Arg *)malloc(32 * sizeof(Arg));
    int n, i;
    MNode *d;
    if (!a) { xml_skip(x); return; }
    n = args_read(x, a, 32);
    d = mn_struct(MK_DELIM, n > 0 ? n : 1, 0);
    if (d) {
      d->ch[0] = o == '(' ? MCH_UNSET : o; d->ch[2] = c == ')' ? MCH_UNSET : c;
      if (n > 1) d->ch[1] = s == '|' ? MCH_UNSET : s;
      for (i = 0; i < n; i++) { set_arg(d, i, a[i].row); a[i].row = NULL; }
      mn_insert(row, -1, d);
    }
    args_free(a, n);
    free(a);
    return;
  }
  if (!strcmp(el, "mtable")) {
    /* rows (mtr, mlabeledtr) of cells (mtd) */
    const char *qk = xml_attr(x, "kind");
    int eqarr = qk && !strcmp(qk, "eqarr") ? 1 : qk && !strcmp(qk, "lines") ? 2 : 0;
    MNode **cells = (MNode **)malloc(256 * sizeof(MNode *));
    int nc = 0, rows = 0, cols = 0, d0 = x->depth, i;
    int rowstart[64];
    if (!cells) { xml_skip(x); return; }
    if (!x->pending_end) while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "mtr") || !strcmp(x->name, "mlabeledtr")) {
        int d1 = x->depth, c = 0;
        if (rows >= 64) { xml_skip(x); continue; }
        rowstart[rows++] = nc;
        if (!x->pending_end) while (xml_next(x) != XT_EOF) {
          if (x->type == XT_END && x->depth < d1) break;
          if (x->type != XT_START) continue;
          if (nc < 256) { MNode *cell = mn_new(MK_ROW); if (cell) { row_children(x, cell, NULL); cells[nc++] = cell; c++; } else xml_skip(x); }
          else xml_skip(x);
        } else xml_next(x);
        if (c > cols) cols = c;
      } else xml_skip(x);
    } else xml_next(x);
    if (rows && eqarr) {
      /* (Quill's own: an equation array) */
      MNode *m = mn_struct(MK_EQARR, rows, 0);
      int rr2;
      if (m) {
        if (eqarr == 2) m->pv[7] = 1;         /* (a display equation's lines: mathml_read splits them) */
        for (rr2 = 0; rr2 < rows; rr2++) if (rowstart[rr2] < nc) { set_arg(m, rr2, cells[rowstart[rr2]]); cells[rowstart[rr2]] = NULL; }
        mn_insert(row, -1, m);
      }
    } else if (rows && cols) {
      MNode *m = mn_struct(MK_MATRIX, rows, cols);
      int r;
      if (m) {
        for (r = 0; r < rows; r++) {
          int a = rowstart[r], b = r + 1 < rows ? rowstart[r + 1] : nc, c;
          for (c = 0; c < b - a && c < cols; c++) { set_arg(m, r * cols + c, cells[a + c]); cells[a + c] = NULL; }
        }
        mn_insert(row, -1, m);
      }
    }
    for (i = 0; i < nc; i++) mn_free(cells[i]);
    free(cells);
    return;
  }
  if (!strcmp(el, "menclose") || !strcmp(el, "mphantom")) {
    MNode *s = mn_struct(el[1] == 'e' ? MK_BORDERBOX : MK_PHANT, 1, 0), *in = mn_new(MK_ROW);
    if (!s || !in) { mn_free(s); mn_free(in); xml_skip(x); return; }
    row_children(x, in, NULL);
    set_arg(s, 0, in);
    mn_insert(row, -1, s);
    return;
  }
  if (!strcmp(el, "mrow")) {
    const char *qk = xml_attr(x, "kind");
    if (qk && !strcmp(qk, "box")) {
      /* (Quill's own: a box) */
      MNode *s = mn_struct(MK_BOX, 1, 0), *in = mn_new(MK_ROW);
      if (!s || !in) { mn_free(s); mn_free(in); xml_skip(x); return; }
      row_children(x, in, NULL);
      set_arg(s, 0, in);
      mn_insert(row, -1, s);
      return;
    }
  }
  /* math, mrow, mstyle, mpadded, merror, maction and anything else: its children */
  row_children(x, row, info);
}

MathEq *mathml_read(Xml *x)
{
  const char *disp = xml_attr(x, "display");
  MathEq *e = math_new(!disp || strcmp(disp, "inline"));
  if (!e) { xml_skip(x); return NULL; }
  row_children(x, e->line[0], NULL);
  {
    /* (Quill's own: the lines of a display equation, as an mtable of kind "lines") */
    MNode *r0 = e->line[0];
    if (r0->nkid == 1 && r0->kid[0]->kind == MK_EQARR && r0->kid[0]->pv[7] == 1 && r0->kid[0]->nkid > 1) {
      MNode *arr = r0->kid[0];
      MNode **nl = (MNode **)malloc(arr->nkid * sizeof(MNode *));
      if (nl) {
        u16 k;
        for (k = 0; k < arr->nkid; k++) nl[k] = arr->kid[k];
        e->nline = arr->nkid;
        arr->nkid = 0;
        mn_free(r0);
        free(e->line);
        e->line = nl;
      }
    }
  }
  return e;
}

MathEq *mathml_parse(const char *xml, size_t n)
{
  Xml x;
  MathEq *e = NULL;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) if (x.type == XT_START && !strcmp(x.name, "math")) { e = mathml_read(&x); break; }
  xml_free(&x);
  return e;
}

/* ------------------------------------------------------------------ */
/* writing */

static void w_row(Buf *o, const MNode *r);
static void w_cp(Buf *o, u32 c)
{
  char u[8];
  int n;
  if (c == '<') { buf_adds(o, "&lt;"); return; }
  if (c == '>') { buf_adds(o, "&gt;"); return; }
  if (c == '&') { buf_adds(o, "&amp;"); return; }
  n = utf8_put(u, c);
  buf_add(o, u, (size_t)n);
}
static void w_mo(Buf *o, u32 c, const char *attrs)
{
  buf_printf(o, "<mo%s>", attrs ? attrs : "");
  if (c) w_cp(o, c);
  buf_adds(o, "</mo>");
}
static int is_letter(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c < 0x2000 && c != 0xD7 && c != 0xF7) || (c >= 0x1D400 && c <= 0x1D7FF) || c == 0x221E; }
static int is_digit(u32 c) { return (c >= '0' && c <= '9') || c == '.'; }
/* a run as tokens: letters mi, numbers mn, the rest mo; upright words one mi; normal text mtext */
static void w_run(Buf *o, const MNode *r)
{
  const char *t = r->text ? r->text : "";
  size_t n = strlen(t), i = 0;
  if (r->pv[P_R_NOR] == MB_ON) {
    buf_adds(o, "<mtext>"); xml_escape(o, t, n); buf_adds(o, "</mtext>");
    return;
  }
  if (r->pv[P_R_STY] == MSTY_P + 1) {
    buf_adds(o, "<mi mathvariant=\"normal\">"); xml_escape(o, t, n); buf_adds(o, "</mi>");
    return;
  }
  while (i < n) {
    int used;
    u32 c = utf8_get(t + i, n - i, &used);
    if (is_digit(c) && c != '.') {
      size_t j = i;
      buf_adds(o, "<mn>");
      while (j < n && is_digit((u32)(u8)t[j])) j++;
      buf_add(o, t + i, j - i);
      buf_adds(o, "</mn>");
      i = j;
      continue;
    }
    if (c == ' ') { i += (size_t)used; continue; }
    if (is_letter(c)) { buf_adds(o, "<mi>"); w_cp(o, c); buf_adds(o, "</mi>"); }
    else w_mo(o, c, NULL);
    i += (size_t)used;
  }
}
/* the spacing accent MathML programs draw for a combining one OMML keeps */
static u32 spacing(u32 c)
{
  switch (c) {
  case 0x302: return '^';
  case 0x303: return '~';
  case 0x307: return 0x2D9;
  case 0x308: return 0xA8;
  case 0x304: case 0x305: return 0xAF;
  case 0x300: return '`';
  case 0x301: return 0xB4;
  case 0x306: return 0x2D8;
  case 0x30C: return 0x2C7;
  default: return c;
  }
}
static void w_arg(Buf *o, const MNode *s, int k)
{
  if (k < s->nkid) w_row(o, s->kid[k]); else buf_adds(o, "<mrow/>");
}
static u32 ch_or(const MNode *s, int k, u32 def) { return s->ch[k] == MCH_UNSET ? def : s->ch[k]; }
static void w_node(Buf *o, const MNode *n)
{
  int k;
  switch (n->kind) {
  case MK_ROW: w_row(o, n); return;
  case MK_RUN: w_run(o, n); return;
  case MK_FRAC:
    if (n->pv[P_F_TYPE] == MF_SKW + 1) buf_adds(o, "<mfrac bevelled=\"true\">");
    else if (n->pv[P_F_TYPE] == MF_LIN + 1) buf_adds(o, "<mfrac bevelled=\"true\" quill:kind=\"lin\">");
    else if (n->pv[P_F_TYPE] == MF_NOBAR + 1) buf_adds(o, "<mfrac linethickness=\"0\">");
    else buf_adds(o, "<mfrac>");
    w_arg(o, n, 0); w_arg(o, n, 1);
    buf_adds(o, "</mfrac>");
    return;
  case MK_SSUB: buf_adds(o, "<msub>"); w_arg(o, n, 0); w_arg(o, n, 1); buf_adds(o, "</msub>"); return;
  case MK_SSUP: buf_adds(o, "<msup>"); w_arg(o, n, 0); w_arg(o, n, 1); buf_adds(o, "</msup>"); return;
  case MK_SSUBSUP: buf_adds(o, "<msubsup>"); w_arg(o, n, 0); w_arg(o, n, 1); w_arg(o, n, 2); buf_adds(o, "</msubsup>"); return;
  case MK_SPRE: buf_adds(o, "<mmultiscripts>"); w_arg(o, n, 2); buf_adds(o, "<mprescripts/>"); w_arg(o, n, 0); w_arg(o, n, 1); buf_adds(o, "</mmultiscripts>"); return;
  case MK_RAD:
    if (n->pv[P_RAD_DEGHIDE] == MB_ON || n->nkid < 1 || !n->kid[0]->nkid) { buf_adds(o, "<msqrt>"); w_arg(o, n, 1); buf_adds(o, "</msqrt>"); }
    else { buf_adds(o, "<mroot>"); w_arg(o, n, 1); w_arg(o, n, 0); buf_adds(o, "</mroot>"); }
    return;
  case MK_NARY: {
    int undovr = n->pv[P_N_LIMLOC] == MLIM_UNDOVR + 1 || (!n->pv[P_N_LIMLOC] && ch_or(n, 0, 0x222B) != 0x222B && is_nary(ch_or(n, 0, 0x222B)) &&
                                                          !(ch_or(n, 0, 0) >= 0x222B && ch_or(n, 0, 0) <= 0x2233));
    int sub = n->pv[P_N_SUBHIDE] != MB_ON && n->nkid > 0 && n->kid[0]->nkid, sup = n->pv[P_N_SUPHIDE] != MB_ON && n->nkid > 1 && n->kid[1]->nkid;
    const char *el = sub && sup ? (undovr ? "munderover" : "msubsup") : sub ? (undovr ? "munder" : "msub") : sup ? (undovr ? "mover" : "msup") : NULL;
    buf_adds(o, "<mrow>");
    if (el) buf_printf(o, "<%s>", el);
    w_mo(o, ch_or(n, 0, 0x222B), " stretchy=\"false\"");
    if (sub) w_arg(o, n, 0);
    if (sup) w_arg(o, n, 1);
    if (el) buf_printf(o, "</%s>", el);
    w_arg(o, n, 2);
    buf_adds(o, "</mrow>");
    return;
  }
  case MK_DELIM:
    buf_adds(o, "<mrow>");
    if (ch_or(n, 0, '(')) w_mo(o, ch_or(n, 0, '('), " fence=\"true\" form=\"prefix\" stretchy=\"true\"");
    else buf_adds(o, "<mo fence=\"true\" form=\"prefix\"></mo>");
    for (k = 0; k < n->nkid; k++) {
      if (k) w_mo(o, ch_or(n, 1, '|'), " separator=\"true\"");
      w_row(o, n->kid[k]);
    }
    if (ch_or(n, 2, ')')) w_mo(o, ch_or(n, 2, ')'), " fence=\"true\" form=\"postfix\" stretchy=\"true\"");
    else buf_adds(o, "<mo fence=\"true\" form=\"postfix\"></mo>");
    buf_adds(o, "</mrow>");
    return;
  case MK_FUNC:
    buf_adds(o, "<mrow>"); w_arg(o, n, 0); buf_adds(o, "<mo>\xE2\x81\xA1</mo>"); w_arg(o, n, 1); buf_adds(o, "</mrow>");
    return;
  case MK_ACC:
    buf_adds(o, "<mover accent=\"true\">"); w_arg(o, n, 0); w_mo(o, spacing(ch_or(n, 0, 0x302)), " stretchy=\"true\""); buf_adds(o, "</mover>");
    return;
  case MK_BAR:
    if (n->pv[P_BAR_POS] == MPOS_TOP + 1) { buf_adds(o, "<mover accent=\"true\">"); w_arg(o, n, 0); w_mo(o, 0xAF, NULL); buf_adds(o, "</mover>"); }
    else { buf_adds(o, "<munder accentunder=\"true\">"); w_arg(o, n, 0); w_mo(o, 0x332, NULL); buf_adds(o, "</munder>"); }
    return;
  case MK_GROUPCHR: {
    int top = n->pv[P_G_POS] == MPOS_TOP + 1;
    buf_adds(o, top ? "<mover>" : "<munder>");
    w_arg(o, n, 0);
    w_mo(o, ch_or(n, 0, 0x23DF), " stretchy=\"true\"");
    buf_adds(o, top ? "</mover>" : "</munder>");
    return;
  }
  case MK_LIMLOW: buf_adds(o, "<munder>"); w_arg(o, n, 0); w_arg(o, n, 1); buf_adds(o, "</munder>"); return;
  case MK_LIMUPP: buf_adds(o, "<mover>"); w_arg(o, n, 0); w_arg(o, n, 1); buf_adds(o, "</mover>"); return;
  case MK_MATRIX: case MK_EQARR: {
    int rows = n->kind == MK_MATRIX ? n->rows : n->nkid, cols = n->kind == MK_MATRIX ? n->cols : 1, r, c;
    buf_adds(o, n->kind == MK_EQARR ? "<mtable columnalign=\"left\" quill:kind=\"eqarr\">" : "<mtable>");
    for (r = 0; r < rows; r++) {
      buf_adds(o, "<mtr>");
      for (c = 0; c < cols; c++) { buf_adds(o, "<mtd>"); w_arg(o, n, r * cols + c); buf_adds(o, "</mtd>"); }
      buf_adds(o, "</mtr>");
    }
    buf_adds(o, "</mtable>");
    return;
  }
  case MK_BORDERBOX: buf_adds(o, "<menclose notation=\"box\">"); w_arg(o, n, 0); buf_adds(o, "</menclose>"); return;
  case MK_PHANT: buf_adds(o, "<mphantom>"); w_arg(o, n, 0); buf_adds(o, "</mphantom>"); return;
  case MK_BOX: buf_adds(o, "<mrow quill:kind=\"box\">"); w_arg(o, n, 0); buf_adds(o, "</mrow>"); return;
  default: return;          /* (MK_RAW: OMML Quill keeps verbatim has no MathML) */
  }
}
static void w_row(Buf *o, const MNode *r)
{
  u16 i;
  buf_adds(o, "<mrow>");
  for (i = 0; i < r->nkid; i++) w_node(o, r->kid[i]);
  buf_adds(o, "</mrow>");
}

void mathml_write(Buf *o, const MathEq *e)
{
  u16 i;
  buf_printf(o, "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" xmlns:quill=\"urn:quill:odt:1\" display=\"%s\"><semantics>", e->display ? "block" : "inline");
  if (e->nline == 1) w_row(o, e->line[0]);
  else {
    buf_adds(o, "<mtable columnalign=\"left\" quill:kind=\"lines\">");
    for (i = 0; i < e->nline; i++) { buf_adds(o, "<mtr><mtd>"); w_row(o, e->line[i]); buf_adds(o, "</mtd></mtr>"); }
    buf_adds(o, "</mtable>");
  }
  buf_adds(o, "</semantics></math>");
}
