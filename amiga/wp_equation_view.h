/*
 * wp_equation_view.h - WP14b's part of the page view, included by view.c (after math_pen) so it can
 * use the view's own data and helpers: editing an equation in place. While the caret is in an
 * equation the view's caret (d->caret, d->anchor) sits just before its reference and this file keeps
 * the place inside it (core/mathedit.h's MPos); every change is made on a copy and put back with
 * doc_set_math, one undo step. view.c calls in from a few places: ev_command (its do_command),
 * ev_key (key), ev_caret (caret_place), ev_hidecaret (caret_show), ev_draw_math (draw_line, an
 * equation's box), ev_click (handle_event), ev_rclick (ctx_build) and ev_free (OM_DISPOSE).
 * This file belongs to WP14b.
 */
#include "wp_equation.h"

/* (view.c's own, defined further on) */
static void refresh(struct Data *d, Object *obj, int edited);
static void relayout(struct Data *d, Object *obj);
static void ensure_laid(struct Data *d, Object *obj, u32 para);
static void caret_hide(struct Data *d, Object *obj);
static void delete_sel(struct Data *d);
static Pos point_pos(struct Data *d, Object *obj, LONG mx, LONG my);

typedef struct {
  int active;                   /* the caret is in the equation at `at` */
  Doc *doc;
  Pos at;                       /* its reference in the text */
  MPos caret, anchor;           /* the caret inside it, and the selection's other end (sel) */
  int sel;
  int linear;                   /* shown in its Linear form: typing doesn't build up */
  int drag;                     /* the mouse button held after a click in it: dragging selects */
} EvState;
static struct Data *ev_last;    /* the view that last edited an equation (wp14b_view_info) */

static EvState *ev(struct Data *d)
{
  if (!d->wp14b) d->wp14b = calloc(1, sizeof(EvState));
  return (EvState *)d->wp14b;
}
static void ev_free(struct Data *d)
{
  free(d->wp14b); d->wp14b = NULL;
  if (ev_last == d) ev_last = NULL;
}
static int ev_has_sel(const EvState *s) { return s->sel && !mp_equal(&s->anchor, &s->caret); }
/* still editing: the caret where it was left, the equation still there */
static int ev_valid(struct Data *d)
{
  EvState *s = (EvState *)d->wp14b;
  Para *p;
  if (!s || !s->active) return 0;
  if (s->doc != d->doc || !d->doc || s->at.para >= d->doc->np || pos_cmp(d->caret, s->at) || pos_cmp(d->anchor, s->at) ||
      s->at.off + 4 > (p = d->doc->p[s->at.para])->len || !IS_MATH(p->t + s->at.off)) { s->active = 0; return 0; }
  return 1;
}
static MathEq *ev_eq(struct Data *d)
{
  EvState *s = (EvState *)d->wp14b;
  return doc_math(d->doc, math_index(d->doc->p[s->at.para]->t + s->at.off));
}
/* the equation's box on the screen: its origin (its left edge on its baseline) in window pixels, and
   the box (its formatting); 0 if it isn't laid out */
static Box *ev_origin(struct Data *d, Object *obj, Pos at, LONG *ox, LONG *oy)
{
  Layout *L = d->lay;
  ParaLay *pl;
  u32 li, k;
  if (!L || at.para >= d->doc->np) return NULL;
  ensure_laid(d, obj, at.para);
  if (at.para >= L->npl) return NULL;
  pl = &L->pl[at.para];
  for (li = 0; li < pl->nlines; li++) {
    Line *ln = &pl->lines[li];
    for (k = ln->box0; k < ln->box0 + ln->nbox && k < pl->nboxes; k++) {
      Box *b = &pl->boxes[k];
      if (b->kind == BX_MATH && b->off == at.off) {
        Placed *pc;
        if (pl->placed0 + li >= L->nplaced) return NULL;
        pc = &L->placed[pl->placed0 + li];
        *ox = _mleft(obj) + page_x(d, pc->page, _mwidth(obj)) + tx(d, lay_page(L, pc->page)->left + pc->x + ln->left + b->x);
        *oy = _mtop(obj) + page_top(d, pc->page) - d->scrolly + tx(d, pc->y + ln->ascent);
        return b;
      }
    }
  }
  return NULL;
}
static void ev_enter(struct Data *d, Pos at, int end)
{
  EvState *s = ev(d);
  MathEq *e;
  if (!s) return;
  s->active = 1; s->doc = d->doc; s->at = at; s->sel = 0;
  d->caret = d->anchor = at; d->pending = 0;
  e = ev_eq(d);
  if (end) mp_end(e, &s->caret); else mp_start(e, &s->caret);
  s->anchor = s->caret;
  s->linear = e && me_is_linear_form(e);
  ev_last = d;
}
/* the editing over: what was typed last built up (Professional form), the caret before or after it */
static void ev_leave(struct Data *d, Object *obj, int after)
{
  EvState *s = (EvState *)d->wp14b;
  MathEq *e, *c;
  if (!s || !s->active) return;
  s->active = 0;
  e = ev_eq(d);
  if (e && !s->linear && (c = math_clone(e))) {
    u16 l;
    int changed = 0;
    for (l = 0; l < c->nline; l++) { MPos q; memset(&q, 0, sizeof q); q.line = l; q.off = (u16)mp_units(c->line[l]); changed |= me_build_up(c, &q, wp14b_latex); }
    if (changed && doc_set_math(d->doc, s->at, c) >= 0) relayout(d, obj);
    else if (!changed) math_free(c);
  }
  d->caret = d->anchor = s->at;
  if (after) d->caret.off += 4, d->anchor = d->caret;
  d->drawn_sel = 1;                    /* (its frame goes: drawn again in full) */
}
/* the edited copy put back (one undo step) and shown */
static void ev_commit(struct Data *d, Object *obj, MathEq *e)
{
  EvState *s = (EvState *)d->wp14b;
  if (doc_set_math(d->doc, s->at, e) < 0) { DisplayBeep(NULL); return; }
  d->caret = d->anchor = s->at;
  d->drawn_sel = 1;
  refresh(d, obj, 1);
}
static void ev_show(struct Data *d, Object *obj)
{
  d->drawn_sel = 1;
  refresh(d, obj, 0);
}
static void ev_tab(void) { if (wp_ribbon()) set(wp_ribbon(), MUIA_Ribbon_Tab, TAB_EQUATION); }

