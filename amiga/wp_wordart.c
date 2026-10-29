/*
 * wp_wordart.c - WP19 WordArt and signature line (docs/wordcraft-gap.md). This file belongs to WP19: only that
 * package edits it. Its commands are ID_WP19_FIRST .. ID_WP19_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Here: text with WordArt effects drawn on the screen (core/wordart.c's raster of the letters'
 * outlines, dithered to the view's colour cube, the last few kept); a text box's text bent by its
 * Transform; Insert > WordArt (a window of Quill's WordArt styles, each shown drawn); Shape Format's
 * WordArt Styles (Quick Styles, Text Fill, Text Outline, Text Effects, Transform), also as a window
 * from the Format menu; Insert > Signature Line and its setup window.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#include "wp_wordart.h"
#include "theme.h"
#include "lang.h"
#include "../core/wordart.h"
#include "../core/charfx.h"
#include "../core/objfx.h"

enum {
  ID_WA_INSERT = ID_WP19_FIRST, ID_WA_STYLEDD, ID_WA_FILLDD, ID_WA_LINEDD, ID_WA_EFFECTSDD, ID_WA_WARPDD, ID_WA_SIG, ID_WA_SIGOK,
  ID_WA_PICK, ID_WA_DIALOG, ID_WA_DLGOK
};
enum { SB_TEXTOBJ = 1, SB_SIG };

/* ------------------------------------------------------------------ drawing on the screen */
typedef struct { u32 key; i32 w, h, ox, oy; UBYTE *pen, *on; } WaCache;
#define NCACHE 24
static WaCache cache[NCACHE];
static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
static u32 fnv(u32 h, const void *p, size_t n) { const u8 *s = (const u8 *)p; while (n--) h = (h ^ *s++) * 16777619UL; return h; }
void wp19_flush(void)
{
  int i;
  for (i = 0; i < NCACHE; i++) { if (cache[i].pen) FreeVec(cache[i].pen); if (cache[i].on) FreeVec(cache[i].on); memset(&cache[i], 0, sizeof cache[i]); }
}
static WaCache *cache_find(u32 key)
{
  int i;
  for (i = 0; i < NCACHE; i++) if (cache[i].pen && cache[i].key == key) return &cache[i];
  return NULL;
}
/* r dithered to the cube's pens into a new cache entry (the oldest goes); NULL if out of memory */
static WaCache *cache_put(u32 key, const WaRaster *r, const LONG *cube, int lv)
{
  WaCache *c;
  LONG n = (LONG)r->w * r->h, row, col;
  if (cache[NCACHE - 1].pen) { FreeVec(cache[NCACHE - 1].pen); FreeVec(cache[NCACHE - 1].on); }
  memmove(cache + 1, cache, (NCACHE - 1) * sizeof cache[0]);
  c = &cache[0];
  memset(c, 0, sizeof *c);
  c->pen = (UBYTE *)AllocVec((ULONG)(n ? n : 1), MEMF_ANY); c->on = (UBYTE *)AllocVec((ULONG)(n ? n : 1), MEMF_ANY);
  if (!c->pen || !c->on) { if (c->pen) FreeVec(c->pen); if (c->on) FreeVec(c->on); c->pen = c->on = NULL; return NULL; }
  c->key = key; c->w = r->w; c->h = r->h; c->ox = r->ox; c->oy = r->oy;
  for (row = 0; row < r->h; row++)
    for (col = 0; col < r->w; col++) {
      const u8 *p = r->px + ((size_t)row * r->w + col) * 4;
      int tt = bayer[row & 3][col & 3] * 255 / 16, q[3], j;
      c->on[row * r->w + col] = (UBYTE)(p[3] > (bayer[(row + 2) & 3][(col + 1) & 3] * 16 + 8));
      for (j = 0; j < 3; j++) { q[j] = (p[j] * (lv - 1) + tt) / 255; if (q[j] > lv - 1) q[j] = lv - 1; }
      c->pen[row * r->w + col] = (UBYTE)cube[(q[0] * lv + q[1]) * lv + q[2]];
    }
  return c;
}
/* an entry's pixels that show, with its origin at x, y */
static void blit(struct RastPort *rp, const WaCache *c, LONG x, LONG y, LONG band0, LONG band1)
{
  LONG bw = GetBitMapAttr(rp->BitMap, BMA_WIDTH), bh = GetBitMapAttr(rp->BitMap, BMA_HEIGHT), row, col, run;
  UBYTE *line;
  if (!(line = (UBYTE *)AllocVec((ULONG)c->w + 1, MEMF_ANY))) return;
  x += c->ox; y += c->oy;
  for (row = 0; row < c->h; row++) {
    LONG yy = y + row;
    if (yy < 0 || yy >= bh || yy < band0 || (band1 && yy > band1)) continue;
    run = -1;
    for (col = 0; col <= c->w; col++) {
      int on = col < c->w && c->on[row * c->w + col] && x + col >= 0 && x + col < bw;
      if (on && run < 0) run = col;
      if (!on && run >= 0) {
        memcpy(line, c->pen + row * c->w + run, (size_t)(col - run));
        WriteChunkyPixels(rp, (ULONG)(x + run), (ULONG)yy, (ULONG)(x + col - 1), (ULONG)yy, line, col - run);
        run = -1;
      }
    }
  }
  FreeVec(line);
}
int wp19_draw_run(struct RastPort *rp, Layout *L, const Box *b, const char *t, LONG x, LONG base, LONG zoom,
                  const LONG *cube, int lv, LONG band0, LONG band1)
{
  const TextFx *fx = b->f.rx ? wa_get(b->f.rx) : NULL;
  u32 key = 2166136261UL;
  WaCache *c;
  if (!fx || lv < 2 || !L) return 0;
  key = fnv(key, &b->f.rx, sizeof b->f.rx); key = fnv(key, &b->f.font, 1); key = fnv(key, &b->f.size, sizeof b->f.size);
  key = fnv(key, &b->f.flags, 1); key = fnv(key, &b->f.fx, 1); key = fnv(key, &b->f.spacing, sizeof b->f.spacing);
  key = fnv(key, &b->f.scale, sizeof b->f.scale); key = fnv(key, &b->f.position, sizeof b->f.position); key = fnv(key, &b->f.color, sizeof b->f.color);
  key = fnv(key, &zoom, sizeof zoom); key = fnv(key, t + b->off, b->len);
  if (!(c = cache_find(key))) {
    WaPath p;
    WaRaster r;
    wa_path_init(&p);
    if (!wa_path_run(&p, L, &b->f, t + b->off, b->len, 0, 0)) { wa_path_free(&p); return 1; }     /* (spaces: nothing to draw) */
    if (!wa_render(&p, fx, b->f.color, zoom, 2000, WL_ALL, &r)) { wa_path_free(&p); return 0; }
    wa_path_free(&p);
    c = cache_put(key, &r, cube, lv);
    wa_raster_free(&r);
    if (!c) return 0;
  }
  blit(rp, c, x, base, band0, band1);
  return 1;
}
/* RGBA s over d (both straight alpha) */
static void over_px(u8 *d, const u8 *s)
{
  u32 a = s[3], da = d[3], oa, k;
  if (!a) return;
  oa = a + da * (255 - a) / 255;
  for (k = 0; k < 3; k++) d[k] = (u8)((s[k] * a + d[k] * da * (255 - a) / 255) / (oa ? oa : 1));
  d[3] = (u8)oa;
}
int wp19_draw_warped(struct RastPort *rp, Layout *L, const Image *src, const Image *t, i32 cx, i32 cy, LONG pl, LONG pt, LONG zoom,
                     const LONG *cube, int lv, LONG band0, LONG band1)
{
  u32 key = 2166136261UL, j;
  WaCache *c;
  if (!src->warp || src->warp >= WW_N || lv < 2 || !L || !src->text) return 0;
  key = fnv(key, &src->warp, 1); key = fnv(key, &t->cx, sizeof t->cx); key = fnv(key, &t->cy, sizeof t->cy); key = fnv(key, &t->rot, sizeof t->rot);
  key = fnv(key, &zoom, sizeof zoom); key = fnv(key, &src->fx.ins, sizeof src->fx.ins); key = fnv(key, &src->text, sizeof src->text);
  for (j = 0; j < src->text->np; j++) { key = fnv(key, &src->text->p[j], sizeof(Para *)); key = fnv(key, &src->text->p[j]->ver, sizeof(u32)); }
  if (!(c = cache_find(key))) {
    Furn f;
    WaPiece *pc = NULL;
    WaRaster *rs;
    WaRaster all;
    int n = 0, k;
    i32 l = tb_inset(src, 0), tp = tb_inset(src, 1), r = tb_inset(src, 2), bt = tb_inset(src, 3), fw = t->cx - l - r, fh = t->cy - tp - bt;
    long dx = (l - r) / 2, dy = (tp - bt) / 2, co = icos(t->rot), sn = isin(t->rot);
    i32 x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF;
    if (fw <= 0 || fh <= 0) return 0;
    if (lay_textbox(L, src, t->cx, &f)) n = wa_pieces(L, &f, &pc);
    lay_furniture_free(&f);
    if (!n) return 1;
    /* bent onto the frame inside the insets, turned with the box, round the box's centre (0, 0) */
    wa_pieces_warp(pc, n, src->warp, fw, fh, t->rot, (i32)((dx * co - dy * sn) / TRIG_ONE), (i32)((dx * sn + dy * co) / TRIG_ONE));
    if (!(rs = (WaRaster *)calloc((size_t)n, sizeof *rs))) { wa_pieces_free(pc, n); return 0; }
    for (k = 0; k < n; k++)
      if (wa_render(&pc[k].p, pc[k].f.rx ? wa_get(pc[k].f.rx) : NULL, pc[k].f.color, zoom, 2000, WL_ALL, &rs[k])) {
        if (rs[k].ox < x0) x0 = rs[k].ox;
        if (rs[k].oy < y0) y0 = rs[k].oy;
        if (rs[k].ox + rs[k].w > x1) x1 = rs[k].ox + rs[k].w;
        if (rs[k].oy + rs[k].h > y1) y1 = rs[k].oy + rs[k].h;
      }
    wa_pieces_free(pc, n);
    memset(&all, 0, sizeof all);
    if (x0 < x1 && (long)(x1 - x0) * (y1 - y0) < 4000000L && (all.px = (u8 *)calloc((size_t)(x1 - x0) * (y1 - y0), 4))) {
      LONG yy, xx;
      all.w = x1 - x0; all.h = y1 - y0; all.ox = x0; all.oy = y0;
      for (k = 0; k < n; k++) {
        if (!rs[k].px) continue;
        for (yy = 0; yy < rs[k].h; yy++)
          for (xx = 0; xx < rs[k].w; xx++)
            over_px(all.px + ((size_t)(rs[k].oy - y0 + yy) * all.w + rs[k].ox - x0 + xx) * 4, rs[k].px + ((size_t)yy * rs[k].w + xx) * 4);
      }
    }
    for (k = 0; k < n; k++) wa_raster_free(&rs[k]);
    free(rs);
    if (!all.px) return 0;
    c = cache_put(key, &all, cube, lv);
    wa_raster_free(&all);
    if (!c) return 0;
  }
  blit(rp, c, pl + (LONG)((cx * zoom) / 2000), pt + (LONG)((cy * zoom) / 2000), band0, band1);
  return 1;
}

