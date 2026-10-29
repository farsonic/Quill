/* review.c - see review.h (WP5: tracked formatting changes, kept table and section property changes,
   moves, how changes and comments are shown, balloons, threads of comments, Track Changes Options).
   Written for Quill from Word's documented behaviour (ECMA-376 Part 1, 17.13 Annotations). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "review.h"
#include "layout.h"

RvOpts rv_opts = {
  RVO_UNDERLINE, RVO_STRIKE, RVO_NONE,
  COLOR_AUTO, COLOR_AUTO, COLOR_AUTO,
  RVL_OUTSIDE, 0x808080UL,
  COLOR_AUTO,
  RVO_DSTRIKE, RVO_DUNDERLINE, 0x008000UL
};
static u32 opts_sig;                    /* bumped when an option that changes the layout does */

/* ------------------------------------------------------------------ */
/* tracked formatting changes */
int rv_add_fmt(Doc *d, int rid, int para, const CharFmt *ocf, const ParaFmt *opf, const char *raw, u32 rawlen)
{
  FmtRev *n, *f;
  if (d->nfrevs >= 65535 || !(n = (FmtRev *)realloc(d->frevs, (size_t)(d->nfrevs + 1) * sizeof(FmtRev)))) return 0;
  d->frevs = n;
  f = &d->frevs[d->nfrevs];
  memset(f, 0, sizeof *f);
  f->rid = (u16)(rid < 0 ? 0 : rid);
  f->para = (u8)(para != 0);
  if (ocf) f->ocf = *ocf;
  if (opf) f->opf = *opf;
  f->ocf.frev = 0; f->opf.prev = 0;
  if (raw && rawlen && (f->raw = (char *)malloc(rawlen + 1))) { memcpy(f->raw, raw, rawlen); f->raw[rawlen] = 0; f->rawlen = rawlen; }
  return ++d->nfrevs;
}
const FmtRev *rv_fmt(const Doc *d, u16 n) { return n && n <= d->nfrevs ? &d->frevs[n - 1] : NULL; }

