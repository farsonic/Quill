/*
 * wp_outline_view.h - WP11's part of the page view, included by view.c (after wp_find_view.h) so it can
 * use the view's own data and helpers: Outline view (web layout with the outline on: paragraphs
 * indented by level, the outline symbols beside them, collapsed headings hiding what is under them,
 * levels shown, text formatting and first lines only) and the outline commands (promote, demote,
 * move up and down, expand, collapse) for any view. view.c calls in from a few places: ov_on
 * (shown_mode: the view mode as the outside sees it), ov_switch (the view mode set), ov_prepare (relayout),
 * ov_draw (draw_all), ov_caret (refresh), ov_mouse (handle_event), ov_command (do_command) and
 * ov_free (OM_DISPOSE). This file belongs to WP11.
 */
#include "wp_outline.h"
#include "../core/outline.h"

static void refresh(struct Data *d, Object *obj, int edited);
static void relayout(struct Data *d, Object *obj);
static void caret_hide(struct Data *d, Object *obj);
static u32 point_page(struct Data *d, Object *obj, LONG mx, LONG my, i32 *px, i32 *py);

typedef struct {
  int on;                       /* Outline view */
  int levels;                   /* headings down to this level shown (1-9), 0 everything */
  int flags;                    /* OL_PLAIN (Show Text Formatting off), OL_FIRST (Show First Line Only) */
  Para **coll; u32 ncoll, ccap; /* the collapsed headings */
  u8 *hid; u32 nhid;            /* each paragraph hidden (as of the last layout) */
  u32 lastpara;                 /* the caret's paragraph last time (which way it was going) */
  ULONG secs, micros; u32 clickpara;   /* the last click on a symbol (double-clicks) */
} OvState;

static OvState *ov(struct Data *d)
{
  if (!d->wp11) d->wp11 = calloc(1, sizeof(OvState));
  return (OvState *)d->wp11;
}
static void ov_free(struct Data *d)
{
  OvState *s = (OvState *)d->wp11;
  if (!s) return;
  free(s->coll); free(s->hid); free(s);
  d->wp11 = NULL;
}
static int ov_on(struct Data *d) { return d->wp11 && ((OvState *)d->wp11)->on; }
/* Outline view on or off (the view mode then being web layout or not) */
static void ov_switch(struct Data *d, int on)
{
  OvState *s = on ? ov(d) : (OvState *)d->wp11;
  if (s) s->on = on;
}
static int ov_collapsed(OvState *s, const Para *p)
{
  u32 k;
  for (k = 0; k < s->ncoll; k++) if (s->coll[k] == p) return 1;
  return 0;
}
static void ov_set_collapsed(OvState *s, Para *p, int on)
{
  u32 k;
  for (k = 0; k < s->ncoll; k++) if (s->coll[k] == p) break;
  if (!on) { if (k < s->ncoll) s->coll[k] = s->coll[--s->ncoll]; return; }
  if (k < s->ncoll) return;
  if (s->ncoll >= s->ccap) {
    u32 nc = s->ccap ? s->ccap * 2 : 16;
    Para **n = (Para **)realloc(s->coll, nc * sizeof *n);
    if (!n) return;
    s->coll = n; s->ccap = nc;
  }
  s->coll[s->ncoll++] = p;
}
/* before each layout: the layout told about Outline view, which paragraphs are hidden worked out
   (collapsed headings that are gone or no longer headings forgotten) */