/* ------------------------------------------------------------------ the document */
static void beep(void) { DisplayBeep(NULL); }
static int sel_obj(void)
{
  LONG k = -1;
  Doc *d = wp_doc();
  get(wp_view(), MUIA_Quill_Object, &k);
  return d && k >= 0 && k < d->nimages ? (int)k : -1;
}
static void obj_set(const Image *m) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)m); }
static Layout *layout(void) { Layout *L = NULL; get(wp_view(), MUIA_Quill_LayoutNow, &L); return L; }
static void sel_range(Pos *a, Pos *b)
{
  Pos *c = NULL, *an = NULL;
  get(wp_view(), MUIA_Quill_Caret, &c); get(wp_view(), MUIA_Quill_Anchor, &an);
  *a = c ? *c : doc_start(wp_doc()); *b = an ? *an : *a;
  if (pos_cmp(*a, *b) > 0) { Pos t = *a; *a = *b; *b = t; }
}
typedef void (*FxFn)(TextFx *fx, int v);
static void fx_style(TextFx *fx, int v) { wa_style(v, fx); }
static void fx_effect(TextFx *fx, int v)
{
  if (v < WA_NSHADOWS) wa_shadow_preset(v, fx);
  else if ((v -= WA_NSHADOWS) < WA_NREFLS) wa_refl_preset(v, fx);
  else if ((v -= WA_NREFLS) < WA_NGLOWS) wa_glow_preset(v, fx);
  else wa_bevel_preset(v - WA_NGLOWS, fx);
}
static unsigned effect_part(int v)
{
  return v < WA_NSHADOWS ? WA_SHADOW : v < WA_NSHADOWS + WA_NREFLS ? WA_REFL : v < WA_NSHADOWS + WA_NREFLS + WA_NGLOWS ? WA_GLOW : WA_3D;
}
static u16 fx_rx(u16 rx, FxFn fn, int v, unsigned which)
{
  TextFx t;
  const TextFx *c = wa_get(rx);
  if (c) t = *c; else memset(&t, 0, sizeof t);
  fn(&t, v);
  return wa_set(rx, &t, which);
}
/* fn's change (which: its WA_ parts) made to the selected shape's text, all of it, or else to the
   selected text */
