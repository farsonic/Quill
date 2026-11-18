/* wordart.c - see wordart.h. WP19's WordArt: text effects parsed from (and written into) a run's
   kept w14 properties, the presets, warps, the letters' outlines drawn with their effects into a
   raster and as PDF paths. Written for Quill from Word's documented behaviour ([MS-DOCX] 2.6, ECMA-376
   20.1.9.19); the warps' shapes follow the description in WordCraft's layout notes (no code taken). */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wordart.h"
#include "charfx.h"
#include "math.h"
#include "xml.h"

/* ------------------------------------------------------------------ colours */
/* the default Office theme (2013 on), which Quill uses for scheme colours */
static const struct { const char *n; u32 c; } theme[] = {
  { "dk1", 0x000000 }, { "tx1", 0x000000 }, { "lt1", 0xFFFFFF }, { "bg1", 0xFFFFFF }, { "dk2", 0x44546A }, { "tx2", 0x44546A },
  { "lt2", 0xE7E6E6 }, { "bg2", 0xE7E6E6 }, { "accent1", 0x4472C4 }, { "accent2", 0xED7D31 }, { "accent3", 0xA5A5A5 },
  { "accent4", 0xFFC000 }, { "accent5", 0x5B9BD5 }, { "accent6", 0x70AD47 }, { "hlink", 0x0563C1 }, { "folHlink", 0x954F72 },
  { "phClr", 0x4472C4 } };
u32 wa_scheme(const char *name)
{
  size_t i;
  if (!name) return COLOR_AUTO;
  for (i = 0; i < sizeof theme / sizeof theme[0]; i++) if (!strcmp(name, theme[i].n)) return theme[i].c;
  return COLOR_AUTO;
}
static long anum(Xml *x, const char *name, long dflt)
{
  const char *v = xml_attr(x, name);
  return v ? atol(v) : dflt;
}
static u32 hexval(const char *v)
{
  u32 c = 0;
  int i;
  if (!v) return 0;
  for (i = 0; i < 6 && v[i]; i++) {
    int ch = v[i], d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : 0;
    c = c << 4 | (u32)d;
  }
  return c;
}
static int is_clr(const char *n) { return !strcmp(n, "srgbClr") || !strcmp(n, "schemeClr") || !strcmp(n, "prstClr") || !strcmp(n, "sysClr"); }
static u32 clamp255(long v) { return (u32)(v < 0 ? 0 : v > 255 ? 255 : v); }
/* a colour element (x at its START, read to its END): 0xRRGGBB, its transparency per cent into
   *alpha (w14:alpha is the transparency, unlike DrawingML's a:alpha) */
static u32 read_colour(Xml *x, u8 *alpha)
{
  int d0 = x->depth;
  const char *n = x->name, *v = xml_attr(x, "val");
  u32 c = 0, r, g, b;
  long lmod = 100000, loff = 0, tint = 100000, shade = 100000;
  *alpha = 0;
  if (!strcmp(n, "srgbClr")) c = hexval(v);
  else if (!strcmp(n, "sysClr")) c = hexval(xml_attr(x, "lastClr"));
  else if (!strcmp(n, "schemeClr")) { c = wa_scheme(v); if (c == COLOR_AUTO) c = 0; }
  else if (!strcmp(n, "prstClr") && v) c = !strcmp(v, "white") ? 0xFFFFFF : !strcmp(v, "red") ? 0xFF0000 : 0;
  if (x->pending_end) xml_skip(x);
  else
    while (xml_next(x) != XT_EOF) {
      long t;
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      t = anum(x, "val", 100000);
      if (!strcmp(x->name, "alpha")) *alpha = (u8)(t <= 0 ? 0 : t >= 100000 ? 100 : t / 1000);
      else if (!strcmp(x->name, "lumMod")) lmod = t;
      else if (!strcmp(x->name, "lumOff")) loff = t;
      else if (!strcmp(x->name, "tint")) tint = t;
      else if (!strcmp(x->name, "shade")) shade = t;
    }
  r = c >> 16 & 255; g = c >> 8 & 255; b = c & 255;
  if (shade != 100000) { r = (u32)(r * shade / 100000); g = (u32)(g * shade / 100000); b = (u32)(b * shade / 100000); }
  if (tint != 100000) { r = (u32)(255 - (255 - r) * tint / 100000); g = (u32)(255 - (255 - g) * tint / 100000); b = (u32)(255 - (255 - b) * tint / 100000); }
  if (lmod != 100000 || loff) {
    r = clamp255((long)r * lmod / 100000 + 255 * loff / 100000); g = clamp255((long)g * lmod / 100000 + 255 * loff / 100000);
    b = clamp255((long)b * lmod / 100000 + 255 * loff / 100000);
  }
  return r << 16 | g << 8 | b;
}
/* the first colour inside the element whose START x is at (read to its END) */
static u32 inner_colour(Xml *x, u8 *alpha)
{
  int d0 = x->depth, got = 0;
  u32 c = 0;
  *alpha = 0;
  if (x->pending_end) { xml_skip(x); return 0; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && is_clr(x->name) && !got) { c = read_colour(x, alpha); got = 1; }
  }
  return c;
}
/* a fill (noFill, solidFill, gradFill) at x (read to its END); 0 if x is none of them */
typedef struct { u8 kind, alpha, ngs, gpos[WA_MAXGS], gal[WA_MAXGS], gpath; u32 col, gcol[WA_MAXGS]; u16 ang; } Fill;
static int read_fill(Xml *x, Fill *f)
{
  const char *n = x->name;
  memset(f, 0, sizeof *f);
  if (!strcmp(n, "noFill")) { f->kind = WF_NONE; xml_skip(x); return 1; }
  if (!strcmp(n, "solidFill")) { f->kind = WF_SOLID; f->col = inner_colour(x, &f->alpha); return 1; }
  if (!strcmp(n, "gradFill")) {
    int d0 = x->depth, k = 0;
    f->ang = 900;
    if (x->pending_end) { xml_skip(x); f->kind = WF_SOLID; return 1; }
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "gs") && k < WA_MAXGS) {
        long p = anum(x, "pos", k ? 100000 : 0);
        f->gpos[k] = (u8)(p <= 0 ? 0 : p >= 100000 ? 100 : p / 1000);
        f->gcol[k] = inner_colour(x, &f->gal[k]);
        k++;
      } else if (!strcmp(x->name, "lin")) { long a = anum(x, "ang", 5400000); f->ang = (u16)((a < 0 ? 0 : a / 6000) % 3600); }
      else if (!strcmp(x->name, "path")) f->gpath = 1;
    }
    if (k == 1) { f->gpos[1] = 100; f->gcol[1] = f->gcol[0]; f->gal[1] = f->gal[0]; k = 2; }
    f->ngs = (u8)k;
    f->kind = k ? WF_GRAD : WF_SOLID;
    f->col = k ? f->gcol[0] : 0;
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ reading rx */
/* the w14 elements WordArt models, in the schema's order (their rank), and the kept ones after them */
static const char *const order[] = { "w14:glow", "w14:shadow", "w14:reflection", "w14:textOutline", "w14:textFill", "w14:scene3d", "w14:props3d",
                                     "w14:ligatures", "w14:numForm", "w14:numSpacing", "w14:stylisticSets", "w14:cntxtAlts" };
