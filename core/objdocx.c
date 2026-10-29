/*
 * objdocx.c - see objfx.h: pictures' adjustments and objects' fills, effects and text settings in
 * .docx (WP17, WP18), and the pictures' files as a .docx holds them.
 *
 * Read and written: a:blip's a:lum, a:grayscl, a:biLevel, a:duotone, a:alphaModFix, a:clrChange
 * (which Word and LibreOffice draw themselves), and Word 2010's a14:imgProps (artistic effects,
 * sharpen/soften, colour temperature, saturation, background removal), which other programs don't
 * draw: those are drawn into the picture saved (as Word does), the unchanged picture kept as
 * a14:imgLayer so they can be changed again. A file whose a14:imgLayer Quill can't read (Word's HD
 * Photo .wdp) keeps its a14:imgProps verbatim, the picture shown as it is. Shapes: a:gradFill, a
 * shape's a:blipFill, a:effectLst (a:outerShdw, a:glow, a:softEdge), wps:bodyPr's vert, anchor
 * and insets, wps:txbx's id and wps:linkedTxbx, the preset geometries of shapex.c and icons
 * (a:custGeom named in wp:docPr).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "objfx.h"
#include "xml.h"
#include "image.h"

static const char *const art_names[ART_N] = { "", "artisticBlur", "artisticPencilGrayscale", "artisticLineDrawing", "artisticPhotocopy",
                                              "artisticCutout", "artisticFilmGrain", "artisticGlowEdges", "artisticMosaicBubbles" };
#define A14_URI "{BEBA8EAE-BF5A-486C-A8C5-ECC9F3942E4B}"
#define SEPIA_DARK 0x2B1D0E
#define SEPIA_LIGHT 0xF2E2C6

static int anum(Xml *x, const char *name, long *v)
{
  const char *s = xml_attr(x, name);
  char *e;
  if (!s) return 0;
  *v = strtol(s, &e, 10);
  return e != s;
}
/* a colour element (x at its START) through docx.c's reader, and its a:alpha (per cent, 100 if none) */
static u32 colour_alpha(Xml *x, ObjColourFn colour, void *ctx, int *alpha)
{
  const char *st;
  size_t n = xml_raw(x, &st);
  Xml y;
  u32 col = 0;
  const char *a;
  *alpha = 100;
  xml_init(&y, st, n);
  if (xml_next(&y) == XT_START) col = colour(&y, ctx);
  xml_free(&y);
  if ((a = strstr(st, "alpha val=\"")) && a < st + n) *alpha = atoi(a + 11) / 1000;
  return col == COLOR_AUTO ? 0 : col;
}
/* the first colour element inside the element x is at (read to its END) */
static u32 inner_colour(Xml *x, ObjColourFn colour, void *ctx, int *alpha)
{
  int d0 = x->depth;
  u32 col = 0;
  *alpha = 100;
  if (x->pending_end) { xml_skip(x); return 0; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && (!strcmp(x->name, "srgbClr") || !strcmp(x->name, "schemeClr") || !strcmp(x->name, "sysClr") || !strcmp(x->name, "prstClr") || !strcmp(x->name, "scrgbClr")))
      col = colour_alpha(x, colour, ctx, alpha);
  }
  return col;
}
/* a14:imgProps (as text): its picture layer and the effects in it */
static void parse_a14(const char *s, size_t n, ObjRead *r)
{
  Xml x;
  long v;
  xml_init(&x, s, n);
  while (xml_next(&x) != XT_EOF) {
    const char *nm = x.name;
    int k;
    if (x.type != XT_START) continue;
    if (!strcmp(nm, "imgLayer")) { const char *e = xml_attr(&x, "embed"); if (e && strlen(e) < sizeof r->layer) strcpy(r->layer, e); }
    else if (!strcmp(nm, "sharpenSoften") && anum(&x, "amount", &v)) { r->a14.sharp = (signed char)(v / 1000 > 100 ? 100 : v / 1000 < -100 ? -100 : v / 1000); r->hasfx = 1; }
    else if (!strcmp(nm, "colorTemperature") && anum(&x, "colorTemp", &v)) { r->a14.temp = (u16)(v < 1500 ? 1500 : v > 11500 ? 11500 : v); r->hasfx = 1; }
    else if (!strcmp(nm, "saturation") && anum(&x, "sat", &v)) { r->a14.sat = (u16)(v / 1000 < 1 ? 1 : v / 1000 > 400 ? 400 : v / 1000); r->hasfx = 1; }
    else if (!strcmp(nm, "brightnessContrast")) {
      if (anum(&x, "bright", &v)) r->a14.bright = (signed char)(v / 1000);
      if (anum(&x, "contrast", &v)) r->a14.contrast = (signed char)(v / 1000);
      r->hasfx = 1;
    } else if (!strcmp(nm, "backgroundRemoval")) { r->a14.bgkey = 1; r->a14.bgtol = 25; r->hasfx = 1; }
    else for (k = 1; k < ART_N; k++) if (!strcmp(nm, art_names[k])) { r->a14.art = (u8)k; r->hasfx = 1; }
  }
  xml_free(&x);
}
int objfx_read(void *xv, Image *im, ObjRead *s, ObjColourFn colour, void *ctx)
{
  Xml *x = (Xml *)xv;
  const char *n = x->name;
  ObjFx *f = &im->fx;
  long v;
  int a;
  if (!strcmp(n, "blip")) { s->inblip = 1; return 0; }
  if (s->inblip) {
    if (!strcmp(n, "lum")) {
      long b = 0, c = 0;
      anum(x, "bright", &b); anum(x, "contrast", &c);
      if (b == 70000 && c == -70000 && !f->cmode) f->cmode = PCM_WASHOUT;
      else { f->bright = (signed char)(b / 1000 > 100 ? 100 : b / 1000 < -100 ? -100 : b / 1000); f->contrast = (signed char)(c / 1000 > 100 ? 100 : c / 1000 < -100 ? -100 : c / 1000); }
      return 0;
    }
    if (!strcmp(n, "grayscl")) { f->cmode = PCM_GREY; return 0; }
    if (!strcmp(n, "biLevel")) { f->cmode = PCM_BW; return 0; }
    if (!strcmp(n, "alphaModFix")) { if (anum(x, "amt", &v)) f->alpha = (u8)(v >= 100000 ? 0 : v <= 0 ? 100 : 100 - v / 1000); return 0; }
    if (!strcmp(n, "duotone")) {
      int d0 = x->depth, k = 0;
      u32 c[2] = { 0, 0xFFFFFF };
      if (x->pending_end) { xml_skip(x); return 1; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d0) break;
        if (x->type == XT_START && k < 2) c[k++] = colour_alpha(x, colour, ctx, &a);
      }
      if (c[0] == SEPIA_DARK && c[1] == SEPIA_LIGHT) f->cmode = PCM_SEPIA;
      else if (c[1] >= 0xF0F0F0 && (c[1] & 0xFF) >= 0xF0 && ((c[1] >> 8) & 0xFF) >= 0xF0) { f->cmode = PCM_LIGHT; f->recolor = c[0]; }
      else { f->cmode = PCM_DARK; f->recolor = c[1]; }
      return 1;
    }
    if (!strcmp(n, "clrChange")) {
      int d0 = x->depth;
      if (x->pending_end) { xml_skip(x); return 1; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d0) break;
        if (x->type == XT_START && !strcmp(x->name, "clrFrom")) { f->bgcolor = inner_colour(x, colour, ctx, &a); f->bgkey = 1; }
      }
      return 1;
    }
    if (!strcmp(n, "imgProps")) {
      const char *st;
      size_t len = xml_raw(x, &st);
      free(s->raw);
      if ((s->raw = (char *)malloc(len + 1))) { memcpy(s->raw, st, len); s->raw[len] = 0; }
      parse_a14(st, len, s);
      return 1;
    }
    return 0;
  }
  if (!strcmp(n, "blipFill") && im->kind != OBJ_PICTURE) { f->filltype = FT_PIC; return 0; }
  if (!strcmp(n, "gradFill")) {
    int d0 = x->depth, k = 0;
    if (x->pending_end) { xml_skip(x); return 1; }
    f->filltype = FT_GRAD; f->gtype = GT_LINEAR; f->gangle = 900;
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "gs") && k < 4) {
        int al;
        f->gpos[k] = (u8)(anum(x, "pos", &v) ? (v / 1000 > 100 ? 100 : v < 0 ? 0 : v / 1000) : k * 100);
        f->gcol[k] = inner_colour(x, colour, ctx, &al);
        k++;
      } else if (!strcmp(x->name, "lin") && anum(x, "ang", &v)) f->gangle = (u16)((v / 6000) % 3600);
      else if (!strcmp(x->name, "path")) { const char *p = xml_attr(x, "path"); f->gtype = (u8)(p && !strcmp(p, "rect") ? GT_RECT : GT_RADIAL); }
    }
    f->ngs = (u8)(k < 2 ? 2 : k);
    if (k < 2) { f->gcol[1] = 0xFFFFFF; f->gpos[1] = 100; }
    return 1;
  }
  if (!strcmp(n, "effectLst")) {
    int d0 = x->depth;
    if (x->pending_end) { xml_skip(x); return 1; }
    while (xml_next(x) != XT_EOF) {
      const char *e;
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      e = x->name;
      if (!strcmp(e, "outerShdw")) {
        f->shadow = 1;
        f->shblur = (u16)(anum(x, "blurRad", &v) ? v / 635 : 0);
        f->shdist = (u16)(anum(x, "dist", &v) ? v / 635 : 0);
        f->shdir = (u16)(anum(x, "dir", &v) ? (v / 6000) % 3600 : 0);
        f->shcolor = inner_colour(x, colour, ctx, &a);
        f->shalpha = (u8)(100 - a);
      } else if (!strcmp(e, "glow")) {
        f->glow = (u16)(anum(x, "rad", &v) ? (v / 635 > 0 ? v / 635 : 1) : 127);
        f->glcolor = inner_colour(x, colour, ctx, &a);
        f->glalpha = (u8)(100 - a);
      } else if (!strcmp(e, "softEdge")) f->soft = (u16)(anum(x, "rad", &v) ? v / 635 : 0);
    }
    return 1;
  }
  if (!strcmp(n, "prstGeom")) { int k = shapex_from_prst(xml_attr(x, "prst")); if (k >= SHX_FIRST) im->shape = (u8)k; return 0; }
  if (!strcmp(n, "bodyPr")) {
    const char *vt = xml_attr(x, "vert"), *an = xml_attr(x, "anchor");
    static const char *const ins[4] = { "lIns", "tIns", "rIns", "bIns" };
    static const i32 dflt[4] = { 144, 72, 144, 72 };
    int k;
    if (vt) f->vert = (u8)(!strcmp(vt, "vert") || !strcmp(vt, "eaVert") || !strcmp(vt, "wordArtVertRtl") ? TV_VERT : !strcmp(vt, "vert270") ? TV_VERT270 : TV_HORZ);
    if (an) f->anchor = (u8)(!strcmp(an, "ctr") ? TA_MIDDLE : !strcmp(an, "b") ? TA_BOTTOM : TA_TOP);
    for (k = 0; k < 4; k++) { f->ins[k] = dflt[k]; if (anum(x, ins[k], &v)) { f->ins[k] = (i32)(v / 635); if (f->ins[k] != dflt[k]) f->hasins = 1; } }
    return 0;
  }
  if (!strcmp(n, "txbx")) { if (anum(x, "id", &v) && v > 0) f->link = (u16)v; return 0; }
  if (!strcmp(n, "linkedTxbx")) {
    if (anum(x, "id", &v) && v > 0) f->link = (u16)v;
    if (anum(x, "seq", &v) && v > 0) f->seq = (u16)v;
    if (im->kind == OBJ_SHAPE && im->shape == SH_RECT) im->kind = OBJ_TEXTBOX;
    xml_skip(x);
    return 1;
  }
  if (!strcmp(n, "custGeom") && im->shape == SHX_ICON) { xml_skip(x); return 1; }
  return 0;
}
void objfx_read_end(void *xv, Image *im, ObjRead *s)
{
  Xml *x = (Xml *)xv;
  (void)im;
  if (!strcmp(x->name, "blip")) s->inblip = 0;
}
int objfx_read_finish(Image *im, ObjRead *s)
{
  ObjFx *f = &im->fx;
  if (f->link && !f->seq && im->kind == OBJ_SHAPE && im->shape == SH_RECT && im->fill == COLOR_AUTO) im->kind = OBJ_TEXTBOX;
  if (f->hasins) { if (f->ins[0] == 144 && f->ins[1] == 72 && f->ins[2] == 144 && f->ins[3] == 72) f->hasins = 0; }
  else memset(f->ins, 0, sizeof f->ins);
  if (im->shape == SHX_ICON && !f->icon) im->shape = SH_RECT;
  return s->raw && s->layer[0];
}
void objfx_read_docpr(void *xv, Image *im)
{
  const char *nm = xml_attr((Xml *)xv, "name");
  int k;
  if (nm && !strncmp(nm, "Icon: ", 6) && (k = objicon_find(nm + 6)) >= 0) { im->shape = SHX_ICON; im->fx.icon = (u16)(k + 1); }
}