static void apply(FxFn fn, int v, unsigned which)
{
  Doc *d = wp_doc();
  int k = sel_obj();
  Pos a, b;
  if (!d) return;
  if (k >= 0) {
    Image m;
    u32 i, r;
    if (!d->images[k].text || !d->images[k].text->np) { beep(); return; }
    if (!img_clone(&m, &d->images[k], 0)) { beep(); return; }
    for (i = 0; i < m.text->np; i++) {
      Para *p = m.text->p[i];
      for (r = 0; r < p->nr; r++) p->r[r].f.rx = fx_rx(p->r[r].f.rx, fn, v, which);
      p->ver++;
    }
    obj_set(&m);
    img_release(&m);
    return;
  }
  sel_range(&a, &b);
  if (!pos_cmp(a, b)) { beep(); return; }
  doc_begin(d, a);
  {
    u32 pi;
    for (pi = a.para; pi <= b.para && pi < d->np; pi++) {
      u32 off = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : d->p[pi]->len;
      while (off < e) {
        Para *p = d->p[pi];
        u32 r = 0, re;
        CharFmt cv;
        Pos A, B;
        while (r + 1 < p->nr && p->r[r].end <= off) r++;
        re = p->r[r].end < e ? p->r[r].end : e;
        if (re <= off) break;
        cv = p->r[r].f;
        cv.rx = fx_rx(cv.rx, fn, v, which);
        A.para = B.para = pi; A.off = off; B.off = re;
        doc_format(d, A, B, CM_RX, &cv);
        off = re;
      }
    }
  }
  doc_end(d, b);
  DoMethod(wp_view(), MUIM_Quill_Relayout);
}
static void fx_fill(TextFx *fx, int v) { wa_fill_preset(v, fx); }
static void fx_line(TextFx *fx, int v) { wa_line_preset(v, fx); }
static void transform(int w)
{
  Doc *d = wp_doc();
  int k = sel_obj();
  Image m;
  if (!d || k < 0 || !d->images[k].text || w < 0 || w >= WW_N) { beep(); return; }
  m = d->images[k];
  m.warp = (u8)w;
  obj_set(&m);
}

