/*
 * wp_picture.c - WP17 Pictures (docs/wordcraft-gap.md). This file belongs to WP17: only that
 * package edits it. Its commands are ID_WP17_FIRST .. ID_WP17_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Picture Format's Adjust group (Remove Background, Corrections, Color, Artistic Effects,
 * Transparency, Compress, Change and Reset Picture), its picture styles and effects, and cropping:
 * by dragging the handles, to a shape, to an aspect ratio. The adjustments themselves are
 * core/objfx.c's (Image.fx); the Format Picture pane is WP18's Format Shape pane.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#include "wp_objects.h"
#include "theme.h"
#include "lang.h"
#include "../core/objfx.h"

enum {
  ID_PIC_REMOVEBG = ID_WP17_FIRST, ID_PIC_CORRDD, ID_PIC_COLORDD, ID_PIC_ARTDD, ID_PIC_ALPHADD, ID_PIC_COMPRESS, ID_PIC_CHANGE, ID_PIC_RESETDD,
  ID_PIC_STYLEDD, ID_PIC_EFFECTSDD, ID_PIC_CROP, ID_PIC_CROPSHAPEDD, ID_PIC_ASPECTDD, ID_PIC_FORMAT, ID_PIC_COMPRESS_GO, ID_PIC_RESET, ID_PIC_RESETSIZE,
  ID_PIC_STYLE0, ID_PIC_STYLE_LAST = ID_PIC_STYLE0 + 13
};
enum { SB_BG = 1, SB_CROP };

static int cur_sel = -1, crop_on;
static int sel_obj(void)
{
  LONG k = -1;
  Doc *d = wp_doc();
  get(wp_view(), MUIA_Quill_Object, &k);
  return d && k >= 0 && k < d->nimages ? (int)k : -1;
}
static void obj_set(const Image *m) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)m); }
/* the selected picture into *m; 0 (and a beep) if no picture is selected */
static int sel_picture(Image *m, int *k)
{
  Doc *d = wp_doc();
  int s = sel_obj();
  if (!d || s < 0 || d->images[s].kind != OBJ_PICTURE) { DisplayBeep(NULL); return 0; }
  *m = d->images[s];
  if (k) *k = s;
  return 1;
}

/* ------------------------------------------------------------------ cropping by dragging */
int wp17_cropping(int k)
{
  Doc *d = wp_doc();
  return crop_on && d && k >= 0 && k < d->nimages && k == cur_sel && d->images[k].kind == OBJ_PICTURE;
}
/* the handles moved the box's sides in (or out) by so much: as much more (or less) of the whole picture cut off */
void wp17_crop_drop(Image *m, const FloatBox *f, i32 l, i32 t, i32 r, i32 b)
{
  long cl = m->crop[0], ct = m->crop[1], cr = m->crop[2], cb = m->crop[3], fw, fh, dl = l - f->x, dr = f->x + f->w - r, dt = t - f->y, db = f->y + f->h - b, x;
  if (cl + cr >= 99000) cl = cr = 0;
  if (ct + cb >= 99000) ct = cb = 0;
  fw = (long)m->cx * 100000L / (100000L - cl - cr); fh = (long)m->cy * 100000L / (100000L - ct - cb);
  if (fw < 1) fw = 1;
  if (fh < 1) fh = 1;
  if (m->flip & 1) { x = dl; dl = dr; dr = x; }
  if (m->flip & 2) { x = dt; dt = db; db = x; }
  cl += dl * 100000L / fw; cr += dr * 100000L / fw; ct += dt * 100000L / fh; cb += db * 100000L / fh;
  if (cl < 0) cl = 0;
  if (cr < 0) cr = 0;
  if (ct < 0) ct = 0;
  if (cb < 0) cb = 0;
  if (cl + cr > 98000) { cr = 98000 - cl; if (cr < 0) { cr = 0; cl = 98000; } }
  if (ct + cb > 98000) { cb = 98000 - ct; if (cb < 0) { cb = 0; ct = 98000; } }
  m->crop[0] = (i32)cl; m->crop[1] = (i32)ct; m->crop[2] = (i32)cr; m->crop[3] = (i32)cb;
  m->cx = (i32)(fw * (100000L - cl - cr) / 100000L); m->cy = (i32)(fh * (100000L - ct - cb) / 100000L);
  if (m->cx < 60) m->cx = 60;
  if (m->cy < 60) m->cy = 60;
}