#define NORDER ((int)(sizeof order / sizeof order[0]))
/* the rank of the element starting at s ('<'): 1.. for w14 ones, 0 for any other */
static int rank_of(const char *s)
{
  int k;
  for (k = 0; k < NORDER; k++) {
    size_t l = strlen(order[k]);
    if (!strncmp(s + 1, order[k], l) && (s[1 + l] == ' ' || s[1 + l] == '/' || s[1 + l] == '>')) return k + 1;
  }
  return !strncmp(s + 1, "w14:", 4) ? NORDER + 1 : 0;
}
static void parse_fx(const char *s, TextFx *fx, int *any)
{
  Xml x;
  xml_init(&x, s, strlen(s));
  while (xml_next(&x) != XT_EOF) {
    int r, d0;
    if (x.type != XT_START || x.depth != 1) continue;
    r = rank_of(x.s + x.tok);
    d0 = x.depth;
    if (r == 1) {                                  /* glow */
      fx->glow = 1; fx->glrad = (i32)(anum(&x, "rad", 0) / 635);
      fx->glcol = inner_colour(&x, &fx->glalpha);
      *any = 1;
    } else if (r == 2) {                           /* shadow */
      fx->shadow = 1;
      fx->shblur = (i32)(anum(&x, "blurRad", 0) / 635); fx->shdist = (i32)(anum(&x, "dist", 0) / 635);
      fx->shdir = (u16)((anum(&x, "dir", 0) / 6000) % 3600);
      fx->shcol = inner_colour(&x, &fx->shalpha);
      *any = 1;
    } else if (r == 3) {                           /* reflection */
      long sta = anum(&x, "stA", 100000), ena = anum(&x, "endA", 0), enp = anum(&x, "endPos", 100000);
      fx->refl = 1;
      fx->rblur = (i32)(anum(&x, "blurRad", 0) / 635); fx->rdist = (i32)(anum(&x, "dist", 0) / 635);
      fx->rsta = (u8)(sta < 0 ? 0 : sta > 100000 ? 100 : sta / 1000); fx->rena = (u8)(ena < 0 ? 0 : ena > 100000 ? 100 : ena / 1000);
      fx->renpos = (u8)(enp < 0 ? 0 : enp > 100000 ? 100 : enp / 1000);
      xml_skip(&x);
      *any = 1;
    } else if (r == 4) {                           /* textOutline */
      fx->lw = (i32)(anum(&x, "w", 9525) / 635);
      if (!fx->lw) fx->lw = 1;
      fx->line = WF_SOLID; fx->lcol = 0;
      if (!x.pending_end)
        while (xml_next(&x) != XT_EOF) {
          Fill f;
          if (x.type == XT_END && x.depth < d0) break;
          if (x.type == XT_START && read_fill(&x, &f)) { fx->line = f.kind == WF_GRAD ? WF_SOLID : f.kind; fx->lcol = f.col; fx->lalpha = f.kind == WF_GRAD ? f.gal[0] : f.alpha; }
        }
      else xml_skip(&x);
      *any = 1;
    } else if (r == 5) {                           /* textFill */
      if (!x.pending_end)
        while (xml_next(&x) != XT_EOF) {
          Fill f;
          if (x.type == XT_END && x.depth < d0) break;
          if (x.type == XT_START && read_fill(&x, &f)) {
            fx->fill = f.kind; fx->fcol = f.col; fx->falpha = f.alpha;
            fx->ngs = f.ngs; memcpy(fx->gpos, f.gpos, sizeof fx->gpos); memcpy(fx->galpha, f.gal, sizeof fx->galpha);
            memcpy(fx->gcol, f.gcol, sizeof fx->gcol); fx->gangle = f.ang; fx->gpath = f.gpath;
          }
        }
      else xml_skip(&x);
      *any = 1;
    } else if (r == 7) {                           /* props3d: its top bevel */
      if (!x.pending_end)
        while (xml_next(&x) != XT_EOF) {
          if (x.type == XT_END && x.depth < d0) break;
          if (x.type == XT_START && !strcmp(x.name, "bevelT")) {
            const char *p = xml_attr(&x, "prst");
            fx->bevel = (u8)(!p || !strcmp(p, "circle") ? BV_CIRCLE : !strcmp(p, "relaxedInset") ? BV_RELAXED : !strcmp(p, "slope") ? BV_SLOPE :
                             !strcmp(p, "angle") ? BV_ANGLE : BV_CIRCLE);
            *any = 1;
          }
        }
      else xml_skip(&x);
    } else xml_skip(&x);
  }
  xml_free(&x);
}
/* parsed effects by rx number (the rx table only grows and its texts never change) */
static TextFx *fxtab; static u8 *fxstate; static int fxcap;     /* fxstate: 0 not parsed, 1 none, 2 parsed */
const TextFx *wa_get(u16 rx)
{
  int any = 0;
  if (!rx) return NULL;
  if (rx > fxcap) {
    int nc = rx + 64;
    TextFx *nt = (TextFx *)realloc(fxtab, (size_t)nc * sizeof *nt);
    u8 *ns;
    if (!nt) return NULL;
    fxtab = nt;
    if (!(ns = (u8 *)realloc(fxstate, (size_t)nc))) return NULL;
    memset(ns + fxcap, 0, (size_t)(nc - fxcap));
    fxstate = ns; fxcap = nc;
  }
  if (!fxstate[rx - 1]) {
    TextFx *f = &fxtab[rx - 1];
    const char *s = rx_text(rx);
    memset(f, 0, sizeof *f);
    if (strstr(s, "w14:")) parse_fx(s, f, &any);
    fxstate[rx - 1] = (u8)(any ? 2 : 1);
  }
  return fxstate[rx - 1] == 2 ? &fxtab[rx - 1] : NULL;
}

/* ------------------------------------------------------------------ writing rx */
static void w_colour(Buf *b, u32 c, int alpha)
{
  if (alpha > 0) buf_printf(b, "<w14:srgbClr w14:val=\"%06lX\"><w14:alpha w14:val=\"%d\"/></w14:srgbClr>", (unsigned long)(c & 0xFFFFFF), alpha * 1000);
  else buf_printf(b, "<w14:srgbClr w14:val=\"%06lX\"/>", (unsigned long)(c & 0xFFFFFF));
}
static void w_fill(Buf *b, int kind, u32 col, int alpha, const TextFx *fx)
{
  int k;
  if (kind == WF_NONE) { buf_adds(b, "<w14:noFill/>"); return; }
  if (kind == WF_GRAD && fx && fx->ngs >= 2) {
    buf_adds(b, "<w14:gradFill><w14:gsLst>");
    for (k = 0; k < fx->ngs && k < WA_MAXGS; k++) { buf_printf(b, "<w14:gs w14:pos=\"%d\">", fx->gpos[k] * 1000); w_colour(b, fx->gcol[k], fx->galpha[k]); buf_adds(b, "</w14:gs>"); }
    buf_adds(b, "</w14:gsLst>");
    if (fx->gpath) buf_adds(b, "<w14:path w14:path=\"circle\"><w14:fillToRect w14:l=\"50000\" w14:t=\"50000\" w14:r=\"50000\" w14:b=\"50000\"/></w14:path>");
    else buf_printf(b, "<w14:lin w14:ang=\"%ld\" w14:scaled=\"0\"/>", (long)fx->gangle * 6000L);
    buf_adds(b, "</w14:gradFill>");
    return;
  }
  buf_adds(b, "<w14:solidFill>"); w_colour(b, col, alpha); buf_adds(b, "</w14:solidFill>");
}
static const char *const bevel_prst[BV_N] = { "", "circle", "relaxedInset", "slope", "angle" };
/* the element of rank r (1..7) for fx, or nothing */
static void w_elem(Buf *b, const TextFx *fx, int r)
{
  switch (r) {
  case 1:
    if (!fx->glow) break;
    buf_printf(b, "<w14:glow w14:rad=\"%ld\">", (long)fx->glrad * 635); w_colour(b, fx->glcol, fx->glalpha); buf_adds(b, "</w14:glow>");
    break;
  case 2:
    if (!fx->shadow) break;
    buf_printf(b, "<w14:shadow w14:blurRad=\"%ld\" w14:dist=\"%ld\" w14:dir=\"%ld\" w14:sx=\"100000\" w14:sy=\"100000\" w14:kx=\"0\" w14:ky=\"0\" w14:algn=\"tl\">",
               (long)fx->shblur * 635, (long)fx->shdist * 635, (long)fx->shdir * 6000L);
    w_colour(b, fx->shcol, fx->shalpha); buf_adds(b, "</w14:shadow>");
    break;
  case 3:
    if (!fx->refl) break;
    buf_printf(b, "<w14:reflection w14:blurRad=\"%ld\" w14:stA=\"%d\" w14:stPos=\"0\" w14:endA=\"%d\" w14:endPos=\"%d\" w14:dist=\"%ld\" w14:dir=\"5400000\" "
                  "w14:fadeDir=\"5400000\" w14:sx=\"100000\" w14:sy=\"-100000\" w14:kx=\"0\" w14:ky=\"0\" w14:algn=\"bl\"/>",
               (long)fx->rblur * 635, fx->rsta * 1000, fx->rena * 1000, fx->renpos * 1000, (long)fx->rdist * 635);
    break;
  case 4:
    if (fx->line == WF_DEFAULT) break;
    buf_printf(b, "<w14:textOutline w14:w=\"%ld\" w14:cap=\"flat\" w14:cmpd=\"sng\" w14:algn=\"ctr\">", (long)fx->lw * 635);
    w_fill(b, fx->line, fx->lcol, fx->lalpha, NULL);
    buf_adds(b, "<w14:prstDash w14:val=\"solid\"/><w14:round/></w14:textOutline>");
    break;
  case 5:
    if (fx->fill == WF_DEFAULT) break;
    buf_adds(b, "<w14:textFill>"); w_fill(b, fx->fill, fx->fcol, fx->falpha, fx); buf_adds(b, "</w14:textFill>");
    break;
  case 6:
    if (!fx->bevel) break;
    buf_adds(b, "<w14:scene3d><w14:camera w14:prst=\"orthographicFront\"/><w14:lightRig w14:rig=\"threePt\" w14:dir=\"t\"><w14:rot w14:lat=\"0\" w14:lon=\"0\" w14:rev=\"0\"/></w14:lightRig></w14:scene3d>");
    break;
  case 7:
    if (!fx->bevel) break;
    buf_printf(b, "<w14:props3d w14:extrusionH=\"0\" w14:contourW=\"0\" w14:prstMaterial=\"warmMatte\"><w14:bevelT w14:w=\"38100\" w14:h=\"38100\" w14:prst=\"%s\"/></w14:props3d>",
               bevel_prst[fx->bevel < BV_N ? fx->bevel : 1]);
    break;
  }
}
/* the end of the element starting at s ('<'), counting nesting */
static const char *elem_end(const char *s)
{
  int depth = 0;
  while (*s) {
    if (s[0] == '<' && s[1] == '/') { const char *e = strchr(s, '>'); if (!e) return s + strlen(s); s = e + 1; if (--depth <= 0) return s; continue; }
    if (s[0] == '<') {
      const char *e = strchr(s, '>');
      if (!e) return s + strlen(s);
      if (e[-1] == '/') { s = e + 1; if (depth == 0) return s; continue; }
      depth++; s = e + 1; continue;
    }
    s++;
  }
  return s;
}
u16 wa_set(u16 rx, const TextFx *fx, unsigned which)
{
  static const unsigned part[8] = { 0, WA_GLOW, WA_SHADOW, WA_REFL, WA_LINE, WA_FILL, WA_3D, WA_3D };
  const char *s = rx_text(rx), *p;
  Buf b;
  int r;
  u16 res;
  buf_init(&b);
  /* the elements of rank 0 (w:effect, w:kern ...) first as they were, then each rank in turn: the
     changed parts written afresh, the others kept */
  for (r = 0; r <= NORDER + 1; r++) {
    if (r >= 1 && r <= 7 && (which & part[r])) { w_elem(&b, fx, r); continue; }
    for (p = s; *p; ) {
      const char *e;
      if (*p != '<') { p++; continue; }
      e = elem_end(p);
      if (rank_of(p) == r) buf_add(&b, p, (size_t)(e - p));
      p = e;
    }
  }
  res = b.oom ? rx : rx_add(b.data ? b.data : "", b.len);
  buf_free(&b);
  return res;
}

