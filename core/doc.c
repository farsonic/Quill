/* doc.c - the document model (see doc.h): paragraphs and their runs, the undo history, the
   editing operations, styles, lists, sections, objects and groups, copy and paste, positions,
   find and replace, and stories (headers, notes...) edited in the body's place */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "doc.h"
#include "keep.h"
#include "image.h"
#include "notes.h"   /* (WP22b) */
#include "info.h"    /* (WP24) */
#include "math.h"     /* WP14a */

/* ------------------------------------------------------------------ */
/* undo: a step replaces paragraphs [p0, p0 + nnew) with copies of what was there before the edit
   (nold paragraphs). A group is the steps of one user action, undone in reverse. */
typedef struct { u32 p0, nnew, nold; Para **old; } Step;
typedef struct { Step *s; int ns, cap; Pos before, after; int typing; int lastspace; Style *styles; int nstyles;
                 u32 serial; int kind; } Group;   /* styles: the style table before, if the action changed it; serial, kind: WP12 (doc_undo_step) */
typedef struct Undo {
  Group *u; int nu, ucap;           /* undo stack, newest last */
  Group *r; int nr, rcap;           /* redo stack */
  int depth;                        /* doc_begin nesting */
  int open;                         /* a group is being filled (u[nu - 1]) */
  Pos caret;
} Undo;
#define UNDO_MAX 300

/* a step's saved paragraphs freed: only those it still owns, as replay sets nold to 0 once they
   are back in the document */
static void step_free(Step *s)
{
  u32 i;
  for (i = 0; i < s->nold; i++) para_free(s->old[i]);
  free(s->old);
}
/* a group's steps and its saved style table freed; the group is left zeroed */
static void group_free(Group *g)
{
  int i;
  for (i = 0; i < g->ns; i++) step_free(&g->s[i]);
  free(g->s);
  free(g->styles);
  memset(g, 0, sizeof *g);
}
/* every group on an undo or redo stack freed; the stack's array is kept for reuse */
static void stack_clear(Group *st, int *n)
{
  int i;
  for (i = 0; i < *n; i++) group_free(&st[i]);
  *n = 0;
}
/* a new, zeroed group on top of a stack (the array doubling as needed); NULL if out of memory */
static Group *push_group(Group **st, int *n, int *cap)
{
  if (*n >= *cap) {
    int nc = *cap ? *cap * 2 : 32;
    Group *ng = (Group *)realloc(*st, nc * sizeof(Group));
    if (!ng) return NULL;
    *st = ng; *cap = nc;
  }
  memset(&(*st)[*n], 0, sizeof(Group));
  return &(*st)[(*n)++];
}
static u32 undo_serials;              /* (WP12) each group's own number, from one counter */
static int undo_classify(Doc *d, const Group *g);
/* a new group for an edit, unless one is open; where: the caret before a single edit */
static Group *cur_group(Doc *d, Pos where)
{
  Undo *u = d->undo;
  Group *g;
  if (u->open) return &u->u[u->nu - 1];
  stack_clear(u->r, &u->nr);
  if (u->nu >= UNDO_MAX) {              /* forget the oldest */
    group_free(&u->u[0]);
    memmove(u->u, u->u + 1, (u->nu - 1) * sizeof(Group));
    u->nu--;
  }
  if (u->nu && !u->u[u->nu - 1].kind) u->u[u->nu - 1].kind = undo_classify(d, &u->u[u->nu - 1]);   /* (WP12: named while the document still shows its result) */
  if (!(g = push_group(&u->u, &u->nu, &u->ucap))) return NULL;
  g->serial = ++undo_serials;
  g->before = g->after = u->depth ? u->caret : where;
  u->open = 1;
  return g;
}
/* an edit made outside doc_begin/doc_end is a group of its own: closed as soon as the edit is
   done, so the next one starts another */
static void close_auto(Doc *d)
{
  if (!d->undo->depth) d->undo->open = 0;
}
/* save paragraphs [p0, p0 + n) before they change; returns the step to complete with its nnew */
static Step *rec(Doc *d, u32 p0, u32 n, Pos where)
{
  Group *g = cur_group(d, where);
  Step *s;
  u32 i;
  if (!g) return NULL;
  if (g->ns >= g->cap) {
    int nc = g->cap ? g->cap * 2 : 4;
    Step *ns = (Step *)realloc(g->s, nc * sizeof(Step));
    if (!ns) return NULL;
    g->s = ns; g->cap = nc;
  }
  s = &g->s[g->ns];
  s->p0 = p0; s->nold = n; s->nnew = n;
  if (!(s->old = (Para **)malloc((n ? n : 1) * sizeof(Para *)))) return NULL;
  for (i = 0; i < n; i++)
    if (!(s->old[i] = para_copy(d->p[p0 + i]))) { s->nold = i; step_free(s); return NULL; }
  g->ns++;
  return s;
}
/* the style table saved before the action changes it (once per action) */
static int rec_styles(Doc *d, Pos where)
{
  Group *g = cur_group(d, where);
  if (!g) return 0;
  if (g->styles) return 1;
  if (!(g->styles = (Style *)malloc((d->nstyles ? d->nstyles : 1) * sizeof(Style)))) return 0;
  memcpy(g->styles, d->styles, d->nstyles * sizeof(Style));
  g->nstyles = d->nstyles;
  return 1;
}
/* the start of paragraph pi */
static Pos para_pos(u32 pi) { Pos p; p.para = pi; p.off = 0; return p; }
static u32 doc_serials;               /* each document's own number (Doc.serial) */
static void restyle_run(CharFmt *c, const CharFmt *op, const CharFmt *np, const CharFmt *ocs, unsigned oem, const CharFmt *ncs, unsigned nem);
/* the document has changed: Doc.ver bumped (views and layout notice) and marked unsaved */
static void changed(Doc *d) { d->ver++; d->dirty = 1; }
/* versions come from one counter, so (paragraph pointer, version) is never reused even when
   malloc hands a freed paragraph's address to a new one: layout caches can trust it */
static u32 stamp;
/* a paragraph has changed: it gets a fresh version from the shared counter above */
static void touch(Para *p) { p->ver = ++stamp; }

/* one user action starts: its edits until the matching doc_end make one undo step. Calls nest and
   only the outermost counts; caret is where undo puts the caret back. The group is only made by
   the first edit, so an action that changes nothing leaves no step */
void doc_begin(Doc *d, Pos caret)
{
  Undo *u = d->undo;
  if (!u->depth++) { u->caret = caret; u->open = 0; }      /* the group starts with the first edit */
}
/* the action begun with doc_begin ends: when the outermost one does, caret is kept as where redo
   puts the caret, and the group is closed */
void doc_end(Doc *d, Pos caret)
{
  Undo *u = d->undo;
  if (u->depth && !--u->depth) {
    if (u->open) u->u[u->nu - 1].after = caret;
    u->open = 0;
  }
}
/* anything to undo, or to redo (for greying out menu items and buttons) */
int doc_can_undo(Doc *d) { return d->undo->nu > 0; }
int doc_can_redo(Doc *d) { return d->undo->nr > 0; }
/* the whole undo and redo history forgotten, the document untouched (the readers and templates
   call it once a document is built) */
void doc_clear_undo(Doc *d)
{
  stack_clear(d->undo->u, &d->undo->nu);
  stack_clear(d->undo->r, &d->undo->nr);
  d->undo->open = 0;
}
/* undo the steps of g (newest first), building the inverse group on the other stack */
static int replay(Doc *d, Group *g, Group *inv)
{
  int i;
  for (i = g->ns - 1; i >= 0; i--) {
    Step *s = &g->s[i], *t;
    u32 k;
    if (s->p0 + s->nnew > d->np) return 0;
    if (inv->ns >= inv->cap) {
      int nc = inv->cap ? inv->cap * 2 : 4;
      Step *ns = (Step *)realloc(inv->s, nc * sizeof(Step));
      if (!ns) return 0;
      inv->s = ns; inv->cap = nc;
    }
    t = &inv->s[inv->ns];
    /* the paragraphs now there become the inverse step's saved copies (moved, not copied) */
    if (!(t->old = (Para **)malloc((s->nnew ? s->nnew : 1) * sizeof(Para *)))) return 0;
    if (s->nold > s->nnew && d->np + (s->nold - s->nnew) > d->pcap) {
      u32 nc = d->pcap * 2 + s->nold;
      Para **np = (Para **)realloc(d->p, nc * sizeof(Para *));
      if (!np) { free(t->old); return 0; }
      d->p = np; d->pcap = nc;
    }
    t->p0 = s->p0; t->nold = s->nnew; t->nnew = s->nold;
    for (k = 0; k < s->nnew; k++) t->old[k] = d->p[s->p0 + k];
    memmove(d->p + s->p0 + s->nold, d->p + s->p0 + s->nnew, (d->np - s->p0 - s->nnew) * sizeof(Para *));
    d->np = d->np - s->nnew + s->nold;
    for (k = 0; k < s->nold; k++) { d->p[s->p0 + k] = s->old[k]; touch(d->p[s->p0 + k]); }
    s->nold = 0;                       /* they belong to the document again */
    inv->ns++;
  }
  if (g->styles) {
    /* the style table back as it was; the inverse keeps the one there now */
    if (!(inv->styles = (Style *)malloc((d->nstyles ? d->nstyles : 1) * sizeof(Style)))) return 0;
    memcpy(inv->styles, d->styles, d->nstyles * sizeof(Style));
    inv->nstyles = d->nstyles;
    memcpy(d->styles, g->styles, g->nstyles * sizeof(Style));
    d->nstyles = g->nstyles;
  }
  inv->before = g->after; inv->after = g->before;
  changed(d);
  return 1;
}
/* undo (redo 0) or redo the newest group: replaying it pushes its inverse on the other stack, so
   undo and redo are one operation. Any group still open is closed first. *caret: where the action
   began. 0 if there was nothing to do, or memory ran out part way (the history is then cleared;
   the document is still consistent) */
static int undo_redo(Doc *d, Pos *caret, int redo)
{
  Undo *u = d->undo;
  Group g, inv, *slot;
  u->open = 0; u->depth = 0;
  if (redo ? !u->nr : !u->nu) return 0;
  g = redo ? u->r[--u->nr] : u->u[--u->nu];
  memset(&inv, 0, sizeof inv);
  if (!g.kind) g.kind = undo_classify(d, &g);
  inv.kind = g.kind; inv.serial = g.serial;
  if (!replay(d, &g, &inv)) {
    /* out of memory part way: each step is whole, so the document is consistent; history is not */
    group_free(&g); group_free(&inv); doc_clear_undo(d);
    return 0;
  }
  *caret = pos_clamp(d, g.before);
  group_free(&g);                       /* its paragraphs went back into the document */
  slot = redo ? push_group(&u->u, &u->nu, &u->ucap) : push_group(&u->r, &u->nr, &u->rcap);
  if (slot) *slot = inv; else group_free(&inv);
  return 1;
}
/* undo and redo one action; 0 if there was nothing to do */
int doc_undo(Doc *d, Pos *caret) { return undo_redo(d, caret, 0); }
int doc_redo(Doc *d, Pos *caret) { return undo_redo(d, caret, 1); }

/* ---- WP12: the history read (Undo and Redo lists, Go Back, Repeat). What an action did, from its
   steps: the paragraphs it saved against what is there now (the newest group only: the document
   shows its result; older ones are named when the next group starts) */
static int undo_classify(Doc *d, const Group *g)
{
  int i, text = 0, runs = 0, pf = 0;
  long grow = 0;
  if (g->typing) return UK_TYPING;
  if (g->styles) return UK_STYLE;
  for (i = 0; i < g->ns; i++) {
    const Step *s = &g->s[i];
    u32 k;
    if (s->p0 + s->nnew > d->np) return UK_EDIT;
    if (s->nold != s->nnew) text = 1;
    for (k = 0; k < s->nold; k++) grow -= (long)s->old[k]->len;
    for (k = 0; k < s->nnew; k++) grow += (long)d->p[s->p0 + k]->len;
    for (k = 0; k < s->nold && k < s->nnew; k++) {
      const Para *a = s->old[k], *b = d->p[s->p0 + k];
      if (a->len != b->len || memcmp(a->t, b->t, a->len)) text = 1;
      else if (a->nr != b->nr || memcmp(a->r, b->r, a->nr * sizeof(Run))) runs = 1;
      if (memcmp(&a->pf, &b->pf, sizeof a->pf)) pf = 1;
    }
  }
  if (text) return grow > 0 ? UK_INSERT : grow < 0 ? UK_DELETE : UK_REPLACE;
  return runs ? UK_FORMAT : pf ? UK_PARA : UK_EDIT;
}
int doc_undo_count(Doc *d, int redo) { return redo ? d->undo->nr : d->undo->nu; }
/* see doc.h */
int doc_undo_step(Doc *d, int redo, int i, Pos *before, Pos *after, u32 *serial)
{
  Undo *u = d->undo;
  Group *g;
  int n = redo ? u->nr : u->nu;
  if (i < 0 || i >= n) return -1;
  g = redo ? &u->r[n - 1 - i] : &u->u[n - 1 - i];
  if (before) *before = g->before;
  if (after) *after = g->after;
  if (serial) *serial = g->serial;
  if (g->kind) return g->kind;
  return redo || i ? UK_EDIT : undo_classify(d, g);
}
void doc_undo_kind(Doc *d, int kind) { if (d->undo->nu) d->undo->u[d->undo->nu - 1].kind = kind; }
/* see doc.h: each typing group newer than since, oldest first; the text a group typed is what its
   paragraph gained between its saved copy and the next copy of it (the next group's, or now) */
char *doc_undo_typed(Doc *d, u32 since)
{
  Undo *u = d->undo;
  int i0 = u->nu, i;
  Buf b;
  while (i0 > 0 && u->u[i0 - 1].typing && u->u[i0 - 1].serial > since && u->u[i0 - 1].ns == 1) i0--;
  if (i0 == u->nu) return NULL;
  buf_init(&b);
  for (i = i0; i < u->nu; i++) {
    const Para *a = u->u[i].s[0].old[0], *n;
    u32 pre = 0, suf = 0;
    if (u->u[i].s[0].nold != 1) continue;
    if (i + 1 < u->nu) { if (u->u[i + 1].s[0].nold != 1 || u->u[i + 1].s[0].p0 != u->u[i].s[0].p0) continue; n = u->u[i + 1].s[0].old[0]; }
    else { if (u->u[i].s[0].p0 >= d->np) continue; n = d->p[u->u[i].s[0].p0]; }
    if (n->len <= a->len) continue;
    while (pre < a->len && a->t[pre] == n->t[pre]) pre++;
    while (suf < a->len - pre && a->t[a->len - 1 - suf] == n->t[n->len - 1 - suf]) suf++;
    while (pre && (n->t[pre] & 0xC0) == 0x80) pre--;        /* (on a character boundary) */
    buf_add(&b, n->t + pre, n->len - suf - pre);
  }
  if (b.oom || !b.len) { buf_free(&b); return NULL; }
  buf_addc(&b, 0);
  return b.data;
}

/* ------------------------------------------------------------------ */
/* paragraphs */
/* a new, empty paragraph with formatting pf and its one run in cf (every paragraph has at least
   one run); the caller owns it. NULL if out of memory */
Para *para_new(const ParaFmt *pf, const CharFmt *cf)
{
  Para *p = (Para *)calloc(1, sizeof(Para));
  if (!p) return NULL;
  p->cap = 32; p->rcap = 4;
  p->t = (char *)malloc(p->cap);
  p->r = (Run *)malloc(p->rcap * sizeof(Run));
  if (!p->t || !p->r) { para_free(p); return NULL; }
  p->t[0] = 0;
  p->nr = 1; p->r[0].end = 0; p->r[0].f = *cf;
  p->pf = *pf;
  touch(p);
  return p;
}
/* a paragraph, its text and runs freed; NULL is fine */
void para_free(Para *p)
{
  if (!p) return;
  free(p->t); free(p->r); free(p);
}
/* a deep copy (text and runs), sized to fit. It keeps the original's ver: whoever puts it in the
   document touches it. NULL if out of memory */
Para *para_copy(const Para *s)
{
  Para *p = (Para *)malloc(sizeof(Para));
  if (!p) return NULL;
  *p = *s;
  p->cap = s->len + 1; p->rcap = s->nr;
  p->t = (char *)malloc(p->cap);
  p->r = (Run *)malloc(p->rcap * sizeof(Run));
  if (!p->t || !p->r) { para_free(p); return NULL; }
  memcpy(p->t, s->t, s->len + 1);
  memcpy(p->r, s->r, s->nr * sizeof(Run));
  return p;
}
/* room for extra more bytes of text and the NUL (at least doubling); 0 if out of memory */
static int text_room(Para *p, u32 extra)
{
  if (p->len + extra + 1 > p->cap) {
    u32 nc = p->cap * 2;
    char *nt;
    if (nc < p->len + extra + 1) nc = p->len + extra + 1;
    if (!(nt = (char *)realloc(p->t, nc))) return 0;
    p->t = nt; p->cap = nc;
  }
  return 1;
}
/* room for extra more runs; 0 if out of memory */
static int run_room(Para *p, u32 extra)
{
  if (p->nr + extra > p->rcap) {
    u32 nc = p->rcap * 2 + extra;
    Run *nr = (Run *)realloc(p->r, nc * sizeof(Run));
    if (!nr) return 0;
    p->r = nr; p->rcap = nc;
  }
  return 1;
}
/* two formats the same in every field (neighbouring runs that are get merged) */
static int cf_eq(const CharFmt *a, const CharFmt *b)
{
  return a->flags == b->flags && a->font == b->font && a->size == b->size && a->color == b->color && a->hl == b->hl && a->rev == b->rev && a->rid == b->rid &&
         a->cstyle == b->cstyle && a->fx == b->fx && a->ul == b->ul && a->spacing == b->spacing && a->scale == b->scale && a->position == b->position &&
         a->link == b->link && a->bmark == b->bmark;
}
/* the run covering byte off (past the end: the last run). Points into the paragraph, so it is good
   until the paragraph next changes */
CharFmt *para_fmt_at(Para *p, u32 off)
{
  u32 i;
  for (i = 0; i < p->nr; i++) if (p->r[i].end > off) return &p->r[i].f;
  return &p->r[p->nr - 1].f;
}
/* the run ending at or covering off (at 0 the first), so typing at a run boundary continues the
   run before it. Points into the paragraph */
CharFmt *para_fmt_before(Para *p, u32 off)
{
  u32 i;
  if (off == 0) return &p->r[0].f;
  for (i = 0; i < p->nr; i++) if (p->r[i].end >= off) return &p->r[i].f;
  return &p->r[p->nr - 1].f;
}
/* runs tidied after an edit: ends clamped to the text, empty runs dropped, equal neighbours merged
   and the last stretched to the end. A paragraph left with no text keeps its first run's
   formatting */
void para_normalise(Para *p)
{
  u32 i, o = 0, prev = 0;
  CharFmt keep = p->r[0].f;
  for (i = 0; i < p->nr; i++) {
    Run r = p->r[i];
    if (r.end > p->len) r.end = p->len;
    if (r.end <= prev) continue;             /* empty */
    if (o && cf_eq(&p->r[o - 1].f, &r.f)) p->r[o - 1].end = r.end;
    else p->r[o++] = r;
    prev = r.end;
  }
  if (!o) { p->r[0].end = 0; p->r[0].f = keep; o = 1; }
  p->r[o - 1].end = p->len;                  /* the runs always reach the end */
  p->nr = o;
}
/* make a run boundary at off */
static int split_run(Para *p, u32 off)
{
  u32 i, start = 0;
  for (i = 0; i < p->nr; i++) {
    if (off > start && off < p->r[i].end) {
      if (!run_room(p, 1)) return 0;
      memmove(p->r + i + 1, p->r + i, (p->nr - i) * sizeof(Run));
      p->r[i].end = off;
      p->nr++;
      return 1;
    }
    start = p->r[i].end;
  }
  return 1;
}
/* n bytes of s put in at off as a run of f (merged with equal neighbours afterwards) and the
   paragraph touched. No undo. 0 if off is past the end or out of memory */
static int para_insert_text(Para *p, u32 off, const char *s, u32 n, const CharFmt *f)
{
  u32 i, at;
  if (!n) return 1;
  if (off > p->len || !text_room(p, n) || !run_room(p, 2) || !split_run(p, off)) return 0;
  memmove(p->t + off + n, p->t + off, p->len - off + 1);
  memcpy(p->t + off, s, n);
  p->len += n;
  /* the new run goes after every run ending at or before off (split_run made a boundary there) */
  for (at = 0; at < p->nr && p->r[at].end <= off; at++) ;
  for (i = at; i < p->nr; i++) p->r[i].end += n;
  memmove(p->r + at + 1, p->r + at, (p->nr - at) * sizeof(Run));
  p->r[at].end = off + n; p->r[at].f = *f;
  p->nr++;
  para_normalise(p);
  touch(p);
  return 1;
}
/* bytes a..b taken out and the runs shrunk to match; a paragraph left empty keeps the formatting
   of the first character deleted. No undo */