/* ------------------------------------------------------------------ lists */
static const char *const corr_labels[] = { "Sharpen: 50%", "Sharpen: 25%", "Sharpen/Soften: 0%", "Soften: 25%", "Soften: 50%",
  "Brightness: -40%", "Brightness: -20%", "Brightness: 0% (Normal)", "Brightness: +20%", "Brightness: +40%",
  "Contrast: -40%", "Contrast: -20%", "Contrast: 0% (Normal)", "Contrast: +20%", "Contrast: +40%", "Picture Corrections Options..." };
static const RibbonList corr_list = { corr_labels, 16, PL_TEXT, NULL };
static const char *const color_labels[] = { "Saturation: 0%", "Saturation: 66%", "Saturation: 100%", "Saturation: 200%", "Saturation: 400%",
  "Temperature: 4700 K", "Temperature: 5900 K", "Temperature: 6500 K", "Temperature: 8800 K", "Temperature: 11200 K",
  "No Recolor", "Grayscale", "Sepia", "Washout", "Black and White: 50%", "Blue, Dark", "Orange, Dark", "Green, Dark",
  "Blue, Light", "Orange, Light", "Green, Light", "Set Transparent Color", "Picture Color Options..." };
static const RibbonList color_list = { color_labels, 23, PL_TEXT, NULL };
static const char *const art_labels[] = { "None", "Blur", "Pencil Grayscale", "Line Drawing", "Photocopy", "Cutout", "Film Grain", "Glow Edges",
  "Mosaic Bubbles", "Artistic Effects Options..." };
static const RibbonList art_list = { art_labels, 10, PL_TEXT, NULL };
static const char *const alpha_labels[] = { "Transparency: 0%", "Transparency: 15%", "Transparency: 30%", "Transparency: 50%", "Transparency: 65%",
  "Transparency: 80%", "Transparency: 95%", "Picture Transparency Options..." };
static const u8 alphas[7] = { 0, 15, 30, 50, 65, 80, 95 };
static const RibbonList alpha_list = { alpha_labels, 8, PL_TEXT, NULL };
static const char *const reset_labels[] = { "Reset Picture", "Reset Picture & Size" };
static const RibbonList reset_list = { reset_labels, 2, PL_TEXT, NULL };
static const char *const style_labels[] = { "Simple Frame, White", "Simple Frame, Black", "Thick Matte, Black", "Drop Shadow Rectangle",
  "Center Shadow Rectangle", "Rounded Rectangle", "Rounded Diagonal Corner, White", "Snip Diagonal Corner, White", "Oval, Black",
  "Soft Edge Rectangle", "Soft Edge Oval", "Metal Frame", "Rotated, White", "No Style" };
static const RibbonList style_list = { style_labels, 14, PL_TEXT, NULL };
static const char *const peff_labels[] = { "No Effects", "Shadow: Offset Bottom Right", "Shadow: Offset Bottom", "Shadow: Offset Center",
  "Glow: 5 pt", "Glow: 10 pt", "Glow: 18 pt", "Soft Edges: 2.5 pt", "Soft Edges: 5 pt", "Soft Edges: 10 pt", "Soft Edges: 25 pt", "Effects Options..." };
static const RibbonList peff_list = { peff_labels, 12, PL_TEXT, NULL };
static const char *const cshape_labels[] = { "Rectangle", "Rounded Rectangle", "Oval", "Triangle", "Diamond", "Hexagon", "Star", "Heart", "Cloud", "Teardrop" };
static const u8 cshapes[10] = { SH_RECT, SH_ROUNDRECT, SH_ELLIPSE, SH_TRIANGLE, SH_DIAMOND, SH_HEXAGON, SH_STAR, SH_HEART, SHX_CLOUD, SHX_TEARDROP };
static const RibbonList cshape_list = { cshape_labels, 10, PL_TEXT, NULL };
static const char *const aspect_labels[] = { "1:1", "2:3", "3:2", "3:4", "4:3", "16:9", "9:16" };
static const u8 aspects[7][2] = { { 1, 1 }, { 2, 3 }, { 3, 2 }, { 3, 4 }, { 4, 3 }, { 16, 9 }, { 9, 16 } };
static const RibbonList aspect_list = { aspect_labels, 7, PL_TEXT, NULL };