/* ------------------------------------------------------------------ */
/* drawing */
typedef struct { struct RastPort *rp; LONG x, base, zoom; } EvDraw;
static void ev_dotted(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
  LONG t;
  for (t = x0; t <= x1; t += 2) { WritePixel(rp, t, y0); WritePixel(rp, t, y1); }
  for (t = y0; t <= y1; t += 2) { WritePixel(rp, x0, t); WritePixel(rp, x1, t); }
}
static void ev_slot(void *ctx, i32 x, i32 top, i32 w, i32 h)
{
  EvDraw *v = (EvDraw *)ctx;
  LONG x0 = v->x + x * v->zoom / 2000, y0 = v->base + top * v->zoom / 2000, x1 = v->x + (x + w) * v->zoom / 2000, y1 = v->base + (top + h) * v->zoom / 2000;
  if (x1 - x0 < 3) x1 = x0 + 3;
  if (y1 - y0 < 4) y1 = y0 + 4;
  ev_dotted(v->rp, x0, y0, x1, y1);
}
/* an equation's box: the selection inside it, the equation, its empty arguments' placeholders and,
   while it is edited, a frame round it */
static void ev_draw_math(struct Data *d, Object *obj, struct RastPort *rp, Layout *L, ParaLay *pl, Placed *pc, Box *b, LONG x, LONG base)
{
  EvState *s = (EvState *)d->wp14b;
  int k = math_index(pl->p->t + b->off);
  MathEq *e = doc_math(L->d, k);
  const MLay *ml = math_layout(L->fs, L->d, k, &b->f);
  int act = s && s->active && s->doc == d->doc && pc->para == s->at.para && b->off == s->at.off && !pos_cmp(d->caret, s->at);
  i32 x0, x1, top, h;
  if (act && e && ev_has_sel(s) && me_sel_box(e, &s->anchor, &s->caret, &x0, &x1, &top, &h)) {
    SetAPen(rp, d->pen_sel);
    RectFill(rp, x + tx(d, x0), base + tx(d, top), x + tx(d, x1) - 1 > x + tx(d, x0) ? x + tx(d, x1) - 1 : x + tx(d, x0), base + tx(d, top + h));
  }
  mpen_d = d; mpen_obj = obj;
  wp14a_draw(rp, L, b, pl->p->t, x, base, d->zoom, math_pen);
  if (e && pc->para != 0xFFFFFFFFUL) {
    EvDraw v;
    v.rp = rp; v.x = x; v.base = base; v.zoom = d->zoom;
    SetAPen(rp, d->pen_marks);
    me_slots(e, ev_slot, &v);
    if (act && ml) ev_dotted(rp, x - 2, base - tx(d, ml->asc) - 2, x + tx(d, ml->w) + 2, base + tx(d, ml->desc) + 2);
  }
}
/* the caret inside the equation (caret_place): 1 if it is there */
static int ev_caret(struct Data *d, Object *obj)
{
  EvState *s;
  LONG ox, oy;
  Box *b;
  MathEq *e;
  i32 x, top, h;
  if (!ev_valid(d)) return 0;
  s = (EvState *)d->wp14b;
  if (!(b = ev_origin(d, obj, s->at, &ox, &oy)) || !(e = ev_eq(d))) return 0;
  math_layout(d->fs, d->doc, math_index(d->doc->p[s->at.para]->t + s->at.off), &b->f);
  mp_clamp(e, &s->caret);
  me_caret_box(e, &s->caret, &x, &top, &h);
  d->cx = ox + tx(d, x); d->cy = oy + tx(d, top); d->ch = tx(d, h);
  if (d->ch < 4) d->ch = 4;
  return 1;
}
static int ev_hidecaret(struct Data *d) { return ev_valid(d) && ev_has_sel((EvState *)d->wp14b); }