static void ov_prepare(struct Data *d)
{
  OvState *s = (OvState *)d->wp11;
  Layout *L = d->lay;
  Doc *doc = d->doc;
  u32 k, i;
  if (!L) return;
  L->ohid = NULL; L->nohid = 0;
  L->outline = 0;
  if (!s || !s->on || !doc) return;
  for (k = 0; k < s->ncoll; ) {
    for (i = 0; i < doc->np && doc->p[i] != s->coll[k]; i++) ;
    if (i < doc->np && outline_level(doc, i)) k++; else s->coll[k] = s->coll[--s->ncoll];
  }
  if (s->nhid < doc->np || !s->hid) {
    u8 *n = (u8 *)realloc(s->hid, doc->np + 16);
    if (!n) return;
    s->hid = n;
  }
  s->nhid = doc->np;
  outline_hidden(doc, s->coll, s->ncoll, s->levels, s->hid);
  L->outline = (u8)(OL_ON | (s->flags & (OL_PLAIN | OL_FIRST)));
  L->ohid = s->hid; L->nohid = doc->np;
  L->oplain = doc->styles[0].cf;
}
static int ov_hidden(OvState *s, Doc *doc, u32 i) { return s && s->on && s->hid && s->nhid == doc->np && i < s->nhid && s->hid[i]; }
/* (refresh) the caret out of hidden paragraphs: on to the next shown one if it was moving down, else
   back to the end of the one above */
static void ov_caret(struct Data *d)
{
  OvState *s = (OvState *)d->wp11;
  Doc *doc = d->doc;
  u32 i;
  int sel;
  if (!s || !s->on || !doc) return;
  sel = has_sel(d);
  if (ov_hidden(s, doc, d->caret.para)) {
    int fwd = d->caret.para >= s->lastpara;
    for (i = d->caret.para; fwd && i < doc->np && ov_hidden(s, doc, i); i++) ;
    if (fwd && i < doc->np) { d->caret.para = i; d->caret.off = 0; }
    else {
      for (i = d->caret.para; i > 0 && ov_hidden(s, doc, i); i--) ;
      d->caret.para = i; d->caret.off = doc->p[i]->len;
    }
    if (!sel || ov_hidden(s, doc, d->anchor.para)) d->anchor = d->caret;
  }
  s->lastpara = d->caret.para;
}
/* the paragraphs a command works on: the selection's (one ending at a paragraph's start leaves that
   one out), or the caret's; with part, a collapsed heading's part in Outline view too */
static void ov_paras(struct Data *d, u32 *pa, u32 *pb, int part)
{
  OvState *s = (OvState *)d->wp11;
  Pos lo = sel_lo(d), hi = sel_hi(d);
  *pa = lo.para; *pb = hi.para;
  if (*pb > *pa && hi.off == 0) (*pb)--;
  /* a heading whose part is hidden (collapsed, or below the levels shown) takes it along */
  if (part && s && s->on && *pb < d->doc->np && outline_level(d->doc, *pb) &&
      (ov_collapsed(s, d->doc->p[*pb]) || ov_hidden(s, d->doc, *pb + 1))) *pb = outline_end(d->doc, *pb) - 1;
}
/* the heading a paragraph is under (itself if it is one), or -1 */
static long ov_heading(Doc *doc, u32 i)
{
  for (;;) {
    if (outline_level(doc, i)) return (long)i;
    if (!i) return -1;
    i--;
  }
}
/* the heading above heading h of a higher level, or -1 */
static long ov_parent(Doc *doc, u32 h)
{
  int l = outline_level(doc, h);
  while (h-- > 0) { int k = outline_level(doc, h); if (k && k < l) return (long)h; }
  return -1;
}
/* Collapse (expand 0) or Expand a level, as Word: collapsing folds the deepest open headings under
   heading h first, then h, then the heading above; expanding opens h, then the shallowest folded
   headings under it */