unsigned rv_cf_diff(const CharFmt *a, const CharFmt *b)
{
  unsigned m = (unsigned)(a->flags ^ b->flags) & 0x7F;
  if (a->font != b->font) m |= CM_FONT;
  if (a->size != b->size) m |= CM_SIZE;
  if (a->color != b->color) m |= CM_COLOR;
  if (a->hl != b->hl) m |= CM_HL;
  if (a->cstyle != b->cstyle) m |= CM_CSTYLE;
  if ((a->fx ^ b->fx) & FX_SMALLCAPS) m |= CM_SMALLCAPS;
  if ((a->fx ^ b->fx) & FX_DSTRIKE) m |= CM_DSTRIKE;
  if ((a->fx ^ b->fx) & FX_BORDER) m |= CM_CBORDER;
  if (a->ul != b->ul && (a->flags & b->flags & CF_UNDERLINE)) m |= CM_UL;
  if (a->spacing != b->spacing) m |= CM_SPACING;
  if (a->scale != b->scale) m |= CM_SCALE;
  if (a->position != b->position) m |= CM_POSITION;
  return m;
}
unsigned rv_pf_diff(const ParaFmt *a, const ParaFmt *b)
{
  static const unsigned groups[] = { PM_STYLE, PM_ALIGN, PM_INDENT, PM_SPACING, PM_LINE, PM_TABS, PM_KEEP, PM_LIST, PM_BREAK,
                                     PM_DROP, PM_BORDER, PM_SHADE, PM_PAGINATION, PM_CONTEXT };
  unsigned m = 0, i;
  for (i = 0; i < sizeof groups / sizeof groups[0]; i++) {
    ParaFmt t = *a;
    pf_apply(&t, groups[i], b);
    if (memcmp(&t, a, sizeof t)) m |= groups[i];
  }
  return m;
}
/* make a run boundary at off in p; 0 if out of memory */
static int run_split(Para *p, u32 off)
{
  u32 i, start = 0;
  for (i = 0; i < p->nr; i++) {
    if (off > start && off < p->r[i].end) {
      if (p->nr >= p->rcap) {
        Run *nr = (Run *)realloc(p->r, (p->rcap * 2 + 2) * sizeof(Run));
        if (!nr) return 0;
        p->r = nr; p->rcap = p->rcap * 2 + 2;
      }
      memmove(p->r + i + 1, p->r + i, (p->nr - i) * sizeof(Run));
      p->r[i].end = off;
      p->nr++;
      return 1;
    }
    if (off <= start) return 1;
    start = p->r[i].end;
  }
  return 1;
}
/* runs a..b of p get frev n */
static void set_frev(Para *p, u32 a, u32 b, u16 n)
{
  u32 i, start = 0;
  if (!run_split(p, a) || !run_split(p, b)) return;
  for (i = 0; i < p->nr; i++) {
    if (start >= a && p->r[i].end <= b && p->r[i].end > start) p->r[i].f.frev = n;
    start = p->r[i].end;
  }
  para_normalise(p);
}
void rv_track_para(Doc *d, const Para *old, Para *cur)
{
  int rid = (int)d->rvtrack - 1;
  u32 io = 0, ic = 0, pos = 0, so = 0, sc = 0;
  u32 seg[64][2]; u16 segn[64]; int ns = 0, k;
  if (!d->rvtrack || (d->rvsettings & RVS_NOFMT) || !old || !cur) return;
  if (old->len != cur->len || memcmp(old->t, cur->t, old->len)) return;     /* the text changed: not a formatting change */
  /* the runs: where both have a run, compare their looks */
  while (pos < cur->len && io < old->nr && ic < cur->nr) {
    const CharFmt *fo = &old->r[io].f, *fc = &cur->r[ic].f;
    u32 eo = old->r[io].end, ec = cur->r[ic].end, e = eo < ec ? eo : ec;
    if (e > pos && !fo->frev && !fc->frev && fc->rev != REV_INS && fo->rev == fc->rev && rv_cf_diff(fo, fc)) {
      /* the same old look as the segment before: one change */
      if (ns && seg[ns - 1][1] == pos && !rv_cf_diff(&d->frevs[segn[ns - 1] - 1].ocf, fo) && d->frevs[segn[ns - 1] - 1].ocf.cstyle == fo->cstyle) seg[ns - 1][1] = e;
      else if (ns < 64) {
        u16 n = (u16)rv_add_fmt(d, rid, 0, fo, NULL, NULL, 0);
        if (n) { seg[ns][0] = pos; seg[ns][1] = e; segn[ns] = n; ns++; }
      }
    }
    pos = e;
    if (eo == e) { so = eo; io++; }
    if (ec == e) { sc = ec; ic++; }
  }
  (void)so; (void)sc;
  for (k = 0; k < ns; k++) set_frev(cur, seg[k][0], seg[k][1], segn[k]);
  /* an empty paragraph's one run (its mark's look) */
  if (!cur->len && !old->r[0].f.frev && !cur->r[0].f.frev && rv_cf_diff(&old->r[0].f, &cur->r[0].f))
    cur->r[0].f.frev = (u16)rv_add_fmt(d, rid, 0, &old->r[0].f, NULL, NULL, 0);
  /* the paragraph's own formatting */
  if (!old->pf.prev && !cur->pf.prev && rv_pf_diff(&old->pf, &cur->pf))
    cur->pf.prev = (u16)rv_add_fmt(d, rid, 1, NULL, &old->pf, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* property changes kept verbatim */
int rv_keep_change(Doc *d, int kind, int table, int row, int col, int sect, const char *xml, u32 len, int rid)
{
  KeptChange *n, *k;
  if (!xml || !len || !(n = (KeptChange *)realloc(d->kchg, (size_t)(d->nkchg + 1) * sizeof(KeptChange)))) return 0;
  d->kchg = n;
  k = &d->kchg[d->nkchg];
  memset(k, 0, sizeof *k);
  if (!(k->xml = (char *)malloc(len + 1))) return 0;
  memcpy(k->xml, xml, len); k->xml[len] = 0; k->len = len;
  k->kind = (u8)kind; k->table = (u16)table; k->row = (u16)row; k->col = (u8)col; k->sect = (u16)sect; k->rid = (u16)(rid < 0 ? 0 : rid);
  return ++d->nkchg;
}
static int kept_is(const KeptChange *k, int kind, int table, int row, int col, int sect)
{
  if (k->kind != kind) return 0;
  if (kind == RK_SECT) return k->sect == sect;
  if (k->table != table) return 0;
  if (kind == RK_TBL || kind == RK_GRID) return 1;
  if (k->row != row) return 0;
  return kind == RK_TR || kind == RK_TRREV || k->col == col;
}
void rv_write_kept(Buf *o, const Doc *d, int kind, int table, int row, int col, int sect)
{
  int i;
  for (i = 0; i < d->nkchg; i++) if (kept_is(&d->kchg[i], kind, table, row, col, sect)) buf_add(o, d->kchg[i].xml, d->kchg[i].len);
}
int rv_has_kept(const Doc *d, int kind, int table, int row, int col, int sect)
{
  int i;
  for (i = 0; i < d->nkchg; i++) if (kept_is(&d->kchg[i], kind, table, row, col, sect)) return 1;
  return 0;
}
/* the kept changes of a table (sect < 0) or a section dropped (accepted) */
static int kept_accept(Doc *d, int table, int sect)
{
  int i, o = 0, any = 0;
  for (i = 0; i < d->nkchg; i++) {
    KeptChange *k = &d->kchg[i];
    if ((table && k->kind != RK_SECT && k->table == table) || (sect >= 0 && k->kind == RK_SECT && k->sect == sect)) { free(k->xml); any = 1; continue; }
    d->kchg[o++] = *k;
  }
  d->nkchg = o;
  if (any) { d->dirty = 1; d->ver++; }
  return any;
}

/* ------------------------------------------------------------------ */
/* people and moves */
int rv_add_person(Doc *d, const char *author, const char *raw, u32 rawlen)
{
  Person *n, *p;
  int i;
  for (i = 0; i < d->npeople; i++) if (!strcmp(d->people[i].author, author ? author : "")) return i;
  if (!(n = (Person *)realloc(d->people, (size_t)(d->npeople + 1) * sizeof(Person)))) return -1;
  d->people = n;
  p = &d->people[d->npeople];
  memset(p, 0, sizeof *p);
  strncpy(p->author, author ? author : "", sizeof p->author - 1);
  if (raw && rawlen && (p->raw = (char *)malloc(rawlen + 1))) { memcpy(p->raw, raw, rawlen); p->raw[rawlen] = 0; p->rawlen = rawlen; }
  return d->npeople++;
}
void rv_set_move(Para *p, u32 a, u32 b, int move)
{
  u32 i, start = 0;
  if (b > p->len) b = p->len;
  if (a >= b || !run_split(p, a) || !run_split(p, b)) return;
  for (i = 0; i < p->nr; i++) {
    if (start >= a && p->r[i].end <= b && p->r[i].f.rev) p->r[i].f.move = (u8)move;
    start = p->r[i].end;
  }
  para_normalise(p);
}
int rv_move_id(Doc *d, const char *name)
{
  int i;
  void *n;
  if (!name || !name[0]) return 0;
  for (i = 0; i < d->nrvmoves; i++) if (!strncmp(d->rvmoves[i], name, 39)) return i + 1;
  if (d->nrvmoves >= 255 || !(n = realloc(d->rvmoves, (size_t)(d->nrvmoves + 1) * 40))) return 0;
  d->rvmoves = (char (*)[40])n;
  strncpy(d->rvmoves[d->nrvmoves], name, 39); d->rvmoves[d->nrvmoves][39] = 0;
  return ++d->nrvmoves;
}
const char *rv_move_name(const Doc *d, int id) { return id > 0 && id <= d->nrvmoves ? d->rvmoves[id - 1] : ""; }

/* ------------------------------------------------------------------ */
/* how changes and comments are shown */
RevView *rv_view(Doc *d)
{
  if (!d->rview && (d->rview = (RevView *)calloc(1, sizeof(RevView)))) d->rview->sig = 1;
  return d->rview;
}
int rv_mode(const Doc *d) { return d->rview ? d->rview->mode : RVM_ALL; }
int rv_hidden_kinds(const Doc *d) { return d->rview ? d->rview->hide : 0; }
int rv_balloons(const Doc *d) { return d->rview ? d->rview->balloons : RVB_FORMAT; }
static void view_changed(Doc *d) { if (d->rview) d->rview->sig++; d->ver++; }
void rv_set_mode(Doc *d, int mode) { if (rv_view(d) && mode >= RVM_ALL && mode <= RVM_ORIGINAL) { d->rview->mode = (u8)mode; view_changed(d); } }
void rv_set_hide(Doc *d, int hide) { if (rv_view(d)) { d->rview->hide = (u8)(hide & 7); view_changed(d); } }
void rv_set_balloons(Doc *d, int b) { if (rv_view(d) && b >= RVB_FORMAT && b <= RVB_INLINE) { d->rview->balloons = (u8)b; view_changed(d); } }
int rv_person_shown(const Doc *d, const char *author)
{
  int i;
  if (!d->rview || !d->rview->npeople) return 1;
  for (i = 0; i < d->rview->npeople; i++) if (!strcmp(d->rview->people[i], author ? author : "")) return 0;
  return 1;
}
void rv_show_person(Doc *d, const char *author, int shown)
{
  RevView *v = rv_view(d);
  int i;
  if (!v) return;
  if (!author) { free(v->people); v->people = NULL; v->npeople = 0; view_changed(d); return; }
  for (i = 0; i < v->npeople; i++)
    if (!strcmp(v->people[i], author)) {
      if (shown) { memmove(v->people + i, v->people + i + 1, (size_t)(v->npeople - i - 1) * 64); v->npeople--; view_changed(d); }
      return;
    }
  if (!shown) {
    void *n = realloc(v->people, (size_t)(v->npeople + 1) * 64);
    if (!n) return;
    v->people = (char (*)[64])n;
    strncpy(v->people[v->npeople], author, 63); v->people[v->npeople][63] = 0;
    v->npeople++;
    view_changed(d);
  }
}
u32 rv_sig(const Doc *d) { return (d->rview ? d->rview->sig : 0) * 31 + opts_sig; }
static const char *rid_author(const Doc *d, int rid) { return rid >= 0 && rid < d->nrevs ? d->revs[rid].author : ""; }
/* a kind of markup (RVH_INSDEL, RVH_FORMAT, RVH_COMMENTS) by someone shown in All Markup */
static int kind_shown(const Doc *d, int kind, const char *author)
{
  return rv_mode(d) == RVM_ALL && !(rv_hidden_kinds(d) & kind) && rv_person_shown(d, author);
}
int rv_show_run(const Doc *d, CharFmt *f, u8 *mark)
{
  int mode = rv_mode(d);
  *mark = 0;
  if (f->rev) {
    int ins = f->rev == REV_INS, kind = f->move ? RVK_MOVE : ins ? RVK_INS : RVK_DEL;
    if (mode == RVM_ORIGINAL) { if (ins) return 1; f->rev = REV_NONE; f->move = 0; }
    else if (!kind_shown(d, RVH_INSDEL, rid_author(d, f->rid))) {
      if (!ins) return 1;
      f->rev = REV_NONE; f->move = 0;
      if (mode == RVM_SIMPLE) *mark = (u8)kind;
    } else {
      *mark = (u8)kind;
      if (!ins && (rv_balloons(d) == RVB_ALL || (rv_opts.delmark == RVO_HIDDEN && kind != RVK_MOVE))) return 1;
    }
  }
  if (f->frev && f->frev <= d->nfrevs) {
    const FmtRev *fr = &d->frevs[f->frev - 1];
    if (mode == RVM_ORIGINAL) { u16 n = f->frev; u8 rv = f->rev; u16 rid = f->rid; cf_apply(f, CM_LOOK | CM_CSTYLE, &fr->ocf); f->frev = n; f->rev = rv; f->rid = rid; }
    else if (!*mark && (mode == RVM_SIMPLE || kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid)))) *mark = RVK_FMT;
  }
  return 0;
}
int rv_show_para(const Doc *d, const ParaFmt *pf)
{
  const FmtRev *fr = rv_fmt(d, pf->prev);
  if (!fr) return 0;
  return rv_mode(d) == RVM_SIMPLE || kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid)) ? RVK_PARA : 0;
}
int rv_show_comments(const Doc *d)
{
  int m = rv_mode(d);
  return (m == RVM_ALL || m == RVM_SIMPLE) && !(rv_hidden_kinds(d) & RVH_COMMENTS);
}
int rv_shown_change(const Doc *d, const CharFmt *f)
{
  const FmtRev *fr = rv_fmt(d, f->frev);
  if (f->rev && kind_shown(d, RVH_INSDEL, rid_author(d, f->rid))) return 1;
  return fr && kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid));
}
static int comment_shown(const Doc *d, int k)
{
  return rv_show_comments(d) && k >= 0 && k < d->ncomments && rv_person_shown(d, d->comments[k].author);
}

