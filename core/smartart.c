/*
 * smartart.c - see chart.h: a SmartArt's saved drawing (the dsp:drawing part Word writes beside the
 * diagram's data) read as shapes with their text, and laid out as primitives. Quill doesn't run
 * SmartArt layouts: the saved drawing is the picture, as in Word when it opens a file and as in
 * WordCraft (crates/docx/src/read/diagram.rs, whose behaviour this follows: groups' child spaces
 * mapped, the drawing scaled to fit its frame but never enlarged).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "chart.h"
#include "layout.h"

#define EMU_PX 9525L              /* EMU in a pixel at 96 dpi */
#define MAXSHAPES 2000
#define MAXDEPTH 8

/* the presets Quill draws (doc.h's SH_ order), and a few look-alikes SmartArt layouts use */
static const char *const presets[SH_N] = { "rect", "roundRect", "ellipse", "line", "straightConnector1", "triangle", "diamond", "rightArrow", "star5",
  "pentagon", "hexagon", "octagon", "parallelogram", "trapezoid", "rtTriangle", "leftArrow", "upArrow", "downArrow",
  "plus", "heart", "wedgeRectCallout", "chevron", "homePlate", "star4" };
static int preset(const char *p)
{
  static const struct { const char *n; u8 sh; } alias[] = {
    { "flowChartProcess", SH_RECT }, { "flowChartAlternateProcess", SH_ROUNDRECT }, { "snip1Rect", SH_RECT }, { "round2SameRect", SH_ROUNDRECT },
    { "round1Rect", SH_ROUNDRECT }, { "flowChartConnector", SH_ELLIPSE }, { "donut", SH_ELLIPSE }, { "pie", SH_ELLIPSE }, { "blockArc", SH_ELLIPSE },
    { "circularArrow", SH_ELLIPSE }, { "flowChartDecision", SH_DIAMOND }, { "notchedRightArrow", SH_RARROW }, { "stripedRightArrow", SH_RARROW },
    { "leftRightArrow", SH_RARROW }, { "curvedRightArrow", SH_RARROW }, { "isoscelesTriangle", SH_TRIANGLE }, { "wedgeRoundRectCallout", SH_CALLOUT },
    { "cube", SH_RECT }, { "can", SH_RECT }, { "upDownArrow", SH_UARROW }, { "mathPlus", SH_PLUS }, { "line", SH_LINE },
    { "bentConnector3", SH_LINE }, { "curvedConnector3", SH_LINE }, { "straightConnector1", SH_LINE }
  };
  size_t i;
  if (!p) return SH_RECT;
  for (i = 0; i < SH_N; i++) if (!strcmp(p, presets[i])) return (int)i;
  for (i = 0; i < sizeof alias / sizeof alias[0]; i++) if (!strcmp(p, alias[i].n)) return alias[i].sh;
  return SH_RECT;
}
void smartart_free(SmartArt *a)
{
  int i;
  if (!a) return;
  for (i = 0; i < a->n; i++) free(a->s[i].text);
  free(a->s);
  free(a);
}
static int attr_l(Xml *x, const char *n, long *v)
{
  const char *s = xml_attr(x, n);
  if (!s) return 0;
  *v = atol(s);
  return 1;
}
/* a group's child space onto its parent's: x' = kx x + ox (and y) */
typedef struct { double kx, ox, ky, oy; } Xf;

/* one shape being read: what its spPr, style, txBody say */
typedef struct {
  SAShape s;
  int insp, inln, instyle, intx, inxfrm, intxx, inrun, int_, ingrad;
  int fillset, lineset, fillref, lnref, tcolset, szset, xfrmdone;
  u32 rfill, rline, rfont;
  long ins[4];
  Buf text;
  int para;
} Rd;

