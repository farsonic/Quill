/*
 * wp_shapes.c - WP18 Shapes and text boxes (docs/wordcraft-gap.md). This file belongs to WP18: only that
 * package edits it. Its commands are ID_WP18_FIRST .. ID_WP18_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Here: objects drawn with their fills and effects on the screen (core/objfx.c's raster dithered
 * to the view's colour cube, kept for the last few); gradient and picture fills, shadow, glow and
 * soft edges; the Format Shape pane (a window beside the page that follows the selection and changes
 * the object at once: fill, line, effects, size, text box, and for a picture its adjustments); text
 * direction and anchoring; linked text boxes (Create Link, Break Link); Word's other preset shapes
 * and the icon library, each chosen from a window of pictures.
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
#include "wp_objects.h"
#include "theme.h"
#include "lang.h"
#include "../core/objfx.h"
#include "../core/objtext.h"
#include "../core/docx.h"

enum {
  ID_SH_GRADDD = ID_WP18_FIRST, ID_SH_PICFILL, ID_SH_EFFECTSDD, ID_SH_PANE, ID_SH_TEXTDIRDD, ID_SH_ALIGNDD, ID_SH_LINK, ID_SH_UNLINK,
  ID_SH_ICONS, ID_SH_MORE, ID_SH_PICK_SHAPE, ID_SH_PICK_ICON, ID_SH_PANE_APPLY, ID_SH_PANE_PICFILE, ID_SH_LINK_DO,
  ID_SH_DIR0, ID_SH_DIR1, ID_SH_DIR2, ID_SH_AL0, ID_SH_AL1, ID_SH_AL2, ID_SH_EFFECTS, ID_SH_GRADIENT
};
/* state bits */
enum { SB_PANE = 1, SB_TEXT, SB_CANLINK, SB_LINKED, SB_LINKING };

/* ------------------------------------------------------------------ drawing on the screen */
/* the last few objects drawn, as pens (and whether each pixel shows), for the zoom they were drawn at */
typedef struct { u32 key; LONG zoom; i32 w, h, ox, oy; UBYTE *pen, *on; } RasterCache;
#define NCACHE 6
static RasterCache cache[NCACHE];
static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
/* what changes how an object looks, hashed */
static u32 look_key(const Image *t)
{
  u32 h = 2166136261UL;
  const u8 *p;
  size_t i;
  ObjFx fx = t->fx;
  struct { const void *data; i32 cx, cy, crop[4]; u16 rot; u8 flip, kind, shape, lstyle, lwidth; u32 lcolor, fill; } k;
  memset(&k, 0, sizeof k);
  k.data = t->data; k.cx = t->cx; k.cy = t->cy; memcpy(k.crop, t->crop, sizeof k.crop); k.rot = t->rot; k.flip = t->flip; k.kind = t->kind;
  k.shape = t->shape; k.lstyle = t->lstyle; k.lwidth = t->lwidth; k.lcolor = t->lcolor; k.fill = t->fill;
  fx.raw = NULL;
  for (p = (const u8 *)&k, i = 0; i < sizeof k; i++) h = (h ^ p[i]) * 16777619UL;
  for (p = (const u8 *)&fx, i = 0; i < sizeof fx; i++) h = (h ^ p[i]) * 16777619UL;
  return h;
}
void wp18_flush(void)
{
  int i;
  for (i = 0; i < NCACHE; i++) { if (cache[i].pen) FreeVec(cache[i].pen); if (cache[i].on) FreeVec(cache[i].on); memset(&cache[i], 0, sizeof cache[i]); }
}
int wp18_draw_rich(struct RastPort *rp, const Image *t, LONG zoom, LONG x, LONG y, const LONG *cube, int lv, LONG band0, LONG band1)
{
  u32 key = look_key(t);
  RasterCache *c = NULL;
  LONG bw = GetBitMapAttr(rp->BitMap, BMA_WIDTH), bh = GetBitMapAttr(rp->BitMap, BMA_HEIGHT), row, i;
  UBYTE *line;
  for (i = 0; i < NCACHE; i++) if (cache[i].pen && cache[i].key == key && cache[i].zoom == zoom) { c = &cache[i]; break; }
  if (!c) {
    ObjRaster r;
    LONG n;
    if (lv < 2 || !objfx_render(t, zoom, 2000, &r)) return 0;
    /* the oldest kept goes */
    if (cache[NCACHE - 1].pen) { FreeVec(cache[NCACHE - 1].pen); FreeVec(cache[NCACHE - 1].on); }
    memmove(cache + 1, cache, (NCACHE - 1) * sizeof cache[0]);
    c = &cache[0];
    memset(c, 0, sizeof *c);
    n = (LONG)r.w * r.h;
    c->pen = (UBYTE *)AllocVec((ULONG)(n ? n : 1), MEMF_ANY); c->on = (UBYTE *)AllocVec((ULONG)(n ? n : 1), MEMF_ANY);
    if (!c->pen || !c->on) { if (c->pen) FreeVec(c->pen); if (c->on) FreeVec(c->on); c->pen = c->on = NULL; objraster_free(&r); return 0; }
    c->key = key; c->zoom = zoom; c->w = r.w; c->h = r.h; c->ox = r.ox; c->oy = r.oy;
    for (row = 0; row < r.h; row++) {
      LONG col;
      for (col = 0; col < r.w; col++) {
        const u8 *p = r.px + ((size_t)row * r.w + col) * 4;
        int tt = bayer[row & 3][col & 3] * 255 / 16, q[3], j;
        /* transparency by leaving pixels out in the same ordered pattern */
        c->on[row * r.w + col] = (UBYTE)(p[3] > (bayer[(row + 2) & 3][(col + 1) & 3] * 16 + 8));
        for (j = 0; j < 3; j++) { q[j] = (p[j] * (lv - 1) + tt) / 255; if (q[j] > lv - 1) q[j] = lv - 1; }
        c->pen[row * r.w + col] = (UBYTE)cube[(q[0] * lv + q[1]) * lv + q[2]];
      }
    }
    objraster_free(&r);
  }
  if (!(line = (UBYTE *)AllocVec((ULONG)c->w + 1, MEMF_ANY))) return 0;
  x += c->ox; y += c->oy;
  for (row = 0; row < c->h; row++) {
    LONG yy = y + row, col, run = -1;
    if (yy < 0 || yy >= bh || yy < band0 || (band1 && yy > band1)) continue;
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
  return 1;
}

/* ------------------------------------------------------------------ the document's objects */
static int sel_obj(void)
{
  LONG k = -1;
  Doc *d = wp_doc();
  get(wp_view(), MUIA_Quill_Object, &k);
  return d && k >= 0 && k < d->nimages ? (int)k : -1;
}
static void obj_set(const Image *m) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)m); }
/* where object k's reference is in the body; 0 if it isn't there */
static int find_ref(Doc *d, int k, Pos *at)
{
  u32 i, j;
  for (i = 0; i < d->np; i++)
    for (j = 0; j + 4 <= d->p[i]->len; j++)
      if (IS_PIC(d->p[i]->t + j)) { if (pic_index(d->p[i]->t + j) == k) { at->para = i; at->off = j; return 1; } j += 3; }
  return 0;
}
/* object k selected (its reference), so a CMD_OBJSET changes it */
static int select_obj(Doc *d, int k)
{
  Pos ab[2];
  if (!find_ref(d, k, &ab[0])) return 0;
  ab[1] = ab[0]; ab[1].off += 4;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
  return 1;
}
static u32 mix(u32 a, u32 b, int pct)     /* a towards b by pct per cent */
{
  int s, o = 0;
  for (s = 16; s >= 0; s -= 8) { int x = (int)((a >> s) & 255), y = (int)((b >> s) & 255); o |= (x + (y - x) * pct / 100) << s; }
  return (u32)o;
}