/* ------------------------------------------------------------------ ribbon lists */
static const char *const style_labels[WA_NSTYLES] = { "Fill: Black, Shadow", "Fill: Blue", "Fill: White, Outline: Blue, Glow", "Outline: Blue, Hollow",
  "Gradient: Blue", "Gradient: Gold, Outline: Brown", "Fill: Orange, Reflection", "Fill: Grey, Bevel", "Fill: Red, Outline: Black",
  "Fill: Green, Shadow", "Gradient: Sunset, Glow", "Fill: Navy, Reflection, Bevel" };
static const RibbonList style_list = { style_labels, WA_NSTYLES, PL_TEXT, NULL };
static const char *const fill_labels[WA_NFILLS] = { "No Fill", "Black", "White", "Blue", "Orange", "Grey", "Gold", "Green", "Red",
  "Gradient: Blue", "Gradient: Gold", "Gradient: Silver", "Gradient: Sunset", "Gradient: From Center" };
static const ULONG fill_cols[WA_NFILLS] = { 0xFF000000, 0x000000, 0xFFFFFF, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x70AD47, 0xC00000,
  0x4472C4, 0xFFC000, 0xBFBFBF, 0xFF6600, 0x5B9BD5 };
static const RibbonList fill_list = { fill_labels, WA_NFILLS, PL_COLOR, fill_cols };
static const char *const line_labels[WA_NLINES] = { "No Outline", "Black", "White", "Blue", "Orange", "Grey", "Gold", "Green", "Red",
  "Weight: 0.5 pt", "Weight: 1 pt", "Weight: 2.25 pt" };
static const ULONG line_cols[WA_NLINES] = { 0xFF000000, 0x000000, 0xFFFFFF, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x70AD47, 0xC00000,
  0xFF000000, 0xFF000000, 0xFF000000 };
static const RibbonList line_list = { line_labels, WA_NLINES, PL_COLOR, line_cols };
#define NEFFECTS (WA_NSHADOWS + WA_NREFLS + WA_NGLOWS + WA_NBEVELS)
static const char *const effect_labels[NEFFECTS] = {
  "No Shadow", "Offset: Bottom Right", "Offset: Bottom", "Offset: Top Left", "Offset: Center", "Inside Far",
  "No Reflection", "Tight Reflection: Touching", "Half Reflection: 4 pt Offset", "Full Reflection: 8 pt Offset",
  "No Glow", "Glow: 5 pt; Blue", "Glow: 8 pt; Orange", "Glow: 11 pt; Gold", "Glow: 18 pt; Green", "Glow: 5 pt; Grey",
  "No Bevel", "Bevel: Circle", "Bevel: Relaxed Inset", "Bevel: Slope", "Bevel: Angle" };
static const RibbonList effect_list = { effect_labels, NEFFECTS, PL_TEXT, NULL };
static const char *const warp_labels[WW_N] = { "No Transform", "Arch Up", "Arch Down", "Circle", "Wave", "Slant Up", "Slant Down", "Inflate",
  "Deflate", "Chevron Up", "Chevron Down", "Triangle Up", "Triangle Down" };
static const RibbonList warp_list = { warp_labels, WW_N, PL_TEXT, NULL };

/* the icons (theme.c's language): Quill's own */
#define XI_WORDART "ca P 3 2 14 8 2 14 14 L 5 10 11 10 co P 4 1 15 5 13 11 14 15 12"
#define XI_SIGLINE "cd L 2 5 6 9 L 6 5 2 9 ci L 1 11 15 11 ca P 5 7 9 9 6 11 9 13 6 15 8"
#define XI_STYLES "ca B A co F 2 13 13 14"
#define XI_FILL "ci B A cr F 1 13 14 15"
#define XI_OUTLINE "ci B A ca R 1 12 14 15"
#define XI_EFFECTS "cy O 8 8 7 ca B A"
#define XI_WARP "ca P 5 1 12 4 8 8 6 12 8 15 12 cd P 5 1 15 4 11 8 9 12 11 15 15"

