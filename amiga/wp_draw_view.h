/*
 * wp_draw_view.h - WP20's part of the page view, included by view.c (before its object drawing) so it
 * can use the view's own data and helpers: freeforms and ink drawn (hidden, or replayed stroke by
 * stroke), drawing with the mouse (the stroke shown as it is drawn, then smoothed into an ink object
 * floating on the page, or a shape with Ink to Shape on, or into the drawing canvas it was drawn on),
 * the eraser (whole strokes, or the bit under it), the lasso (choosing strokes, dragging them).
 * view.c calls in from draw_leaf (dv_leaf) and handle_event (dv_mouse). This file belongs to WP20.
 */
#include "wp_draw.h"
#include "../core/ink.h"

/* (view.c's own, defined further on) */
static void refresh(struct Data *d, Object *obj, int edited);
static void relayout(struct Data *d, Object *obj);
static void update_toolbar(struct Data *d);
static void caret_hide(struct Data *d, Object *obj);
static int obj_hit(struct Data *d, Object *obj, LONG mx, LONG my, Pos *at, FloatBox *box);
static int obj_box(struct Data *d, Pos at, int k, FloatBox *box);
static LONG obj_ref(Doc *doc, u32 pi, int img);
static u32 point_page(struct Data *d, Object *obj, LONG mx, LONG my, i32 *px, i32 *py);
static void thick_line(struct RastPort *rp, LONG ax, LONG ay, LONG bx, LONG by, int t);
static void fill_poly(struct RastPort *rp, const LONG *xy, int n);

enum { DG_NONE, DG_INK, DG_ERASE, DG_LASSO, DG_MOVE };
static struct {
  int drag;                     /* DG_ */
  Object *obj;                  /* the view dragged in */
  u32 page;                     /* the page it started on */
  i32 *pts; u32 n, cap;         /* the points so far (twips on the page) */
  LONG lx, ly;                  /* the last point on the screen */
  int incanvas; Pos cat; FloatBox cbox;   /* drawn on a drawing canvas: its reference and box */
  LONG mx0, my0, mx1, my1; int shown;     /* moving the lasso's: where it started, the frame shown */
  i32 bx0, by0, bx1, by1;       /* and their box on the page */
  /* Ink Replay: the strokes in the order they were drawn, how far it has got */
  int replay; Object *robj; Freeform **seq; u32 nseq, cur, upto;
} dv;

/* the screen point of a point on page k, and back */
static LONG dv_sx(struct Data *d, Object *obj, u32 k, i32 px) { return _mleft(obj) + page_x(d, k, _mwidth(obj)) + tx(d, px); }
static LONG dv_sy(struct Data *d, Object *obj, u32 k, i32 py) { return _mtop(obj) + page_top(d, k) - d->scrolly + tx(d, py); }
static i32 dv_px(struct Data *d, Object *obj, u32 k, LONG mx) { return tw(d, mx - _mleft(obj) - page_x(d, k, _mwidth(obj))); }
static i32 dv_py(struct Data *d, Object *obj, u32 k, LONG my) { return tw(d, my - _mtop(obj) + d->scrolly - page_top(d, k)); }