/* ------------------------------------------------------------------ */
/* the mouse */
/* the equation under the pointer (next to point_pos's place) and the place in it; 0 if none */
static int ev_hit(struct Data *d, Object *obj, LONG mx, LONG my, Pos *at, MPos *mp)
{
  Pos p = point_pos(d, obj, mx, my), c[2];
  int i;
  if (p.para >= d->doc->np) return 0;
  c[0] = p; c[1] = p; c[1].off = p.off >= 4 ? p.off - 4 : 0xFFFFFFFFUL;
  for (i = 0; i < 2; i++) {
    Para *q = d->doc->p[p.para];
    LONG ox, oy;
    Box *b;
    const MLay *ml;
    int k;
    if (c[i].off == 0xFFFFFFFFUL || c[i].off + 4 > q->len || !IS_MATH(q->t + c[i].off)) continue;
    if (!(b = ev_origin(d, obj, c[i], &ox, &oy))) continue;
    k = math_index(q->t + c[i].off);
    if (!(ml = math_layout(d->fs, d->doc, k, &b->f))) continue;
    if (mx >= ox - 2 && mx <= ox + tx(d, ml->w) + 2 && my >= oy - tx(d, ml->asc) - 3 && my <= oy + tx(d, ml->desc) + 3) {
      *at = c[i];
      me_hit(doc_math(d->doc, k), tw(d, mx - ox), tw(d, my - oy), mp);
      return 1;
    }
  }
  return 0;
}
static int ev_click(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  EvState *s;
  Pos at;
  MPos mp;
  int shift = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, was = ev_valid(d);
  if (d->vmode == VM_READ) return 0;
  if (!ev_hit(d, obj, im->MouseX, im->MouseY, &at, &mp)) {
    if (was) { ev_leave(d, obj, 0); }
    return 0;
  }
  if (!(s = ev(d))) return 0;
  caret_hide(d, obj);
  if (was && shift && !pos_cmp(at, s->at)) {
    if (!s->sel) { s->anchor = s->caret; s->sel = 1; }
    s->caret = mp;
  } else {
    if (was && pos_cmp(at, s->at)) ev_leave(d, obj, 0);
    ev_enter(d, at, 0);
    s->caret = s->anchor = mp;
    s->sel = 1;                          /* (empty until the pointer is dragged) */
  }
  s->drag = 1;
  set(_win(obj), MUIA_Window_ActiveObject, (ULONG)obj);
  ev_show(d, obj);
  if (!was) ev_tab();
  return 1;
}
/* the pointer dragged with the button held, after a click in the equation: the selection follows it */
static ULONG ev_mouse(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  EvState *s = (EvState *)d->wp14b;
  Pos at;
  MPos mp;
  if (!s || !s->drag) return 0;
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP) { s->drag = 0; return MUI_EventHandlerRC_Eat; }
  if (im->Class != IDCMP_MOUSEMOVE) return 0;
  if (!ev_valid(d)) { s->drag = 0; return 0; }
  if (ev_hit(d, obj, im->MouseX, im->MouseY, &at, &mp) && !pos_cmp(at, s->at) && !mp_equal(&mp, &s->caret)) {
    caret_hide(d, obj);
    if (!s->sel) { s->anchor = s->caret; s->sel = 1; }
    s->caret = mp;
    ev_show(d, obj);
  }
  return MUI_EventHandlerRC_Eat;
}
static int ev_rclick(struct Data *d, Object *obj, LONG mx, LONG my)
{
  EvState *s;
  Pos at;
  MPos mp;
  int was = ev_valid(d);
  if (!ev_hit(d, obj, mx, my, &at, &mp)) { if (was) ev_leave(d, obj, 0); return 0; }
  if (!(s = ev(d))) return 0;
  if (was && !pos_cmp(at, s->at) && ev_has_sel(s)) return 1;      /* (the selection stays for its commands) */
  if (was && pos_cmp(at, s->at)) ev_leave(d, obj, 0);
  caret_hide(d, obj);
  ev_enter(d, at, 0);
  s->caret = s->anchor = mp;
  ev_show(d, obj);
  return 1;
}

/* ------------------------------------------------------------------ */
/* changes */
/* a new equation at the caret: the selected text built up into it, or what row holds (taken), or
   empty; the caret in it */