SmartArt *smartart_read(const char *xml, size_t len, const u32 *theme)
{
  Xml x;
  SmartArt *a;
  Xf xf[MAXDEPTH + 1];
  int depth = 0, grpdepth[MAXDEPTH + 1], ingrpsp = 0, inspdepth = -1;
  long goff[2] = { 0, 0 }, gext[2] = { 0, 0 }, gchoff[2] = { 0, 0 }, gchext[2] = { 0, 0 };
  Rd r;
  if (!xml || chart_nofp || !(a = (SmartArt *)calloc(1, sizeof *a))) return NULL;
  memset(&r, 0, sizeof r);
  buf_init(&r.text);
  xf[0].kx = xf[0].ky = 1; xf[0].ox = xf[0].oy = 0;
  grpdepth[0] = -1;
  xml_init(&x, xml, len);
  while (xml_next(&x) != XT_EOF) {
    const char *n = x.name;
    long v;
    if (x.type == XT_START) {
      if (!strcmp(n, "grpSp") && inspdepth < 0) {
        /* a group inside the tree: its child space comes with its grpSpPr */
        if (depth < MAXDEPTH) { depth++; xf[depth] = xf[depth - 1]; grpdepth[depth] = x.depth; }
        continue;
      }
      if (!strcmp(n, "grpSpPr") && inspdepth < 0) { ingrpsp = 1; goff[0] = goff[1] = gext[0] = gext[1] = gchoff[0] = gchoff[1] = gchext[0] = gchext[1] = 0; continue; }
      if (ingrpsp) {
        if (!strcmp(n, "off")) { attr_l(&x, "x", &goff[0]); attr_l(&x, "y", &goff[1]); }
        else if (!strcmp(n, "ext")) { attr_l(&x, "cx", &gext[0]); attr_l(&x, "cy", &gext[1]); }
        else if (!strcmp(n, "chOff")) { attr_l(&x, "x", &gchoff[0]); attr_l(&x, "y", &gchoff[1]); }
        else if (!strcmp(n, "chExt")) { attr_l(&x, "cx", &gchext[0]); attr_l(&x, "cy", &gchext[1]); }
        continue;
      }
      if (!strcmp(n, "sp") && inspdepth < 0) {
        memset(&r.s, 0, sizeof r.s);
        r.insp = r.inln = r.instyle = r.intx = r.inxfrm = r.intxx = r.inrun = r.int_ = r.ingrad = 0;
        r.fillset = r.lineset = r.fillref = r.lnref = r.tcolset = r.szset = r.xfrmdone = 0;
        r.rfill = r.rline = r.rfont = 0;
        r.ins[0] = r.ins[2] = 91440; r.ins[1] = r.ins[3] = 45720;
        r.text.len = 0; if (r.text.data) r.text.data[0] = 0;
        r.para = 0;
        r.s.algn = 1; r.s.anchor = 1; r.s.lw = 12700;
        inspdepth = x.depth;
        continue;
      }
      if (inspdepth < 0) continue;
      /* inside a shape (its effects and 3-D settings aren't drawn) */
      if (!strcmp(n, "effectLst") || !strcmp(n, "effectDag") || !strcmp(n, "scene3d") || !strcmp(n, "sp3d") || !strcmp(n, "extLst")) { xml_skip(&x); continue; }
      if (!strcmp(n, "spPr")) r.insp = 1;
      else if (!strcmp(n, "style")) r.instyle = 1;
      else if (!strcmp(n, "txBody")) r.intx = 1;
      else if (!strcmp(n, "txXfrm")) r.intxx = 1;
      else if (!strcmp(n, "xfrm") && r.insp && !r.xfrmdone) {
        const char *fh = xml_attr(&x, "flipH"), *fv = xml_attr(&x, "flipV");
        if (attr_l(&x, "rot", &v)) { v = (v / 6000) % 3600; r.s.rot = (u16)(v < 0 ? v + 3600 : v); }
        if (fh && (*fh == '1' || *fh == 't')) r.s.flip |= 1;
        if (fv && (*fv == '1' || *fv == 't')) r.s.flip |= 2;
        r.inxfrm = 1;
      }
      else if (!strcmp(n, "off") && (r.inxfrm || r.intxx)) {
        long ox = 0, oy = 0;
        attr_l(&x, "x", &ox); attr_l(&x, "y", &oy);
        if (r.inxfrm) { r.s.x = (i32)(xf[depth].kx * ox + xf[depth].ox); r.s.y = (i32)(xf[depth].ky * oy + xf[depth].oy); }
        else { r.s.tx = (i32)(xf[depth].kx * ox + xf[depth].ox); r.s.ty = (i32)(xf[depth].ky * oy + xf[depth].oy); }
      }
      else if (!strcmp(n, "ext") && (r.inxfrm || r.intxx)) {
        long cx = 0, cy = 0;
        attr_l(&x, "cx", &cx); attr_l(&x, "cy", &cy);
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (r.inxfrm) { r.s.w = (i32)(xf[depth].kx * cx); r.s.h = (i32)(xf[depth].ky * cy); }
        else { r.s.tw = (i32)(xf[depth].kx * cx); r.s.th = (i32)(xf[depth].ky * cy); }
      }
      else if (!strcmp(n, "prstGeom") && r.insp) r.s.shape = (u8)preset(xml_attr(&x, "prst"));
      else if (!strcmp(n, "custGeom") && r.insp) r.s.shape = SH_RECT;
      else if (!strcmp(n, "ln") && r.insp) { r.inln = 1; if (attr_l(&x, "w", &v)) r.s.lw = (i32)v; }
      else if (!strcmp(n, "noFill") && r.insp) { if (r.inln) { r.s.noline = 1; r.lineset = 1; } else if (!r.intx) { r.s.nofill = 1; r.fillset = 1; } }
      else if (!strcmp(n, "gradFill") && r.insp && !r.inln) r.ingrad = 1;
      else if (!strcmp(n, "bodyPr")) {
        const char *an = xml_attr(&x, "anchor");
        if (an) r.s.anchor = (u8)(!strcmp(an, "t") ? 0 : !strcmp(an, "b") ? 2 : 1);
        attr_l(&x, "lIns", &r.ins[0]); attr_l(&x, "tIns", &r.ins[1]); attr_l(&x, "rIns", &r.ins[2]); attr_l(&x, "bIns", &r.ins[3]);
      }
      else if (!strcmp(n, "p") && r.intx) { if (r.para++) buf_addc(&r.text, '\n'); }
      else if ((!strcmp(n, "r") || !strcmp(n, "fld")) && r.intx) r.inrun = 1;
      else if (!strcmp(n, "t") && r.intx && r.inrun) r.int_ = 1;
      else if (!strcmp(n, "br") && r.intx) buf_addc(&r.text, '\n');
      else if (!strcmp(n, "pPr") && r.intx && r.para == 1) {
        const char *al = xml_attr(&x, "algn");
        if (al) r.s.algn = (u8)(!strcmp(al, "l") ? 0 : !strcmp(al, "r") ? 2 : 1);
      }
      else if ((!strcmp(n, "rPr") || !strcmp(n, "endParaRPr") || !strcmp(n, "defRPr")) && r.intx) {
        const char *b = xml_attr(&x, "b"), *i = xml_attr(&x, "i");
        if (!r.szset && attr_l(&x, "sz", &v) && v > 0 && v < 400000) { r.s.sz = (u16)(v > 65000 ? 65000 : v); r.szset = n[0] == 'r'; }
        if (n[0] == 'r') {
          if (b) r.s.bold = (u8)(*b == '1' || *b == 't');
          if (i) r.s.italic = (u8)(*i == '1' || *i == 't');
        }
      }
      else if ((!strcmp(n, "srgbClr") || !strcmp(n, "schemeClr") || !strcmp(n, "sysClr") || !strcmp(n, "prstClr"))) {
        /* whose colour it is: the text's, the line's, the fill's, or a style reference's */
        int intext = r.intx && r.inrun, inline_ = r.inln, insp = r.insp, ingrad = r.ingrad, instyle = r.instyle;
        u32 c = chart_dml_color(&x, theme);
        if (!c) continue;
        c &= 0xFFFFFF;
        if (intext) { if (!r.tcolset) { r.s.tcolor = c; r.tcolset = 1; } }
        else if (insp && inline_) { if (!r.lineset || !r.s.noline) { r.s.line = c; r.lineset = 1; r.s.noline = 0; } }
        else if (insp && ingrad) { if (!r.fillset) { r.s.fill = c; r.fillset = 1; } }
        else if (insp && !r.intx) { if (!r.fillset || r.s.nofill) { r.s.fill = c; r.fillset = 1; r.s.nofill = 0; } }
        else if (instyle) {
          if (r.lnref == 1) { r.rline = c | 0x01000000UL; r.lnref = 2; }
          else if (r.fillref == 1) { r.rfill = c | 0x01000000UL; r.fillref = 2; }
          else if (r.rfont == 1) r.rfont = c | 0x01000000UL;
        }
        continue;      /* (read to its END by chart_dml_color) */
      }
      else if (r.instyle && (!strcmp(n, "lnRef") || !strcmp(n, "fillRef") || !strcmp(n, "fontRef"))) {
        long idx = 1;
        attr_l(&x, "idx", &idx);
        if (n[0] == 'l') r.lnref = idx > 0 ? 1 : 3;
        else if (n[0] == 'f' && n[1] == 'i') r.fillref = idx > 0 ? 1 : 3;
        else r.rfont = 1;
      }
      continue;
    }
    if (x.type == XT_TEXT) {
      if (inspdepth >= 0 && r.intx && r.para) {
        /* only a:t's text counts (whitespace between elements comes as text too) */
        if (r.int_) buf_add(&r.text, x.text.data, x.text.len);
      }
      continue;
    }
    if (x.type == XT_END) {
      if (inspdepth >= 0) {
        if (!strcmp(n, "t")) r.int_ = 0;
        if (!strcmp(n, "spPr")) r.insp = 0;
        else if (!strcmp(n, "ln")) r.inln = 0;
        else if (!strcmp(n, "gradFill")) r.ingrad = 0;
        else if (!strcmp(n, "style")) r.instyle = 0;
        else if (!strcmp(n, "txBody")) r.intx = 0;
        else if (!strcmp(n, "txXfrm")) r.intxx = 0;
        else if (!strcmp(n, "xfrm") && r.inxfrm) { r.inxfrm = 0; r.xfrmdone = 1; }
        else if (!strcmp(n, "r") || !strcmp(n, "fld")) r.inrun = 0;
        else if (!strcmp(n, "lnRef") && r.lnref == 1) r.lnref = 3;
        else if (!strcmp(n, "fillRef") && r.fillref == 1) r.fillref = 3;
        if (x.depth < inspdepth) {
          /* the shape done: what its style gives where it gives nothing itself */
          SAShape *ns;
          inspdepth = -1;
          if (!r.fillset) { if ((r.rfill & 0x01000000UL)) r.s.fill = r.rfill & 0xFFFFFF; else r.s.nofill = 1; }
          if (!r.lineset) { if ((r.rline & 0x01000000UL)) r.s.line = r.rline & 0xFFFFFF; else r.s.noline = 1; }
          if (!r.tcolset) r.s.tcolor = (r.rfont & 0x01000000UL) ? r.rfont & 0xFFFFFF : 0;
          if (r.text.len) {
            /* trailing empty paragraphs left out */
            while (r.text.len && r.text.data[r.text.len - 1] == '\n') r.text.data[--r.text.len] = 0;
            r.s.text = r.text.len ? str_ndup(r.text.data, r.text.len) : NULL;
          }
          if (r.s.text && !r.s.tw) { r.s.tx = r.s.x; r.s.ty = r.s.y; r.s.tw = r.s.w; r.s.th = r.s.h; }
          if (r.s.text) {
            /* the text's insets, inside its box */
            long iw = r.ins[0] + r.ins[2], ih = r.ins[1] + r.ins[3];
            if (iw < r.s.tw) { r.s.tx += (i32)r.ins[0]; r.s.tw -= (i32)iw; }
            if (ih < r.s.th) { r.s.ty += (i32)r.ins[1]; r.s.th -= (i32)ih; }
          }
          if (a->n < MAXSHAPES && (r.s.w > 0 || r.s.h > 0) && (ns = (SAShape *)realloc(a->s, (size_t)(a->n + 1) * sizeof(SAShape)))) {
            a->s = ns;
            a->s[a->n++] = r.s;
            if (r.s.x + r.s.w > a->ext[0]) a->ext[0] = r.s.x + r.s.w;
            if (r.s.y + r.s.h > a->ext[1]) a->ext[1] = r.s.y + r.s.h;
          } else free(r.s.text);
          r.s.text = NULL;
        }
        continue;
      }
      if (!strcmp(n, "grpSpPr") && ingrpsp) {
        ingrpsp = 0;
        if (depth > 0) {
          /* the group's child space onto its parent's */
          Xf *p = &xf[depth - 1], *g = &xf[depth];
          double kx = gchext[0] > 0 && gext[0] >= 0 ? (double)gext[0] / gchext[0] : 1, ky = gchext[1] > 0 && gext[1] >= 0 ? (double)gext[1] / gchext[1] : 1;
          g->kx = p->kx * kx; g->ky = p->ky * ky;
          g->ox = p->kx * (goff[0] - gchoff[0] * kx) + p->ox;
          g->oy = p->ky * (goff[1] - gchoff[1] * ky) + p->oy;
        }
        continue;
      }
      if (!strcmp(n, "grpSp") && depth > 0 && x.depth <= grpdepth[depth]) depth--;
    }
  }
  xml_free(&x);
  buf_free(&r.text);
  if (!a->n) { smartart_free(a); return NULL; }
  return a;
}

