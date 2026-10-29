/* mathedit.c - see mathedit.h: editing equations in place (WP14b). The navigation and deletion rules
   follow WordCraft's crates/doc/src/math_edit.rs (MIT OR Apache-2.0, (c) the WordCraft authors),
   written again in C for Quill's tree (math.h) */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mathedit.h"

/* ------------------------------------------------------------------ */
/* units */
static int cps(const char *s)
{
  int n = 0;
  if (!s) return 0;
  for (; *s; s++) if (((u8)*s & 0xC0) != 0x80) n++;
  return n;
}
/* the byte where code point k of s starts */
static size_t byte_at(const char *s, int k)
{
  size_t i = 0;
  if (!s) return 0;
  while (s[i] && k > 0) { i++; while (s[i] && ((u8)s[i] & 0xC0) == 0x80) i++; k--; }
  return i;
}
static int is_struct(const MNode *n) { return n->kind != MK_RUN && n->kind != MK_ROW; }
static int node_units(const MNode *n) { return n->kind == MK_RUN ? cps(n->text) : 1; }
int mp_units(const MNode *row)
{
  int u = 0;
  u16 i;
  if (!row) return 0;
  for (i = 0; i < row->nkid; i++) u += node_units(row->kid[i]);
  return u;
}
static int node_start(const MNode *row, int i)
{
  int u = 0, k;
  for (k = 0; k < i && k < row->nkid; k++) u += node_units(row->kid[k]);
  return u;
}
/* where unit off of a row falls: in node *i at character *k (a run), or before node *i (*k 0; *i may
   be nkid) */