static void ev_new(struct Data *d, Object *obj, MNode *row, const char *text)
{
  Doc *doc = d->doc;
  MathEq *e;
  Pos at;
  int display, k;
  EvState *s;
  if (!doc || !(s = ev(d))) { mn_free(row); return; }
  caret_hide(d, obj);
  if (has_sel(d)) {
    u32 n;
    char *t = doc_text(doc, sel_lo(d), sel_hi(d), &n);
    Pos lo = sel_lo(d);
    display = lo.off == 0 && pos_cmp(sel_hi(d), lo) && sel_hi(d).para == lo.para && sel_hi(d).off == doc->p[lo.para]->len;
    e = t ? math_from_linear(t, display) : math_new(display);
    free(t);
    delete_sel(d);
  } else {
    display = doc->p[d->caret.para]->len == 0;     /* alone in its paragraph: a display equation, as Word */
    e = math_new(display);
  }
  if (!e) { mn_free(row); DisplayBeep(NULL); return; }
  at = d->caret;
  if ((k = doc_insert_math(doc, &d->caret, e, NULL)) < 0) { mn_free(row); DisplayBeep(NULL); refresh(d, obj, 1); return; }
  ev_enter(d, at, 1);
  if (row || text) {
    MathEq *c = math_clone(doc_math(doc, k));
    if (c) {
      mp_start(c, &s->caret);
      if (row) me_insert_row(c, &s->caret, row, NULL);
      else me_insert_text(c, &s->caret, text, wp14b_normal);
      s->anchor = s->caret;
      doc_set_math(doc, at, c);
    } else mn_free(row);
  }
  s->linear = 0;
  d->caret = d->anchor = at;
  d->drawn_sel = 1;
  refresh(d, obj, 1);
  ev_tab();
}
/* the equation selected (exactly one) or edited: where it is */
static int ev_target(struct Data *d, Pos *at)
{
  Pos lo, hi;
  if (ev_valid(d)) { *at = ((EvState *)d->wp14b)->at; return 1; }
  lo = sel_lo(d); hi = sel_hi(d);
  if (lo.para < d->doc->np && lo.para == hi.para && hi.off == lo.off + 4 && IS_MATH(d->doc->p[lo.para]->t + lo.off)) { *at = lo; return 1; }
  return 0;
}
/* display (a paragraph of its own) or inline */
static void ev_display(struct Data *d, Object *obj, int disp)
{
  Pos at, q;
  MathEq *e;
  EvState *s = (EvState *)d->wp14b;
  int act = ev_valid(d);
  if (!ev_target(d, &at) || !(e = math_clone(doc_math(d->doc, math_index(d->doc->p[at.para]->t + at.off))))) { DisplayBeep(NULL); return; }
  caret_hide(d, obj);
  if (disp) {
    if (at.off > 0) { q = at; doc_begin(d->doc, q); doc_insert(d->doc, &q, "\n", 1, NULL); doc_end(d->doc, q); at = q; }
    if (at.off + 4 < d->doc->p[at.para]->len) { q = at; q.off += 4; doc_begin(d->doc, q); doc_insert(d->doc, &q, "\n", 1, NULL); doc_end(d->doc, q); }
  }
  e->display = (u8)disp;
  if (doc_set_math(d->doc, at, e) < 0) { DisplayBeep(NULL); refresh(d, obj, 1); return; }
  if (act) { s->at = at; d->caret = d->anchor = at; }
  else { d->anchor = at; d->caret = at; d->caret.off += 4; }
  math_renumber(d->doc);
  d->drawn_sel = 1;
  refresh(d, obj, 1);
}
/* one equation changed by f (on a copy; 0: nothing to do) */
static int ev_change(struct Data *d, Object *obj, int (*f)(MathEq *e, MPos *p, long arg), long arg)
{
  Pos at;
  MathEq *e;
  EvState *s = (EvState *)d->wp14b;
  int act = ev_valid(d);
  MPos dummy;
  if (!ev_target(d, &at) || !(e = math_clone(doc_math(d->doc, math_index(d->doc->p[at.para]->t + at.off))))) { DisplayBeep(NULL); return 0; }
  memset(&dummy, 0, sizeof dummy);
  if (!f(e, act ? &s->caret : &dummy, arg)) { math_free(e); DisplayBeep(NULL); return 0; }
  if (act) { s->sel = 0; mp_clamp(e, &s->caret); s->anchor = s->caret; }
  caret_hide(d, obj);
  if (doc_set_math(d->doc, at, e) < 0) { DisplayBeep(NULL); return 0; }
  if (act) d->caret = d->anchor = at;
  math_renumber(d->doc);
  d->drawn_sel = 1;
  refresh(d, obj, 1);
  return 1;
}
static int f_convert(MathEq *e, MPos *p, long latex_linear)
{
  int r = (latex_linear & 2) ? me_to_linear_form(e, (int)(latex_linear & 1)) : me_to_professional(e, (int)(latex_linear & 1));
  if (r) mp_end(e, p);
  return r;
}
static int f_jc(MathEq *e, MPos *p, long jc) { (void)p; e->jc = (u8)(jc + 1); return 1; }
static int f_apply(MathEq *e, MPos *p, long arg) { int dp = (int)(arg & 255); return me_apply(e, p, (int)(arg >> 8), dp == 255 ? -1 : dp); }
/* every equation of the document converted (each its own undo step) */
static void ev_convert_all(struct Data *d, Object *obj, int linear)
{
  u32 pi, o;
  int n = 0;
  caret_hide(d, obj);
  for (pi = 0; pi < d->doc->np; pi++)
    for (o = 0; o + 4 <= d->doc->p[pi]->len; o++) {
      Para *p = d->doc->p[pi];
      MathEq *e;
      Pos at;
      if (!IS_MATH(p->t + o)) continue;
      at.para = pi; at.off = o;
      if ((e = math_clone(doc_math(d->doc, math_index(p->t + o))))) {
        if ((linear ? me_to_linear_form(e, wp14b_latex) : me_to_professional(e, wp14b_latex)) && doc_set_math(d->doc, at, e) >= 0) n++;
        else math_free(e);
      }
      o += 3;
    }
  if (d->wp14b) ((EvState *)d->wp14b)->linear = linear;
  if (!n) DisplayBeep(NULL);
  d->drawn_sel = 1;
  refresh(d, obj, 1);
}
/* text typed (or put in) at the equation's caret, the selection replaced; Professional form builds
   up on spaces and operators */