/* ---- drawing a freeform (and ink, unless hidden or not replayed yet) */
static u32 ff_total(const Freeform *f) { u32 k, t = 0; for (k = 0; k < f->np; k++) t += f->p[k].n; return t; }
/* how many of ink f's points are shown: all (0xFFFFFFFF), or fewer while it is replayed */
static u32 dv_shown(const Freeform *f)
{
  u32 i;
  if (!dv.replay) return 0xFFFFFFFFUL;
  for (i = 0; i < dv.nseq; i++) if (dv.seq[i] == f) return i < dv.cur ? 0xFFFFFFFFUL : i == dv.cur ? dv.upto : 0;
  return 0xFFFFFFFFUL;
}
static int dv_leaf(struct Data *d, Object *obj, struct RastPort *rp, const ObjPart *part, LONG pl, LONG pt, i32 cx, i32 cy)
{
  const Image *src = part->im;
  const Freeform *f = src->ff;
  Image t;
  LONG ox, oy, *xy;
  u32 k, i, left;
  int th;
  if (src->canvas == CANVAS_BG && src->fill == COLOR_AUTO && (src->lstyle == PB_NONE || !src->lwidth)) {
    /* a canvas with no fill or outline: its edge dotted on the screen, so it can be found (not in PDFs) */
    LONG x0 = pl + tx(d, cx - part->cx / 2), y0 = pt + tx(d, cy - part->cy / 2), x1 = x0 + tx(d, part->cx) - 1, y1 = y0 + tx(d, part->cy) - 1, k;
    SetAPen(rp, d->pen_shadow);
    for (k = x0; k <= x1; k += 3) { WritePixel(rp, k, y0); WritePixel(rp, k, y1); }
    for (k = y0; k <= y1; k += 3) { WritePixel(rp, x0, k); WritePixel(rp, x1, k); }
    return 0;
  }
  if (!f || src->kind != OBJ_SHAPE) return 0;
  if (ff_is_ink(src) && wp20_hidden()) return 1;
  left = ff_is_ink(src) ? dv_shown(f) : 0xFFFFFFFFUL;
  if (!left) return 1;
  t = *src; t.cx = part->cx; t.cy = part->cy; t.rot = part->rot; t.flip = part->flip;
  ox = pl + tx(d, cx - OBJ_W(&t) / 2); oy = pt + tx(d, cy - OBJ_H(&t) / 2);
  th = (int)tx(d, src->lwidth * 5 / 2);
  if (th < 1) th = 1;
  if (th > 32) th = 32;
  for (k = 0; k < f->np && left; k++) {
    const FfPath *p = &f->p[k];
    u32 n = p->n < left ? p->n : left;
    if (!n || !(xy = (LONG *)malloc(n * 2 * sizeof(LONG)))) continue;
    left = left == 0xFFFFFFFFUL ? left : left - n;
    for (i = 0; i < n; i++) {
      i32 u, v, X, Y;
      ff_point(f, t.cx, t.cy, k, i, &u, &v);
      obj_point(&t, u, v, &X, &Y);
      xy[2 * i] = ox * 16 + (LONG)((long)X * 16 * d->zoom / 2000); xy[2 * i + 1] = oy * 16 + (LONG)((long)Y * 16 * d->zoom / 2000);
    }
    if (p->closed && n == p->n && f->tool == INK_NONE && src->fill != COLOR_AUTO && n >= 3) {
      SetAPen(rp, pen_bg(d, obj, src->fill == 0 ? 0x010101 : src->fill));
      fill_poly(rp, xy, (int)n);
    }
    if (src->lstyle != PB_NONE && src->lwidth) {
      SetAPen(rp, pen_for(d, obj, src->lcolor));
      for (i = 0; i + 1 < n + (p->closed && n == p->n ? 1 : 0); i++) thick_line(rp, xy[2 * i], xy[2 * i + 1], xy[2 * ((i + 1) % n)], xy[2 * ((i + 1) % n) + 1], th);
      if (th > 2 || n == 1)       /* round the joins (and a dot) */
        for (i = 0; i < n; i++) RectFill(rp, (xy[2 * i] >> 4) - (th - 1) / 2, (xy[2 * i + 1] >> 4) - (th - 1) / 2, (xy[2 * i] >> 4) + th / 2, (xy[2 * i + 1] >> 4) + th / 2);
    }
    free(xy);
  }
  return 1;
}

