/*
 * wp_primview.c - see wp_primview.h (WP15b and WP15c). The painting follows Ledger's chartview.c
 * (same author), with integer arithmetic only.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "wp_primview.h"
#include "theme.h"

/* ------------------------------------------------------------------ text */
static struct { int px, bold; struct TextFont *tf; } fc[16];
static int nfc;
static struct TextFont *font_for(int px, int bold)
{
  int i;
  UWORD ys;
  if (px < 6) px = 6;
  if (px > 60) px = 60;
  for (i = 0; i < nfc; i++) if (fc[i].px == px && fc[i].bold == bold) return fc[i].tf ? fc[i].tf : theme_font();
  if (nfc >= 16) { for (i = 0; i < nfc; i++) if (fc[i].tf) theme_close_font(fc[i].tf); nfc = 0; }
  ys = (UWORD)((px * 11 + 5) / 10);
  fc[nfc].px = px; fc[nfc].bold = bold;
  fc[nfc].tf = theme_open_font(bold ? "QuillSansB.font" : "QuillSans.font", ys);
  nfc++;
  return fc[nfc - 1].tf ? fc[nfc - 1].tf : theme_font();
}
static void latin(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? (u32)strlen(u) : 0, out, cap); }
i32 pv_text_width(void *ctx, const char *utf8, int px, int bold)
{
  /* (ctx unused: one font for every preview) */
  struct RastPort rp;
  char t[300];
  latin(utf8, t, sizeof t);
  InitRastPort(&rp);
  SetFont(&rp, font_for(px, bold));
  return (i32)TextLength(&rp, (STRPTR)t, (ULONG)strlen(t));
}
/* text turned to read upwards, its top left corner at x, y (through a one-plane bitmap) */
static void text_up(struct RastPort *rp, struct TextFont *tf, const char *t, LONG x, LONG y, LONG pen)
{
  struct RastPort trp;
  struct BitMap *bm;
  LONG w, h, i, j;
  InitRastPort(&trp);
  SetFont(&trp, tf);
  w = TextLength(&trp, (STRPTR)t, (ULONG)strlen(t));
  h = tf->tf_YSize;
  if (w <= 0 || w > 1000 || !(bm = AllocBitMap((ULONG)w, (ULONG)h, 1, BMF_CLEAR, NULL))) return;
  trp.BitMap = bm;
  SetAPen(&trp, 1); SetDrMd(&trp, JAM1);
  Move(&trp, 0, tf->tf_Baseline);
  Text(&trp, (STRPTR)t, (ULONG)strlen(t));
  SetAPen(rp, pen);
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++)
      if (ReadPixel(&trp, i, j)) WritePixel(rp, x + j, y + w - 1 - i);
  FreeBitMap(bm);
}