/* ------------------------------------------------------------------ presets */
const char *const wa_style_names[WA_NSTYLES] = { "Fill: Black, Shadow", "Fill: Blue", "Fill: White, Outline: Blue, Glow", "Outline: Blue, Hollow",
  "Gradient: Blue", "Gradient: Gold, Outline: Brown", "Fill: Orange, Reflection", "Fill: Grey, Bevel", "Fill: Red, Outline: Black",
  "Fill: Green, Shadow", "Gradient: Sunset, Glow", "Fill: Navy, Reflection, Bevel" };
static void solid(TextFx *fx, u32 c) { fx->fill = WF_SOLID; fx->fcol = c; fx->falpha = 0; }
static void line(TextFx *fx, u32 c, i32 w) { fx->line = WF_SOLID; fx->lcol = c; fx->lalpha = 0; fx->lw = w; }
static void grad2(TextFx *fx, u32 a, u32 b, u16 ang)
{
  fx->fill = WF_GRAD; fx->ngs = 2; fx->gpos[0] = 0; fx->gpos[1] = 100; fx->gcol[0] = a; fx->gcol[1] = b;
  fx->galpha[0] = fx->galpha[1] = 0; fx->gangle = ang; fx->gpath = 0;
}
static void shadow(TextFx *fx, u32 c, int alpha, i32 blur, i32 dist, u16 dir) { fx->shadow = 1; fx->shcol = c; fx->shalpha = (u8)alpha; fx->shblur = blur; fx->shdist = dist; fx->shdir = dir; }
static void glow(TextFx *fx, u32 c, int alpha, i32 rad) { fx->glow = 1; fx->glcol = c; fx->glalpha = (u8)alpha; fx->glrad = rad; }
static void refl(TextFx *fx, int sta, int enpos, i32 dist) { fx->refl = 1; fx->rsta = (u8)sta; fx->rena = 0; fx->renpos = (u8)enpos; fx->rdist = dist; fx->rblur = 10; }
void wa_style(int i, TextFx *fx)
{
  memset(fx, 0, sizeof *fx);
  switch (i) {
  case 0: solid(fx, 0x000000); shadow(fx, 0x000000, 60, 76, 38, 450); break;
  case 1: solid(fx, 0x4472C4); break;
  case 2: solid(fx, 0xFFFFFF); line(fx, 0x4472C4, 15); glow(fx, 0x4472C4, 60, 120); break;
  case 3: fx->fill = WF_NONE; line(fx, 0x4472C4, 20); break;
  case 4: grad2(fx, 0x8FAADC, 0x203864, 900); break;
  case 5: grad2(fx, 0xFFE699, 0xBF9000, 900); line(fx, 0x7F6000, 10); break;
  case 6: solid(fx, 0xED7D31); refl(fx, 50, 55, 0); break;
  case 7: solid(fx, 0x7F7F7F); fx->bevel = BV_CIRCLE; break;
  case 8: solid(fx, 0xC00000); line(fx, 0x000000, 10); break;
  case 9: solid(fx, 0x548235); shadow(fx, 0x000000, 50, 60, 60, 450); break;
  case 10: grad2(fx, 0xFFC000, 0xC00000, 900); glow(fx, 0xFFD966, 40, 160); break;
  case 11: solid(fx, 0x1F3864); refl(fx, 45, 40, 20); fx->bevel = BV_RELAXED; break;
  }
}
const char *const wa_fill_names[WA_NFILLS] = { "No Fill", "Black", "White", "Blue", "Orange", "Grey", "Gold", "Green", "Red",
  "Gradient: Blue", "Gradient: Gold", "Gradient: Silver", "Gradient: Sunset", "Gradient: From Center" };
const u32 wa_fill_colours[WA_NFILLS] = { 0xFF000000, 0x000000, 0xFFFFFF, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x70AD47, 0xC00000,
  0x4472C4, 0xFFC000, 0xBFBFBF, 0xFF6600, 0x5B9BD5 };
void wa_fill_preset(int i, TextFx *fx)
{
  if (i <= 0) { fx->fill = WF_NONE; if (fx->line == WF_DEFAULT || fx->line == WF_NONE) line(fx, 0, 10); return; }
  if (i < 9) { solid(fx, wa_fill_colours[i]); return; }
  switch (i) {
  case 9: grad2(fx, 0x8FAADC, 0x203864, 900); break;
  case 10: grad2(fx, 0xFFE699, 0xBF9000, 900); break;
  case 11: fx->fill = WF_GRAD; fx->ngs = 3; fx->gpos[0] = 0; fx->gpos[1] = 50; fx->gpos[2] = 100; fx->gcol[0] = 0xF2F2F2; fx->gcol[1] = 0x7F7F7F;
    fx->gcol[2] = 0xD9D9D9; fx->galpha[0] = fx->galpha[1] = fx->galpha[2] = 0; fx->gangle = 900; fx->gpath = 0; break;
  case 12: grad2(fx, 0xFFC000, 0xC00000, 900); break;
  default: grad2(fx, 0xDEEBF7, 0x2E75B6, 0); fx->gpath = 1; break;
  }
}
const char *const wa_line_names[WA_NLINES] = { "No Outline", "Black", "White", "Blue", "Orange", "Grey", "Gold", "Green", "Red",
  "Weight: 0.5 pt", "Weight: 1 pt", "Weight: 2.25 pt" };
const u32 wa_line_colours[WA_NLINES] = { 0xFF000000, 0x000000, 0xFFFFFF, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x70AD47, 0xC00000,
  0xFF000000, 0xFF000000, 0xFF000000 };
