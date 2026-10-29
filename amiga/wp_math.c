/*
 * wp_math.c - WP14a Equations: model, layout, .docx (docs/wordcraft-gap.md). This file belongs to WP14a: only that
 * package edits it. Its commands are ID_WP14A_FIRST .. ID_WP14A_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Insert > Equation: the equation typed in linear format (UnicodeMath, its characters outside
 * Latin-1 as \control words), put in at the caret (a display equation if the paragraph is empty, as
 * Word does), or the selected (or right-clicked) equation changed. The equation editor and its tab
 * are WP14b's. Drawing equations on the page: glyphs rendered from the fonts' outlines (core
 * ttraster), Quill's drawn shapes filled as spans.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_math.h"
#include "lang.h"
#include "../core/math.h"
#include "../core/ttraster.h"

enum { ID_EQ_INSERT = ID_WP14A_FIRST,      /* (ribbon.c's Symbols group names it ID_WP14A_FIRST) */
       ID_EQ_OK, ID_EQ_EDIT };

/* ------------------------------------------------------------------ */
/* drawing */
/* glyphs as pixels, made once: their covered pixels (threshold half), kept for the session */
#define NGC 256
typedef struct { Face *f; u16 g; u8 flags; LONG ppem; TTBitmap bm; } GlyphPix;
static GlyphPix gc[NGC];
static int ngc, gcnext;
static Face *hinted[16]; static int nhinted;
static TTBitmap *glyph_pix(Face *f, u16 g, LONG ppem64, int flags)
{
  int i, x, y;
  TTRaster *r;
  GlyphPix *p;
  for (i = 0; i < ngc; i++) if (gc[i].f == f && gc[i].g == g && gc[i].ppem == ppem64 && gc[i].flags == flags) return &gc[i].bm;
  if (!(r = math_raster(f))) return NULL;
  for (i = 0; i < nhinted && hinted[i] != f; i++) ;
  if (i == nhinted && nhinted < 16) { ttr_hint(r, f->latin_glyph); hinted[nhinted++] = f; }
  if (ngc < NGC) p = &gc[ngc++];
  else { p = &gc[gcnext]; free(p->bm.cov); gcnext = (gcnext + 1) % NGC; }
  memset(p, 0, sizeof *p);
  p->f = f; p->g = g; p->ppem = ppem64; p->flags = (u8)flags;
  if (!ttr_glyph(r, g, ppem64, &p->bm)) memset(&p->bm, 0, sizeof p->bm);
  if (p->bm.cov) {
    TTBitmap *b = &p->bm;
    ttr_dropout(b, 128);
    if (flags & MIF_FLIPX) {
      LONG adv = f->nmetrics ? ((LONG)f->adv[g < f->nmetrics ? g : f->nmetrics - 1] * ppem64 / (f->upm ? f->upm : 2048) + 32) / 64 : b->w;
      for (y = 0; y < b->h; y++) for (x = 0; x < b->w / 2; x++) { u8 t = b->cov[y * b->w + x]; b->cov[y * b->w + x] = b->cov[y * b->w + b->w - 1 - x]; b->cov[y * b->w + b->w - 1 - x] = t; }
      b->left = (int)(adv - (b->left + b->w));
    }
    if (flags & MIF_FLIPY)
      for (y = 0; y < b->h / 2; y++) for (x = 0; x < b->w; x++) { u8 t = b->cov[y * b->w + x]; b->cov[y * b->w + x] = b->cov[(b->h - 1 - y) * b->w + x]; b->cov[(b->h - 1 - y) * b->w + x] = t; }
  }
  return &p->bm;
}
static struct RastPort *span_rp;
static void span(void *ctx, int y, int x0, int x1) { (void)ctx; RectFill(span_rp, x0, y, x1, y); }
void wp14a_draw(struct RastPort *rp, Layout *L, const Box *b, const char *t, LONG x, LONG base, LONG zoom, LONG (*pen)(ULONG rgb))
{
  const MLay *ml = math_layout(L->fs, L->d, math_index(t + b->off), &b->f);
  struct Rectangle clip;
  u32 i;
  int j;
  LONG lastpen = -1;
  i32 *seg = NULL;
  if (!ml) return;
  /* (the window's drawable rows: shapes far outside it aren't filled row by row) */
  clip.MinY = -32000; clip.MaxY = 32000;
  if (rp->Layer) { clip = rp->Layer->bounds; clip.MaxY = (WORD)(clip.MaxY - clip.MinY); clip.MinY = 0; }
  SetDrMd(rp, JAM1);
  for (i = 0; i < ml->n; i++) {
    const MItem *it = &ml->it[i];
    LONG p = pen(it->color == COLOR_AUTO ? b->f.color : it->color);
    if (p != lastpen) { SetAPen(rp, p); lastpen = p; }
    if (it->kind == MI_RECT) {
      LONG x0 = x + it->x * zoom / 2000, y0 = base + it->y * zoom / 2000, x1 = x + (it->x + it->w) * zoom / 2000 - 1, y1 = base + (it->y + it->h) * zoom / 2000 - 1;
      if (x1 < x0) x1 = x0;
      if (y1 < y0) y1 = y0;
      RectFill(rp, x0, y0, x1, y1);
    } else if (it->kind == MI_POLY) {
      /* an outline: its edges in sixteenths of a pixel, filled */
      LONG miny = 0x7FFFFFFF, maxy = -0x7FFFFFFF;
      if (!seg && !(seg = (i32 *)malloc(4 * 64 * sizeof(i32)))) continue;
      if (it->h > 64) continue;
      for (j = 0; j < it->h; j++) {
        int k = (j + 1) % it->h;
        seg[4 * j] = x * 16 + ml->pt[it->w + 2 * j] * zoom * 16 / 2000;
        seg[4 * j + 1] = base * 16 + ml->pt[it->w + 2 * j + 1] * zoom * 16 / 2000;
        seg[4 * j + 2] = x * 16 + ml->pt[it->w + 2 * k] * zoom * 16 / 2000;
        seg[4 * j + 3] = base * 16 + ml->pt[it->w + 2 * k + 1] * zoom * 16 / 2000;
        if (seg[4 * j + 1] < miny) miny = seg[4 * j + 1];
        if (seg[4 * j + 1] > maxy) maxy = seg[4 * j + 1];
      }
      miny /= 16; maxy = maxy / 16 + 1;
      if (miny < clip.MinY) miny = clip.MinY;
      if (maxy > clip.MaxY) maxy = clip.MaxY;
      span_rp = rp;
      if (miny <= maxy) math_fill(seg, it->h, (int)miny, (int)maxy, span, NULL);
    } else if (it->kind == MI_GLYPH && it->face < ml->nfaces) {
      LONG ppem64 = (LONG)it->size * zoom * 4 / 125, gx = x + it->x * zoom / 2000, gy = base + it->y * zoom / 2000;
      TTBitmap *bm;
      int row, c0;
      if (ppem64 < 64) { WritePixel(rp, gx, gy); continue; }
      if (ppem64 > 64 * 400) ppem64 = 64 * 400;
      if (!(bm = glyph_pix(ml->faces[it->face], it->glyph, ppem64, it->flags)) || !bm->cov) continue;
      for (row = 0; row < bm->h; row++) {
        const u8 *cv = bm->cov + row * bm->w;
        LONG yy = gy - bm->top + row;
        if (yy < clip.MinY || yy > clip.MaxY) continue;
        for (c0 = 0; c0 < bm->w; ) {
          int c1;
          if (cv[c0] < 128) { c0++; continue; }
          for (c1 = c0; c1 + 1 < bm->w && cv[c1 + 1] >= 128; c1++) ;
          RectFill(rp, gx + bm->left + c0, yy, gx + bm->left + c1, yy);
          c0 = c1 + 1;
        }
      }
    }
  }
  free(seg);
}

