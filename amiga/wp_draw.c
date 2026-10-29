/*
 * wp_draw.c - WP20 Draw tab and drawing canvas (docs/wordcraft-gap.md). This file belongs to WP20: only that
 * package edits it. Its commands are ID_WP20_FIRST .. ID_WP20_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The Draw tab: Select, Lasso Select, the eraser (whole strokes or points), pens (a black, red and blue
 * pen, a pencil, a highlighter, and pens added with Add Pen), their thickness and colour, Ink to Shape
 * (as strokes are drawn, or on the strokes chosen), Drawing Canvas, Ink Replay; Review's Hide Ink and
 * Delete All Ink; adding shapes to a canvas from its right-button menu. Drawing itself is in
 * wp_draw_view.h (part of the page view); the model, .docx and PDF side is core/ink.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "wp_draw.h"
#include "../core/ink.h"

enum {
  ID_DR_SELECT = ID_WP20_FIRST, ID_DR_LASSO, ID_DR_ERASER, ID_DR_ERASERLIST, ID_DR_ERASESTROKE, ID_DR_ERASEPOINT,
  ID_DR_PEN0, ID_DR_PEN1, ID_DR_PEN2, ID_DR_PEN3, ID_DR_PEN4,
  ID_DR_ADDPEN, ID_DR_THICKLIST, ID_DR_COLORLIST, ID_DR_MYPENS, ID_DR_NEXTPEN,
  ID_DR_INKSHAPE, ID_DR_CONVERT, ID_DR_CANVAS, ID_DR_REPLAY, ID_DR_HIDE, ID_DR_DELALL, ID_DR_DELSEL,
  ID_DR_THICK0, ID_DR_THICK1, ID_DR_THICK2, ID_DR_THICK3, ID_DR_THICK4, ID_DR_THICK5,
  ID_DR_COL0, ID_DR_COL1, ID_DR_COL2, ID_DR_COL3, ID_DR_COL4, ID_DR_COL5, ID_DR_COL6, ID_DR_COL7,
  ID_DR_CANVASADD, ID_DR_INKCONVERT1
};
/* the state bits (RB_WP(WPI_20, n)) */
enum { ST_SELECT = 1, ST_LASSO, ST_ERASER, ST_PEN0, ST_PEN1, ST_PEN2, ST_PEN3, ST_PEN4, ST_CUSTOM, ST_INKSHAPE, ST_REPLAY, ST_HIDDEN, ST_CHOSEN };

/* ---- icons, in theme.c's icon language */
#define IC_SELECT  "ci P 8 3 2 3 12 6 9 9 14 10 13 7 8 11 7 3 2"
#define IC_LASSO   "ca L 3 3 5 2 L 8 2 10 2 L 12 3 13 5 L 13 7 12 9 L 10 10 8 10 L 5 10 3 9 L 2 7 2 5 ci L 4 10 5 14 L 5 14 3 15"
#define IC_ERASER  "ci P 5 9 2 14 7 7 14 2 9 9 2 L 5 6 10 11 cs F 3 10 5 11 F 4 12 6 13 ci L 6 15 14 15"
#define PEN(c)     "ci P 5 4 12 12 4 14 6 6 14 4 12 L 11 5 13 7 c" c " P 3 4 12 1 15 6 14 F 1 14 3 15"
#define IC_PEN0    PEN("t")
#define IC_PEN1    PEN("r")
#define IC_PEN2    PEN("a")
#define IC_PENCIL  "co P 5 4 12 12 4 14 6 6 14 4 12 ci L 11 5 13 7 cd P 3 4 12 1 15 6 14"
#define IC_HILITE  "ci P 5 5 9 10 4 13 7 8 12 5 9 L 5 9 3 12 L 3 12 6 12 cy F 2 13 14 15"
#define IC_ADDPEN  "ci P 5 2 12 9 5 11 7 4 14 2 12 L 1 15 3 13 cg L 13 1 13 7 L 10 4 15 4"
#define IC_THICK   "ci L 2 3 14 3 F 2 6 14 7 F 2 10 14 14"
#define IC_COLOR   "cr E 5 5 3 cg E 11 5 3 ca E 5 11 3 cy E 11 11 3"
#define IC_MYPENS  "ci P 5 1 12 7 6 9 8 3 14 1 12 ca P 5 6 12 12 6 14 8 8 14 6 12"
#define IC_INKSHAPE "ca P 5 1 5 3 2 5 6 7 3 8 6 ci L 8 9 9 9 L 9 8 10 9 L 9 10 10 9 ca R 10 6 15 12"
#define IC_CONVERT "ca P 4 1 10 3 6 5 10 7 6 ci L 8 8 10 8 ca O 12 11 3"
#define IC_CANVAS  "ci R 1 2 14 13 ca O 5 7 2 cg F 8 5 12 9 cr L 3 11 12 11"
#define IC_REPLAY  "ci O 8 8 6 ca P 4 6 5 11 8 6 11 6 5"
#define IC_HIDE    "ci P 5 1 8 4 5 8 4 12 5 15 8 P 5 1 8 4 11 8 12 12 11 15 8 ca E 8 8 2 cr L 2 14 14 2"
#define IC_DELINK  "ci P 4 1 10 4 6 7 10 10 6 cr L 9 9 14 14 L 14 9 9 14"