void wa_line_preset(int i, TextFx *fx)
{
  static const i32 w[3] = { 10, 20, 45 };
  if (i <= 0) { fx->line = WF_NONE; if (fx->fill == WF_NONE) solid(fx, 0); return; }
  if (i < 9) { line(fx, wa_line_colours[i], fx->line == WF_SOLID && fx->lw ? fx->lw : 10); return; }
  if (fx->line != WF_SOLID) line(fx, 0, 10);
  fx->lw = w[(i - 9) % 3];
}
const char *const wa_shadow_names[WA_NSHADOWS] = { "No Shadow", "Offset: Bottom Right", "Offset: Bottom", "Offset: Top Left", "Offset: Center", "Inside Far" };
void wa_shadow_preset(int i, TextFx *fx)
{
  fx->shadow = 0;
  switch (i) {
  case 1: shadow(fx, 0, 60, 60, 60, 450); break;
  case 2: shadow(fx, 0, 60, 60, 60, 900); break;
  case 3: shadow(fx, 0, 60, 60, 60, 2250); break;
  case 4: shadow(fx, 0, 40, 120, 0, 0); break;
  case 5: shadow(fx, 0x404040, 30, 30, 140, 450); break;
  }
}
const char *const wa_refl_names[WA_NREFLS] = { "No Reflection", "Tight Reflection: Touching", "Half Reflection: 4 pt Offset", "Full Reflection: 8 pt Offset" };
void wa_refl_preset(int i, TextFx *fx)
{
  fx->refl = 0;
  if (i == 1) refl(fx, 50, 35, 0);
  else if (i == 2) refl(fx, 50, 55, 80);
  else if (i == 3) refl(fx, 50, 90, 160);
}
const char *const wa_glow_names[WA_NGLOWS] = { "No Glow", "Glow: 5 pt; Blue", "Glow: 8 pt; Orange", "Glow: 11 pt; Gold", "Glow: 18 pt; Green", "Glow: 5 pt; Grey" };
void wa_glow_preset(int i, TextFx *fx)
{
  fx->glow = 0;
  switch (i) {
  case 1: glow(fx, 0x4472C4, 40, 100); break;
  case 2: glow(fx, 0xED7D31, 40, 160); break;
  case 3: glow(fx, 0xFFC000, 40, 220); break;
  case 4: glow(fx, 0x70AD47, 40, 360); break;
  case 5: glow(fx, 0xA5A5A5, 40, 100); break;
  }
}
const char *const wa_bevel_names[WA_NBEVELS] = { "No Bevel", "Bevel: Circle", "Bevel: Relaxed Inset", "Bevel: Slope", "Bevel: Angle" };
void wa_bevel_preset(int i, TextFx *fx) { fx->bevel = (u8)(i > 0 && i < BV_N ? i : 0); }

/* ------------------------------------------------------------------ warps */
static const char *const warps[WW_ALL] = { NULL,
  "textArchUp", "textArchDown", "textCircle", "textWave1", "textSlantUp", "textSlantDown", "textInflate", "textDeflate",
  "textChevron", "textChevronInverted", "textTriangle", "textTriangleInverted",
  "textPlain", "textNoShape", "textStop", "textRingInside", "textRingOutside", "textButton", "textArchUpPour", "textArchDownPour",
  "textCirclePour", "textButtonPour", "textCurveUp", "textCurveDown", "textCanUp", "textCanDown", "textWave2", "textDoubleWave1",
  "textWave4", "textInflateBottom", "textDeflateBottom", "textInflateTop", "textDeflateTop", "textDeflateInflate",
  "textDeflateInflateDeflate", "textFadeRight", "textFadeLeft", "textFadeUp", "textFadeDown", "textCascadeUp", "textCascadeDown" };
const char *const wa_warp_labels[WW_N] = { "No Transform", "Arch Up", "Arch Down", "Circle", "Wave", "Slant Up", "Slant Down", "Inflate",
  "Deflate", "Chevron Up", "Chevron Down", "Triangle Up", "Triangle Down" };
const char *wa_warp_prst(int w) { return w > 0 && w < WW_ALL ? warps[w] : NULL; }
int wa_warp_from_prst(const char *prst)
{
  int k;
  if (!prst) return 0;
  for (k = 1; k < WW_ALL; k++) if (!strcmp(prst, warps[k])) return k;
  return 0;
}
/* sine of a (256ths of a tenth of a degree), TRIG_ONE scale, between the table's tenths */
static long fsin(long a)
{
  long t = a >> 8, f = a & 255, s0, s1;
  s0 = isin((int)(t % 3600)); s1 = isin((int)((t + 1) % 3600));
  return s0 + (s1 - s0) * f / 256;
}
static long fcos(long a) { return fsin(a + 900L * 256); }
#define ONE 65536L
void wa_warp_map(int warp, i32 w, i32 h, int aspect, i32 u, i32 v, i32 *x, i32 *y)
{
  long top = 0, bot = ONE, k, t, s;
  long long X, Y;
  switch (warp) {
  case WW_ARCHUP: case WW_ARCHDOWN: case WW_CIRCLE: {
    /* along an ellipse: from angle start (tenths of a degree, counter-clockwise from the right, y up)
       sweeping sweep; the letters' thickness a part of the radius, keeping their proportions */
    long start, sweep, a, rho, thick;
    int outward = warp != WW_ARCHDOWN;
    if (warp == WW_ARCHUP) { start = 1800; sweep = -1800; }
    else if (warp == WW_ARCHDOWN) { start = 1800; sweep = 1800; }
    else { start = 2700 - 11; sweep = -(3600 - 23); }
    thick = (long)aspect * (sweep < 0 ? -sweep : sweep) * 31416 / 1800 / 10000;     /* aspect x sweep in radians, per 1000 */
    if (thick < 80) thick = 80;
    if (thick > 600) thick = 600;
    thick = thick * ONE / 1000;
    a = start * 256 + (long)((long long)sweep * 256 * u / ONE);
    rho = outward ? ONE - (long)((long long)v * thick / ONE) : ONE - thick + (long)((long long)v * thick / ONE);
    X = (long long)w / 2 + (long long)w / 2 * rho / ONE * fcos(a) / TRIG_ONE;
    Y = (long long)h / 2 - (long long)h / 2 * rho / ONE * fsin(a) / TRIG_ONE;
    *x = (i32)X; *y = (i32)Y;
    return; }
  case WW_SLANTUP: k = ONE * 4 / 10; top = k * (ONE - u) / ONE; bot = ONE - k * u / ONE; break;
  case WW_SLANTDOWN: k = ONE * 4 / 10; top = k * u / ONE; bot = ONE - k * (ONE - u) / ONE; break;
  case WW_INFLATE: case WW_DEFLATE:
    k = ONE * 3 / 10;
    s = fsin((long)((long long)1800 * 256 * u / ONE)) * ONE / TRIG_ONE;
    if (warp == WW_INFLATE) { top = k * (ONE - s) / ONE; bot = ONE - top; }
    else { top = k * s / ONE; bot = ONE - top; }
    break;
  case WW_WAVE:
    k = ONE / 8;
    s = k * (fsin((long)((long long)3600 * 256 * u / ONE)) * ONE / TRIG_ONE) / ONE;
    top = k - s; bot = ONE - k - s;
    break;
  case WW_CHEVRONUP: case WW_CHEVRONDOWN:
    k = ONE * 4 / 10; t = 2 * (long)u - ONE; if (t < 0) t = -t;
    if (warp == WW_CHEVRONUP) { top = k * t / ONE; bot = ONE - k * (ONE - t) / ONE; }
    else { top = k * (ONE - t) / ONE; bot = ONE - k * t / ONE; }
    break;
  case WW_TRIUP: k = ONE * 6 / 10; t = 2 * (long)u - ONE; if (t < 0) t = -t; top = k * t / ONE; break;
  case WW_TRIDOWN: k = ONE * 6 / 10; t = 2 * (long)u - ONE; if (t < 0) t = -t; bot = ONE - k * t / ONE; break;
  }
  X = (long long)w * u / ONE;
  Y = (long long)h * (top + (long long)(bot - top) * v / ONE) / ONE;
  *x = (i32)X; *y = (i32)Y;
}