static const Item i_wordart[] = {
  { K_BUTTON, S_SMALL, 0, 0, "WordArt", ID_WA_INSERT, 0, 0, 0, 0, "Decorative text in a box: pick a style, then type", NULL, XI_WORDART },
  { K_BUTTON, S_SMALL, 0, 0, "Signature Line", ID_WA_SIG, 0, 0, 0, 0, "A line for someone to sign, with their name and title under it", NULL, XI_SIGLINE } };
static const Item s_wordart[] = {
  { K_DROP, S_SMALL, 0, 0, "Quick Styles", ID_WA_STYLEDD, 0, 0, 0, 0, "A WordArt style for the shape's text (or the selected text)", &style_list, XI_STYLES },
  { K_DROP, S_SMALL, 0, 0, "Text Fill", ID_WA_FILLDD, 0, 0, 0, 0, "The letters filled with a colour, a gradient or nothing", &fill_list, XI_FILL },
  { K_DROP, S_SMALL, 0, 0, "Text Outline", ID_WA_LINEDD, 0, 0, 0, 0, "A line round the letters: its colour and weight", &line_list, XI_OUTLINE },
  { K_DROP, S_SMALL, 0, 0, "Text Effects", ID_WA_EFFECTSDD, 0, 0, F_NEWCOL, 0, "A shadow, reflection, glow or bevel on the letters", &effect_list, XI_EFFECTS },
  { K_DROP, S_SMALL, 0, 0, "Transform", ID_WA_WARPDD, 0, RB_WP(WPI_19, SB_TEXTOBJ), 0, 0, "The shape's text bent: an arch, a circle, a wave...", &warp_list, XI_WARP } };
static const WpGroup groups[] = {
  { TAB_INSERT, -1, { "WordArt", G_COLS, i_wordart, 2 } },
  { TAB_SHAPE, 2, { "WordArt Styles", G_COLS, s_wordart, 5 } } };
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"WordArt...", 0, 0, 0, (APTR)ID_WA_INSERT } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Signature Line...", 0, 0, 0, (APTR)ID_WA_SIG } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"WordArt Styles...", 0, 0, 0, (APTR)ID_WA_DIALOG } } };
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))

/* ------------------------------------------------------------------ Insert WordArt: a window of styles */
/* a cell: a style's "A" drawn as it looks; pressing it notes which (picked) before MUI returns the id */
#define MUIA_WaCell_Which (QTAG + 0x1990)
struct CellData { int which; };
static struct MUI_CustomClass *cellcls;
static int picked = -1;
static LONG ppen[64]; static int npen, live; static struct ColorMap *pcm;   /* a 4-level cube of pens for the cells */
static void cell_pens(Object *obj)
{
  int r, g, b;
  if (npen) return;
  pcm = _screen(obj)->ViewPort.ColorMap;
  for (r = 0; r < 4; r++) for (g = 0; g < 4; g++) for (b = 0; b < 4; b++)
    ppen[npen++] = ObtainBestPen(pcm, (ULONG)(r * 85) * 0x01010101UL, (ULONG)(g * 85) * 0x01010101UL, (ULONG)(b * 85) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
static void draw_cell(Object *obj, struct RastPort *rp, int which, LONG x0, LONG y0, LONG s, int sel)
{
  Layout *L = layout();
  FaceSize *z;
  WaPath p;
  WaRaster r;
  TextFx fx;
  theme_fill(rp, x0, y0, x0 + s - 1, y0 + s - 1, tpen(sel ? T_CHECKED : T_WHITE));
  theme_rect(rp, x0, y0, x0 + s - 1, y0 + s - 1, tpen(T_BORDER2));
  if (!L || !(z = fonts_size(L->fs, "Calibri", 1, 0, (u16)(s * 3 / 2)))) return;
  cell_pens(obj);
  wa_style(which, &fx);
  wa_path_init(&p);
  if (wa_path_text(&p, z, "A", 1, 0, 0) && wa_render(&p, &fx, 0, 1, 10, WL_ALL, &r)) {
    LONG x, y, cx = x0 + (s - (p.x1 - p.x0) / WA_SUB / 10) / 2 - p.x0 / WA_SUB / 10, by = y0 + s * 3 / 4;
    for (y = 0; y < r.h; y++)
      for (x = 0; x < r.w; x++) {
        const u8 *q = r.px + ((size_t)y * r.w + x) * 4;
        LONG X = cx + r.ox + x, Y = by + r.oy + y;
        int c[3], j, tt = bayer[y & 3][x & 3] * 85 / 16;
        if (q[3] < 24 || X <= x0 || Y <= y0 || X >= x0 + s - 1 || Y >= y0 + s - 1) continue;
        for (j = 0; j < 3; j++) { c[j] = (q[j] * q[3] + 255 * (255 - q[3])) / 255; c[j] = (c[j] * 3 + tt) / 255; if (c[j] > 3) c[j] = 3; }
        SetAPen(rp, (ULONG)ppen[(c[0] * 4 + c[1]) * 4 + c[2]]);
        WritePixel(rp, X, Y);
      }
    wa_raster_free(&r);
  }
  wa_path_free(&p);
}
static ULONG cdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct CellData *c;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    c = INST_DATA(cl, obj);
    c->which = (int)GetTagData(MUIA_WaCell_Which, 0, ((struct opSet *)msg)->ops_AttrList);
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Pressed, ((struct opSet *)msg)->ops_AttrList);
    c = INST_DATA(cl, obj);
    if (t && !t->ti_Data) picked = c->which;
    break; }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 44; m->MinMaxInfo->DefWidth += 44; m->MinMaxInfo->MaxWidth += 44;
    m->MinMaxInfo->MinHeight += 44; m->MinMaxInfo->DefHeight += 44; m->MinMaxInfo->MaxHeight += 44;
    return 0; }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    live++;
    return TRUE;
  case MUIM_Cleanup:
    /* the last cell gone: its pens back (the screen may close) */
    if (--live <= 0) { int i; live = 0; for (i = 0; i < npen; i++) ReleasePen(pcm, (ULONG)ppen[i]); npen = 0; }
    break;
  case MUIM_Draw: {
    ULONG sel = 0;
    DoSuperMethodA(cl, obj, msg);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    get(obj, MUIA_Selected, &sel);
    c = INST_DATA(cl, obj);
    draw_cell(obj, _rp(obj), c->which, _mleft(obj), _mtop(obj), _mwidth(obj) < _mheight(obj) ? _mwidth(obj) : _mheight(obj), (int)sel);
    return 0; }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static Object *insertwin, *ins_text;