static void locate(const MNode *row, int off, int *i, int *k)
{
  int s = 0;
  u16 n;
  for (n = 0; n < row->nkid; n++) {
    int u = node_units(row->kid[n]);
    if (off < s + u) { *i = n; *k = row->kid[n]->kind == MK_RUN ? off - s : 0; return; }
    s += u;
  }
  *i = row->nkid; *k = 0;
}
/* the structure a step names: at unit off of row, a structure (not a run); -1 if none */
static int struct_at(const MNode *row, int off)
{
  int i, k;
  locate(row, off, &i, &k);
  if (k || i >= row->nkid || !is_struct(row->kid[i])) return -1;
  return i;
}
static int row_empty(const MNode *r)
{
  u16 i;
  if (!r) return 1;
  for (i = 0; i < r->nkid; i++) if (r->kid[i]->kind != MK_RUN || (r->kid[i]->text && r->kid[i]->text[0])) return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* arguments in reading order */
int mn_nav(const MNode *n, int *out, int max)
{
  int c = 0, i;
#define ADD(k) do { if ((k) < n->nkid && c < max) out[c++] = (k); } while (0)
  switch (n->kind) {
  case MK_FRAC: case MK_SSUB: case MK_SSUP: case MK_FUNC: case MK_LIMLOW: case MK_LIMUPP: ADD(0); ADD(1); break;
  case MK_SSUBSUP: case MK_SPRE: ADD(0); ADD(1); ADD(2); break;
  case MK_RAD: if (n->pv[P_RAD_DEGHIDE] != MB_ON) ADD(0); ADD(1); break;
  case MK_NARY: if (n->pv[P_N_SUBHIDE] != MB_ON) ADD(0); if (n->pv[P_N_SUPHIDE] != MB_ON) ADD(1); ADD(2); break;
  case MK_DELIM: case MK_EQARR: case MK_MATRIX: for (i = 0; i < n->nkid; i++) ADD(i); break;
  case MK_ACC: case MK_BAR: case MK_BOX: case MK_BORDERBOX: case MK_GROUPCHR: case MK_PHANT: ADD(0); break;
  default: break;
  }
#undef ADD
  return c;
}
int mn_main_arg(const MNode *n)
{
  switch (n->kind) {
  case MK_SPRE: case MK_NARY: return 2;
  case MK_RAD: case MK_FUNC: return 1;
  default: return 0;
  }
}
static int nav_pos(const MNode *n, int arg, int *nav, int *nn)
{
  int i;
  *nn = mn_nav(n, nav, 64);
  for (i = 0; i < *nn; i++) if (nav[i] == arg) return i;
  return -1;
}
static int all_empty(const MNode *n)
{
  int nav[64], nn = mn_nav(n, nav, 64), i;
  for (i = 0; i < nn; i++) if (!row_empty(n->kid[nav[i]])) return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* places */
/* the row at depth d of a place's path (d <= depth); NULL if the path breaks before */
static MNode *row_at_depth(const MathEq *e, const MPos *p, int d)
{
  MNode *row;
  int k;
  if (!e || p->line >= e->nline) return NULL;
  row = e->line[p->line];
  for (k = 0; k < d; k++) {
    int i = struct_at(row, p->node[k]);
    MNode *n;
    if (i < 0) return NULL;
    n = row->kid[i];
    if (p->arg[k] >= n->nkid || n->kind == MK_RAW) return NULL;
    row = n->kid[p->arg[k]];
    if (!row || row->kind != MK_ROW) return NULL;
  }
  return row;
}
MNode *mp_row(const MathEq *e, const MPos *p) { return p->depth > MP_DEPTH ? NULL : row_at_depth(e, p, p->depth); }
/* the structure step d goes through */
static MNode *step_node(const MathEq *e, const MPos *p, int d)
{
  MNode *row = row_at_depth(e, p, d);
  int i = row ? struct_at(row, p->node[d]) : -1;
  return i < 0 ? NULL : row->kid[i];
}
void mp_clamp(const MathEq *e, MPos *p)
{
  MNode *row;
  int d, u;
  if (!e || !e->nline) { memset(p, 0, sizeof *p); return; }
  if (p->line >= e->nline) { p->line = (u16)(e->nline - 1); p->depth = 0; p->off = (u16)mp_units(e->line[p->line]); return; }
  if (p->depth > MP_DEPTH) p->depth = MP_DEPTH;
  for (d = 0; d < p->depth; d++) {
    MNode *r = row_at_depth(e, p, d), *n;
    int i = r ? struct_at(r, p->node[d]) : -1;
    if (i < 0) break;
    n = r->kid[i];
    if (p->arg[d] >= n->nkid || n->kind == MK_RAW) break;
    {
      int nav[64], nn, j = nav_pos(n, p->arg[d], nav, &nn);
      if (j < 0) break;                     /* (a hidden argument) */
    }
  }
  if (d < p->depth) { p->off = p->node[d]; p->depth = (u16)d; }
  row = mp_row(e, p);
  u = mp_units(row);
  if (p->off > u) p->off = (u16)u;
}
void mp_start(const MathEq *e, MPos *p) { memset(p, 0, sizeof *p); (void)e; }
void mp_end(const MathEq *e, MPos *p)
{
  memset(p, 0, sizeof *p);
  if (e && e->nline) { p->line = (u16)(e->nline - 1); p->off = (u16)mp_units(e->line[p->line]); }
}
int mp_same_row(const MPos *a, const MPos *b)
{
  int d;
  if (a->line != b->line || a->depth != b->depth) return 0;
  for (d = 0; d < a->depth; d++) if (a->node[d] != b->node[d] || a->arg[d] != b->arg[d]) return 0;
  return 1;
}
int mp_equal(const MPos *a, const MPos *b) { return mp_same_row(a, b) && a->off == b->off; }
static void push(MPos *p, int at, int arg, int off)
{
  if (p->depth >= MP_DEPTH) return;
  p->node[p->depth] = (u16)at; p->arg[p->depth] = (u16)arg; p->depth++;
  p->off = (u16)off;
}

/* ------------------------------------------------------------------ */
/* moving */
int me_right(const MathEq *e, MPos *p)
{
  MNode *row, *parent, *n;
  int i, k, nav[64], nn, j;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return 0;
  if (p->off < mp_units(row)) {
    locate(row, p->off, &i, &k);
    n = row->kid[i];
    if (n->kind != MK_RUN && n->kind != MK_RAW && (nn = mn_nav(n, nav, 64)) > 0 && p->depth < MP_DEPTH) { push(p, p->off, nav[0], 0); return 1; }
    p->off++;
    return 1;
  }
  if (!p->depth) {
    if (p->line + 1 < e->nline) { p->line++; p->off = 0; return 1; }
    return 0;
  }
  parent = row_at_depth(e, p, p->depth - 1);
  n = step_node(e, p, p->depth - 1);
  if (!parent || !n) return 0;
  j = nav_pos(n, p->arg[p->depth - 1], nav, &nn);
  if (j >= 0 && j + 1 < nn) { p->arg[p->depth - 1] = (u16)nav[j + 1]; p->off = 0; return 1; }
  p->depth--;
  p->off = (u16)(p->node[p->depth] + 1);
  return 1;
}
int me_left(const MathEq *e, MPos *p)
{
  MNode *row, *n;
  int i, k, nav[64], nn, j;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return 0;
  if (p->off > 0) {
    locate(row, p->off - 1, &i, &k);
    n = row->kid[i];
    if (n->kind != MK_RUN && n->kind != MK_RAW && (nn = mn_nav(n, nav, 64)) > 0 && p->depth < MP_DEPTH) {
      push(p, p->off - 1, nav[nn - 1], mp_units(n->kid[nav[nn - 1]]));
      return 1;
    }
    p->off--;
    return 1;
  }
  if (!p->depth) {
    if (p->line > 0) { p->line--; p->off = (u16)mp_units(e->line[p->line]); return 1; }
    return 0;
  }
  n = step_node(e, p, p->depth - 1);
  if (!n) return 0;
  j = nav_pos(n, p->arg[p->depth - 1], nav, &nn);
  if (j > 0) { p->arg[p->depth - 1] = (u16)nav[j - 1]; p->off = (u16)mp_units(n->kid[nav[j - 1]]); return 1; }
  p->depth--;
  p->off = p->node[p->depth];
  return 1;
}
/* the argument to go to from argument c of n, up or down; -1 none */
static int vertical_target(const MNode *n, int c, int up)
{
  switch (n->kind) {
  case MK_FRAC: return up && c == 1 ? 0 : !up && c == 0 ? 1 : -1;
  case MK_SSUP: return !up && c == 1 ? 0 : up && c == 0 ? 1 : -1;
  case MK_SSUB: return up && c == 1 ? 0 : !up && c == 0 ? 1 : -1;
  case MK_SSUBSUP:
    if (up) return c == 1 ? 2 : c == 0 ? 2 : -1;
    return c == 2 ? 1 : c == 0 ? 1 : -1;
  case MK_SPRE:
    if (up) return c == 0 ? 1 : c == 2 ? 1 : -1;
    return c == 1 ? 0 : c == 2 ? 0 : -1;
  case MK_NARY:
    if (up) return c == 0 && n->pv[P_N_SUPHIDE] != MB_ON ? 1 : c == 2 && n->pv[P_N_SUPHIDE] != MB_ON ? 1 : -1;
    return c == 1 && n->pv[P_N_SUBHIDE] != MB_ON ? 0 : c == 2 && n->pv[P_N_SUBHIDE] != MB_ON ? 0 : -1;
  case MK_LIMLOW: return (!up && c == 0) ? 1 : (up && c == 1) ? 0 : -1;
  case MK_LIMUPP: return (up && c == 0) ? 1 : (!up && c == 1) ? 0 : -1;
  case MK_RAD: return up && c == 1 && n->pv[P_RAD_DEGHIDE] != MB_ON ? 0 : !up && c == 0 ? 1 : -1;
  case MK_EQARR: return up ? (c > 0 ? c - 1 : -1) : (c + 1 < n->nkid ? c + 1 : -1);
  case MK_MATRIX: {
    int cols = n->cols ? n->cols : 1, r = c / cols, col = c % cols;
    if (up) return r > 0 ? (r - 1) * cols + col : -1;
    return (r + 1) * cols + col < n->nkid ? (r + 1) * cols + col : -1;
  }
  }
  return -1;
}
/* x (twips, from the equation's origin) of unit off in a row; and the row's origin */
static void row_origin(const MathEq *e, const MPos *p, int depth, i32 *x, i32 *y);
static i32 off_x(const MNode *row, int off);
static int off_near(const MNode *row, i32 rx, i32 x)
{
  int u = mp_units(row), best = 0, k;
  i32 bd = 0x7FFFFFFF;
  for (k = 0; k <= u; k++) {
    i32 d = off_x(row, k) + rx - x;
    if (d < 0) d = -d;
    if (d < bd) { bd = d; best = k; }
  }
  return best;
}
int me_vertical(const MathEq *e, MPos *p, int up)
{
  int level;
  i32 cx, ry, rx;
  MNode *row;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return 0;
  row_origin(e, p, p->depth, &rx, &ry);
  cx = rx + off_x(row, p->off);
  for (level = p->depth - 1; level >= 0; level--) {
    MNode *n = step_node(e, p, level);
    int t = n ? vertical_target(n, p->arg[level], up) : -1;
    if (t >= 0) {
      MPos q = *p;
      q.depth = (u16)(level + 1); q.arg[level] = (u16)t; q.off = 0;
      row = mp_row(e, &q);
      row_origin(e, &q, q.depth, &rx, &ry);
      q.off = (u16)off_near(row, rx, cx);
      *p = q;
      return 1;
    }
  }
  if (up ? p->line > 0 : p->line + 1 < e->nline) {
    MPos q;
    memset(&q, 0, sizeof q);
    q.line = (u16)(p->line + (up ? -1 : 1));
    row_origin(e, &q, 0, &rx, &ry);
    q.off = (u16)off_near(e->line[q.line], rx, cx);
    *p = q;
    return 1;
  }
  return 0;
}
void me_home(const MathEq *e, MPos *p, int end)
{
  MNode *row;
  int target;
  mp_clamp(e, p);
  row = mp_row(e, p);
  target = end ? mp_units(row) : 0;
  if (p->off != target || !p->depth) { p->off = (u16)target; return; }
  if (end) mp_end(e, p); else mp_start(e, p);
}
/* every argument's place in reading order, depth first */
typedef struct { MPos *v; int n, cap; } PList;
static void collect(const MathEq *e, MPos *at, const MNode *row, PList *l)
{
  u16 i;
  int u = 0;
  if (l->n < l->cap) l->v[l->n++] = *at;
  for (i = 0; i < row->nkid; i++) {
    const MNode *n = row->kid[i];
    if (is_struct(n) && n->kind != MK_RAW && at->depth < MP_DEPTH) {
      int nav[64], nn = mn_nav(n, nav, 64), j;
      for (j = 0; j < nn; j++) {
        MPos q = *at;
        push(&q, u, nav[j], 0);
        collect(e, &q, n->kid[nav[j]], l);
      }
    }
    u += node_units(n);
  }
}
int me_next_slot(const MathEq *e, MPos *p, int back)
{
  PList l;
  int i, cur = -1, pick = -1;
  u16 ln;
  l.cap = 2000; l.n = 0;
  if (!(l.v = (MPos *)malloc(l.cap * sizeof(MPos)))) return 0;
  for (ln = 0; ln < e->nline; ln++) { MPos q; memset(&q, 0, sizeof q); q.line = ln; collect(e, &q, e->line[ln], &l); }
  for (i = 0; i < l.n; i++) if (mp_same_row(&l.v[i], p)) { cur = i; break; }
  if (cur < 0) cur = 0;
  if (!back) {
    for (i = cur + 1; i < l.n && pick < 0; i++) if (l.v[i].depth && row_empty(mp_row(e, &l.v[i]))) pick = i;
    for (i = 0; i <= cur && pick < 0; i++) if (l.v[i].depth && row_empty(mp_row(e, &l.v[i]))) pick = i;
  } else {
    for (i = cur - 1; i >= 0 && pick < 0; i--) if (l.v[i].depth && row_empty(mp_row(e, &l.v[i]))) pick = i;
    for (i = l.n - 1; i >= cur && pick < 0; i--) if (l.v[i].depth && row_empty(mp_row(e, &l.v[i]))) pick = i;
  }
  if (pick >= 0) { *p = l.v[pick]; p->off = 0; }
  free(l.v);
  return pick >= 0;
}
static int wordch(u32 c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || (c >= 0xC0 && c < 0x2000 && c != 0xD7 && c != 0xF7); }
/* the code point at unit off of a row (0 a structure or the end) */
static u32 unit_cp(const MNode *row, int off)
{
  int i, k, used;
  const MNode *n;
  locate(row, off, &i, &k);
  if (i >= row->nkid) return 0;
  n = row->kid[i];
  if (n->kind != MK_RUN) return 0;
  {
    size_t b = byte_at(n->text, k);
    return utf8_get(n->text + b, strlen(n->text + b), &used);
  }
}
void me_word(const MathEq *e, MPos *p, int right)
{
  MNode *row;
  int u;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return;
  u = mp_units(row);
  if (right) {
    if (p->off >= u) { me_right(e, p); return; }
    if (!wordch(unit_cp(row, p->off))) { p->off++; return; }
    while (p->off < u && wordch(unit_cp(row, p->off))) p->off++;
  } else {
    if (p->off == 0) { me_left(e, p); return; }
    if (!wordch(unit_cp(row, p->off - 1))) { p->off--; return; }
    while (p->off > 0 && wordch(unit_cp(row, p->off - 1))) p->off--;
  }
}

/* ------------------------------------------------------------------ */
/* changing */
static int same_look(const MNode *a, const MNode *b)
{
  if (memcmp(a->pv, b->pv, sizeof a->pv) || a->size != b->size || a->color != b->color || a->wb != b->wb || a->wi != b->wi || strcmp(a->font, b->font)) return 0;
  if ((a->rpr == NULL) != (b->rpr == NULL) || (a->rpr && strcmp(a->rpr, b->rpr))) return 0;
  if (a->xpr || b->xpr || a->ctrl || b->ctrl) return 0;
  if (a->pv[P_R_BRK] == MB_ON || b->pv[P_R_BRK] == MB_ON) return 0;    /* (a break before a run stays at its start) */
  return 1;
}
static void tidy_row(MNode *row)
{
  int i;
  for (i = 0; i < row->nkid; ) {
    MNode *a = row->kid[i];
    if (a->kind == MK_RUN && (!a->text || !a->text[0]) && a->pv[P_R_BRK] != MB_ON && !a->ctrl && !a->xpr) { mn_free(mn_remove(row, i)); continue; }
    if (a->kind == MK_RUN && i + 1 < row->nkid && row->kid[i + 1]->kind == MK_RUN && same_look(a, row->kid[i + 1])) {
      MNode *b = row->kid[i + 1];
      size_t la = a->text ? strlen(a->text) : 0, lb = b->text ? strlen(b->text) : 0;
      char *t = (char *)malloc(la + lb + 1);
      if (!t) { i++; continue; }
      if (la) memcpy(t, a->text, la);
      if (lb) memcpy(t + la, b->text, lb);
      t[la + lb] = 0;
      free(a->text); a->text = t;
      mn_free(mn_remove(row, i + 1));
      continue;
    }
    i++;
  }
}
static void tidy_all(MNode *n, int depth)
{
  u16 i;
  if (depth > 60) return;
  for (i = 0; i < n->nkid; i++) if (n->kid[i]->kind != MK_RUN && n->kid[i]->kind != MK_RAW) tidy_all(n->kid[i], depth + 1);
  if (n->kind == MK_ROW) tidy_row(n);
}
void me_tidy(MathEq *e)
{
  u16 i;
  for (i = 0; i < e->nline; i++) tidy_all(e->line[i], 0);
}
/* unit off falls between nodes afterwards (a run split there); returns the node index there */
static int split_at(MNode *row, int off)
{
  int i, k;
  MNode *r, *b;
  size_t at;
  locate(row, off, &i, &k);
  if (!k) return i;
  r = row->kid[i];
  at = byte_at(r->text, k);
  b = mn_clone(r);
  if (!b) return i;
  free(b->text);
  if (!(b->text = str_dup(r->text + at))) { mn_free(b); return i; }
  r->text[at] = 0;
  b->pv[P_R_BRK] = 0;                     /* (a break stays before the first part) */
  if (!mn_insert(row, i + 1, b)) return i;
  return i + 1;
}
static int is_normal_run(const MNode *n) { return n->kind == MK_RUN && n->pv[P_R_NOR] == MB_ON; }
static int text_into(MNode *r, size_t at, const char *s)
{
  size_t l = r->text ? strlen(r->text) : 0, n = strlen(s);
  char *t = (char *)malloc(l + n + 1);
  if (!t) return 0;
  if (at > l) at = l;
  if (at) memcpy(t, r->text, at);
  memcpy(t + at, s, n);
  if (l > at) memcpy(t + at + n, r->text + at, l - at);
  t[l + n] = 0;
  free(r->text); r->text = t;
  return 1;
}
int me_insert_text(MathEq *e, MPos *p, const char *s, int normal)
{
  MNode *row, *n;
  int i, k, cn = cps(s);
  mp_clamp(e, p);
  if (!(row = mp_row(e, p)) || !cn) return 0;
  locate(row, p->off, &i, &k);
  if (k) {
    n = row->kid[i];
    if (is_normal_run(n) == !!normal) {
      if (!text_into(n, byte_at(n->text, k), s)) return 0;
      p->off = (u16)(p->off + cn);
      return 1;
    }
    i = split_at(row, p->off);
  }
  if (i > 0 && row->kid[i - 1]->kind == MK_RUN && is_normal_run(row->kid[i - 1]) == !!normal && row->kid[i - 1]->text && !row->kid[i - 1]->xpr) {
    n = row->kid[i - 1];
    if (!text_into(n, strlen(n->text), s)) return 0;
  } else if (i < row->nkid && row->kid[i]->kind == MK_RUN && is_normal_run(row->kid[i]) == !!normal && row->kid[i]->pv[P_R_BRK] != MB_ON && !row->kid[i]->xpr) {
    if (!text_into(row->kid[i], 0, s)) return 0;
  } else {
    MNode *r = mn_run(s, strlen(s)), *like = i > 0 && row->kid[i - 1]->kind == MK_RUN ? row->kid[i - 1] : i < row->nkid && row->kid[i]->kind == MK_RUN ? row->kid[i] : NULL;
    if (!r) return 0;
    if (normal) r->pv[P_R_NOR] = MB_ON;
    if (like) {
      /* the neighbour's look (its w:rPr: the font and size Word gave it) */
      if (like->rpr && !(r->rpr = str_dup(like->rpr))) { mn_free(r); return 0; }
      r->size = like->size; r->color = like->color;
    }
    if (!mn_insert(row, i, r)) return 0;
  }
  p->off = (u16)(p->off + cn);
  return 1;
}
/* the first empty argument shown inside nodes [a, b) of row (in reading order): its place */
static int find_empty(const MNode *row, int a, int b, MPos *at)
{
  int i, u = node_start(row, a);
  for (i = a; i < b && i < row->nkid; i++) {
    const MNode *n = row->kid[i];
    if (is_struct(n) && n->kind != MK_RAW && at->depth < MP_DEPTH) {
      int nav[64], nn = mn_nav(n, nav, 64), j;
      for (j = 0; j < nn; j++) {
        MPos q = *at;
        push(&q, u, nav[j], 0);
        if (row_empty(n->kid[nav[j]])) { *at = q; return 1; }
        if (find_empty(n->kid[nav[j]], 0, n->kid[nav[j]]->nkid, &q)) { *at = q; return 1; }
      }
    }
    u += node_units(n);
  }
  return 0;
}
int me_insert_row(MathEq *e, MPos *p, MNode *src, MNode *cut)
{
  MNode *row;
  int idx, n, first = -1, i;
  MPos q;
  mp_clamp(e, p);
  if (!src || !(row = mp_row(e, p))) { mn_free(src); mn_free(cut); return 0; }
  idx = split_at(row, p->off);
  n = 0;
  while (src->nkid) {
    MNode *k = mn_remove(src, 0);
    if (!mn_insert(row, idx + n, k)) break;
    n++;
  }
  mn_free(src);
  for (i = idx; i < idx + n; i++) if (is_struct(row->kid[i]) && row->kid[i]->kind != MK_RAW) { first = i; break; }
  if (cut && cut->nkid && first >= 0) {
    MNode *s = row->kid[first];
    int a = mn_main_arg(s);
    if (a < s->nkid && row_empty(s->kid[a])) {
      MNode *t = s->kid[a];
      while (t->nkid) mn_free(mn_remove(t, 0));
      while (cut->nkid) if (!mn_insert(t, -1, mn_remove(cut, 0))) break;
    }
  }
  mn_free(cut);
  q = *p;
  q.off = 0;
  if (find_empty(row, idx, idx + n, &q)) *p = q;
  else p->off = (u16)node_start(row, idx + n);
  me_tidy(e);
  mp_clamp(e, p);
  return 1;
}
/* code point k of run n taken out (the run gone if that empties it) */
static void del_char(MNode *row, int i, int k)
{
  MNode *n = row->kid[i];
  size_t b = byte_at(n->text, k), c = byte_at(n->text, k + 1);
  memmove(n->text + b, n->text + c, strlen(n->text + c) + 1);
  if (!n->text[0] && n->pv[P_R_BRK] != MB_ON && !n->ctrl) mn_free(mn_remove(row, i));
}
/* structure j of row taken out: its arguments' contents left in its place (dissolve), or nothing */
static void remove_struct(MNode *row, int j, int keep)
{
  MNode *n = mn_remove(row, j);
  int nav[64], nn, c, at = j;
  if (!n) return;
  nn = keep ? mn_nav(n, nav, 64) : 0;
  for (c = 0; c < nn; c++) {
    MNode *a = n->kid[nav[c]];
    while (a->nkid) if (mn_insert(row, at, mn_remove(a, 0))) at++;
  }
  mn_free(n);
}
int me_backspace(MathEq *e, MPos *p, MPos *anchor, int *sel)
{
  MNode *row, *parent, *n;
  int i, k, nav[64], nn, j, start;
  *sel = 0;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return 0;
  if (p->off > 0) {
    locate(row, p->off - 1, &i, &k);
    n = row->kid[i];
    if (n->kind == MK_RUN) { del_char(row, i, k); p->off--; me_tidy(e); return 1; }
    if (n->kind == MK_RAW || all_empty(n)) { mn_free(mn_remove(row, i)); p->off--; me_tidy(e); return 1; }
    /* a structure: selected first (the next Backspace deletes it), as Word */
    *anchor = *p; anchor->off = (u16)(p->off - 1);
    *sel = 1;
    return 0;
  }
  if (!p->depth) {
    if (p->line > 0 && row_empty(row) && e->nline > 1) {
      /* an empty line of a broken equation: gone, the caret at the previous one's end */
      u16 l = p->line;
      mn_free(e->line[l]);
      memmove(e->line + l, e->line + l + 1, (e->nline - l - 1) * sizeof(MNode *));
      e->nline--;
      p->line--; p->off = (u16)mp_units(e->line[p->line]);
      return 1;
    }
    return 0;
  }
  parent = row_at_depth(e, p, p->depth - 1);
  j = parent ? struct_at(parent, p->node[p->depth - 1]) : -1;
  if (j < 0) return 0;
  n = parent->kid[j];
  start = p->node[p->depth - 1];
  i = nav_pos(n, p->arg[p->depth - 1], nav, &nn);
  if (all_empty(n) || i == 0) {
    remove_struct(parent, j, !all_empty(n));
    p->depth--; p->off = (u16)start;
    me_tidy(e);
    mp_clamp(e, p);
    return 1;
  }
  if (i > 0) { p->arg[p->depth - 1] = (u16)nav[i - 1]; p->off = (u16)mp_units(n->kid[nav[i - 1]]); }
  else { p->depth--; p->off = (u16)start; }
  return 0;
}
int me_delete(MathEq *e, MPos *p, MPos *anchor, int *sel)
{
  MNode *row, *parent, *n;
  int i, k, nav[64], nn, j, start;
  *sel = 0;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return 0;
  if (p->off < mp_units(row)) {
    locate(row, p->off, &i, &k);
    n = row->kid[i];
    if (n->kind == MK_RUN) { del_char(row, i, k); me_tidy(e); return 1; }
    if (n->kind == MK_RAW || all_empty(n)) { mn_free(mn_remove(row, i)); me_tidy(e); return 1; }
    *anchor = *p; p->off++;
    *sel = 1;
    return 0;
  }
  if (!p->depth) {
    if (p->line + 1 < e->nline) {
      /* the next line joined on */
      MNode *nx = e->line[p->line + 1];
      while (nx->nkid) if (!mn_insert(row, -1, mn_remove(nx, 0))) break;
      mn_free(nx);
      memmove(e->line + p->line + 1, e->line + p->line + 2, (e->nline - p->line - 2) * sizeof(MNode *));
      e->nline--;
      me_tidy(e);
      return 1;
    }
    return 0;
  }
  parent = row_at_depth(e, p, p->depth - 1);
  j = parent ? struct_at(parent, p->node[p->depth - 1]) : -1;
  if (j < 0) return 0;
  n = parent->kid[j];
  start = p->node[p->depth - 1];
  if (all_empty(n)) {
    remove_struct(parent, j, 0);
    p->depth--; p->off = (u16)start;
    me_tidy(e);
    return 1;
  }
  i = nav_pos(n, p->arg[p->depth - 1], nav, &nn);
  if (i >= 0 && i + 1 < nn) { p->arg[p->depth - 1] = (u16)nav[i + 1]; p->off = 0; }
  else { p->depth--; p->off = (u16)(start + 1); }
  return 0;
}

/* ------------------------------------------------------------------ */
/* selections */
int me_sel_range(const MathEq *e, const MPos *a, const MPos *b, MPos *row, int *u0, int *u1)
{
  int L = 0, alo, ahi, blo, bhi;
  if (a->line != b->line) return 0;
  while (L < a->depth && L < b->depth && a->node[L] == b->node[L] && a->arg[L] == b->arg[L]) L++;
  *row = *a; row->depth = (u16)L; row->off = 0;
  if (a->depth == L) alo = ahi = a->off; else { alo = a->node[L]; ahi = alo + 1; }
  if (b->depth == L) blo = bhi = b->off; else { blo = b->node[L]; bhi = blo + 1; }
  *u0 = alo < blo ? alo : blo;
  *u1 = ahi > bhi ? ahi : bhi;
  (void)e;
  return 1;
}
MNode *me_cut(MathEq *e, const MPos *a, const MPos *b, MPos *p)
{
  MPos rp;
  int u0, u1, i0, i1, k;
  MNode *row, *out;
  if (!me_sel_range(e, a, b, &rp, &u0, &u1) || !(row = mp_row(e, &rp))) return NULL;
  if (!(out = mn_new(MK_ROW))) return NULL;
  split_at(row, u1);
  i0 = split_at(row, u0);
  i1 = split_at(row, u1);                   /* (again: splitting at u0 moved the indexes) */
  for (k = i0; k < i1; k++) if (!mn_insert(out, -1, mn_remove(row, i0))) break;
  *p = rp; p->off = (u16)u0;
  me_tidy(e);
  return out;
}
MNode *me_copy(const MathEq *e, const MPos *a, const MPos *b)
{
  MathEq *c = math_clone(e);
  MPos p;
  MNode *r = c ? me_cut(c, a, b, &p) : NULL;
  math_free(c);
  return r;
}
int me_set_normal(MathEq *e, const MPos *a, const MPos *b, int on)
{
  MPos rp;
  int u0, u1, i0, i1, k, changed = 0;
  MNode *row;
  if (!me_sel_range(e, a, b, &rp, &u0, &u1) || u0 == u1 || !(row = mp_row(e, &rp))) return 0;
  split_at(row, u1);
  i0 = split_at(row, u0);
  i1 = split_at(row, u1);
  for (k = i0; k < i1 && k < row->nkid; k++)
    if (row->kid[k]->kind == MK_RUN && (row->kid[k]->pv[P_R_NOR] == MB_ON) != !!on) { row->kid[k]->pv[P_R_NOR] = on ? MB_ON : 0; changed = 1; }
  me_tidy(e);
  return changed;
}
int me_is_normal(const MathEq *e, const MPos *p)
{
  MNode *row = mp_row(e, p);
  int i, k;
  if (!row) return 0;
  if (p->off == 0) return row->nkid && is_normal_run(row->kid[0]);
  locate(row, p->off - 1, &i, &k);
  return i < row->nkid && is_normal_run(row->kid[i]);
}

/* ------------------------------------------------------------------ */
/* build-up and AutoCorrect */
/* a node build-up may rewrite: plain math runs and structures with nothing kept verbatim */
static int plain(const MNode *n)
{
  if (n->kind == MK_RUN) return n->pv[P_R_NOR] != MB_ON && n->pv[P_R_LIT] != MB_ON && !n->pv[P_R_SCR] && n->pv[P_R_BRK] != MB_ON &&
                                !n->xpr && !n->ctrl && n->color == COLOR_AUTO && !n->size;
  if (n->kind == MK_RAW || n->kind == MK_EQARR || n->kind == MK_MATRIX || n->ctrl || n->xpr) return 0;
  return 1;
}
static int builds(const char *s)
{
  static const u32 bc[] = { '/', 0x2044, 0x2215, 0xA6, '^', '_', 0x221A, 0x221B, 0x221C, 0x2592, 0x252C, 0x2534, 0x2588, 0x25A0, 0x25AD,
                            0x25A1, 0xAF, 0x2581, 0x27E1, 0x23DE, 0x23DF, '(', '[', '{', '|', 0x2016, 0x27E8, 0x2308, 0x230A, 0x3016,
                            0x251C, 0x2061, 0x2298, '\\', '"', 0x2032, '\'', 0 };
  static const char *const fn[] = { "sin", "cos", "tan", "cot", "sec", "csc", "log", "ln", "lim", "max", "min", "exp", "det", "sinh", "cosh", "tanh", NULL };
  size_t i = 0, n = strlen(s);
  int k;
  while (i < n) {
    int used;
    u32 c = utf8_get(s + i, n - i, &used);
    for (k = 0; bc[k]; k++) if (c == bc[k]) return 1;
    if ((c >= 0x300 && c <= 0x36F) || (c >= 0x20D0 && c <= 0x20FF)) return 1;
    if ((c >= 0x220F && c <= 0x2211) || (c >= 0x222B && c <= 0x2233) || (c >= 0x22C0 && c <= 0x22C3) || (c >= 0x2A00 && c <= 0x2A0C)) return 1;
    i += (size_t)used;
  }
  for (k = 0; fn[k]; k++) if (strstr(s, fn[k])) return 1;
  return 0;
}
/* brackets opened and not closed yet (still being typed): build-up waits */
static int unbalanced(const char *s, int latex)
{
  int depth = 0, braces = 0;
  size_t i = 0, n = strlen(s);
  while (i < n) {
    int used;
    u32 c = utf8_get(s + i, n - i, &used);
    if (c == '(' || c == '[' || c == 0x27E8 || c == 0x2308 || c == 0x230A || c == 0x3016 || c == 0x251C) depth++;
    else if ((c == ')' || c == ']' || c == 0x27E9 || c == 0x2309 || c == 0x230B || c == 0x3017 || c == 0x2524) && depth > 0) depth--;
    else if (c == '{') braces++;
    else if (c == '}' && braces > 0) braces--;
    i += (size_t)used;
  }
  return latex ? braces > 0 : (depth > 0 || braces > 0);
}
/* the last character is an operator still waiting for its operand */
static int dangling(const char *s)
{
  size_t n = strlen(s);
  u32 c;
  int used;
  if (!n) return 0;
  n = utf8_prev(s, n);
  c = utf8_get(s + n, strlen(s + n), &used);
  return c == '/' || c == '^' || c == '_' || c == 0x221A || c == 0x2592 || c == 0x252C || c == 0x2534 || c == 0x2215 || c == 0x2044 || c == 0xA6 || c == '\\';
}
static void lin_row_text(const MNode *row, int a, int b, Buf *o)
{
  MathEq tmp;
  MNode r, *one[1];
  memset(&tmp, 0, sizeof tmp); memset(&r, 0, sizeof r);
  r.kind = MK_ROW; r.kid = row->kid + a; r.nkid = (u16)(b - a);
  one[0] = &r; tmp.line = one; tmp.nline = 1;
  math_to_linear(&tmp, o);
}
int me_build_up(MathEq *e, MPos *p, int latex)
{
  MNode *row, *built;
  MathEq *b;
  int at, start, i, nb, before;
  Buf t;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p))) return 0;
  at = split_at(row, p->off);
  for (start = at; start > 0 && plain(row->kid[start - 1]); start--) ;
  if (start == at) { me_tidy(e); return 0; }
  buf_init(&t);
  lin_row_text(row, start, at, &t);
  if (!t.data || (!latex && !builds(t.data)) || unbalanced(t.data, latex) || dangling(t.data)) { buf_free(&t); me_tidy(e); return 0; }
  b = latex ? math_from_latex(t.data, 0) : math_from_linear(t.data, 0);
  buf_free(&t);
  if (!b || !b->nline) { math_free(b); me_tidy(e); return 0; }
  built = b->line[0];
  /* (a # numbers the whole equation, not this piece: its row alone) */
  if (built->nkid == 1 && built->kid[0]->kind == MK_EQARR && built->kid[0]->nkid == 1) {
    MNode *rw = mn_remove(built->kid[0], 0);
    mn_free(mn_remove(built, 0));
    while (rw && rw->nkid) mn_insert(built, -1, mn_remove(rw, 0));
    mn_free(rw);
  }
  /* unchanged (plain text that builds nothing): left alone */
  {
    Buf x, y;
    MathEq tmp;
    MNode *one[1];
    int same;
    buf_init(&x); buf_init(&y);
    lin_row_text(row, start, at, &x);
    memset(&tmp, 0, sizeof tmp); one[0] = built; tmp.line = one; tmp.nline = 1;
    math_to_linear(&tmp, &y);
    same = x.data && y.data && !strcmp(x.data, y.data);
    for (i = 0; same && i < built->nkid; i++) if (built->kid[i]->kind != MK_RUN) same = 0;
    buf_free(&x); buf_free(&y);
    if (same) { math_free(b); me_tidy(e); return 0; }
  }
  before = node_start(row, start);
  for (i = start; i < at; i++) mn_free(mn_remove(row, start));
  nb = 0;
  while (built->nkid) { if (!mn_insert(row, start + nb, mn_remove(built, 0))) break; nb++; }
  math_free(b);
  {
    int u = before, k;
    for (k = start; k < start + nb; k++) u += node_units(row->kid[k]);
    p->off = (u16)u;
  }
  me_tidy(e);
  mp_clamp(e, p);
  return 1;
}
int me_autocorrect(MathEq *e, MPos *p)
{
  MNode *row, *n;
  int i, k, s, j;
  size_t b0, b1;
  u32 c;
  mp_clamp(e, p);
  if (!(row = mp_row(e, p)) || !p->off) return 0;
  locate(row, p->off - 1, &i, &k);
  n = row->kid[i];
  if (n->kind != MK_RUN || n->pv[P_R_NOR] == MB_ON || n->pv[P_R_LIT] == MB_ON) return 0;
  k++;                                    /* characters of the run before the caret */
  for (s = k - 1; s >= 0; s--) {
    size_t bb = byte_at(n->text, s);
    if (n->text[bb] == '\\') break;
    if (!((n->text[bb] | 32) >= 'a' && (n->text[bb] | 32) <= 'z')) return 0;
  }
  if (s < 0 || s == k - 1) return 0;
  b0 = byte_at(n->text, s); b1 = byte_at(n->text, k);
  if (!(c = math_keyword(n->text + b0 + 1, b1 - b0 - 1))) return 0;
  {
    char u[8];
    int l = utf8_put(u, c);
    size_t rest = strlen(n->text + b1);
    memmove(n->text + b0 + l, n->text + b1, rest + 1);
    memcpy(n->text + b0, u, (size_t)l);
  }
  j = k - s - 1;
  p->off = (u16)(p->off - j);
  return 1;
}