/* ---- the pens: five of the gallery's, then those added (Add Pen) */
#define NPENS 5
#define MAXADDED 8
static DrawPen pens[NPENS + MAXADDED] = {
  { INK_PEN, 0x000000, 8 }, { INK_PEN, 0xC00000, 8 }, { INK_PEN, 0x1F4FC8, 8 }, { INK_PENCIL, 0x595959, 8 }, { INK_HIGHLIGHTER, 0xFFFF00, 96 } };
static int nadded;
static int tool = DT_NONE, curpen, eraser = DT_ERASER, inkshape, hidden;

static const char *const erasers[] = { "Stroke Eraser", "Point Eraser" };
static const RibbonList l_eraser = { erasers, 2, PL_TEXT, NULL };
static const char *const thicks[] = { "0.25 mm", "0.5 mm", "1 mm", "2 mm", "4 mm", "6 mm" };
static const u8 thickw[6] = { 6, 11, 23, 45, 91, 136 };      /* eighths of a point */
static const RibbonList l_thick = { thicks, 6, PL_TEXT, NULL };
static const char *const colnames[] = { "Black", "Red", "Blue", "Green", "Yellow", "Orange", "Purple", "Grey" };
static const ULONG colrgb[8] = { 0x000000, 0xC00000, 0x1F4FC8, 0x00A050, 0xFFFF00, 0xFF8000, 0x8A2BB0, 0x808080 };
static const RibbonList l_color = { colnames, 8, PL_COLOR, colrgb };
static char addedlab[MAXADDED][64];
static const char *addedp[MAXADDED];
static RibbonList l_added = { addedp, 0, PL_TEXT, NULL };      /* (its count grows with Add Pen) */
static const char *const canvas_shapes[] = { "Rectangle", "Oval", "Line", "Arrow", "Isosceles Triangle", "Text Box" };
static const int canvas_sh[6] = { SH_RECT, SH_ELLIPSE, SH_LINE, SH_ARROW, SH_TRIANGLE, -1 };

/* ---- for the view (wp_draw.h) */
int  wp20_tool(void) { return tool; }
const DrawPen *wp20_pen(void) { return curpen >= 0 && curpen < NPENS + nadded ? &pens[curpen] : &pens[0]; }
int  wp20_inkshape(void) { return inkshape; }
int  wp20_hidden(void) { return hidden; }
void wp20_set_tool(int t) { tool = t; wp_refresh(); }

/* an added pen's line in the list: its kind, colour and thickness */
static void added_label(int i)
{
  const DrawPen *p = &pens[NPENS + i];
  const char *kind = p->kind == INK_PENCIL ? "Pencil" : p->kind == INK_HIGHLIGHTER ? "Highlighter" : "Pen", *col = "";
  int k, t = 0;
  for (k = 0; k < 8; k++) if (colrgb[k] == p->color) col = colnames[k];
  for (k = 0; k < 6; k++) if (thickw[k] <= p->width) t = k;
  snprintf(addedlab[i], sizeof addedlab[i], "%s, %s%s%s", tr(kind), tr(col), *col ? ", " : "", thicks[t]);
  addedp[i] = addedlab[i];
}