/* ------------------------------------------------------------------ writing */
/* the effects Word can't draw from the file alone (drawn into the picture saved) */
static int baked(const ObjFx *f) { return f->art || f->sharp || f->temp || f->sat || (f->bgkey && f->bgtol); }
void objfx_write_blip(Buf *o, const Image *im, int orig)
{
  const ObjFx *f = &im->fx;
  int any = f->bright || f->contrast || f->cmode || f->alpha || f->bgkey || f->raw || (orig && baked(f));
  if (!any) { buf_adds(o, "/>"); return; }
  buf_adds(o, ">");
  if (f->bgkey && !f->bgtol)
    buf_printf(o, "<a:clrChange><a:clrFrom><a:srgbClr val=\"%06lX\"/></a:clrFrom><a:clrTo><a:srgbClr val=\"%06lX\"><a:alpha val=\"0\"/></a:srgbClr></a:clrTo></a:clrChange>",
               (unsigned long)(f->bgcolor & 0xFFFFFF), (unsigned long)(f->bgcolor & 0xFFFFFF));
  switch (f->cmode) {
  case PCM_GREY: buf_adds(o, "<a:grayscl/>"); break;
  case PCM_BW: buf_adds(o, "<a:biLevel thresh=\"50000\"/>"); break;
  case PCM_SEPIA: buf_printf(o, "<a:duotone><a:srgbClr val=\"%06lX\"/><a:srgbClr val=\"%06lX\"/></a:duotone>", (unsigned long)SEPIA_DARK, (unsigned long)SEPIA_LIGHT); break;
  case PCM_DARK: buf_printf(o, "<a:duotone><a:srgbClr val=\"000000\"/><a:srgbClr val=\"%06lX\"/></a:duotone>", (unsigned long)(f->recolor & 0xFFFFFF)); break;
  case PCM_LIGHT: buf_printf(o, "<a:duotone><a:srgbClr val=\"%06lX\"/><a:srgbClr val=\"FFFFFF\"/></a:duotone>", (unsigned long)(f->recolor & 0xFFFFFF)); break;
  }
  if (f->cmode == PCM_WASHOUT) buf_adds(o, "<a:lum bright=\"70000\" contrast=\"-70000\"/>");
  else if (f->bright || f->contrast) buf_printf(o, "<a:lum bright=\"%d\" contrast=\"%d\"/>", f->bright * 1000, f->contrast * 1000);
  if (f->alpha) buf_printf(o, "<a:alphaModFix amt=\"%d\"/>", (100 - f->alpha) * 1000);
  if (orig && baked(f)) {
    buf_printf(o, "<a:extLst><a:ext uri=\"" A14_URI "\"><a14:imgProps xmlns:a14=\"http://schemas.microsoft.com/office/drawing/2010/main\"><a14:imgLayer r:embed=\"rIdImg%d\">", orig);
    if (f->art) buf_printf(o, "<a14:imgEffect><a14:%s/></a14:imgEffect>", art_names[f->art]);
    if (f->bgkey && f->bgtol) buf_adds(o, "<a14:imgEffect><a14:backgroundRemoval t=\"0\" b=\"100000\" l=\"0\" r=\"100000\"/></a14:imgEffect>");
    if (f->sharp) buf_printf(o, "<a14:imgEffect><a14:sharpenSoften amount=\"%d\"/></a14:imgEffect>", f->sharp * 1000);
    if (f->temp) buf_printf(o, "<a14:imgEffect><a14:colorTemperature colorTemp=\"%u\"/></a14:imgEffect>", (unsigned)f->temp);
    if (f->sat) buf_printf(o, "<a14:imgEffect><a14:saturation sat=\"%u\"/></a14:imgEffect>", (unsigned)f->sat * 1000);
    buf_adds(o, "</a14:imgLayer></a14:imgProps></a:ext></a:extLst>");
  } else if (f->raw) {
    buf_adds(o, "<a:extLst><a:ext uri=\"" A14_URI "\">");
    buf_adds(o, f->raw);
    buf_adds(o, "</a:ext></a:extLst>");
  }
  buf_adds(o, "</a:blip>");
}
void objfx_write_geom(Buf *o, const Image *im, const char *prst)
{
  const char *x = im->shape >= SHX_FIRST ? shapex_prst(im->shape) : prst;
  if (im->shape == SHX_ICON) { objicon_custgeom(o, im->fx.icon ? im->fx.icon - 1 : 0); return; }
  buf_printf(o, "<a:prstGeom prst=\"%s\"><a:avLst/></a:prstGeom>", x ? x : "rect");
}
static void srgb(Buf *o, u32 c, int alpha)
{
  if (alpha < 100) buf_printf(o, "<a:srgbClr val=\"%06lX\"><a:alpha val=\"%d\"/></a:srgbClr>", (unsigned long)(c & 0xFFFFFF), alpha * 1000);
  else buf_printf(o, "<a:srgbClr val=\"%06lX\"/>", (unsigned long)(c & 0xFFFFFF));
}
void objfx_write_fill(Buf *o, const Image *im, int m)
{
  const ObjFx *f = &im->fx;
  int a = 100 - f->falpha, k;
  if (f->filltype == FT_PIC && m > 0) {
    buf_printf(o, "<a:blipFill rotWithShape=\"1\"><a:blip r:embed=\"rIdImg%d\"", m);
    if (f->falpha) buf_printf(o, "><a:alphaModFix amt=\"%d\"/></a:blip>", a * 1000); else buf_adds(o, "/>");
    buf_adds(o, "<a:stretch><a:fillRect/></a:stretch></a:blipFill>");
  } else if (f->filltype == FT_GRAD) {
    int n = f->ngs >= 2 && f->ngs <= 4 ? f->ngs : 2;
    buf_adds(o, "<a:gradFill rotWithShape=\"1\"><a:gsLst>");
    for (k = 0; k < n; k++) { buf_printf(o, "<a:gs pos=\"%d\">", f->gpos[k] * 1000); srgb(o, f->gcol[k], a); buf_adds(o, "</a:gs>"); }
    buf_adds(o, "</a:gsLst>");
    if (f->gtype == GT_LINEAR) buf_printf(o, "<a:lin ang=\"%ld\" scaled=\"0\"/>", (long)f->gangle * 6000L);
    else buf_printf(o, "<a:path path=\"%s\"><a:fillToRect l=\"50000\" t=\"50000\" r=\"50000\" b=\"50000\"/></a:path>", f->gtype == GT_RECT ? "rect" : "circle");
    buf_adds(o, "</a:gradFill>");
  } else if (im->fill == COLOR_AUTO) buf_adds(o, "<a:noFill/>");
  else { buf_adds(o, "<a:solidFill>"); srgb(o, im->fill, a); buf_adds(o, "</a:solidFill>"); }
}
void objfx_write_effects(Buf *o, const Image *im)
{
  const ObjFx *f = &im->fx;
  if (!f->shadow && !f->glow && !f->soft) return;
  buf_adds(o, "<a:effectLst>");
  if (f->glow) { buf_printf(o, "<a:glow rad=\"%ld\">", (long)f->glow * 635); srgb(o, f->glcolor, 100 - f->glalpha); buf_adds(o, "</a:glow>"); }
  if (f->shadow) {
    buf_printf(o, "<a:outerShdw blurRad=\"%ld\" dist=\"%ld\" dir=\"%ld\" algn=\"tl\" rotWithShape=\"0\">", (long)f->shblur * 635, (long)f->shdist * 635, (long)f->shdir * 6000L);
    srgb(o, f->shcolor, 100 - f->shalpha);
    buf_adds(o, "</a:outerShdw>");
  }
  if (f->soft) buf_printf(o, "<a:softEdge rad=\"%ld\"/>", (long)f->soft * 635);
  buf_adds(o, "</a:effectLst>");
}
void objfx_write_bodypr(Buf *o, const Image *im)
{
  static const char *const verts[3] = { "horz", "vert", "vert270" }, *const anchors[3] = { "t", "ctr", "b" };
  const ObjFx *f = &im->fx;
  buf_printf(o, "<wps:bodyPr rot=\"0\" vert=\"%s\" wrap=\"square\" lIns=\"%ld\" tIns=\"%ld\" rIns=\"%ld\" bIns=\"%ld\" anchor=\"%s\" anchorCtr=\"0\"><a:noAutofit/></wps:bodyPr>",
             verts[f->vert < 3 ? f->vert : 0], (long)tb_inset(im, 0) * 635, (long)tb_inset(im, 1) * 635, (long)tb_inset(im, 2) * 635, (long)tb_inset(im, 3) * 635,
             anchors[f->anchor < 3 ? f->anchor : 0]);
}
int objfx_linked_follower(const Image *im) { return im->fx.link && im->fx.seq; }
void objfx_write_linked(Buf *o, const Image *im) { buf_printf(o, "<wps:linkedTxbx id=\"%u\" seq=\"%u\"/>", (unsigned)im->fx.link, (unsigned)im->fx.seq); }
void objfx_write_txbx(Buf *o, const Image *im)
{
  if (im->fx.link) buf_printf(o, "<wps:txbx id=\"%u\">", (unsigned)im->fx.link);
  else buf_adds(o, "<wps:txbx>");
}
const char *objfx_docpr_name(const Image *im, char *buf, size_t cap)
{
  if (im->shape != SHX_ICON || !im->fx.icon) return NULL;
  snprintf(buf, cap, "Icon: %s", objicon_name(im->fx.icon - 1));
  return buf;
}