static void para_delete_text(Para *p, u32 a, u32 b)
{
  u32 i;
  CharFmt f;
  if (b > p->len) b = p->len;
  if (a >= b) return;
  f = *para_fmt_at(p, a);
  memmove(p->t + a, p->t + b, p->len - b + 1);
  for (i = 0; i < p->nr; i++) {
    if (p->r[i].end >= b) p->r[i].end -= b - a;
    else if (p->r[i].end > a) p->r[i].end = a;
  }
  p->len -= b - a;
  if (!p->len) { p->nr = 1; p->r[0].end = 0; p->r[0].f = f; }
  para_normalise(p);
  touch(p);
}
/* text added at the end in f (no undo); 0 if out of memory */
int para_append(Para *p, const char *s, u32 n, const CharFmt *f) { return para_insert_text(p, p->len, s, n, f); }
/* append q's text from byte from, with its formatting */
static int para_append_from(Para *p, const Para *q, u32 from)
{
  u32 i, start = 0;
  for (i = 0; i < q->nr; i++) {
    u32 a = start > from ? start : from, b = q->r[i].end;
    if (b > a && !para_insert_text(p, p->len, q->t + a, b - a, &q->r[i].f)) return 0;
    start = q->r[i].end;
  }
  return 1;
}
/* p put in at index at (clamped to the end), the document owning it from now on. Not undoable and
   the document isn't marked changed: for building documents (the readers, doc_new). 0 if out of
   memory, p then still the caller's */
int doc_insert_para(Doc *d, u32 at, Para *p)
{
  if (at > d->np) at = d->np;
  if (d->np >= d->pcap) {
    u32 nc = d->pcap ? d->pcap * 2 : 64;
    Para **np = (Para **)realloc(d->p, nc * sizeof(Para *));
    if (!np) return 0;
    d->p = np; d->pcap = nc;
  }
  memmove(d->p + at + 1, d->p + at, (d->np - at) * sizeof(Para *));
  d->p[at] = p;
  d->np++;
  return 1;
}
/* paragraphs [at, at + n) freed and taken out; no undo (the caller has saved them already) */
static void doc_remove_paras(Doc *d, u32 at, u32 n)
{
  u32 i;
  for (i = 0; i < n; i++) para_free(d->p[at + i]);
  memmove(d->p + at, d->p + at + n, (d->np - at - n) * sizeof(Para *));
  d->np -= n;
}
/* split paragraph pi at off; the second half is a new paragraph after it */
static int split_para(Doc *d, u32 pi, u32 off)
{
  Para *p = d->p[pi], *q;
  const Style *st = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0];
  int atend = off >= p->len;
  CharFmt f = *para_fmt_before(p, off);
  ParaFmt pf = p->pf;
  if (atend && st->next != p->pf.style && st->next < d->nstyles) {    /* Return after a heading: Normal */
    const Style *ns = &d->styles[st->next];
    pf = ns->pf; pf.style = st->next; pf.cell = p->pf.cell; pf.outer = p->pf.outer;
    f = ns->cf;
  }
  /* the second part ends where the paragraph did (its section break); breaks before it and a drop cap stay with the first */
  pf.sect = p->pf.sect; pf.pagebreak = pf.colbreak = 0; pf.dropcap = pf.dropmargin = 0;
  if (!(q = para_new(&pf, &f))) return 0;
  if (!atend && !para_append_from(q, p, off)) { para_free(q); return 0; }
  if (!doc_insert_para(d, pi + 1, q)) { para_free(q); return 0; }
  p->pf.sect = 0;
  para_delete_text(p, off, p->len);
  touch(p);
  return 1;
}

/* ------------------------------------------------------------------ */
/* edits */
/* see doc.h. Typing coalesces: one character (up to 4 bytes) put in where the last one typed
   ended, in the same paragraph with nothing to redo, joins the last undo step instead of making
   one. A letter after a space starts a new step, so undo takes back a word at a time with the
   spaces after it. CR and CR LF count as newlines */
int doc_insert(Doc *d, Pos *at, const char *s, u32 n, const CharFmt *f)
{
  Para *p;
  Step *st;
  CharFmt fmt;
  u32 i = 0, pi, off, nparas = 1;
  Buf clean;
  int single;
  if (at->para >= d->np) return 0;
  p = d->p[at->para];
  if (at->off > p->len) at->off = p->len;
  fmt = f ? *f : *para_fmt_before(p, at->off);
  /* typing one character in a run of typing: no new undo step, the paragraph's copy is saved */
  single = n > 0 && n <= 4 && memchr(s, '\n', n) == NULL && memchr(s, '\r', n) == NULL;
  {
    Undo *u = d->undo;
    Group *g = (u->nu && !u->depth) ? &u->u[u->nu - 1] : NULL;
    int space = single && s[0] == ' ';
    if (single && g && g->typing && g->ns == 1 && g->s[0].p0 == at->para && g->s[0].nnew == 1 && pos_cmp(g->after, *at) == 0 &&
        !(g->lastspace && !space) && u->nr == 0) {
      st = NULL;                        /* coalesce */
      g->lastspace = space;
    } else {
      if (!(st = rec(d, at->para, 1, *at))) { close_auto(d); return 0; }
      if (!u->depth) { u->u[u->nu - 1].typing = single; u->u[u->nu - 1].lastspace = space; }
    }
  }
  buf_init(&clean);
  pi = at->para; off = at->off;
  /* walk the text: runs of characters go in, a newline splits */
  while (i <= n) {
    int brk = i == n || s[i] == '\n' || s[i] == '\r';
    if (!brk && ((u8)s[i] < 0x20 && s[i] != '\t' && s[i] != '\v')) { i++; continue; }
    if (!brk && s[i] == 0x7F) { i++; continue; }
    if (!brk) { buf_addc(&clean, s[i]); i++; continue; }
    if (clean.len) {
      if (clean.oom || !para_insert_text(d->p[pi], off, clean.data, (u32)clean.len, &fmt)) { buf_free(&clean); break; }
      off += (u32)clean.len;
      clean.len = 0;
    }
    if (i == n) break;
    if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') i++;
    i++;
    if (!split_para(d, pi, off)) break;
    pi++; off = 0; nparas++;
    {
      CharFmt nf = d->p[pi]->r[0].f;        /* a new heading-next paragraph has its own formatting */
      if (!f) fmt = nf;
    }
  }
  buf_free(&clean);
  if (st) st->nnew = nparas;
  else if (d->undo->nu) d->undo->u[d->undo->nu - 1].s[0].nnew = nparas;
  at->para = pi; at->off = off;
  if (d->undo->nu && !d->undo->depth) d->undo->u[d->undo->nu - 1].after = *at;
  changed(d);
  close_auto(d);
  return 1;
}
/* see doc.h. Joining paragraphs keeps the first one's formatting (the last one's if the first went
   whole), and the joined paragraph ends whatever section the last one did. One undo step, the
   caret after it at a; 0 if the range is empty */
int doc_delete(Doc *d, Pos a, Pos b)
{
  Step *st;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  if (!(st = rec(d, a.para, b.para - a.para + 1, b))) { close_auto(d); return 0; }
  if (a.para == b.para) para_delete_text(d->p[a.para], a.off, b.off);
  else {
    Para *p = d->p[a.para], *q = d->p[b.para];
    if (a.off == 0) p->pf = q->pf;          /* a whole first paragraph went: the merge keeps the last one's */
    p->pf.sect = q->pf.sect;                 /* it ends where the last one did (a section break between them goes) */
    para_delete_text(p, a.off, p->len);
    if (!para_append_from(p, q, b.off)) { close_auto(d); return 0; }
    doc_remove_paras(d, a.para + 1, b.para - a.para);
    touch(p);
  }
  st->nnew = 1;
  if (d->undo->nu && !d->undo->depth) d->undo->u[d->undo->nu - 1].after = a;
  changed(d);
  close_auto(d);
  return 1;
}
/* the fields mask names copied from v into c. The CM_ flag bits are the CF_ ones, so the flags go
   in one step; superscript and subscript exclude each other */
void cf_apply(CharFmt *c, unsigned mask, const CharFmt *v)
{
  c->flags = (u8)((c->flags & ~(mask & 0x7F)) | (v->flags & mask & 0x7F));
  if ((mask & CM_SUPER) && (v->flags & CF_SUPER)) c->flags &= ~CF_SUB;     /* the two exclude each other */
  if ((mask & CM_SUB) && (v->flags & CF_SUB)) c->flags &= ~CF_SUPER;
  if (mask & CM_FONT) c->font = v->font;
  if (mask & CM_SIZE) c->size = v->size;
  if (mask & CM_COLOR) c->color = v->color;
  if (mask & CM_HL) c->hl = v->hl;
  if (mask & CM_REV) { c->rev = v->rev; c->rid = v->rid; }
  if (mask & CM_CSTYLE) c->cstyle = v->cstyle;
  if (mask & CM_SMALLCAPS) c->fx = (u8)((c->fx & ~FX_SMALLCAPS) | (v->fx & FX_SMALLCAPS));
  if (mask & CM_DSTRIKE) c->fx = (u8)((c->fx & ~FX_DSTRIKE) | (v->fx & FX_DSTRIKE));
  if (mask & CM_CBORDER) c->fx = (u8)((c->fx & ~FX_BORDER) | (v->fx & FX_BORDER));
  if (mask & CM_UL) c->ul = v->ul;
  if (mask & CM_SPACING) c->spacing = v->spacing;
  if (mask & CM_SCALE) c->scale = v->scale;
  if (mask & CM_POSITION) c->position = v->position;
  if (mask & CM_LINK) c->link = v->link;
  if (mask & CM_BMARK) c->bmark = v->bmark;
}
/* the CM_ fields mask names set from val on a..b (either order), runs split at the ends; an empty
   paragraph's one run takes it too. One undo step; 0 if the range is empty */
int doc_format(Doc *d, Pos a, Pos b, unsigned mask, const CharFmt *val)
{
  u32 pi;
  Step *st;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  if (!(st = rec(d, a.para, b.para - a.para + 1, a))) { close_auto(d); return 0; }
  for (pi = a.para; pi <= b.para; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, i, start = 0;
    if (!p->len) { cf_apply(&p->r[0].f, mask, val); touch(p); continue; }
    if (!split_run(p, s) || !split_run(p, e)) break;
    for (i = 0; i < p->nr; i++) {
      if (start >= s && p->r[i].end <= e) cf_apply(&p->r[i].f, mask, val);
      start = p->r[i].end;
    }
    para_normalise(p);
    touch(p);
  }
  changed(d);
  close_auto(d);
  return 1;
}
/* see doc.h. flag is a CF_ bit, used as its CM_ mask too (they match); via doc_format, so 0 for an
   empty range */
int doc_toggle(Doc *d, Pos a, Pos b, unsigned flag)
{
  u32 pi;
  int all = 1;
  CharFmt v;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  for (pi = a.para; pi <= b.para && all; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, i, start = 0;
    for (i = 0; i < p->nr; i++) {
      if (p->r[i].end > s && start < e && !(p->r[i].f.flags & flag)) { all = 0; break; }
      start = p->r[i].end;
    }
  }
  memset(&v, 0, sizeof v);
  v.flags = (u8)(all ? 0 : flag);
  return doc_format(d, a, b, flag, &v);
}
/* each run between a and b (split at them) changed by fn: one undo step */
static int each_run(Doc *d, Pos a, Pos b, void (*fn)(Doc *, Para *, CharFmt *, void *), void *arg)
{
  u32 pi;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  if (!rec(d, a.para, b.para - a.para + 1, a)) { close_auto(d); return 0; }
  for (pi = a.para; pi <= b.para; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, i, start = 0;
    if (!p->len) { fn(d, p, &p->r[0].f, arg); touch(p); continue; }
    if (!split_run(p, s) || !split_run(p, e)) break;
    for (i = 0; i < p->nr; i++) {
      if (start >= s && p->r[i].end <= e) fn(d, p, &p->r[i].f, arg);
      start = p->r[i].end;
    }
    para_normalise(p);
    touch(p);
  }
  changed(d);
  close_auto(d);
  return 1;
}
/* one run's size a step along Word's list of sizes (half-points: 8 pt to 72 pt); beyond it 10 pt
   steps up to 1600 pt, below it 1 pt steps down to 1 pt */
static void grow_one(Doc *d, Para *p, CharFmt *f, void *arg)
{
  static const u16 steps[] = { 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 52, 56, 72, 96, 144 };
  int dir = *(int *)arg, i, n = (int)(sizeof steps / sizeof steps[0]);
  (void)d; (void)p;
  if (dir > 0) {
    for (i = 0; i < n && steps[i] <= f->size; i++) ;
    f->size = i < n ? steps[i] : (u16)(f->size + 20 > 3200 ? 3200 : f->size + 20);
  } else {
    for (i = n - 1; i >= 0 && steps[i] >= f->size; i--) ;
    f->size = i >= 0 ? steps[i] : (u16)(f->size > 3 ? f->size - 2 : 2);
  }
}
/* see doc.h; one undo step */
int doc_grow(Doc *d, Pos a, Pos b, int dir) { return each_run(d, a, b, grow_one, &dir); }
/* a run's formatting replaced by its paragraph style's, which drops its character style and any
   tracked-change mark too */
static void clear_one(Doc *d, Para *p, CharFmt *f, void *arg)
{
  (void)arg;
  *f = d->styles[p->pf.style < d->nstyles ? p->pf.style : 0].cf;
}
/* see doc.h; one undo step, 0 if the range is empty */
int doc_clear_format(Doc *d, Pos a, Pos b) { return each_run(d, a, b, clear_one, NULL); }

/* letters whose case changes in place: ASCII, and Latin-1's (U+00C0-U+00FE but for the signs) */
static int is_lower_at(const u8 *t) { return (*t >= 'a' && *t <= 'z') || (t[0] == 0xC3 && t[1] >= 0xA0 && t[1] <= 0xBE && t[1] != 0xB7 && t[1] != 0x9F); }
static int is_upper_at(const u8 *t) { return (*t >= 'A' && *t <= 'Z') || (t[0] == 0xC3 && t[1] >= 0x80 && t[1] <= 0x9E && t[1] != 0x97); }
/* the letter at t made upper (else lower) case in place. In ASCII and Latin-1 the cases are 0x20
   apart, and Latin-1's letters are all two bytes (C3 xx) either way, so the text's length never
   changes */
static void set_case(u8 *t, int upper)
{
  if (upper && is_lower_at(t)) { if (t[0] == 0xC3) t[1] -= 0x20; else t[0] -= 0x20; }
  else if (!upper && is_upper_at(t)) { if (t[0] == 0xC3) t[1] += 0x20; else t[0] += 0x20; }
}
/* a letter of either case, or the sharp s (U+00DF), which has no capital here */
static int letter_at(const u8 *t) { return is_lower_at(t) || is_upper_at(t) || (t[0] == 0xC3 && t[1] == 0x9F); }
/* see doc.h; one undo step. Sentence case capitalises the first letter of each paragraph and after
   . ! or ?, lowering the rest; title case each word's first letter, an apostrophe (straight or
   U+2019) not starting a new word */
int doc_change_case(Doc *d, Pos a, Pos b, int mode)
{
  u32 pi;
  int start = 1;                          /* the next letter starts a sentence (or a word) */
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  if (!rec(d, a.para, b.para - a.para + 1, a)) { close_auto(d); return 0; }
  for (pi = a.para; pi <= b.para; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, k;
    u8 *t = (u8 *)p->t;
    if (mode == CASE_SENTENCE) start = 1;
    if (mode == CASE_TITLE) start = s == 0 || !letter_at(t + utf8_prev(p->t, s));
    for (k = s; k < e; k = (u32)utf8_next(p->t, p->len, k)) {
      int letter = letter_at(t + k);
      switch (mode) {
      case CASE_LOWER: set_case(t + k, 0); break;
      case CASE_UPPER: set_case(t + k, 1); break;
      case CASE_TOGGLE: if (is_lower_at(t + k)) set_case(t + k, 1); else set_case(t + k, 0); break;
      case CASE_TITLE:
        if (letter) { set_case(t + k, start); start = 0; }
        else start = t[k] != '\'' && !(t[k] == 0xE2 && k + 2 < p->len && t[k + 1] == 0x80 && t[k + 2] == 0x99);   /* not inside "don't" */
        break;
      case CASE_SENTENCE:
        if (letter) { set_case(t + k, start); start = 0; }
        else if (t[k] == '.' || t[k] == '!' || t[k] == '?') start = 1;
        break;
      }
    }
    touch(p);
  }
  changed(d);
  close_auto(d);
  return 1;
}
/* see doc.h. NULL author or date count as empty. Also 0 if out of memory or 65535 are in use, so
   such changes fall back on the first author */
int doc_add_rev(Doc *d, const char *author, const char *date)
{
  int k;
  void *n;
  for (k = 0; k < d->nrevs; k++) if (!strcmp(d->revs[k].author, author ? author : "") && !strcmp(d->revs[k].date, date ? date : "")) return k;
  if (d->nrevs >= 65535 || !(n = realloc(d->revs, (d->nrevs + 1) * sizeof d->revs[0]))) return 0;
  d->revs = n;
  memset(&d->revs[d->nrevs], 0, sizeof d->revs[0]);
  strncpy(d->revs[d->nrevs].author, author ? author : "", sizeof d->revs[0].author - 1);
  strncpy(d->revs[d->nrevs].date, date ? date : "", sizeof d->revs[0].date - 1);
  return d->nrevs++;
}
/* the runs between a and b, last first: (paragraph, start, end, format) to fn, which may change the
   text after them only */
typedef int (*RevFn)(Doc *d, u32 pi, u32 s, u32 e, CharFmt *f, void *arg);
/* returns whether fn did anything (it returns non-zero for that) */
static int each_run_back(Doc *d, Pos a, Pos b, RevFn fn, void *arg)
{
  u32 pi;
  int any = 0;
  for (pi = b.para + 1; pi-- > a.para; ) {
    u32 s = pi == a.para ? a.off : 0, cur = pi == b.para ? b.off : d->p[pi]->len;
    /* from the end back: fn changes only the text from x0 on, so what is before it stays put */
    while (cur > s) {
      Para *p = d->p[pi];
      u32 r = 0, rs, x0;
      CharFmt f;
      while (r + 1 < p->nr && p->r[r].end < cur) r++;
      rs = r ? p->r[r - 1].end : 0;
      x0 = rs > s ? rs : s;
      f = p->r[r].f;
      any |= fn(d, pi, x0, cur, &f, arg);
      cur = x0;
    }
  }
  return any;
}
/* one run of a tracked deletion: anyone's insertion simply goes, text already deleted stays, the
   rest is marked deleted by rid (*arg) */
static int track_one(Doc *d, u32 pi, u32 s, u32 e, CharFmt *f, void *arg)
{
  Pos a, b;
  a.para = b.para = pi; a.off = s; b.off = e;
  if (f->rev == REV_INS) return doc_delete(d, a, b);
  if (f->rev == REV_DEL) return 0;
  {
    CharFmt v = *f;
    v.rev = REV_DEL; v.rid = (u16)*(int *)arg;
    return doc_format(d, a, b, CM_REV, &v);
  }
}
/* see doc.h. The runs go last first, so deleting inserted text doesn't move what is still to do; 0
   if nothing changed */
int doc_track_delete(Doc *d, Pos a, Pos b, int rid)
{
  int any;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  doc_begin(d, a);
  any = each_run_back(d, a, b, track_one, &rid);
  doc_end(d, a);
  return any;
}
/* one run of doc_resolve: made plain text (an accepted insertion or rejected deletion) or deleted;
   1 if it was a tracked change at all */
static int resolve_one(Doc *d, u32 pi, u32 s, u32 e, CharFmt *f, void *arg)
{
  int accept = *(int *)arg;
  Pos a, b;
  a.para = b.para = pi; a.off = s; b.off = e;
  if (!f->rev) return 0;
  if ((f->rev == REV_INS) == (accept != 0)) {
    CharFmt v = *f;
    v.rev = REV_NONE; v.rid = 0;
    doc_format(d, a, b, CM_REV, &v);
  } else doc_delete(d, a, b);
  return 1;
}
/* see doc.h; through each_run_back, in one undo step */
int doc_resolve(Doc *d, Pos a, Pos b, int accept)
{
  int any;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  doc_begin(d, a);
  any = each_run_back(d, a, b, resolve_one, &accept);
  doc_end(d, a);
  return any;
}
/* see doc.h. Goes round the document once, wrapping at its ends; in p's own paragraph it looks
   only after p (forward) or before it (back) until it comes back round. A change over several runs
   is found one run at a time */
int doc_next_change(Doc *d, Pos p, int dir, Pos *a, Pos *b)
{
  u32 pi = p.para, n;
  p = pos_clamp(d, p);
  for (n = 0; n <= d->np; n++) {
    Para *q = d->p[pi];
    u32 r;
    if (dir > 0) {
      for (r = 0; r < q->nr; r++) {
        u32 rs = r ? q->r[r - 1].end : 0;
        if (q->r[r].f.rev && q->r[r].end > rs && (n > 0 || rs >= p.off)) {
          a->para = b->para = pi; a->off = rs; b->off = q->r[r].end;
          return 1;
        }
      }
      if (++pi >= d->np) pi = 0;
    } else {
      for (r = q->nr; r-- > 0; ) {
        u32 rs = r ? q->r[r - 1].end : 0;
        if (q->r[r].f.rev && q->r[r].end > rs && (n > 0 || q->r[r].end <= p.off)) {
          a->para = b->para = pi; a->off = rs; b->off = q->r[r].end;
          return 1;
        }
      }
      pi = pi ? pi - 1 : d->np - 1;
    }
  }
  return 0;
}
/* paragraphs pa..pb in alphabetical order of their text (not in tables): two compared by their
   bytes with ASCII letters folded to lower case, so accented letters sort after z */