/* ------------------------------------------------------------------ ribbon lists */
static const char *const grad_labels[] = { "No Gradient", "Light Variation: Linear Down", "Light Variation: Linear Right", "Light Variation: Linear Diagonal",
  "Light Variation: From Center", "Dark Variation: Linear Up", "Dark Variation: Linear Left", "Dark Variation: From Corners", "More Gradients..." };
static const RibbonList grad_list = { grad_labels, 9, PL_TEXT, NULL };
static const char *const fx_labels[] = { "No Effects", "Shadow: Offset Bottom Right", "Shadow: Offset Bottom", "Shadow: Offset Center",
  "Glow: 5 pt", "Glow: 10 pt", "Glow: 18 pt", "Soft Edges: 2.5 pt", "Soft Edges: 5 pt", "Soft Edges: 10 pt", "Soft Edges: 25 pt", "Effects Options..." };
static const RibbonList fx_list = { fx_labels, 12, PL_TEXT, NULL };
static const char *const dir_labels[] = { "Horizontal", "Rotate all text 90\xB0", "Rotate all text 270\xB0" };
static const RibbonList dir_list = { dir_labels, 3, PL_TEXT, NULL };
static const char *const align_labels[] = { "Top", "Middle", "Bottom" };
static const RibbonList align_list = { align_labels, 3, PL_TEXT, NULL };

/* the icons (theme.c's language) */
#define XI_ICONS "ca E 8 5 4 F 6 9 10 11 ci F 7 12 9 12 L 2 2 3 3 L 14 2 13 3"
#define XI_MORE "ca P 4 1 14 8 1 15 14 1 14 cg E 12 4 2 co F 9 9 14 14"
#define XI_GRAD "cd F 2 2 13 4 ci F 2 5 13 7 ca F 2 8 13 10 ca F 2 11 13 13 ci R 1 1 14 14"
#define XI_PICFILL "ci R 1 1 14 14 cg P 4 2 12 6 7 9 10 13 12 ca E 11 5 2"
#define XI_EFFECTS "cd F 4 4 14 14 ca F 2 2 11 11 ci R 2 2 11 11"
#define XI_PANE "ci R 1 1 14 14 L 10 1 10 14 ca F 11 3 13 4 F 11 6 13 7 F 11 9 13 10 cd L 3 4 8 4 L 3 7 8 7"
#define XI_TEXTDIR "ci L 3 2 3 13 L 1 4 3 2 L 5 4 3 2 ca B A"
#define XI_ALIGN "ci R 1 1 14 14 ca L 4 7 11 7 L 4 9 11 9"
#define XI_LINK "ci R 1 3 6 12 R 9 3 14 12 ca L 6 7 9 7 L 8 6 9 7 L 8 8 9 7"
#define XI_UNLINK "ci R 1 3 6 12 R 9 3 14 12 cr L 6 5 9 10 L 9 5 6 10"

static const Item i_icons[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Icons", ID_SH_ICONS, 0, 0, 0, 0, "A picture from the icon library, drawn as a shape you can colour", NULL, XI_ICONS },
  { K_BUTTON, S_SMALL, 0, 0, "More Shapes", ID_SH_MORE, 0, 0, 0, 0, "Word's other shapes: callouts, flowchart, block arrows, stars", NULL, XI_MORE } };
static const Item s_fx[] = {
  { K_DROP, S_SMALL, 0, 0, "Gradient", ID_SH_GRADDD, 0, RB_ONESHAPE, 0, 0, "The shape filled with colours running into each other", &grad_list, XI_GRAD },
  { K_DROP, S_SMALL, 0, 0, "Shape Effects", ID_SH_EFFECTSDD, 0, RB_SHAPESEL, 0, 0, "A shadow, a glow or soft edges", &fx_list, XI_EFFECTS },
  { K_BUTTON, S_SMALL, 0, 0, "Picture Fill", ID_SH_PICFILL, 0, RB_ONESHAPE, 0, 0, "A picture stretched over the shape", NULL, XI_PICFILL },
  { K_BUTTON, S_SMALL, 0, 0, "Format Pane", ID_SH_PANE, RB_WP(WPI_18, SB_PANE), 0, F_NEWCOL, 0, "The Format Shape pane: fill, line, effects, size and text box beside the page", NULL, XI_PANE },
  { K_BUTTON, S_SMALL, 0, 0, "More Shapes", ID_SH_MORE, 0, 0, 0, 0, "Word's other shapes: callouts, flowchart, block arrows, stars", NULL, XI_MORE },
  { K_BUTTON, S_SMALL, 0, 0, "Icons", ID_SH_ICONS, 0, 0, 0, 0, "A picture from the icon library", NULL, XI_ICONS } };
static const Item s_text[] = {
  { K_DROP, S_SMALL, 0, 0, "Text Direction", ID_SH_TEXTDIRDD, 0, RB_WP(WPI_18, SB_TEXT), 0, 0, "The text across, or turned to run down", &dir_list, XI_TEXTDIR },
  { K_DROP, S_SMALL, 0, 0, "Align Text", ID_SH_ALIGNDD, 0, RB_WP(WPI_18, SB_TEXT), 0, 0, "The text at the top, in the middle or at the bottom of the box", &align_list, XI_ALIGN },
  { K_BUTTON, S_SMALL, 0, 0, "Create Link", ID_SH_LINK, RB_WP(WPI_18, SB_LINKING), RB_WP(WPI_18, SB_CANLINK), 0, 0, "The text goes on into an empty text box you click next", NULL, XI_LINK },
  { K_BUTTON, S_SMALL, 0, 0, "Break Link", ID_SH_UNLINK, 0, RB_WP(WPI_18, SB_LINKED), F_NEWCOL, 0, "The text stops at this box", NULL, XI_UNLINK } };