static void open_insert(void)
{
  if (!insertwin) {
    Object *grid, *o;
    int i;
    if (!cellcls && !(cellcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct CellData), (APTR)cdispatch))) { beep(); return; }
    grid = ColGroup(6), MUIA_Group_Spacing, 3, End;
    if (!grid) { beep(); return; }
    for (i = 0; i < WA_NSTYLES; i++) {
      o = (Object *)NewObject(cellcls->mcc_Class, NULL, MUIA_WaCell_Which, i, MUIA_InputMode, MUIV_InputMode_RelVerify, MUIA_ShortHelp, (ULONG)tr(style_labels[i]),
                              MUIA_CycleChain, 1, TAG_DONE);
      if (!o) continue;
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_WA_PICK);
      DoMethod(grid, OM_ADDMEMBER, (ULONG)o);
    }
    insertwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Insert WordArt"), MUIA_Window_ID, MAKE_ID('W','1','9','A'),
      WindowContents, VGroup, QDLG,
        Child, HGroup, Child, QLabel(tr("Text"), MUIO_Label_DoubleFrame),
          Child, ins_text = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_String_Contents, (ULONG)tr("Your text here"), MUIA_CycleChain, 1, End, End,
        Child, grid,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Pick a style to put the text in a box of its own."), MUIA_Font, MUIV_Font_Tiny, End,
      End, End;
    if (!insertwin) { beep(); return; }
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)insertwin);
    DoMethod(insertwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)insertwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(insertwin, MUIA_Window_Open, TRUE);
}
/* a text box with no fill or line holding the text in style i, centred, sized to it, in front of the text */
static void insert_wordart(int i)
{
  Doc *d = wp_doc();
  Layout *L = layout();
  STRPTR s = NULL;
  char u[400];
  Story *st;
  Para *p;
  ParaFmt pf;
  CharFmt cf;
  TextFx fx;
  FaceSize *z;
  Image m;
  int k;
  if (!d || i < 0 || i >= WA_NSTYLES) return;
  get(ins_text, MUIA_String_Contents, &s);
  wp_latin1_utf8(s && s[0] ? (const char *)s : tr("Your text here"), u, sizeof u);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SHAPE, (ULONG)-1);
  if ((k = sel_obj()) < 0) return;
  pf = d->styles[0].pf; cf = d->styles[0].cf;
  pf.align = AL_CENTER; pf.before = pf.after = 0; pf.line = 240; pf.linerule = LS_AUTO;
  wa_style(i, &fx);
  cf.size = 72; cf.flags |= CF_BOLD; cf.rx = wa_set(0, &fx, WA_ALL);
  if (!(st = (Story *)calloc(1, sizeof(Story))) || !(st->p = (Para **)malloc(sizeof(Para *))) || !(p = para_new(&pf, &cf))) { free(st ? st->p : NULL); free(st); beep(); return; }
  para_append(p, u, (u32)strlen(u), &cf);
  st->p[0] = p; st->np = 1;
  m = d->images[k];
  m.text = st; m.fill = COLOR_AUTO; m.lstyle = PB_NONE; m.wrap = WRAP_FRONT; m.kind = OBJ_TEXTBOX;
  if (L && (z = lay_face(L, &cf))) {
    m.cx = (i32)(fonts_text(z, u, (u32)strlen(u)) / QFINE) + 2 * TB_INSET_X + 288;
    m.cy = z->height + 2 * TB_INSET_Y + 144;
  }
  obj_set(&m);
  story_free(st); free(st);
}