static int para_cmp(const Para *x, const Para *y)
{
  const u8 *a = (const u8 *)x->t, *b = (const u8 *)y->t;
  for (;; a++, b++) {
    int ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb || !ca) return ca - cb;
  }
}
/* see doc.h. Sorted copies replace the paragraphs through doc_replace: one undo step. 0 if the
   range is one paragraph, past the end, has a table in it, or memory ran out */
int doc_sort(Doc *d, u32 pa, u32 pb)
{
  Para **nw;
  u32 n, i, j;
  Pos where;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pb >= d->np || pa == pb) return 0;
  for (i = pa; i <= pb; i++) if (d->p[i]->pf.cell.table) return 0;
  n = pb - pa + 1;
  if (!(nw = (Para **)malloc(n * sizeof(Para *)))) return 0;
  for (i = 0; i < n; i++) if (!(nw[i] = para_copy(d->p[pa + i]))) { while (i) para_free(nw[--i]); free(nw); return 0; }
  for (i = 1; i < n; i++) {                /* insertion sort: stable, and lists are short */
    Para *x = nw[i];
    for (j = i; j > 0 && para_cmp(nw[j - 1], x) > 0; j--) nw[j] = nw[j - 1];
    nw[j] = x;
  }
  where.para = pa; where.off = 0;
  i = (u32)doc_replace(d, pa, n, nw, n, where);
  free(nw);
  return (int)i;
}
/* the PM_ groups mask names copied from v into p (each group several fields that go together, as
   the three indents) */
void pf_apply(ParaFmt *p, unsigned mask, const ParaFmt *v)
{
  if (mask & PM_STYLE) p->style = v->style;
  if (mask & PM_ALIGN) p->align = v->align;
  if (mask & PM_INDENT) { p->left = v->left; p->right = v->right; p->first = v->first; }
  if (mask & PM_SPACING) { p->before = v->before; p->after = v->after; }
  if (mask & PM_LINE) { p->line = v->line; p->linerule = v->linerule; }
  if (mask & PM_TABS) { memcpy(p->tabs, v->tabs, sizeof p->tabs); memcpy(p->tabkind, v->tabkind, sizeof p->tabkind); memcpy(p->tablead, v->tablead, sizeof p->tablead); }
  if (mask & PM_KEEP) p->keepnext = v->keepnext;
  if (mask & PM_LIST) { p->list = v->list; p->level = v->level; }
  if (mask & PM_BREAK) { p->pagebreak = v->pagebreak; p->colbreak = v->colbreak; }
  if (mask & PM_CELL) { p->cell = v->cell; p->outer = v->outer; }
  if (mask & PM_SECT) p->sect = v->sect;
  if (mask & PM_DROP) { p->dropcap = v->dropcap; p->dropmargin = v->dropmargin; }
  if (mask & PM_BORDER) { memcpy(p->bstyle, v->bstyle, sizeof p->bstyle); memcpy(p->bwidth, v->bwidth, sizeof p->bwidth); memcpy(p->bspace, v->bspace, sizeof p->bspace); memcpy(p->bcolor, v->bcolor, sizeof p->bcolor); }
  if (mask & PM_SHADE) p->shade = v->shade;
  if (mask & PM_PAGINATION) { p->keeplines = v->keeplines; p->nowidow = v->nowidow; }
  if (mask & PM_CONTEXT) p->contextual = v->contextual;
}
/* the PM_ groups mask names set from val on paragraphs pa..pb (either order, pb clamped to the
   last). One undo step */
int doc_para_format(Doc *d, u32 pa, u32 pb, unsigned mask, const ParaFmt *val)
{
  u32 i;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pb >= d->np) pb = d->np - 1;
  if (pa > pb || !rec(d, pa, pb - pa + 1, para_pos(pa))) { close_auto(d); return 0; }
  for (i = pa; i <= pb; i++) { pf_apply(&d->p[i]->pf, mask, val); touch(d->p[i]); }
  changed(d);
  close_auto(d);
  return 1;
}
/* see doc.h. The paragraph's formatting becomes the style's, except its table cell, section break
   and drop cap, which stay. One undo step; 0 if style isn't a paragraph style */
int doc_apply_style(Doc *d, u32 pa, u32 pb, int style)
{
  u32 i, k;
  if (style < 0 || style >= d->nstyles || d->styles[style].type != ST_PARA) return 0;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pb >= d->np) pb = d->np - 1;
  if (pa > pb || !rec(d, pa, pb - pa + 1, para_pos(pa))) { close_auto(d); return 0; }
  for (i = pa; i <= pb; i++) {
    Para *p = d->p[i];
    const Style *os = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0], *ns = &d->styles[style];
    /* the text: the old style's look becomes the new one's (character styles and direct formatting stay) */
    for (k = 0; k < p->nr; k++) {
      CharFmt *c = &p->r[k].f;
      int cs = c->cstyle;
      const CharFmt *csf = cs && cs < d->nstyles && d->styles[cs].type == ST_CHAR ? &d->styles[cs].cf : NULL;
      unsigned em = csf ? d->styles[cs].ecmask : 0;
      restyle_run(c, &os->cf, &ns->cf, csf, em, csf, em);
    }
    para_normalise(p);
    {
      ParaFmt keep = p->pf;
      p->pf = ns->pf;
      p->pf.cell = keep.cell; p->pf.outer = keep.outer; p->pf.sect = keep.sect;
      p->pf.dropcap = keep.dropcap; p->pf.dropmargin = keep.dropmargin;
    }
    p->pf.style = (u8)style;
    touch(p);
  }
  changed(d);
  close_auto(d);
  return 1;
}

/* ------------------------------------------------------------------ */
/* the document */
/* see doc.h; l copied in. Not undoable: lists are only ever added to */
int doc_add_list(Doc *d, const ListDef *l)
{
  ListDef *nl;
  if (d->nlists >= MAX_LISTS) return 0;
  if (!(nl = (ListDef *)realloc(d->lists, (d->nlists + 1) * sizeof(ListDef)))) return 0;
  d->lists = nl;
  d->lists[d->nlists] = *l;
  return ++d->nlists;
}
/* see doc.h; tp copied in. Also 0 if 65535 are in use. Not undoable: tables are only ever added to */
int doc_add_table(Doc *d, const Table *tp)
{
  Table *nt, tc = *tp, *t = &tc;          /* tp may point into d->tables, which moves */
  if (d->ntables >= 65535) return 0;
  if (!(nt = (Table *)realloc(d->tables, (d->ntables + 1) * sizeof(Table)))) return 0;
  d->tables = nt;
  d->tables[d->ntables] = *t;
  return ++d->ntables;
}
/* see doc.h; ids compared exactly (case matters) */
int doc_tstyle(Doc *d, const char *id)
{
  int i;
  for (i = 0; i < d->ntstyles; i++) if (!strcmp(d->tstyles[i].id, id)) return i + 1;
  return 0;
}
/* see doc.h; sp copied first, as it may point into d->tstyles */
int doc_add_tstyle(Doc *d, const TableStyle *sp)
{
  TableStyle *ns, sc = *sp, *s = &sc;
  int k = doc_tstyle(d, s->id);
  if (k) { d->tstyles[k - 1] = *s; return k; }
  if (d->ntstyles >= MAX_TSTYLES) return 0;
  if (!(ns = (TableStyle *)realloc(d->tstyles, (d->ntstyles + 1) * sizeof(TableStyle)))) return 0;
  d->tstyles = ns;
  d->tstyles[d->ntstyles] = *s;
  return ++d->ntstyles;
}
/* a section with no settings of its own: one column, starting on a new page, 720 twips (half an
   inch) between columns, no line numbers */
void sect_default(SectFmt *s)
{
  memset(s, 0, sizeof *s);
  s->start = SB_NEXT; s->cols = 1; s->valign = VA_TOP; s->linenum = LN_NONE; s->lnby = 1; s->colgap = 720; s->lndist = 360;
}
static Story *hf_copy(const Story *src)       /* 2 * HF_N stories, NULL if out of memory */
{
  Story *c = (Story *)calloc(2 * HF_N, sizeof(Story));
  int k;
  if (!c) return NULL;
  for (k = 0; k < 2 * HF_N; k++) {
    Story *one = story_copy(&src[k]);
    if (!one) { while (k-- > 0) story_free(&c[k]); free(c); return NULL; }
    c[k] = *one; free(one);
  }
  return c;
}
/* a section's own headers and footers (2 * HF_N stories) freed; NULL is fine */
static void hf_free(Story *hf)
{
  int k;
  if (!hf) return;
  for (k = 0; k < 2 * HF_N; k++) story_free(&hf[k]);
  free(hf);
}
/* see doc.h. s is copied with its own copies of its headers and footers; columns are clamped to
   1..MAX_SECT_COLS and lnby made at least 1. Also 0 if 65535 are in use */
int doc_add_sect(Doc *d, const SectFmt *s)
{
  SectFmt *ns;
  Story *hf = NULL;
  if (d->nsects >= 65535) return 0;
  if (s->hf && !(hf = hf_copy(s->hf))) return 0;
  if (!(ns = (SectFmt *)realloc(d->sects, (d->nsects + 1) * sizeof(SectFmt)))) { hf_free(hf); return 0; }
  d->sects = ns;
  d->sects[d->nsects] = *s;
  ns[d->nsects].hf = hf;
  if (ns[d->nsects].cols < 1) ns[d->nsects].cols = 1;
  if (ns[d->nsects].cols > MAX_SECT_COLS) ns[d->nsects].cols = MAX_SECT_COLS;
  if (!ns[d->nsects].lnby) ns[d->nsects].lnby = 1;
  return ++d->nsects;
}
/* see doc.h: the first paragraph from pi on that holds a section format, else the last paragraph */
u32 doc_sect_end(Doc *d, u32 pi)
{
  while (pi + 1 < d->np && !d->p[pi]->pf.sect) pi++;
  return pi < d->np ? pi : d->np - 1;
}
/* see doc.h. A section without a format (the last, usually) gets a static default one. The pointer
   may be into Doc.sects, which moves when a section is added */
const SectFmt *doc_sect(Doc *d, u32 pi)
{
  static SectFmt def;
  u32 e;
  if (!def.cols) sect_default(&def);
  if (!d->np) return &def;
  e = doc_sect_end(d, pi);
  return d->p[e]->pf.sect && d->p[e]->pf.sect <= d->nsects ? &d->sects[d->p[e]->pf.sect - 1] : &def;
}
/* pi's section is the last (whose page setup, headers and footers are the document's); also 1 for
   an empty document */
int doc_last_sect(Doc *d, u32 pi) { return !d->np || doc_sect_end(d, pi) + 1 >= d->np; }
/* a section's page setup: its own if it has one and isn't the last, else the document's */
const PageSetup *sect_page(const Doc *d, const SectFmt *s, int last) { return !last && s && s->haspage ? &s->page : &d->page; }
/* a section's header (footer: footer) of kind which, HF_DEFAULT if which is out of range: its own,
   else the document's */
Story *sect_story(Doc *d, const SectFmt *s, int last, int footer, int which)
{
  if (which < 0 || which >= HF_N) which = HF_DEFAULT;
  if (!last && s && s->hf) return &s->hf[(footer ? HF_N : 0) + which];
  return footer ? &d->ftr[which] : &d->hdr[which];
}
/* whether a section's first page has its own header and footer (the last section's is Doc.titlepg) */
int sect_titlepg(const Doc *d, const SectFmt *s, int last) { return last || !s ? d->titlepg : s->titlepg; }
/* see doc.h. Not while a story is being edited (the document's headers and footers are put aside
   then). Marks the document changed; not undoable */
int doc_sect_own_hf(Doc *d, int k, int own)
{
  SectFmt *s;
  if (k < 1 || k > d->nsects || d->edit.kind) return 0;
  s = &d->sects[k - 1];
  if (!own) { hf_free(s->hf); s->hf = NULL; }
  else if (!s->hf) {
    Story both[2 * HF_N];
    int w;
    for (w = 0; w < HF_N; w++) { both[w] = d->hdr[w]; both[HF_N + w] = d->ftr[w]; }
    if (!(s->hf = hf_copy(both))) return 0;
  }
  d->ver++; d->dirty = 1;
  return 1;
}
/* see doc.h; NULL too for a table number out of range */
Table *doc_table(Doc *d, const ParaFmt *pf)
{
  return pf->cell.table && pf->cell.table <= d->ntables ? &d->tables[pf->cell.table - 1] : NULL;
}
/* the index in a 4-byte reference (F3/F4 xx xx xx): the low 14 bits of its code point, which is
   why pictures, notes and comments have 16384 places each. Comment marks are read with it too */
int pic_index(const char *t)
{
  return (((u8)t[1] & 0x03) << 12) | (((u8)t[2] & 0x3F) << 6) | ((u8)t[3] & 0x3F);
}
/* footnote and endnote references read and made as pictures' are (out: 4 bytes and a NUL) */
int note_index(const char *t) { return pic_index(t); }
void note_utf8(char *out, int index) { out[utf8_put(out, 0xF4000u + (u32)index)] = 0; }
void enote_utf8(char *out, int index) { out[utf8_put(out, 0x100000u + (u32)index)] = 0; }
/* see doc.h; -1 if MAX_NOTES are in use or out of memory. Not undoable; the caller puts its
   reference in */
int doc_add_endnote(Doc *d)
{
  Story *n;
  if (d->nenotes >= MAX_NOTES || !(n = (Story *)realloc(d->enotes, (d->nenotes + 1) * sizeof(Story)))) return -1;
  d->enotes = n;
  d->enotes[d->nenotes].p = NULL; d->enotes[d->nenotes].np = 0;
  return d->nenotes++;
}
/* see doc.h; -1 if MAX_NOTES are in use or out of memory. Not undoable; the caller puts its
   reference in */
int doc_add_note(Doc *d)
{
  Story *n;
  if (d->nnotes >= MAX_NOTES || !(n = (Story *)realloc(d->notes, (d->nnotes + 1) * sizeof(Story)))) return -1;
  d->notes = n;
  d->notes[d->nnotes].p = NULL; d->notes[d->nnotes].np = 0;
  return d->nnotes++;
}
/* comment k's start mark (end: its end mark): 4 bytes and a NUL */
void cmark_utf8(char *out, int index, int end) { out[utf8_put(out, (end ? 0xFC000u : 0xF8000u) + (u32)index)] = 0; }
/* see doc.h. The strings are copied (cut to fit, NULL as empty). Not undoable */
int doc_add_comment(Doc *d, const char *author, const char *initials, const char *date)
{
  Comment *n, *c;
  if (d->ncomments >= MAX_COMMENTS || !(n = (Comment *)realloc(d->comments, (d->ncomments + 1) * sizeof(Comment)))) return -1;
  d->comments = n;
  c = &d->comments[d->ncomments];
  memset(c, 0, sizeof *c);
  strncpy(c->author, author ? author : "", sizeof c->author - 1);
  strncpy(c->initials, initials ? initials : "", sizeof c->initials - 1);
  strncpy(c->date, date ? date : "", sizeof c->date - 1);
  return d->ncomments++;
}
/* see doc.h. If the marks can't go in, the entry stays anyway (with no marks it is not saved) */
int doc_comment(Doc *d, Pos a, Pos b, const char *text, const char *author, const char *initials, const char *date)
{
  int k, st;
  char m[8];
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if ((k = doc_add_comment(d, author, initials, date)) < 0) return -1;
  st = doc_style(d, "CommentText");
  if (!story_set_text(d, &d->comments[k].text, text, st < 0 ? 0 : st, AL_LEFT)) return -1;
  doc_begin(d, a);
  /* the end first, so a stays where it is */
  cmark_utf8(m, k, 1);
  if (!doc_insert(d, &b, m, 4, NULL)) { doc_end(d, a); return -1; }
  cmark_utf8(m, k, 0);
  doc_insert(d, &a, m, 4, NULL);
  doc_end(d, a);
  return k;
}
/* see doc.h; a scan of the whole body for the mark's 4 bytes */
int doc_comment_mark(Doc *d, int k, int end, Pos *at)
{
  char m[8];
  u32 i;
  cmark_utf8(m, k, end);
  for (i = 0; i < d->np; i++) {
    const char *f = d->p[i]->len >= 4 ? (const char *)memchr(d->p[i]->t, 0xF3, d->p[i]->len) : NULL;
    while (f && f + 4 <= d->p[i]->t + d->p[i]->len) {
      if (!memcmp(f, m, 4)) { at->para = i; at->off = (u32)(f - d->p[i]->t); return 1; }
      f = (const char *)memchr(f + 1, 0xF3, (size_t)(d->p[i]->t + d->p[i]->len - (f + 1)));
    }
  }
  return 0;
}
/* see doc.h. It looks back 300 paragraphs at most, so a comment starting further back isn't found;
   *end is *start if the end mark has gone. Not reentrant: it keeps a static scratch array */
int doc_comment_at(Doc *d, Pos p, Pos *start, Pos *end)
{
  /* back from p: a start whose end was not passed is open at p (as far back as 300 paragraphs) */
  static u8 *seen;
  static int seencap;
  u32 pi = p.para, lim = p.para > 300 ? p.para - 300 : 0;
  int k = -1;
  if (pi >= d->np) return -1;
  if (seencap < d->ncomments) { u8 *ns = (u8 *)realloc(seen, (size_t)d->ncomments + 64); if (!ns) return -1; seen = ns; seencap = d->ncomments + 64; }
  if (seen) memset(seen, 0, (size_t)seencap);
  for (;;) {
    Para *q = d->p[pi];
    u32 o = pi == p.para ? (p.off < q->len ? p.off : q->len) : q->len;
    while (o >= 4) {
      o = (u32)utf8_prev(q->t, o);
      if (IS_MARK(q->t + o)) {
        int n = note_index(q->t + o);
        if (n < d->ncomments) {
          if (IS_CEND(q->t + o)) seen[n] = 1;
          else if (!seen[n]) { k = n; if (start) { start->para = pi; start->off = o; } goto found; }
        }
      }
    }
    if (pi == lim) break;
    pi--;
  }
  return -1;
found:
  if (end && !doc_comment_mark(d, k, 1, end)) *end = *start;
  return k;
}
/* see doc.h. The end mark goes first and the start is looked for again after. The entry stays
   (with no marks it is not saved); 0 if neither mark is in the text */
int doc_delete_comment(Doc *d, int k)
{
  Pos a, b, a2, b2;
  int hs = doc_comment_mark(d, k, 0, &a), he = doc_comment_mark(d, k, 1, &b);
  if (!hs && !he) return 0;
  doc_begin(d, hs ? a : b);
  if (he) { b2 = b; b2.off += 4; doc_delete(d, b, b2); }
  if (hs && doc_comment_mark(d, k, 0, &a)) { a2 = a; a2.off += 4; doc_delete(d, a, a2); }
  doc_end(d, hs ? a : b);
  return 1;
}
/* a reference to Doc.images[index]: 4 bytes and a NUL */
void pic_utf8(char *out, int index)
{
  out[utf8_put(out, 0xF0000u + (u32)index)] = 0;
}
/* see doc.h. The default size is the pixels at 96 dpi, 15 twips each. Not undoable; the caller
   puts the reference in */
int doc_add_image(Doc *d, u8 *data, u32 len, i32 cx, i32 cy)
{
  Image *ni, *im;
  u32 w, h;
  int t = data ? img_info(data, len, &w, &h) : IMG_NONE;
  if (t == IMG_NONE || d->nimages >= MAX_IMAGES || !(ni = (Image *)realloc(d->images, (d->nimages + 1) * sizeof(Image)))) { free(data); return -1; }
  d->images = ni;
  im = &d->images[d->nimages];
  img_defaults(im);
  im->data = data; im->len = len; im->type = (u8)t; im->pw = w; im->ph = h;
  im->cx = cx > 0 ? cx : (i32)w * 15; im->cy = cy > 0 ? cy : (i32)h * 15;
  return d->nimages++;
}
/* see doc.h: zeroed, then in line, no outline, no fill, a rectangle */
void img_defaults(Image *im)
{
  memset(im, 0, sizeof *im);
  im->kind = OBJ_PICTURE; im->wrap = WRAP_INLINE;
  im->hrel = POS_COLUMN; im->vrel = POS_PARA;
  im->dl = im->dr = 180;                    /* Word's eighth of an inch at the sides */
  im->lstyle = PB_NONE; im->lcolor = 0; im->fill = COLOR_AUTO; im->shape = SH_RECT;
}
/* see doc.h; NULL s gives an empty story. A paragraph that can't be copied is left out rather than
   failing the copy. Free it with story_free, then free */
Story *story_copy(const Story *s)
{
  Story *c = (Story *)calloc(1, sizeof(Story));
  u32 i;
  if (!c) return NULL;
  if (s && s->np && !(c->p = (Para **)malloc(s->np * sizeof(Para *)))) { free(c); return NULL; }
  for (i = 0; s && i < s->np; i++) if ((c->p[c->np] = para_copy(s->p[i]))) c->np++;
  return c;
}
/* see doc.h. A plain copy first, then what dst must own copied; a group's objects are cloned the
   same way, recursively */