static const WpGroup groups[] = {
  { TAB_INSERT, 3, { "Icons", G_COLS, i_icons, 2 } },
  { TAB_SHAPE, 2, { "Fill & Effects", G_COLS, s_fx, 6 } },
  { TAB_SHAPE, 2, { "Text", G_COLS, s_text, 4 } } };
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Icons...", 0, 0, 0, (APTR)ID_SH_ICONS } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"More Shapes...", 0, 0, 0, (APTR)ID_SH_MORE } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Shape", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Format Shape Pane", 0, 0, 0, (APTR)ID_SH_PANE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Gradient...", 0, 0, 0, (APTR)ID_SH_GRADIENT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Picture Fill...", 0, 0, 0, (APTR)ID_SH_PICFILL } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Shape Effects...", 0, 0, 0, (APTR)ID_SH_EFFECTS } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text Across", 0, 0, 0, (APTR)ID_SH_DIR0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text Turned 90\xB0", 0, 0, 0, (APTR)ID_SH_DIR1 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text Turned 270\xB0", 0, 0, 0, (APTR)ID_SH_DIR2 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text at the Top", 0, 0, 0, (APTR)ID_SH_AL0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text in the Middle", 0, 0, 0, (APTR)ID_SH_AL1 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text at the Bottom", 0, 0, 0, (APTR)ID_SH_AL2 } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Create Link", 0, 0, 0, (APTR)ID_SH_LINK } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Break Link", 0, 0, 0, (APTR)ID_SH_UNLINK } } };
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))

/* ------------------------------------------------------------------ the pickers: windows of shapes and icons */
/* a cell: a shape's or icon's picture; pressing it notes which (picked) before MUI returns the id */
#define MUIA_Cell_Which (QTAG + 0x1880)
#define MUIA_Cell_Icon  (QTAG + 0x1881)
struct CellData { int which, icon; };
static struct MUI_CustomClass *cellcls;
static int picked = -1;
static void draw_cell(struct RastPort *rp, struct CellData *c, LONG x0, LONG y0, LONG s, int sel)
{
  theme_fill(rp, x0, y0, x0 + s - 1, y0 + s - 1, tpen(sel ? T_CHECKED : T_WHITE));
  theme_rect(rp, x0, y0, x0 + s - 1, y0 + s - 1, tpen(T_BORDER2));
  SetAPen(rp, tpen(T_ICON));
  if (c->icon) {
    i32 n = s - 6, i;
    u8 *m = (u8 *)calloc((size_t)n * n, 1);
    if (!m) return;
    objicon_mask(c->which, m, n, n);
    for (i = 0; i < n * n; i++) if (m[i] > 127) WritePixel(rp, x0 + 3 + i % n, y0 + 3 + i / n);
    free(m);
  } else {
    i32 xy[2 * SHAPE_MAXPTS], w = (s - 8) * 100, h = (s - 8) * 100;
    int closed, i;
    u32 n;
    if (c->which >= SHX_ROUNDCALLOUT && c->which <= SHX_OVALCALLOUT) h = h * 8 / 9;
    if (c->which == SH_LINE || c->which == SH_ARROW) h = h * 2 / 3;
    n = shape_points(c->which, w, h, xy, SHAPE_MAXPTS, &closed);
    SetAPen(rp, tpen(T_ACCENT));
    for (i = 0; i < (int)n + (closed ? 1 : 0) - 1; i++) {
      int j = (i + 1) % (int)n;
      Move(rp, x0 + 4 + xy[2 * i] / 100, y0 + 4 + xy[2 * i + 1] / 100); Draw(rp, x0 + 4 + xy[2 * j] / 100, y0 + 4 + xy[2 * j + 1] / 100);
    }
  }
}
static ULONG cdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct CellData *c;
  switch (msg->MethodID) {
  case OM_NEW: {
    struct opSet *m = (struct opSet *)msg;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    c = INST_DATA(cl, obj);
    c->which = (int)GetTagData(MUIA_Cell_Which, 0, m->ops_AttrList);
    c->icon = (int)GetTagData(MUIA_Cell_Icon, 0, m->ops_AttrList);
    return (ULONG)obj;
  }
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Pressed, ((struct opSet *)msg)->ops_AttrList);
    c = INST_DATA(cl, obj);
    if (t && !t->ti_Data) picked = c->which;      /* (before the notification returns the id) */
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 30; m->MinMaxInfo->DefWidth += 30; m->MinMaxInfo->MaxWidth += 30;
    m->MinMaxInfo->MinHeight += 30; m->MinMaxInfo->DefHeight += 30; m->MinMaxInfo->MaxHeight += 30;
    return 0;
  }
  case MUIM_Draw: {
    ULONG sel = 0;
    DoSuperMethodA(cl, obj, msg);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    get(obj, MUIA_Selected, &sel);
    draw_cell(_rp(obj), INST_DATA(cl, obj), _mleft(obj), _mtop(obj), _mwidth(obj) < _mheight(obj) ? _mwidth(obj) : _mheight(obj), (int)sel);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static Object *cell(int which, int icon, ULONG id, const char *help)
{
  Object *o;
  if (!cellcls && !(cellcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct CellData), (APTR)cdispatch))) return NULL;
  o = (Object *)NewObject(cellcls->mcc_Class, NULL, MUIA_Cell_Which, which, MUIA_Cell_Icon, icon, MUIA_InputMode, MUIV_InputMode_RelVerify,
                          MUIA_ShortHelp, (ULONG)help, MUIA_CycleChain, 1, TAG_DONE);
  if (o) DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id);
  return o;
}
static Object *shapewin, *iconwin;
static const char *pagetitles[SC_N + 1 > IC_CAT_N + 1 ? SC_N + 1 : IC_CAT_N + 1];
/* a window of pages (Register), each a grid of cells */
static Object *picker(int icons)
{
  Object *reg, *w, *pages[IC_CAT_N > SC_N ? IC_CAT_N : SC_N];
  int np = icons ? IC_CAT_N : SC_N, p, i;
  for (p = 0; p < np; p++) {
    pagetitles[p] = tr(icons ? objicon_category[p] : shapex_category[p]);
    pages[p] = ColGroup(10), MUIA_Group_Spacing, 2, End;
    if (!pages[p]) return NULL;
    if (icons) {
      for (i = 0; i < objicon_count(); i++) if (objicon_cat(i) == p) { Object *c = cell(i, 1, ID_SH_PICK_ICON, tr(objicon_name(i))); if (c) DoMethod(pages[p], OM_ADDMEMBER, (ULONG)c); }
    } else {
      int first, end;
      shapex_range(p, &first, &end);
      for (i = first; i < end; i++) { Object *c = cell(i, 0, ID_SH_PICK_SHAPE, tr(shapex_label(i))); if (c) DoMethod(pages[p], OM_ADDMEMBER, (ULONG)c); }
    }
    /* the last row filled out so the cells keep their size */
    for (i = 0; i < 10; i++) DoMethod(pages[p], OM_ADDMEMBER, (ULONG)HSpace(0));
  }
  pagetitles[np] = NULL;
  reg = RegisterGroup(pagetitles), MUIA_CycleChain, 1, End;
  if (!reg) return NULL;
  for (p = 0; p < np; p++) DoMethod(reg, OM_ADDMEMBER, (ULONG)pages[p]);
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(icons ? "Insert Icons" : "More Shapes"), MUIA_Window_ID, icons ? MAKE_ID('I','C','O','N') : MAKE_ID('S','H','P','X'),
    WindowContents, VGroup, QDLG, Child, reg, End, End;
  if (!w) return NULL;
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  return w;
}