/* ------------------------------------------------------------------ outlines */
void wa_path_init(WaPath *p) { memset(p, 0, sizeof *p); p->x0 = p->y0 = 0x7FFFFFFF; p->x1 = p->y1 = -0x7FFFFFFF; }
void wa_path_free(WaPath *p) { free(p->seg); wa_path_init(p); }
static void bound(WaPath *p, i32 x, i32 y)
{
  if (x < p->x0) p->x0 = x;
  if (x > p->x1) p->x1 = x;
  if (y < p->y0) p->y0 = y;
  if (y > p->y1) p->y1 = y;
}
static void add_seg(WaPath *p, i32 ax, i32 ay, i32 bx, i32 by)
{
  if (p->n >= p->cap) {
    int nc = p->cap ? p->cap * 2 : 1024;
    i32 *ns;
    if (nc > 4000000) { p->oom = 1; return; }
    if (!(ns = (i32 *)realloc(p->seg, (size_t)nc * 4 * sizeof(i32)))) { p->oom = 1; return; }
    p->seg = ns; p->cap = nc;
  }
  p->seg[4 * p->n] = ax; p->seg[4 * p->n + 1] = ay; p->seg[4 * p->n + 2] = bx; p->seg[4 * p->n + 3] = by;
  p->n++;
  bound(p, ax, ay); bound(p, bx, by);
}
static u32 upper(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) ? c - 32 : c; }
static int lower(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7); }
int wa_path_text(WaPath *p, FaceSize *z, const char *s, u32 n, i32 x, i32 base)
{
  long pen = (long)x * 16;
  u32 i = 0;
  int any = 0;
  i32 *seg;
  if (!z || !z->face || !(seg = (i32 *)malloc(4 * 4000 * sizeof(i32)))) return 0;
  while (i < n) {
    int used, k, ns;
    u32 cp = utf8_get(s + i, n - i, &used);
    i += (u32)used;
    if (cp != ' ') {
      Face *fc = z->face;
      long upm = fc->upm ? fc->upm : 2048, em = (long)z->size * 10 * WA_SUB;
      ns = math_glyph_segments(fc, ttf_glyph(fc, cp), seg, 4000);
      for (k = 0; k < ns; k++) {
        add_seg(p, (i32)(pen * WA_SUB / 16 + (long long)seg[4 * k] * em / upm), (i32)((long)base * WA_SUB - (long long)seg[4 * k + 1] * em / upm),
                (i32)(pen * WA_SUB / 16 + (long long)seg[4 * k + 2] * em / upm), (i32)((long)base * WA_SUB - (long long)seg[4 * k + 3] * em / upm));
        any = 1;
      }
    }
    pen += (long)fonts_fine(z, cp);
  }
  free(seg);
  return any;
}
int wa_path_run(WaPath *p, Layout *L, const CharFmt *f, const char *s, u32 n, i32 x, i32 base)
{
  FaceSize *z = lay_face(L, f), *zs = NULL;
  int small = (f->fx & FX_SMALLCAPS) && !(f->flags & CF_CAPS), any = 0;
  long scale = f->scale && f->scale != 100 ? f->scale : 100;
  long pen = (long)x * 16;                       /* fine units */
  u32 i = 0;
  i32 *seg;
  if (!z || !z->face) return 0;
  if (small) { CharFmt t = *f; t.size = (u16)((f->size * 4 + 2) / 5); zs = lay_face(L, &t); }
  if (f->flags & CF_SUPER) base -= f->size * 10 / 3;
  else if (f->flags & CF_SUB) base += f->size * 10 / 8;
  base -= f->position * 10;
  if (!(seg = (i32 *)malloc(4 * 4000 * sizeof(i32)))) { p->oom = 1; return 0; }
  if ((f->flags & CF_BOLD) && !z->face->bold) p->bold = z->size * 10 / 30 + 1;
  while (i < n) {
    int used, low = 0, k, ns;
    u32 cp = utf8_get(s + i, n - i, &used);
    FaceSize *zz;
    i += (u32)used;
    if (cp == 0xAD) continue;
    if (cp == 0x2011) cp = '-';
    if (cp == '\t' || cp == '\v' || cp >= 0xE000) cp = ' ';
    if (f->flags & CF_CAPS) cp = upper(cp);
    if (small && lower(cp)) { cp = upper(cp); low = 1; }
    zz = low && zs ? zs : z;
    if (cp != ' ') {
      Face *fc = zz->face;
      u16 g = ttf_glyph(fc, cp);
      long upm = fc->upm ? fc->upm : 2048, em = (long)zz->size * 10 * WA_SUB;      /* path units an em */
      int synth_i = (f->flags & CF_ITALIC) && !fc->italic;
      ns = math_glyph_segments(fc, g, seg, 4000);
      for (k = 0; k < ns; k++) {
        i32 pt[4];
        int e;
        for (e = 0; e < 2; e++) {
          long u = seg[4 * k + 2 * e], v = seg[4 * k + 2 * e + 1];
          long X = (long)((long long)u * em * scale / 100 / upm), Y = (long)((long long)v * em / upm);
          if (synth_i) X += Y * 21 / 100;
          pt[2 * e] = (i32)(pen * WA_SUB / 16 + X); pt[2 * e + 1] = (i32)((long)base * WA_SUB - Y);
        }
        add_seg(p, pt[0], pt[1], pt[2], pt[3]);
        any = 1;
      }
    }
    pen += (long)fonts_fine(zz, cp) * scale / 100 + (long)f->spacing * 16;
  }
  free(seg);
  return any;
}
void wa_path_warp(WaPath *p, int warp, const i32 *tb, i32 w, i32 h, int rot, i32 cx, i32 cy)
{
  WaPath q;
  long tw = (long)(tb[2] - tb[0]) * WA_SUB, th = (long)(tb[3] - tb[1]) * WA_SUB, step;
  int aspect, k;
  long c = icos(rot), sn = isin(rot);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  aspect = (int)(th * 1000 / tw);
  step = tw / 96 > WA_SUB * 4 ? tw / 96 : WA_SUB * 4;
  wa_path_init(&q);
  q.bold = p->bold;
  for (k = 0; k < p->n && !q.oom; k++) {
    long ax = p->seg[4 * k], ay = p->seg[4 * k + 1], bx = p->seg[4 * k + 2], by = p->seg[4 * k + 3];
    long dx = bx - ax, dy = by - ay, len = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    int m = (int)(len / step) + 1, j;
    i32 px = 0, py = 0;
    if (m > 64) m = 64;
    if (warp <= 0 || warp >= WW_N) m = 1;
    for (j = 0; j <= m; j++) {
      long X = ax + dx * j / m, Y = ay + dy * j / m;
      i32 u = (i32)(((long long)X - (long long)tb[0] * WA_SUB) * ONE / tw), v = (i32)(((long long)Y - (long long)tb[1] * WA_SUB) * ONE / th), mx, my;
      long rx, ry;
      if (warp > 0 && warp < WW_N) wa_warp_map(warp, w * WA_SUB, h * WA_SUB, aspect, u, v, &mx, &my);
      else { mx = (i32)((long long)w * WA_SUB * u / ONE); my = (i32)((long long)h * WA_SUB * v / ONE); }
      /* turned round the frame's centre */
      rx = mx - (long)w * WA_SUB / 2; ry = my - (long)h * WA_SUB / 2;
      mx = (i32)((long)cx * WA_SUB + (long)(((long long)rx * c - (long long)ry * sn) / TRIG_ONE));
      my = (i32)((long)cy * WA_SUB + (long)(((long long)rx * sn + (long long)ry * c) / TRIG_ONE));
      if (j) add_seg(&q, px, py, mx, my);
      px = mx; py = my;
    }
  }
  if (q.oom) { wa_path_free(&q); p->oom = 1; return; }
  free(p->seg);
  *p = q;
}

int wa_pieces(Layout *L, const Furn *f, WaPiece **out)
{
  WaPiece *pc = NULL;
  int n = 0, cap = 0;
  u32 j, l, k;
  *out = NULL;
  for (j = 0; j < f->n; j++) {
    const ParaLay *pl = &f->f[j].pl;
    i32 y = f->f[j].y;
    for (l = 0; l < pl->nlines; l++) {
      const Line *ln = &pl->lines[l];
      for (k = ln->box0; k < ln->box0 + ln->nbox && k < pl->nboxes; k++) {
        const Box *b = &pl->boxes[k];
        if (b->kind != BX_TEXT || (b->f.fx2 & FX2_HIDDEN)) continue;
        if (n >= cap) {
          int nc = cap ? cap * 2 : 16;
          WaPiece *np = (WaPiece *)realloc(pc, (size_t)nc * sizeof *np);
          if (!np) break;
          pc = np; cap = nc;
        }
        wa_path_init(&pc[n].p);
        pc[n].f = b->f;
        if (wa_path_run(&pc[n].p, L, &b->f, pl->p->t + b->off, b->len, ln->left + b->x, y + ln->ascent)) n++;
        else wa_path_free(&pc[n].p);
      }
      y += ln->height;
    }
  }
  *out = pc;
  if (!n) { free(pc); *out = NULL; }
  return n;
}
void wa_pieces_free(WaPiece *pc, int n)
{
  int k;
  for (k = 0; k < n; k++) wa_path_free(&pc[k].p);
  free(pc);
}
void wa_pieces_warp(WaPiece *pc, int n, int warp, i32 w, i32 h, int rot, i32 cx, i32 cy)
{
  i32 tb[4] = { 0x7FFFFFFF, 0x7FFFFFFF, -0x7FFFFFFF, -0x7FFFFFFF };
  int k;
  for (k = 0; k < n; k++) {
    if (pc[k].p.n <= 0) continue;
    if (pc[k].p.x0 / WA_SUB < tb[0]) tb[0] = pc[k].p.x0 / WA_SUB;
    if (pc[k].p.y0 / WA_SUB < tb[1]) tb[1] = pc[k].p.y0 / WA_SUB;
    if (pc[k].p.x1 / WA_SUB + 1 > tb[2]) tb[2] = pc[k].p.x1 / WA_SUB + 1;
    if (pc[k].p.y1 / WA_SUB + 1 > tb[3]) tb[3] = pc[k].p.y1 / WA_SUB + 1;
  }
  if (tb[0] >= tb[2]) return;
  for (k = 0; k < n; k++) wa_path_warp(&pc[k].p, warp, tb, w, h, rot, cx, cy);
}