/* ---- the document */
static void relaid(void) { DoMethod(wp_view(), MUIM_Quill_Relayout); }
static int pos_desc(const void *a, const void *b) { return pos_cmp(*(const Pos *)b, *(const Pos *)a); }
static Image *at_obj(Doc *d, Pos p)
{
  if (p.para >= d->np || p.off + 4 > d->p[p.para]->len || !IS_PIC(d->p[p.para]->t + p.off)) return NULL;
  return &d->images[pic_index(d->p[p.para]->t + p.off)];
}
/* the objects chosen (the lasso's, or the one selected), last first */
static int chosen(Pos *ps, int max)
{
  int n = dv_chosen(wp_view(), ps, max);
  qsort(ps, (size_t)n, sizeof(Pos), pos_desc);
  return n;
}
static int any_ink(const Image *im)
{
  int k;
  if (ff_is_ink(im)) return 1;
  for (k = 0; k < im->nkids; k++) if (any_ink(&im->kids[k])) return 1;
  return 0;
}
/* Ink to Shape on the strokes chosen (and those in a chosen canvas or group): how many became shapes */
static int convert_chosen(void)
{
  Doc *d = wp_doc();
  Pos ps[16];
  int n = chosen(ps, 16), i, done = 0;
  if (!d || !n) return 0;
  doc_begin(d, ps[0]);
  for (i = 0; i < n; i++) {
    Image *im = at_obj(d, ps[i]), out;
    i32 dx, dy;
    if (!im) continue;
    if (ff_is_ink(im)) {
      if (!ink_to_shape(im, &out, &dx, &dy)) continue;
      if (!out.halign) out.x += dx;
      if (!out.valign) out.y += dy;
      doc_set_object(d, ps[i], &out);
      img_release(&out);
      done++;
    } else if (im->kind == OBJ_GROUP && any_ink(im)) {
      Image g;
      int k, any = 0;
      if (!img_clone(&g, im, 0)) continue;
      for (k = 0; k < g.nkids; k++)
        if (ff_is_ink(&g.kids[k]) && ink_to_shape(&g.kids[k], &out, &dx, &dy)) {
          out.x = g.kids[k].x + dx; out.y = g.kids[k].y + dy;
          img_release(&g.kids[k]);
          g.kids[k] = out;
          any++;
        }
      if (any) { doc_set_object(d, ps[i], &g); done += any; }
      img_release(&g);
    }
  }
  doc_end(d, ps[0]);
  if (done) relaid();
  return done;
}
/* the chosen ink's colour (pens and the lasso's strokes) */
static int recolor_chosen(u32 rgb)
{
  Doc *d = wp_doc();
  Pos ps[16];
  int n = chosen(ps, 16), i, done = 0;
  if (!d || !n) return 0;
  doc_begin(d, ps[0]);
  for (i = 0; i < n; i++) {
    Image *im = at_obj(d, ps[i]), m;
    if (!im || !ff_is_ink(im)) continue;
    m = *im; m.lcolor = rgb;
    doc_set_object(d, ps[i], &m);
    done++;
  }
  doc_end(d, ps[0]);
  if (done) relaid();
  return done;
}
/* the objects chosen deleted (several lasso'd: Del) */
static void delete_chosen(void)
{
  Doc *d = wp_doc();
  Pos ps[16];
  int n = chosen(ps, 16), i;
  if (!d || !n) return;
  doc_begin(d, ps[n - 1]);
  for (i = 0; i < n; i++) { Pos e = ps[i]; e.off += 4; if (at_obj(d, ps[i])) doc_delete(d, ps[i], e); }
  doc_end(d, ps[n - 1]);
  dv_unchoose(wp_view());
  relaid();
}
/* Delete All Ink: every stroke in the text and in canvases and groups */
static void delete_all_ink(void)
{
  Doc *d = wp_doc();
  u32 i, k;
  int any = 0;
  Pos c0 = { 0, 0 };
  if (!d) return;
  doc_begin(d, c0);
  for (i = d->np; i-- > 0; )
    for (k = d->p[i]->len >= 4 ? d->p[i]->len - 4 + 1 : 0; k-- > 0; ) {
      Pos a, e;
      Image *im;
      if (!IS_PIC(d->p[i]->t + k)) continue;
      a.para = i; a.off = k; e = a; e.off += 4;
      im = &d->images[pic_index(d->p[i]->t + k)];
      if (ff_is_ink(im)) { doc_delete(d, a, e); any = 1; }
      else if (im->kind == OBJ_GROUP && any_ink(im)) {
        Image g;
        int j;
        if (!img_clone(&g, im, 0)) continue;
        for (j = g.nkids; j-- > 0; ) if (ff_is_ink(&g.kids[j])) { img_release(&g.kids[j]); memmove(g.kids + j, g.kids + j + 1, (size_t)(g.nkids - j - 1) * sizeof(Image)); g.nkids--; }
        if (g.nkids) doc_set_object(d, a, &g); else doc_delete(d, a, e);
        img_release(&g);
        any = 1;
      }
    }
  doc_end(d, c0);
  if (any) relaid(); else DisplayBeep(NULL);
}
/* Drawing Canvas: an empty one the column's width in line at the caret */
static void insert_canvas(void)
{
  Doc *d = wp_doc();
  Image c;
  int k;
  i32 w;
  if (!d) return;
  w = d->page.width - d->page.left - d->page.right;
  if (w < 1440) w = 1440;
  {
    /* after the selection (an object, say), not in its place */
    Pos *ca = NULL, *an = NULL, p;
    get(wp_view(), MUIA_Quill_Caret, &ca); get(wp_view(), MUIA_Quill_Anchor, &an);
    if (ca && an) { p = pos_cmp(*ca, *an) > 0 ? *ca : *an; set(wp_view(), MUIA_Quill_Caret, (ULONG)&p); }
  }
  if (!canvas_make(&c, w, w / 2 < 3600 ? w / 2 : 3600)) return;
  k = doc_add_object(d, &c);
  img_release(&c);
  if (k >= 0) DoMethod(wp_view(), MUIM_Quill_Command, CMD_PICTURE, (ULONG)k);
}
/* a shape (canvas_sh) added in the middle of the canvas selected */
static void canvas_add_shape(int which)
{
  Doc *d = wp_doc();
  Pos ps[1];
  Image *c, g, s;
  long cw, ch;
  int sh = which >= 0 && which < 6 ? canvas_sh[which] : SH_RECT;
  if (!d || dv_chosen(wp_view(), ps, 1) != 1 || !(c = at_obj(d, ps[0])) || c->canvas != CANVAS_GROUP) { DisplayBeep(NULL); return; }
  cw = c->chw > 0 ? c->chw : c->cx; ch = c->chh > 0 ? c->chh : c->cy;
  img_defaults(&s);
  s.kind = sh < 0 ? OBJ_TEXTBOX : OBJ_SHAPE; s.shape = (u8)(sh < 0 ? SH_RECT : sh);
  s.cx = (i32)(cw / 4); s.cy = (i32)(sh == SH_LINE || sh == SH_ARROW ? ch / 6 : ch / 3);
  s.lstyle = PB_SINGLE; s.lwidth = 6; s.lcolor = sh < 0 ? 0 : 0x2F528F;
  s.fill = sh < 0 ? 0xFFFFFF : sh == SH_LINE || sh == SH_ARROW ? COLOR_AUTO : 0x4472C4;
  if (sh < 0 && !(s.text = (Story *)calloc(1, sizeof(Story)))) return;
  if (!img_clone(&g, c, 0)) { img_release(&s); return; }
  if (canvas_add(&g, &s, (i32)(cw / 2 - s.cx / 2), (i32)(ch / 2 - s.cy / 2))) {
    doc_begin(d, ps[0]);
    doc_set_object(d, ps[0], &g);
    doc_end(d, ps[0]);
    relaid();
  }
  img_release(&g); img_release(&s);
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp20_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_DR_SELECT: tool = DT_NONE; break;
  case ID_DR_LASSO: tool = tool == DT_LASSO ? DT_NONE : DT_LASSO; break;
  case ID_DR_ERASER: tool = tool == eraser ? DT_NONE : eraser; break;
  case ID_DR_ERASERLIST: if (value == 0 || value == 1) { eraser = value ? DT_ERASEPT : DT_ERASER; tool = eraser; } break;
  case ID_DR_ERASESTROKE: tool = eraser = DT_ERASER; break;
  case ID_DR_ERASEPOINT: tool = eraser = DT_ERASEPT; break;
  case ID_DR_PEN0: case ID_DR_PEN1: case ID_DR_PEN2: case ID_DR_PEN3: case ID_DR_PEN4: {
    int k = (int)(id - ID_DR_PEN0);
    if (tool == DT_PEN && curpen == k) tool = DT_NONE; else { tool = DT_PEN; curpen = k; }
    break; }
  case ID_DR_ADDPEN:
    /* the pen in use (or the black pen) again, as a pen of its own to change */
    if (nadded >= MAXADDED) { wp_ask("OK", "%s", tr("Eight pens have been added already.")); break; }
    pens[NPENS + nadded] = *wp20_pen();
    added_label(nadded);
    curpen = NPENS + nadded++;
    l_added.n = nadded;
    tool = DT_PEN;
    break;
  case ID_DR_MYPENS:
    if (value >= 0 && value < nadded) { curpen = NPENS + (int)value; tool = DT_PEN; }
    else if (!nadded) wp_ask("OK", "%s", tr("Add a pen first (Draw > Add Pen)."));
    break;
  case ID_DR_NEXTPEN:
    if (!nadded) { wp_ask("OK", "%s", tr("Add a pen first (Draw > Add Pen).")); break; }
    curpen = curpen >= NPENS && curpen + 1 < NPENS + nadded ? curpen + 1 : NPENS;
    tool = DT_PEN;
    break;
  case ID_DR_THICKLIST: case ID_DR_THICK0: case ID_DR_THICK1: case ID_DR_THICK2: case ID_DR_THICK3: case ID_DR_THICK4: case ID_DR_THICK5: {
    int k = id == ID_DR_THICKLIST ? (int)value : (int)(id - ID_DR_THICK0);
    if (k < 0 || k > 5) break;
    if (curpen < 0 || curpen >= NPENS + nadded) curpen = 0;
    pens[curpen].width = (u8)(pens[curpen].kind == INK_HIGHLIGHTER && k < 3 ? thickw[k + 3] : thickw[k]);
    if (curpen >= NPENS) added_label(curpen - NPENS);
    tool = DT_PEN;
    break; }
  case ID_DR_COLORLIST: case ID_DR_COL0: case ID_DR_COL1: case ID_DR_COL2: case ID_DR_COL3: case ID_DR_COL4: case ID_DR_COL5: case ID_DR_COL6: case ID_DR_COL7: {
    int k = id == ID_DR_COLORLIST ? (int)value : (int)(id - ID_DR_COL0);
    if (k < 0 || k > 7) break;
    /* the lasso's strokes take it; otherwise the pen in use does */
    if (recolor_chosen(colrgb[k])) break;
    if (curpen < 0 || curpen >= NPENS + nadded) curpen = 0;
    if (curpen < NPENS && curpen != 3 && curpen != 4) {
      /* the gallery's pens keep their colours: an added pen takes the new one */
      if (nadded < MAXADDED) { pens[NPENS + nadded] = pens[curpen]; curpen = NPENS + nadded++; l_added.n = nadded; }
    }
    pens[curpen].color = colrgb[k];
    if (curpen >= NPENS) added_label(curpen - NPENS);
    tool = DT_PEN;
    break; }
  case ID_DR_INKSHAPE: inkshape = !inkshape; break;
  case ID_DR_CONVERT: case ID_DR_INKCONVERT1:
    if (!convert_chosen()) wp_ask("OK", "%s", tr("Choose ink strokes first (Lasso Select), or click one. Strokes that don't look like a shape stay ink."));
    break;
  case ID_DR_CANVAS: insert_canvas(); break;
  case ID_DR_CANVASADD: canvas_add_shape((int)value); break;
  case ID_DR_REPLAY: dv_replay(wp_view()); break;
  case ID_DR_HIDE: hidden = !hidden; relaid(); break;
  case ID_DR_DELALL: delete_all_ink(); break;
  case ID_DR_DELSEL: delete_chosen(); break;
  }
  wp_refresh();
  return WPR_DONE;
}