#define XI_REMOVEBG "ca F 2 2 13 13 cw E 8 8 4 cg P 6 4 6 6 4 10 4 12 6 12 10 10 12"
#define XI_CORR "ci E 8 8 6 cw Q 3 8 2 8 14 2 8 ca O 8 8 6"
#define XI_COLOR "cr F 1 1 7 7 cg F 8 1 14 7 ca F 1 8 7 14 cy F 8 8 14 14"
#define XI_ART "ci R 1 1 14 14 ca P 4 3 12 6 6 9 10 12 4 co E 11 4 2"
#define XI_ALPHA "ca F 1 1 9 9 cd F 6 6 14 14 ci R 6 6 14 14"
#define XI_COMPRESS "ci R 4 4 11 11 ca L 1 1 4 4 L 14 1 11 4 L 1 14 4 11 L 14 14 11 11"
#define XI_CHANGE "ci R 1 3 10 12 ca L 11 5 14 5 L 13 4 14 5 L 13 6 14 5 L 14 10 11 10 L 12 9 11 10 L 12 11 11 10"
#define XI_RESET "ci P 7 4 3 2 6 2 10 4 13 8 14 12 13 14 10 ca L 4 3 4 7 L 4 7 0 7"
#define XI_STYLES "cd F 4 4 14 14 cw F 2 2 12 12 ci R 2 2 12 12 ca F 4 4 10 10"
#define XI_PEFFECTS "cd E 9 9 5 ca E 7 7 5 cw E 6 6 2"
#define XI_CROPDRAG "ca F 5 5 11 11 ci F 3 1 4 12 F 3 11 15 12 F 1 3 12 4 F 11 3 12 15"
#define XI_CROPSHAPE "ci F 3 1 4 12 F 1 3 12 4 ca E 9 9 5"
#define XI_ASPECT "ci R 1 4 14 11 ca L 3 6 3 9 L 12 6 12 9 L 3 6 5 6 L 10 9 12 9"

static const Item p_adjust[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Remove Background", ID_PIC_REMOVEBG, RB_WP(WPI_17, SB_BG), RB_PICSEL, 0, 0, "The picture's background (the colour round its edges) made transparent, or back", NULL, XI_REMOVEBG },
  { K_DROP, S_SMALL, 0, 0, "Corrections", ID_PIC_CORRDD, 0, RB_PICSEL, F_NEWCOL, 0, "Sharper or softer, brighter or darker, more or less contrast", &corr_list, XI_CORR },
  { K_DROP, S_SMALL, 0, 0, "Color", ID_PIC_COLORDD, 0, RB_PICSEL, 0, 0, "Saturation, tone, or the picture in grey, sepia or another colour", &color_list, XI_COLOR },
  { K_DROP, S_SMALL, 0, 0, "Artistic Effects", ID_PIC_ARTDD, 0, RB_PICSEL, 0, 0, "The picture as a pencil sketch, a photocopy, blurred...", &art_list, XI_ART },
  { K_DROP, S_SMALL, 0, 0, "Transparency", ID_PIC_ALPHADD, 0, RB_PICSEL, F_NEWCOL, 0, "How much shows through the picture", &alpha_list, XI_ALPHA },
  { K_BUTTON, S_SMALL, 0, 0, "Compress Pictures", ID_PIC_COMPRESS, 0, RB_PICSEL, 0, 0, "Pictures made smaller: fewer pixels, cropped parts deleted", NULL, XI_COMPRESS },
  { K_BUTTON, S_SMALL, 0, 0, "Change Picture", ID_PIC_CHANGE, 0, RB_PICSEL, 0, 0, "Another picture in its place, keeping its size and formatting", NULL, XI_CHANGE },
  { K_DROP, S_SMALL, 0, 0, "Reset Picture", ID_PIC_RESETDD, 0, RB_PICSEL, F_SPLIT, 0, "The picture's formatting (and size) undone", &reset_list, XI_RESET, ID_PIC_RESET } };
