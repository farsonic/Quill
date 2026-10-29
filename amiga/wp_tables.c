/*
 * wp_tables.c - WP21a Table styles and quick tables (docs/wordcraft-gap.md). This file belongs to WP21a: only that
 * package edits it. Its commands are ID_WP21A_FIRST .. ID_WP21A_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The table style editor (New, Modify and Delete Table Style: every part of a style, Word's
 * conditional formatting, with a picture of the result), the Border Painter (a table tool: strokes
 * on the page get the Borders group's pen, see wp_tables2.c), Quick Tables and Insert Spreadsheet
 * (a table from a CSV file). The work itself is in core/tstyle.c and core/tblquick.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "wp_tables.h"
#include "../core/table.h"
#include "../core/tstyle.h"
#include "../core/tblquick.h"
#include "../core/docx.h"
#include <libraries/iffparse.h>          /* MAKE_ID */

enum { ID_TA_TS_NEW = ID_WP21A_FIRST, ID_TA_TS_MODIFY, ID_TA_TS_DELETE, ID_TA_TS_OK, ID_TA_TS_PART, ID_TA_TS_BOLD, ID_TA_TS_ITALIC, ID_TA_TS_UNDERLINE,
       ID_TA_TS_COLOR, ID_TA_TS_SIZE, ID_TA_TS_FILL, ID_TA_TS_LINES, ID_TA_TS_ALIGN, ID_TA_TS_CLEAR,
       ID_TA_PAINTER, ID_TA_QT_FIRST, ID_TA_QT_LAST = ID_TA_QT_FIRST + QT_N - 1, ID_TA_QUICKOK };

/* ---- the style editor's lists. Colours (Automatic first; the last entry stands for one not in the
   list, which choosing leaves as it is) */
static const char *const colnames[] = { "Automatic", "Black", "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue", "Dark Blue",
                                        "Purple", "Grey", "Slate", "Steel Blue", "Sky Blue", "Burnt Orange", "Sunflower", "Leaf Green", "Other colour" };
static const ULONG colvals[] = { 0xFF000000, 0x000000, 0xC00000, 0xFF0000, 0xFFA500, 0xBF9000, 0x00B050, 0x008080, 0x0070C0, 0x002060,
                                 0x7030A0, 0x808080, 0x44546A, 0x4472C4, 0x5B9BD5, 0xED7D31, 0xFFC000, 0x70AD47 };
#define NCOL ((int)(sizeof colvals / sizeof colvals[0]))
static const char *const fillnames[] = { "No Colour", "Light Grey", "Grey", "Light Blue", "Light Green", "Light Yellow", "Light Red", "Black",
                                         "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue", "Dark Blue", "Purple", "Slate", "Steel Blue",
                                         "Sky Blue", "Other colour" };
static const ULONG fillvals[] = { 0xFF000000, 0xE7E6E6, 0xBFBFBF, 0xDEEAF6, 0xE2EFD9, 0xFFF2CC, 0xF8CBAD, 0x000000, 0xC00000, 0xFF0000,
                                  0xFFA500, 0xBF9000, 0x00B050, 0x008080, 0x0070C0, 0x002060, 0x7030A0, 0x44546A, 0x4472C4, 0x5B9BD5 };
#define NFILL ((int)(sizeof fillvals / sizeof fillvals[0]))
/* the parts, in the order Word's window lists them */
static const char *const partnames[] = { "Whole table", "Header row", "Total row", "First column", "Last column", "Odd banded rows",
                                         "Even banded rows", "Odd banded columns", "Even banded columns", "Top left cell", "Top right cell",
                                         "Bottom left cell", "Bottom right cell" };
static const u8 parttc[] = { TC_WHOLE, TC_FIRSTROW, TC_LASTROW, TC_FIRSTCOL, TC_LASTCOL, TC_BAND1H, TC_BAND2H, TC_BAND1V, TC_BAND2V,
                             TC_NW, TC_NE, TC_SW, TC_SE };
#define NPART 13
static const char *const borders[] = { "Borders...", "All Borders", "No Border", "Outside Borders", "Inside Borders", "Inside Horizontal Border",
                                       "Inside Vertical Border", "Top Border", "Bottom Border", "Left Border", "Right Border" };   /* (then TB_ order) */