/* ------------------------------------------------------------------ */
/* structure commands */
static const char *const act_labels[MA_N] = {
  "Remove Structure", "Change to Stacked Fraction", "Change to Skewed Fraction", "Change to Linear Fraction", "Remove Fraction Bar",
  "Hide Degree", "Change Limit Location to Subscript-Superscript", "Change Limit Location to Under-Over",
  "Hide Lower Limit", "Hide Upper Limit", "Grow with Argument",
  "Insert Argument After", "Delete Argument", "Stretch Brackets",
  "Remove Subscript", "Remove Superscript", "Add Subscript", "Add Superscript",
  "Insert Row Above", "Insert Row Below", "Insert Column Before", "Insert Column After", "Delete Row", "Delete Column",
  "Insert Equation Before", "Insert Equation After", "Delete Equation", "Move Character Below", "Change to Underbar",
  "Parentheses ( )", "Square Brackets [ ]", "Braces { }", "Angle Brackets", "Vertical Bars | |", "Double Bars", "Floor", "Ceiling", "No Brackets"
};
static int applies(const MNode *n, int act, int arg)
{
  int t = n->kind;
  (void)arg;
  switch (act) {
  case MA_REMOVE: return is_struct(n) && t != MK_RAW;
  case MA_FRAC_BAR: case MA_FRAC_SKEWED: case MA_FRAC_LINEAR: case MA_FRAC_NOBAR:
    return t == MK_FRAC && (n->pv[P_F_TYPE] ? n->pv[P_F_TYPE] - 1 : MF_BAR) != act - MA_FRAC_BAR;
  case MA_RAD_DEGREE: return t == MK_RAD;
  case MA_NARY_SIDE: case MA_NARY_UNDER: case MA_NARY_LOWER: case MA_NARY_UPPER: case MA_NARY_GROW: return t == MK_NARY;
  case MA_DELIM_ADD: case MA_DELIM_GROW: return t == MK_DELIM;
  case MA_DELIM_DELETE: return t == MK_DELIM && n->nkid > 1;
  case MA_DELIM_PAREN: case MA_DELIM_SQUARE: case MA_DELIM_BRACE: case MA_DELIM_ANGLE: case MA_DELIM_BARS: case MA_DELIM_NORM:
  case MA_DELIM_FLOOR: case MA_DELIM_CEIL: case MA_DELIM_NONE: return t == MK_DELIM;
  case MA_SCRIPT_NOSUB: case MA_SCRIPT_NOSUP: return t == MK_SSUBSUP;
  case MA_SCRIPT_ADDSUB: return t == MK_SSUP;
  case MA_SCRIPT_ADDSUP: return t == MK_SSUB;
  case MA_ROW_ABOVE: case MA_ROW_BELOW: case MA_COL_BEFORE: case MA_COL_AFTER: return t == MK_MATRIX;
  case MA_ROW_DELETE: return t == MK_MATRIX && n->rows > 1;
  case MA_COL_DELETE: return t == MK_MATRIX && n->cols > 1;
  case MA_EQ_BEFORE: case MA_EQ_AFTER: return t == MK_EQARR;
  case MA_EQ_DELETE: return t == MK_EQARR && n->nkid > 1;
  case MA_GROUP_FLIP: return t == MK_GROUPCHR;
  case MA_BAR_FLIP: return t == MK_BAR;
  }
  return 0;
}
int me_actions(const MathEq *e, const MPos *p, MAct *out, int max)
{
  int d, a, c = 0;
  MPos q = *p;
  mp_clamp(e, &q);
  for (d = q.depth - 1; d >= 0; d--) {
    MNode *n = step_node(e, &q, d);
    if (!n) continue;
    for (a = 1; a < MA_N; a++) if (applies(n, a, q.arg[d]) && c < max) { out[c].act = (u8)a; out[c].depth = (u8)d; c++; }
    if (c < max) { out[c].act = MA_REMOVE; out[c].depth = (u8)d; c++; }
  }
  return c;
}
const char *me_action_label(int act, const MathEq *e, const MPos *p, int depth)
{
  MNode *n = depth >= 0 && depth < p->depth ? step_node(e, p, depth) : NULL;
  if (act < 0 || act >= MA_N) return "";
  if (n) {
    switch (act) {
    case MA_RAD_DEGREE: return n->pv[P_RAD_DEGHIDE] == MB_ON ? "Show Degree" : "Hide Degree";
    case MA_NARY_LOWER: return n->pv[P_N_SUBHIDE] == MB_ON ? "Show Lower Limit" : "Hide Lower Limit";
    case MA_NARY_UPPER: return n->pv[P_N_SUPHIDE] == MB_ON ? "Show Upper Limit" : "Hide Upper Limit";
    case MA_NARY_GROW: return n->pv[P_N_GROW] == MB_ON ? "Don't Grow with Argument" : "Grow with Argument";
    case MA_DELIM_GROW: return n->pv[P_D_GROW] == MB_OFF ? "Stretch Brackets" : "Don't Stretch Brackets";
    case MA_GROUP_FLIP: return n->pv[P_G_POS] == MPOS_TOP + 1 ? "Move Character Below" : "Move Character Above";
    case MA_BAR_FLIP: return n->pv[P_BAR_POS] == MPOS_TOP + 1 ? "Change to Underbar" : "Change to Overbar";
    case MA_REMOVE:
      switch (n->kind) {
      case MK_DELIM: return "Remove Brackets";
      case MK_RAD: return "Remove Radical";
      case MK_FRAC: return "Remove Fraction";
      case MK_SSUB: case MK_SSUP: case MK_SSUBSUP: case MK_SPRE: return "Remove Scripts";
      case MK_NARY: return "Remove Operator";
      case MK_ACC: return "Remove Accent";
      case MK_MATRIX: return "Remove Matrix";
      }
      break;
    }
  }
  return act_labels[act];
}
static MNode *new_row(void) { return mn_new(MK_ROW); }
int me_apply(MathEq *e, MPos *p, int act, int depth)
{
  MNode *parent, *n;
  int d, j, c, start;
  mp_clamp(e, p);
  if (depth < 0) {
    for (d = p->depth - 1; d >= 0; d--) { MNode *x = step_node(e, p, d); if (x && applies(x, act, p->arg[d])) break; }
    depth = d;
  }
  if (depth < 0 || depth >= p->depth) return 0;
  parent = row_at_depth(e, p, depth);
  j = parent ? struct_at(parent, p->node[depth]) : -1;
  if (j < 0) return 0;
  n = parent->kid[j];
  if (!applies(n, act, p->arg[depth])) return 0;
  c = p->arg[depth];
  start = p->node[depth];
#define HERE(k) do { p->depth = (u16)(depth + 1); p->arg[depth] = (u16)(k); p->off = 0; } while (0)
  switch (act) {
  case MA_REMOVE:
    remove_struct(parent, j, 1);
    p->depth = (u16)depth; p->off = (u16)start;
    break;
  case MA_FRAC_BAR: case MA_FRAC_SKEWED: case MA_FRAC_LINEAR: case MA_FRAC_NOBAR:
    n->pv[P_F_TYPE] = act == MA_FRAC_BAR ? 0 : (u8)(act - MA_FRAC_BAR + 1);
    break;
  case MA_RAD_DEGREE:
    if (n->pv[P_RAD_DEGHIDE] == MB_ON) { n->pv[P_RAD_DEGHIDE] = MB_OFF; HERE(0); }
    else { n->pv[P_RAD_DEGHIDE] = MB_ON; while (n->kid[0]->nkid) mn_free(mn_remove(n->kid[0], 0)); HERE(1); }
    break;
  case MA_NARY_SIDE: n->pv[P_N_LIMLOC] = MLIM_SUBSUP + 1; break;
  case MA_NARY_UNDER: n->pv[P_N_LIMLOC] = MLIM_UNDOVR + 1; break;
  case MA_NARY_LOWER:
    if (n->pv[P_N_SUBHIDE] == MB_ON) { n->pv[P_N_SUBHIDE] = MB_OFF; HERE(0); }
    else { n->pv[P_N_SUBHIDE] = MB_ON; while (n->kid[0]->nkid) mn_free(mn_remove(n->kid[0], 0)); HERE(2); }
    break;
  case MA_NARY_UPPER:
    if (n->pv[P_N_SUPHIDE] == MB_ON) { n->pv[P_N_SUPHIDE] = MB_OFF; HERE(1); }
    else { n->pv[P_N_SUPHIDE] = MB_ON; while (n->kid[1]->nkid) mn_free(mn_remove(n->kid[1], 0)); HERE(2); }
    break;
  case MA_NARY_GROW: n->pv[P_N_GROW] = n->pv[P_N_GROW] == MB_ON ? MB_OFF : MB_ON; break;
  case MA_DELIM_GROW: n->pv[P_D_GROW] = n->pv[P_D_GROW] == MB_OFF ? MB_ON : MB_OFF; break;
  case MA_DELIM_ADD: {
    int at = (p->depth > depth ? c : n->nkid - 1) + 1;
    if (at > n->nkid) at = n->nkid;
    if (!mn_insert(n, at, new_row())) return 0;
    HERE(at);
    break; }
  case MA_DELIM_DELETE: {
    int k = p->depth > depth ? c : n->nkid - 1;
    if (k >= n->nkid) k = n->nkid - 1;
    mn_free(mn_remove(n, k));
    HERE(k < n->nkid ? k : n->nkid - 1);
    break; }
  case MA_DELIM_PAREN: case MA_DELIM_SQUARE: case MA_DELIM_BRACE: case MA_DELIM_ANGLE: case MA_DELIM_BARS: case MA_DELIM_NORM:
  case MA_DELIM_FLOOR: case MA_DELIM_CEIL: case MA_DELIM_NONE: {
    static const u32 beg[] = { '(', '[', '{', 0x27E8, '|', 0x2016, 0x230A, 0x2308, 0 }, end[] = { ')', ']', '}', 0x27E9, '|', 0x2016, 0x230B, 0x2309, 0 };
    int k = act - MA_DELIM_PAREN;
    if (k == 0) { n->ch[0] = n->ch[2] = MCH_UNSET; }
    else { n->ch[0] = beg[k]; n->ch[2] = end[k]; }
    break; }
  case MA_SCRIPT_NOSUB: case MA_SCRIPT_NOSUP: {
    /* sSubSup (e, sub, sup) to sSup (e, sup) or sSub (e, sub) */
    MNode *gone = mn_remove(n, act == MA_SCRIPT_NOSUB ? 1 : 2);
    mn_free(gone);
    n->kind = act == MA_SCRIPT_NOSUB ? MK_SSUP : MK_SSUB;
    n->pv[P_SS_ALNSCR] = 0;
    HERE(0);
    break; }
  case MA_SCRIPT_ADDSUB: case MA_SCRIPT_ADDSUP:
    if (!mn_insert(n, act == MA_SCRIPT_ADDSUB ? 1 : 2, new_row())) return 0;
    n->kind = MK_SSUBSUP;
    HERE(act == MA_SCRIPT_ADDSUB ? 1 : 2);
    break;
  case MA_ROW_ABOVE: case MA_ROW_BELOW: case MA_COL_BEFORE: case MA_COL_AFTER: case MA_ROW_DELETE: case MA_COL_DELETE: {
    int cols = n->cols ? n->cols : 1, rows = n->rows ? n->rows : 1, r = (p->depth > depth ? c : 0) / cols, col = (p->depth > depth ? c : 0) % cols, k, tr = r, tc = col;
    if (act == MA_ROW_ABOVE || act == MA_ROW_BELOW) {
      int at = (act == MA_ROW_ABOVE ? r : r + 1) * cols;
      if (rows >= 100) return 0;
      for (k = 0; k < cols; k++) if (!mn_insert(n, at, new_row())) return 0;
      n->rows++;
      tr = act == MA_ROW_ABOVE ? r : r + 1;
    } else if (act == MA_COL_BEFORE || act == MA_COL_AFTER) {
      int ac = act == MA_COL_BEFORE ? col : col + 1;
      if (cols >= 64) return 0;
      for (k = rows - 1; k >= 0; k--) if (!mn_insert(n, k * cols + ac, new_row())) return 0;
      n->cols++;
      free(n->mcs); n->mcs = NULL; n->nmcs = 0;      /* (the column groups no longer match) */
      tc = ac;
    } else if (act == MA_ROW_DELETE) {
      for (k = 0; k < cols; k++) mn_free(mn_remove(n, r * cols));
      n->rows--;
      if (tr >= n->rows) tr = n->rows - 1;
    } else {
      for (k = rows - 1; k >= 0; k--) mn_free(mn_remove(n, k * cols + col));
      n->cols--;
      free(n->mcs); n->mcs = NULL; n->nmcs = 0;
      if (tc >= n->cols) tc = n->cols - 1;
    }
    HERE(tr * (n->cols ? n->cols : 1) + tc);
    break; }
  case MA_EQ_BEFORE: case MA_EQ_AFTER: {
    int at = (p->depth > depth ? c : n->nkid - 1) + (act == MA_EQ_AFTER);
    if (!mn_insert(n, at, new_row())) return 0;
    HERE(at);
    break; }
  case MA_EQ_DELETE: {
    int k = p->depth > depth ? c : n->nkid - 1;
    mn_free(mn_remove(n, k));
    HERE(k < n->nkid ? k : n->nkid - 1);
    break; }
  case MA_GROUP_FLIP: {
    int top = n->pv[P_G_POS] == MPOS_TOP + 1;
    u32 ch = n->ch[0] == MCH_UNSET ? 0x23DF : n->ch[0];
    n->pv[P_G_POS] = top ? MPOS_BOT + 1 : MPOS_TOP + 1;
    n->pv[P_G_VERTJC] = top ? MPOS_TOP + 1 : MPOS_BOT + 1;
    ch = ch == 0x23DE ? 0x23DF : ch == 0x23DF ? 0x23DE : ch == 0x23DC ? 0x23DD : ch == 0x23DD ? 0x23DC : ch;
    n->ch[0] = ch == 0x23DF ? MCH_UNSET : ch;
    break; }
  case MA_BAR_FLIP: n->pv[P_BAR_POS] = n->pv[P_BAR_POS] == MPOS_TOP + 1 ? MPOS_BOT + 1 : MPOS_TOP + 1; break;
  default: return 0;
  }
#undef HERE
  me_tidy(e);
  mp_clamp(e, p);
  return 1;
}