/* ------------------------------------------------------------------ the Format Shape pane */
static const u32 pal[16] = { 0x000000, 0xFFFFFF, 0x595959, 0xA5A5A5, 0x4472C4, 0x1F3864, 0xBDD7EE, 0xED7D31, 0xFFC000, 0xFFFF00, 0x70AD47, 0x375623, 0xC00000, 0xFF99CC, 0x7030A0, 0x00B0F0 };
static const char *const palnames[17] = { "Black", "White", "Dark Grey", "Grey", "Blue", "Dark Blue", "Light Blue", "Orange", "Gold", "Yellow", "Green",
                                          "Dark Green", "Red", "Pink", "Purple", "Turquoise", "Other" };
static const char *const filltypes[4] = { "No Fill", "Solid Fill", "Gradient Fill", "Picture Fill" };
static const char *const gtypes[3] = { "Linear", "Radial", "Rectangular" };
static const char *const lines_[5] = { "No Line", "Solid Line", "Dotted", "Dashed", "Double" };
static const char *const weights[9] = { "1/4 pt", "1/2 pt", "3/4 pt", "1 pt", "1 1/2 pt", "2 1/4 pt", "3 pt", "4 1/2 pt", "6 pt" };
static const u8 weight8[9] = { 2, 4, 6, 8, 12, 18, 24, 36, 48 };
static const char *const valigns[3] = { "Top", "Middle", "Bottom" };
static const char *const tdirs[3] = { "Horizontal", "Rotate all text 90\xB0", "Rotate all text 270\xB0" };
static const char *const cmodes[PCM_N] = { "No Recolor", "Grayscale", "Sepia", "Washout", "Black and White", "Recolor: Dark", "Recolor: Light" };
static const char *const arts[ART_N] = { "None", "Blur", "Pencil Grayscale", "Line Drawing", "Photocopy", "Cutout", "Film Grain", "Glow Edges", "Mosaic Bubbles" };
static const char *const panepages[6] = { "Fill & Line", "Effects", "Size", "Text Box", "Picture", NULL };
static const char *tl[16][18];          /* the cycles' entries translated */
enum { G_FILLTYPE, G_FILLCOL, G_FILLALPHA, G_GTYPE, G_GANGLE, G_GCOL2, G_LINE, G_LINECOL, G_WEIGHT,
       G_SHADOW, G_SHCOL, G_SHALPHA, G_SHDIST, G_SHBLUR, G_SHDIR, G_GLOW, G_GLCOL, G_GLALPHA, G_SOFT,
       G_H, G_W, G_ROT, G_VALIGN, G_TDIR, G_INS0, G_INS1, G_INS2, G_INS3,
       G_BRIGHT, G_CONTRAST, G_SHARP, G_SAT, G_TEMP, G_ALPHA, G_CMODE, G_RECOL, G_ART, G_N };