int img_clone(Image *dst, const Image *src, int own)
{
  int k;
  *dst = *src;
  dst->text = NULL; dst->kids = NULL; dst->nkids = 0; dst->poly = NULL; dst->npoly = 0;
  if (own) {
    dst->data = NULL; dst->shared = 0;
    if (src->data && (dst->data = (u8 *)malloc(src->len ? src->len : 1))) memcpy(dst->data, src->data, src->len);
    if (src->data && !dst->data) return 0;
  } else dst->shared = src->data != NULL;     /* the file stays with the entry that brought it */
  if (src->text && !(dst->text = story_copy(src->text))) goto oom;
  if (src->npoly) {
    if (!(dst->poly = (i32 *)malloc(src->npoly * 2 * sizeof(i32)))) goto oom;
    memcpy(dst->poly, src->poly, src->npoly * 2 * sizeof(i32));
    dst->npoly = src->npoly;
  }
  if (src->nkids) {
    if (!(dst->kids = (Image *)calloc(src->nkids, sizeof(Image)))) goto oom;
    for (k = 0; k < src->nkids; k++) {
      if (!img_clone(&dst->kids[k], &src->kids[k], own)) goto oom;
      dst->nkids = k + 1;
    }
  }
  return 1;
oom:
  img_release(dst);
  return 0;
}
/* see doc.h. Recursive for groups; the Image itself isn't freed, its pointers are left NULL */
void img_release(Image *im)
{
  int k;
  if (!im->shared) free(im->data);
  im->data = NULL;
  if (im->text) { story_free(im->text); free(im->text); im->text = NULL; }
  for (k = 0; k < im->nkids; k++) img_release(&im->kids[k]);
  free(im->kids); im->kids = NULL; im->nkids = 0;
  free(im->poly); im->poly = NULL; im->npoly = 0;
}
/* an entry added that the document takes as it is (what it holds is its own) */
int doc_take_object(Doc *d, Image *im)
{
  Image *ni;
  if (d->nimages >= MAX_IMAGES || !(ni = (Image *)realloc(d->images, (d->nimages + 1) * sizeof(Image)))) return -1;
  d->images = ni;
  d->images[d->nimages] = *im;
  return d->nimages++;
}
/* see doc.h. Not undoable; the caller puts the reference in */
int doc_add_object(Doc *d, const Image *pp)
{
  Image im;
  int k;
  if (d->nimages >= MAX_IMAGES || !img_clone(&im, pp, 0)) return -1;    /* (pp may point into d->images, which moves) */
  if ((k = doc_take_object(d, &im)) < 0) img_release(&im);
  return k;
}
/* ---- objects turned by any angle, and groups */
/* an object's width on the page: turned, the box round it is |cx cos| + |cy sin| wide. icos and
   isin are fixed point (TRIG_ONE stands for 1): no floating point on a 68k without an FPU */
i32 obj_w(const Image *im)
{
  long c = icos(im->rot), s = isin(im->rot);
  if (c < 0) c = -c;
  if (s < 0) s = -s;
  return (i32)((im->cx * c + im->cy * s + TRIG_ONE / 2) / TRIG_ONE);
}
/* and its height, |cx sin| + |cy cos| */
i32 obj_h(const Image *im)
{
  long c = icos(im->rot), s = isin(im->rot);
  if (c < 0) c = -c;
  if (s < 0) s = -s;
  return (i32)((im->cx * s + im->cy * c + TRIG_ONE / 2) / TRIG_ONE);
}
/* n / TRIG_ONE, rounded to the nearest with halves away from 0 (C's division truncates towards 0) */
static i32 div_round(long n) { return (i32)(n >= 0 ? (n + TRIG_ONE / 2) / TRIG_ONE : -((-n + TRIG_ONE / 2) / TRIG_ONE)); }
/* see doc.h: flipped first, then turned about the centre, into the box obj_w x obj_h */
void obj_point(const Image *im, i32 u, i32 v, i32 *bx, i32 *by)
{
  long c = icos(im->rot), s = isin(im->rot), dx, dy;
  if (im->flip & 1) u = im->cx - u;
  if (im->flip & 2) v = im->cy - v;
  dx = u - im->cx / 2; dy = v - im->cy / 2;
  *bx = obj_w(im) / 2 + div_round(dx * c - dy * s);
  *by = obj_h(im) / 2 + div_round(dx * s + dy * c);
}
/* obj_point backwards: turned back, then unflipped */
void obj_unpoint(const Image *im, i32 bx, i32 by, i32 *u, i32 *v)
{
  long c = icos(im->rot), s = isin(im->rot), dx = bx - obj_w(im) / 2, dy = by - obj_h(im) / 2;
  *u = im->cx / 2 + div_round(dx * c + dy * s);
  *v = im->cy / 2 + div_round(dy * c - dx * s);
  if (im->flip & 1) *u = im->cx - *u;
  if (im->flip & 2) *v = im->cy - *v;
}
/* v * num / den with a 64-bit product (twips times twips overflows 32 bits); v as it is when den
   isn't positive */
static i32 scale(i32 v, i32 num, i32 den) { return den > 0 ? (i32)((long long)v * num / den) : v; }
/* im drawn cx x cy, turned rot, flipped flip, its box's top left at ox, oy */
static int parts_of(const Image *im, i32 cx, i32 cy, int rot, int flip, i32 ox, i32 oy, ObjPart *out, int max, int depth)
{
  Image t = *im;
  int n = 0, k;
  t.cx = cx; t.cy = cy; t.rot = (u16)rot; t.flip = (u8)flip;
  if (max < 1) return 0;
  if (im->kind != OBJ_GROUP || !im->nkids || depth > 8) {
    out->im = im; out->cx = cx; out->cy = cy; out->x = ox + obj_w(&t) / 2; out->y = oy + obj_h(&t) / 2; out->rot = (u16)rot; out->flip = (u8)flip;
    return 1;
  }
  for (k = 0; k < im->nkids && n < max; k++) {
    const Image *K = &im->kids[k];
    Image kt = *K;
    i32 kx = scale(K->x, cx, im->chw), ky = scale(K->y, cy, im->chh), X, Y;
    int one = (flip == 1 || flip == 2);           /* mirrored once: turning goes the other way */
    kt.cx = scale(K->cx, cx, im->chw); kt.cy = scale(K->cy, cy, im->chh);
    obj_point(&t, kx + kt.cx / 2, ky + kt.cy / 2, &X, &Y);
    kt.flip = (u8)(K->flip ^ flip);
    kt.rot = (u16)((rot + (one ? 3600 - K->rot : K->rot)) % 3600);
    n += parts_of(K, kt.cx, kt.cy, kt.rot, kt.flip, ox + X - obj_w(&kt) / 2, oy + Y - obj_h(&kt) / 2, out + n, max - n, depth + 1);
  }
  return n;
}
/* see doc.h; groups nested more than 8 deep are drawn as one part */
int obj_parts(const Image *im, ObjPart *out, int max) { return parts_of(im, im->cx, im->cy, im->rot, im->flip, 0, 0, out, max, 0); }
/* qsort order for positions, last first */
static int pos_desc(const void *a, const void *b) { return pos_cmp(*(const Pos *)b, *(const Pos *)a); }
/* see doc.h. The group's space is the boxes' union; each object keeps its size and turn, centred
   where it was. The group floats as at[0] did (in line: in front of the text). The references go
   last first so the others' positions stay right; -1 also if n < 2 or any at[] isn't a picture
   reference */
int doc_group(Doc *d, const Pos *at, const i32 *box, int n)
{
  Image g;
  Pos *ps, first;
  i32 ux0 = 0x7FFFFFFF, uy0 = 0x7FFFFFFF, ux1 = -0x7FFFFFFF, uy1 = -0x7FFFFFFF;
  CharFmt f;
  char t[8];
  int i, k;
  if (n < 2) return -1;
  for (i = 0; i < n; i++) {
    if (at[i].para >= d->np || at[i].off + 4 > d->p[at[i].para]->len || !IS_PIC(d->p[at[i].para]->t + at[i].off) ||
        pic_index(d->p[at[i].para]->t + at[i].off) >= d->nimages) return -1;
    if (box[4 * i] < ux0) ux0 = box[4 * i];
    if (box[4 * i + 1] < uy0) uy0 = box[4 * i + 1];
    if (box[4 * i] + box[4 * i + 2] > ux1) ux1 = box[4 * i] + box[4 * i + 2];
    if (box[4 * i + 1] + box[4 * i + 3] > uy1) uy1 = box[4 * i + 1] + box[4 * i + 3];
  }
  img_defaults(&g);
  g.kind = OBJ_GROUP;
  g.cx = g.chw = ux1 - ux0; g.cy = g.chh = uy1 - uy0;
  g.hrel = g.vrel = POS_PAGE; g.x = ux0; g.y = uy0;
  if (!(g.kids = (Image *)calloc(n, sizeof(Image)))) return -1;
  for (i = 0; i < n; i++) {
    const Image *src = &d->images[pic_index(d->p[at[i].para]->t + at[i].off)];
    Image *kid = &g.kids[i];
    if (!img_clone(kid, src, 0)) { img_release(&g); return -1; }
    g.nkids = i + 1;
    if (!i) { g.wrap = src->wrap == WRAP_INLINE ? WRAP_FRONT : src->wrap; g.dl = src->dl; g.dr = src->dr; g.dt = src->dt; g.db = src->db; }
    /* its unturned box in the group, round the same centre as its box on the page */
    kid->x = box[4 * i] + box[4 * i + 2] / 2 - ux0 - src->cx / 2;
    kid->y = box[4 * i + 1] + box[4 * i + 3] / 2 - uy0 - src->cy / 2;
  }
  if (!(ps = (Pos *)malloc(n * sizeof(Pos)))) { img_release(&g); return -1; }
  memcpy(ps, at, n * sizeof(Pos));
  qsort(ps, (size_t)n, sizeof(Pos), pos_desc);
  first = ps[n - 1];
  f = *para_fmt_at(d->p[first.para], first.off);
  if ((k = doc_take_object(d, &g)) < 0) { img_release(&g); free(ps); return -1; }
  doc_begin(d, first);
  for (i = 0; i < n; i++) { Pos e = ps[i]; e.off += 4; doc_delete(d, ps[i], e); }
  pic_utf8(t, k);
  doc_insert(d, &first, t, 4, &f);
  doc_end(d, first);
  free(ps);
  return k;
}
/* see doc.h. Each object becomes a new entry, its size, turn and flip worked out from the group's;
   one that was in line inside the group floats as the group did */
int doc_ungroup(Doc *d, Pos at, i32 gx, i32 gy)
{
  const Image *g;
  Image t;
  Buf refs;
  CharFmt f;
  Pos e;
  int k, n = 0;
  if (at.para >= d->np || at.off + 4 > d->p[at.para]->len || !IS_PIC(d->p[at.para]->t + at.off)) return 0;
  k = pic_index(d->p[at.para]->t + at.off);
  if (k >= d->nimages || d->images[k].kind != OBJ_GROUP || !d->images[k].nkids) return 0;
  buf_init(&refs);
  t = d->images[k];
  for (k = 0; k < t.nkids; k++) {
    const Image *K = &t.kids[k];
    Image kt;
    i32 X, Y;
    int one = t.flip == 1 || t.flip == 2, nk;
    char r[8];
    g = &t;
    if (!img_clone(&kt, K, 0)) break;
    kt.cx = scale(K->cx, g->cx, g->chw); kt.cy = scale(K->cy, g->cy, g->chh);
    obj_point(g, scale(K->x, g->cx, g->chw) + kt.cx / 2, scale(K->y, g->cy, g->chh) + kt.cy / 2, &X, &Y);
    kt.flip = (u8)(K->flip ^ g->flip);
    kt.rot = (u16)((g->rot + (one ? 3600 - K->rot : K->rot)) % 3600);
    /* floating as the group did, where it was in it, from the page */
    if (K->wrap == WRAP_INLINE) { kt.wrap = g->wrap; kt.dl = g->dl; kt.dr = g->dr; kt.dt = g->dt; kt.db = g->db; }     /* else its own, from before it was grouped */
    kt.hrel = kt.vrel = POS_PAGE; kt.halign = kt.valign = PA_NONE;
    kt.x = gx + X - obj_w(&kt) / 2; kt.y = gy + Y - obj_h(&kt) / 2;
    if ((nk = doc_take_object(d, &kt)) < 0) { img_release(&kt); break; }
    pic_utf8(r, nk);
    buf_add(&refs, r, 4);
    n++;
  }
  if (!n || refs.oom) { buf_free(&refs); return 0; }
  f = *para_fmt_at(d->p[at.para], at.off);
  e = at; e.off += 4;
  doc_begin(d, at);
  doc_delete(d, at, e);
  doc_insert(d, &at, refs.data, (u32)refs.len, &f);
  doc_end(d, at);
  buf_free(&refs);
  return n;
}
/* see doc.h. Word's look: a blue fill with a darker 0.75 pt outline; a text box white with black,
   wrapped square, with an empty story; a line or arrow no fill. Not undoable; the caller puts the
   reference in */
int doc_add_shape(Doc *d, int shape, i32 cx, i32 cy)
{
  Image im;
  img_defaults(&im);
  im.kind = shape < 0 ? OBJ_TEXTBOX : OBJ_SHAPE;
  im.shape = (u8)(shape < 0 ? SH_RECT : shape);
  im.cx = cx; im.cy = cy;
  im.wrap = WRAP_FRONT;
  im.lstyle = PB_SINGLE; im.lwidth = 6;
  im.lcolor = im.kind == OBJ_TEXTBOX ? 0 : 0x2F528F;
  im.fill = im.kind == OBJ_TEXTBOX ? 0xFFFFFF : 0x4472C4;
  if (shape == SH_LINE || shape == SH_ARROW) im.fill = COLOR_AUTO;
  if (im.kind == OBJ_TEXTBOX) { im.wrap = WRAP_SQUARE; if (!(im.text = (Story *)calloc(1, sizeof(Story)))) return -1; }
  {
    int k = doc_add_object(d, &im);
    img_release(&im);
    return k;
  }
}
/* see doc.h. The old entry stays, so undo can bring its reference back */
int doc_set_object(Doc *d, Pos at, const Image *im)
{
  Para *p;
  CharFmt f;
  char t[8];
  int k;
  Pos e;
  if (at.para >= d->np || at.off + 4 > (p = d->p[at.para])->len || !IS_PIC(p->t + at.off)) return -1;
  if ((k = doc_add_object(d, im)) < 0) return -1;
  f = *para_fmt_at(p, at.off);
  pic_utf8(t, k);
  e = at; e.off += 4;
  doc_begin(d, at);
  doc_delete(d, at, e);
  doc_insert(d, &at, t, 4, &f);
  doc_end(d, at);
  return k;
}
/* see doc.h; in a nested table the outer cell must match too */
int same_cell(const ParaFmt *a, const ParaFmt *b)
{
  if (a->cell.table != b->cell.table || a->outer.table != b->outer.table) return 0;
  if (a->outer.table && (a->outer.row != b->outer.row || a->outer.col != b->outer.col)) return 0;
  return !a->cell.table || (a->cell.row == b->cell.row && a->cell.col == b->cell.col);
}
/* see doc.h; NULL also for t 0 */
const Cell *cell_in(const ParaFmt *pf, u16 t)
{
  if (!t) return NULL;
  if (pf->cell.table == t) return &pf->cell;
  if (pf->outer.table == t) return &pf->outer;
  return NULL;
}
/* see doc.h. One undo step saving the old ones; where is the caret undo goes back to. 0 if out of
   range, out of memory, or it would leave the document with no paragraphs; the new ones are freed
   then, so the caller never frees them */
int doc_replace(Doc *d, u32 p0, u32 nold, Para **nw, u32 nnew, Pos where)
{
  Step *st;
  u32 i;
  if (p0 > d->np || p0 + nold > d->np || (d->np - nold + nnew) == 0) goto fail;
  if (d->np - nold + nnew > d->pcap) {
    u32 nc = d->np - nold + nnew + 64;
    Para **np = (Para **)realloc(d->p, nc * sizeof(Para *));
    if (!np) goto fail;
    d->p = np; d->pcap = nc;
  }
  if (!(st = rec(d, p0, nold, where))) { close_auto(d); goto fail; }
  for (i = 0; i < nold; i++) para_free(d->p[p0 + i]);
  memmove(d->p + p0 + nnew, d->p + p0 + nold, (d->np - p0 - nold) * sizeof(Para *));
  for (i = 0; i < nnew; i++) { d->p[p0 + i] = nw[i]; touch(nw[i]); }
  d->np = d->np - nold + nnew;
  st->nnew = nnew;
  changed(d);
  close_auto(d);
  return 1;
fail:
  for (i = 0; i < nnew; i++) para_free(nw[i]);
  return 0;
}
/* see doc.h. The levels go round bullet, o, square, or 1. a. i., each half an inch further in with
   a quarter-inch hanging indent. Not undoable */
int doc_new_list(Doc *d, int numbered)
{
  static const char *const bullets[3] = { "\xE2\x80\xA2", "o", "\xE2\x96\xAA" };   /* bullet, o, small square */
  static const u8 numfmt[3] = { NF_DECIMAL, NF_LOWER_LETTER, NF_LOWER_ROMAN };
  ListDef l;
  int k, maxid = 0;
  memset(&l, 0, sizeof l);
  for (k = 0; k < d->nlists; k++) if (d->lists[k].id > maxid) maxid = d->lists[k].id;
  l.id = (u16)(maxid + 1);
  for (k = 0; k < 9; k++) {
    ListLevel *v = &l.lvl[k];
    v->left = 720 * (k + 1); v->hanging = 360; v->start = 1;
    if (numbered) { v->fmt = numfmt[k % 3]; sprintf(v->text, "%%%d.", k + 1); }
    else { v->fmt = NF_BULLET; strcpy(v->text, bullets[k % 3]); }
  }
  return doc_add_list(d, &l);
}
/* ------------------------------------------------------------------ */
/* lists: kinds ready made, and numbering started again or carried on */
/* a w:num id no list has yet (one more than the highest) */
static int next_list_id(Doc *d)
{
  int k, maxid = 0;
  for (k = 0; k < d->nlists; k++) if (d->lists[k].id > maxid) maxid = d->lists[k].id;
  return maxid + 1;
}
/* see doc.h. The legal and heading kinds' indents widen with each level, as their labels grow. Not
   undoable */
int doc_list_preset(Doc *d, int preset)
{
  static const char *const bullets[3] = { "\xE2\x80\xA2", "o", "\xE2\x96\xAA" };
  static const u8 outline[9] = { NF_UPPER_ROMAN, NF_UPPER_LETTER, NF_DECIMAL, NF_LOWER_LETTER, NF_LOWER_ROMAN, NF_DECIMAL, NF_LOWER_LETTER, NF_LOWER_ROMAN, NF_DECIMAL };
  static const u8 abc[3] = { NF_DECIMAL, NF_LOWER_LETTER, NF_LOWER_ROMAN };
  ListDef l;
  int k, j;
  if (preset < 0 || preset >= LP_N) return 0;
  memset(&l, 0, sizeof l);
  l.id = (u16)next_list_id(d);
  for (k = 0; k < 9; k++) {
    ListLevel *v = &l.lvl[k];
    v->start = 1; v->left = 720 * (k + 1); v->hanging = 360;
    switch (preset) {
    case LP_BULLETS: v->fmt = NF_BULLET; strcpy(v->text, bullets[k % 3]); break;
    case LP_NUMBERS: v->fmt = abc[k % 3]; sprintf(v->text, "%%%d.", k + 1); break;
    case LP_PARENS: v->fmt = abc[k % 3]; sprintf(v->text, "%%%d)", k + 1); break;
    case LP_OUTLINE: v->fmt = outline[k]; sprintf(v->text, k == 3 ? "%%%d)" : k >= 4 ? "(%%%d)" : "%%%d.", k + 1); break;
    case LP_LEGAL: case LP_HEADINGS:
      /* 1. 1.1. 1.1.1. (headings: without the last full stop, at the margin) */
      v->fmt = NF_DECIMAL;
      v->text[0] = 0;
      for (j = 0; j <= k; j++) sprintf(v->text + strlen(v->text), j < k ? "%%%d." : (preset == LP_LEGAL ? "%%%d." : "%%%d"), j + 1);
      if (preset == LP_LEGAL) { v->left = 360 + 432 * k; v->hanging = 360 + 72 * k; }
      else { v->left = 432 + 144 * k; v->hanging = 432 + 144 * k; }
      break;
    }
  }
  return doc_add_list(d, &l);
}
/* see doc.h; one undo step. A paragraph already in a list keeps its level, others go in at level
   0; one taken out of a list gets its style's indents back. 1 if every paragraph was done */