/* ------------------------------------------------------------------ */
/* Professional and Linear */
static MNode *number_row(const MathEq *e);
int me_is_linear_form(const MathEq *e)
{
  u16 i, k;
  for (i = 0; i < e->nline; i++)
    for (k = 0; k < e->line[i]->nkid; k++) if (!plain(e->line[i]->kid[k]) || e->line[i]->kid[k]->kind != MK_RUN) return 0;
  {
    Buf b;
    int r;
    buf_init(&b);
    math_to_linear(e, &b);
    r = b.data && builds(b.data);
    buf_free(&b);
    return r;
  }
}
/* the lines of e replaced by those of n (taken): e keeps its display, justification and properties */
static void take_lines(MathEq *e, MathEq *n)
{
  u16 i;
  for (i = 0; i < e->nline; i++) mn_free(e->line[i]);
  free(e->line);
  e->line = n->line; e->nline = n->nline;
  n->line = NULL; n->nline = 0;
  math_free(n);
}
int me_to_linear_form(MathEq *e, int latex)
{
  Buf b;
  MathEq *n;
  MNode *r;
  buf_init(&b);
  if (latex) math_to_latex(e, &b); else math_to_linear(e, &b);
  if (!b.data) { buf_free(&b); return 0; }
  if (!(n = math_new(e->display)) || !(r = mn_run(b.data, b.len))) { math_free(n); buf_free(&b); return 0; }
  buf_free(&b);
  if (!mn_insert(n->line[0], -1, r)) { math_free(n); return 0; }
  take_lines(e, n);
  return 1;
}
int me_to_professional(MathEq *e, int latex)
{
  Buf b;
  MathEq *n;
  (void)number_row;
  buf_init(&b);
  math_to_linear(e, &b);
  if (!b.data) { buf_free(&b); return 0; }
  n = latex ? math_from_latex(b.data, e->display) : math_from_linear(b.data, e->display);
  buf_free(&b);
  if (!n) return 0;
  take_lines(e, n);
  return 1;
}