/* ------------------------------------------------------------------ raster */
void wa_raster_free(WaRaster *r) { free(r->px); r->px = NULL; r->w = r->h = 0; }
typedef struct { long num, den; i32 ox, oy; } Xf;     /* path units to pixels x 16, less the raster's origin */
static i32 tx16(const Xf *t, i32 v, i32 o) { return (i32)((long long)v * t->num * 16 / ((long long)t->den * WA_SUB)) - o * 16; }
static int cmp_i32(const void *a, const void *b) { i32 x = *(const i32 *)a, y = *(const i32 *)b; return x < y ? -1 : x > y; }
/* segments s[0..n) (pixels x 16) filled into mask w x h with non-zero winding: four rows of samples
   a pixel, coverage across it exact to a sixteenth; maxop: kept where the mask has more */
static void fill_segs(u8 *mask, i32 w, i32 h, const i32 *s, int n, int maxop)
{
  i32 y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF, py, *xs = NULL, *ord = NULL;
  u16 *acc;
  int i, k, nact, xcap, *act = NULL, next = 0;
  if (n <= 0) return;
  for (i = 0; i < n; i++) {
    i32 a = s[4 * i + 1], b = s[4 * i + 3];
    if (a < y0) y0 = a;
    if (b < y0) y0 = b;
    if (a > y1) y1 = a;
    if (b > y1) y1 = b;
  }
  y0 = y0 < 0 ? 0 : y0 / 16; y1 = y1 / 16 + 1;
  if (y1 > h) y1 = h;
  if (y0 >= y1) return;
  acc = (u16 *)calloc((size_t)w + 1, sizeof(u16));
  ord = (i32 *)malloc((size_t)n * 2 * sizeof(i32));          /* (ymin, index) pairs */
  act = (int *)malloc((size_t)n * sizeof(int));
  xcap = 256; xs = (i32 *)malloc((size_t)xcap * sizeof(i32));
  if (!acc || !ord || !act || !xs) goto done;
  for (i = 0; i < n; i++) { i32 a = s[4 * i + 1], b = s[4 * i + 3]; ord[2 * i] = a < b ? a : b; ord[2 * i + 1] = i; }
  qsort(ord, (size_t)n, 2 * sizeof(i32), cmp_i32);
  nact = 0;
  for (py = y0; py < y1; py++) {
    int sub, any = 0;
    i32 lo = w, hi = -1;
    for (sub = 0; sub < 4; sub++) {
      i32 sy = py * 16 + 2 + sub * 4;
      int nx = 0, wind = 0;
      while (next < n && ord[2 * next] <= sy) act[nact++] = ord[2 * next++ + 1];
      for (k = 0; k < nact; ) {
        const i32 *g = s + 4 * act[k];
        i32 ay = g[1], by = g[3], ymax = ay > by ? ay : by;
        if (ymax <= sy) { act[k] = act[--nact]; continue; }
        if (ay != by && ((ay <= sy) != (by <= sy))) {
          i32 xx = g[0] + (i32)((long long)(g[2] - g[0]) * (sy - ay) / (by - ay));
          if (nx >= xcap) { i32 *nb = (i32 *)realloc(xs, (size_t)xcap * 2 * sizeof(i32)); if (!nb) break; xs = nb; xcap *= 2; }
          xs[nx++] = xx * 2 + (by > ay);
        }
        k++;
      }
      if (nx > 1) qsort(xs, (size_t)nx, sizeof(i32), cmp_i32);
      for (k = 0; k + 1 < nx; k++) {
        wind += xs[k] & 1 ? 1 : -1;
        if (wind) {
          i32 a = xs[k] >> 1, b = xs[k + 1] >> 1, pa, pb, q;
          if (a < 0) a = 0;
          if (b > w * 16) b = w * 16;
          if (b <= a) continue;
          pa = a >> 4; pb = (b - 1) >> 4;
          if (pa < lo) lo = pa;
          if (pb > hi) hi = pb;
          any = 1;
          if (pa == pb) { acc[pa] = (u16)(acc[pa] + (b - a)); continue; }
          acc[pa] = (u16)(acc[pa] + (16 - (a & 15)));
          for (q = pa + 1; q < pb; q++) acc[q] = (u16)(acc[q] + 16);
          acc[pb] = (u16)(acc[pb] + (b - pb * 16));
        }
      }
    }
    if (!any) continue;
    for (k = lo; k <= hi; k++) {
      u32 v = (u32)acc[k] * 255 / 64;
      u8 *m = mask + (size_t)py * w + k;
      if (v > 255) v = 255;
      if (!maxop || v > *m) *m = (u8)v;
      acc[k] = 0;
    }
  }
done:
  free(acc); free(ord); free(act); free(xs);
}
/* the path's segments in pixels x 16 for raster t (malloc'd) */
static i32 *segs16(const WaPath *p, const Xf *t)
{
  i32 *s = (i32 *)malloc((size_t)(p->n ? p->n : 1) * 4 * sizeof(i32));
  int k;
  if (!s) return NULL;
  for (k = 0; k < p->n; k++) {
    s[4 * k] = tx16(t, p->seg[4 * k], t->ox); s[4 * k + 1] = tx16(t, p->seg[4 * k + 1], t->oy);
    s[4 * k + 2] = tx16(t, p->seg[4 * k + 2], t->ox); s[4 * k + 3] = tx16(t, p->seg[4 * k + 3], t->oy);
  }
  return s;
}
static long isq(long v) { long r = 0, b = 1L << 30; if (v <= 0) return 0; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
/* the outline stroked lw16 wide (pixels x 16): each segment a band, round at its ends */
static void stroke_segs(u8 *mask, i32 w, i32 h, const i32 *s, int n, i32 lw16)
{
  int k, j;
  i32 hw = lw16 / 2 > 4 ? lw16 / 2 : 4;
  for (k = 0; k < n; k++) {
    i32 ax = s[4 * k], ay = s[4 * k + 1], bx = s[4 * k + 2], by = s[4 * k + 3], q[4 * 4], dx = bx - ax, dy = by - ay;
    long len = isq((long)dx * dx + (long)dy * dy), nx, ny;
    i32 pt[8];
    if (len) {
      nx = -(long)dy * hw / len; ny = (long)dx * hw / len;
      pt[0] = (i32)(ax + nx); pt[1] = (i32)(ay + ny); pt[2] = (i32)(bx + nx); pt[3] = (i32)(by + ny);
      pt[4] = (i32)(bx - nx); pt[5] = (i32)(by - ny); pt[6] = (i32)(ax - nx); pt[7] = (i32)(ay - ny);
      for (j = 0; j < 4; j++) { q[4 * j] = pt[2 * j]; q[4 * j + 1] = pt[2 * j + 1]; q[4 * j + 2] = pt[2 * ((j + 1) & 3)]; q[4 * j + 3] = pt[2 * ((j + 1) & 3) + 1]; }
      fill_segs(mask, w, h, q, 4, 1);
    }
    if (hw >= 12) {
      /* a round join: an octagon at the segment's start */
      static const int ox[8] = { 100, 71, 0, -71, -100, -71, 0, 71 }, oy[8] = { 0, 71, 100, 71, 0, -71, -100, -71 };
      i32 d[8 * 4];
      for (j = 0; j < 8; j++) {
        int j2 = (j + 1) & 7;
        d[4 * j] = ax + hw * ox[j] / 100; d[4 * j + 1] = ay + hw * oy[j] / 100; d[4 * j + 2] = ax + hw * ox[j2] / 100; d[4 * j + 3] = ay + hw * oy[j2] / 100;
      }
      fill_segs(mask, w, h, d, 8, 1);
    }
  }
}
/* a box blur of radius r, run twice, on a w x h mask */
static void blur(u8 *m, i32 w, i32 h, i32 r)
{
  u8 *t;
  i32 x, y, pass;
  if (r < 1) return;
  if (r > 200) r = 200;
  if (!(t = (u8 *)malloc((size_t)(w > h ? w : h)))) return;
  for (pass = 0; pass < 2; pass++) {
    for (y = 0; y < h; y++) {
      u8 *row = m + (size_t)y * w;
      long sum = 0, d = 2 * r + 1;
      for (x = -r; x <= r; x++) sum += row[x < 0 ? 0 : x >= w ? w - 1 : x] * (x >= 0 && x < w);
      for (x = 0; x < w; x++) {
        i32 a = x - r, b = x + r + 1;
        t[x] = (u8)(sum / d);
        if (b < w) sum += row[b];
        if (a >= 0) sum -= row[a];
      }
      memcpy(row, t, (size_t)w);
    }
    for (x = 0; x < w; x++) {
      long sum = 0, d = 2 * r + 1;
      for (y = 0; y <= r && y < h; y++) sum += m[(size_t)y * w + x];
      for (y = 0; y < h; y++) {
        i32 a = y - r, b = y + r + 1;
        t[y] = (u8)(sum / d);
        if (b < h) sum += m[(size_t)b * w + x];
        if (a >= 0) sum -= m[(size_t)a * w + x];
      }
      for (y = 0; y < h; y++) m[(size_t)y * w + x] = t[y];
    }
  }
  free(t);
}
/* a mask grown by r pixels (a square max filter, across then down) */
static void dilate(u8 *m, i32 w, i32 h, i32 r)
{
  u8 *t;
  i32 x, y, k;
  if (r < 1) return;
  if (r > 200) r = 200;
  if (!(t = (u8 *)malloc((size_t)(w > h ? w : h)))) return;
  for (y = 0; y < h; y++) {
    u8 *row = m + (size_t)y * w;
    for (x = 0; x < w; x++) { u8 v = 0; for (k = x - r; k <= x + r; k++) if (k >= 0 && k < w && row[k] > v) v = row[k]; t[x] = v; }
    memcpy(row, t, (size_t)w);
  }
  for (x = 0; x < w; x++) {
    for (y = 0; y < h; y++) { u8 v = 0; for (k = y - r; k <= y + r; k++) if (k >= 0 && k < h && m[(size_t)k * w + x] > v) v = m[(size_t)k * w + x]; t[y] = v; }
    for (y = 0; y < h; y++) m[(size_t)y * w + x] = t[y];
  }
  free(t);
}
/* colour c at coverage a (0-255) over pixel d (RGBA, straight alpha) */
static void over(u8 *d, u32 c, u32 a)
{
  u32 da = d[3], oa, k;
  if (!a) return;
  oa = a + da * (255 - a) / 255;
  for (k = 0; k < 3; k++) {
    u32 s = c >> (16 - 8 * k) & 255;
    d[k] = (u8)((s * a + d[k] * da * (255 - a) / 255) / (oa ? oa : 1));
  }
  d[3] = (u8)oa;
}
/* a gradient's colour and opacity (0-255) at t (0-1000) */
static u32 grad_at(const TextFx *fx, long t, u32 *op)
{
  int k, n = fx->ngs >= 2 && fx->ngs <= WA_MAXGS ? fx->ngs : 2;
  long p = t / 10;
  for (k = 0; k + 1 < n && p > fx->gpos[k + 1]; k++) ;
  if (k + 1 >= n || p <= fx->gpos[k]) {
    int j = p <= fx->gpos[0] ? 0 : n - 1;
    if (k + 1 < n && p <= fx->gpos[k]) j = k;
    *op = 255 - fx->galpha[j] * 255 / 100;
    return fx->gcol[j];
  } else {
    long span = fx->gpos[k + 1] - fx->gpos[k], f = span ? (t - fx->gpos[k] * 10) * 256 / (span * 10) : 0, s;
    u32 a = fx->gcol[k], b = fx->gcol[k + 1], o = 0;
    if (f < 0) f = 0;
    if (f > 256) f = 256;
    for (s = 16; s >= 0; s -= 8) { long x = a >> s & 255, y = b >> s & 255; o |= (u32)(x + (y - x) * f / 256) << s; }
    *op = (u32)(255 - (fx->galpha[k] + (fx->galpha[k + 1] - fx->galpha[k]) * f / 256) * 255 / 100);
    return o;
  }
}
static u32 opac(int alpha) { return (u32)(255 - (alpha < 0 ? 0 : alpha > 100 ? 100 : alpha) * 255 / 100); }
int wa_render(const WaPath *p, const TextFx *fx, u32 color, long num, long den, int layers, WaRaster *r)
{
  static const TextFx plain;
  Xf t;
  i32 bx0, by0, bx1, by1, m, mb, w, h, lw16 = 0, *s = NULL, bw, bh;
  u8 *fill = NULL, *ink = NULL, *tmp = NULL, *px;
  int hollow, ok = 0;
  long k, n;
  u32 fc;
  memset(r, 0, sizeof *r);
  if (!fx) fx = &plain;
  if (p->n <= 0 || p->oom || num <= 0 || den <= 0) return 0;
  if (color == COLOR_AUTO) color = 0;
#define PXV(v) ((i32)((long long)(v) * num / ((long long)den * WA_SUB)))
#define TW(v) ((i32)((long long)(v) * num / den))
  bx0 = PXV(p->x0) - 1; by0 = PXV(p->y0) - 1; bx1 = PXV(p->x1) + 2; by1 = PXV(p->y1) + 2;
  bw = bx1 - bx0; bh = by1 - by0;
  if (fx->line == WF_SOLID || p->bold) { lw16 = (i32)((long long)(fx->line == WF_SOLID ? fx->lw : 0) * num * 16 / den); if (lw16 < 12) lw16 = 12; }
  m = lw16 / 32 + 2;
  if ((layers & WL_SOFT) && fx->glow) m += TW(fx->glrad) + 2;
  if ((layers & WL_SOFT) && fx->shadow) { i32 sm = TW(fx->shdist) + TW(fx->shblur) + 2; if (sm > m) m = sm; }
  mb = m;
  if ((layers & WL_SOFT) && fx->refl) mb += TW(fx->rdist) + TW(fx->rblur) + (i32)((long)bh * fx->renpos / 100) + 2;
  w = bw + 2 * m; h = bh + m + mb;
  if (w <= 0 || h <= 0 || (long)w * h > 6000000L) return 0;
  t.num = num; t.den = den; t.ox = bx0 - m; t.oy = by0 - m;
  n = (long)w * h;
  px = (u8 *)calloc((size_t)n, 4);
  fill = (u8 *)calloc((size_t)n, 1); ink = (u8 *)calloc((size_t)n, 1); tmp = (u8 *)malloc((size_t)n);
  s = segs16(p, &t);
  if (!px || !fill || !ink || !tmp || !s) goto done;
  fill_segs(fill, w, h, s, p->n, 0);
  hollow = fx->fill == WF_NONE;
  /* the letters' ink: their fill, with their outline */
  if (!hollow) memcpy(ink, fill, (size_t)n);
  if (lw16) stroke_segs(ink, w, h, s, p->n, lw16);
  fc = fx->fill == WF_SOLID ? fx->fcol : fx->fill == WF_GRAD ? fx->gcol[0] : color;
  if (layers & WL_SOFT) {
    if (fx->refl) {
      /* mirrored about the letters' foot (dist below), fading from rsta to rena over renpos of their height */
      i32 foot = by1 - t.oy, gap = TW(fx->rdist), hh = (i32)((long)bh * (fx->renpos ? fx->renpos : 1) / 100), y, x;
      memset(tmp, 0, (size_t)n);
      for (y = 0; y < hh; y++) {
        i32 sy = foot - 1 - y, dy = foot + gap + y;
        long a = fx->rsta * 255L / 100 + (fx->rena - fx->rsta) * 255L / 100 * y / (hh ? hh : 1);
        if (sy < 0 || dy >= h) continue;
        for (x = 0; x < w; x++) tmp[(size_t)dy * w + x] = (u8)(ink[(size_t)sy * w + x] * a / 255);
      }
      blur(tmp, w, h, TW(fx->rblur) / 2);
      for (k = 0; k < n; k++) over(px + 4 * k, fc, tmp[k]);
    }
    if (fx->shadow) {
      i32 dx = (i32)((long)TW(fx->shdist) * icos(fx->shdir) / TRIG_ONE), dy = (i32)((long)TW(fx->shdist) * isin(fx->shdir) / TRIG_ONE), y, x;
      u32 op = opac(fx->shalpha);
      memset(tmp, 0, (size_t)n);
      for (y = 0; y < h; y++) {
        i32 sy = y - dy;
        if (sy < 0 || sy >= h) continue;
        for (x = 0; x < w; x++) { i32 sx = x - dx; if (sx >= 0 && sx < w) tmp[(size_t)y * w + x] = ink[(size_t)sy * w + sx]; }
      }
      blur(tmp, w, h, TW(fx->shblur) / 2);
      for (k = 0; k < n; k++) over(px + 4 * k, fx->shcol, tmp[k] * op / 255);
    }
    if (fx->glow && fx->glrad) {
      i32 rad = TW(fx->glrad);
      u32 op = opac(fx->glalpha);
      memcpy(tmp, ink, (size_t)n);
      dilate(tmp, w, h, rad / 2);
      blur(tmp, w, h, rad / 2 > 0 ? rad / 2 : 1);
      for (k = 0; k < n; k++) over(px + 4 * k, fx->glcol, tmp[k] * op / 255);
    }
  }
  if (layers & WL_LETTERS) {
    i32 y, x;
    if (fx->fill == WF_GRAD) {
      /* across the letters' box at the gradient's angle, or out from its centre */
      long c = icos(fx->gangle), sn = isin(fx->gangle), cx = (bx0 + bx1) / 2 - t.ox, cy = (by0 + by1) / 2 - t.oy;
      long d = ((long)bw * (c < 0 ? -c : c) + (long)bh * (sn < 0 ? -sn : sn)) / TRIG_ONE / 2, rad = isq((long)bw * bw + (long)bh * bh) / 2;
      if (d < 1) d = 1;
      if (rad < 1) rad = 1;
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
          u8 a = fill[(size_t)y * w + x];
          long tt;
          u32 col, op;
          if (!a) continue;
          if (fx->gpath) tt = isq((x - cx) * (x - cx) + (y - cy) * (y - cy)) * 1000 / rad;
          else tt = 500 + ((x - cx) * c + (y - cy) * sn) / TRIG_ONE * 500 / d;
          if (tt < 0) tt = 0;
          if (tt > 1000) tt = 1000;
          col = grad_at(fx, tt, &op);
          over(px + 4 * ((size_t)y * w + x), col, a * op / 255);
        }
    } else if (!hollow) {
      u32 op = fx->fill == WF_SOLID ? opac(fx->falpha) : 255;
      for (k = 0; k < n; k++) over(px + 4 * k, fc, fill[k] * op / 255);
    }
    if (fx->bevel && !hollow) {
      /* a light edge up and left, a dark one down and right, inside the letters */
      i32 d = (bh / 24) > 1 ? bh / 24 : 1;
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
          u8 a = fill[(size_t)y * w + x], up = (y >= d && x >= d) ? fill[(size_t)(y - d) * w + x - d] : 0, dn = (y + d < h && x + d < w) ? fill[(size_t)(y + d) * w + x + d] : 0;
          if (!a) continue;
          if (up < a) over(px + 4 * ((size_t)y * w + x), 0xFFFFFF, (u32)(a - up) * 150 / 255);
          if (dn < a) over(px + 4 * ((size_t)y * w + x), 0x000000, (u32)(a - dn) * 110 / 255);
        }
    }
    if (lw16) {
      /* the outline (or a synthetic bold's rim in the fill's colour) */
      u32 lc = fx->line == WF_SOLID ? fx->lcol : fc, op = fx->line == WF_SOLID ? opac(fx->lalpha) : 255;
      if (fx->line == WF_SOLID || fx->fill != WF_GRAD) {
        memset(tmp, 0, (size_t)n);
        stroke_segs(tmp, w, h, s, p->n, lw16);
        for (k = 0; k < n; k++) over(px + 4 * k, lc, tmp[k] * op / 255);
      }
    }
  }
  r->w = w; r->h = h; r->ox = t.ox; r->oy = t.oy; r->px = px; px = NULL;
  ok = 1;