/* ------------------------------------------------------------------ */
/* comments: which are in the text (cached on Doc.ver) */
static const Doc *live_doc; static u32 live_ver, live_serial; static int live_n; static u8 *live_map;
static const u8 *live(Doc *d)
{
  u32 i, k;
  if (live_doc == d && live_ver == d->ver && live_serial == d->serial && live_n == d->ncomments && live_map) return live_map;
  free(live_map);
  live_map = (u8 *)calloc((size_t)d->ncomments + 1, 1);
  live_doc = d; live_ver = d->ver; live_serial = d->serial; live_n = d->ncomments;
  if (!live_map || d->edit.kind) {
    /* (a story edited in the body's place: the body is put aside, so nothing is counted live) */
    if (live_map && d->edit.kind) for (i = 0; i < d->edit.np; i++) {
      Para *p = d->edit.p[i];
      for (k = 0; k + 4 <= p->len; k++) if (IS_CSTART(p->t + k)) { int n = note_index(p->t + k); if (n < d->ncomments) live_map[n] = 1; k += 3; }
    }
    return live_map;
  }
  for (i = 0; i < d->np; i++) {
    Para *p = d->p[i];
    for (k = 0; k + 4 <= p->len; k++) if (IS_CSTART(p->t + k)) { int n = note_index(p->t + k); if (n < d->ncomments) live_map[n] = 1; k += 3; }
  }
  return live_map;
}
int rv_comment_live(Doc *d, int k) { const u8 *m = live(d); return m && k >= 0 && k < d->ncomments && m[k]; }
int rv_thread_root(const Doc *d, int k)
{
  int guard = 0;
  while (k >= 0 && k < d->ncomments && d->comments[k].parent > 0 && d->comments[k].parent <= d->ncomments && guard++ < 64) k = d->comments[k].parent - 1;
  return k;
}
int rv_replies(const Doc *d, int k, int *out, int max)
{
  int j, n = 0;
  const u8 *m = live((Doc *)d);
  for (j = 0; j < d->ncomments; j++)
    if (j != k && rv_thread_root(d, j) == k && (!m || m[j])) { if (n < max) out[n] = j; n++; }
  return n;
}
int rv_reply(Doc *d, int k, const char *text, const char *author, const char *initials, const char *date)
{
  int root = rv_thread_root(d, k), n, st, hs, he;
  Pos s, e, a;
  char m[8];
  if (root < 0 || root >= d->ncomments) return -1;
  hs = doc_comment_mark(d, root, 0, &s);
  he = doc_comment_mark(d, root, 1, &e);
  if (!hs) return -1;
  if ((n = doc_add_comment(d, author, initials, date)) < 0) return -1;
  st = doc_style(d, "CommentText");
  if (!story_set_text(d, &d->comments[n].text, text, st < 0 ? 0 : st, AL_LEFT)) return -1;
  d->comments[n].parent = root + 1;
  d->comments[n].done = d->comments[root].done;
  doc_begin(d, s);
  /* the end mark first (after the thread's), then the start mark after the thread's start */
  a = he ? e : s;
  a.off += 4;
  cmark_utf8(m, n, 1);
  if (!doc_insert(d, &a, m, 4, NULL)) { doc_end(d, s); return -1; }
  a = s; a.off += 4;
  cmark_utf8(m, n, 0);
  doc_insert(d, &a, m, 4, NULL);
  doc_end(d, s);
  return n;
}
void rv_set_done(Doc *d, int k, int done)
{
  int root = rv_thread_root(d, k), j;
  if (root < 0 || root >= d->ncomments) return;
  for (j = 0; j < d->ncomments; j++) if (rv_thread_root(d, j) == root) d->comments[j].done = (u8)(done != 0);
  d->dirty = 1; d->ver++;
}
int rv_delete_thread(Doc *d, int k)
{
  int root = rv_thread_root(d, k), j, any = 0;
  Pos at = doc_start(d);
  if (root < 0 || root >= d->ncomments) return 0;
  doc_comment_mark(d, root, 0, &at);
  doc_begin(d, at);
  for (j = 0; j < d->ncomments; j++) if (j != root && rv_thread_root(d, j) == root) any |= doc_delete_comment(d, j);
  any |= doc_delete_comment(d, root);
  doc_end(d, at);
  return any;
}
int rv_delete_all_comments(Doc *d)
{
  int j, any = 0;
  Pos at = doc_start(d);
  doc_begin(d, at);
  for (j = 0; j < d->ncomments; j++) any |= doc_delete_comment(d, j);
  doc_end(d, at);
  return any;
}