/* ------------------------------------------------------------------ */
/* equation numbers */
/* the numbered row: the single line is one equation array of one row with a # in a top-level run */
static MNode *number_row(const MathEq *e)
{
  MNode *a, *r;
  u16 i;
  if (!e || e->nline != 1 || e->line[0]->nkid != 1) return NULL;
  a = e->line[0]->kid[0];
  if (a->kind != MK_EQARR || a->nkid != 1) return NULL;
  r = a->kid[0];
  for (i = 0; i < r->nkid; i++) if (r->kid[i]->kind == MK_RUN && r->kid[i]->pv[P_R_NOR] != MB_ON && r->kid[i]->text && strchr(r->kid[i]->text, '#')) return r;
  return NULL;
}
/* the # run's index and the byte of the # in it */
static int hash_at(const MNode *r, size_t *b)
{
  u16 i;
  for (i = 0; i < r->nkid; i++) {
    const char *h;
    if (r->kid[i]->kind == MK_RUN && r->kid[i]->pv[P_R_NOR] != MB_ON && r->kid[i]->text && (h = strchr(r->kid[i]->text, '#'))) { *b = (size_t)(h - r->kid[i]->text); return i; }
  }
  return -1;
}
/* what follows the # is (digits): the delimiter holding them, else NULL */
static MNode *auto_number(MNode *r)
{
  size_t b;
  int i = hash_at(r, &b);
  MNode *d, *in;
  const char *t;
  if (i < 0 || r->kid[i]->text[b + 1] || i + 2 != r->nkid) return NULL;
  d = r->kid[i + 1];
  if (d->kind != MK_DELIM || d->nkid != 1 || d->ch[0] != MCH_UNSET || d->ch[2] != MCH_UNSET) return NULL;
  in = d->kid[0];
  if (in->nkid != 1 || in->kid[0]->kind != MK_RUN || !in->kid[0]->text || !in->kid[0]->text[0]) return NULL;
  for (t = in->kid[0]->text; *t; t++) if (*t < '0' || *t > '9') return NULL;
  return d;
}
int math_numbered(const MathEq *e)
{
  MNode *r = number_row(e);
  if (!r || !e->display) return 0;
  return auto_number(r) ? 2 : 1;
}
static MNode *number_delim(int n)
{
  char num[16];
  MNode *d = mn_struct(MK_DELIM, 1, 0), *r;
  sprintf(num, "%d", n);
  if (!d || !(r = mn_run(num, strlen(num))) || !mn_insert(d->kid[0], -1, r)) { mn_free(d); return NULL; }
  return d;
}
int me_number(MathEq *e, int on, int n)
{
  MNode *r = number_row(e);
  if (on) {
    MNode *arr, *row, *h, *d;
    if (r) return 0;
    if (e->nline != 1) return 0;
    if (!(arr = mn_new(MK_EQARR))) return 0;
    row = e->line[0];
    if (!(e->line[0] = mn_new(MK_ROW))) { e->line[0] = row; mn_free(arr); return 0; }
    mn_insert(arr, -1, row);
    h = mn_run("#", 1);
    d = number_delim(n);
    if (!h || !d || !mn_insert(row, -1, h) || !mn_insert(row, -1, d) || !mn_insert(e->line[0], -1, arr)) { mn_free(d); return 0; }
    e->display = 1;
    return 1;
  } else {
    size_t b;
    int i;
    MNode *arr, *row;
    if (!r) return 0;
    i = hash_at(r, &b);
    /* everything from the # on goes */
    r->kid[i]->text[b] = 0;
    while (r->nkid > i + 1) mn_free(mn_remove(r, i + 1));
    arr = mn_remove(e->line[0], 0);
    row = mn_remove(arr, 0);
    mn_free(arr);
    mn_free(e->line[0]);
    e->line[0] = row;
    me_tidy(e);
    return 1;
  }
}
int math_renumber(Doc *d)
{
  u32 pi;
  int n = 0, changed = 0;
  for (pi = 0; pi < d->np; pi++) {
    Para *p = d->p[pi];
    u32 o;
    int touched = 0;
    for (o = 0; o + 4 <= p->len; o++) {
      MathEq *e;
      MNode *r, *dl;
      if (!IS_MATH(p->t + o)) continue;
      e = doc_math(d, math_index(p->t + o));
      o += 3;
      if (!e || !(r = number_row(e)) || !e->display) continue;
      n++;
      if ((dl = auto_number(r))) {
        char num[16];
        MNode *run = dl->kid[0]->kid[0];
        sprintf(num, "%d", n);
        if (strcmp(run->text, num)) {
          char *t = str_dup(num);
          if (t) { free(run->text); run->text = t; e->ver++; touched = 1; changed++; }
        }
      }
    }
    if (touched) p->ver++;
  }
  if (changed) d->ver++;
  return changed;
}
i32 math_fit(FontSet *fs, Doc *d, int k, const CharFmt *f, i32 room)
{
  MathEq *e = doc_math(d, k);
  const MLay *ml;
  if (!e) return 0;
  if (e->display && math_numbered(e) && e->room != room) e->room = room;
  ml = math_layout(fs, d, k, f);
  return ml ? ml->w : 0;
}