static const char *const penstyles[] = { "Single line", "Double line", "Thick line", "Dotted line", "Dashed line" };
static const char *const penwidths[] = { "1/4 pt", "1/2 pt", "3/4 pt", "1 pt", "1 1/2 pt", "2 1/4 pt", "3 pt" };
static const u8 penw[] = { 2, 4, 6, 8, 12, 18, 24 };
static const char *const aligns[] = { "As it is", "Align Top Left", "Align Top Centre", "Align Top Right", "Align Centre Left", "Align Centre",
                                      "Align Centre Right", "Align Bottom Left", "Align Bottom Centre", "Align Bottom Right" };
static const char *const tri[] = { "As it is", "On", "Off" };

/* ---- the picture of the style being edited: a table of 5 columns by 5 rows, every part switched on */
static TableStyle work;                  /* the style as edited */
static int editk;                        /* 0: a new style; else the style changed (Doc.tstyles number) */
static int curpart;                      /* the part shown (partnames index) */
struct PvData { int dummy; };
static ULONG pen_rgb(Object *obj, ULONG v)
{
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  return (ULONG)ObtainBestPen(cm, ((v >> 16) & 255) * 0x01010101UL, ((v >> 8) & 255) * 0x01010101UL, (v & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
static void pen_free(Object *obj, ULONG p) { ReleasePen(_screen(obj)->ViewPort.ColorMap, (LONG)p); }
static void preview_draw(Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x = _mleft(obj) + 4, y = _mtop(obj) + 4, w = _mwidth(obj) - 8, h = _mheight(obj) - 8;
  LONG cw = w / 5, ch = h / 5;
  ULONG white = pen_rgb(obj, 0xFFFFFF), black = pen_rgb(obj, 0x000000), grey = pen_rgb(obj, 0xA0A0A0);
  int r, c, e;
  SetAPen(rp, white); RectFill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  for (r = 0; r < 5; r++)
    for (c = 0; c < 5; c++) {
      CellLook lk;
      LONG x0 = x + c * cw, y0 = y + r * ch, x1 = x0 + cw, y1 = y0 + ch;
      tstyle_preview_in(wp_doc(), &work, LK_FIRSTROW | LK_LASTROW | LK_FIRSTCOL | LK_LASTCOL, r, c, 5, 5, &lk);
      if (lk.shade != COLOR_AUTO) { ULONG p = pen_rgb(obj, lk.shade); SetAPen(rp, p); RectFill(rp, x0, y0, x1, y1); pen_free(obj, p); }
      {
        /* the text: a stroke in its colour, thicker when bold, placed as the alignment says */
        ULONG col = (lk.cmask & CM_COLOR) && lk.cf.color != COLOR_AUTO ? lk.cf.color : 0x000000, p = pen_rgb(obj, col);
        int bold = (lk.cmask & CM_BOLD) && (lk.cf.flags & CF_BOLD), va = lk.valign_set ? lk.valign : 0, ha = lk.align_set ? lk.align : 0;
        LONG tw = cw / 2, tx0 = ha == AL_CENTER ? x0 + (cw - tw) / 2 : ha == AL_RIGHT ? x1 - 3 - tw : x0 + 3;
        LONG ty = va == 1 ? (y0 + y1) / 2 : va == 2 ? y1 - 3 : y0 + 3;
        SetAPen(rp, p);
        RectFill(rp, tx0, ty - (bold ? 1 : 0), tx0 + tw, ty);
        if ((lk.cmask & CM_UL) || ((lk.cmask & CM_UNDERLINE) && (lk.cf.flags & CF_UNDERLINE))) { Move(rp, tx0, ty + 2); Draw(rp, tx0 + tw, ty + 2); }
        pen_free(obj, p);
      }
      for (e = 0; e < 4; e++) {
        ULONG v = lk.bcolor[e], p;
        if (!(lk.borders & (1 << e))) {
          /* no line: a faint grid, as View Gridlines shows */
          SetAPen(rp, grey);
          if (e == E_TOP) { LONG i; for (i = x0; i <= x1; i += 2) WritePixel(rp, i, y0); }
          else if (e == E_LEFT) { LONG i; for (i = y0; i <= y1; i += 2) WritePixel(rp, x0, i); }
          continue;
        }
        p = v == COLOR_AUTO ? black : pen_rgb(obj, v);
        SetAPen(rp, p);
        if (e == E_TOP) { Move(rp, x0, y0); Draw(rp, x1, y0); if (lk.bstyle[e] == PB_DOUBLE || lk.bwidth[e] >= 12) { Move(rp, x0, y0 + 2); Draw(rp, x1, y0 + 2); } }
        else if (e == E_BOTTOM) { Move(rp, x0, y1); Draw(rp, x1, y1); if (lk.bstyle[e] == PB_DOUBLE || lk.bwidth[e] >= 12) { Move(rp, x0, y1 - 2); Draw(rp, x1, y1 - 2); } }
        else if (e == E_LEFT) { Move(rp, x0, y0); Draw(rp, x0, y1); }
        else { Move(rp, x1, y0); Draw(rp, x1, y1); }
        if (v != COLOR_AUTO) pen_free(obj, p);
      }
    }
  pen_free(obj, white); pen_free(obj, black); pen_free(obj, grey);
}
static ULONG pvdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  switch (msg->MethodID) {
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 150; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += 300;
    m->MinMaxInfo->MinHeight += 70; m->MinMaxInfo->DefHeight += 90; m->MinMaxInfo->MaxHeight += 120;
    return 0;
  }
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & MADF_DRAWOBJECT) preview_draw(obj);
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
static struct MUI_CustomClass *pvcls;

/* ---- the style editor window (made afresh each time: its Based On list is the document's styles) */
static Object *tsclear, *tswin, *tsname, *tsbased, *tspart, *tsbold, *tsital, *tsul, *tscolor, *tssize, *tsfill, *tslines, *tslstyle, *tslwidth, *tslcolor, *tsalign, *tspv;
static const char *basednames[MAX_TSTYLES + 2];
static int basedk[MAX_TSTYLES + 2], nbased;
static char basedbuf[MAX_TSTYLES + 1][48];
static void set_quiet(Object *o, ULONG attr, ULONG v) { SetAttrs(o, MUIA_NoNotify, TRUE, attr, v, TAG_DONE); }
/* the gadgets show part curpart of the style */
static void show_part(void)
{
  const TCond *c = &work.c[parttc[curpart]];
  int k;
  char b[16];
  set_quiet(tsbold, MUIA_Cycle_Active, !(c->cmask & CM_BOLD) ? 0 : (c->cf.flags & CF_BOLD) ? 1 : 2);
  set_quiet(tsital, MUIA_Cycle_Active, !(c->cmask & CM_ITALIC) ? 0 : (c->cf.flags & CF_ITALIC) ? 1 : 2);
  set_quiet(tsul, MUIA_Cycle_Active, !(c->cmask & CM_UNDERLINE) ? 0 : (c->cf.flags & CF_UNDERLINE) ? 1 : 2);
  for (k = 0; k < NCOL && colvals[k] != c->cf.color; k++) ;
  set_quiet(tscolor, MUIA_Cycle_Active, !(c->cmask & CM_COLOR) ? 0 : k < NCOL ? k : NCOL);
  b[0] = 0;
  if (c->cmask & CM_SIZE) sprintf(b, "%u%s", (unsigned)(c->cf.size / 2), c->cf.size & 1 ? ".5" : "");
  set_quiet(tssize, MUIA_String_Contents, (ULONG)b);
  for (k = 0; k < NFILL && fillvals[k] != c->shade; k++) ;
  set_quiet(tsfill, MUIA_Cycle_Active, !c->shade_set ? 0 : k < NFILL ? k + 1 : NFILL + 1);
  set_quiet(tsalign, MUIA_Cycle_Active, c->align_set || c->valign_set ? 1 + (c->valign_set ? c->valign : 0) * 3 + (c->align_set ? (c->align == AL_CENTER ? 1 : c->align == AL_RIGHT ? 2 : 0) : 0) : 0);
}
static ULONG cyc(Object *o) { ULONG v = 0; get(o, MUIA_Cycle_Active, &v); return v; }
/* a three-way choice (as it is, on, off) into a part's character flag */
static void tri_flag(TCond *c, unsigned mask, u8 flag, ULONG v)
{
  if (!v) { c->cmask &= ~mask; c->cf.flags &= (u8)~flag; return; }
  c->cmask |= mask;
  if (v == 1) c->cf.flags |= flag; else c->cf.flags &= (u8)~flag;
}
/* one gadget changed: its setting into the part shown, and the picture drawn again */
static void part_changed(ULONG id)
{
  TCond *c = &work.c[parttc[curpart]];
  ULONG v;
  int k;
  switch (id) {
  case ID_TA_TS_PART: curpart = (int)cyc(tspart) % NPART; show_part(); MUI_Redraw(tspv, MADF_DRAWOBJECT); return;
  case ID_TA_TS_BOLD: tri_flag(c, CM_BOLD, CF_BOLD, cyc(tsbold)); break;
  case ID_TA_TS_ITALIC: tri_flag(c, CM_ITALIC, CF_ITALIC, cyc(tsital)); break;
  case ID_TA_TS_UNDERLINE: tri_flag(c, CM_UNDERLINE, CF_UNDERLINE, cyc(tsul)); if (cyc(tsul) == 1) { c->cmask |= CM_UL; c->cf.ul = UL_SINGLE; } else c->cmask &= ~CM_UL; break;
  case ID_TA_TS_COLOR:
    v = cyc(tscolor);
    if (!v) c->cmask &= ~CM_COLOR;
    else if ((int)v < NCOL) { c->cmask |= CM_COLOR; c->cf.color = colvals[v]; }
    break;
  case ID_TA_TS_SIZE: {
    char *s = NULL;
    long whole = 0, half = 0;
    get(tssize, MUIA_String_Contents, &s);
    if (s) { while (*s == ' ') s++; while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0'); if ((*s == '.' || *s == ',') && s[1] >= '5') half = 1; }
    if (whole > 0 && whole < 1000) { c->cmask |= CM_SIZE; c->cf.size = (u16)(whole * 2 + half); } else c->cmask &= ~CM_SIZE;
    break; }
  case ID_TA_TS_FILL:
    v = cyc(tsfill);
    if (!v) c->shade_set = 0;
    else if ((int)v <= NFILL) { c->shade_set = 1; c->shade = fillvals[v - 1]; }
    break;
  case ID_TA_TS_LINES: {
    /* lines on the part's edges in the pen chosen beside the list; the list back to its title */
    static const u8 sides[10] = { 0x3F, 0x3F, 0x0F, 0x30, 0x10, 0x20, 0x01, 0x04, 0x02, 0x08 };   /* TE_ bits for TB_ */
    v = cyc(tslines);
    if (!v) return;
    for (k = 0; k < TE_N; k++)
      if (sides[v - 1] & (1 << k)) {
        TBorder *b = &c->b[k];
        b->set = 1;
        if (v - 1 == TB_NONE) { b->style = PB_NONE; b->width = 0; b->color = COLOR_AUTO; }
        else { b->style = (u8)(cyc(tslstyle) + PB_SINGLE); b->width = penw[cyc(tslwidth) % 7]; b->color = colvals[cyc(tslcolor) % NCOL]; }
      }
    set_quiet(tslines, MUIA_Cycle_Active, 0);
    break; }
  case ID_TA_TS_ALIGN:
    v = cyc(tsalign);
    if (!v) { c->align_set = 0; c->valign_set = 0; }
    else { static const u8 al[3] = { AL_LEFT, AL_CENTER, AL_RIGHT }; c->align_set = 1; c->align = al[(v - 1) % 3]; c->valign_set = 1; c->valign = (u8)((v - 1) / 3); }
    break;
  case ID_TA_TS_CLEAR:
    memset(c, 0, sizeof *c);
    show_part();
    break;
  }
  c->used = 1;
  if (parttc[curpart] != TC_WHOLE && id == ID_TA_TS_CLEAR) c->used = 0;
  MUI_Redraw(tspv, MADF_DRAWOBJECT);
}
/* the caret's table's style (Doc.tstyles number), 0 if none or not in a table */
static int caret_style(Doc *d)
{
  Table *t;
  if (!d || wp21_caret.para >= d->np || !(t = doc_table(d, &d->p[wp21_caret.para]->pf))) return 0;
  return t->tstyle && t->tstyle <= d->ntstyles && !d->tstyles[t->tstyle - 1].gone ? t->tstyle : 0;
}
/* New (k 0) or Modify Table Style (k: the style): the window made with the document's styles to base
   it on, showing the whole table's part */
static void open_editor(int k)
{
  static const char *parts[NPART + 1], *bord[12], *pst[6], *pwd[8], *cols[NCOL + 2], *fills[NFILL + 3], *als[11], *tris[4];
  Doc *d = wp_doc();
  Object *c;
  int i, sel = 0;
  char name[48];
  if (!d) return;
  if (!pvcls && !(pvcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PvData), (APTR)pvdispatch))) return;
  editk = k;
  if (k) work = d->tstyles[k - 1];
  else {
    int n = 1;
    /* "Table Style 1", or the first number not taken */
    for (;;) {
      char u[48];
      sprintf(name, "%s %d", tr("Table Style"), n);
      wp_latin1_utf8(name, u, sizeof u);
      for (i = 0; i < d->ntstyles && (d->tstyles[i].gone || strcmp(d->tstyles[i].name, u)); i++) ;
      if (i == d->ntstyles || n > 99) break;
      n++;
    }
    wp_latin1_utf8(name, name, sizeof name);
    tstyle_blank(&work, d, caret_style(d), name);
  }
  curpart = 0;
  if (tswin) { set(tswin, MUIA_Window_Open, FALSE); DoMethod(wp_app(), OM_REMMEMBER, (ULONG)tswin); MUI_DisposeObject(tswin); tswin = NULL; }
  for (i = 0; i < NPART; i++) parts[i] = tr(partnames[i]);
  parts[NPART] = NULL;
  for (i = 0; i < 11; i++) bord[i] = tr(borders[i]);
  bord[11] = NULL;
  for (i = 0; i < 5; i++) pst[i] = tr(penstyles[i]);
  pst[5] = NULL;
  for (i = 0; i < 7; i++) pwd[i] = penwidths[i];
  pwd[7] = NULL;
  cols[0] = tr("As it is");
  for (i = 0; i <= NCOL; i++) cols[i + 1] = tr(colnames[i]);
  cols[NCOL + 1] = NULL;
  fills[0] = tr("As it is");
  for (i = 0; i <= NFILL; i++) fills[i + 1] = tr(fillnames[i]);
  fills[NFILL + 2] = NULL;
  for (i = 0; i < 10; i++) als[i] = tr(aligns[i]);
  als[10] = NULL;
  for (i = 0; i < 3; i++) tris[i] = tr(tri[i]);
  tris[3] = NULL;
  /* Based on: none, then the document's other styles (not those based on this one) */
  nbased = 0;
  basednames[nbased] = tr("(no style)"); basedk[nbased++] = 0;
  for (i = 1; i <= d->ntstyles && nbased < MAX_TSTYLES; i++) {
    const TableStyle *ts = &d->tstyles[i - 1];
    int b = i, guard = 0, loop = 0;
    if (ts->gone || i == k || !strcmp(ts->id, "TableNormal")) continue;
    while (b && guard++ < 16) { if (b == k) { loop = 1; break; } b = d->tstyles[b - 1].based[0] ? doc_tstyle(d, d->tstyles[b - 1].based) : 0; }
    if (loop) continue;
    utf8_to_latin1(ts->name, strlen(ts->name), basedbuf[nbased], sizeof basedbuf[0]);
    if (work.based[0] && !strcmp(work.based, ts->id)) sel = nbased;
    basednames[nbased] = basedbuf[nbased]; basedk[nbased++] = i;
  }
  basednames[nbased] = NULL;
  c = VGroup,
    Child, ColGroup(2),
      Child, QLabel(tr("Name"), MUIO_Label_DoubleFrame), Child, tsname = StringObject, StringFrame, MUIA_String_MaxLen, 46, MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Style based on"), MUIO_Label_SingleFrame), Child, tsbased = CycleObject, MUIA_Cycle_Entries, (ULONG)basednames, MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Apply formatting to"), MUIO_Label_SingleFrame), Child, tspart = CycleObject, MUIA_Cycle_Entries, (ULONG)parts, MUIA_CycleChain, 1, End,
    End,
    Child, QTitleObject(tr("Formatting")),
    Child, ColGroup(4),
      Child, QLabel(tr("Bold"), MUIO_Label_SingleFrame), Child, tsbold = CycleObject, MUIA_Cycle_Entries, (ULONG)tris, MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Italic"), MUIO_Label_SingleFrame), Child, tsital = CycleObject, MUIA_Cycle_Entries, (ULONG)tris, MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Underline"), MUIO_Label_SingleFrame), Child, tsul = CycleObject, MUIA_Cycle_Entries, (ULONG)tris, MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Size"), MUIO_Label_DoubleFrame), Child, tssize = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Font colour"), MUIO_Label_SingleFrame), Child, tscolor = CycleObject, MUIA_Cycle_Entries, (ULONG)cols, MUIA_CycleChain, 1, End,
      Child, QLabel(tr("Shading"), MUIO_Label_SingleFrame), Child, tsfill = CycleObject, MUIA_Cycle_Entries, (ULONG)fills, MUIA_CycleChain, 1, End,
    End,
    Child, HGroup,
      Child, tslines = CycleObject, MUIA_Cycle_Entries, (ULONG)bord, MUIA_CycleChain, 1, End,
      Child, tslstyle = CycleObject, MUIA_Cycle_Entries, (ULONG)pst, MUIA_CycleChain, 1, End,
      Child, tslwidth = CycleObject, MUIA_Cycle_Entries, (ULONG)pwd, MUIA_Cycle_Active, 1, MUIA_CycleChain, 1, End,
      Child, tslcolor = CycleObject, MUIA_Cycle_Entries, (ULONG)(cols + 1), MUIA_CycleChain, 1, End,
    End,
    Child, HGroup,
      Child, QLabel(tr("Alignment"), MUIO_Label_SingleFrame), Child, tsalign = CycleObject, MUIA_Cycle_Entries, (ULONG)als, MUIA_CycleChain, 1, End,
      Child, HSpace(0), Child, tsclear = QButton(tr("Clear")),
    End,
    Child, tspv = NewObject(pvcls->mcc_Class, NULL, MUIA_Frame, MUIV_Frame_Group, TAG_DONE),
  End;
  if (!c || !(tswin = wp_dialog(k ? "Modify Table Style" : "New Table Style", MAKE_ID('W','2','T','S'), c, "_OK", ID_TA_TS_OK))) return;
  /* every gadget tells the package when it changes */
  DoMethod(tspart, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_PART);
  DoMethod(tsbold, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_BOLD);
  DoMethod(tsital, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_ITALIC);
  DoMethod(tsul, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_UNDERLINE);
  DoMethod(tscolor, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_COLOR);
  DoMethod(tssize, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_SIZE);
  DoMethod(tsfill, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_FILL);
  DoMethod(tslines, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_LINES);
  DoMethod(tsalign, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_ALIGN);
  DoMethod(tsclear, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TA_TS_CLEAR);
  {
    char b[48];
    utf8_to_latin1(work.name, strlen(work.name), b, sizeof b);
    set(tsname, MUIA_String_Contents, (ULONG)b);
  }
  set_quiet(tsbased, MUIA_Cycle_Active, (ULONG)sel);
  show_part();
  set(tswin, MUIA_Window_Open, TRUE);
}
/* OK: the style made or changed (its tables formatted afresh); a new one goes on the caret's table */
static void editor_ok(void)
{
  Doc *d = wp_doc();
  char *s = NULL;
  ULONG b = 0;
  int k;
  Pos c = wp21_caret;
  set(tswin, MUIA_Window_Open, FALSE);
  if (!d) return;
  get(tsname, MUIA_String_Contents, &s);
  wp_latin1_utf8(s && *s ? s : tr("Table Style"), work.name, sizeof work.name);
  get(tsbased, MUIA_Cycle_Active, &b);
  work.based[0] = 0;
  if ((int)b < nbased && basedk[b] > 0 && basedk[b] <= d->ntstyles) strcpy(work.based, d->tstyles[basedk[b] - 1].id);
  if (c.para >= d->np) c = doc_start(d);
  if (!(k = tstyle_define(d, editk, &work, c))) { DisplayBeep(NULL); return; }
  if (!editk && wp21_intable) DoMethod(wp_view(), MUIM_Quill_Command, CMD_TSTYLE, (ULONG)k);
  else wp21_changed(c);
}
/* Delete Table Style: the caret's table's style, after asking */
static void delete_style(void)
{
  Doc *d = wp_doc();
  int k = caret_style(d);
  char b[60];
  Pos c = wp21_caret;
  if (!k) { DisplayBeep(NULL); return; }
  utf8_to_latin1(d->tstyles[k - 1].name, strlen(d->tstyles[k - 1].name), b, sizeof b);
  if (wp_ask("_Delete|_Cancel", "Delete the table style %s? Tables in it keep how they look.", b) != 1) return;
  if (tstyle_delete(d, k, c)) wp21_changed(c); else DisplayBeep(NULL);
}