static void ov_fold(OvState *s, Doc *doc, u32 h, int expand)
{
  u32 e = outline_end(doc, h), i;
  int deep = 0, shallow = 10;
  if (expand) {
    if (ov_collapsed(s, doc->p[h])) { ov_set_collapsed(s, doc->p[h], 0); return; }
    for (i = h + 1; i < e; i++) if (outline_level(doc, i) && ov_collapsed(s, doc->p[i]) && outline_level(doc, i) < shallow) shallow = outline_level(doc, i);
    for (i = h + 1; i < e; i++) if (outline_level(doc, i) == shallow && ov_collapsed(s, doc->p[i])) ov_set_collapsed(s, doc->p[i], 0);
    return;
  }
  if (ov_collapsed(s, doc->p[h]) || !outline_has_kids(doc, h)) {
    long p = ov_parent(doc, h);
    if (p >= 0 && !ov_collapsed(s, doc->p[p])) ov_set_collapsed(s, doc->p[p], 1);
    return;
  }
  /* the deepest open headings with something under them (not inside a folded one) */
  for (i = h + 1; i < e; i++) {
    int l = outline_level(doc, i);
    if (!l) continue;
    if (ov_collapsed(s, doc->p[i])) { i = outline_end(doc, i) - 1; continue; }
    if (outline_has_kids(doc, i) && l > deep) deep = l;
  }
  if (!deep) { ov_set_collapsed(s, doc->p[h], 1); return; }
  for (i = h + 1; i < e; i++) {
    int l = outline_level(doc, i);
    if (!l) continue;
    if (ov_collapsed(s, doc->p[i])) { i = outline_end(doc, i) - 1; continue; }
    if (l == deep && outline_has_kids(doc, i)) ov_set_collapsed(s, doc->p[i], 1);
  }
}
/* where paragraph i went when paragraphs a..b moved to before dest */
static u32 ov_map(u32 i, u32 a, u32 b, u32 dest)
{
  u32 n = b - a + 1;
  if (i >= a && i <= b) return (dest < a ? dest : dest - n) + (i - a);
  if (dest < a && i >= dest && i < a) return i + n;
  if (dest > b && i > b && i < dest) return i - n;
  return i;
}
/* the view's WP11 commands (CMD_OV_); 1 if it was one */
static int ov_command(struct Data *d, Object *obj, struct MUIP_Quill_Command *m)
{
  OvState *s;
  Doc *doc = d->doc;
  u32 pa, pb;
  int done = 0;
  if (!IS_OV_CMD(m->cmd)) return 0;
  s = (OvState *)d->wp11;
  if (m->cmd == CMD_OV_INFO) {
    struct OvInfo *oi = (struct OvInfo *)m->arg;
    memset(oi, 0, sizeof *oi);
    if (s) { oi->on = s->on; oi->levels = s->levels; oi->plain = (s->flags & OL_PLAIN) != 0; oi->first = (s->flags & OL_FIRST) != 0; }
    oi->level = outline_level(doc, d->caret.para);
    return 1;
  }
  caret_hide(d, obj);
  switch (m->cmd) {
  case CMD_OV_PROMOTE:
    ov_paras(d, &pa, &pb, 1);
    done = outline_promote(doc, pa, pb, (LONG)m->arg < 0 ? -1 : 1);
    break;
  case CMD_OV_SETLEVEL:
    ov_paras(d, &pa, &pb, 0);
    done = outline_set_level(doc, pa, pb, (int)m->arg);
    break;
  case CMD_OV_MOVE: case CMD_OV_MOVEPART: {
    u32 a, b, dest = 0, na = 0, i, np = doc->np;
    u8 *was = s && s->ncoll ? (u8 *)calloc(np, 1) : NULL;
    int ok_ = 0, dir = (LONG)m->arg < 0 ? -1 : 1;
    if (m->cmd == CMD_OV_MOVE) ov_paras(d, &a, &b, 1);
    else {
      const u32 *ft = (const u32 *)m->arg;
      a = ft[0]; dest = ft[1];
      b = a < np ? outline_end(doc, a) - 1 : a;
    }
    /* the folds go with the paragraphs (which are copies after the move) */
    if (was) for (i = 0; i < np; i++) was[i] = (u8)ov_collapsed(s, doc->p[i]);
    if (a >= np) { free(was); break; }
    if (m->cmd == CMD_OV_MOVE) {
      ok_ = outline_move(doc, a, b, dir, s && s->on && s->nhid == np ? s->hid : NULL, &na);
      dest = dir < 0 ? na : na + (b - a + 1);
    } else ok_ = dest <= np && (dest < a || dest > b + 1) && outline_move_to(doc, a, b, dest, &na);
    if (ok_) {
      d->caret.para = ov_map(d->caret.para, a, b, dest); d->anchor.para = ov_map(d->anchor.para, a, b, dest);
      if (m->cmd == CMD_OV_MOVEPART) { d->caret.para = d->anchor.para = na; d->caret.off = d->anchor.off = 0; }
      if (was) {
        s->ncoll = 0;
        for (i = 0; i < np; i++) if (was[i] && ov_map(i, a, b, dest) < doc->np) ov_set_collapsed(s, doc->p[ov_map(i, a, b, dest)], 1);
      }
      done = 1;
    }
    free(was);
    break; }
  case CMD_OV_EXPAND: {
    long h;
    if (!(s = ov(d))) break;
    ov_paras(d, &pa, &pb, 0);
    if (pa == pb) { if ((h = ov_heading(doc, pa)) >= 0) ov_fold(s, doc, (u32)h, (int)m->arg); }
    else { u32 i; for (i = pa; i <= pb; i++) if (outline_level(doc, i)) ov_fold(s, doc, i, (int)m->arg); }
    relayout(d, obj); refresh(d, obj, 0);               /* (the view changes, not the document) */
    if (s->on && s->nhid == doc->np && s->hid[d->caret.para] && (h = ov_heading(doc, d->caret.para)) >= 0) {
      /* the caret was folded away: to the heading that hides it */
      u32 hh = (u32)h;
      while (s->hid[hh] && hh > 0) { long p = ov_parent(doc, hh); if (p < 0) break; hh = (u32)p; }
      d->caret.para = d->anchor.para = hh; d->caret.off = d->anchor.off = 0;
      refresh(d, obj, 0);
    }
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 1; }
  case CMD_OV_SHOWLEVEL:
    if (!(s = ov(d))) break;
    s->levels = (int)m->arg;
    s->ncoll = 0;
    relayout(d, obj); refresh(d, obj, 0);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 1;
  case CMD_OV_FLAGS:
    if (!(s = ov(d))) break;
    s->flags ^= (int)m->arg & (OL_PLAIN | OL_FIRST);
    relayout(d, obj); refresh(d, obj, 0);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 1;
  }
  if (done) refresh(d, obj, 1);
  else { DisplayBeep(NULL); refresh(d, obj, 0); }
  return 1;
}
/* (draw_all, Outline view) the outline symbols beside page k's paragraphs: a plus in a circle for a
   heading with something under it, a minus for one without, a small square for body text; a dotted
   line under a collapsed heading, and "..." after a body paragraph cut to its first line */