/* the ribbon's state: the tool and pen in use, Ink to Shape, ink hidden, strokes chosen */
static void state(ULONG *on, const WpCtx *c)
{
  if (tool == DT_NONE) *on |= RBIT(ST_SELECT);
  if (tool == DT_LASSO) *on |= RBIT(ST_LASSO);
  if (tool == DT_ERASER || tool == DT_ERASEPT) *on |= RBIT(ST_ERASER);
  if (tool == DT_PEN && curpen < NPENS) *on |= RBIT(ST_PEN0 + curpen);
  if (tool == DT_PEN && curpen >= NPENS) *on |= RBIT(ST_CUSTOM);
  if (inkshape) *on |= RBIT(ST_INKSHAPE);
  if (hidden) *on |= RBIT(ST_HIDDEN);
  if (c->obj >= 0) *on |= RBIT(ST_CHOSEN);
}
/* Esc puts the pen down; Del and Backspace delete the strokes the lasso chose */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  Pos ps[2];
  if (code & IECODE_UP_PREFIX) return 0;
  if (code == 0x45 && tool != DT_NONE) return ID_DR_SELECT;
  if ((code == 0x46 || code == 0x41) && dv_chosen(wp_view(), ps, 2) > 1) return ID_DR_DELSEL;
  return 0;
}
/* the right-button menu: on ink, Ink to Shape; on a canvas, a shape added to it */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  const Image *im;
  if (c->where != WPC_OBJECT || !c->doc || c->obj < 0 || c->obj >= c->doc->nimages) return;
  im = &c->doc->images[c->obj];
  if (any_ink(im)) wp_ctx_item(menu, tr("Convert Ink to Shape"), WP_CTX_ID(ID_DR_INKCONVERT1), 1);
  if (im->canvas == CANVAS_GROUP) wp_ctx_sub(menu, tr("Add Shape to Canvas"), ID_DR_CANVASADD, canvas_shapes, 6);
}