/* ---- Quick Tables and Insert Spreadsheet */
static const char *const quicknames[QT_N] = { "Calendar 1", "Calendar 2", "Calendar 3", "Double Table", "Matrix", "Tabular List", "With Subheads 1", "With Subheads 2" };
static const char *const monthnames[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const daynames[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
/* the room a new table gets: the page's text width, or its section's column */
static i32 table_room(Doc *d, u32 pi)
{
  const SectFmt *s = doc_sect(d, pi);
  const PageSetup *pg = sect_page(d, s, doc_last_sect(d, pi));
  i32 body = pg->width - pg->left - pg->right;
  if (s && s->cols > 1) body = (body - (s->cols - 1) * s->colgap) / s->cols;
  return body;
}
static void quick_table(int kind)
{
  Doc *d = wp_doc();
  QuickWords w;
  char buf[24][48];
  struct DateStamp ds;
  LONG days;
  int y = 1978, m = 1, i, n = 0;
  Pos at = wp21_caret;
  if (!d || kind < 0 || kind >= QT_N) return;
  if (at.para >= d->np) at = doc_start(d);
  if (in_table(&d->p[at.para]->pf) && is_nested(&d->p[at.para]->pf)) { DisplayBeep(NULL); return; }
  /* today's month, from the days since 1978 */
  DateStamp(&ds);
  days = ds.ds_Days;
  while (days >= 365 + ((y % 4 == 0 && y % 100) || y % 400 == 0)) { days -= 365 + ((y % 4 == 0 && y % 100) || y % 400 == 0); y++; }
  while (days >= month_days(y, m) && m < 12) { days -= month_days(y, m); m++; }
  memset(&w, 0, sizeof w);
  for (i = 0; i < 12; i++) { wp_latin1_utf8(tr(monthnames[i]), buf[n], 48); w.month[i] = buf[n++]; }
  for (i = 0; i < 7; i++) { wp_latin1_utf8(tr(daynames[i]), buf[n], 48); w.wday[i] = buf[n++]; }
  wp_latin1_utf8(tr("Wk"), buf[n], 48); w.week = buf[n++];
  wp_latin1_utf8(tr("Item"), buf[n], 48); w.item = buf[n++];
  wp_latin1_utf8(tr("Amount"), buf[n], 48); w.amount = buf[n++];
  wp_latin1_utf8(tr("Total"), buf[n], 48); w.total = buf[n++];
  wp_latin1_utf8(tr("Point"), buf[n], 48); w.point = buf[n++];
  /* (two more fit: needed, subheading) */
  {
    static char needed[48], sub[48];
    wp_latin1_utf8(tr("Needed"), needed, sizeof needed); w.needed = needed;
    wp_latin1_utf8(tr("Subheading"), sub, sizeof sub); w.subhead = sub;
  }
  wp_story_close();
  if (tbl_quick(d, &at, kind, y, m, &w, in_table(&d->p[at.para]->pf) ? table_room(d, at.para) / 2 : table_room(d, at.para))) wp21_changed(at);
  else DisplayBeep(NULL);
}
/* Quick Tables: the kinds, one to choose (the Insert tab's button) */
static Object *qtwin, *qtradio;
static void open_quick(void)
{
  static const char *names[QT_N + 1];
  int i;
  if (!qtwin) {
    for (i = 0; i < QT_N; i++) names[i] = tr(quicknames[i]);
    names[QT_N] = NULL;
    qtwin = wp_dialog("Quick Tables", MAKE_ID('W','2','Q','T'), VGroup,
      Child, QTitleObject(tr("Built-In")),
      Child, qtradio = RadioObject, MUIA_Radio_Entries, (ULONG)names, MUIA_CycleChain, 1, End,
    End, "_Insert", ID_TA_QUICKOK);
    if (!qtwin) return;
  }
  set(qtwin, MUIA_Window_Open, TRUE);
}
static void spreadsheet(void)
{
  static char path[512] = "";
  Doc *d = wp_doc();
  u8 *data;
  size_t n = 0;
  Pos at = wp21_caret;
  if (!d) return;
  if (!wp_file_request(0, "Insert Spreadsheet (CSV)", path, sizeof path, "#?.(csv|txt|tab|tsv)")) return;
  if (!(data = file_read(path, &n))) { wp_ask("_OK", "%s can't be read.", path); return; }
  if (at.para >= d->np) at = doc_start(d);
  if (tbl_from_csv(d, &at, (const char *)data, n, table_room(d, at.para))) wp21_changed(at);
  else wp_ask("_OK", "%s has no table in it (or a table can't go here).", path);
  free(data);
}

int wp21a_command(ULONG id, LONG value)
{
  if (id >= ID_TA_QT_FIRST && id <= ID_TA_QT_LAST) { quick_table((int)(id - ID_TA_QT_FIRST)); return WPR_DONE; }
  if (tswin && id >= ID_TA_TS_PART && id <= ID_TA_TS_CLEAR) { part_changed(id); return WPR_KEEPFOCUS; }
  switch (id) {
  case ID_TA_TS_NEW: open_editor(0); return WPR_KEEPFOCUS;
  case ID_TA_TS_MODIFY: { int k = caret_style(wp_doc()); if (!k) { DisplayBeep(NULL); break; } open_editor(k); return WPR_KEEPFOCUS; }
  case ID_TA_TS_DELETE: delete_style(); break;
  case ID_TA_TS_OK: if (tswin) editor_ok(); break;
  case ID_TA_PAINTER: wp21_set_tool(WP21_PAINT); break;
  case WP21_ID_QUICKTABLES: open_quick(); return WPR_KEEPFOCUS;
  case ID_TA_QUICKOK: { ULONG v = 0; get(qtradio, MUIA_Radio_Active, &v); set(qtwin, MUIA_Window_Open, FALSE); quick_table((int)v); break; }
  case WP21_ID_SPREADSHEET: spreadsheet(); break;
  }
  return WPR_DONE;
}
/* bit 1: the Border Painter is on; bit 2: the caret's table has a style (to modify or delete) */
static void wp21a_state(ULONG *on, const WpCtx *c)
{
  Doc *d = c->doc;
  Table *t;
  if (wp21_tool() == WP21_PAINT) *on |= RBIT(1);
  if (d && c->intable && c->caret.para < d->np && (t = doc_table(d, &d->p[c->caret.para]->pf)) && t->tstyle && t->tstyle <= d->ntstyles && !d->tstyles[t->tstyle - 1].gone)
    *on |= RBIT(2);
}
static void wp21a_cleanup(void)
{
  if (pvcls) { MUI_DeleteCustomClass(pvcls); pvcls = NULL; }
}

#define IC_NEWTS "ci R 1 2 12 13 L 1 6 12 6 L 5 2 5 13 ca F 2 3 11 5 cg L 13 0 13 6 L 10 3 16 3"
#define IC_MODTS "ci R 1 2 12 13 L 1 6 12 6 L 5 2 5 13 ca F 2 3 11 5 co P 4 9 15 15 9 14 8 8 14"
#define IC_DELTS "ci R 1 2 12 13 L 1 6 12 6 L 5 2 5 13 ca F 2 3 11 5 cr L 10 9 15 14 L 15 9 10 14"
#define IC_PAINTER "cd R 0 6 9 15 L 0 10 9 10 L 4 6 4 15 ca F 9 1 15 4 ci R 9 1 15 4 L 12 4 12 7 L 12 7 6 7 L 6 7 6 10"
static const Item i_styles[] = {
  { K_BUTTON, S_SMALL, 0, 0, "New Style", ID_TA_TS_NEW, 0, 0, 0, 0, "A table style of your own: lines, fills and text for each part of a table", NULL, IC_NEWTS, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Modify Style", ID_TA_TS_MODIFY, 0, RB_WP(WPI_21A, 2), 0, 0, "Change the table's style: every table in it follows", NULL, IC_MODTS, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Delete Style", ID_TA_TS_DELETE, 0, RB_WP(WPI_21A, 2), 0, 0, "Delete the table's style (tables keep how they look)", NULL, IC_DELTS, 0 } };
static const Item i_painter[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Border Painter", ID_TA_PAINTER, RB_WP(WPI_21A, 1), RB_INTABLE, 0, 0, "Click or drag along cell edges to give them the pen's line (Esc when done)", NULL, IC_PAINTER, 0 } };
static const WpGroup groups[] = {
  { TAB_TABLEDESIGN, 2, { "Style", G_COLS, i_styles, 3 } },
  { TAB_TABLEDESIGN, -1, { "Paint", G_COLS, i_painter, 1 } } };
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Quick Tables", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Calendar 1", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 0) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Calendar 2", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 1) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Calendar 3", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 2) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Double Table", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 3) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Matrix", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 4) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Tabular List", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 5) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"With Subheads 1", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 6) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"With Subheads 2", 0, 0, 0, (APTR)(ID_TA_QT_FIRST + 7) } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Spreadsheet (CSV)...", 0, 0, 0, (APTR)WP21_ID_SPREADSHEET } },
  { WM_TABLE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Table Styles", 0, 0, 0, 0 } },
  { WM_TABLE, { NM_SUB, (STRPTR)"New Table Style...", 0, 0, 0, (APTR)ID_TA_TS_NEW } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Modify Table Style...", 0, 0, 0, (APTR)ID_TA_TS_MODIFY } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Delete Table Style", 0, 0, 0, (APTR)ID_TA_TS_DELETE } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Border Painter", 0, 0, 0, (APTR)ID_TA_PAINTER } } };

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp21a_package = {
  "WP21a", ID_WP21A_FIRST, ID_WP21A_LAST,
  wp21a_command, wp21a_state, NULL, NULL, NULL, NULL, wp21a_cleanup,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};