/* ------------------------------------------------------------------ the signature line */
static Object *sigwin, *sg_signer, *sg_title, *sg_email, *sg_instr, *sg_comments, *sg_date;
static int sig_target = -1;
static void set_str(Object *o, const char *utf8) { char t[300]; utf8_to_latin1(utf8, strlen(utf8), t, sizeof t); set(o, MUIA_String_Contents, (ULONG)t); }
static void get_str(Object *o, char *out, size_t cap) { STRPTR s = NULL; get(o, MUIA_String_Contents, &s); wp_latin1_utf8(s ? (const char *)s : "", out, cap); }
static void open_sig(void)
{
  Doc *d = wp_doc();
  int k = sel_obj();
  const struct SigLine *s = d && k >= 0 ? d->images[k].sig : NULL;
  if (!sigwin) {
    Object *c = VGroup,
      Child, ColGroup(2),
        Child, QLabel(tr("Suggested signer"), MUIO_Label_DoubleFrame), Child, sg_signer = StringObject, StringFrame, MUIA_String_MaxLen, 100, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Suggested signer's title"), MUIO_Label_DoubleFrame), Child, sg_title = StringObject, StringFrame, MUIA_String_MaxLen, 100, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Suggested signer's e-mail address"), MUIO_Label_DoubleFrame), Child, sg_email = StringObject, StringFrame, MUIA_String_MaxLen, 100, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Instructions to the signer"), MUIO_Label_DoubleFrame), Child, sg_instr = StringObject, StringFrame, MUIA_String_MaxLen, 200, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, sg_comments = CheckMark(FALSE), Child, QLabel(tr("Allow the signer to add comments in the Sign dialog"), 0), Child, HSpace(0), End,
      Child, HGroup, Child, sg_date = CheckMark(TRUE), Child, QLabel(tr("Show sign date in signature line"), 0), Child, HSpace(0), End,
    End;
    if (!(sigwin = wp_dialog("Signature Setup", MAKE_ID('W','1','9','S'), c, "_OK", ID_WA_SIGOK))) { beep(); return; }
  }
  sig_target = s ? k : -1;
  set_str(sg_signer, s ? s->signer : ""); set_str(sg_title, s ? s->title : ""); set_str(sg_email, s ? s->email : "");
  set_str(sg_instr, s ? s->instr : tr("Before signing this document, verify that the content you are signing is correct."));
  set(sg_comments, MUIA_Selected, s && s->comments); set(sg_date, MUIA_Selected, !s || s->showdate);
  set(sigwin, MUIA_Window_Open, TRUE);
  set(sigwin, MUIA_Window_ActiveObject, (ULONG)sg_signer);
}
static void sig_done(void)
{
  Doc *d = wp_doc();
  Layout *L = layout();
  struct SigLine sl;
  ULONG cm = 0, dt = 0;
  Image m;
  int k;
  set(sigwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (!d || !L) return;
  memset(&sl, 0, sizeof sl);
  get_str(sg_signer, sl.signer, sizeof sl.signer); get_str(sg_title, sl.title, sizeof sl.title);
  get_str(sg_email, sl.email, sizeof sl.email); get_str(sg_instr, sl.instr, sizeof sl.instr);
  get(sg_comments, MUIA_Selected, &cm); get(sg_date, MUIA_Selected, &dt);
  sl.comments = (u8)(cm != 0); sl.showdate = (u8)(dt != 0);
  if (sig_target >= 0 && sig_target == sel_obj() && d->images[sig_target].sig) {
    /* the line changed: a new picture, its id kept */
    strcpy(sl.id, d->images[sig_target].sig->id);
    if (!img_clone(&m, &d->images[sig_target], 1)) { beep(); return; }
    if (!sig_make(&m, &sl, L->fs) || (k = doc_take_object(d, &m)) < 0) { img_release(&m); beep(); return; }
    obj_set(&d->images[k]);
    return;
  }
  img_defaults(&m);
  if (!sig_make(&m, &sl, L->fs) || (k = doc_take_object(d, &m)) < 0) { img_release(&m); beep(); return; }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_PICTURE, (ULONG)k);
}