/* ------------------------------------------------------------------ */
/* balloons */
int rv_page_items(struct Layout *L, u32 page, RvItem *out, int max)
{
  Doc *d = L->d;
  u32 a, b, i, j;
  int n = 0, mode = rv_mode(d), dels = mode == RVM_ALL && rv_balloons(d) == RVB_ALL, fmts = mode == RVM_ALL && rv_balloons(d) != RVB_INLINE;
  const PageSetup *pg = lay_page(L, page);
  lay_page_lines(L, page, &a, &b);
  for (i = a; i < b && n < max; i++) {
    Placed *pc = &L->placed[i];
    ParaLay *pl;
    Line *ln;
    i32 x0, y;
    if (pc->page != page || pc->para >= L->npl) continue;
    pl = &L->pl[pc->para];
    if (!pl->lines || pc->line >= pl->nlines) continue;
    ln = &pl->lines[pc->line];
    x0 = pg->left + pc->x + ln->left;
    y = pc->y + ln->height;
    if (fmts && pc->line == 0 && pl->p->pf.prev && n < max) {
      const FmtRev *fr = rv_fmt(d, pl->p->pf.prev);
      if (fr && kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid))) {
        RvItem *it = &out[n++];
        memset(it, 0, sizeof *it);
        it->kind = RVI_PARA; it->k = fr->rid; it->frev = pl->p->pf.prev;
        it->a.para = it->b.para = pc->para; it->a.off = 0; it->b.off = pl->p->len;
        it->x = x0; it->y = y;
      }
    }
    for (j = ln->box0; j < ln->box0 + ln->nbox && n < max; j++) {
      Box *bx = &pl->boxes[j];
      RvItem *it, *last = n ? &out[n - 1] : NULL;
      if (bx->kind == BX_MARK) {
        int k;
        if (!IS_CSTART(pl->p->t + bx->off)) continue;
        k = note_index(pl->p->t + bx->off);
        if (k >= d->ncomments || !comment_shown(d, k)) continue;
        /* a reply goes in its thread's balloon (unless the comment it answers is gone) */
        if (d->comments[k].parent && rv_comment_live(d, rv_thread_root(d, k))) continue;
        it = &out[n++];
        memset(it, 0, sizeof *it);
        it->kind = RVI_COMMENT; it->k = k;
        it->a.para = it->b.para = pc->para; it->a.off = it->b.off = bx->off;
        it->x = x0 + bx->x; it->y = y;
        continue;
      }
      if (dels && bx->kind == BX_REVHIDE && bx->f.rev == REV_DEL && kind_shown(d, RVH_INSDEL, rid_author(d, bx->f.rid))) {
        if (last && last->kind == RVI_DELETE && last->k == bx->f.rid && last->b.para == pc->para && last->b.off == bx->off) { last->b.off = bx->off + bx->len; continue; }
        it = &out[n++];
        memset(it, 0, sizeof *it);
        it->kind = RVI_DELETE; it->k = bx->f.rid;
        it->a.para = it->b.para = pc->para; it->a.off = bx->off; it->b.off = bx->off + bx->len;
        it->x = x0 + bx->x; it->y = y;
        continue;
      }
      if (fmts && bx->f.frev && bx->kind != BX_REVHIDE) {
        const FmtRev *fr = rv_fmt(d, bx->f.frev);
        if (!fr || !kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid))) continue;
        if (last && last->kind == RVI_FORMAT && last->frev == bx->f.frev && last->b.para == pc->para && last->b.off == bx->off) { last->b.off = bx->off + bx->len; continue; }
        it = &out[n++];
        memset(it, 0, sizeof *it);
        it->kind = RVI_FORMAT; it->k = fr->rid; it->frev = bx->f.frev;
        it->a.para = it->b.para = pc->para; it->a.off = bx->off; it->b.off = bx->off + bx->len;
        it->x = x0 + bx->x; it->y = y;
      }
    }
  }
  return n;
}
static const char *no_tr(const char *s) { return s; }
/* text of p from a to b (UTF-8), references and marks left out, cut to fit cap */
static void para_text(const Para *p, u32 a, u32 b, char *out, size_t cap)
{
  size_t o = 0;
  u32 i;
  if (b > p->len) b = p->len;
  for (i = a; i < b && o + 5 < cap; i++) {
    u8 c = (u8)p->t[i];
    if (c == 0xF3 || c == 0xF4) { i += 3; continue; }              /* pictures, notes, comment marks */
    if (c == 0xEE && i + 2 < b && IS_FIELD(p->t + i)) { i += 2; continue; }
    out[o++] = (char)(c == '\t' || c == '\v' ? ' ' : c);
  }
  if (i < b && o + 4 < cap) { out[o++] = '.'; out[o++] = '.'; out[o++] = '.'; }
  out[o] = 0;
}
static void add_name(char *out, size_t cap, const char *s)
{
  size_t l = strlen(out);
  if (l + strlen(s) + 3 >= cap) return;
  if (l && out[l - 1] != ' ') strcat(out, ", ");
  strcat(out, s);
}
void rv_fmt_names(const Doc *d, u16 frev, const CharFmt *now, const ParaFmt *pnow, char *out, size_t cap, RvTr tr)
{
  const FmtRev *fr = rv_fmt(d, frev);
  char b[120];
  if (!tr) tr = no_tr;
  out[0] = 0;
  if (!fr || !cap) return;
  if (!fr->para && now) {
    unsigned m = rv_cf_diff(&fr->ocf, now);
    if (m & CM_BOLD) add_name(out, cap, tr(now->flags & CF_BOLD ? "Bold" : "Not Bold"));
    if (m & CM_ITALIC) add_name(out, cap, tr(now->flags & CF_ITALIC ? "Italic" : "Not Italic"));
    if (m & (CM_UNDERLINE | CM_UL)) add_name(out, cap, tr(now->flags & CF_UNDERLINE ? "Underline" : "No underline"));
    if (m & CM_STRIKE) add_name(out, cap, tr(now->flags & CF_STRIKE ? "Strikethrough" : "Not Strikethrough"));
    if (m & (CM_SUPER | CM_SUB)) add_name(out, cap, tr(now->flags & CF_SUPER ? "Superscript" : now->flags & CF_SUB ? "Subscript" : "Not Superscript/ Subscript"));
    if (m & CM_CAPS) add_name(out, cap, tr(now->flags & CF_CAPS ? "All caps" : "Not All caps"));
    if (m & CM_SMALLCAPS) add_name(out, cap, tr(now->fx & FX_SMALLCAPS ? "Small caps" : "Not Small caps"));
    if (m & CM_DSTRIKE) add_name(out, cap, tr("Double strikethrough"));
    if (m & CM_FONT) { snprintf(b, sizeof b, "%s: %s", tr("Font"), now->font < d->nfonts ? d->fonts[now->font] : "?"); add_name(out, cap, b); }
    if (m & CM_SIZE) { snprintf(b, sizeof b, "%s: %u%s pt", tr("Font size"), (unsigned)(now->size / 2), now->size & 1 ? ".5" : ""); add_name(out, cap, b); }
    if (m & CM_COLOR) {
      if (now->color == COLOR_AUTO) snprintf(b, sizeof b, "%s: %s", tr("Font color"), tr("Auto"));
      else snprintf(b, sizeof b, "%s: #%06lX", tr("Font color"), (unsigned long)(now->color & 0xFFFFFF));
      add_name(out, cap, b);
    }
    if (m & CM_HL) add_name(out, cap, tr(now->hl ? "Highlight" : "No highlight"));
    if (m & (CM_SPACING | CM_SCALE)) add_name(out, cap, tr("Character spacing"));
    if (m & CM_POSITION) add_name(out, cap, tr(now->position > 0 ? "Raised" : now->position < 0 ? "Lowered" : "Not Raised by / Lowered by"));
    if (m & CM_CBORDER) add_name(out, cap, tr("Border"));
    if (m & CM_CSTYLE) {
      snprintf(b, sizeof b, "%s: %s", tr("Style"), now->cstyle && now->cstyle < d->nstyles ? d->styles[now->cstyle].name : tr("Default Paragraph Font"));
      add_name(out, cap, b);
    }
  } else if (fr->para && pnow) {
    unsigned m = rv_pf_diff(&fr->opf, pnow);
    static const char *const al[4] = { "Left", "Centered", "Right", "Justified" };
    if (m & PM_STYLE) { snprintf(b, sizeof b, "%s: %s", tr("Style"), pnow->style < d->nstyles ? d->styles[pnow->style].name : "?"); add_name(out, cap, b); }
    if (m & PM_ALIGN) add_name(out, cap, tr(al[pnow->align & 3]));
    if (m & PM_INDENT) add_name(out, cap, tr("Indent"));
    if (m & PM_SPACING) add_name(out, cap, tr("Space Before/After"));
    if (m & PM_LINE) add_name(out, cap, tr("Line spacing"));
    if (m & PM_TABS) add_name(out, cap, tr("Tab stops"));
    if (m & PM_KEEP) add_name(out, cap, tr("Keep with next"));
    if (m & PM_LIST) add_name(out, cap, tr(pnow->list ? "Numbering" : "No bullets or numbering"));
    if (m & PM_BREAK) add_name(out, cap, tr("Page break before"));
    if (m & PM_BORDER) add_name(out, cap, tr("Border"));
    if (m & PM_SHADE) add_name(out, cap, tr("Shading"));
    if (m & PM_PAGINATION) add_name(out, cap, tr("Keep lines together"));
    if (m & PM_CONTEXT) add_name(out, cap, tr("Don't add space between paragraphs of the same style"));
    if (m & PM_DROP) add_name(out, cap, tr("Drop cap"));
  }
  if (!out[0]) add_name(out, cap, tr(fr->para ? "Paragraph" : "Font"));
}
/* the text of a comment and its replies' count is the caller's; a balloon's one line */
void rv_item_text(const Doc *d, const RvItem *it, char *out, size_t cap, RvTr tr)
{
  char t[300], names[300];
  if (!tr) tr = no_tr;
  out[0] = 0;
  if (!cap) return;
  if (it->kind == RVI_COMMENT) {
    char *s = it->k < d->ncomments ? story_text(&d->comments[it->k].text) : NULL;
    snprintf(out, cap, "%s", s ? s : "");
    free(s);
  } else if (it->kind == RVI_DELETE) {
    if (it->a.para < d->np) para_text(d->p[it->a.para], it->a.off, it->b.off, t, sizeof t); else t[0] = 0;
    snprintf(out, cap, "%s %s", tr("Deleted:"), t);
  } else {
    const Para *p = it->a.para < d->np ? d->p[it->a.para] : NULL;
    const CharFmt *now = NULL;
    if (p) { u32 r; for (r = 0; r < p->nr && p->r[r].end <= it->a.off; r++) ; now = &p->r[r < p->nr ? r : p->nr - 1].f; }
    rv_fmt_names(d, it->frev, now, p ? &p->pf : NULL, names, sizeof names, tr);
    snprintf(out, cap, "%s %s", tr("Formatted:"), names);
  }
}
static u32 margin_ver, margin_serial, margin_sig; static int margin_val = -1; static const Doc *margin_doc;
int rv_any_margin(Doc *d)
{
  u32 i, r;
  int any = 0, j;
  if (margin_doc == d && margin_ver == d->ver && margin_serial == d->serial && margin_sig == rv_sig(d) && margin_val >= 0) return margin_val;
  if (rv_show_comments(d)) for (j = 0; j < d->ncomments && !any; j++) if (rv_comment_live(d, j) && rv_person_shown(d, d->comments[j].author)) any = 1;
  if (!any && rv_mode(d) == RVM_ALL && rv_balloons(d) != RVB_INLINE && !d->edit.kind)
    for (i = 0; i < d->np && !any; i++) {
      const Para *p = d->p[i];
      if (p->pf.prev && !(rv_hidden_kinds(d) & RVH_FORMAT)) any = 1;
      for (r = 0; r < p->nr && !any; r++)
        if ((p->r[r].f.frev && !(rv_hidden_kinds(d) & RVH_FORMAT)) || (p->r[r].f.rev == REV_DEL && rv_balloons(d) == RVB_ALL && !(rv_hidden_kinds(d) & RVH_INSDEL))) any = 1;
    }
  margin_doc = d; margin_ver = d->ver; margin_serial = d->serial; margin_sig = rv_sig(d); margin_val = any;
  return any;
}