static Object *pane, *pg[G_N], *panereg;
static int pane_k = -1;                 /* the object the pane shows */
static u32 pane_ver;
static const char *const *tlist(int slot, const char *const *src, int n)
{
  int i;
  for (i = 0; i < n; i++) tl[slot][i] = tr(src[i]);
  tl[slot][n] = NULL;
  return tl[slot];
}
static Object *cyc(int slot, const char *const *src, int n) { return CycleObject, MUIA_Cycle_Entries, (ULONG)tlist(slot, src, n), MUIA_CycleChain, 1, End; }
static Object *sld(LONG lo, LONG hi) { return SliderObject, MUIA_Numeric_Min, lo, MUIA_Numeric_Max, hi, MUIA_CycleChain, 1, End; }
static Object *str(void) { return StringObject, StringFrame, MUIA_String_MaxLen, 12, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End; }
static Object *lab(const char *s) { return QLabel(tr(s), MUIO_Label_DoubleFrame); }
static int palette_index(u32 c) { int i; for (i = 0; i < 16; i++) if (pal[i] == (c & 0xFFFFFF)) return i; return 16; }
static int metric(void) { Doc *d = wp_doc(); return !d || d->page.width != 12240; }
/* twips as centimetres or inches with two decimals, and back */
static void units_text(i32 tw, char *b)
{
  long v = metric() ? ((long)tw * 254 + 720) / 1440 : ((long)tw * 100 + 720) / 1440;
  sprintf(b, "%ld.%02ld", v / 100, v % 100);
}
static i32 units_parse(Object *o)
{
  STRPTR s = NULL;
  long w = 0, f = 0, k = 0;
  get(o, MUIA_String_Contents, &s);
  if (!s) return 0;
  while (*s >= '0' && *s <= '9') w = w * 10 + (*s++ - '0');
  if (*s == '.' || *s == ',') for (s++; *s >= '0' && *s <= '9' && k < 2; s++, k++) f = f * 10 + (*s - '0');
  while (k++ < 2) f *= 10;
  w = w * 100 + f;
  return (i32)(metric() ? w * 1440 / 254 : w * 1440 / 100);
}
static void open_pane(int page);
int wpget_open(Object *w);
static void pane_build(void)
{
  Object *fill, *effects, *size, *text, *pic, *g;
  const char *un = metric() ? "cm" : "in";
  char hl[40], wl[40];
  int i;
  sprintf(hl, "%s (%s)", tr("Height"), un); sprintf(wl, "%s (%s)", tr("Width"), un);
  fill = ColGroup(2),
    Child, lab("Fill"), Child, pg[G_FILLTYPE] = cyc(0, filltypes, 4),
    Child, lab("Color"), Child, pg[G_FILLCOL] = cyc(1, palnames, 17),
    Child, lab("Transparency"), Child, pg[G_FILLALPHA] = sld(0, 100),
    Child, lab("Gradient"), Child, pg[G_GTYPE] = cyc(2, gtypes, 3),
    Child, lab("Angle"), Child, pg[G_GANGLE] = sld(0, 359),
    Child, lab("Second Color"), Child, pg[G_GCOL2] = cyc(3, palnames, 17),
    Child, lab("Picture"), Child, g = QButton(tr("_Insert Picture...")),
    Child, lab("Line"), Child, pg[G_LINE] = cyc(4, lines_, 5),
    Child, lab("Line Color"), Child, pg[G_LINECOL] = cyc(5, palnames, 17),
    Child, lab("Width"), Child, pg[G_WEIGHT] = cyc(6, weights, 9),
  End;
  effects = ColGroup(2),
    Child, lab("Shadow"), Child, HGroup, Child, pg[G_SHADOW] = CheckMark(FALSE), Child, HSpace(0), End,
    Child, lab("Shadow Color"), Child, pg[G_SHCOL] = cyc(7, palnames, 17),
    Child, lab("Transparency"), Child, pg[G_SHALPHA] = sld(0, 100),
    Child, lab("Distance (pt)"), Child, pg[G_SHDIST] = sld(0, 30),
    Child, lab("Blur (pt)"), Child, pg[G_SHBLUR] = sld(0, 30),
    Child, lab("Angle"), Child, pg[G_SHDIR] = sld(0, 359),
    Child, lab("Glow Size (pt)"), Child, pg[G_GLOW] = sld(0, 30),
    Child, lab("Glow Color"), Child, pg[G_GLCOL] = cyc(8, palnames, 17),
    Child, lab("Glow Transparency"), Child, pg[G_GLALPHA] = sld(0, 100),
    Child, lab("Soft Edges (pt)"), Child, pg[G_SOFT] = sld(0, 25),
  End;
  size = ColGroup(2),
    Child, QLabel(hl, MUIO_Label_DoubleFrame), Child, pg[G_H] = str(),
    Child, QLabel(wl, MUIO_Label_DoubleFrame), Child, pg[G_W] = str(),
    Child, lab("Rotation"), Child, pg[G_ROT] = sld(0, 359),
  End;
  text = ColGroup(2),
    Child, lab("Vertical Alignment"), Child, pg[G_VALIGN] = cyc(9, valigns, 3),
    Child, lab("Text Direction"), Child, pg[G_TDIR] = cyc(10, tdirs, 3),
    Child, lab("Left Margin"), Child, pg[G_INS0] = str(),
    Child, lab("Top Margin"), Child, pg[G_INS1] = str(),
    Child, lab("Right Margin"), Child, pg[G_INS2] = str(),
    Child, lab("Bottom Margin"), Child, pg[G_INS3] = str(),
  End;
  pic = ColGroup(2),
    Child, lab("Brightness"), Child, pg[G_BRIGHT] = sld(-100, 100),
    Child, lab("Contrast"), Child, pg[G_CONTRAST] = sld(-100, 100),
    Child, lab("Sharpness"), Child, pg[G_SHARP] = sld(-100, 100),
    Child, lab("Saturation"), Child, pg[G_SAT] = sld(0, 400),
    Child, lab("Temperature"), Child, pg[G_TEMP] = sld(1500, 11500),
    Child, lab("Transparency"), Child, pg[G_ALPHA] = sld(0, 100),
    Child, lab("Color"), Child, pg[G_CMODE] = cyc(11, cmodes, PCM_N),
    Child, lab("Recolor"), Child, pg[G_RECOL] = cyc(12, palnames, 17),
    Child, lab("Artistic Effects"), Child, pg[G_ART] = cyc(13, arts, ART_N),
  End;
  for (i = 0; i < 5; i++) tl[14][i] = tr(panepages[i]);
  tl[14][5] = NULL;
  pane = QWindowObject, MUIA_Window_Title, (ULONG)tr("Format Shape"), MUIA_Window_ID, MAKE_ID('F','M','T','S'), MUIA_Window_Activate, FALSE,
    WindowContents, VGroup, QDLG,
      Child, panereg = RegisterGroup(tl[14]), MUIA_CycleChain, 1, Child, fill, Child, effects, Child, size, Child, text, Child, pic, End,
    End, End;
  if (!pane) return;
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)pane);
  DoMethod(pane, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)pane, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(pane, MUIM_Notify, MUIA_Window_Open, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SH_PANE_APPLY);
  DoMethod(g, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SH_PANE_PICFILE);
  for (i = 0; i < G_N; i++) {
    ULONG attr;
    if (!pg[i]) continue;
    attr = i == G_SHADOW ? MUIA_Selected : (i == G_H || i == G_W || (i >= G_INS0 && i <= G_INS3)) ? MUIA_String_Acknowledge :
           (i == G_FILLTYPE || i == G_FILLCOL || i == G_GTYPE || i == G_GCOL2 || i == G_LINE || i == G_LINECOL || i == G_WEIGHT || i == G_SHCOL || i == G_GLCOL ||
            i == G_VALIGN || i == G_TDIR || i == G_CMODE || i == G_RECOL || i == G_ART) ? MUIA_Cycle_Active : MUIA_Numeric_Value;
    DoMethod(pg[i], MUIM_Notify, attr, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SH_PANE_APPLY);
  }
}
static void nn(int g, ULONG attr, LONG v) { if (pg[g]) nnset(pg[g], attr, v); }
/* the pane shows object k */
static void pane_show(int k)
{
  Doc *d = wp_doc();
  const Image *im;
  const ObjFx *f;
  char b[24];
  int i, w8 = 0, pic, shape;
  pane_k = k; pane_ver = d ? d->ver : 0;
  for (i = 0; i < G_N; i++) if (pg[i]) set(pg[i], MUIA_Disabled, k < 0);
  if (k < 0 || !d) return;
  im = &d->images[k]; f = &im->fx;
  pic = im->kind == OBJ_PICTURE; shape = im->kind == OBJ_SHAPE || im->kind == OBJ_TEXTBOX;
  set(pane, MUIA_Window_Title, (ULONG)tr(pic ? "Format Picture" : "Format Shape"));
  nn(G_FILLTYPE, MUIA_Cycle_Active, f->filltype == FT_GRAD ? 2 : f->filltype == FT_PIC && im->data ? 3 : im->fill == COLOR_AUTO ? 0 : 1);
  nn(G_FILLCOL, MUIA_Cycle_Active, palette_index(f->filltype == FT_GRAD ? f->gcol[0] : im->fill));
  nn(G_FILLALPHA, MUIA_Numeric_Value, f->falpha);
  nn(G_GTYPE, MUIA_Cycle_Active, f->gtype);
  nn(G_GANGLE, MUIA_Numeric_Value, f->gangle / 10);
  nn(G_GCOL2, MUIA_Cycle_Active, palette_index(f->ngs >= 2 ? f->gcol[f->ngs - 1] : 0xFFFFFF));
  nn(G_LINE, MUIA_Cycle_Active, im->lstyle == PB_NONE || !im->lwidth ? 0 : im->lstyle == PB_DOTTED ? 2 : im->lstyle == PB_DASHED ? 3 : im->lstyle == PB_DOUBLE ? 4 : 1);
  nn(G_LINECOL, MUIA_Cycle_Active, palette_index(im->lcolor));
  for (i = 0; i < 9; i++) if (weight8[i] <= im->lwidth) w8 = i;
  nn(G_WEIGHT, MUIA_Cycle_Active, w8);
  nn(G_SHADOW, MUIA_Selected, f->shadow != 0);
  nn(G_SHCOL, MUIA_Cycle_Active, palette_index(f->shcolor));
  nn(G_SHALPHA, MUIA_Numeric_Value, f->shadow ? f->shalpha : 60);
  nn(G_SHDIST, MUIA_Numeric_Value, f->shadow ? f->shdist / 20 : 3);
  nn(G_SHBLUR, MUIA_Numeric_Value, f->shadow ? f->shblur / 20 : 4);
  nn(G_SHDIR, MUIA_Numeric_Value, f->shadow ? f->shdir / 10 : 45);
  nn(G_GLOW, MUIA_Numeric_Value, f->glow / 20);
  nn(G_GLCOL, MUIA_Cycle_Active, palette_index(f->glow ? f->glcolor : 0xFFC000));
  nn(G_GLALPHA, MUIA_Numeric_Value, f->glow ? f->glalpha : 60);
  nn(G_SOFT, MUIA_Numeric_Value, f->soft / 20);
  units_text(im->cy, b); nn(G_H, MUIA_String_Contents, (LONG)b);
  units_text(im->cx, b); nn(G_W, MUIA_String_Contents, (LONG)b);
  nn(G_ROT, MUIA_Numeric_Value, im->rot / 10);
  nn(G_VALIGN, MUIA_Cycle_Active, f->anchor);
  nn(G_TDIR, MUIA_Cycle_Active, f->vert);
  for (i = 0; i < 4; i++) { units_text(tb_inset(im, i), b); nn(G_INS0 + i, MUIA_String_Contents, (LONG)b); }
  nn(G_BRIGHT, MUIA_Numeric_Value, f->bright); nn(G_CONTRAST, MUIA_Numeric_Value, f->contrast); nn(G_SHARP, MUIA_Numeric_Value, f->sharp);
  nn(G_SAT, MUIA_Numeric_Value, f->sat ? f->sat : 100); nn(G_TEMP, MUIA_Numeric_Value, f->temp ? f->temp : 6500); nn(G_ALPHA, MUIA_Numeric_Value, f->alpha);
  nn(G_CMODE, MUIA_Cycle_Active, f->cmode); nn(G_RECOL, MUIA_Cycle_Active, palette_index(f->recolor)); nn(G_ART, MUIA_Cycle_Active, f->art);
  for (i = G_FILLTYPE; i <= G_GCOL2; i++) set(pg[i], MUIA_Disabled, !shape);
  for (i = G_VALIGN; i <= G_INS3; i++) set(pg[i], MUIA_Disabled, !shape);
  for (i = G_BRIGHT; i <= G_ART; i++) set(pg[i], MUIA_Disabled, !pic);
}
static LONG gv(int g, ULONG attr) { LONG v = 0; if (pg[g]) get(pg[g], attr, &v); return v; }
static u32 colour_of(int g, u32 old) { LONG i = gv(g, MUIA_Cycle_Active); return i >= 0 && i < 16 ? pal[i] : old; }
/* the pane's settings into the object it shows (a new entry, one undo step), if they change it */
static void pane_apply(void)
{
  Doc *d = wp_doc();
  Image m;
  ObjFx *f;
  int k = sel_obj(), ft, ln;
  LONG v;
  if (!d || k < 0 || k != pane_k) return;
  m = d->images[k]; f = &m.fx;
  if (m.kind == OBJ_SHAPE || m.kind == OBJ_TEXTBOX) {
    ft = (int)gv(G_FILLTYPE, MUIA_Cycle_Active);
    if (ft == 0) { m.fill = COLOR_AUTO; f->filltype = FT_SOLID; }
    else if (ft == 1) { f->filltype = FT_SOLID; m.fill = colour_of(G_FILLCOL, m.fill == COLOR_AUTO ? 0x4472C4 : m.fill); }
    else if (ft == 2) {
      f->filltype = FT_GRAD; f->gtype = (u8)gv(G_GTYPE, MUIA_Cycle_Active); f->gangle = (u16)(gv(G_GANGLE, MUIA_Numeric_Value) * 10);
      if (f->ngs < 2) { f->ngs = 2; f->gpos[0] = 0; f->gpos[1] = 100; f->gcol[0] = m.fill == COLOR_AUTO ? 0x4472C4 : m.fill; f->gcol[1] = 0xFFFFFF; }
      f->gcol[0] = colour_of(G_FILLCOL, f->gcol[0]); f->gcol[f->ngs - 1] = colour_of(G_GCOL2, f->gcol[f->ngs - 1]);
      if (m.fill == COLOR_AUTO) m.fill = f->gcol[0];
    } else if (ft == 3 && !m.data) { nn(G_FILLTYPE, MUIA_Cycle_Active, 1); return; }
    else if (ft == 3) f->filltype = FT_PIC;
    f->falpha = (u8)gv(G_FILLALPHA, MUIA_Numeric_Value);
    f->anchor = (u8)gv(G_VALIGN, MUIA_Cycle_Active);
    f->vert = (u8)gv(G_TDIR, MUIA_Cycle_Active);
    {
      i32 ins[4];
      int s, chg = 0;
      for (s = 0; s < 4; s++) { ins[s] = units_parse(pg[G_INS0 + s]); if (ins[s] != tb_inset(&m, s) && abs(ins[s] - tb_inset(&m, s)) > 15) chg = 1; }
      if (chg) { f->hasins = 1; memcpy(f->ins, ins, sizeof ins); }
    }
  }
  ln = (int)gv(G_LINE, MUIA_Cycle_Active);
  if (ln == 0) m.lstyle = PB_NONE;
  else { m.lstyle = (u8)(ln == 2 ? PB_DOTTED : ln == 3 ? PB_DASHED : ln == 4 ? PB_DOUBLE : PB_SINGLE); m.lwidth = weight8[gv(G_WEIGHT, MUIA_Cycle_Active) % 9]; m.lcolor = colour_of(G_LINECOL, m.lcolor); }
  f->shadow = (u8)(gv(G_SHADOW, MUIA_Selected) != 0);
  if (f->shadow) {
    f->shcolor = colour_of(G_SHCOL, f->shcolor); f->shalpha = (u8)gv(G_SHALPHA, MUIA_Numeric_Value);
    f->shdist = (u16)(gv(G_SHDIST, MUIA_Numeric_Value) * 20); f->shblur = (u16)(gv(G_SHBLUR, MUIA_Numeric_Value) * 20); f->shdir = (u16)(gv(G_SHDIR, MUIA_Numeric_Value) * 10);
  }
  f->glow = (u16)(gv(G_GLOW, MUIA_Numeric_Value) * 20);
  if (f->glow) { f->glcolor = colour_of(G_GLCOL, f->glcolor ? f->glcolor : 0xFFC000); f->glalpha = (u8)gv(G_GLALPHA, MUIA_Numeric_Value); }
  f->soft = (u16)(gv(G_SOFT, MUIA_Numeric_Value) * 20);
  {
    i32 h = units_parse(pg[G_H]), w = units_parse(pg[G_W]);
    if (h >= 60 && abs(h - m.cy) > 15) m.cy = h;
    if (w >= 60 && abs(w - m.cx) > 15) m.cx = w;
    v = gv(G_ROT, MUIA_Numeric_Value);
    if (v * 10 != m.rot / 10 * 10) m.rot = (u16)(v * 10 % 3600);
  }
  if (m.kind == OBJ_PICTURE) {
    f->bright = (signed char)gv(G_BRIGHT, MUIA_Numeric_Value); f->contrast = (signed char)gv(G_CONTRAST, MUIA_Numeric_Value); f->sharp = (signed char)gv(G_SHARP, MUIA_Numeric_Value);
    v = gv(G_SAT, MUIA_Numeric_Value); f->sat = (u16)(v == 100 ? 0 : v < 1 ? 1 : v);
    v = gv(G_TEMP, MUIA_Numeric_Value); f->temp = (u16)(v == 6500 ? 0 : v);
    f->alpha = (u8)gv(G_ALPHA, MUIA_Numeric_Value); f->cmode = (u8)gv(G_CMODE, MUIA_Cycle_Active); f->recolor = colour_of(G_RECOL, f->recolor ? f->recolor : 0x4472C4);
    f->art = (u8)gv(G_ART, MUIA_Cycle_Active);
  }
  if (!memcmp(&m, &d->images[k], sizeof m)) return;
  obj_set(&m);
  pane_k = sel_obj(); pane_ver = d->ver;
}
static void open_pane(int page)
{
  if (!pane) pane_build();
  if (!pane) { DisplayBeep(NULL); return; }
  if (page >= 0) set(panereg, MUIA_Group_ActivePage, page);
  pane_show(sel_obj());
  set(pane, MUIA_Window_Open, TRUE);
  wp_refresh();
}
void wp18_open_pane(int page) { open_pane(page); }