int doc_list_apply(Doc *d, u32 pa, u32 pb, int list)
{
  u32 i;
  Pos at;
  if (pa > pb) { u32 t = pa; pa = pb; pb = t; }
  if (pb >= d->np || list < 0 || list > d->nlists) return 0;
  at.para = pa; at.off = 0;
  doc_begin(d, at);
  for (i = pa; i <= pb; i++) {
    ParaFmt pf = d->p[i]->pf;
    if (list) {
      const ListLevel *lv = &d->lists[list - 1].lvl[pf.list ? pf.level % 9 : 0];
      if (!pf.list) pf.level = 0;
      pf.list = (u16)list; pf.left = lv->left; pf.first = -lv->hanging;
    } else {
      const Style *st = &d->styles[pf.style < d->nstyles ? pf.style : 0];
      pf.list = 0; pf.level = 0; pf.left = st->pf.left; pf.first = st->pf.first;
    }
    if (!doc_para_format(d, i, i, PM_LIST | PM_INDENT, &pf)) break;
  }
  doc_end(d, at);
  return i > pb;
}
/* paragraph pi and those after it in its list move to list nl */
static int list_move(Doc *d, u32 pi, int ol, int nl)
{
  u32 i;
  Pos at;
  ParaFmt pf;
  at.para = pi; at.off = 0;
  doc_begin(d, at);
  memset(&pf, 0, sizeof pf);
  pf.list = (u16)nl;
  for (i = pi; i < d->np; i++)
    if (d->p[i]->pf.list == ol) { pf.level = d->p[i]->pf.level; if (!doc_para_format(d, i, i, PM_LIST, &pf)) break; }
  doc_end(d, at);
  return 1;
}
/* see doc.h. A copy of the list with a new id; value sets only the start of pi's level. One undo
   step */
int doc_list_restart(Doc *d, u32 pi, int value)
{
  ListDef l;
  int ol, nl;
  if (pi >= d->np || !(ol = d->p[pi]->pf.list) || ol > d->nlists) return 0;
  l = d->lists[ol - 1];
  l.id = (u16)next_list_id(d);
  if (value > 0) l.lvl[d->p[pi]->pf.level % 9].start = (u16)value;
  else { int k; for (k = 0; k < 9; k++) if (!l.lvl[k].start) l.lvl[k].start = 1; }
  if (!(nl = doc_add_list(d, &l))) return 0;
  return list_move(d, pi, ol, nl);
}
/* see doc.h. 'Of its kind': bulleted or numbered, as its first level says; the nearest such list
   before pi. One undo step */
int doc_list_continue(Doc *d, u32 pi)
{
  int ol, k;
  u32 i;
  if (pi >= d->np || !(ol = d->p[pi]->pf.list) || ol > d->nlists) return 0;
  /* the nearest list of the same kind before it */
  for (i = pi; i-- > 0; ) {
    k = d->p[i]->pf.list;
    if (k && k != ol && k <= d->nlists && (d->lists[k - 1].lvl[0].fmt == NF_BULLET) == (d->lists[ol - 1].lvl[0].fmt == NF_BULLET))
      return list_move(d, pi, ol, k);
  }
  return 0;
}
/* see doc.h. Each of Heading1 to Heading9 that exists gets the list at its level (and the list's
   indents), or loses its numbering. 0 if the list can't be made or a style can't be changed */
int doc_heading_numbering(Doc *d, int preset)
{
  int list = preset >= 0 ? doc_list_preset(d, preset) : 0, k, ok = 1;
  Pos at;
  at.para = 0; at.off = 0;
  if (preset >= 0 && !list) return 0;
  doc_begin(d, at);
  for (k = 0; k < 9 && ok; k++) {
    char id[16];
    int s;
    Style ns;
    sprintf(id, "Heading%d", k + 1);
    if ((s = doc_style(d, id)) < 0) continue;
    ns = d->styles[s];
    if (list) { ns.pmask |= PM_LIST; ns.opf.list = (u16)list; ns.opf.level = (u8)k; ns.pmask &= ~PM_INDENT; }
    else ns.pmask &= ~PM_LIST;
    ok = doc_set_style(d, s, &ns) >= 0;
  }
  doc_end(d, at);
  return ok;
}
/* ------------------------------------------------------------------ */
/* the Design tab: style sets, fonts and paragraph spacing for the whole document */
/* style id gets the fields cm of cf and the groups pm of pf as its own settings, through
   doc_set_style (undoable, the text following). 1 for a missing or non-paragraph style, which is
   skipped */
static int set_own(Doc *d, const char *id, unsigned cm, const CharFmt *cf, unsigned pm, const ParaFmt *pf)
{
  int k = doc_style(d, id);
  Style s;
  if (k < 0 || d->styles[k].type != ST_PARA) return 1;
  s = d->styles[k];
  if (cm) { s.cmask |= cm; cf_apply(&s.ocf, cm, cf); }
  if (pm) { s.pmask |= pm; pf_apply(&s.opf, pm, pf); }
  return doc_set_style(d, k, &s) >= 0;
}
static const char *const heading_ids[] = { "Heading1", "Heading2", "Heading3", "Heading4", "Heading5", "Heading6", "Title", "Subtitle" };
/* see doc.h: Normal (style 0) gets the body font; the headings, Title and Subtitle the heading
   font */
int doc_theme_fonts(Doc *d, int pair)
{
  static const char *const body[TF_N] = { "Times New Roman", "Calibri", "Calibri", "Cambria", "Arial", "Times New Roman" };
  static const char *const head[TF_N] = { "Arial", "Cambria", "Calibri", "Calibri", "Arial", "Times New Roman" };
  CharFmt cf;
  Pos at;
  unsigned i;
  int ok;
  if (pair < 0 || pair >= TF_N) return 0;
  at.para = 0; at.off = 0;
  memset(&cf, 0, sizeof cf);
  doc_begin(d, at);
  cf.font = (u8)doc_font(d, body[pair]);
  ok = set_own(d, d->styles[0].id, CM_FONT, &cf, 0, NULL);
  cf.font = (u8)doc_font(d, head[pair]);
  for (i = 0; ok && i < sizeof heading_ids / sizeof heading_ids[0]; i++) ok = set_own(d, heading_ids[i], CM_FONT, &cf, 0, NULL);
  doc_end(d, at);
  if (ok) d->look[1] = (signed char)pair;
  return ok;
}
/* see doc.h: Normal gets no space before, the preset's space after (twips) and its line spacing
   (240ths of a line) */
int doc_para_spacing(Doc *d, int preset)
{
  static const i32 after[PS_N] = { 0, 80, 120, 160, 160, 160 }, line[PS_N] = { 240, 240, 259, 276, 288, 480 };
  ParaFmt pf;
  Pos at;
  int ok;
  if (preset < 0 || preset >= PS_N) return 0;
  memset(&pf, 0, sizeof pf);
  pf.before = 0; pf.after = after[preset]; pf.line = line[preset]; pf.linerule = LS_AUTO;
  at.para = 0; at.off = 0;
  doc_begin(d, at);
  ok = set_own(d, d->styles[0].id, 0, NULL, PM_SPACING | PM_LINE, &pf);
  doc_end(d, at);
  if (ok) d->look[2] = (signed char)preset;
  return ok;
}
/* see doc.h: the set's fonts and spacing, then the headings' colour, bold and small capitals and
   the titles' colour. One undo step */
int doc_style_set(Doc *d, int set)
{
  /* body font, heading font, heading colour, headings bold, title colour, small capitals in the headings */
  static const struct { int fonts; u32 hcolor; u8 hbold; u32 tcolor; u8 smallcaps; int spacing; } sets[SS_N] = {
    { TF_TIMES_ARIAL, COLOR_AUTO, 1, COLOR_AUTO, 0, PS_COMPACT },
    { TF_CALIBRI_CAMBRIA, 0x2F5496, 0, 0x1F3864, 0, PS_OPEN },
    { TF_CAMBRIA_CALIBRI, COLOR_AUTO, 1, COLOR_AUTO, 0, PS_TIGHT },
    { TF_CALIBRI_CAMBRIA, 0x7B4F1E, 0, 0x7B4F1E, 1, PS_RELAXED },
    { TF_ARIAL, 0x404040, 1, 0x262626, 0, PS_COMPACT },
    { TF_CALIBRI_CALIBRI, 0xC00000, 1, 0xC00000, 0, PS_OPEN },
  };
  CharFmt cf;
  Pos at;
  unsigned i;
  int ok;
  if (set < 0 || set >= SS_N) return 0;
  at.para = 0; at.off = 0;
  doc_begin(d, at);
  ok = doc_theme_fonts(d, sets[set].fonts) && doc_para_spacing(d, sets[set].spacing);
  memset(&cf, 0, sizeof cf);
  for (i = 0; ok && i < sizeof heading_ids / sizeof heading_ids[0]; i++) {
    int title = i >= 6;
    cf.color = title ? sets[set].tcolor : sets[set].hcolor;
    cf.flags = (u8)(!title && sets[set].hbold ? CF_BOLD : 0);
    cf.fx = (u8)(sets[set].smallcaps ? FX_SMALLCAPS : 0);
    ok = set_own(d, heading_ids[i], CM_COLOR | CM_SMALLCAPS | (title ? 0 : CM_BOLD), &cf, 0, NULL);
  }
  doc_end(d, at);
  if (ok) d->look[0] = (signed char)set;
  return ok;
}
static const struct { u32 h, t; } color_sets[CS_N] = {
  { COLOR_AUTO, COLOR_AUTO }, { 0x2F5496, 0x1F3864 }, { 0x1F6E6B, 0x0E4D4A }, { 0x538135, 0x375623 },
  { 0xC55A11, 0x843C0C }, { 0xC00000, 0x7F0000 }, { 0x7030A0, 0x4B1E6E }, { 0x404040, 0x262626 } };
static const struct { u8 set, fonts, colors; } themes[TH_N] = {
  { SS_QUILL, TF_TIMES_ARIAL, CS_QUILL }, { SS_MODERN, TF_CALIBRI_CAMBRIA, CS_BLUE }, { SS_SIMPLE, TF_ARIAL, CS_GREY },
  { SS_BOLD, TF_CALIBRI_CALIBRI, CS_RED }, { SS_CLASSIC, TF_CAMBRIA_CALIBRI, CS_BLUEGREEN }, { SS_ELEGANT, TF_CALIBRI_CAMBRIA, CS_ORANGE },
  { SS_MODERN, TF_ARIAL, CS_GREEN }, { SS_ELEGANT, TF_TIMES, CS_VIOLET } };
/* see doc.h: the headings 1-6 in the set's heading colour, Title and Subtitle in its title colour */
int doc_color_set(Doc *d, int set)
{
  CharFmt cf;
  Pos at;
  unsigned i;
  int ok = 1;
  if (set < 0 || set >= CS_N) return 0;
  at.para = 0; at.off = 0;
  memset(&cf, 0, sizeof cf);
  doc_begin(d, at);
  for (i = 0; ok && i < sizeof heading_ids / sizeof heading_ids[0]; i++) {
    cf.color = i >= 6 ? color_sets[set].t : color_sets[set].h;
    ok = set_own(d, heading_ids[i], CM_COLOR, &cf, 0, NULL);
  }
  doc_end(d, at);
  if (ok) d->look[3] = (signed char)set;
  return ok;
}
/* see doc.h */
int doc_theme(Doc *d, int theme)
{
  Pos at;
  int ok;
  if (theme < 0 || theme >= TH_N) return 0;
  at.para = 0; at.off = 0;
  doc_begin(d, at);
  ok = doc_style_set(d, themes[theme].set) && doc_theme_fonts(d, themes[theme].fonts) && doc_color_set(d, themes[theme].colors);
  doc_end(d, at);
  return ok;
}
/* see doc.h */
void look_info(int kind, int which, const char **head, const char **body, u32 *hcolor, u32 *tcolor)
{
  static const char *const bodyf[TF_N] = { "Times New Roman", "Calibri", "Calibri", "Cambria", "Arial", "Times New Roman" };
  static const char *const headf[TF_N] = { "Arial", "Cambria", "Calibri", "Calibri", "Arial", "Times New Roman" };
  static const u8 setfonts[SS_N] = { TF_TIMES_ARIAL, TF_CALIBRI_CAMBRIA, TF_CAMBRIA_CALIBRI, TF_CALIBRI_CAMBRIA, TF_ARIAL, TF_CALIBRI_CALIBRI };
  static const u32 seth[SS_N] = { COLOR_AUTO, 0x2F5496, COLOR_AUTO, 0x7B4F1E, 0x404040, 0xC00000 }, sett[SS_N] = { COLOR_AUTO, 0x1F3864, COLOR_AUTO, 0x7B4F1E, 0x262626, 0xC00000 };
  int f = TF_TIMES_ARIAL;
  u32 h = COLOR_AUTO, t = COLOR_AUTO;
  if (kind == 0 && which >= 0 && which < SS_N) { f = setfonts[which]; h = seth[which]; t = sett[which]; }
  else if (kind == 1 && which >= 0 && which < TH_N) { f = themes[which].fonts; h = color_sets[themes[which].colors].h; t = color_sets[themes[which].colors].t; }
  else if (kind == 2 && which >= 0 && which < CS_N) { h = color_sets[which].h; t = color_sets[which].t; }
  if (head) *head = headf[f];
  if (body) *body = bodyf[f];
  if (hcolor) *hcolor = h;
  if (tcolor) *tcolor = t;
}
/* ------------------------------------------------------------------ */
/* copying with formatting: a fragment holds copies of what it needs (fonts, styles, lists,
   pictures), so it can be pasted after its document is gone */
struct Fragment {
  u32 serial;               /* the document it came from */
  Para **p; u32 np;
  char (*fonts)[48]; int nfonts;
  Style *styles; int nstyles;
  ListDef *lists; int nlists;
  Image *imgs; int *imgfrom; int nimgs;   /* pictures copied (data owned), and their indexes in the source */
  char *text; u32 ntext;
  char **links; int nlinks;  /* the source's link targets (CharFmt.link numbers point into them) */
  struct MathEq **maths; int *mathfrom; int nmaths;   /* WP14a: equations copied, and their indexes in the source */
};
/* a fragment and all its copies freed; NULL is fine */
void fragment_free(Fragment *f)
{
  u32 i;
  int k;
  if (!f) return;
  for (i = 0; i < f->np; i++) para_free(f->p[i]);
  free(f->p); free(f->fonts); free(f->styles); free(f->lists);
  for (k = 0; k < f->nimgs; k++) {
    img_release(&f->imgs[k]);
  }
  free(f->imgs); free(f->imgfrom); free(f->text);
  for (k = 0; k < f->nlinks; k++) free(f->links[k]);
  free(f->links);
  for (k = 0; k < f->nmaths; k++) math_free(f->maths[k]);     /* WP14a */
  free(f->maths); free(f->mathfrom);
  free(f);
}
/* see doc.h; the fragment owns it, *n its length */
const char *fragment_text(const Fragment *f, u32 *n) { *n = f->ntext; return f->text; }
/* where the fragment keeps its copy of the source's picture k, or -1 */
static int frag_has_img(const Fragment *f, int k) { int i; for (i = 0; i < f->nimgs; i++) if (f->imgfrom[i] == k) return i; return -1; }
/* see doc.h. The paragraphs are copied without their table cells and section breaks; the fonts,
   styles and lists are copied whole, each picture referred to once (its data too, so it outlives
   its document), and the plain text kept as well */
Fragment *doc_fragment(Doc *d, Pos a, Pos b)
{
  Fragment *f;
  u32 i, k;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b) || !(f = (Fragment *)calloc(1, sizeof(Fragment)))) return NULL;
  f->serial = d->serial;
  f->np = b.para - a.para + 1;
  if (!(f->p = (Para **)calloc(f->np, sizeof(Para *)))) goto oom;
  for (i = 0; i < f->np; i++) {
    Para *s = d->p[a.para + i], *q;
    u32 st = i == 0 ? a.off : 0, en = i + 1 == f->np ? b.off : s->len, r, pos = 0;
    ParaFmt pf = s->pf;
    memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer); pf.sect = 0;
    if (!(q = para_new(&pf, para_fmt_at(s, st)))) goto oom;
    f->p[i] = q;
    for (r = 0; r < s->nr; r++) {
      u32 rs = pos > st ? pos : st, re = s->r[r].end < en ? s->r[r].end : en;
      if (re > rs && !para_append(q, s->t + rs, re - rs, &s->r[r].f)) goto oom;
      pos = s->r[r].end;
    }
    /* the pictures it refers to, copied */
    for (k = 0; k + 4 <= q->len; k++)
      if (IS_PIC(q->t + k)) {
        int n = pic_index(q->t + k);
        Image *ni;
        if (n < d->nimages && frag_has_img(f, n) < 0) {
          if (!(ni = (Image *)realloc(f->imgs, (f->nimgs + 1) * sizeof(Image)))) goto oom;
          f->imgs = ni;
          if (!(f->imgfrom = (int *)realloc(f->imgfrom, (f->nimgs + 1) * sizeof(int)))) goto oom;
          ni = &f->imgs[f->nimgs];
          if (!img_clone(ni, &d->images[n], 1)) goto oom;
          f->imgfrom[f->nimgs] = n;
          f->nimgs++;
        }
        k += 3;
      }
    /* WP14a: the equations it refers to, copied (each once) */
    for (k = 0; k + 4 <= q->len; k++)
      if (IS_MATH(q->t + k)) {
        int n = math_index(q->t + k), j;
        for (j = 0; j < f->nmaths && f->mathfrom[j] != n; j++) ;
        if (n < d->nmaths && j == f->nmaths) {
          struct MathEq **nm = (struct MathEq **)realloc(f->maths, (f->nmaths + 1) * sizeof *nm);
          int *nf;
          if (!nm) goto oom;
          f->maths = nm;
          if (!(nf = (int *)realloc(f->mathfrom, (f->nmaths + 1) * sizeof(int)))) goto oom;
          f->mathfrom = nf;
          if (!(f->maths[f->nmaths] = math_clone(d->maths[n]))) goto oom;
          f->mathfrom[f->nmaths++] = n;
        }
        k += 3;
      }
  }
  /* what its formatting refers to */
  if (!(f->fonts = (char (*)[48])malloc((d->nfonts ? d->nfonts : 1) * 48))) goto oom;
  memcpy(f->fonts, d->fonts, d->nfonts * 48); f->nfonts = d->nfonts;
  if (!(f->styles = (Style *)malloc((d->nstyles ? d->nstyles : 1) * sizeof(Style)))) goto oom;
  memcpy(f->styles, d->styles, d->nstyles * sizeof(Style)); f->nstyles = d->nstyles;
  if (d->nlists) {
    if (!(f->lists = (ListDef *)malloc(d->nlists * sizeof(ListDef)))) goto oom;
    memcpy(f->lists, d->lists, d->nlists * sizeof(ListDef)); f->nlists = d->nlists;
  }
  if (!(f->text = doc_text(d, a, b, &f->ntext))) goto oom;
  if (d->nlinks) {
    int k;
    if (!(f->links = (char **)calloc((size_t)d->nlinks, sizeof(char *)))) goto oom;
    for (k = 0; k < d->nlinks; k++) { if (!(f->links[k] = str_ndup(d->links[k], (u32)strlen(d->links[k])))) goto oom; f->nlinks = k + 1; }
  }
  return f;
oom:
  fragment_free(f);
  return NULL;
}
/* a pasted run's formatting: kept (fonts and styles found again in d), or the destination's with
   the run's emphasis (merge), or the destination's (text only) */
static CharFmt paste_cf(Doc *d, const Fragment *f, const CharFmt *src, const CharFmt *dst, int mode, int same)
{
  CharFmt c = *src;
  /* a link keeps its target, found again (or added) in d's table; text only drops it */
  u16 link = (u16)(!src->link ? 0 : same ? src->link : src->link <= f->nlinks ? doc_link(d, f->links[src->link - 1]) : 0);
  if (mode == PASTE_TEXT) { c = *dst; c.rev = 0; c.rid = 0; c.link = 0; return c; }
  c.bmark = same ? src->bmark : 0;     /* (a bookmark goes with its text only in its own document) */
  if (mode == PASTE_MERGE) {
    c = *dst;
    c.flags = (u8)((dst->flags & ~(CF_BOLD | CF_ITALIC | CF_UNDERLINE)) | (src->flags & (CF_BOLD | CF_ITALIC | CF_UNDERLINE)));
    c.rev = 0; c.rid = 0;
    c.link = link;
    c.bmark = 0;
    return c;
  }
  c.link = link;
  if (!same) {
    c.font = (u8)(src->font < f->nfonts ? doc_font(d, f->fonts[src->font]) : 0);
    if (src->cstyle && src->cstyle < f->nstyles) { int k = doc_style(d, f->styles[src->cstyle].id); c.cstyle = (u8)(k > 0 && d->styles[k].type == ST_CHAR ? k : 0); }
  }
  c.rev = 0; c.rid = 0;
  return c;
}
/* a pasted paragraph's text with its references made right for d: notes and comment marks left out,
   pictures kept (same document), added again (another) or left out (text only) */