static void ov_draw(struct Data *d, Object *obj, struct RastPort *rp, u32 k, LONG px, LONG pt, LONG h)
{
  OvState *s = (OvState *)d->wp11;
  Layout *L = d->lay;
  Doc *doc = d->doc;
  u32 a, b, i;
  LONG r = tx(d, 80) < 3 ? 3 : tx(d, 80) > 6 ? 6 : tx(d, 80);
  if (!s || !s->on || !L || !L->outline) return;
  lay_page_lines(L, k, &a, &b);
  SetDrMd(rp, JAM1);
  for (i = a; i < b; i++) {
    Placed *pc = &L->placed[i];
    ParaLay *pl;
    Line *ln;
    LONG x, y, lh, cx, cy;
    u32 pi = pc->para;
    if (pc->page != k || pi >= L->npl) continue;
    pl = &L->pl[pi];
    if (!pl->laid || pc->line >= pl->nlines || pl->okey & OK_HIDDEN || in_table(&pl->p->pf)) continue;
    ln = &pl->lines[pc->line];
    x = px + tx(d, page_of(d, k)->left + pc->x + ln->left);
    y = pt + tx(d, pc->y);
    lh = tx(d, ln->height);
    if (y + lh < 0 || y > h) continue;
    if (pc->line == 0) {
      int lv = pi < doc->np ? outline_level(doc, pi) : 0;
      cx = x - tx(d, 220); cy = y + lh / 2;
      SetAPen(rp, d->pen_text);
      if (lv) {
        DrawEllipse(rp, cx, cy, r, r);
        Move(rp, cx - r + 2, cy); Draw(rp, cx + r - 2, cy);
        if (outline_has_kids(doc, pi)) { Move(rp, cx, cy - r + 2); Draw(rp, cx, cy + r - 2); }
      } else RectFill(rp, cx - r / 2, cy - r / 2, cx + r / 2, cy + r / 2);
    }
    if (pc->line + 1 == pl->nlines) {
      LONG xe = x + tx(d, ln->width);
      if (pi < doc->np && ov_collapsed(s, doc->p[pi]) && outline_has_kids(doc, pi)) {
        LONG xx, yy = y + lh - 1;
        SetAPen(rp, d->pen_shadow);
        for (xx = x; xx < xe; xx += 3) WritePixel(rp, xx, yy);
      }
      if (pl->ocut) {
        SetAPen(rp, d->pen_text);
        WritePixel(rp, xe + 3, y + lh * 3 / 4); WritePixel(rp, xe + 6, y + lh * 3 / 4); WritePixel(rp, xe + 9, y + lh * 3 / 4);
      }
    }
  }
  (void)obj;
}
/* (handle_event) a click on an outline symbol selects the heading and what is under it (or the body
   paragraph); a double-click folds or unfolds the heading. MUI_EventHandlerRC_Eat if it was one */