/* ------------------------------------------------------------------ commands */
static void gradient_preset(Image *m, int v)
{
  ObjFx *f = &m->fx;
  u32 c = m->fill == COLOR_AUTO ? 0x4472C4 : m->fill;
  static const u16 ang[8] = { 0, 900, 0, 450, 0, 2700, 1800, 0 };
  if (v == 0) { f->filltype = FT_SOLID; if (f->ngs) m->fill = f->gcol[0]; return; }
  if (f->filltype == FT_GRAD && f->ngs) c = v <= 4 ? f->gcol[f->ngs - 1] : f->gcol[0];
  f->filltype = FT_GRAD; f->ngs = 2; f->gpos[0] = 0; f->gpos[1] = 100;
  f->gtype = (u8)(v == 4 ? GT_RADIAL : v == 7 ? GT_RECT : GT_LINEAR); f->gangle = ang[v];
  if (v <= 4) { f->gcol[0] = mix(c, 0xFFFFFF, 55); f->gcol[1] = c; }
  else { f->gcol[0] = c; f->gcol[1] = mix(c, 0x000000, 45); }
  m->fill = c;
}
void wp18_effect_preset(Image *m, int v)
{
  ObjFx *f = &m->fx;
  switch (v) {
  case 0: f->shadow = 0; f->glow = 0; f->soft = 0; break;
  case 1: case 2: case 3:
    f->shadow = 1; f->shcolor = 0; f->shalpha = 60; f->shblur = v == 3 ? 100 : 80; f->shdist = v == 3 ? 0 : 60; f->shdir = (u16)(v == 1 ? 450 : 900);
    break;
  case 4: case 5: case 6: f->glow = (u16)(v == 4 ? 100 : v == 5 ? 200 : 360); f->glcolor = 0xFFC000; f->glalpha = 60; break;
  default: f->soft = (u16)(v == 7 ? 50 : v == 8 ? 100 : v == 9 ? 200 : 500); break;
  }
}
/* a picture file chosen and kept in the document (an entry the text doesn't refer to); its index or -1 */
static char picpath[512];
int wp18_load_picture(const char *title)
{
  size_t n;
  u8 *data;
  int k;
  Doc *d = wp_doc();
  if (!d || !wp_file_request(0, title, picpath, sizeof picpath, "#?.(png|jpg|jpeg|gif|bmp|iff|ilbm|lbm|pic)")) return -1;
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  data = file_read(picpath, &n);
  k = data ? doc_add_image(d, data, (u32)n, 0, 0) : -1;
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  if (k < 0) wp_ask("OK", "%s\nis not a picture Quill can read (PNG, JPEG, GIF, BMP or IFF ILBM).", picpath);
  return k;
}
static void picture_fill(void)
{
  Doc *d = wp_doc();
  int k = sel_obj(), pk;
  Image m;
  if (k < 0 || d->images[k].kind == OBJ_PICTURE || d->images[k].kind == OBJ_GROUP) { DisplayBeep(NULL); return; }
  if ((pk = wp18_load_picture("Picture Fill")) < 0) return;
  m = d->images[k];
  m.data = d->images[pk].data; m.len = d->images[pk].len; m.type = d->images[pk].type; m.pw = d->images[pk].pw; m.ph = d->images[pk].ph; m.shared = 1;
  m.fx.filltype = FT_PIC;
  obj_set(&m);
}
/* Create Link: the box selected (the last of its chain, or none) waits for the empty box clicked next */
static int link_from = -1;
static u32 link_ver;
static int is_box(const Image *im) { return im->kind == OBJ_TEXTBOX || (im->kind == OBJ_SHAPE && im->shape != SH_LINE && im->shape != SH_ARROW && im->shape != SHX_ICON); }
static int empty_text(const Image *im) { return !im->text || !im->text->np || (im->text->np == 1 && !im->text->p[0]->len); }
static int last_in_chain(Doc *d, int k)
{
  int ch[64], n, i;
  if (!d->images[k].fx.link) return 1;
  n = tb_chain(d, d->images[k].fx.link, ch, 64);
  for (i = 0; i < n; i++) if (d->images[ch[i]].fx.seq > d->images[k].fx.seq) return 0;
  return 1;
}
static void do_link(int src, int dst)
{
  Doc *d = wp_doc();
  Image a, b;
  int ch[64], n, i, seq = 0;
  if (src < 0 || dst < 0 || src == dst || src >= d->nimages || dst >= d->nimages) return;
  a = d->images[src]; b = d->images[dst];
  if (!is_box(&b) || b.fx.link || !empty_text(&b)) { wp_ask("OK", "%s", "The text box to link to must be empty and not linked already."); return; }
  if (!a.fx.link) { a.fx.link = (u16)tb_new_link(d); a.fx.seq = 0; }
  n = tb_chain(d, a.fx.link, ch, 64);
  for (i = 0; i < n; i++) if (d->images[ch[i]].fx.seq >= seq) seq = d->images[ch[i]].fx.seq + 1;
  if (seq <= a.fx.seq) seq = a.fx.seq + 1;
  b.fx.link = a.fx.link; b.fx.seq = (u16)seq;
  if (b.kind == OBJ_SHAPE && b.shape == SH_RECT) b.kind = OBJ_TEXTBOX;
  b.text = NULL;
  if (select_obj(d, src)) obj_set(&a);
  if (select_obj(d, dst)) obj_set(&b);
}
static void break_link(void)
{
  Doc *d = wp_doc();
  int k = sel_obj(), ch[64], n, i;
  u16 link, seq;
  if (k < 0 || !d->images[k].fx.link) { DisplayBeep(NULL); return; }
  link = d->images[k].fx.link; seq = d->images[k].fx.seq;
  n = tb_chain(d, link, ch, 64);
  for (i = 0; i < n; i++) {
    Image m = d->images[ch[i]];
    if (m.fx.seq <= seq) continue;
    m.fx.link = 0; m.fx.seq = 0; m.text = NULL;
    if (select_obj(d, ch[i])) obj_set(&m);
  }
  if (!seq) {
    /* the first box keeps no chain of its own */
    int h = -1;
    n = tb_chain(d, link, ch, 64);
    for (i = 0; i < n; i++) if (!d->images[ch[i]].fx.seq) h = ch[i];
    if (h >= 0 && n == 1) { Image m = d->images[h]; m.fx.link = 0; if (select_obj(d, h)) obj_set(&m); }
  }
}
static void textbox_set(int dir, int align)
{
  Doc *d = wp_doc();
  int k = sel_obj();
  Image m;
  if (k < 0 || !is_box(&d->images[k])) { DisplayBeep(NULL); return; }
  m = d->images[k];
  if (dir >= 0) m.fx.vert = (u8)dir;
  if (align >= 0) m.fx.anchor = (u8)align;
  obj_set(&m);
}
static void insert_icon(int which)
{
  Doc *d = wp_doc();
  int k;
  Image m;
  if (!d || which < 0 || which >= objicon_count()) return;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SHAPE, SHX_ICON);
  if ((k = sel_obj()) < 0) return;
  m = d->images[k];
  m.fx.icon = (u16)(which + 1); m.fill = 0x262626; m.lstyle = PB_NONE;
  strncpy(m.alt, objicon_name(which), sizeof m.alt - 1);
  obj_set(&m);
}
int wp18_command(ULONG id, LONG value)
{
  Doc *d = wp_doc();
  int k = sel_obj();
  Image m;
  if (!d) return WPR_DONE;
  switch (id) {
  case ID_SH_GRADDD:
    if (value == 8) { open_pane(0); return WPR_DONE; }
    if (k < 0 || !is_box(&d->images[k]) || value < 0 || value > 7) { DisplayBeep(NULL); break; }
    m = d->images[k]; gradient_preset(&m, (int)value); obj_set(&m);
    break;
  case ID_SH_GRADIENT: open_pane(0); break;
  case ID_SH_EFFECTSDD:
    if (value == 11) { open_pane(1); return WPR_DONE; }
    if (k < 0 || value < 0 || value > 10) { DisplayBeep(NULL); break; }
    m = d->images[k]; wp18_effect_preset(&m, (int)value); obj_set(&m);
    break;
  case ID_SH_EFFECTS: open_pane(1); break;
  case ID_SH_PICFILL: picture_fill(); break;
  case ID_SH_PANE:
    if (pane && wpget_open(pane)) { set(pane, MUIA_Window_Open, FALSE); wp_refresh(); }
    else open_pane(-1);
    break;
  case ID_SH_PANE_APPLY: if (pane && wpget_open(pane)) pane_apply(); else wp_refresh(); break;
  case ID_SH_PANE_PICFILE: picture_fill(); break;
  case ID_SH_TEXTDIRDD: if (value >= 0 && value < 3) textbox_set((int)value, -1); break;
  case ID_SH_ALIGNDD: if (value >= 0 && value < 3) textbox_set(-1, (int)value); break;
  case ID_SH_DIR0: case ID_SH_DIR1: case ID_SH_DIR2: textbox_set((int)(id - ID_SH_DIR0), -1); break;
  case ID_SH_AL0: case ID_SH_AL1: case ID_SH_AL2: textbox_set(-1, (int)(id - ID_SH_AL0)); break;
  case ID_SH_LINK:
    if (link_from >= 0) { link_from = -1; wp_refresh(); break; }
    if (k < 0 || !is_box(&d->images[k]) || !last_in_chain(d, k)) { DisplayBeep(NULL); break; }
    link_from = k; link_ver = d->ver;
    wp_refresh();
    break;
  case ID_SH_LINK_DO: { int src = link_from; link_from = -1; if (src >= 0 && k >= 0 && k != src) do_link(src, k); wp_refresh(); break; }
  case ID_SH_UNLINK: break_link(); break;
  case ID_SH_ICONS:
    if (!iconwin) iconwin = picker(1);
    if (iconwin) set(iconwin, MUIA_Window_Open, TRUE);
    return WPR_KEEPFOCUS;
  case ID_SH_MORE:
    if (!shapewin) shapewin = picker(0);
    if (shapewin) set(shapewin, MUIA_Window_Open, TRUE);
    return WPR_KEEPFOCUS;
  case ID_SH_PICK_ICON: if (iconwin) set(iconwin, MUIA_Window_Open, FALSE); insert_icon(picked); break;
  case ID_SH_PICK_SHAPE:
    if (shapewin) set(shapewin, MUIA_Window_Open, FALSE);
    if (picked >= SHX_FIRST && picked < SHX_ICON) DoMethod(wp_view(), MUIM_Quill_Command, CMD_SHAPE, (ULONG)picked);
    break;
  }
  return WPR_DONE;
}
int wpget_open(Object *w) { ULONG o = 0; get(w, MUIA_Window_Open, &o); return o != 0; }