static int paste_runs(Doc *d, Para *q, const Fragment *f, const Para *s, const CharFmt *dst, int mode, int same, int *imgmap)
{
  u32 r, pos = 0;
  for (r = 0; r < s->nr; r++) {
    CharFmt c = paste_cf(d, f, &s->r[r].f, dst, mode, same);
    u32 o = pos, e = s->r[r].end;
    while (o < e) {
      u32 m = o;
      while (m < e && !((u8)s->t[m] >= 0xF3 && m + 4 <= e && (IS_PIC(s->t + m) || IS_NOTE(s->t + m) || IS_MARK(s->t + m) || IS_ENOTE(s->t + m) || IS_MATH(s->t + m)))) m++;
      if (m > o && !para_append(q, s->t + o, m - o, &c)) return 0;
      if (m < e && IS_MATH(s->t + m)) {
        /* WP14a: an equation: the same entry (same document) or a copy of the fragment's */
        int n = math_index(s->t + m), j;
        char ref[8];
        for (j = 0; !same && j < f->nmaths && f->mathfrom[j] != n; j++) ;
        if (!same && j < f->nmaths) { MathEq *me = math_clone(f->maths[j]); n = me ? doc_add_math(d, me) : -1; }
        else if (!same) n = -1;
        if (n >= 0) { math_utf8(ref, n); if (!para_append(q, ref, 4, &c)) return 0; }
        o = m + 4;
        continue;
      }
      if (m < e) {
        if (IS_PIC(s->t + m) && mode != PASTE_TEXT) {
          int n = pic_index(s->t + m), j = frag_has_img(f, n);
          char ref[8];
          if (!same && j >= 0) {
            if (imgmap[j] < 0) {
              Image im;
              if (!img_clone(&im, &f->imgs[j], 1)) return 0;     /* (its own copy) */
              if ((imgmap[j] = doc_take_object(d, &im)) < 0) { img_release(&im); return 0; }
            }
            n = imgmap[j];
          }
          if (same || j >= 0) { pic_utf8(ref, n); if (!para_append(q, ref, 4, &c)) return 0; }
        }
        m += 4;
      }
      o = m;
    }
    pos = e;
  }
  return 1;
}
/* see doc.h. New paragraphs replace the one pasted into (one doc_replace: one undo step), the
   first starting with the text before the caret and the last ending with the text after it. From
   another document, styles are found again by id, fonts by name, and lists added anew. *at ends
   after the pasted text */
int doc_paste(Doc *d, Pos *at, const Fragment *f, int mode)
{
  Para *dp, **nw;
  u32 i, n = f->np, off;
  int same = f->serial == d->serial, *imgmap, *listmap, ok = 1, k;
  CharFmt dst;
  Pos where;
  if (!n) return 0;
  where = *at = pos_clamp(d, *at);
  dp = d->p[at->para];
  off = at->off;
  dst = *para_fmt_before(dp, off);
  if (!(nw = (Para **)calloc(n, sizeof(Para *)))) return 0;
  imgmap = (int *)malloc((f->nimgs ? f->nimgs : 1) * sizeof(int));
  listmap = (int *)malloc((f->nlists ? f->nlists : 1) * sizeof(int));
  if (!imgmap || !listmap) { free(nw); free(imgmap); free(listmap); return 0; }
  for (k = 0; k < f->nimgs; k++) imgmap[k] = -1;
  for (k = 0; k < f->nlists; k++) listmap[k] = -1;
  for (i = 0; i < n && ok; i++) {
    const Para *s = f->p[i];
    ParaFmt pf = dp->pf;
    /* the paragraph's formatting: the pasted one's (keeping), except the last, which goes on as the
       paragraph pasted into; in a table cell, in that cell */
    if (mode == PASTE_KEEP && i + 1 < n) {
      pf = s->pf;
      if (!same) {
        int st = s->pf.style < f->nstyles ? doc_style(d, f->styles[s->pf.style].id) : 0;
        pf.style = (u8)(st >= 0 && d->styles[st].type == ST_PARA ? st : 0);
        if (pf.list && pf.list <= f->nlists) {
          if (listmap[pf.list - 1] < 0) { ListDef l = f->lists[pf.list - 1]; l.id = (u16)next_list_id(d); listmap[pf.list - 1] = doc_add_list(d, &l); }
          pf.list = (u16)(listmap[pf.list - 1] > 0 ? listmap[pf.list - 1] : 0);
        } else pf.list = 0;
      }
      pf.cell = dp->pf.cell; pf.outer = dp->pf.outer; pf.sect = 0; pf.dropcap = 0;
    }
    if (!(nw[i] = para_new(&pf, i == 0 ? para_fmt_at(dp, 0) : &dst))) { ok = 0; break; }
    nw[i]->len = 0; nw[i]->t[0] = 0;
    /* the first takes what came before the caret, the last what came after */
    if (i == 0 && off) {
      u32 r, pos = 0;
      for (r = 0; r < dp->nr && ok; r++) {
        u32 re = dp->r[r].end < off ? dp->r[r].end : off;
        if (re > pos) ok = para_append(nw[i], dp->t + pos, re - pos, &dp->r[r].f);
        pos = dp->r[r].end;
        if (pos >= off) break;
      }
    }
    if (ok) ok = paste_runs(d, nw[i], f, s, &dst, mode, same, imgmap);
    if (i + 1 == n && ok) {
      u32 r, pos = 0;
      where.para = at->para + i; where.off = nw[i]->len;
      for (r = 0; r < dp->nr && ok; r++) {
        u32 rs = pos > off ? pos : off;
        if (dp->r[r].end > rs) ok = para_append(nw[i], dp->t + rs, dp->r[r].end - rs, &dp->r[r].f);
        pos = dp->r[r].end;
      }
    }
    if (ok) {
      /* an empty paragraph keeps a run giving its formatting */
      if (!nw[i]->len && nw[i]->nr) nw[i]->r[0].f = i + 1 == n ? *para_fmt_at(dp, dp->len) : paste_cf(d, f, para_fmt_at((Para *)s, 0), &dst, mode, same);
      para_normalise(nw[i]);
    }
  }
  free(imgmap); free(listmap);
  if (!ok) { for (i = 0; i < n; i++) if (nw[i]) para_free(nw[i]); free(nw); return 0; }
  ok = doc_replace(d, at->para, 1, nw, n, *at);
  free(nw);
  if (ok) *at = where;
  return ok;
}
/* see doc.h; names compared ignoring case. 0 (the first font) when the table is full */
int doc_font(Doc *d, const char *family)
{
  int i;
  for (i = 0; i < d->nfonts; i++) if (str_ieq(d->fonts[i], family)) return i;
  if (d->nfonts >= MAX_FONTS) return 0;
  strncpy(d->fonts[d->nfonts], family, sizeof d->fonts[0] - 1);
  d->fonts[d->nfonts][sizeof d->fonts[0] - 1] = 0;
  return d->nfonts++;
}
/* see doc.h; ids compared ignoring case. Deleted styles (ST_GONE) are found too */
int doc_style(Doc *d, const char *id)
{
  int i;
  for (i = 0; i < d->nstyles; i++) if (str_ieq(d->styles[i].id, id)) return i;
  return -1;
}
/* a style by its shown name, ignoring case; -1 if none */
int doc_style_by_name(Doc *d, const char *name)
{
  int i;
  for (i = 0; i < d->nstyles; i++) if (str_ieq(d->styles[i].name, name)) return i;
  return -1;
}
/* see doc.h. A style with the same id is replaced in place. Nothing is resolved: call
   doc_resolve_styles after */
int doc_add_style(Doc *d, const Style *s)
{
  int i = doc_style(d, s->id);
  if (i >= 0) { d->styles[i] = *s; return i; }
  if (d->nstyles >= MAX_STYLES) return -1;
  d->styles[d->nstyles] = *s;
  return d->nstyles++;
}
/* ------------------------------------------------------------------ */
/* styles: what they set themselves, and what that comes to */
static const unsigned pf_groups[] = { PM_ALIGN, PM_INDENT, PM_SPACING, PM_LINE, PM_TABS, PM_KEEP, PM_LIST, PM_BREAK, PM_BORDER, PM_SHADE,
                                      PM_PAGINATION, PM_CONTEXT };
static const unsigned cf_fields[] = { CM_BOLD, CM_ITALIC, CM_UNDERLINE, CM_STRIKE, CM_SUPER, CM_SUB, CM_CAPS, CM_FONT, CM_SIZE, CM_COLOR, CM_HL,
                                      CM_SMALLCAPS, CM_DSTRIKE, CM_UL, CM_SPACING, CM_SCALE, CM_POSITION, CM_CBORDER };
#define NGROUPS (sizeof pf_groups / sizeof pf_groups[0])
#define NFIELDS (sizeof cf_fields / sizeof cf_fields[0])
/* a and b agree in PM_ group g */
static int pf_same(const ParaFmt *a, const ParaFmt *b, unsigned g)
{
  switch (g) {
  case PM_ALIGN: return a->align == b->align;
  case PM_INDENT: return a->left == b->left && a->right == b->right && a->first == b->first;
  case PM_SPACING: return a->before == b->before && a->after == b->after;
  case PM_LINE: return a->line == b->line && a->linerule == b->linerule;
  case PM_TABS: return !memcmp(a->tabs, b->tabs, sizeof a->tabs) && !memcmp(a->tabkind, b->tabkind, sizeof a->tabkind) && !memcmp(a->tablead, b->tablead, sizeof a->tablead);
  case PM_KEEP: return a->keepnext == b->keepnext;
  case PM_LIST: return a->list == b->list && a->level == b->level;
  case PM_BREAK: return a->pagebreak == b->pagebreak && a->colbreak == b->colbreak;
  case PM_BORDER: return !memcmp(a->bstyle, b->bstyle, sizeof a->bstyle) && !memcmp(a->bwidth, b->bwidth, sizeof a->bwidth) &&
                         !memcmp(a->bspace, b->bspace, sizeof a->bspace) && !memcmp(a->bcolor, b->bcolor, sizeof a->bcolor);
  case PM_SHADE: return a->shade == b->shade;
  case PM_PAGINATION: return a->keeplines == b->keeplines && a->nowidow == b->nowidow;
  case PM_CONTEXT: return a->contextual == b->contextual;
  }
  return 1;
}
/* a and b agree in the one CM_ field f */
static int cf_same(const CharFmt *a, const CharFmt *b, unsigned f)
{
  if (f <= CM_CAPS) return (a->flags & f) == (b->flags & f);
  switch (f) {
  case CM_FONT: return a->font == b->font;
  case CM_SIZE: return a->size == b->size;
  case CM_COLOR: return a->color == b->color;
  case CM_HL: return a->hl == b->hl;
  case CM_SMALLCAPS: return (a->fx & FX_SMALLCAPS) == (b->fx & FX_SMALLCAPS);
  case CM_DSTRIKE: return (a->fx & FX_DSTRIKE) == (b->fx & FX_DSTRIKE);
  case CM_CBORDER: return (a->fx & FX_BORDER) == (b->fx & FX_BORDER);
  case CM_UL: return a->ul == b->ul;
  case CM_SPACING: return a->spacing == b->spacing;
  case CM_SCALE: return a->scale == b->scale;
  case CM_POSITION: return a->position == b->position;
  case CM_LINK: return a->link == b->link;
  case CM_BMARK: return a->bmark == b->bmark;
  }
  return 1;
}
/* the groups of a and b that differ (PM_), the fields (CM_) */
static unsigned pf_diff(const ParaFmt *a, const ParaFmt *b)
{
  unsigned m = 0, i;
  for (i = 0; i < NGROUPS; i++) if (!pf_same(a, b, pf_groups[i])) m |= pf_groups[i];
  return m;
}
/* the CM_ fields in which a and b differ */
static unsigned cf_diff(const CharFmt *a, const CharFmt *b)
{
  unsigned m = 0, i;
  for (i = 0; i < NFIELDS; i++) if (!cf_same(a, b, cf_fields[i])) m |= cf_fields[i];
  return m;
}
/* k is a style, of type (ST_PARA or ST_CHAR) */
static int style_ok(const Doc *d, int k, int type) { return k >= 0 && k < d->nstyles && d->styles[k].type == type; }
/* style k's effective formatting worked out, its base's first: the base's (or the document's
   defaults) with what k sets itself over it. state[k]: 0 not done, 1 being done, 2 done, so a loop
   of bases falls back on the defaults instead of recursing for ever */
static void style_resolve1(Doc *d, int k, u8 *state)
{
  Style *s = &d->styles[k];
  ParaFmt pf;
  CharFmt cf;
  unsigned em = 0;
  int b = s->based;
  if (state[k]) return;
  state[k] = 1;
  if (b != k && style_ok(d, b, s->type) && (style_resolve1(d, b, state), state[b] == 2)) { pf = d->styles[b].pf; cf = d->styles[b].cf; em = d->styles[b].ecmask; }
  else { pf = d->defpf; cf = d->defcf; }
  pf_apply(&pf, s->pmask & PM_STYLED, &s->opf);
  /* numbered by the style without indents of its own: the list level's */
  if ((s->pmask & PM_LIST) && !(s->pmask & PM_INDENT) && pf.list && pf.list <= d->nlists) {
    const ListLevel *lv = &d->lists[pf.list - 1].lvl[pf.level % 9];
    pf.left = lv->left; pf.first = -lv->hanging;
  }
  cf_apply(&cf, s->cmask & CM_LOOK, &s->ocf);
  memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer);
  pf.sect = 0; pf.dropcap = pf.dropmargin = 0;
  pf.style = (u8)(s->type == ST_PARA ? k : 0);
  cf.cstyle = 0; cf.rev = 0; cf.rid = 0;
  s->pf = pf; s->cf = cf; s->ecmask = em | (s->cmask & CM_LOOK);
  state[k] = 2;
}
/* see doc.h; each style is worked out once, bases first */
void doc_resolve_styles(Doc *d)
{
  u8 state[MAX_STYLES];
  int k;
  memset(state, 0, sizeof state);
  for (k = 0; k < d->nstyles; k++) if (d->styles[k].type != ST_GONE) style_resolve1(d, k, state);
}
/* what a style sets itself, from its effective formatting: what differs from its base's */
static void own_from_effective(Doc *d, Style *s)
{
  const ParaFmt *bp = &d->defpf;
  const CharFmt *bc = &d->defcf;
  if (style_ok(d, s->based, s->type)) { bp = &d->styles[s->based].pf; bc = &d->styles[s->based].cf; }
  s->pmask = s->type == ST_PARA ? pf_diff(&s->pf, bp) : 0;
  s->cmask = cf_diff(&s->cf, bc);
  s->opf = s->pf; s->ocf = s->cf;
}
/* see doc.h; every style resolved again afterwards */
int doc_add_style_from(Doc *d, const Style *sp, int based)
{
  Style s = *sp;
  int k;
  s.based = (u8)(based >= 0 ? based : NO_STYLE);
  own_from_effective(d, &s);
  if ((k = doc_add_style(d, &s)) < 0) return -1;
  doc_resolve_styles(d);
  return k;
}
/* see doc.h. Measured against a copy of the table taken first, so the order the styles come in
   doesn't matter */
void doc_styles_from_effective(Doc *d)
{
  int k;
  /* bases first: a style's own settings are measured against its base's effective formatting */
  Style *was = (Style *)malloc((d->nstyles ? d->nstyles : 1) * sizeof(Style));
  if (!was) return;
  memcpy(was, d->styles, d->nstyles * sizeof(Style));
  for (k = 0; k < d->nstyles; k++) {
    Style s = was[k];
    if (s.type == ST_GONE) continue;
    {
      const ParaFmt *bp = &d->defpf;
      const CharFmt *bc = &d->defcf;
      if (style_ok(d, s.based, s.type)) { bp = &was[s.based].pf; bc = &was[s.based].cf; }
      d->styles[k].pmask = s.type == ST_PARA ? pf_diff(&s.pf, bp) : 0;
      d->styles[k].cmask = cf_diff(&s.cf, bc);
      d->styles[k].opf = s.pf; d->styles[k].ocf = s.cf;
    }
  }
  free(was);
  doc_resolve_styles(d);
}
/* one run from one look to another: each field that had the old look's value takes the new one's.
   op, np: the paragraph style's character formatting before and after; ocs, ncs: its character
   style's (NULL: none), oem, nem: the fields that sets */
static void restyle_run(CharFmt *c, const CharFmt *op, const CharFmt *np, const CharFmt *ocs, unsigned oem, const CharFmt *ncs, unsigned nem)
{
  unsigned i;
  CharFmt was = *c;
  for (i = 0; i < NFIELDS; i++) {
    unsigned f = cf_fields[i];
    const CharFmt *o = ocs && (oem & f) ? ocs : op, *n = ncs && (nem & f) ? ncs : np;
    if (cf_same(&was, o, f)) {
      if (f <= CM_CAPS) c->flags = (u8)((c->flags & ~f) | (n->flags & f));
      else cf_apply(c, f, n);
    }
  }
}
/* a paragraph's formatting from its style as it was (opf, ocf per style, oem per character style)
   to as it is now */
static void restyle_para(Doc *d, Para *p, const ParaFmt *opf, const CharFmt *ocf, const unsigned *oem, int nold)
{
  int ps = p->pf.style < d->nstyles ? p->pf.style : 0;
  const ParaFmt *was = ps < nold ? &opf[ps] : &d->styles[ps].pf;
  unsigned i;
  ParaFmt cur = p->pf;
  for (i = 0; i < NGROUPS; i++)
    if (pf_same(&cur, was, pf_groups[i])) pf_apply(&p->pf, pf_groups[i], &d->styles[ps].pf);
  for (i = 0; i < p->nr; i++) {
    CharFmt *c = &p->r[i].f;
    int cs = c->cstyle;
    const CharFmt *op = ps < nold ? &ocf[ps] : &d->styles[ps].cf, *ocs = NULL, *ncs = NULL;
    unsigned em0 = 0, em1 = 0;
    if (cs && style_ok(d, cs, ST_CHAR)) { ocs = cs < nold ? &ocf[cs] : &d->styles[cs].cf; em0 = cs < nold ? oem[cs] : d->styles[cs].ecmask; ncs = &d->styles[cs].cf; em1 = d->styles[cs].ecmask; }
    restyle_run(c, op, &d->styles[ps].cf, ocs, em0, ncs, em1);
  }
  para_normalise(p);
  touch(p);
}
/* every paragraph whose style (or any of whose text's character styles) differs from before; the
   body's undoably, headers, footers and notes as they are */
static int restyle_all(Doc *d, const ParaFmt *opf, const CharFmt *ocf, const unsigned *oem, int nold)
{
  u8 differs[MAX_STYLES];
  u32 i, k;
  int j, w;
  for (j = 0; j < d->nstyles; j++)
    differs[j] = (u8)(j >= nold || pf_diff(&opf[j], &d->styles[j].pf) || cf_diff(&ocf[j], &d->styles[j].cf) || oem[j] != d->styles[j].ecmask);
  for (i = 0; i < d->np; ) {
    u32 e = i;
    /* a run of paragraphs that need it, saved for undo in one step */
    while (e < d->np) {
      Para *p = d->p[e];
      int need = differs[p->pf.style < d->nstyles ? p->pf.style : 0];
      for (k = 0; !need && k < p->nr; k++) need = p->r[k].f.cstyle && p->r[k].f.cstyle < d->nstyles && differs[p->r[k].f.cstyle];
      if (!need) break;
      e++;
    }
    if (e > i) {
      if (!rec(d, i, e - i, para_pos(i))) return 0;
      for (k = i; k < e; k++) restyle_para(d, d->p[k], opf, ocf, oem, nold);
      i = e;
    } else i++;
  }
  for (w = 0; w < HF_N * 2; w++) {
    Story *st = w < HF_N ? &d->hdr[w] : &d->ftr[w - HF_N];
    for (i = 0; i < st->np; i++) restyle_para(d, st->p[i], opf, ocf, oem, nold);
  }
  for (j = 0; j < d->nnotes; j++) for (i = 0; i < d->notes[j].np; i++) restyle_para(d, d->notes[j].p[i], opf, ocf, oem, nold);
  for (j = 0; j < d->nenotes; j++) for (i = 0; i < d->enotes[j].np; i++) restyle_para(d, d->enotes[j].p[i], opf, ocf, oem, nold);
  return 1;
}
/* the effective formatting of every style, saved to compare with afterwards */
static int save_effective(Doc *d, ParaFmt **opf, CharFmt **ocf, unsigned **oem)
{
  int n = d->nstyles ? d->nstyles : 1, k;
  *opf = (ParaFmt *)malloc(n * sizeof(ParaFmt)); *ocf = (CharFmt *)malloc(n * sizeof(CharFmt)); *oem = (unsigned *)malloc(n * sizeof(unsigned));
  if (!*opf || !*ocf || !*oem) { free(*opf); free(*ocf); free(*oem); return 0; }
  for (k = 0; k < d->nstyles; k++) { (*opf)[k] = d->styles[k].pf; (*ocf)[k] = d->styles[k].cf; (*oem)[k] = d->styles[k].ecmask; }
  return 1;
}
/* an id from a style's name: its ASCII letters and digits (30 at most), or "Style", with 2, 3...
   added until no style but self has it. id must hold 40 bytes */
static void make_id(Doc *d, const char *name, char *id, int self)
{
  char base[40];
  int i, n = 0, k;
  for (i = 0; name[i] && n < 30; i++) {
    u8 c = (u8)name[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) base[n++] = (char)c;
  }
  base[n] = 0;
  if (!n) strcpy(base, "Style");
  strcpy(id, base);
  for (k = 2; (i = doc_style(d, id)) >= 0 && i != self; k++) sprintf(id, "%s%d", base, k);
}
/* see doc.h. The effective formatting of every style is saved first, so restyle_all can tell what
   text had the old look. Normal (0) can't be based on anything */