/* ------------------------------------------------------------------ the WordArt Styles window (Format menu) */
static Object *dlgwin, *dg[7];
static const char *dl[7][24];
static const char *const nochange = "(No change)";
static Object *dcyc(int slot, const char *const *src, int n)
{
  int i;
  dl[slot][0] = tr(nochange);
  for (i = 0; i < n && i < 22; i++) dl[slot][i + 1] = tr(src[i]);
  dl[slot][i + 1] = NULL;
  return CycleObject, MUIA_Cycle_Entries, (ULONG)dl[slot], MUIA_CycleChain, 1, End;
}
static void open_dialog(void)
{
  if (!dlgwin) {
    Object *c = ColGroup(2),
      Child, QLabel(tr("Quick Styles"), MUIO_Label_DoubleFrame), Child, dg[0] = dcyc(0, style_labels, WA_NSTYLES),
      Child, QLabel(tr("Text Fill"), MUIO_Label_DoubleFrame), Child, dg[1] = dcyc(1, fill_labels, WA_NFILLS),
      Child, QLabel(tr("Text Outline"), MUIO_Label_DoubleFrame), Child, dg[2] = dcyc(2, line_labels, WA_NLINES),
      Child, QLabel(tr("Shadow"), MUIO_Label_DoubleFrame), Child, dg[3] = dcyc(3, effect_labels, WA_NSHADOWS),
      Child, QLabel(tr("Reflection"), MUIO_Label_DoubleFrame), Child, dg[4] = dcyc(4, effect_labels + WA_NSHADOWS, WA_NREFLS),
      Child, QLabel(tr("Glow"), MUIO_Label_DoubleFrame), Child, dg[5] = dcyc(5, effect_labels + WA_NSHADOWS + WA_NREFLS, WA_NGLOWS + WA_NBEVELS),
      Child, QLabel(tr("Transform"), MUIO_Label_DoubleFrame), Child, dg[6] = dcyc(6, warp_labels, WW_N),
    End;
    if (!(dlgwin = wp_dialog("WordArt Styles", MAKE_ID('W','1','9','W'), c, "_Apply", ID_WA_DLGOK))) { beep(); return; }
  }
  { int i; for (i = 0; i < 7; i++) set(dg[i], MUIA_Cycle_Active, 0); }
  set(dlgwin, MUIA_Window_Open, TRUE);
}
static void dialog_done(void)
{
  ULONG v[7];
  int i;
  for (i = 0; i < 7; i++) { v[i] = 0; get(dg[i], MUIA_Cycle_Active, &v[i]); }
  set(dlgwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (v[0]) apply(fx_style, (int)v[0] - 1, WA_ALL);
  if (v[1]) apply(fx_fill, (int)v[1] - 1, WA_FILL | WA_LINE);
  if (v[2]) apply(fx_line, (int)v[2] - 1, WA_FILL | WA_LINE);
  if (v[3]) apply(fx_effect, (int)v[3] - 1, WA_SHADOW);
  if (v[4]) apply(fx_effect, WA_NSHADOWS + (int)v[4] - 1, WA_REFL);
  if (v[5]) apply(fx_effect, WA_NSHADOWS + WA_NREFLS + (int)v[5] - 1, effect_part(WA_NSHADOWS + WA_NREFLS + (int)v[5] - 1));
  if (v[6]) transform((int)v[6] - 1);
}

/* ------------------------------------------------------------------ commands and hooks */
int wp19_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_WA_INSERT: open_insert(); return WPR_KEEPFOCUS;
  case ID_WA_PICK: if (insertwin) set(insertwin, MUIA_Window_Open, FALSE); insert_wordart(picked); break;
  case ID_WA_STYLEDD: if (value >= 0 && value < WA_NSTYLES) apply(fx_style, (int)value, WA_ALL); break;
  case ID_WA_FILLDD: if (value >= 0 && value < WA_NFILLS) apply(fx_fill, (int)value, WA_FILL | WA_LINE); break;
  case ID_WA_LINEDD: if (value >= 0 && value < WA_NLINES) apply(fx_line, (int)value, WA_FILL | WA_LINE); break;
  case ID_WA_EFFECTSDD: if (value >= 0 && value < NEFFECTS) apply(fx_effect, (int)value, effect_part((int)value)); break;
  case ID_WA_WARPDD: transform((int)value); break;
  case ID_WA_SIG: open_sig(); return WPR_KEEPFOCUS;
  case ID_WA_SIGOK: sig_done(); break;
  case ID_WA_DIALOG: open_dialog(); return WPR_KEEPFOCUS;
  case ID_WA_DLGOK: dialog_done(); break;
  }
  return WPR_DONE;
}
static void state(ULONG *on, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (!sig_fonts) { Layout *L = layout(); if (L) sig_fonts = L->fs; }     /* (signature lines read from files drawn with them) */
  if (im && im->text && im->text->np) *on |= RBIT(SB_TEXTOBJ);
  if (im && im->sig) *on |= RBIT(SB_SIG);
}
static void ctxmenu(Object *menu, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (c->where != WPC_OBJECT || !im) return;
  if (im->sig) { wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1); wp_ctx_item(menu, "Signature Setup...", WP_CTX_ID(ID_WA_SIG), 1); }
  else if (im->text && im->text->np) {
    wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
    wp_ctx_item(menu, "WordArt Styles...", WP_CTX_ID(ID_WA_DIALOG), 1);
    wp_ctx_sub(menu, "Transform", ID_WA_WARPDD, warp_labels, WW_N);
  }
}
static void cleanup(void)
{
  int i;
  wp19_flush();
  (void)i;
  if (cellcls) { MUI_DeleteCustomClass(cellcls); cellcls = NULL; }
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp19_package = {
  "WP19", ID_WP19_FIRST, ID_WP19_LAST,
  wp19_command, state, NULL, ctxmenu, NULL, NULL, cleanup,
  groups, 2,
  menus, NMENUS
};