static const Item p_styles[] = {
  { K_DROP, S_LARGE, 0, 0, "Picture Styles", ID_PIC_STYLEDD, 0, RB_PICSEL, 0, 0, "A frame, shadow, rounded corners or soft edges", &style_list, XI_STYLES },
  { K_DROP, S_SMALL, 0, 0, "Picture Effects", ID_PIC_EFFECTSDD, 0, RB_PICSEL, F_NEWCOL, 0, "A shadow, a glow or soft edges", &peff_list, XI_PEFFECTS },
  { K_BUTTON, S_SMALL, 0, 0, "Format Picture", ID_PIC_FORMAT, 0, RB_PICSEL, 0, 0, "The Format Picture pane: corrections, colour, effects, size", NULL, "ci R 1 1 14 14 L 10 1 10 14 ca F 11 3 13 4 F 11 6 13 7 F 11 9 13 10" } };
static const Item p_crop[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Crop by Dragging", ID_PIC_CROP, RB_WP(WPI_17, SB_CROP), RB_PICSEL, 0, 0, "The handles crop the picture instead of sizing it (click again to stop)", NULL, XI_CROPDRAG },
  { K_DROP, S_SMALL, 0, 0, "Crop to Shape", ID_PIC_CROPSHAPEDD, 0, RB_PICSEL, 0, 0, "The picture in the shape of an oval, a star, a heart...", &cshape_list, XI_CROPSHAPE },
  { K_DROP, S_SMALL, 0, 0, "Aspect Ratio", ID_PIC_ASPECTDD, 0, RB_PICSEL, 0, 0, "Cropped to a ratio of width to height, from the middle", &aspect_list, XI_ASPECT } };
static const WpGroup groups[] = {
  { TAB_PICTURE, 0, { "Adjust", G_COLS, p_adjust, 8 } },
  { TAB_PICTURE, 0, { "Styles", G_COLS, p_styles, 3 } },
  { TAB_PICTURE, 2, { "Crop", G_COLS, p_crop, 3 } } };
static const WpMenu menus[] = {
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Picture", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Format Picture...", 0, 0, 0, (APTR)ID_PIC_FORMAT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Remove Background", 0, 0, 0, (APTR)ID_PIC_REMOVEBG } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Compress Pictures...", 0, 0, 0, (APTR)ID_PIC_COMPRESS } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Change Picture...", 0, 0, 0, (APTR)ID_PIC_CHANGE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Reset Picture", 0, 0, 0, (APTR)ID_PIC_RESET } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Reset Picture & Size", 0, 0, 0, (APTR)ID_PIC_RESETSIZE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Crop by Dragging", 0, 0, 0, (APTR)ID_PIC_CROP } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Picture Styles", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Simple Frame, White", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 0) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Simple Frame, Black", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 1) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Thick Matte, Black", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 2) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Drop Shadow Rectangle", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 3) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Center Shadow Rectangle", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 4) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Rounded Rectangle", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 5) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Rounded Diagonal Corner, White", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 6) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Snip Diagonal Corner, White", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 7) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Oval, Black", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 8) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Soft Edge Rectangle", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 9) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Soft Edge Oval", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 10) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Metal Frame", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 11) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Rotated, White", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 12) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"No Style", 0, 0, 0, (APTR)(ID_PIC_STYLE0 + 13) } } };
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))