int doc_set_style(Doc *d, int k, const Style *ns)
{
  ParaFmt *opf;
  CharFmt *ocf;
  unsigned *oem;
  Style *s;
  int b, steps, nold = d->nstyles, ok;
  Pos here = para_pos(0);
  if (ns->type != ST_PARA && ns->type != ST_CHAR) return -1;
  if (k >= 0 && (k >= d->nstyles || d->styles[k].type != ns->type)) return -1;
  if (k == 0 && ns->based != NO_STYLE) return -1;              /* Normal is based on nothing */
  if (k < 0 && d->nstyles >= MAX_STYLES) return -1;
  /* no loops of bases */
  for (b = ns->based, steps = 0; b != NO_STYLE && steps < MAX_STYLES; b = d->styles[b].based, steps++) {
    if (!style_ok(d, b, ns->type) || b == k) return -1;
  }
  if (steps >= MAX_STYLES) return -1;
  if (!save_effective(d, &opf, &ocf, &oem)) return -1;
  doc_begin(d, here);
  if (!rec_styles(d, here)) { doc_end(d, here); free(opf); free(ocf); free(oem); return -1; }
  if (k < 0) {
    k = d->nstyles++;
    memset(&d->styles[k], 0, sizeof(Style));
    d->styles[k].type = ns->type;
    d->styles[k].next = (u8)k;
  }
  s = &d->styles[k];
  if (ns->name[0]) { strncpy(s->name, ns->name, sizeof s->name - 1); s->name[sizeof s->name - 1] = 0; }
  if (!s->id[0] || (ns->id[0] && strcmp(ns->id, s->id))) {
    if (ns->id[0] && (doc_style(d, ns->id) < 0 || doc_style(d, ns->id) == k)) strcpy(s->id, ns->id);
    else make_id(d, s->name[0] ? s->name : "Style", s->id, k);
  }
  if (!s->name[0]) strcpy(s->name, s->id);
  s->based = ns->based;
  if (ns->type == ST_PARA && style_ok(d, ns->next, ST_PARA)) s->next = ns->next;
  s->quick = ns->quick;
  s->pmask = ns->type == ST_PARA ? ns->pmask & PM_STYLED : 0;
  s->cmask = ns->cmask & CM_LOOK;
  s->opf = ns->opf; s->ocf = ns->ocf;
  doc_resolve_styles(d);
  ok = restyle_all(d, opf, ocf, oem, nold);
  changed(d);
  doc_end(d, here);
  free(opf); free(ocf); free(oem);
  return ok ? k : -1;
}
/* see doc.h. The style keeps its slot as ST_GONE, as paragraphs and undo steps refer to styles by
   index; styles based on it take over what it set, so they look the same */
int doc_delete_style(Doc *d, int k)
{
  ParaFmt *opf;
  CharFmt *ocf;
  unsigned *oem;
  Style *s;
  int j, base, ok;
  u32 i, r;
  Pos here = para_pos(0);
  if (k <= 0 || k >= d->nstyles || (d->styles[k].type != ST_PARA && d->styles[k].type != ST_CHAR)) return 0;
  if (!save_effective(d, &opf, &ocf, &oem)) return 0;
  doc_begin(d, here);
  if (!rec_styles(d, here)) { doc_end(d, here); free(opf); free(ocf); free(oem); return 0; }
  s = &d->styles[k];
  base = style_ok(d, s->based, s->type) ? s->based : (s->type == ST_PARA ? 0 : NO_STYLE);
  /* styles based on it: on its base, with what it set as their own (so they look the same) */
  for (j = 0; j < d->nstyles; j++) {
    Style *c = &d->styles[j];
    unsigned g;
    if (j == k || c->type != s->type) continue;
    if (c->based == k) {
      for (g = 0; g < NGROUPS; g++) if ((s->pmask & pf_groups[g]) && !(c->pmask & pf_groups[g])) { pf_apply(&c->opf, pf_groups[g], &s->opf); c->pmask |= pf_groups[g]; }
      for (g = 0; g < NFIELDS; g++) if ((s->cmask & cf_fields[g]) && !(c->cmask & cf_fields[g])) { cf_apply(&c->ocf, cf_fields[g], &s->ocf); c->cmask |= cf_fields[g]; }
      c->based = (u8)base;
    }
    if (c->next == k) c->next = (u8)j;
  }
  s->type = ST_GONE;
  doc_resolve_styles(d);
  /* its paragraphs to its base; its text without it (keeping the look) */
  ok = 1;
  for (i = 0; i < d->np && ok; i++) {
    Para *p = d->p[i];
    int need = p->pf.style == k;
    for (r = 0; !need && r < p->nr; r++) need = p->r[r].f.cstyle == k;
    if (!need) continue;
    if (!rec(d, i, 1, para_pos(i))) { ok = 0; break; }
    if (p->pf.style == k) {
      p->pf.style = (u8)base;
      /* from its look (opf[k]) to its base's, the paragraph's own formatting kept */
      {
        ParaFmt cur = p->pf;
        unsigned g;
        for (g = 0; g < NGROUPS; g++) if (pf_same(&cur, &opf[k], pf_groups[g])) pf_apply(&p->pf, pf_groups[g], &d->styles[base].pf);
        for (r = 0; r < p->nr; r++) {
          CharFmt *c = &p->r[r].f;
          int cs = c->cstyle;
          const CharFmt *csf = cs && style_ok(d, cs, ST_CHAR) ? &d->styles[cs].cf : NULL;
          unsigned em = csf ? d->styles[cs].ecmask : 0;
          restyle_run(c, &ocf[k], &d->styles[base].cf, csf, em, csf, em);
        }
      }
    }
    for (r = 0; r < p->nr; r++) if (p->r[r].f.cstyle == k) p->r[r].f.cstyle = 0;
    para_normalise(p);
    touch(p);
  }
  if (ok) ok = restyle_all(d, opf, ocf, oem, d->nstyles);       /* styles that were based on it (should look the same) */
  changed(d);
  doc_end(d, here);
  free(opf); free(ocf); free(oem);
  return ok;
}
/* see doc.h. A quick paragraph style, based on Normal if base isn't a paragraph style; next is
   NO_STYLE, so doc_set_style makes a new style its own next */
void doc_style_from(Doc *d, Pos p, int base, Style *s)
{
  Para *q;
  memset(s, 0, sizeof *s);
  p = pos_clamp(d, p);
  q = d->p[p.para];
  s->type = ST_PARA; s->based = (u8)(style_ok(d, base, ST_PARA) ? base : 0); s->quick = 1;
  s->pf = q->pf;
  s->cf = *para_fmt_at(q, p.off);
  s->cf.cstyle = 0; s->cf.rev = 0; s->cf.rid = 0;
  own_from_effective(d, s);
  s->next = NO_STYLE;
}
/* see doc.h; one undo step. What the old character style set goes back to the paragraph style's,
   then the new one's goes on; direct formatting stays */
int doc_char_style(Doc *d, Pos a, Pos b, int k)
{
  u32 pi;
  if (k && !style_ok(d, k, ST_CHAR)) return 0;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  if (!rec(d, a.para, b.para - a.para + 1, a)) { close_auto(d); return 0; }
  for (pi = a.para; pi <= b.para; pi++) {
    Para *p = d->p[pi];
    const CharFmt *pc = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0].cf;
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, i, start = 0;
    if (p->len && (!split_run(p, s) || !split_run(p, e))) break;
    for (i = 0; i < p->nr; i++) {
      if (!p->len || (start >= s && p->r[i].end <= e)) {
        CharFmt *c = &p->r[i].f;
        int os = c->cstyle;
        const CharFmt *ocs = os && style_ok(d, os, ST_CHAR) ? &d->styles[os].cf : NULL, *ncs = k ? &d->styles[k].cf : NULL;
        restyle_run(c, pc, pc, ocs, ocs ? d->styles[os].ecmask : 0, ncs, k ? d->styles[k].ecmask : 0);
        c->cstyle = (u8)k;
      }
      start = p->r[i].end;
    }
    para_normalise(p);
    touch(p);
  }
  changed(d);
  close_auto(d);
  return 1;
}
/* a built-in paragraph style for doc_new: size in half-points, spacing and indents in twips. Each
   is based on Normal (Normal, the first, on nothing) and is its own next style */
static void builtin(Doc *d, const char *id, const char *name, const char *font, int size, int flags,
                    i32 before, i32 after, int keep, i32 left, i32 right)
{
  Style s;
  memset(&s, 0, sizeof s);
  strcpy(s.id, id); strcpy(s.name, name);
  s.type = ST_PARA; s.builtin = 1; s.quick = 1;
  s.cf = d->defcf;
  s.cf.font = (u8)doc_font(d, font); s.cf.size = (u16)size; s.cf.flags = (u8)flags;
  s.pf = d->defpf;
  s.pf.before = before; s.pf.after = after;
  s.pf.keepnext = (u8)keep; s.pf.left = left; s.pf.right = right;
  s.pf.style = (u8)d->nstyles;
  s.next = (u8)d->nstyles;
  doc_add_style_from(d, &s, d->nstyles ? 0 : -1);           /* each based on Normal */
}
/* a built-in character style setting only the fields cmask names (from flags and color) */
static void builtin_char(Doc *d, const char *id, const char *name, unsigned cmask, int flags, u32 color)
{
  Style s;
  memset(&s, 0, sizeof s);
  strcpy(s.id, id); strcpy(s.name, name);
  s.type = ST_CHAR; s.builtin = 1; s.quick = 1; s.based = NO_STYLE;
  s.cmask = cmask; s.ocf.flags = (u8)flags; s.ocf.color = color;
  doc_add_style(d, &s);
  doc_resolve_styles(d);
}
/* see doc.h. A4, Word's defaults under the styles, and Normal, three headings, Title, Subtitle,
   Quote, List Paragraph and Plain Text, with five character styles */
Doc *doc_new(void)
{
  Doc *d = (Doc *)calloc(1, sizeof(Doc));
  Para *p;
  int i;
  if (!d) return NULL;
  if (!(d->undo = (Undo *)calloc(1, sizeof(Undo)))) { free(d); return NULL; }
  d->serial = ++doc_serials;
  d->look[0] = d->look[1] = d->look[2] = d->look[3] = -1;
  /* A4 with 2.54 cm margins: Europe's default; Letter documents bring their own page size */
  d->page.width = 11906; d->page.height = 16838;
  d->page.top = d->page.bottom = d->page.left = d->page.right = 1440;
  d->page.header = d->page.footer = 708;
  /* the defaults under every style: Word's (Times New Roman 10 pt, single spacing) */
  d->defpf.line = 240; d->defpf.linerule = LS_AUTO;
  d->defcf.font = (u8)doc_font(d, "Times New Roman"); d->defcf.size = 20; d->defcf.color = COLOR_AUTO;
  builtin(d, "Normal", "Normal", "Times New Roman", 24, 0, 0, 120, 0, 0, 0);
  builtin(d, "Heading1", "heading 1", "Arial", 32, CF_BOLD, 240, 120, 1, 0, 0);
  builtin(d, "Heading2", "heading 2", "Arial", 28, CF_BOLD, 200, 100, 1, 0, 0);
  builtin(d, "Heading3", "heading 3", "Arial", 24, CF_BOLD, 160, 80, 1, 0, 0);
  builtin(d, "Title", "Title", "Arial", 52, 0, 0, 240, 0, 0, 0);
  builtin(d, "Subtitle", "Subtitle", "Arial", 30, CF_ITALIC, 0, 240, 0, 0, 0);
  builtin(d, "Quote", "Quote", "Times New Roman", 24, CF_ITALIC, 120, 120, 0, 720, 720);
  builtin(d, "ListParagraph", "List Paragraph", "Times New Roman", 24, 0, 0, 60, 0, 720, 0);
  builtin(d, "PlainText", "Plain Text", "Courier New", 20, 0, 0, 0, 0, 0, 0);
  for (i = 1; i <= 6; i++) d->styles[i].next = 0;              /* after headings and titles: Normal */
  builtin_char(d, "Strong", "Strong", CM_BOLD, CF_BOLD, COLOR_AUTO);
  builtin_char(d, "Emphasis", "Emphasis", CM_ITALIC, CF_ITALIC, COLOR_AUTO);
  builtin_char(d, "SubtleEmphasis", "Subtle Emphasis", CM_ITALIC | CM_COLOR, CF_ITALIC, 0x404040);
  builtin_char(d, "IntenseEmphasis", "Intense Emphasis", CM_ITALIC | CM_COLOR, CF_ITALIC, 0x2F5496);
  builtin_char(d, "Hyperlink", "Hyperlink", CM_UNDERLINE | CM_COLOR, CF_UNDERLINE, 0x0563C1);
  if (!(p = para_new(&d->styles[0].pf, &d->styles[0].cf)) || !doc_insert_para(d, 0, p)) { para_free(p); doc_free(d); return NULL; }
  return d;
}
/* an undo history and both its stacks freed; NULL is fine */
static void undo_free(Undo *u)
{
  if (!u) return;
  stack_clear(u->u, &u->nu); free(u->u);
  stack_clear(u->r, &u->nr); free(u->r);
  free(u);
}
/* everything the document owns freed, a story being edited dropped first; NULL is fine */
void doc_free(Doc *d)
{
  u32 i;
  if (!d) return;
  doc_story_end(d, 0);
  for (i = 0; i < d->np; i++) para_free(d->p[i]);
  free(d->p);
  for (i = 0; i < (u32)d->nlinks; i++) free(d->links[i]);
  free(d->links);
  for (i = 0; i < (u32)d->nbmarks; i++) free(d->bmarks[i]);
  free(d->bmarks);
  free(d->lists);
  free(d->tables);
  free(d->tstyles);
  for (i = 0; i < (u32)d->nsects; i++) hf_free(d->sects[i].hf);
  free(d->sects);
  for (i = 0; i < (u32)d->nimages; i++) img_release(&d->images[i]);
  free(d->images);
  for (i = 0; i < (u32)d->nnotes; i++) story_free(&d->notes[i]);
  free(d->notes);
  for (i = 0; i < (u32)d->ncomments; i++) story_free(&d->comments[i].text);
  free(d->comments);
  free(d->revs);
  for (i = 0; i < (u32)d->nenotes; i++) story_free(&d->enotes[i]);
  free(d->enotes);
  for (i = 0; i < HF_N; i++) { story_free(&d->hdr[i]); story_free(&d->ftr[i]); }
  undo_free(d->undo);
  keep_free(d);
  notes_free(d);                 /* (WP22b) */
  info_free(d);                  /* (WP24) */
  math_doc_free(d);     /* WP14a: equations */
  free(d);
}

/* ------------------------------------------------------------------ */
/* positions */
/* -1, 0 or 1 as a is before, at or after b */
int pos_cmp(Pos a, Pos b)
{
  if (a.para != b.para) return a.para < b.para ? -1 : 1;
  if (a.off != b.off) return a.off < b.off ? -1 : 1;
  return 0;
}
/* p moved into the document: past the last paragraph to its end, past a paragraph's end to that
   end. It doesn't move it onto a character boundary */
Pos pos_clamp(Doc *d, Pos p)
{
  if (p.para >= d->np) { p.para = d->np - 1; p.off = d->p[p.para]->len; }
  if (p.off > d->p[p.para]->len) p.off = d->p[p.para]->len;
  return p;
}
/* the first and the last position in the document */
Pos doc_start(Doc *d) { Pos p; (void)d; p.para = 0; p.off = 0; return p; }
Pos doc_end_pos(Doc *d) { Pos p; p.para = d->np - 1; p.off = d->p[p.para]->len; return p; }
/* comment marks have no width: the caret steps over them with the character next to them */
Pos pos_left(Doc *d, Pos p)
{
  p = pos_clamp(d, p);
  if (p.off) {
    const char *t = d->p[p.para]->t;
    p.off = (u32)utf8_prev(t, p.off);
    while (p.off >= 4 && IS_MARK(t + p.off)) p.off = (u32)utf8_prev(t, p.off);
  }
  else if (p.para) { p.para--; p.off = d->p[p.para]->len; }
  return p;
}
/* one character right (comment marks stepped over on both sides), or to the next paragraph's start */
Pos pos_right(Doc *d, Pos p)
{
  Para *q;
  p = pos_clamp(d, p);
  q = d->p[p.para];
  if (p.off < q->len) {
    while (p.off + 4 <= q->len && IS_MARK(q->t + p.off)) p.off += 4;
    if (p.off < q->len) p.off = (u32)utf8_next(q->t, q->len, p.off);
    while (p.off + 4 <= q->len && IS_MARK(q->t + p.off)) p.off += 4;
  }
  else if (p.para + 1 < d->np) { p.para++; p.off = 0; }
  return p;
}
/* the character at off is part of a word: an ASCII letter, digit, _ or apostrophe, or anything
   above ASCII but the no-break space, general punctuation (U+2000-U+206F), fields (U+E000-U+E0FF)
   and object references */
static int is_word(const char *t, u32 len, u32 off)
{
  int used;
  u32 c = utf8_get(t + off, len - off, &used);
  return c > 127 ? (c != 0xA0 && !(c >= 0x2000 && c <= 0x206F) && !(c >= 0xE000 && c <= 0xE0FF) && c < 0xF0000) : ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '\'');
}
/* to the start of the word before p (over any gap first); at a paragraph's start, to the end of
   the one before */
Pos pos_word_left(Doc *d, Pos p)
{
  Para *q;
  p = pos_clamp(d, p);
  if (!p.off) return pos_left(d, p);
  q = d->p[p.para];
  while (p.off && !is_word(q->t, q->len, (u32)utf8_prev(q->t, p.off))) p.off = (u32)utf8_prev(q->t, p.off);
  while (p.off && is_word(q->t, q->len, (u32)utf8_prev(q->t, p.off))) p.off = (u32)utf8_prev(q->t, p.off);
  return p;
}
/* over the rest of this word and the gap after it, to the next word's start; at a paragraph's end,
   to the next paragraph's start */
Pos pos_word_right(Doc *d, Pos p)
{
  Para *q;
  p = pos_clamp(d, p);
  q = d->p[p.para];
  if (p.off >= q->len) return pos_right(d, p);
  while (p.off < q->len && is_word(q->t, q->len, p.off)) p.off = (u32)utf8_next(q->t, q->len, p.off);
  while (p.off < q->len && !is_word(q->t, q->len, p.off)) p.off = (u32)utf8_next(q->t, q->len, p.off);
  return p;
}
/* see doc.h. Cells of one table row are separated by tabs instead. *len (may be NULL) gets its
   length; the caller frees it */
char *doc_text(Doc *d, Pos a, Pos b, u32 *len)
{
  Buf o;
  u32 pi;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  buf_init(&o);
  for (pi = a.para; pi <= b.para; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len;
    if (e > s) buf_add(&o, p->t + s, e - s);
    if (pi < b.para) {                /* cells of a table row: tab-separated */
      const ParaFmt *x = &p->pf, *y = &d->p[pi + 1]->pf;
      int tab = in_table(x) && y->cell.table == x->cell.table && y->cell.row == x->cell.row && !same_cell(x, y);
      buf_addc(&o, tab ? '\t' : '\n');
    }
  }
  if (len) *len = (u32)o.len;
  return buf_take(&o);
}
/* the body's words, as runs of word characters (is_word); headers, footers and notes aren't
   counted */
u32 doc_count_words(Doc *d)
{
  u32 pi, n = 0;
  for (pi = 0; pi < d->np; pi++) {
    Para *p = d->p[pi];
    u32 o = 0;
    int in = 0;
    while (o < p->len) {
      int w = is_word(p->t, p->len, o);
      if (w && !in) n++;
      in = w;
      o = (u32)utf8_next(p->t, p->len, o);
    }
  }
  return n;
}

/* ------------------------------------------------------------------ */
/* find and replace */
/* a character folded for finding regardless of case: ASCII, Latin-1, and most of Latin Extended-A
   (capital then small letter in pairs) */
static u32 fold(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;         /* Latin-1 capitals */
  if (c >= 0x100 && c < 0x180 && !(c & 1)) return c + 1;          /* Latin Extended-A pairs (mostly) */
  return c;
}
/* does what match p's text at off? *end: where the match ends */
static int match_at(const Para *p, u32 off, const char *what, u32 wn, unsigned flags, u32 *end)
{
  u32 o = off, k = 0;
  while (k < wn) {
    int u1, u2;
    u32 c1, c2;
    if (o >= p->len) return 0;
    c1 = utf8_get(p->t + o, p->len - o, &u1);
    c2 = utf8_get(what + k, wn - k, &u2);
    if (flags & FIND_CASE ? c1 != c2 : fold(c1) != fold(c2)) return 0;
    o += (u32)u1; k += (u32)u2;
  }
  if (flags & FIND_WORD) {
    if (off > 0 && is_word(p->t, p->len, (u32)utf8_prev(p->t, off))) return 0;
    if (o < p->len && is_word(p->t, p->len, o)) return 0;
  }
  *end = o;
  return 1;
}
/* see doc.h; FIND_BACK goes backwards from 'from'. Each paragraph is looked at once, from's own
   twice: the part after it first, the part before it last (the other way round going back) */