/* ---- the ribbon: the Draw tab's groups, and Review's Ink */
static const Item i_tools[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Select", ID_DR_SELECT, RB_WP(WPI_20, ST_SELECT), 0, 0, 0, "Select text and objects (put the pen down: Esc)", NULL, IC_SELECT, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Lasso Select", ID_DR_LASSO, RB_WP(WPI_20, ST_LASSO), 0, 0, 0, "Draw round ink strokes to choose them; drag them to move them", NULL, IC_LASSO, 0 },
  { K_DROP, S_SMALL, 0, 0, "Eraser", ID_DR_ERASERLIST, RB_WP(WPI_20, ST_ERASER), 0, F_SPLIT, 0, "Rub out ink: whole strokes, or the bits under it", &l_eraser, IC_ERASER, ID_DR_ERASER } };
static const Item i_pens[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Pen", ID_DR_PEN0, RB_WP(WPI_20, ST_PEN0), 0, 0, 0, "Draw on the page with a black pen", NULL, IC_PEN0, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Red Pen", ID_DR_PEN1, RB_WP(WPI_20, ST_PEN1), 0, 0, 0, "Draw with a red pen", NULL, IC_PEN1, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Blue Pen", ID_DR_PEN2, RB_WP(WPI_20, ST_PEN2), 0, 0, 0, "Draw with a blue pen", NULL, IC_PEN2, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Pencil", ID_DR_PEN3, RB_WP(WPI_20, ST_PEN3), 0, F_NEWCOL, 0, "Draw with a pencil", NULL, IC_PENCIL, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Highlighter", ID_DR_PEN4, RB_WP(WPI_20, ST_PEN4), 0, 0, 0, "Mark text with a highlighter (it lies behind the text)", NULL, IC_HILITE, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Add Pen", ID_DR_ADDPEN, 0, 0, 0, 0, "Add a pen of your own, like the one in use, to change its colour and thickness", NULL, IC_ADDPEN, 0 },
  { K_DROP, S_SMALL, 0, 0, "Thickness", ID_DR_THICKLIST, 0, 0, F_NEWCOL, 0, "How thick the pen draws", &l_thick, IC_THICK, 0 },
  { K_DROP, S_SMALL, 0, 0, "Color", ID_DR_COLORLIST, 0, 0, 0, 0, "The pen's colour (or the colour of the strokes chosen)", &l_color, IC_COLOR, 0 },
  { K_DROP, S_SMALL, 0, 0, "My Pens", ID_DR_MYPENS, RB_WP(WPI_20, ST_CUSTOM), 0, 0, 0, "The pens added with Add Pen", &l_added, IC_MYPENS, 0 } };