/* ------------------------------------------------------------------ commands */
static void style_preset(Image *m, int v)
{
  ObjFx *f = &m->fx;
  /* what a style sets, cleared first */
  m->shape = SH_RECT; m->lstyle = PB_NONE; f->shadow = 0; f->glow = 0; f->soft = 0;
  switch (v) {
  case 0: case 1: case 2: case 6: case 7: case 11: case 12:
    m->lstyle = PB_SINGLE; m->lwidth = (u8)(v == 2 ? 160 : v == 11 ? 56 : 64);
    m->lcolor = v == 1 || v == 2 ? 0x000000 : v == 11 ? 0xA5A5A5 : 0xFFFFFF;
    if (v == 6) m->shape = SHX_ROUND2DIAG;
    if (v == 7) m->shape = SHX_SNIP2DIAG;
    if (v != 1 && v != 2) { f->shadow = 1; f->shcolor = 0; f->shalpha = 65; f->shdist = 40; f->shdir = 450; f->shblur = 80; }
    if (v == 12) m->rot = (u16)((m->rot + 30) % 3600);
    break;
  case 3: f->shadow = 1; f->shcolor = 0; f->shalpha = 57; f->shdist = 80; f->shdir = 450; f->shblur = 120; break;
  case 4: f->shadow = 1; f->shcolor = 0; f->shalpha = 60; f->shdist = 0; f->shdir = 0; f->shblur = 240; break;
  case 5: m->shape = SH_ROUNDRECT; f->shadow = 1; f->shcolor = 0; f->shalpha = 65; f->shdist = 40; f->shdir = 450; f->shblur = 80; break;
  case 8: m->shape = SH_ELLIPSE; m->lstyle = PB_SINGLE; m->lwidth = 24; m->lcolor = 0x000000; break;
  case 9: f->soft = 140; break;
  case 10: m->shape = SH_ELLIPSE; f->soft = 200; break;
  }
}
static void reset_picture(int size)
{
  Image m;
  if (!sel_picture(&m, NULL)) return;
  memset(&m.fx, 0, sizeof m.fx);              /* (raw: the old entry keeps its own) */
  m.shape = SH_RECT; m.lstyle = PB_NONE;
  if (size) {
    memset(m.crop, 0, sizeof m.crop);
    m.rot = 0; m.flip = 0;
    m.cx = (i32)m.pw * 15; m.cy = (i32)m.ph * 15;
    if (m.cx > 9360) { m.cy = (i32)((long)m.cy * 9360 / m.cx); m.cx = 9360; }
  }
  obj_set(&m);
}
static void change_picture(void)
{
  Doc *d = wp_doc();
  Image m;
  int k;
  if (!sel_picture(&m, NULL)) return;
  if ((k = wp18_load_picture("Change Picture")) < 0) return;
  m.data = d->images[k].data; m.len = d->images[k].len; m.type = d->images[k].type; m.pw = d->images[k].pw; m.ph = d->images[k].ph; m.shared = 1;
  memset(m.crop, 0, sizeof m.crop);
  m.fx.bgkey = 0; m.fx.raw = NULL;
  if (m.pw) m.cy = (i32)((long)m.cx * m.ph / m.pw);      /* (its width kept) */
  obj_set(&m);
}
static void crop_aspect(int v)
{
  Image m;
  long cl, ct, cr, cb, fw, fh, want, have, cut;
  if (!sel_picture(&m, NULL)) return;
  cl = m.crop[0]; ct = m.crop[1]; cr = m.crop[2]; cb = m.crop[3];
  fw = (long)m.cx * 100000L / (100000L - cl - cr); fh = (long)m.cy * 100000L / (100000L - ct - cb);
  /* the shown width:height made a:b by cutting the longer way equally from both sides */
  want = (long)m.cx * aspects[v][1] / aspects[v][0];
  have = m.cy;
  if (want < have) { cut = (have - want) * 100000L / fh / 2; ct += cut; cb += cut; m.cy = (i32)want; }
  else if (want > have) { want = (long)m.cy * aspects[v][0] / aspects[v][1]; cut = ((long)m.cx - want) * 100000L / fw / 2; cl += cut; cr += cut; m.cx = (i32)want; }
  m.crop[0] = (i32)cl; m.crop[1] = (i32)ct; m.crop[2] = (i32)cr; m.crop[3] = (i32)cb;
  obj_set(&m);
}
/* Compress Pictures */
static Object *cpwin, *cp_res, *cp_crop, *cp_one;
static const char *cp_labels[4];
static const int cp_ppi[3] = { 220, 150, 96 };
static void open_compress(void)
{
  if (!cpwin) {
    Object *c;
    cp_labels[0] = tr("Print (220 ppi)"); cp_labels[1] = tr("Web (150 ppi)"); cp_labels[2] = tr("E-mail (96 ppi)"); cp_labels[3] = NULL;
    c = ColGroup(2),
      Child, QLabel(tr("Resolution"), MUIO_Label_DoubleFrame), Child, cp_res = CycleObject, MUIA_Cycle_Entries, (ULONG)cp_labels, MUIA_CycleChain, 1, End,
      Child, cp_crop = CheckMark(TRUE), Child, QLabel(tr("Delete cropped areas of pictures"), MUIO_Label_LeftAligned),
      Child, cp_one = CheckMark(TRUE), Child, QLabel(tr("Apply only to this picture"), MUIO_Label_LeftAligned),
    End;
    if (!(cpwin = wp_dialog("Compress Pictures", MAKE_ID('C','M','P','R'), c, "_OK", ID_PIC_COMPRESS_GO))) return;
  }
  set(cp_res, MUIA_Cycle_Active, 1);
  set(cpwin, MUIA_Window_Open, TRUE);
}
/* picture k (its reference selected) compressed; 1 if it was */
static int compress_one(int k, int ppi, int delcrop)
{
  Doc *d = wp_doc();
  Image m = d->images[k];
  u32 n;
  u8 *data = objfx_compress(&m, ppi, delcrop, &n);
  int nk;
  if (!data) return 0;
  if ((nk = doc_add_image(d, data, n, 0, 0)) < 0) return 0;
  m.data = d->images[nk].data; m.len = d->images[nk].len; m.type = d->images[nk].type; m.pw = d->images[nk].pw; m.ph = d->images[nk].ph; m.shared = 1;
  if (delcrop) memset(m.crop, 0, sizeof m.crop);
  m.fx.raw = NULL;
  obj_set(&m);
  return 1;
}
static void compress_go(void)
{
  Doc *d = wp_doc();
  ULONG res = 1, crop = 1, one = 1;
  int k = sel_obj(), n = 0;
  char msg[60];
  get(cp_res, MUIA_Cycle_Active, &res); get(cp_crop, MUIA_Selected, &crop); get(cp_one, MUIA_Selected, &one);
  set(cpwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  if (one) { if (k >= 0 && d->images[k].kind == OBJ_PICTURE) n = compress_one(k, cp_ppi[res % 3], (int)crop); }
  else {
    u32 i, j;
    for (i = 0; i < d->np; i++)
      for (j = 0; j + 4 <= d->p[i]->len; j++)
        if (IS_PIC(d->p[i]->t + j)) {
          int pk = pic_index(d->p[i]->t + j);
          Pos ab[2];
          if (pk < d->nimages && d->images[pk].kind == OBJ_PICTURE) {
            ab[0].para = i; ab[0].off = j; ab[1] = ab[0]; ab[1].off += 4;
            DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
            n += compress_one(pk, cp_ppi[res % 3], (int)crop);
          }
          j += 3;
        }
  }
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  sprintf(msg, "%d", n);
  wp_ask("OK", "Pictures compressed: %s", msg);
}
int wp17_command(ULONG id, LONG value)
{
  Doc *d = wp_doc();
  Image m;
  int k;
  if (!d) return WPR_DONE;
  switch (id) {
  case ID_PIC_REMOVEBG:
    if (!sel_picture(&m, NULL)) break;
    if (m.fx.bgkey) m.fx.bgkey = 0;
    else { m.fx.bgkey = 1; m.fx.bgtol = 25; m.fx.bgcolor = objfx_corner_colour(&m); }
    obj_set(&m);
    break;
  case ID_PIC_CORRDD:
    if (value == 15) { wp18_open_pane(4); break; }
    if (value < 0 || value > 14 || !sel_picture(&m, NULL)) break;
    if (value < 5) m.fx.sharp = (signed char)(50 - value * 25);
    else if (value < 10) m.fx.bright = (signed char)((value - 7) * 20);
    else m.fx.contrast = (signed char)((value - 12) * 20);
    obj_set(&m);
    break;
  case ID_PIC_COLORDD: {
    static const u16 sats[5] = { 1, 66, 0, 200, 400 }, temps[5] = { 4700, 5900, 0, 8800, 11200 };
    static const u32 recol[3] = { 0x4472C4, 0xED7D31, 0x70AD47 };
    if (value == 22) { wp18_open_pane(4); break; }
    if (value < 0 || value > 21 || !sel_picture(&m, NULL)) break;
    if (value < 5) m.fx.sat = sats[value];
    else if (value < 10) m.fx.temp = temps[value - 5];
    else if (value <= 14) m.fx.cmode = (u8)(value - 10);
    else if (value <= 20) { m.fx.cmode = (u8)(value <= 17 ? PCM_DARK : PCM_LIGHT); m.fx.recolor = recol[(value - 15) % 3]; }
    else { m.fx.bgkey = 1; m.fx.bgtol = 0; m.fx.bgcolor = objfx_corner_colour(&m); }
    obj_set(&m);
    break; }
  case ID_PIC_ARTDD:
    if (value == 9) { wp18_open_pane(4); break; }
    if (value < 0 || value >= ART_N || !sel_picture(&m, NULL)) break;
    m.fx.art = (u8)value; obj_set(&m);
    break;
  case ID_PIC_ALPHADD:
    if (value == 7) { wp18_open_pane(4); break; }
    if (value < 0 || value > 6 || !sel_picture(&m, NULL)) break;
    m.fx.alpha = alphas[value]; obj_set(&m);
    break;
  case ID_PIC_COMPRESS: if (sel_obj() < 0) { DisplayBeep(NULL); break; } open_compress(); return WPR_KEEPFOCUS;
  case ID_PIC_COMPRESS_GO: compress_go(); break;
  case ID_PIC_CHANGE: change_picture(); break;
  case ID_PIC_RESETDD: if (value == 0 || value == 1) reset_picture((int)value); break;
  case ID_PIC_RESET: reset_picture(0); break;
  case ID_PIC_RESETSIZE: reset_picture(1); break;
  case ID_PIC_STYLEDD:
    if (value < 0 || value > 13 || !sel_picture(&m, NULL)) break;
    style_preset(&m, (int)value); obj_set(&m);
    break;
  case ID_PIC_EFFECTSDD:
    if (value == 11) { wp18_open_pane(1); break; }
    if (value < 0 || value > 10 || !sel_picture(&m, NULL)) break;
    wp18_effect_preset(&m, (int)value); obj_set(&m);
    break;
  case ID_PIC_FORMAT: if (sel_obj() < 0) { DisplayBeep(NULL); break; } wp18_open_pane(4); break;
  case ID_PIC_CROP:
    if (!crop_on && !sel_picture(&m, &k)) break;
    crop_on = !crop_on; cur_sel = sel_obj();
    wp_refresh();
    break;
  case ID_PIC_CROPSHAPEDD:
    if (value < 0 || value > 9 || !sel_picture(&m, NULL)) break;
    m.shape = cshapes[value]; obj_set(&m);
    break;
  case ID_PIC_ASPECTDD: if (value >= 0 && value < 7) crop_aspect((int)value); break;
  default:
    if (id >= ID_PIC_STYLE0 && id <= ID_PIC_STYLE_LAST && sel_picture(&m, NULL)) { style_preset(&m, (int)(id - ID_PIC_STYLE0)); obj_set(&m); }
  }
  return WPR_DONE;
}

/* ------------------------------------------------------------------ hooks */
static void state(ULONG *on, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  cur_sel = c->obj;
  if (!im || im->kind != OBJ_PICTURE) crop_on = 0;
  if (im && im->kind == OBJ_PICTURE && im->fx.bgkey) *on |= RBIT(SB_BG);
  if (crop_on) *on |= RBIT(SB_CROP);
}
static void ctxmenu(Object *menu, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (c->where != WPC_OBJECT || !im || im->kind != OBJ_PICTURE) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, "Change Picture...", WP_CTX_ID(ID_PIC_CHANGE), 1);
  wp_ctx_item(menu, "Reset Picture", WP_CTX_ID(ID_PIC_RESET), 1);
  wp_ctx_item(menu, "Crop by Dragging", WP_CTX_ID(ID_PIC_CROP), 1);
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp17_package = {
  "WP17", ID_WP17_FIRST, ID_WP17_LAST,
  wp17_command, state, NULL, ctxmenu, NULL, NULL, NULL,
  groups, 3,
  menus, NMENUS
};