/* ------------------------------------------------------------------ the files */
const char *objfx_ext(int type) { return type == IMG_JPEG ? "jpeg" : type == IMG_GIF ? "gif" : type == IMG_BMP ? "bmp" : "png"; }
u32 objfx_orig_key(const Image *im) { return im->type == IMG_ILBM ? 1 : 0; }
u32 objfx_media_key(const Image *im)
{
  const ObjFx *f = &im->fx;
  u32 h;
  if (im->kind != OBJ_PICTURE || !baked(f)) return objfx_orig_key(im);
  h = (u32)f->art * 31 + (u32)(u8)f->sharp * 977 + (u32)f->temp * 7 + (u32)f->sat * 131 + (u32)f->bgkey * 5 + (u32)f->bgtol * 1013 + f->bgcolor * 3;
  return h < 2 ? h + 2 : h;
}
/* the whole picture at most 2048 pixels a side (each side as it is if smaller) */
static void fit_size(const Image *im, u32 *w, u32 *h)
{
  *w = im->pw ? im->pw : 1; *h = im->ph ? im->ph : 1;
  if (*w > 2048) { *h = (u32)((unsigned long)*h * 2048 / *w); *w = 2048; }
  if (*h > 2048) { *w = (u32)((unsigned long)*w * 2048 / *h); *h = 2048; }
  if (!*w) *w = 1;
  if (!*h) *h = 1;
}
u8 *objfx_media_bytes(const Image *im, u32 key, u32 *len, u8 *type)
{
  u32 w, h, i;
  u8 *rgb, *out;
  Rgba ra;
  size_t n = 0;
  int alpha = 0;
  ObjFx b;
  if (!im->data) return NULL;
  fit_size(im, &w, &h);
  if (key == 1) { w = im->pw; h = im->ph; }
  if ((long)w * h > 8000000L || !(rgb = img_decode(im->data, im->len, w, h))) return NULL;
  if (!rgba_alloc(&ra, (i32)w, (i32)h)) { free(rgb); return NULL; }
  /* only the effects Word doesn't draw itself; the others stay in the file as they are */
  memset(&b, 0, sizeof b);
  if (key > 1) { b.art = im->fx.art; b.sharp = im->fx.sharp; b.temp = im->fx.temp; b.sat = im->fx.sat; }
  for (i = 0; i < w * h; i++) {
    u8 *p = rgb + i * 3, *q = ra.px + i * 4;
    q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255;
    if (key > 1 && im->fx.bgkey && im->fx.bgtol) {
      int tol = im->fx.bgtol * 3 + 8, d = abs(p[0] - (int)((im->fx.bgcolor >> 16) & 255)) + abs(p[1] - (int)((im->fx.bgcolor >> 8) & 255)) + abs(p[2] - (int)(im->fx.bgcolor & 255));
      if (d <= tol) { q[3] = 0; alpha = 1; } else if (d < 2 * tol) { q[3] = (u8)(255 * (d - tol) / tol); alpha = 1; }
    }
  }
  objfx_adjust(rgb, w, h, &b);
  for (i = 0; i < w * h; i++) { ra.px[i * 4] = rgb[i * 3]; ra.px[i * 4 + 1] = rgb[i * 3 + 1]; ra.px[i * 4 + 2] = rgb[i * 3 + 2]; }
  free(rgb);
  if (im->type == IMG_JPEG && !alpha) { out = jpeg_encode(&ra, 90, 0, &n); *type = IMG_JPEG; }
  else { out = png_encode(&ra, 0, &n); *type = IMG_PNG; }
  rgba_free(&ra);
  *len = (u32)n;
  return out;
}
u8 *objfx_compress(const Image *im, int ppi, int delcrop, u32 *len)
{
  long cl = im->crop[0], ct = im->crop[1], cr = im->crop[2], cb = im->crop[3], fw, fh;
  u32 w, h, i;
  u8 *rgb = NULL, *out;
  Rgba ra;
  size_t n = 0;
  if (!im->data || im->kind != OBJ_PICTURE || ppi < 50) return NULL;
  if (cl < 0) cl = 0;
  if (ct < 0) ct = 0;
  if (cr < 0) cr = 0;
  if (cb < 0) cb = 0;
  if (cl + cr >= 99000) cl = cr = 0;
  if (ct + cb >= 99000) ct = cb = 0;
  /* the size wanted: the whole picture's (or the cropped part's) at ppi when shown cx x cy */
  w = (u32)((long)im->cx * ppi / 1440); h = (u32)((long)im->cy * ppi / 1440);
  if (!delcrop) { w = (u32)((long)w * 100000L / (100000L - cl - cr)); h = (u32)((long)h * 100000L / (100000L - ct - cb)); }
  fw = delcrop ? (long)im->pw * (100000L - cl - cr) / 100000L : (long)im->pw;
  fh = delcrop ? (long)im->ph * (100000L - ct - cb) / 100000L : (long)im->ph;
  if ((long)w > fw) w = (u32)fw;
  if ((long)h > fh) h = (u32)fh;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (!delcrop && w >= im->pw && h >= im->ph && im->type != IMG_ILBM && im->type != IMG_BMP) return NULL;     /* nothing to gain */
  if (delcrop) { Image t = *im; t.cx = (i32)w; t.cy = (i32)h; rgb = objfx_picture_rgb(&t, (i32)w, (i32)h, 1); }
  else rgb = img_decode(im->data, im->len, w, h);
  if (!rgb) return NULL;
  if (!rgba_alloc(&ra, (i32)w, (i32)h)) { free(rgb); return NULL; }
  for (i = 0; i < w * h; i++) { ra.px[i * 4] = rgb[i * 3]; ra.px[i * 4 + 1] = rgb[i * 3 + 1]; ra.px[i * 4 + 2] = rgb[i * 3 + 2]; ra.px[i * 4 + 3] = 255; }
  free(rgb);
  out = im->type == IMG_JPEG ? jpeg_encode(&ra, 85, ppi, &n) : png_encode(&ra, ppi, &n);
  rgba_free(&ra);
  if (out && !delcrop && n >= im->len && im->type != IMG_ILBM && im->type != IMG_BMP) { free(out); return NULL; }
  *len = (u32)n;
  return out;
}