/* ------------------------------------------------------------------ */
/* Insert > Equation */
static WpCtx now;                       /* the caret and selection, as the last state call had them */
static Object *eqwin, *eqstr;
static int eqedit;                      /* the dialog changes the equation at eqat */
static Pos eqat;
/* the equation at the selection (exactly one), or next to the caret (any: right-clicked); where */
static int math_at(const WpCtx *c, int any, Pos *at)
{
  Doc *d = c->doc;
  Pos lo, hi;
  Para *p;
  if (!d) return 0;
  lo = pos_cmp(c->anchor, c->caret) <= 0 ? c->anchor : c->caret;
  hi = pos_cmp(c->anchor, c->caret) <= 0 ? c->caret : c->anchor;
  if (lo.para >= d->np) return 0;
  p = d->p[lo.para];
  if (c->hassel) {
    if (lo.para != hi.para || hi.off - lo.off != 4 || lo.off + 4 > p->len || !IS_MATH(p->t + lo.off)) return 0;
    *at = lo;
    return 1;
  }
  if (!any) return 0;
  if (lo.off + 4 <= p->len && IS_MATH(p->t + lo.off)) { *at = lo; return 1; }
  if (lo.off >= 4 && IS_MATH(p->t + lo.off - 4)) { *at = lo; at->off -= 4; return 1; }
  return 0;
}
static void open_dialog(int edit)
{
  Doc *d = wp_doc();
  char lat[1100];
  if (!d) return;
  if (!eqwin) {
    Object *c = VGroup,
        Child, TextObject, MUIA_Text_Contents, tr("Type the equation in linear format, for example:"), End,
        Child, TextObject, MUIA_Text_Contents, "x^2+(a+b)/c   \\sqrt(x)   \\sum_(i=1)^n i   \\matrix(a&b@c&d)", MUIA_Font, MUIV_Font_Fixed, End,
        Child, HGroup,
          Child, Label2(tr("Equation:")),
          Child, eqstr = StringObject, StringFrame, MUIA_String_MaxLen, 1024, MUIA_CycleChain, 1, End,
        End,
      End;
    if (!(eqwin = wp_dialog("Equation", MAKE_ID('E', 'Q', 'N', 'S'), c, "_Insert", ID_EQ_OK))) return;
    DoMethod(eqstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_EQ_OK);
  }
  eqedit = edit && math_at(&now, edit == 2, &eqat);
  lat[0] = 0;
  if (eqedit) {
    /* its linear text, as Latin-1 with control words */
    const MathEq *e = doc_math(d, math_index(d->p[eqat.para]->t + eqat.off));
    Buf u, k;
    buf_init(&u); buf_init(&k);
    if (e) math_to_linear(e, &u);
    if (u.data) math_keywords(u.data, &k);
    if (k.data) utf8_to_latin1(k.data, k.len, lat, sizeof lat);
    buf_free(&u); buf_free(&k);
  }
  set(eqstr, MUIA_String_Contents, (ULONG)lat);
  set(eqwin, MUIA_Window_Open, TRUE);
  set(eqwin, MUIA_Window_ActiveObject, (ULONG)eqstr);
}
static void dialog_done(void)
{
  Doc *d = wp_doc();
  STRPTR s = NULL;
  char utf[3300], ref[8];
  MathEq *e;
  int k, display = 0;
  set(eqwin, MUIA_Window_Open, FALSE);
  get(eqstr, MUIA_String_Contents, &s);
  if (!d || !s || !s[0]) return;
  wp_latin1_utf8((const char *)s, utf, sizeof utf);
  if (eqedit) { const MathEq *o = doc_math(d, math_index(d->p[eqat.para]->t + eqat.off)); display = o && o->display; }
  else if (now.caret.para < d->np) display = d->p[now.caret.para]->len == 0;     /* alone in its paragraph: a display equation, as Word */
  if (!(e = math_from_linear(utf, display))) { DisplayBeep(NULL); return; }
  if ((k = doc_add_math(d, e)) < 0) { DisplayBeep(NULL); return; }
  math_utf8(ref, k);
  if (eqedit) {
    Pos sel[2];
    sel[0] = eqat; sel[1] = eqat; sel[1].off += 4;
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)sel);
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)ref);
  } else DoMethod(wp_view(), MUIM_Quill_Command, CMD_TEXT, (ULONG)ref);
  set(wp_win(), MUIA_Window_Activate, TRUE);
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp14a_command(ULONG id, LONG value)
{
  (void)value;
  switch (id) {
  case ID_EQ_INSERT: open_dialog(1); return WPR_KEEPFOCUS;
  case ID_EQ_EDIT: open_dialog(2); return WPR_KEEPFOCUS;
  case ID_EQ_OK: dialog_done(); break;
  }
  return WPR_DONE;
}
static void wp14a_state(ULONG *on, const WpCtx *c)
{
  Pos at;
  now = *c;
  if (math_at(c, 1, &at)) *on |= 2;             /* bit 1: an equation by the caret */
}
static void wp14a_ctxmenu(Object *menu, const WpCtx *c)
{
  Pos at;
  now = *c;
  if (c->where == WPC_TEXT && math_at(c, 1, &at)) {
    wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
    wp_ctx_item(menu, tr("Edit Equation..."), WP_CTX_ID(ID_EQ_EDIT), 1);
  }
}
static void wp14a_cleanup(void)
{
  int i;
  for (i = 0; i < ngc; i++) free(gc[i].bm.cov);
  ngc = gcnext = 0; nhinted = 0;
  math_fonts_free();
}

static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Equation...", 0, 0, 0, (APTR)ID_EQ_INSERT } },
};
/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp14a_package = {
  "WP14a", ID_WP14A_FIRST, ID_WP14A_LAST,
  wp14a_command, wp14a_state, NULL, wp14a_ctxmenu, NULL, NULL, wp14a_cleanup,
  NULL, 0,
  menus, 2
};