/* ------------------------------------------------------------------ */
/* changes as a whole */
int rv_is_change(const CharFmt *f) { return f->rev || f->frev; }
static int same_change(const CharFmt *a, const CharFmt *b) { return a->rev == b->rev && a->rid == b->rid && a->frev == b->frev && a->move == b->move; }
/* paragraph pi's changes in order: the i'th one's extent (the paragraph's own formatting first, as
   the whole paragraph; then runs of one change merged); 0 past the last */
static int para_change(const Para *q, int i, u32 *s, u32 *e)
{
  u32 r = 0, rs = 0;
  if (q->pf.prev) { if (i == 0) { *s = 0; *e = q->len; return 1; } i--; }
  while (r < q->nr) {
    u32 st = rs, en;
    if (!rv_is_change(&q->r[r].f) || q->r[r].end <= rs) { rs = q->r[r].end; r++; continue; }
    en = q->r[r].end;
    while (r + 1 < q->nr && same_change(&q->r[r].f, &q->r[r + 1].f)) { r++; en = q->r[r].end; }
    rs = q->r[r].end; r++;
    if (i-- == 0) { *s = st; *e = en; return 1; }
  }
  return 0;
}
int rv_next_change(Doc *d, Pos p, int dir, Pos *a, Pos *b)
{
  u32 pi, n;
  p = pos_clamp(d, p);
  pi = p.para;
  for (n = 0; n <= d->np; n++) {
    Para *q = d->p[pi];
    u32 s, e;
    int i, best = -1;
    for (i = 0; para_change(q, i, &s, &e); i++) {
      if (dir > 0) { if (n > 0 || s >= p.off) { if (!(n == 0 && s == p.off && e == p.off)) { best = i; break; } } }
      else if (n > 0 || e <= p.off) best = i;      /* the last one before p */
    }
    if (best >= 0 && para_change(q, best, &s, &e)) {
      a->para = b->para = pi; a->off = s; b->off = e;
      return 1;
    }
    if (dir > 0) { if (++pi >= d->np) pi = 0; }
    else pi = pi ? pi - 1 : d->np - 1;
  }
  return 0;
}
/* one run of rv_resolve (s..e of paragraph pi, formatted f): 1 if it held a change resolved */
static int resolve_run(Doc *d, u32 pi, u32 s, u32 e, CharFmt f, int accept, int shownonly)
{
  Pos a, b;
  int done = 0;
  const FmtRev *fr = rv_fmt(d, f.frev);
  a.para = b.para = pi; a.off = s; b.off = e;
  if (fr && (!shownonly || kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid)))) {
    CharFmt v = f;
    if (!accept) cf_apply(&v, CM_LOOK | CM_CSTYLE, &fr->ocf);
    v.frev = 0;
    doc_format(d, a, b, (accept ? 0 : CM_LOOK | CM_CSTYLE) | CM_FREV, &v);
    done = 1;
  }
  if (f.rev && (!shownonly || kind_shown(d, RVH_INSDEL, rid_author(d, f.rid)))) {
    if ((f.rev == REV_INS) == (accept != 0)) {
      CharFmt v = f;
      v.rev = REV_NONE; v.rid = 0; v.move = 0;
      doc_format(d, a, b, CM_REV, &v);
    } else doc_delete(d, a, b);
    done = 1;
  }
  return done;
}
int rv_resolve(Doc *d, Pos a, Pos b, int accept, int shownonly)
{
  int any = 0;
  u32 pi;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  doc_begin(d, a);
  /* the runs, last first: deleting text only moves what is after it */
  for (pi = b.para + 1; pi-- > a.para; ) {
    u32 s = pi == a.para ? a.off : 0, cur = pi == b.para ? b.off : d->p[pi]->len;
    while (cur > s) {
      Para *p = d->p[pi];
      u32 r = 0, rs, x0;
      while (r + 1 < p->nr && p->r[r].end < cur) r++;
      rs = r ? p->r[r - 1].end : 0;
      x0 = rs > s ? rs : s;
      any |= resolve_run(d, pi, x0, cur, p->r[r].f, accept, shownonly);
      cur = x0;
    }
  }
  /* the paragraphs' own formatting, and their tables' and sections' kept changes */
  for (pi = a.para; pi <= b.para && pi < d->np; pi++) {
    Para *p = d->p[pi];
    const FmtRev *fr = rv_fmt(d, p->pf.prev);
    if (fr && (!shownonly || kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid)))) {
      ParaFmt v = p->pf;
      if (!accept) pf_apply(&v, PM_ALL | PM_DROP, &fr->opf);
      v.prev = 0;
      doc_para_format(d, pi, pi, (accept ? 0 : PM_ALL | PM_DROP) | PM_PREV, &v);
      any = 1;
    }
    if (accept && d->nkchg && !shownonly) {
      if (p->pf.cell.table) any |= kept_accept(d, p->pf.cell.table, -1);
      if (p->pf.outer.table) any |= kept_accept(d, p->pf.outer.table, -1);
      if (p->pf.sect) any |= kept_accept(d, 0, p->pf.sect);
      if (pi + 1 == d->np) any |= kept_accept(d, 0, 0);
    }
  }
  doc_end(d, a);
  return any;
}
int rv_changes(Doc *d, RvChange *out, int max)
{
  u32 pi;
  int n = 0, k;
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    u32 s, e, o = 0;
    int i = 0, have = para_change(q, 0, &s, &e);
    /* the paragraph's changes and the comments starting in it, in the order they come */
    for (;;) {
      while (o + 4 <= q->len && !IS_CSTART(q->t + o)) o++;
      if (o + 4 <= q->len && (!have || o < s)) {
        int c = note_index(q->t + o);
        if (c < d->ncomments) {
          if (n < max) { RvChange *x = &out[n]; memset(x, 0, sizeof *x); x->kind = RVC_COMMENT; x->k = c; x->a.para = x->b.para = pi; x->a.off = o; x->b.off = o + 4; }
          n++;
        }
        o += 4;
        continue;
      }
      if (!have) break;
      if (n < max) {
        RvChange *x = &out[n];
        u32 r;
        memset(x, 0, sizeof *x);
        x->a.para = x->b.para = pi; x->a.off = s; x->b.off = e;
        if (q->pf.prev && i == 0) { x->kind = RVC_PARA; x->frev = q->pf.prev; x->k = q->pf.prev <= d->nfrevs ? d->frevs[q->pf.prev - 1].rid : 0; }
        else {
          const CharFmt *f;
          for (r = 0; r < q->nr && q->r[r].end <= s; r++) ;
          f = &q->r[r < q->nr ? r : q->nr - 1].f;
          if (f->rev) { x->kind = (u8)(f->move ? (f->rev == REV_INS ? RVC_MOVETO : RVC_MOVEFROM) : f->rev == REV_INS ? RVC_INS : RVC_DEL); x->k = f->rid; x->frev = f->frev; }
          else { x->kind = RVC_FORMAT; x->frev = f->frev; x->k = f->frev && f->frev <= d->nfrevs ? d->frevs[f->frev - 1].rid : 0; }
        }
      }
      n++;
      have = para_change(q, ++i, &s, &e);
    }
  }
  /* the kept table and section changes, at their tables' first paragraphs */
  for (k = 0; k < d->nkchg; k++) {
    KeptChange *c = &d->kchg[k];
    if (n < max) {
      RvChange *x = &out[n];
      memset(x, 0, sizeof *x);
      x->kind = c->kind == RK_SECT ? RVC_SECT : RVC_TABLE; x->k = k;
      for (pi = 0; pi < d->np; pi++)
        if (c->kind == RK_SECT ? (c->sect ? d->p[pi]->pf.sect == c->sect : pi + 1 == d->np) : (d->p[pi]->pf.cell.table == c->table || d->p[pi]->pf.outer.table == c->table)) break;
      if (pi >= d->np) pi = d->np - 1;
      x->a.para = x->b.para = pi; x->a.off = 0; x->b.off = 0;
    }
    n++;
  }
  return n;
}
const char *rv_change_author(const Doc *d, const RvChange *c)
{
  if (c->kind == RVC_COMMENT) return c->k < d->ncomments ? d->comments[c->k].author : "";
  if (c->kind == RVC_TABLE || c->kind == RVC_SECT) return c->k < d->nkchg ? rid_author(d, d->kchg[c->k].rid) : "";
  return rid_author(d, c->k);
}
const char *rv_change_date(const Doc *d, const RvChange *c)
{
  int rid = c->k;
  if (c->kind == RVC_COMMENT) return c->k < d->ncomments ? d->comments[c->k].date : "";
  if (c->kind == RVC_TABLE || c->kind == RVC_SECT) rid = c->k < d->nkchg ? d->kchg[c->k].rid : -1;
  return rid >= 0 && rid < d->nrevs ? d->revs[rid].date : "";
}
void rv_change_text(const Doc *d, const RvChange *c, char *out, size_t cap, RvTr tr)
{
  char t[260], names[260];
  const Para *p = c->a.para < d->np ? d->p[c->a.para] : NULL;
  if (!tr) tr = no_tr;
  out[0] = 0;
  if (!p || !cap) return;
  switch (c->kind) {
  case RVC_INS: case RVC_DEL: case RVC_MOVEFROM: case RVC_MOVETO:
    para_text(p, c->a.off, c->b.off, t, sizeof t);
    snprintf(out, cap, "%s %s", tr(c->kind == RVC_INS ? "Inserted:" : c->kind == RVC_DEL ? "Deleted:" : c->kind == RVC_MOVETO ? "Moved (insertion):" : "Moved (deletion):"), t);
    break;
  case RVC_FORMAT: case RVC_PARA: {
    u32 r;
    for (r = 0; r < p->nr && p->r[r].end <= c->a.off; r++) ;
    rv_fmt_names(d, c->frev, &p->r[r < p->nr ? r : p->nr - 1].f, &p->pf, names, sizeof names, tr);
    snprintf(out, cap, "%s %s", tr("Formatted:"), names);
    break; }
  case RVC_COMMENT: {
    char *s = c->k < d->ncomments ? story_text(&d->comments[c->k].text) : NULL;
    snprintf(out, cap, "%s %s", tr(c->k < d->ncomments && d->comments[c->k].parent ? "Reply:" : "Comment:"), s ? s : "");
    free(s);
    break; }
  case RVC_TABLE: case RVC_SECT: {
    static const char *const kinds[RK_N] = { "Formatted Table", "Formatted Table", "Formatted Table", "Inserted or deleted row", "Formatted Table",
                                             "Inserted or deleted cell", "Formatted: Section" };
    snprintf(out, cap, "%s", tr(c->k < d->nkchg ? kinds[d->kchg[c->k].kind % RK_N] : "Formatted Table"));
    break; }
  }
}
int rv_reviewers(const Doc *d, const char **out, int max)
{
  int n = 0, i, j;
  for (i = 0; i < d->nrevs + d->ncomments; i++) {
    const char *a = i < d->nrevs ? d->revs[i].author : d->comments[i - d->nrevs].author;
    for (j = 0; j < n && j < max; j++) if (!strcmp(out[j], a)) break;
    if (j < n) continue;
    if (n < max) out[n] = a;
    n++;
  }
  return n;
}