done:
  free(px); free(fill); free(ink); free(tmp); free(s);
  return ok;
#undef PXV
#undef TW
}

/* ------------------------------------------------------------------ PDF */
static void pdf_rgb(Buf *b, u32 c, const char *op)
{
  buf_printf(b, "%d.%03d %d.%03d %d.%03d %s ", (int)((c >> 16) & 255) / 255, (int)(((c >> 16) & 255) * 1000 / 255) % 1000,
             (int)((c >> 8) & 255) / 255, (int)(((c >> 8) & 255) * 1000 / 255) % 1000, (int)(c & 255) / 255, (int)((c & 255) * 1000 / 255) % 1000, op);
}
/* c seen through alpha per cent of transparency over white (PDFs without transparency groups) */
static u32 on_white(u32 c, int alpha)
{
  u32 o = 0;
  int s;
  for (s = 16; s >= 0; s -= 8) { u32 v = c >> s & 255; o |= (v + (255 - v) * (u32)alpha / 100) << s; }
  return o;
}
static void pdf_path(Buf *b, const WaPath *p, i32 ox, i32 oy)
{
  int k;
  i32 lx = 0x7FFFFFFF, ly = 0;
  for (k = 0; k < p->n; k++) {
    i32 ax = p->seg[4 * k], ay = p->seg[4 * k + 1], bx = p->seg[4 * k + 2], by = p->seg[4 * k + 3];
    if (ax != lx || ay != ly) buf_printf(b, "%ld %ld m ", (long)ox + ax / WA_SUB, (long)oy - ay / WA_SUB);
    buf_printf(b, "%ld %ld l ", (long)ox + bx / WA_SUB, (long)oy - by / WA_SUB);
    lx = bx; ly = by;
  }
}
void wa_pdf_letters(Buf *b, const WaPath *p, const TextFx *fx, u32 color, i32 ox, i32 oy)
{
  u32 fc;
  if (p->n <= 0) return;
  if (color == COLOR_AUTO) color = 0;
  fc = fx->fill == WF_SOLID ? on_white(fx->fcol, fx->falpha) : fx->fill == WF_GRAD ? fx->gcol[0] : color;
  if (fx->fill == WF_GRAD) {
    /* the gradient as 64 bands across the letters' box, under the letters as a clip */
    long c = icos(fx->gangle), sn = isin(fx->gangle);
    i32 x0 = p->x0 / WA_SUB, y0 = p->y0 / WA_SUB, x1 = p->x1 / WA_SUB + 1, y1 = p->y1 / WA_SUB + 1, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    long d = ((long)(x1 - x0) * (c < 0 ? -c : c) + (long)(y1 - y0) * (sn < 0 ? -sn : sn)) / TRIG_ONE / 2 + 1, e = (long)(x1 - x0 + y1 - y0);
    int k;
    buf_adds(b, "q ");
    pdf_path(b, p, ox, oy);
    buf_adds(b, "W n\n");
    for (k = 0; k < 64; k++) {
      u32 op, col;
      long a0 = -d + 2 * d * k / 64, a1 = -d + 2 * d * (k + 1) / 64 + 1;
      if (fx->gpath) {
        /* rings out from the centre: squares shrinking (the last drawn the innermost) */
        long rr = (long)(64 - k) * (e / 2 + 1) / 64;
        col = grad_at(fx, (long)(64 - k) * 1000 / 64, &op);
        pdf_rgb(b, on_white(col, (int)(100 - op * 100 / 255)), "rg");
        buf_printf(b, "%ld %ld %ld %ld re f\n", (long)ox + cx - rr, (long)oy - cy - rr, 2 * rr, 2 * rr);
        continue;
      }
      col = grad_at(fx, (long)k * 1000 / 63, &op);
      pdf_rgb(b, on_white(col, (int)(100 - op * 100 / 255)), "rg");
      /* the band between a0 and a1 along (c, sn) from the centre, e long across */
      {
        long px = -sn, py = c, pts[8], j;
        long ux[4] = { a0, a1, a1, a0 }, vx[4] = { -e, -e, e, e };
        for (j = 0; j < 4; j++) {
          pts[2 * j] = cx + (ux[j] * c + vx[j] * px) / TRIG_ONE;
          pts[2 * j + 1] = cy + (ux[j] * sn + vx[j] * py) / TRIG_ONE;
        }
        buf_printf(b, "%ld %ld m %ld %ld l %ld %ld l %ld %ld l h f\n", (long)ox + pts[0], (long)oy - pts[1], (long)ox + pts[2], (long)oy - pts[3],
                   (long)ox + pts[4], (long)oy - pts[5], (long)ox + pts[6], (long)oy - pts[7]);
      }
    }
    buf_adds(b, "Q\n");
  } else if (fx->fill != WF_NONE) {
    pdf_rgb(b, fc, "rg");
    pdf_path(b, p, ox, oy);
    buf_adds(b, "f\n");
  }
  if (fx->line == WF_SOLID || (p->bold && fx->fill != WF_GRAD && fx->fill != WF_NONE)) {
    i32 lw = fx->line == WF_SOLID ? fx->lw : p->bold;
    pdf_rgb(b, fx->line == WF_SOLID ? on_white(fx->lcol, fx->lalpha) : fc, "RG");
    buf_printf(b, "%ld w 1 j 1 J ", (long)(lw > 2 ? lw : 2));
    pdf_path(b, p, ox, oy);
    buf_adds(b, "S\n");
  }
}

/* ------------------------------------------------------------------ .docx shapes */
int wa_read_shape(void *xv, Image *im)
{
  Xml *x = (Xml *)xv;
  if (strcmp(x->name, "prstTxWarp")) return 0;
  im->warp = (u8)wa_warp_from_prst(xml_attr(x, "prst"));
  xml_skip(x);
  return 1;
}
void wa_write_warp(Buf *o, const Image *im)
{
  const char *n = wa_warp_prst(im->warp);
  if (n) buf_printf(o, "<a:prstTxWarp prst=\"%s\"><a:avLst/></a:prstTxWarp>", n);
}