int doc_find(Doc *d, Pos from, const char *what, unsigned flags, Pos *a, Pos *b)
{
  u32 wn = (u32)strlen(what), n, pi, e;
  if (!wn || !d->np) return 0;
  from = pos_clamp(d, from);
  /* every paragraph once, from the start one round to it again */
  for (n = 0; n <= d->np; n++) {
    Para *p;
    u32 o, lo, hi;
    if (flags & FIND_BACK) pi = (from.para + d->np - n % d->np) % d->np;
    else pi = (from.para + n) % d->np;
    p = d->p[pi];
    lo = 0; hi = p->len;
    if (n == 0) { if (flags & FIND_BACK) hi = from.off; else lo = from.off; }
    else if (n == d->np) { if (flags & FIND_BACK) lo = from.off; else hi = from.off; }
    if (flags & FIND_BACK) {
      for (o = hi; o-- > lo; ) {
        if ((p->t[o] & 0xC0) == 0x80) continue;
        if (match_at(p, o, what, wn, flags, &e) && e <= hi) { a->para = b->para = pi; a->off = o; b->off = e; return 1; }
      }
    } else {
      for (o = lo; o < hi; o = (u32)utf8_next(p->t, p->len, o))
        if (match_at(p, o, what, wn, flags, &e)) { a->para = b->para = pi; a->off = o; b->off = e; return 1; }
    }
  }
  return 0;
}
/* see doc.h. Each match is deleted and 'with' put in, formatted as the match's first character;
   the search goes on after the replacement, so 'with' containing 'what' can't loop */
u32 doc_replace_all(Doc *d, const char *what, const char *with, unsigned flags)
{
  u32 wn = (u32)strlen(what), count = 0, pi;
  if (!wn) return 0;
  doc_begin(d, doc_start(d));
  for (pi = 0; pi < d->np; pi++) {
    u32 o = 0, e;
    while (o < d->p[pi]->len) {
      if (match_at(d->p[pi], o, what, wn, flags, &e)) {
        Pos a, b;
        CharFmt f = *para_fmt_at(d->p[pi], o);
        a.para = b.para = pi; a.off = o; b.off = e;
        doc_delete(d, a, b);
        if (*with) doc_insert(d, &a, with, (u32)strlen(with), &f);
        o = a.off;                         /* after the replacement (it may hold a newline: then a moved on) */
        if (a.para != pi) { pi = a.para; }
        count++;
        if (!*with && o >= d->p[pi]->len) break;
      } else o = (u32)utf8_next(d->p[pi]->t, d->p[pi]->len, o);
    }
  }
  doc_end(d, doc_start(d));
  return count;
}

/* ------------------------------------------------------------------ */
/* headers and footers */
/* a story's paragraphs freed, the story left empty (the Story itself isn't freed) */
void story_free(Story *s)
{
  u32 i;
  for (i = 0; i < s->np; i++) para_free(s->p[i]);
  free(s->p);
  s->p = NULL; s->np = 0;
}
/* see doc.h. Control characters but tab are dropped. The old text is freed first, even if this
   then fails */
int story_set_text(Doc *d, Story *s, const char *text, int style, int align)
{
  ParaFmt pf;
  CharFmt cf;
  Para *p;
  Buf b;
  const char *t;
  story_free(s);
  if (!text || !*text) return 1;
  if (style < 0 || style >= d->nstyles) style = 0;
  pf = d->styles[style].pf; pf.style = (u8)style; pf.align = (u8)align;
  memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer); pf.list = 0;
  cf = d->styles[style].cf;
  buf_init(&b);
  for (t = text; *t; ) {
    if (!strncmp(t, "{page}", 6)) { buf_adds(&b, FIELD_PAGE); t += 6; }
    else if (!strncmp(t, "{pages}", 7)) { buf_adds(&b, FIELD_PAGES); t += 7; }
    else if ((u8)*t >= 0x20 || *t == '\t') buf_addc(&b, *t++);
    else t++;
  }
  if (b.oom || !(p = para_new(&pf, &cf)) || !(s->p = (Para **)malloc(sizeof(Para *)))) { buf_free(&b); return 0; }
  if (b.len && !para_append(p, b.data, (u32)b.len, &cf)) { para_free(p); free(s->p); s->p = NULL; buf_free(&b); return 0; }
  buf_free(&b);
  s->p[0] = p; s->np = 1;
  return 1;
}
/* see doc.h; s is replaced only on success */
int story_set_paras(Doc *d, Story *s, const char *text, const Story *like)
{
  ParaFmt pf;
  CharFmt cf;
  Story n;
  const char *t = text, *e;
  if (like && like->np) { pf = like->p[0]->pf; cf = like->p[0]->nr ? like->p[0]->r[0].f : d->styles[pf.style < d->nstyles ? pf.style : 0].cf; }
  else { pf = d->styles[0].pf; pf.style = 0; cf = d->styles[0].cf; }
  memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer);
  n.p = NULL; n.np = 0;
  do {
    Para *p, **np;
    e = strchr(t, '\n');
    if (!e) e = t + strlen(t);
    if (!(np = (Para **)realloc(n.p, (n.np + 1) * sizeof(Para *))) || !(p = para_new(&pf, &cf))) { if (np) n.p = np; story_free(&n); return 0; }
    n.p = np;
    n.p[n.np++] = p;
    if (e > t && !para_append(p, t, (u32)(e - t), &cf)) { story_free(&n); return 0; }
    t = *e ? e + 1 : e;
  } while (*e);
  story_free(s);
  *s = n;
  return 1;
}
/* see doc.h; NULL s gives "". The caller frees it */
char *story_paras(const Story *s)
{
  Buf b;
  u32 i;
  buf_init(&b);
  for (i = 0; s && i < s->np; i++) {
    if (i) buf_addc(&b, '\n');
    buf_add(&b, s->p[i]->t, s->p[i]->len);
  }
  buf_addc(&b, 0);
  return buf_take(&b);
}
/* see doc.h; the caller frees it */
char *story_text(const Story *s)
{
  Buf b;
  u32 i, k;
  buf_init(&b);
  for (i = 0; i < s->np; i++) {
    const Para *p = s->p[i];
    if (i && b.len) buf_adds(&b, " / ");
    for (k = 0; k < p->len; ) {
      if (!strncmp(p->t + k, FIELD_PAGE, 3)) { buf_adds(&b, "{page}"); k += 3; }
      else if (!strncmp(p->t + k, FIELD_PAGES, 3)) { buf_adds(&b, "{pages}"); k += 3; }
      else buf_addc(&b, p->t[k++]);
    }
  }
  buf_addc(&b, 0);
  return buf_take(&b);
}

/* ------------------------------------------------------------------ */
/* stories edited in the body's place */
/* see doc.h. While a story is being edited the document's headers and footers are put aside in
   Doc.edit, so they are found there */
Story *doc_story(Doc *d, int kind, int index, int sect)
{
  switch (kind) {
  case SK_HEADER: case SK_FOOTER:
    if (index < 0 || index >= HF_N) return NULL;
    if (sect) return sect <= d->nsects && d->sects[sect - 1].hf ? &d->sects[sect - 1].hf[(kind == SK_FOOTER ? HF_N : 0) + index] : NULL;
    if (d->edit.kind) return kind == SK_HEADER ? &d->edit.hdr[index] : &d->edit.ftr[index];
    return kind == SK_HEADER ? &d->hdr[index] : &d->ftr[index];
  case SK_NOTE: return index >= 0 && index < d->nnotes ? &d->notes[index] : NULL;
  case SK_ENOTE: return index >= 0 && index < d->nenotes ? &d->enotes[index] : NULL;
  case SK_COMMENT: return index >= 0 && index < d->ncomments ? &d->comments[index].text : NULL;
  case SK_TEXTBOX: return index >= 0 && index < d->nimages ? d->images[index].text : NULL;
  }
  return NULL;
}
/* see doc.h. The story's paragraphs are copied in (the story itself is untouched until
   doc_story_end) with an undo history of their own, and Doc.page becomes the story's text area for
   layout; the watermark, page border, headers and footers are put aside meanwhile */
int doc_story_begin(Doc *d, int kind, int index, int sect, u32 fpage, u32 fpages)
{
  static const char *const sty[SK_TEXTBOX + 1] = { "", "Header", "Footer", "FootnoteText", "EndnoteText", "CommentText", "" };
  StoryEdit *e = &d->edit;
  Story *s;
  Para **np;
  Undo *u;
  PageSetup pg = d->page;
  u32 i, n;
  if (e->kind || kind <= SK_NONE || kind > SK_TEXTBOX) return 0;
  if (kind == SK_TEXTBOX && index >= 0 && index < d->nimages && !d->images[index].text &&
      !(d->images[index].text = (Story *)calloc(1, sizeof(Story)))) return 0;
  if (!(s = doc_story(d, kind, index, sect))) return 0;
  n = s->np ? s->np : 1;
  np = (Para **)malloc(n * sizeof(Para *));
  u = (Undo *)calloc(1, sizeof(Undo));
  if (!np || !u) { free(np); free(u); return 0; }
  for (i = 0; i < s->np; i++)
    if (!(np[i] = para_copy(s->p[i]))) { while (i-- > 0) para_free(np[i]); free(np); free(u); return 0; }
  if (!s->np) {
    int st = doc_style(d, sty[kind]);
    ParaFmt pf;
    if (st < 0 && kind == SK_ENOTE) st = doc_style(d, "FootnoteText");
    if (st < 0) st = 0;
    pf = d->styles[st].pf; pf.style = (u8)st;
    memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer);
    if (!(np[0] = para_new(&pf, &d->styles[st].cf))) { free(np); free(u); return 0; }
    if (st == 0 && (kind == SK_NOTE || kind == SK_ENOTE || kind == SK_COMMENT)) np[0]->r[0].f.size = 20;    /* 10 pt */
  }
  /* the text area: a header's or footer's from its distance from the page's edge; a text box's its width */
  if (kind == SK_HEADER || kind == SK_FOOTER) {
    if (sect && d->sects[sect - 1].haspage) pg = d->sects[sect - 1].page;
    pg.top = kind == SK_HEADER ? pg.header : pg.footer;
    pg.bottom = pg.top;
  } else if (kind == SK_TEXTBOX) {
    i32 w = d->images[index].cx - 2 * 144;   /* layout.h's TB_INSET_X each side */
    if (w < 360) w = 360;
    if (pg.width < w + 1440) pg.width = w + 1440;
    pg.left = (pg.width - w) / 2; pg.right = pg.width - pg.left - w;
  }
  e->p = d->p; e->np = d->np; e->pcap = d->pcap; e->undo = d->undo; e->page = d->page;
  e->wm = (u8)d->wm.text[0]; d->wm.text[0] = 0;
  e->border = d->border.style; d->border.style = PB_NONE;
  memcpy(e->hdr, d->hdr, sizeof e->hdr); memcpy(e->ftr, d->ftr, sizeof e->ftr);
  memset(d->hdr, 0, sizeof d->hdr); memset(d->ftr, 0, sizeof d->ftr);
  d->p = np; d->np = n; d->pcap = n; d->undo = u; d->page = pg;
  e->kind = kind; e->index = index; e->sect = sect; e->fpage = fpage; e->fpages = fpages;
  d->ver++;
  e->ver0 = d->ver;
  return 1;
}
/* see doc.h. The edited paragraphs are kept only if anything changed; Doc.ver is bumped either way
   so views redraw */
int doc_story_end(Doc *d, int keep)
{
  StoryEdit *e = &d->edit;
  Para **sp = d->p;
  u32 sn = d->np, i;
  int kind = e->kind, changed_ = d->ver != e->ver0;
  Story *s;
  if (!kind) return 0;
  undo_free(d->undo);
  d->p = e->p; d->np = e->np; d->pcap = e->pcap; d->undo = e->undo; d->page = e->page;
  d->wm.text[0] = (char)e->wm; d->border.style = e->border;
  memcpy(d->hdr, e->hdr, sizeof d->hdr); memcpy(d->ftr, e->ftr, sizeof d->ftr);
  e->kind = SK_NONE; e->p = NULL; e->np = 0; e->undo = NULL;
  s = keep && changed_ ? doc_story(d, kind, e->index, e->sect) : NULL;
  if (s) {
    if ((kind == SK_HEADER || kind == SK_FOOTER) && sn == 1 && !sp[0]->len) { para_free(sp[0]); sn = 0; }   /* nothing in it: none */
    for (i = 0; i < sn; i++) sp[i]->pf.sect = 0;           /* (a story has no sections) */
    story_free(s);
    if (sn) { s->p = sp; s->np = sn; } else free(sp);
    d->dirty = 1;
  } else {
    for (i = 0; i < sn; i++) para_free(sp[i]);
    free(sp);
  }
  d->ver++;
  return 1;
}

/* ------------------------------------------------------------------ */
/* hyperlinks. A run's CharFmt.link numbers a target in Doc.links (1-based, so 0 can mean "no
   link"); the table only grows, so undo never leaves a run pointing nowhere */
int doc_link(Doc *d, const char *url)
{
  int i;
  char **nl, *u;
  if (!url || !url[0]) return 0;
  for (i = 0; i < d->nlinks; i++) if (!strcmp(d->links[i], url)) return i + 1;
  if (d->nlinks >= 0xFFFE || !(u = str_ndup(url, (u32)strlen(url)))) return 0;
  if (!(nl = (char **)realloc(d->links, (size_t)(d->nlinks + 1) * sizeof(char *)))) { free(u); return 0; }
  d->links = nl;
  d->links[d->nlinks++] = u;
  return d->nlinks;
}
/* see doc.h: the address of link (Doc.links index + 1), "" for 0 or one out of range */
const char *doc_link_url(const Doc *d, int link)
{
  return link > 0 && link <= d->nlinks ? d->links[link - 1] : "";
}
/* see doc.h: the run at p (or just before it, so the caret at a link's end still finds it), then
   the neighbouring runs with the same link */
int doc_link_at(const Doc *d, Pos p, Pos *a, Pos *b)
{
  const Para *q;
  u32 i, start = 0, ri = 0;
  int link = 0;
  if (p.para >= d->np) return 0;
  q = d->p[p.para];
  for (i = 0; i < q->nr; i++) {
    if (q->r[i].f.link && ((p.off >= start && p.off < q->r[i].end) || (p.off == q->r[i].end && p.off > start))) { link = q->r[i].f.link; ri = i; if (p.off < q->r[i].end) break; }
    start = q->r[i].end;
  }
  if (!link) return 0;
  {
    u32 lo = ri, hi = ri, s0 = 0;
    while (lo > 0 && q->r[lo - 1].f.link == link) lo--;
    while (hi + 1 < q->nr && q->r[hi + 1].f.link == link) hi++;
    for (i = 0; i < lo; i++) s0 = q->r[i].end;
    if (a) { a->para = p.para; a->off = s0; }
    if (b) { b->para = p.para; b->off = q->r[hi].end; }
  }
  return link;
}
/* the Hyperlink character style (Word's: blue, underlined), made if the document hasn't one */
static int hyperlink_style(Doc *d)
{
  int k = doc_style(d, "Hyperlink");
  Style s;
  if (k > 0 && d->styles[k].type == ST_CHAR) return k;
  memset(&s, 0, sizeof s);
  strcpy(s.id, "Hyperlink"); strcpy(s.name, "Hyperlink");
  s.type = ST_CHAR; s.builtin = 1; s.based = NO_STYLE;
  s.cmask = CM_COLOR | CM_UNDERLINE; s.ocf.flags = CF_UNDERLINE; s.ocf.color = 0x0563C1;
  if ((k = doc_add_style(d, &s)) < 0) return 0;
  doc_resolve_styles(d);
  return k;
}
/* see doc.h. One undo step: a and b's text (replaced by text if given) gets the link, or loses its
   links, and the Hyperlink character style; *end is after the linked text */
int doc_set_link(Doc *d, Pos a, Pos b, const char *url, const char *text, Pos *end)
{
  CharFmt v;
  int ok = 0, hs;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  memset(&v, 0, sizeof v);
  doc_begin(d, a);
  if (url && url[0]) {
    if (!pos_cmp(a, b) && text && text[0]) {
      Pos p = a;
      if (doc_insert(d, &p, text, (u32)strlen(text), NULL)) b = p;
    }
    if (pos_cmp(a, b) && (v.link = (u16)doc_link(d, url))) {
      ok = doc_format(d, a, b, CM_LINK, &v);
      if (ok && (hs = hyperlink_style(d)) > 0) doc_char_style(d, a, b, hs);
    }
  } else if (pos_cmp(a, b)) {
    /* links off; the Hyperlink style's blue underline goes with them (other character styles stay) */
    u32 pi;
    hs = doc_style(d, "Hyperlink");
    ok = doc_format(d, a, b, CM_LINK, &v);
    if (ok && hs > 0)
      for (pi = a.para; pi <= b.para; pi++) {
        Para *q = d->p[pi];
        u32 i, start = 0;
        for (i = 0; i < q->nr; i++) {
          Pos ra = { 0, 0 }, rb = { 0, 0 };
          ra.para = rb.para = pi; ra.off = start; rb.off = q->r[i].end;
          if (ra.off < (pi == a.para ? a.off : 0)) ra.off = pi == a.para ? a.off : 0;
          if (rb.off > (pi == b.para ? b.off : q->len)) rb.off = pi == b.para ? b.off : q->len;
          if (q->r[i].f.cstyle == hs && ra.off < rb.off && doc_char_style(d, ra, rb, 0)) { q = d->p[pi]; i = (u32)-1; start = 0; continue; }   /* (runs split and merged: from the start again) */
          start = q->r[i].end;
        }
      }
  }
  doc_end(d, b);
  if (end) *end = b;
  return ok;
}

/* ---- bookmarks. Like links, a run's CharFmt.bmark numbers a name in Doc.bmarks (1-based), and
   the table only grows, so undo never leaves a run naming nothing */
int doc_bmark(Doc *d, const char *name)
{
  int i;
  char *u, **nb;
  if (!name || !name[0]) return 0;
  for (i = 0; i < d->nbmarks; i++) if (!strcmp(d->bmarks[i], name)) return i + 1;
  if (d->nbmarks >= 0xFFFE || !(u = str_ndup(name, (u32)strlen(name)))) return 0;
  if (!(nb = (char **)realloc(d->bmarks, (size_t)(d->nbmarks + 1) * sizeof(char *)))) { free(u); return 0; }
  d->bmarks = nb;
  d->bmarks[d->nbmarks++] = u;
  return d->nbmarks;
}
/* see doc.h */
const char *doc_bmark_name(const Doc *d, int k)
{
  return k > 0 && k <= d->nbmarks ? d->bmarks[k - 1] : "";
}
/* the stretch of bookmark k that starts in run ri of paragraph pi: on to the end of its last run,
   over into following paragraphs while they open with it and the one before ended with it */
static void bmark_stretch(const Doc *d, int k, u32 pi, u32 ri, Pos *a, Pos *b)
{
  const Para *q = d->p[pi];
  u32 lo = ri, hi = ri;
  while (lo > 0 && q->r[lo - 1].f.bmark == k) lo--;
  if (a) { a->para = pi; a->off = lo ? q->r[lo - 1].end : 0; }
  for (;;) {
    while (hi + 1 < q->nr && q->r[hi + 1].f.bmark == k) hi++;
    if (hi + 1 < q->nr || pi + 1 >= d->np || !d->p[pi + 1]->len || d->p[pi + 1]->r[0].f.bmark != k) break;
    q = d->p[++pi]; hi = 0;
  }
  if (b) { b->para = pi; b->off = q->r[hi].end; }
}
/* see doc.h */
int doc_bmark_find(const Doc *d, const char *name, Pos *a, Pos *b)
{
  int k;
  u32 pi, i;
  for (k = 1; k <= d->nbmarks && strcmp(d->bmarks[k - 1], name); k++) ;
  if (k > d->nbmarks) return 0;
  for (pi = 0; pi < d->np; pi++)
    for (i = 0; i < d->p[pi]->nr; i++)
      if (d->p[pi]->r[i].f.bmark == k && d->p[pi]->len) { bmark_stretch(d, k, pi, i, a, b); return 1; }
  return 0;
}
/* see doc.h */
int doc_bmark_at(const Doc *d, Pos p, Pos *a, Pos *b)
{
  const Para *q;
  u32 i, start = 0, ri = 0;
  int k = 0;
  if (p.para >= d->np) return 0;
  q = d->p[p.para];
  for (i = 0; i < q->nr; start = q->r[i].end, i++)
    if (q->r[i].f.bmark && ((p.off >= start && p.off < q->r[i].end) || (p.off == q->r[i].end && p.off > start))) { k = q->r[i].f.bmark; ri = i; if (p.off < q->r[i].end) break; }
  if (!k) return 0;
  bmark_stretch(d, k, p.para, ri, a, b);
  return k;
}
/* see doc.h */
int doc_remove_bmark(Doc *d, const char *name)
{
  Pos a, b;
  CharFmt v;
  int n = 0;
  memset(&v, 0, sizeof v);
  a.para = 0; a.off = 0;
  doc_begin(d, a);
  while (doc_bmark_find(d, name, &a, &b) && n < 10000) { if (!doc_format(d, a, b, CM_BMARK, &v)) break; n++; }
  doc_end(d, a);
  return n > 0;
}
/* see doc.h */
int doc_set_bmark(Doc *d, Pos a, Pos b, const char *name)
{
  CharFmt v;
  int ok;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  if (!pos_cmp(a, b)) return 0;
  memset(&v, 0, sizeof v);
  doc_begin(d, a);
  if (name && name[0]) {
    doc_remove_bmark(d, name);
    ok = (v.bmark = (u16)doc_bmark(d, name)) && doc_format(d, a, b, CM_BMARK, &v);
  } else ok = doc_format(d, a, b, CM_BMARK, &v);
  doc_end(d, b);
  return ok;
}