static void ev_type(struct Data *d, Object *obj, const char *u, int typed)
{
  EvState *s = (EvState *)d->wp14b;
  MathEq *e = math_clone(ev_eq(d));
  int used, built = 0, normal = wp14b_normal;
  u32 c = utf8_get(u, strlen(u), &used);
  char buf[8];
  if (!e) return;
  if (ev_has_sel(s)) { MPos q; mn_free(me_cut(e, &s->anchor, &s->caret, &q)); s->caret = q; }
  s->sel = 0;
  if (typed && !normal && (size_t)used == strlen(u)) {
    int op = c < 128 && c && strchr("+-=<>,;:!*", (int)c);
    if (c == '-') { buf[utf8_put(buf, 0x2212)] = 0; u = buf; }
    if (!s->linear && (c == ' ' || (op && !wp14b_latex))) built = me_build_up(e, &s->caret, wp14b_latex);
    if (c == ' ' && !s->linear && !wp14b_latex) {
      /* a space builds up, or completes a \name; it isn't kept */
      if (!built) built = me_autocorrect(e, &s->caret);
      if (built) { s->anchor = s->caret; ev_commit(d, obj, e); return; }
    } else if (c == ' ' && wp14b_latex && built) { s->anchor = s->caret; ev_commit(d, obj, e); return; }
  }
  me_insert_text(e, &s->caret, u, normal);
  s->anchor = s->caret;
  ev_commit(d, obj, e);
}
/* the selection in the equation as linear text on the clipboard (Latin-1, \names for the rest) */
static void ev_copy(struct Data *d)
{
  EvState *s = (EvState *)d->wp14b;
  MNode *r = me_copy(ev_eq(d), &s->anchor, &s->caret);
  MathEq tmp;
  MNode *one[1];
  Buf u, k;
  char *l;
  if (!r) return;
  memset(&tmp, 0, sizeof tmp); one[0] = r; tmp.line = one; tmp.nline = 1;
  buf_init(&u); buf_init(&k);
  if (wp14b_latex) math_to_latex(&tmp, &u); else math_to_linear(&tmp, &u);
  if (u.data) math_keywords(u.data, &k);
  if (k.data && (l = (char *)malloc(k.len * 3 + 4))) { size_t m = utf8_to_latin1(k.data, (u32)k.len, l, k.len * 3 + 4); clip_write(l, (long)m); free(l); }
  buf_free(&u); buf_free(&k);
  mn_free(r);
}
static void ev_paste(struct Data *d, Object *obj)
{
  EvState *s = (EvState *)d->wp14b;
  long n = 0, i;
  char *t = clip_read(&n), *u;
  MathEq *in, *e;
  if (!t) return;
  if (!(u = (char *)malloc((size_t)n * 2 + 1))) { free(t); return; }
  {
    long o = 0;
    for (i = 0; i < n; i++) if (t[i] != '\n' && t[i] != '\r') o += utf8_put(u + o, (u8)t[i]);
    u[o] = 0;
  }
  free(t);
  in = wp14b_latex ? math_from_latex(u, 0) : math_from_linear(u, 0);
  free(u);
  if (!in || !(e = math_clone(ev_eq(d)))) { math_free(in); return; }
  if (ev_has_sel(s)) { MPos q; mn_free(me_cut(e, &s->anchor, &s->caret, &q)); s->caret = q; }
  s->sel = 0;
  {
    MNode *row = in->line[0];
    in->line[0] = mn_new(MK_ROW);
    math_free(in);
    me_insert_row(e, &s->caret, row, NULL);
  }
  s->anchor = s->caret;
  ev_commit(d, obj, e);
}