static ULONG ov_mouse(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  OvState *s = (OvState *)d->wp11;
  Layout *L = d->lay;
  Doc *doc = d->doc;
  i32 px, py, tx_;
  u32 pg, a, b, i;
  if (!s || !s->on || !L || !L->outline || im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
  if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
  if (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT | IEQUALIFIER_LALT | IEQUALIFIER_RALT)) return 0;
  pg = point_page(d, obj, im->MouseX, im->MouseY, &px, &py);
  tx_ = px - page_of(d, pg)->left;
  lay_page_lines(L, pg, &a, &b);
  for (i = a; i < b; i++) {
    Placed *pc = &L->placed[i];
    ParaLay *pl;
    Line *ln;
    if (pc->page != pg || pc->line || pc->para >= L->npl) continue;
    pl = &L->pl[pc->para];
    if (!pl->laid || !pl->nlines || pl->okey & OK_HIDDEN || in_table(&pl->p->pf)) continue;
    ln = &pl->lines[0];
    if (py < pc->y || py >= pc->y + ln->height) continue;
    if (tx_ >= pc->x + ln->left - 40 || tx_ < pc->x + ln->left - 420) return 0;
    caret_hide(d, obj);
    if (DoubleClick(s->secs, s->micros, im->Seconds, im->Micros) && s->clickpara == pc->para && outline_has_kids(doc, pc->para)) {
      ov_set_collapsed(s, doc->p[pc->para], !ov_collapsed(s, doc->p[pc->para]));
      s->secs = 0;
      d->caret.para = d->anchor.para = pc->para; d->caret.off = d->anchor.off = 0;
      relayout(d, obj); refresh(d, obj, 0);
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    s->secs = im->Seconds; s->micros = im->Micros; s->clickpara = pc->para;
    {
      u32 e = outline_end(doc, pc->para);
      d->anchor.para = pc->para; d->anchor.off = 0;
      d->caret.para = e - 1; d->caret.off = doc->p[e - 1]->len;
    }
    refresh(d, obj, 0);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}
/* (wphooks.h's packages) the Outline view state of the view the last WpCtx was made for */
void wp11_view_info(struct OvInfo *oi)
{
  OvState *s = ctx_view ? (OvState *)ctx_view->wp11 : NULL;
  memset(oi, 0, sizeof *oi);
  if (s) { oi->on = s->on; oi->levels = s->levels; oi->plain = (s->flags & OL_PLAIN) != 0; oi->first = (s->flags & OL_FIRST) != 0; }
  if (ctx_view && ctx_view->doc) oi->level = outline_level(ctx_view->doc, ctx_view->caret.para);
}