/* ------------------------------------------------------------------ hooks */
static void state(ULONG *on, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (pane && wpget_open(pane)) {
    *on |= RBIT(SB_PANE);
    if (c->obj != pane_k || (c->doc && c->doc->ver != pane_ver && c->obj >= 0)) pane_show(c->obj);
  }
  if (im && is_box(im)) {
    *on |= RBIT(SB_TEXT);
    if (c->doc && last_in_chain(c->doc, c->obj)) *on |= RBIT(SB_CANLINK);
    if (im->fx.link && c->doc) {
      int ch[64], n = tb_chain(c->doc, im->fx.link, ch, 64), i;
      for (i = 0; i < n; i++) if (c->doc->images[ch[i]].fx.seq > im->fx.seq) *on |= RBIT(SB_LINKED);
    }
  }
  if (link_from >= 0) {
    *on |= RBIT(SB_LINKING) | RBIT(SB_CANLINK);
    /* the box clicked next: linked to (after this hook, from the main loop) */
    if (c->obj >= 0 && c->obj != link_from && im && is_box(im)) DoMethod(wp_app(), MUIM_Application_ReturnID, ID_SH_LINK_DO);
    else if (c->obj < 0 && c->doc && c->doc->ver != link_ver) link_from = -1;
  }
}
static void ctxmenu(Object *menu, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (c->where != WPC_OBJECT || !im) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, im->kind == OBJ_PICTURE ? "Format Picture..." : "Format Shape...", WP_CTX_ID(ID_SH_PANE), 1);
  if (is_box(im)) {
    wp_ctx_item(menu, "Create Link", WP_CTX_ID(ID_SH_LINK), c->doc && last_in_chain(c->doc, c->obj));
    if (im->fx.link) wp_ctx_item(menu, "Break Link", WP_CTX_ID(ID_SH_UNLINK), 1);
  }
}
static void cleanup(void)
{
  wp18_flush();
  if (cellcls) { MUI_DeleteCustomClass(cellcls); cellcls = NULL; }
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp18_package = {
  "WP18", ID_WP18_FIRST, ID_WP18_LAST,
  wp18_command, state, NULL, ctxmenu, NULL, NULL, cleanup,
  groups, 3,
  menus, NMENUS
};