/* ------------------------------------------------------------------ filling */
/* a polygon (xy: n points) filled row by row, offset by ox, oy (crossings in sixteenths of a pixel) */
static void fill_poly(struct RastPort *rp, const i32 *xy, int n, LONG ox, LONG oy)
{
  LONG y0 = 0x7FFFFFF, y1 = -0x7FFFFFF, y, *xs;
  int i, j, m;
  if (n < 3) return;
  for (i = 0; i < n; i++) { if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1]; if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1]; }
  if (y1 - y0 > 4000 || !(xs = (LONG *)malloc((size_t)(n + 1) * sizeof(LONG)))) return;
  for (y = y0; y <= y1; y++) {
    LONG sy = y * 16 + 8;
    for (i = 0, m = 0; i < n; i++) {
      LONG ax = xy[2 * i] * 16, ay = xy[2 * i + 1] * 16, bx = xy[2 * ((i + 1) % n)] * 16, by = xy[2 * ((i + 1) % n) + 1] * 16;
      if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs[m++] = (ax + (sy - ay) * (bx - ax) / (by - ay) + 8) >> 4;
    }
    for (i = 1; i < m; i++) { LONG t = xs[i]; for (j = i - 1; j >= 0 && xs[j] > t; j--) xs[j + 1] = xs[j]; xs[j + 1] = t; }
    for (i = 0; i + 1 < m; i += 2) if (xs[i + 1] > xs[i]) RectFill(rp, ox + xs[i], oy + y, ox + xs[i + 1] - 1, oy + y);
  }
  free(xs);
}
static LONG isqrt(LONG v) { LONG r = 0, b = 1L << 30; if (v <= 0) return 0; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static void fill_ellipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry)
{
  LONG dy;
  if (rx < 1 || ry < 1) { RectFill(rp, cx - rx, cy - ry, cx + rx, cy + ry); return; }
  for (dy = -ry; dy <= ry; dy++) {
    /* dx = rx sqrt(1 - dy^2/ry^2) */
    LONG dx = isqrt((ry * ry - dy * dy) * 256 / (ry * ry) * 256) * rx / 256;
    RectFill(rp, cx - dx, cy + dy, cx + dx, cy + dy);
  }
}
static void thick(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, int w)
{
  int i;
  LONG adx = x1 > x0 ? x1 - x0 : x0 - x1, ady = y1 > y0 ? y1 - y0 : y0 - y1;
  if (w <= 1) { Move(rp, x0, y0); Draw(rp, x1, y1); return; }
  for (i = -w / 2; i < w - w / 2; i++) {
    if (adx >= ady) { Move(rp, x0, y0 + i); Draw(rp, x1, y1 + i); }
    else { Move(rp, x0 + i, y0); Draw(rp, x1 + i, y1); }
  }
}
void pv_paint(struct RastPort *rp, const PrimList *pl, LONG x, LONG y, PvPen pen, void *ctx)
{
  int i, k;
  SetDrMd(rp, JAM1);
  for (i = 0; i < pl->n; i++) {
    const Prim *p = &pl->p[i];
    switch (p->t) {
    case PR_RECT:
      if (p->fill) { SetAPen(rp, pen(ctx, p->fill)); RectFill(rp, x + p->x, y + p->y, x + p->x + (p->w > 0 ? p->w - 1 : 0), y + p->y + (p->h > 0 ? p->h - 1 : 0)); }
      if (p->line) {
        LONG a = x + p->x, b = y + p->y, c = a + p->w - 1, d = b + p->h - 1;
        SetAPen(rp, pen(ctx, p->line));
        Move(rp, a, b); Draw(rp, c, b); Draw(rp, c, d); Draw(rp, a, d); Draw(rp, a, b);
      }
      break;
    case PR_LINE:
      if (!p->line) break;
      SetAPen(rp, pen(ctx, p->line));
      for (k = 0; k + 1 < p->np; k++) thick(rp, x + p->pts[2 * k], y + p->pts[2 * k + 1], x + p->pts[2 * k + 2], y + p->pts[2 * k + 3], p->width);
      break;
    case PR_POLY:
      if (p->fill) { SetAPen(rp, pen(ctx, p->fill)); fill_poly(rp, p->pts, p->np, x, y); }
      if (p->line && p->np > 1) {
        SetAPen(rp, pen(ctx, p->line));
        Move(rp, x + p->pts[0], y + p->pts[1]);
        for (k = 1; k <= p->np; k++) Draw(rp, x + p->pts[2 * (k % p->np)], y + p->pts[2 * (k % p->np) + 1]);
      }
      break;
    case PR_ELLIPSE: {
      LONG rx = p->w / 2, ry = p->h / 2, cx = x + p->x + rx, cy = y + p->y + ry;
      if (p->fill) { SetAPen(rp, pen(ctx, p->fill)); fill_ellipse(rp, cx, cy, rx, ry); }
      if (p->line) { SetAPen(rp, pen(ctx, p->line)); DrawEllipse(rp, cx, cy, rx > 0 ? rx : 1, ry > 0 ? ry : 1); }
      break; }
    case PR_TEXT: {
      struct TextFont *tf = font_for(p->size, p->bold);
      char t[300];
      LONG w, h, tx, ty;
      if (!p->text) break;
      latin(p->text, t, sizeof t);
      SetFont(rp, tf);
      w = TextLength(rp, (STRPTR)t, (ULONG)strlen(t)); h = tf->tf_YSize;
      if (p->vert) {
        tx = x + p->x - (p->align == 1 ? h / 2 : p->align == 2 ? h : 0);
        ty = y + p->y - (p->valign == 1 ? w / 2 : p->valign == 2 ? w : 0);
        text_up(rp, tf, t, tx, ty, pen(ctx, p->line));
        break;
      }
      tx = x + p->x - (p->align == 1 ? w / 2 : p->align == 2 ? w : 0);
      ty = y + p->y - (p->valign == 1 ? h / 2 : p->valign == 2 ? h : 0);
      SetAPen(rp, pen(ctx, p->line));
      Move(rp, tx, ty + tf->tf_Baseline);
      Text(rp, (STRPTR)t, (ULONG)strlen(t));
      break; }
    }
  }
}