/* ------------------------------------------------------------------ */
/* Track Changes Options and colours */
void rv_opts_default(RvOpts *o)
{
  memset(o, 0, sizeof *o);
  o->insmark = RVO_UNDERLINE; o->delmark = RVO_STRIKE; o->fmtmark = RVO_NONE;
  o->inscolor = o->delcolor = o->fmtcolor = o->commentcolor = COLOR_AUTO;
  o->lines = RVL_OUTSIDE; o->linecolor = 0x808080;
  o->movefrom = RVO_DSTRIKE; o->moveto = RVO_DUNDERLINE; o->movecolor = 0x008000;
  opts_sig++;
}
static const char *const optkeys[] = { "insmark", "delmark", "fmtmark", "inscolor", "delcolor", "fmtcolor", "lines", "linecolor", "commentcolor",
                                       "movefrom", "moveto", "movecolor" };
int rv_opts_load(RvOpts *o, const char *text)
{
  int n = 0;
  while (text && *text) {
    const char *e = strchr(text, '\n'), *eq = strchr(text, '=');
    size_t l = e ? (size_t)(e - text) : strlen(text);
    if (eq && eq < text + l) {
      size_t kl = (size_t)(eq - text), i;
      unsigned long v = strtoul(eq + 1, NULL, 0);
      for (i = 0; i < sizeof optkeys / sizeof optkeys[0]; i++)
        if (strlen(optkeys[i]) == kl && !strncmp(text, optkeys[i], kl)) {
          switch (i) {
          case 0: if (v < RVO_N) o->insmark = (u8)v; break;
          case 1: if (v < RVO_N) o->delmark = (u8)v; break;
          case 2: if (v < RVO_N) o->fmtmark = (u8)v; break;
          case 3: o->inscolor = (u32)v; break;
          case 4: o->delcolor = (u32)v; break;
          case 5: o->fmtcolor = (u32)v; break;
          case 6: if (v <= RVL_OUTSIDE) o->lines = (u8)v; break;
          case 7: o->linecolor = (u32)v; break;
          case 8: o->commentcolor = (u32)v; break;
          case 9: if (v < RVO_N) o->movefrom = (u8)v; break;
          case 10: if (v < RVO_N) o->moveto = (u8)v; break;
          case 11: o->movecolor = (u32)v; break;
          }
          n++;
        }
    }
    text = e ? e + 1 : text + l;
  }
  opts_sig++;
  return n;
}
void rv_opts_save(const RvOpts *o, Buf *out)
{
  buf_printf(out, "insmark=%u\ndelmark=%u\nfmtmark=%u\ninscolor=0x%08lX\ndelcolor=0x%08lX\nfmtcolor=0x%08lX\nlines=%u\nlinecolor=0x%06lX\ncommentcolor=0x%08lX\n",
             (unsigned)o->insmark, (unsigned)o->delmark, (unsigned)o->fmtmark, (unsigned long)o->inscolor, (unsigned long)o->delcolor,
             (unsigned long)o->fmtcolor, (unsigned)o->lines, (unsigned long)o->linecolor, (unsigned long)o->commentcolor);
  buf_printf(out, "movefrom=%u\nmoveto=%u\nmovecolor=0x%06lX\n", (unsigned)o->movefrom, (unsigned)o->moveto, (unsigned long)o->movecolor);
}
static const u32 palette[8] = { 0xC00000, 0x1F4FC8, 0x008040, 0x8A2BB0, 0xB85C00, 0x007A87, 0xC0007A, 0x5A6E00 };
/* an author's place among the reviewers (the order they first appear in) */
static int author_index(const Doc *d, const char *author)
{
  const char *r[64];
  int n = rv_reviewers(d, r, 64), i;
  for (i = 0; i < n && i < 64; i++) if (!strcmp(r[i], author)) return i;
  return 0;
}
u32 rv_author_color(const Doc *d, int rid) { return palette[author_index(d, rid_author(d, rid)) % 8]; }
u32 rv_comment_color(const Doc *d, const char *author)
{
  return rv_opts.commentcolor != COLOR_AUTO ? rv_opts.commentcolor : palette[author_index(d, author ? author : "") % 8];
}
void rv_ink(const Doc *d, const CharFmt *f, u32 *color, u8 *mark)
{
  *color = COLOR_AUTO; *mark = RVO_NONE;
  if (f->rev) {
    if (f->move) { *color = rv_opts.movecolor; *mark = f->rev == REV_INS ? rv_opts.moveto : rv_opts.movefrom; return; }
    if (f->rev == REV_INS) { *color = rv_opts.inscolor == COLOR_AUTO ? rv_author_color(d, f->rid) : rv_opts.inscolor; *mark = rv_opts.insmark; }
    else { *color = rv_opts.delcolor == COLOR_AUTO ? rv_author_color(d, f->rid) : rv_opts.delcolor; *mark = rv_opts.delmark; }
    return;
  }
  if (f->frev && rv_opts.fmtmark != RVO_NONE) {
    const FmtRev *fr = rv_fmt(d, f->frev);
    if (fr && kind_shown(d, RVH_FORMAT, rid_author(d, fr->rid))) {
      *color = rv_opts.fmtcolor == COLOR_AUTO ? rv_author_color(d, fr->rid) : rv_opts.fmtcolor;
      *mark = rv_opts.fmtmark;
    }
  }
}

/* ------------------------------------------------------------------ */
void rv_free(Doc *d)
{
  int i;
  for (i = 0; i < d->nfrevs; i++) free(d->frevs[i].raw);
  free(d->frevs); d->frevs = NULL; d->nfrevs = 0;
  if (d->rview) { free(d->rview->people); free(d->rview); d->rview = NULL; }
  for (i = 0; i < d->npeople; i++) free(d->people[i].raw);
  free(d->people); d->people = NULL; d->npeople = 0;
  free(d->rvmoves); d->rvmoves = NULL; d->nrvmoves = 0;
  for (i = 0; i < d->nkchg; i++) free(d->kchg[i].xml);
  free(d->kchg); d->kchg = NULL; d->nkchg = 0;
  if (live_doc == d) { live_doc = NULL; free(live_map); live_map = NULL; }
  if (margin_doc == d) margin_doc = NULL;
}