/* ------------------------------------------------------------------ */
/* keys */
static int ev_key(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  Doc *doc = d->doc;
  UWORD code = im->Code, q = im->Qualifier;
  int shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  int ctrl = (q & IEQUALIFIER_CONTROL) != 0, changed = 0, leave = -1, sel = 0;
  EvState *s;
  MathEq *e;
  if (code & IECODE_UP_PREFIX) return 0;
  if (q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) return 0;
  if (!ev_valid(d)) {
    Para *p = doc->p[d->caret.para];
    /* Alt+=: a new equation (Word's key; = on a US keyboard) */
    if (code == 0x0C && alt && !ctrl && !shift) { ev_new(d, obj, NULL, NULL); return 1; }
    /* the cursor keys into an equation next to the caret */
    if (!has_sel(d) && !shift && !alt && !ctrl && (code == 0x4E || code == 0x4F)) {
      Pos at = d->caret;
      if (code == 0x4E && at.off + 4 <= p->len && IS_MATH(p->t + at.off)) ;
      else if (code == 0x4F && at.off >= 4 && IS_MATH(p->t + at.off - 4)) at.off -= 4;
      else return 0;
      caret_hide(d, obj);
      ev_enter(d, at, code == 0x4F);
      ev_show(d, obj);
      ev_tab();
      return 1;
    }
    return 0;
  }
  s = (EvState *)d->wp14b;
  ev_last = d;
  if (!(e = math_clone(ev_eq(d)))) return 1;
  mp_clamp(e, &s->caret); mp_clamp(e, &s->anchor);
  caret_hide(d, obj);
  switch (code) {
  case 0x4F: case 0x4E: {                      /* left, right */
    int right = code == 0x4E;
    if (!shift && ev_has_sel(s)) {
      MPos rp; int u0, u1;
      if (me_sel_range(e, &s->anchor, &s->caret, &rp, &u0, &u1)) { s->caret = rp; s->caret.off = (u16)(right ? u1 : u0); }
      s->sel = 0;
      break;
    }
    if (shift && !s->sel) { s->anchor = s->caret; s->sel = 1; }
    if (!shift) s->sel = 0;
    if (ctrl) me_home(e, &s->caret, right);
    else if (alt) me_word(e, &s->caret, right);
    else if (!(right ? me_right(e, &s->caret) : me_left(e, &s->caret)) && !shift) leave = right;
    break; }
  case 0x4C: case 0x4D:                        /* up, down */
    if (shift && !s->sel) { s->anchor = s->caret; s->sel = 1; }
    if (!shift) s->sel = 0;
    if (!me_vertical(e, &s->caret, code == 0x4C) && !shift) leave = 2 + (code == 0x4D);     /* out, and the view moves up or down */
    break;
  case 0x70: case 0x71:                        /* Home, End */
    if (shift && !s->sel) { s->anchor = s->caret; s->sel = 1; }
    if (!shift) s->sel = 0;
    me_home(e, &s->caret, code == 0x71);
    break;
  case 0x41: case 0x46:                        /* Backspace, Del */
    if (ev_has_sel(s)) { MPos qq; mn_free(me_cut(e, &s->anchor, &s->caret, &qq)); s->caret = qq; s->sel = 0; changed = 1; break; }
    s->sel = 0;
    if (alt) { MPos a = s->caret; me_word(e, &a, code == 0x46); s->anchor = a; s->sel = 1; if (ev_has_sel(s)) { MPos qq; mn_free(me_cut(e, &s->anchor, &s->caret, &qq)); s->caret = qq; changed = 1; } s->sel = 0; break; }
    changed = code == 0x41 ? me_backspace(e, &s->caret, &s->anchor, &sel) : me_delete(e, &s->caret, &s->anchor, &sel);
    if (sel) s->sel = 1;
    break;
  case 0x44: case 0x43:                        /* Return: a new line in a display equation (Shift), or out after it */
    if (shift) { changed = me_split_line(e, &s->caret); if (!changed) DisplayBeep(NULL); break; }
    if (!s->linear) changed = me_build_up(e, &s->caret, wp14b_latex);
    {
      MPos endp;
      mp_end(e, &endp);
      if (mp_equal(&endp, &s->caret) || !e->display) leave = 4;   /* out after it; the view makes the new paragraph */
    }
    break;
  case 0x42:                                   /* Tab: the next empty argument */
    if (!s->linear) changed = me_build_up(e, &s->caret, wp14b_latex);
    s->sel = 0;
    if (!me_next_slot(e, &s->caret, shift)) { if (!changed) { math_free(e); ev_show(d, obj); return 1; } }
    break;
  case 0x45:                                   /* Esc: out of the equation, after it */
    if (ev_has_sel(s)) { s->sel = 0; break; }
    leave = 1;
    break;
  case 0x0C:
    if (ctrl) {                                /* Ctrl+=: Professional / Linear */
      int r = s->linear ? me_to_professional(e, wp14b_latex) : me_to_linear_form(e, wp14b_latex);
      if (r) { s->linear = !s->linear; mp_end(e, &s->caret); s->sel = 0; changed = 1; }
      break;
    }
    /* fall through: typed */
  default: {
    struct InputEvent ie;
    char buf[8], u[32];
    LONG n, i, o = 0;
    if (code == 0x7A || code == 0x7B) { math_free(e); return 0; }    /* the wheel: the view scrolls */
    memset(&ie, 0, sizeof ie);
    ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = code; ie.ie_Qualifier = q & ~IEQUALIFIER_CONTROL;
    ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
    n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
    if (n > 0 && (u8)buf[0] == 0x9B) n = 0;
    for (i = 0; i < n; i++) if ((u8)buf[i] >= 32 && (u8)buf[i] != 127 && !((u8)buf[i] >= 0x80 && (u8)buf[i] < 0xA0)) o += utf8_put(u + o, (u8)buf[i]);
    u[o] = 0;
    math_free(e);
    if (!o) { ev_show(d, obj); return n > 0 ? 1 : 0; }
    {
      /* each character on its own (a space or an operator may build up what is before it) */
      int k = 0;
      while (k < o) {
        int used;
        char one[8];
        utf8_get(u + k, (size_t)(o - k), &used);
        memcpy(one, u + k, (size_t)used); one[used] = 0;
        ev_type(d, obj, one, 1);
        k += used;
      }
    }
    return 1; }
  }
  if (leave >= 0) {
    math_free(e);
    ev_leave(d, obj, leave == 1 || leave == 3 || leave == 4);
    if (leave >= 2) { refresh(d, obj, 0); return 0; }        /* the view goes on with the key from just outside */
    ev_show(d, obj);
    return 1;
  }
  if (changed) { if (!s->sel) s->anchor = s->caret; ev_commit(d, obj, e); return 1; }
  math_free(e);
  if (!s->sel) s->anchor = s->caret;
  ev_show(d, obj);
  return 1;
}