/* ------------------------------------------------------------------ the class */
struct PVData {
  Chart *c; SAModel *m; char text[160];
  struct { ULONG rgb; LONG pen; } pens[48]; int npens;
  struct ColorMap *cm;
  int minw, minh;
};
static struct MUI_CustomClass *pvcls;
static LONG pv_pen(void *ctx, ULONG rgb)
{
  struct PVData *d = (struct PVData *)ctx;
  int i;
  rgb &= 0xFFFFFF;
  for (i = 0; i < d->npens; i++) if (d->pens[i].rgb == rgb) return d->pens[i].pen;
  if (d->npens >= 48 || !d->cm) return 1;
  d->pens[d->npens].rgb = rgb;
  d->pens[d->npens].pen = ObtainBestPen(d->cm, ((rgb >> 16) & 255) * 0x01010101UL, ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
  return d->pens[d->npens++].pen;
}
static void pv_set(struct PVData *d, struct TagItem *tags, Object *obj, int redraw)
{
  struct TagItem *t;
  int changed = 0;
  while ((t = NextTagItem(&tags))) {
    if (t->ti_Tag == MUIA_PrimView_Chart) { chart_free(d->c); d->c = (Chart *)t->ti_Data; changed = 1; }
    else if (t->ti_Tag == MUIA_PrimView_Art) { sa_free(d->m); d->m = (SAModel *)t->ti_Data; changed = 1; }
    else if (t->ti_Tag == MUIA_PrimView_Text) { snprintf(d->text, sizeof d->text, "%s", t->ti_Data ? (const char *)t->ti_Data : ""); changed = 1; }
  }
  if (changed && redraw) MUI_Redraw(obj, MADF_DRAWOBJECT);
}
static ULONG pv_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PVData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->minw = 200; d->minh = 120;
    pv_set(d, ((struct opSet *)msg)->ops_AttrList, obj, 0);
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    chart_free(d->c); d->c = NULL;
    sa_free(d->m); d->m = NULL;
    break;
  case OM_SET:
    d = INST_DATA(cl, obj);
    pv_set(d, ((struct opSet *)msg)->ops_AttrList, obj, 1);
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    d = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += d->minw; m->MinMaxInfo->DefWidth += d->minw * 5 / 4; m->MinMaxInfo->MaxWidth += d->minw * 2;
    m->MinMaxInfo->MinHeight += d->minh; m->MinMaxInfo->DefHeight += d->minh * 5 / 4; m->MinMaxInfo->MaxHeight += d->minh * 2;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    d->cm = _screen(obj)->ViewPort.ColorMap;
    return TRUE;
  case MUIM_Cleanup: {
    int i;
    d = INST_DATA(cl, obj);
    for (i = 0; i < d->npens; i++) ReleasePen(d->cm, d->pens[i].pen);
    d->npens = 0;
    break;
  }
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
      struct RastPort *rp = _rp(obj);
      PrimList pl;
      int w = _mwidth(obj), h = _mheight(obj);
      prims_init(&pl);
      SetAPen(rp, pv_pen(d, 0xFFFFFF));
      RectFill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
      if (d->c) chart_render(d->c, w, h, pv_text_width, NULL, &pl);
      else if (d->m) {
        Buf b;
        SmartArt *a;
        buf_init(&b);
        if (sa_drawing_xml(d->m, (i32)(w - 8) * 15, (i32)(h - 8) * 15, &b) && (a = smartart_read(b.data, b.len, NULL))) {
          smartart_render(a, w - 8, h - 8, pv_text_width, NULL, &pl);
          smartart_free(a);
          {
            int i;
            for (i = 0; i < pl.n; i++) {     /* (a margin of 4 pixels) */
              int k;
              pl.p[i].x += 4; pl.p[i].y += 4;
              for (k = 0; k < pl.p[i].np; k++) { pl.p[i].pts[2 * k] += 4; pl.p[i].pts[2 * k + 1] += 4; }
            }
          }
        }
        buf_free(&b);
      }
      if (pl.n) {
        pv_paint(rp, &pl, _mleft(obj), _mtop(obj), pv_pen, d);
      } else if (d->text[0]) theme_text(rp, theme_font(), _mleft(obj) + 8, _mtop(obj) + 8, d->text, pv_pen(d, 0x595959));
      prims_free(&pl);
    }
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
Object *primview_new(int minw, int minh)
{
  Object *o;
  if (!pvcls) pvcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PVData), (APTR)pv_dispatch);
  if (!pvcls) return NULL;
  o = NewObject(pvcls->mcc_Class, NULL, TextFrame, MUIA_FillArea, FALSE, TAG_DONE);
  if (o) { struct PVData *d = INST_DATA(pvcls->mcc_Class, o); d->minw = minw; d->minh = minh; }
  return o;
}
void primview_cleanup(void)
{
  int i;
  for (i = 0; i < nfc; i++) if (fc[i].tf) theme_close_font(fc[i].tf);
  nfc = 0;
  if (pvcls) { MUI_DeleteCustomClass(pvcls); pvcls = NULL; }
}