/* ------------------------------------------------------------------ laying out */
static Prim *add_text(PrimList *l, const char *s, size_t n, i32 x, i32 y, int px, u32 rgb, int bold, int italic, int align, i32 w)
{
  Prim *p = prim_add(l, PR_TEXT, CE_NONE);
  if (!p) return NULL;
  if (!(p->text = str_ndup(s, n))) { l->n--; l->oom = 1; return NULL; }
  p->x = x; p->y = y; p->size = (u8)(px < 1 ? 1 : px > 255 ? 255 : px); p->bold = (u8)bold; p->italic = (u8)italic;
  p->align = (u8)align; p->valign = 0; p->line = PRIM_ON | rgb; p->w = w; p->h = px * 6 / 5;
  return p;
}
/* a shape's text wrapped into its text box (px: its pixels across, at scale k pixels an EMU) */
static void shape_text(const SAShape *s, double k, TextWidthFn tw, void *ctx, PrimList *l)
{
  int px = (int)((s->sz ? s->sz : 1800) * 96.0 / 7200.0 * k * EMU_PX + 0.5), lh, nl = 0, i;
  i32 bx = (i32)(s->tx * k), by = (i32)(s->ty * k), bw = (i32)(s->tw * k), bh = (i32)(s->th * k), y;
  struct { const char *p; size_t n; i32 w; } lines[64];
  const char *t = s->text;
  if (!t || bw < 2) return;
  if (px < 4) px = 4;
  lh = px * 6 / 5;
  /* lines: each paragraph broken at spaces where the box's width runs out */
  while (*t && nl < 64) {
    const char *e = t, *brk = NULL, *q;
    i32 w = 0;
    while (*e && *e != '\n') {
      size_t m = (size_t)(e - t) + 1;
      char buf[400];
      if (*e == ' ') brk = e;
      if (m >= sizeof buf) break;
      memcpy(buf, t, m); buf[m] = 0;
      if (tw && tw(ctx, buf, px, s->bold) > bw && e > t) break;
      e++;
    }
    if (*e && *e != '\n' && brk && brk > t) e = brk;
    q = e;
    {
      char buf[400];
      size_t m = (size_t)(q - t) < sizeof buf - 1 ? (size_t)(q - t) : sizeof buf - 1;
      memcpy(buf, t, m); buf[m] = 0;
      w = tw ? tw(ctx, buf, px, s->bold) : (i32)m * px / 2;
    }
    lines[nl].p = t; lines[nl].n = (size_t)(q - t); lines[nl].w = w; nl++;
    t = q;
    if (*t == ' ') t++;
    else if (*t == '\n') t++;
  }
  y = s->anchor == 0 ? by : s->anchor == 2 ? by + bh - nl * lh : by + (bh - nl * lh) / 2;
  for (i = 0; i < nl; i++, y += lh) {
    i32 x = s->algn == 0 ? bx : s->algn == 2 ? bx + bw : bx + bw / 2;
    if (lines[i].n) add_text(l, lines[i].p, lines[i].n, x, y + (lh - px) / 2, px, s->tcolor, s->bold, s->italic, s->algn, lines[i].w);
  }
}
int smartart_render(const SmartArt *a, int w, int h, TextWidthFn tw, void *ctx, PrimList *out)
{
  double fit = 1, k;
  int i, j;
  if (!a || w < 2 || h < 2) return 1;
  /* one scale for both directions, never above 1:1 */
  if (a->ext[0] > 0 && (double)w * EMU_PX / a->ext[0] < fit) fit = (double)w * EMU_PX / a->ext[0];
  if (a->ext[1] > 0 && (double)h * EMU_PX / a->ext[1] < fit) fit = (double)h * EMU_PX / a->ext[1];
  k = fit / EMU_PX;
  for (i = 0; i < a->n; i++) {
    const SAShape *s = &a->s[i];
    i32 xy[2 * SHAPE_MAXPTS], sw = (i32)(s->w * k + 0.5), sh = (i32)(s->h * k + 0.5), x0 = (i32)(s->x * k + 0.5), y0 = (i32)(s->y * k + 0.5);
    int closed;
    u32 n;
    Prim *p;
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (!s->nofill || !s->noline) {
      n = shape_points(s->shape, sw * 16, sh * 16, xy, SHAPE_MAXPTS, &closed);    /* (in sixteenths, for smooth corners) */
      if (n >= 2 && (p = prim_add(out, closed ? PR_POLY : PR_LINE, CE_NONE))) {
        i32 c = icos(s->rot), sn = isin(s->rot);
        if (!(p->pts = (i32 *)malloc((size_t)n * 2 * sizeof(i32)))) { out->n--; out->oom = 1; return 0; }
        for (j = 0; j < (int)n; j++) {
          /* about the shape's centre: flipped, then turned clockwise */
          long u = xy[2 * j] - sw * 8, v = xy[2 * j + 1] - sh * 8;
          if (s->flip & 1) u = -u;
          if (s->flip & 2) v = -v;
          if (s->rot) { long ru = (u * c - v * sn) / TRIG_ONE, rv = (u * sn + v * c) / TRIG_ONE; u = ru; v = rv; }
          p->pts[2 * j] = x0 + sw / 2 + (i32)((u + (u < 0 ? -8 : 8)) / 16);
          p->pts[2 * j + 1] = y0 + sh / 2 + (i32)((v + (v < 0 ? -8 : 8)) / 16);
        }
        p->np = (int)n;
        p->fill = closed && !s->nofill ? PRIM_ON | s->fill : 0;
        p->line = !s->noline ? PRIM_ON | s->line : 0;
        p->width = (u8)(s->lw * k > 1.5 ? (s->lw * k > 20 ? 20 : (int)(s->lw * k + 0.5)) : 1);
      }
    }
    if (s->text) shape_text(s, k, tw, ctx, out);
  }
  return !out->oom;
}