static const Item i_convert[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Ink to Shape", ID_DR_INKSHAPE, RB_WP(WPI_20, ST_INKSHAPE), 0, 0, 0, "Strokes drawn from now on that look like shapes become those shapes", NULL, IC_INKSHAPE, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Convert", ID_DR_CONVERT, 0, 0, 0, 0, "The strokes chosen that look like shapes become those shapes", NULL, IC_CONVERT, 0 },
  /* (Review's Ink group has it too, but Review has no room left on a 640-pixel screen) */
  { K_BUTTON, S_SMALL, 0, 0, "Hide Ink", ID_DR_HIDE, RB_WP(WPI_20, ST_HIDDEN), 0, 0, 0, "Hide the ink in the document, or show it again", NULL, IC_HIDE, 0 } };
static const Item i_insert[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Drawing Canvas", ID_DR_CANVAS, 0, 0, 0, 0, "A drawing canvas in the text: shapes and ink in it move as one", NULL, IC_CANVAS, 0 } };
static const Item i_replay[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Ink Replay", ID_DR_REPLAY, 0, 0, 0, 0, "The ink drawn again, stroke by stroke, in the order it was drawn", NULL, IC_REPLAY, 0 } };
static const Item i_ink[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Hide Ink", ID_DR_HIDE, RB_WP(WPI_20, ST_HIDDEN), 0, 0, 0, "Hide the ink in the document, or show it again", NULL, IC_HIDE, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Delete All Ink", ID_DR_DELALL, 0, 0, 0, 0, "Delete every ink stroke in the document", NULL, IC_DELINK, 0 } };
static const WpGroup groups[] = {
  { TAB_DRAW, -1, { "Tools", G_COLS, i_tools, 3 } },
  { TAB_DRAW, -1, { "Pens", G_COLS, i_pens, 9 } },
  { TAB_DRAW, -1, { "Convert", G_COLS, i_convert, 3 } },
  { TAB_DRAW, -1, { "Insert", G_COLS, i_insert, 1 } },
  { TAB_DRAW, -1, { "Replay", G_COLS, i_replay, 1 } },
  { TAB_REVIEW, -1, { "Ink", G_COLS, i_ink, 2 } } };

static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Drawing Canvas", 0, 0, 0, (APTR)ID_DR_CANVAS } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Draw", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Select", 0, 0, 0, (APTR)ID_DR_SELECT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Lasso Select", 0, 0, 0, (APTR)ID_DR_LASSO } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Stroke Eraser", 0, 0, 0, (APTR)ID_DR_ERASESTROKE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Point Eraser", 0, 0, 0, (APTR)ID_DR_ERASEPOINT } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Pen", 0, 0, 0, (APTR)ID_DR_PEN0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Red Pen", 0, 0, 0, (APTR)ID_DR_PEN1 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Blue Pen", 0, 0, 0, (APTR)ID_DR_PEN2 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Pencil", 0, 0, 0, (APTR)ID_DR_PEN3 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Highlighter", 0, 0, 0, (APTR)ID_DR_PEN4 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Add Pen", 0, 0, 0, (APTR)ID_DR_ADDPEN } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Next Added Pen", 0, 0, 0, (APTR)ID_DR_NEXTPEN } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Ink to Shape", 0, 0, 0, (APTR)ID_DR_INKSHAPE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Convert Ink to Shape", 0, 0, 0, (APTR)ID_DR_CONVERT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Ink Replay", 0, 0, 0, (APTR)ID_DR_REPLAY } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Pen Thickness", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"0.25 mm", 0, 0, 0, (APTR)ID_DR_THICK0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"0.5 mm", 0, 0, 0, (APTR)ID_DR_THICK1 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"1 mm", 0, 0, 0, (APTR)ID_DR_THICK2 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"2 mm", 0, 0, 0, (APTR)ID_DR_THICK3 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"4 mm", 0, 0, 0, (APTR)ID_DR_THICK4 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"6 mm", 0, 0, 0, (APTR)ID_DR_THICK5 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Pen Color", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Black", 0, 0, 0, (APTR)ID_DR_COL0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Red", 0, 0, 0, (APTR)ID_DR_COL1 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Blue", 0, 0, 0, (APTR)ID_DR_COL2 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Green", 0, 0, 0, (APTR)ID_DR_COL3 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Yellow", 0, 0, 0, (APTR)ID_DR_COL4 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Orange", 0, 0, 0, (APTR)ID_DR_COL5 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Purple", 0, 0, 0, (APTR)ID_DR_COL6 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Grey", 0, 0, 0, (APTR)ID_DR_COL7 } },
  { WM_REVIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Hide Ink", 0, 0, 0, (APTR)ID_DR_HIDE } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Delete All Ink", 0, 0, 0, (APTR)ID_DR_DELALL } } };

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp20_package = {
  "WP20", ID_WP20_FIRST, ID_WP20_LAST,
  wp20_command, state, NULL, ctxmenu, key, NULL, NULL,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};