/* ------------------------------------------------------------------ */
/* geometry */
static i32 off_x(const MNode *row, int off)
{
  int i, k;
  const MNode *n;
  locate(row, off, &i, &k);
  if (i >= row->nkid) {
    if (!row->nkid) return 0;
    n = row->kid[row->nkid - 1];
    return n->x + n->w;
  }
  n = row->kid[i];
  if (!k) return n->x;
  if (n->cx && k < n->ncx) return n->x + n->cx[k];
  {
    int len = cps(n->text);
    return n->x + (len ? n->w * k / len : 0);
  }
}
static void row_origin(const MathEq *e, const MPos *p, int depth, i32 *x, i32 *y)
{
  MNode *row;
  int d;
  *x = 0; *y = 0;
  if (!e || p->line >= e->nline) return;
  row = e->line[p->line];
  *x = row->x; *y = row->y;
  for (d = 0; d < depth; d++) {
    int i = struct_at(row, p->node[d]);
    MNode *n;
    if (i < 0) return;
    n = row->kid[i];
    if (p->arg[d] >= n->nkid) return;
    row = n->kid[p->arg[d]];
    *x += n->x + row->x; *y += n->y + row->y;
  }
}
void me_caret_box(const MathEq *e, const MPos *p, i32 *x, i32 *top, i32 *h)
{
  MNode *row = mp_row(e, p);
  i32 rx, ry, asc, desc;
  if (!row) { *x = 0; *top = -200; *h = 240; return; }
  row_origin(e, p, p->depth, &rx, &ry);
  *x = rx + off_x(row, p->off);
  asc = row->asc; desc = row->desc;
  if (asc + desc < 120) { asc = 160; desc = 40; }
  *top = ry - asc; *h = asc + desc;
}
typedef struct { i32 x, y; MPos best; i32 bd; int bdepth; } Hit;
static void hit_row(const MathEq *e, MPos *at, const MNode *row, i32 ox, i32 oy, Hit *h)
{
  i32 x0 = ox, x1 = ox + (row->w > 60 ? row->w : 60), y0 = oy - row->asc, y1 = oy + row->desc, dx = 0, dy = 0, dist;
  u16 i;
  int u = 0;
  if (h->x < x0) dx = x0 - h->x; else if (h->x > x1) dx = h->x - x1;
  if (h->y < y0) dy = y0 - h->y; else if (h->y > y1) dy = h->y - y1;
  dist = dx + dy * 2;
  if (dist < h->bd || (dist == h->bd && at->depth > h->bdepth)) {
    h->bd = dist; h->bdepth = at->depth;
    h->best = *at; h->best.off = (u16)off_near(row, ox, h->x);
  }
  for (i = 0; i < row->nkid; i++) {
    const MNode *n = row->kid[i];
    if (is_struct(n) && n->kind != MK_RAW && at->depth < MP_DEPTH) {
      int nav[64], nn = mn_nav(n, nav, 64), j;
      for (j = 0; j < nn; j++) {
        MPos q = *at;
        const MNode *a = n->kid[nav[j]];
        push(&q, u, nav[j], 0);
        hit_row(e, &q, a, ox + n->x + a->x, oy + n->y + a->y, h);
      }
    }
    u += node_units(n);
  }
}
void me_hit(const MathEq *e, i32 x, i32 y, MPos *p)
{
  Hit h;
  u16 l;
  memset(&h, 0, sizeof h);
  h.x = x; h.y = y; h.bd = 0x7FFFFFFF; h.bdepth = -1;
  for (l = 0; l < e->nline; l++) {
    MPos q;
    memset(&q, 0, sizeof q); q.line = l;
    hit_row(e, &q, e->line[l], e->line[l]->x, e->line[l]->y, &h);
  }
  *p = h.best;
  mp_clamp(e, p);
}
static void slots_row(const MNode *row, i32 ox, i32 oy, void (*f)(void *ctx, i32 x, i32 top, i32 w, i32 h), void *ctx, int depth)
{
  u16 i;
  if (depth > 40) return;
  if (row_empty(row)) { f(ctx, ox, oy - row->asc, row->w, row->asc + row->desc); return; }
  for (i = 0; i < row->nkid; i++) {
    const MNode *n = row->kid[i];
    if (is_struct(n) && n->kind != MK_RAW) {
      int nav[64], nn = mn_nav(n, nav, 64), j;
      for (j = 0; j < nn; j++) {
        const MNode *a = n->kid[nav[j]];
        slots_row(a, ox + n->x + a->x, oy + n->y + a->y, f, ctx, depth + 1);
      }
    }
  }
}
void me_slots(const MathEq *e, void (*f)(void *ctx, i32 x, i32 top, i32 w, i32 h), void *ctx)
{
  u16 l;
  for (l = 0; l < e->nline; l++) slots_row(e->line[l], e->line[l]->x, e->line[l]->y, f, ctx, 0);
}
int me_sel_box(const MathEq *e, const MPos *a, const MPos *b, i32 *x0, i32 *x1, i32 *top, i32 *h)
{
  MPos rp;
  int u0, u1;
  MNode *row;
  i32 rx, ry;
  if (!me_sel_range(e, a, b, &rp, &u0, &u1) || u0 == u1 || !(row = mp_row(e, &rp))) return 0;
  row_origin(e, &rp, rp.depth, &rx, &ry);
  *x0 = rx + off_x(row, u0); *x1 = rx + off_x(row, u1);
  {
    /* as tall as the structures selected */
    int i0, k0, i1, k1, i;
    i32 asc = 0, desc = 0;
    locate(row, u0, &i0, &k0); locate(row, u1, &i1, &k1);
    if (k1) i1++;
    for (i = i0; i < i1 && i < row->nkid; i++) {
      if (row->kid[i]->asc > asc) asc = row->kid[i]->asc;
      if (row->kid[i]->desc > desc) desc = row->kid[i]->desc;
    }
    if (asc + desc < 120) { asc = row->asc; desc = row->desc; }
    *top = ry - asc; *h = asc + desc;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* templates */
static void set_limits(MNode *n, int lim)
{
  u16 i;
  if (n->kind == MK_NARY) n->pv[P_N_LIMLOC] = (u8)(lim + 1);
  for (i = 0; i < n->nkid; i++) set_limits(n->kid[i], lim);
}
MathEq *math_template_eq(const MTemplate *t)
{
  MathEq *e = math_from_linear(t->linear, 1);
  if (e && t->lim >= 0) { u16 i; for (i = 0; i < e->nline; i++) set_limits(e->line[i], t->lim); }
  return e;
}
MNode *math_template(const MTemplate *t)
{
  MathEq *e = math_template_eq(t);
  MNode *r;
  if (!e || !e->nline) { math_free(e); return NULL; }
  r = e->line[0];
  e->line[0] = mn_new(MK_ROW);
  math_free(e);
  return r;
}