/* ------------------------------------------------------------------ */
/* commands (do_command): the package's, and Copy, Cut, Paste, Delete, Select All and typed text while
   an equation is edited. 1 if done here */
static int ev_command(struct Data *d, Object *obj, struct MUIP_Quill_Command *m)
{
  EvState *s;
  if (!d->doc) return 0;
  if (IS_EV_CMD(m->cmd)) {
    int act = ev_valid(d);
    s = ev(d);
    if (!s) return 1;
    if (act) ev_last = d;
    switch (m->cmd) {
    case CMD_EV_INFO: {
      EvInfo *in = (EvInfo *)m->arg;
      Pos at;
      MathEq *e = NULL;
      memset(in, 0, sizeof *in);
      in->fs = d->fs; in->doc = d->doc;
      in->normal = wp14b_normal;
      if (ev_target(d, &at)) e = doc_math(d->doc, math_index(d->doc->p[at.para]->t + at.off));
      if (!e) return 1;
      in->active = act; in->selected = 1;
      in->display = e->display; in->jc = e->jc ? e->jc - 1 : -1; in->numbered = math_numbered(e) != 0;
      in->linear = act ? s->linear : me_is_linear_form(e);
      if (act) {
        int i;
        mp_clamp(e, &s->caret);
        in->nact = me_actions(e, &s->caret, in->act, EV_MAXACT);
        for (i = 0; i < in->nact; i++) in->label[i] = me_action_label(in->act[i].act, e, &s->caret, in->act[i].depth);
        in->normal = wp14b_normal || me_is_normal(e, &s->caret);
      }
      return 1; }
    case CMD_EV_NEW:
      if (act) return 1;
      ev_new(d, obj, NULL, NULL);
      return 1;
    case CMD_EV_INSERT: case CMD_EV_TEXT: {
      MNode *row = m->cmd == CMD_EV_INSERT ? (MNode *)m->arg : NULL;
      const char *text = m->cmd == CMD_EV_TEXT ? (const char *)m->arg : NULL;
      Pos at;
      if (!act && ev_target(d, &at)) {
        /* the equation selected: edited, the structure put in at its end */
        caret_hide(d, obj);
        ev_enter(d, at, 1);
        act = 1;
      }
      if (!act) { ev_new(d, obj, row, text); return 1; }
      if (row) {
        MathEq *e = math_clone(ev_eq(d));
        MNode *cut = NULL;
        if (!e) { mn_free(row); return 1; }
        caret_hide(d, obj);
        if (ev_has_sel(s)) { MPos q; cut = me_cut(e, &s->anchor, &s->caret, &q); s->caret = q; }
        s->sel = 0;
        me_insert_row(e, &s->caret, row, cut);
        s->anchor = s->caret;
        ev_commit(d, obj, e);
      } else { caret_hide(d, obj); ev_type(d, obj, text, 0); }
      ev_tab();
      return 1; }
    case CMD_EV_APPLY: ev_change(d, obj, f_apply, (long)m->arg); return 1;
    case CMD_EV_CONVERT:
      if (m->arg >= 2) { ev_convert_all(d, obj, m->arg == 3); return 1; }
      if (ev_change(d, obj, f_convert, (long)((m->arg == 1 ? 2 : 0) | (wp14b_latex ? 1 : 0))) && act) s->linear = m->arg == 1;
      return 1;
    case CMD_EV_NORMAL:
      if (act && ev_has_sel(s)) {
        MathEq *e = math_clone(ev_eq(d));
        if (e && me_set_normal(e, &s->anchor, &s->caret, wp14b_normal)) { caret_hide(d, obj); ev_commit(d, obj, e); }
        else math_free(e);
      }
      return 1;
    case CMD_EV_DISPLAY: ev_display(d, obj, (int)m->arg); return 1;
    case CMD_EV_JUSTIFY: ev_change(d, obj, f_jc, (long)m->arg); return 1;
    case CMD_EV_NUMBER: {
      Pos at;
      MathEq *e;
      int n = 1;
      if (!ev_target(d, &at) || !(e = doc_math(d->doc, math_index(d->doc->p[at.para]->t + at.off)))) { DisplayBeep(NULL); return 1; }
      if (!math_numbered(e)) {
        /* a number needs a display equation, on a line of its own */
        if (!e->display) { ev_display(d, obj, 1); if (!ev_target(d, &at)) return 1; e = doc_math(d->doc, math_index(d->doc->p[at.para]->t + at.off)); }
        {
          MathEq *c = math_clone(e);
          if (!c || !me_number(c, 1, n)) { math_free(c); DisplayBeep(NULL); return 1; }
          caret_hide(d, obj);
          doc_set_math(d->doc, at, c);
        }
      } else {
        MathEq *c = math_clone(e);
        if (!c || !me_number(c, 0, 0)) { math_free(c); DisplayBeep(NULL); return 1; }
        caret_hide(d, obj);
        doc_set_math(d->doc, at, c);
      }
      if (act) { mp_clamp(doc_math(d->doc, math_index(d->doc->p[s->at.para]->t + s->at.off)), &s->caret); s->anchor = s->caret; s->sel = 0; d->caret = d->anchor = s->at; }
      math_renumber(d->doc);
      d->drawn_sel = 1;
      refresh(d, obj, 1);
      return 1; }
    case CMD_EV_CLOSE:
      if (act) { caret_hide(d, obj); ev_leave(d, obj, 1); ev_show(d, obj); }
      return 1;
    }
    return 1;
  }
  if (!ev_valid(d)) return 0;
  s = (EvState *)d->wp14b;
  ev_last = d;
  switch (m->cmd) {
  case CMD_TEXT: case CMD_REPLACE:
    if (m->arg && ((const char *)m->arg)[0] && ((const char *)m->arg)[0] != '\n') { caret_hide(d, obj); ev_type(d, obj, (const char *)m->arg, 0); }
    return 1;
  case CMD_COPY: if (ev_has_sel(s)) ev_copy(d); return 1;
  case CMD_CUT: case CMD_DELETE:
    if (ev_has_sel(s)) {
      MathEq *e = math_clone(ev_eq(d));
      MPos q;
      if (m->cmd == CMD_CUT) ev_copy(d);
      if (e) { caret_hide(d, obj); mn_free(me_cut(e, &s->anchor, &s->caret, &q)); s->caret = s->anchor = q; s->sel = 0; ev_commit(d, obj, e); }
    }
    return 1;
  case CMD_PASTE: case CMD_PASTEAS: caret_hide(d, obj); ev_paste(d, obj); return 1;
  case CMD_SELALL: {
    MathEq *e = ev_eq(d);
    if (e) {
      MPos a;
      memset(&a, 0, sizeof a); a.line = s->caret.line;
      s->anchor = a;
      s->caret = a; s->caret.off = (u16)mp_units(e->line[a.line]);
      s->sel = 1;
      caret_hide(d, obj);
      ev_show(d, obj);
    }
    return 1; }
  }
  return 0;
}

/* (wp_equation.h) the equation being edited in one of doc's views */
int wp14b_view_info(const Doc *doc, EvInfo *out)
{
  Object *v = ev_last && ev_last->doc == doc && ev_last->obj ? ev_last->obj : wp_view();
  memset(out, 0, sizeof *out);
  if (!v) return 0;
  DoMethod(v, MUIM_Quill_Command, CMD_EV_INFO, (ULONG)out);
  if (out->doc != doc) { FontSet *fs = out->fs; memset(out, 0, sizeof *out); out->fs = fs; }
  return out->selected;
}