/* ---- after an edit made here: laid out and drawn again, without taking the view to the caret */
static void dv_edited(struct Data *d, Object *obj, int keepchosen)
{
  if (!keepchosen) d->nmsel = 0;
  relayout(d, obj);
  d->words_stale = 1; d->idle = 0;
  if (!d->changed) { d->changed = 1; set(obj, MUIA_Quill_Changed, TRUE); }
  d->caret = pos_clamp(d->doc, d->caret);
  d->anchor = pos_clamp(d->doc, d->anchor);
  update_toolbar(d);
  d->back_ok = 0;
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
/* the caret and selection kept on their text when 4 bytes go in (delta 4) or out (-4) at para, off */
static void dv_shift(struct Data *d, u32 para, u32 off, int delta)
{
  Pos *q[2];
  int i;
  q[0] = &d->caret; q[1] = &d->anchor;
  for (i = 0; i < 2; i++) {
    if (q[i]->para != para) continue;
    if (delta > 0 && q[i]->off >= off) q[i]->off += (u32)delta;
    else if (delta < 0 && q[i]->off >= off + 4) q[i]->off -= 4;
    else if (delta < 0 && q[i]->off > off) q[i]->off = off;
  }
}
/* the mouse's moves asked for while dragging (and the ticks, while replaying) */
static void dv_events(struct Data *d, Object *obj, ULONG add)
{
  DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
  d->ehn.ehn_Events |= add;
  DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
}
static void dv_add_point(i32 x, i32 y)
{
  if (dv.n >= FF_MAXPTS) return;
  if (dv.n == dv.cap) {
    u32 c = dv.cap ? dv.cap * 2 : 256;
    i32 *n = (i32 *)realloc(dv.pts, c * 2 * sizeof(i32));
    if (!n) return;
    dv.pts = n; dv.cap = c;
  }
  dv.pts[2 * dv.n] = x; dv.pts[2 * dv.n + 1] = y; dv.n++;
}

/* ---- a stroke finished: smoothed into ink (or the shape it looks like), on the page or in its canvas */
static void dv_stroke_done(struct Data *d, Object *obj)
{
  Doc *doc = d->doc;
  const DrawPen *pen = wp20_pen();
  i32 *q, x, y, tol = tw(d, 1) / 2;
  u32 m;
  Image im;
  if (!dv.n || !pen) return;
  if (!(q = (i32 *)malloc(FF_MAXPTS * 2 * sizeof(i32)))) return;
  m = ink_smooth(dv.pts, dv.n, q, FF_MAXPTS, tol > 3 ? tol : 3);
  if (!ink_make(&im, pen->kind, pen->color, pen->width, q, m, &x, &y)) { free(q); return; }
  free(q);
  im.ff->order = ink_next_order(doc);
  if (wp20_inkshape() && pen->kind != INK_HIGHLIGHTER) {
    Image s;
    i32 dx, dy;
    if (ink_to_shape(&im, &s, &dx, &dy)) { img_release(&im); im = s; x += dx; y += dy; }
  }
  if (dv.incanvas && dv.cat.para < doc->np && dv.cat.off + 4 <= doc->p[dv.cat.para]->len && IS_PIC(doc->p[dv.cat.para]->t + dv.cat.off)) {
    /* into the canvas, in its own space */
    const Image *c = &doc->images[pic_index(doc->p[dv.cat.para]->t + dv.cat.off)];
    Image ng;
    long cw = c->chw > 0 ? c->chw : c->cx, ch = c->chh > 0 ? c->chh : c->cy, bw = dv.cbox.w > 0 ? dv.cbox.w : 1, bh = dv.cbox.h > 0 ? dv.cbox.h : 1;
    i32 kx = (i32)((long long)(x - dv.cbox.x) * cw / bw), ky = (i32)((long long)(y - dv.cbox.y) * ch / bh);
    im.cx = (i32)((long long)im.cx * cw / bw); im.cy = (i32)((long long)im.cy * ch / bh);
    if (img_clone(&ng, c, 0)) {
      if (canvas_add(&ng, &im, kx, ky)) { doc_set_object(doc, dv.cat, &ng); dv_edited(d, obj, 0); }
      img_release(&ng);
    }
    img_release(&im);
    return;
  }
  {
    /* floating on the page, anchored to the line it was drawn by */
    Pos at;
    i32 yy = y + im.cy / 2;
    char t[8];
    int k;
    CharFmt f;
    if (yy < 0) yy = 0;
    at = lay_line_start(d->lay, lay_hit(d->lay, dv.page, 0, yy));
    im.hrel = POS_PAGE; im.vrel = POS_PAGE; im.halign = im.valign = PA_NONE; im.x = x; im.y = y;
    if (at.para < doc->np && (k = doc_add_object(doc, &im)) >= 0) {
      f = *para_fmt_at(doc->p[at.para], at.off);
      f.link = 0; f.rev = REV_NONE;
      pic_utf8(t, k);
      doc_begin(doc, d->caret);
      dv_shift(d, at.para, at.off, 4);
      doc_insert(doc, &at, t, 4, &f);
      doc_end(doc, d->caret);
      dv_edited(d, obj, 0);
    }
  }
  img_release(&im);
}

/* ---- the eraser: the ink under the point (twips on dv.page; mx, my on the screen) rubbed out, whole or the bit under it */
/* in object im (its box's top left at bx, by), the stroke under page point px, py: a kid's index (-1 im itself), or -2 */
static int dv_under(const Image *im, i32 bx, i32 by, i32 px, i32 py, i32 r)
{
  i32 u, v;
  int k;
  obj_unpoint(im, px - bx, py - by, &u, &v);
  if (ff_is_ink(im)) return ff_hit(im, u, v, r) ? -1 : -2;
  if (im->kind != OBJ_GROUP || im->cx <= 0 || im->cy <= 0) return -2;
  {
    long cw = im->chw > 0 ? im->chw : im->cx, ch = im->chh > 0 ? im->chh : im->cy;
    i32 gu = (i32)((long long)u * cw / im->cx), gv = (i32)((long long)v * ch / im->cy), gr = (i32)((long long)r * cw / im->cx);
    for (k = im->nkids; k-- > 0; ) {
      const Image *kid = &im->kids[k];
      i32 ku, kv;
      if (!ff_is_ink(kid)) continue;
      obj_unpoint(kid, gu - kid->x, gv - kid->y, &ku, &kv);
      if (ff_hit(kid, ku, kv, gr)) return k;
    }
  }
  return -2;
}
static int dv_erase(struct Data *d, Object *obj, i32 px, i32 py, LONG mx, LONG my)
{
  Doc *doc = d->doc;
  Layout *L = d->lay;
  i32 r = tw(d, 4);
  int point = wp20_tool() == DT_ERASEPT, img = -1, kid = -2, hit;
  u32 j;
  Pos at;
  FloatBox box;
  i32 bx = 0, by = 0;
  if (wp20_hidden()) return 0;
  for (j = L->nfloats; j-- > 0 && kid == -2; ) {
    FloatBox *f = &L->floats[j];
    LONG off;
    if (f->page != dv.page || px < f->x - r || px > f->x + f->w + r || py < f->y - r || py > f->y + f->h + r || f->img >= doc->nimages) continue;
    if ((kid = dv_under(&doc->images[f->img], f->x, f->y, px, py, r)) == -2) continue;
    if ((off = obj_ref(doc, f->para, f->img)) < 0) { kid = -2; continue; }
    img = f->img; at.para = f->para; at.off = (u32)off; bx = f->x; by = f->y;
  }
  if (kid == -2 && (img = obj_hit(d, obj, mx, my, &at, &box)) >= 0 && box.page == dv.page) {
    /* a canvas (or group) in the line */
    bx = box.x; by = box.y;
    kid = dv_under(&doc->images[img], bx, by, px, py, r);
  }
  if (kid == -2 || img < 0) return 0;
  if (kid == -1 && !point) {
    dv_shift(d, at.para, at.off, -4);
    { Pos e = at; e.off += 4; doc_delete(doc, at, e); }
  } else if (kid == -1) {
    Image t = doc->images[img];
    i32 u, v;
    Freeform *nf;
    obj_unpoint(&t, px - bx, py - by, &u, &v);
    nf = ff_erase(&t, u, v, r, &hit);
    if (!nf) { dv_shift(d, at.para, at.off, -4); { Pos e = at; e.off += 4; doc_delete(doc, at, e); } }
    else { t.ff = nf; doc_set_object(doc, at, &t); ff_unref(nf); }
  } else {
    /* in a canvas: the group again without that stroke (or with what is left of it) */
    Image ng;
    if (!img_clone(&ng, &doc->images[img], 0)) return 0;
    if (point) {
      Image *k = &ng.kids[kid];
      long cw = ng.chw > 0 ? ng.chw : ng.cx;
      i32 u, v, gu, gv, ku, kv;
      Freeform *nf;
      obj_unpoint(&ng, px - bx, py - by, &u, &v);
      gu = (i32)((long long)u * cw / ng.cx); gv = (i32)((long long)v * (ng.chh > 0 ? ng.chh : ng.cy) / ng.cy);
      obj_unpoint(k, gu - k->x, gv - k->y, &ku, &kv);
      nf = ff_erase(k, ku, kv, (i32)((long long)r * cw / ng.cx), &hit);
      ff_unref(k->ff); k->ff = nf;
    }
    if (!point || !ng.kids[kid].ff) {
      img_release(&ng.kids[kid]);
      memmove(ng.kids + kid, ng.kids + kid + 1, (size_t)(ng.nkids - kid - 1) * sizeof(Image));
      ng.nkids--;
    }
    doc_set_object(doc, at, &ng);
    img_release(&ng);
  }
  dv_edited(d, obj, 0);
  return 1;
}

/* ---- the lasso */
static void dv_xor_line(struct Data *d, Object *obj, LONG ax, LONG ay, LONG bx, LONG by)
{
  struct RastPort *rp = _rp(obj);
  APTR clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  SetDrMd(rp, COMPLEMENT);
  Move(rp, ax, ay); Draw(rp, bx, by);
  SetDrMd(rp, JAM1);
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static void dv_xor_frame(struct Data *d, Object *obj)
{
  LONG dx = dv.mx1 - dv.mx0, dy = dv.my1 - dv.my0;
  LONG x0 = dv_sx(d, obj, dv.page, dv.bx0) + dx, y0 = dv_sy(d, obj, dv.page, dv.by0) + dy, x1 = dv_sx(d, obj, dv.page, dv.bx1) + dx, y1 = dv_sy(d, obj, dv.page, dv.by1) + dy;
  dv_xor_line(d, obj, x0, y0, x1, y0); dv_xor_line(d, obj, x1, y0 + 1, x1, y1);
  dv_xor_line(d, obj, x1 - 1, y1, x0, y1); dv_xor_line(d, obj, x0, y1 - 1, x0, y0 + 1);
}
/* the floating objects inside the loop drawn chosen */
static void dv_lasso_done(struct Data *d, Object *obj)
{
  Doc *doc = d->doc;
  Layout *L = d->lay;
  u32 j;
  d->nmsel = 0;
  if (dv.n >= 3)
    for (j = 0; j < L->nfloats && d->nmsel < 16; j++) {
      FloatBox *f = &L->floats[j];
      LONG off;
      if (f->page != dv.page || f->img >= doc->nimages) continue;
      if (ff_is_ink(&doc->images[f->img]) && wp20_hidden()) continue;
      if (!ink_in_lasso(&doc->images[f->img], f->x, f->y, dv.pts, dv.n) || (off = obj_ref(doc, f->para, f->img)) < 0) continue;
      d->msel[d->nmsel].para = f->para; d->msel[d->nmsel].off = (u32)off; d->nmsel++;
    }
  caret_hide(d, obj);
  if (d->nmsel) { d->anchor = d->msel[0]; d->caret = d->anchor; d->caret.off += 4; }
  if (d->nmsel == 1) d->nmsel = 0;               /* one: simply selected */
  else d->anchor = d->caret;                     /* several: the dotted frames show them */
  d->back_ok = 0;
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
/* the box round the chosen ones on dv.page; 0 if none there */
static int dv_chosen_box(struct Data *d, i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
  Pos ps[16];
  int n = d->nmsel, i, any = 0;
  FloatBox b;
  if (n) memcpy(ps, d->msel, (size_t)n * sizeof(Pos));
  else if ((i = sel_object(d, &ps[0])) >= 0) n = 1;
  for (i = 0; i < n; i++) {
    if (ps[i].para >= d->doc->np || !IS_PIC(d->doc->p[ps[i].para]->t + ps[i].off)) continue;
    if (!obj_box(d, ps[i], pic_index(d->doc->p[ps[i].para]->t + ps[i].off), &b) || b.wrap == WRAP_INLINE || b.page != dv.page) continue;
    if (!any || b.x < *x0) *x0 = b.x;
    if (!any || b.y < *y0) *y0 = b.y;
    if (!any || b.x + b.w > *x1) *x1 = b.x + b.w;
    if (!any || b.y + b.h > *y1) *y1 = b.y + b.h;
    any = 1;
  }
  return any;
}
/* the chosen ones moved by dx, dy twips: one undo step */
static void dv_move_chosen(struct Data *d, Object *obj, i32 dx, i32 dy)
{
  Doc *doc = d->doc;
  Pos ps[16];
  int n = d->nmsel, i, keep = d->nmsel;
  if (n) memcpy(ps, d->msel, (size_t)n * sizeof(Pos));
  else if (sel_object(d, &ps[0]) >= 0) n = 1;
  doc_begin(doc, d->caret);
  for (i = 0; i < n; i++) {
    FloatBox b;
    Image m;
    int k;
    if (ps[i].para >= doc->np || !IS_PIC(doc->p[ps[i].para]->t + ps[i].off)) continue;
    k = pic_index(doc->p[ps[i].para]->t + ps[i].off);
    if (!obj_box(d, ps[i], k, &b) || b.wrap == WRAP_INLINE) continue;
    m = doc->images[k];
    lay_float_moved(d->lay, &b, &m, b.x + dx, b.y + dy);
    doc_set_object(doc, ps[i], &m);
  }
  doc_end(doc, d->caret);
  dv_edited(d, obj, 1);
  d->nmsel = keep;     /* (still chosen: their references stayed where they were) */
}

/* ---- Ink Replay */
static void dv_replay_stop(void)
{
  u32 i;
  for (i = 0; i < dv.nseq; i++) ff_unref(dv.seq[i]);
  free(dv.seq); dv.seq = NULL; dv.nseq = 0;
  dv.replay = 0;
}
static void dv_collect(const Image *im, Freeform ***seq, u32 *n, u32 *cap)
{
  int k;
  if (ff_is_ink(im)) {
    if (*n == *cap) {
      u32 c = *cap ? *cap * 2 : 64;
      Freeform **s = (Freeform **)realloc(*seq, c * sizeof(Freeform *));
      if (!s) return;
      *seq = s; *cap = c;
    }
    ff_ref(im->ff);
    (*seq)[(*n)++] = im->ff;
  }
  for (k = 0; k < im->nkids; k++) dv_collect(&im->kids[k], seq, n, cap);
}
void dv_replay(Object *view)
{
  struct Data *d = INST_DATA(cls->mcc_Class, view);
  Doc *doc = d->doc;
  u32 i, k, cap = 0;
  if (!doc) return;
  dv_replay_stop();
  for (i = 0; i < doc->np; i++)
    for (k = 0; k + 4 <= doc->p[i]->len; k++) if (IS_PIC(doc->p[i]->t + k)) dv_collect(&doc->images[pic_index(doc->p[i]->t + k)], &dv.seq, &dv.nseq, &cap);
  if (!dv.nseq) { DisplayBeep(NULL); return; }
  /* by drawing order (strokes from files that don't record it first, in the document's order) */
  for (i = 1; i < dv.nseq; i++)
    for (k = i; k > 0 && dv.seq[k - 1]->order > dv.seq[k]->order; k--) { Freeform *t = dv.seq[k]; dv.seq[k] = dv.seq[k - 1]; dv.seq[k - 1] = t; }
  dv.replay = 1; dv.robj = view; dv.cur = 0; dv.upto = 0;
  dv_events(d, view, IDCMP_INTUITICKS);
  d->back_ok = 0;
  MUI_Redraw(view, MADF_DRAWOBJECT);
}
static void dv_replay_tick(struct Data *d, Object *obj)
{
  dv.upto += 6;
  while (dv.cur < dv.nseq && dv.upto >= ff_total(dv.seq[dv.cur])) { dv.cur++; dv.upto = 0; break; }
  if (dv.cur >= dv.nseq) {
    dv_replay_stop();
    if (!d->dragging) { DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn); d->ehn.ehn_Events &= ~IDCMP_INTUITICKS; DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn); }
    wp_refresh();
  }
  d->back_ok = 0;
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}

/* ---- for wp_draw.c */
int dv_chosen(Object *view, Pos *out, int max)
{
  struct Data *d = INST_DATA(cls->mcc_Class, view);
  int n = 0;
  Pos at;
  if (d->nmsel) { for (n = 0; n < d->nmsel && n < max; n++) out[n] = d->msel[n]; return n; }
  if (max > 0 && sel_object(d, &at) >= 0) { out[0] = at; return 1; }
  return 0;
}
void dv_unchoose(Object *view)
{
  struct Data *d = INST_DATA(cls->mcc_Class, view);
  d->nmsel = 0;
}

/* ---- the mouse on the page while a Draw tool is in use: 1 if it was used here */
static ULONG dv_mouse(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  int tool = wp20_tool();
  if (im->Class == IDCMP_INTUITICKS && dv.replay && dv.robj == obj) { dv_replay_tick(d, obj); return d->dragging ? 0 : MUI_EventHandlerRC_Eat; }
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && dv.replay && dv.robj == obj) { dv_replay_stop(); d->back_ok = 0; MUI_Redraw(obj, MADF_DRAWOBJECT); wp_refresh(); return MUI_EventHandlerRC_Eat; }
  if (dv.drag && dv.obj == obj) {
    LONG mx = im->MouseX, my = im->MouseY;
    if (im->Class == IDCMP_MOUSEMOVE) {
      i32 px = dv_px(d, obj, dv.page, mx), py = dv_py(d, obj, dv.page, my);
      if (mx == dv.lx && my == dv.ly) return MUI_EventHandlerRC_Eat;
      if (dv.drag == DG_INK) {
        /* the stroke shown as it goes */
        const DrawPen *pen = wp20_pen();
        struct RastPort *rp = _rp(obj);
        int th = pen ? (int)tx(d, pen->width * 5 / 2) : 1;
        APTR clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
        if (th < 1) th = 1;
        if (th > 32) th = 32;
        SetDrMd(rp, JAM1);
        SetAPen(rp, pen_for(d, obj, pen ? pen->color : 0));
        thick_line(rp, dv.lx * 16 + 8, dv.ly * 16 + 8, mx * 16 + 8, my * 16 + 8, th);
        if (th > 2) RectFill(rp, mx - (th - 1) / 2, my - (th - 1) / 2, mx + th / 2, my + th / 2);
        MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
        dv_add_point(px, py);
      } else if (dv.drag == DG_LASSO) {
        dv_xor_line(d, obj, dv.lx, dv.ly, mx, my);
        dv_add_point(px, py);
      } else if (dv.drag == DG_ERASE) dv_erase(d, obj, px, py, mx, my);
      else if (dv.drag == DG_MOVE) { if (dv.shown) dv_xor_frame(d, obj); dv.mx1 = mx; dv.my1 = my; dv_xor_frame(d, obj); dv.shown = 1; }
      dv.lx = mx; dv.ly = my;
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP) {
      int drag = dv.drag;
      dv.drag = DG_NONE;
      if (drag == DG_INK) dv_stroke_done(d, obj);
      else if (drag == DG_ERASE) { doc_end(d->doc, d->caret); }
      else if (drag == DG_LASSO) dv_lasso_done(d, obj);
      else if (drag == DG_MOVE) {
        if (dv.shown) dv_xor_frame(d, obj);
        if (dv.mx1 != dv.mx0 || dv.my1 != dv.my0) dv_move_chosen(d, obj, tw(d, dv.mx1 - dv.mx0), tw(d, dv.my1 - dv.my0));
      }
      dv.n = 0;
      return MUI_EventHandlerRC_Eat;
    }
    return 0;
  }
  if (tool == DT_NONE || im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
  if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
  if (!paged(d) || d->doc->edit.kind || !d->lay || !d->lay->npages) { DisplayBeep(NULL); return MUI_EventHandlerRC_Eat; }   /* (drawing is on pages: Print Layout) */
  {
    i32 px, py;
    dv.page = point_page(d, obj, im->MouseX, im->MouseY, &px, &py);
    dv.obj = obj; dv.n = 0; dv.lx = im->MouseX; dv.ly = im->MouseY; dv.incanvas = 0; dv.shown = 0;
    caret_hide(d, obj);
    if (tool == DT_PEN) {
      Pos at;
      FloatBox b;
      int k = obj_hit(d, obj, im->MouseX, im->MouseY, &at, &b);
      if (k >= 0 && d->doc->images[k].canvas == CANVAS_GROUP && b.page == dv.page) { dv.incanvas = 1; dv.cat = at; dv.cbox = b; }
      dv.drag = DG_INK;
      dv_add_point(px, py);
    } else if (tool == DT_ERASER || tool == DT_ERASEPT) {
      dv.drag = DG_ERASE;
      doc_begin(d->doc, d->caret);
      dv_erase(d, obj, px, py, im->MouseX, im->MouseY);
    } else if (tool == DT_LASSO) {
      i32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      if (dv_chosen_box(d, &x0, &y0, &x1, &y1) && px >= x0 && px <= x1 && py >= y0 && py <= y1) {
        dv.drag = DG_MOVE; dv.mx0 = dv.mx1 = im->MouseX; dv.my0 = dv.my1 = im->MouseY;
        dv.bx0 = x0; dv.by0 = y0; dv.bx1 = x1; dv.by1 = y1;
      } else {
        d->nmsel = 0;
        dv.drag = DG_LASSO;
        dv_add_point(px, py);
      }
    }
    dv_events(d, obj, IDCMP_MOUSEMOVE);
    set(_win(obj), MUIA_Window_ActiveObject, (ULONG)obj);
  }
  return MUI_EventHandlerRC_Eat;
}
