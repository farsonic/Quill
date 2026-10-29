/*
 * wp_chart.c - WP15b Insert Chart and Chart Design (docs/wordcraft-gap.md). This file belongs to WP15b: only that
 * package edits it. Its commands are ID_WP15B_FIRST .. ID_WP15B_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Insert > Chart: every kind with a preview of Word's sample data, put in at the caret. With a chart
 * selected, the Chart Design tab: Add Chart Element, Quick Layout, Change Colors and the chart styles,
 * Switch Row/Column, Select Data, Edit Data (a sheet of the chart's data), Change Chart Type, and
 * Format (each part's fill and outline, the titles, the value axis, legend, labels and gridlines).
 * Each change writes the chart's parts afresh (core/chartedit.c) as a new object put in the
 * selected one's place: one undo step. The dialogs follow Ledger's charttab.c (same author).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_primview.h"
#include "lang.h"
#include "theme.h"

#undef Label1
#undef Label2
#undef LLabel
#define LLabel(l) QLabel(l, MUIO_Label_LeftAligned)
#define Label1(l) QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l) QLabel(l, MUIO_Label_DoubleFrame)

enum { ID_CH_INSERT = ID_WP15B_FIRST,       /* (ribbon.c's Illustrations group names it ID_WP15B_FIRST) */
       ID_CH_INSGO, ID_CH_PICK, ID_CH_TYPE, ID_CH_EDITDATA = ID_WP15B_FIRST + 4 /* (main.c runs it for a double-clicked chart) */, ID_CH_DATAGO, ID_CH_DATAL, ID_CH_DATAR, ID_CH_DATAU, ID_CH_DATAD, ID_CH_DATASIZE,
       ID_CH_SWITCH, ID_CH_SELECT, ID_CH_SELGO, ID_CH_SELPICK, ID_CH_SELUP, ID_CH_SELDOWN, ID_CH_SELDEL, ID_CH_SELNAME, ID_CH_SELSWITCH,
       ID_CH_ELEMENT, ID_CH_QUICK, ID_CH_COLORS, ID_CH_STYLE, ID_CH_FORMAT, ID_CH_FMTGO, ID_CH_FMTELEM, ID_CH_FMTQUICK };
#define ST_CHART 1                   /* state bit: a chart is selected */

static void beep(void) { DisplayBeep(NULL); }
static ULONG xget(Object *o, ULONG a) { ULONG v = 0; if (o) get(o, a, &v); return v; }
static const char *sget(Object *o) { const char *s = (const char *)xget(o, MUIA_String_Contents); return s ? s : ""; }
static void to_latin(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? strlen(u) : 0, out, cap); }
static const char **trl(const char *const *en, int n, const char **out) { int i; for (i = 0; i < n; i++) out[i] = tr(en[i]); out[n] = NULL; return out; }
static Object *check_obj(void)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
         MUIA_Image_FreeVert, TRUE, MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End;
}

/* ------------------------------------------------------------------ the selected chart */
static int sel_index(void)
{
  Doc *d = wp_doc();
  LONG k = (LONG)xget(wp_view(), MUIA_Quill_Object);
  if (!d || k < 0 || k >= d->nimages || d->images[k].kind != OBJ_CHART || !d->images[k].pack || !d->images[k].pack->chart) return -1;
  return (int)k;
}
static const Chart *sel_chart(void) { int k = sel_index(); return k >= 0 ? wp_doc()->images[k].pack->chart : NULL; }
/* the selected chart becomes c (its parts written afresh): one undo step. 0 if it couldn't be */
static int apply(const Chart *c)
{
  Doc *d = wp_doc();
  int k = sel_index();
  Image im;
  if (k < 0 || !c || c->kind == CK_OTHER) { beep(); return 0; }     /* (a kind Quill can't write: change its type first) */
  im = d->images[k];
  if (!(im.pack = chart_pack_make(c, d->images[k].pack))) { beep(); return 0; }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)&im);
  pack_unref(im.pack);
  wp_refresh();
  return 1;
}
/* a change made to a copy of the selected chart and applied */
typedef void (*ChartFn)(Chart *c, LONG v);
static void change(ChartFn f, LONG v)
{
  const Chart *c = sel_chart();
  Chart *n = c ? chart_copy(c) : NULL;
  if (!n) { beep(); return; }
  f(n, v);
  apply(n);
  chart_free(n);
}
static void f_element(Chart *c, LONG v) { chart_add_element(c, (int)v); }
static void f_quick(Chart *c, LONG v) { chart_quick_layout(c, (int)v); }
static void f_colors(Chart *c, LONG v) { int k; c->palette = (u8)v; for (k = 0; k < c->ns; k++) c->s[k].color = 0; }
static void f_style(Chart *c, LONG v) { c->style = (u8)(v + 1); }
static void f_switch(Chart *c, LONG v) { if (v || !chart_switch_rowcol(c)) beep(); }

/* ------------------------------------------------------------------ Insert Chart and Change Chart Type */
static Object *icwin, *iclist, *icprev;
static int icmode;                        /* 0 insert, 1 change the selected chart's kind */
static int ickinds[CK_N], icn;
static int pick_kind(void) { LONG a = (LONG)xget(iclist, MUIA_List_Active); return a >= 0 && a < icn ? ickinds[a] : -1; }
static void ic_show(void)
{
  int k = pick_kind();
  Chart *c = NULL;
  if (k < 0) return;
  if (icmode && sel_chart() && (c = chart_copy(sel_chart()))) chart_set_kind(c, k);
  else if (!icmode) c = chart_sample(k);
  set(icprev, MUIA_PrimView_Chart, (ULONG)c);
}
static void open_insert(int mode)
{
  int i, a = 0;
  if (mode && !sel_chart()) { beep(); return; }
  if (!icwin) {
    Object *c = HGroup,
      Child, VGroup, MUIA_HorizWeight, 40,
        Child, LLabel(tr("Chart type:")),
        Child, ListviewObject, MUIA_Listview_List, iclist = ListObject, InputListFrame, MUIA_List_AdjustWidth, TRUE, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, End,
      End,
      Child, icprev = primview_new(240, 180),
    End;
    if (!c || !icprev) return;
    if (!(icwin = wp_dialog("Insert Chart", MAKE_ID('W','1','5','B'), c, "_OK", ID_CH_INSGO))) return;
    for (i = 0, icn = 0; i < CK_N; i++) {
      char t[64];
      if (i == CK_OTHER) continue;
      ickinds[icn++] = i;
      snprintf(t, sizeof t, "%s", tr(chart_kind_name(i)));
      DoMethod(iclist, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom);
    }
    DoMethod(iclist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_PICK);
    DoMethod(iclist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_INSGO);
  }
  icmode = mode;
  set(icwin, MUIA_Window_Title, (ULONG)tr(mode ? "Change Chart Type" : "Insert Chart"));
  if (mode) for (i = 0; i < icn; i++) if (ickinds[i] == sel_chart()->kind) a = i;
  set(iclist, MUIA_List_Active, a);
  ic_show();
  set(icwin, MUIA_Window_Open, TRUE);
  set(icwin, MUIA_Window_ActiveObject, (ULONG)iclist);
}
static void insert_go(void)
{
  int k = pick_kind();
  set(icwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (k < 0) return;
  if (icmode) {
    const Chart *c = sel_chart();
    Chart *n = c ? chart_copy(c) : NULL;
    if (!n) { beep(); return; }
    chart_set_kind(n, k);
    apply(n);
    chart_free(n);
  } else {
    Doc *d = wp_doc();
    Chart *c = chart_sample(k);
    ObjPack *pk = c ? chart_pack_make(c, NULL) : NULL;
    int im;
    Pos *cp, ab[2];
    chart_free(c);
    wp_story_close();
    /* Word's size, 6 x 3.5 inches (narrowed to the column when it goes in) */
    if (!pk || (im = doc_add_graphic(d, pk, OBJ_CHART, 8640, 5040)) < 0) { beep(); return; }
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_PICTURE, (ULONG)im);
    /* selected, so the Chart Design tab shows */
    if ((cp = wp_caret()) && cp->off >= 4) { ab[0] = *cp; ab[0].off -= 4; ab[1] = *cp; DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab); }
    wp_refresh();
  }
}

/* ------------------------------------------------------------------ Edit Data: the chart's sheet */
#define GR 9                      /* rows shown: the series' names and 8 categories */
#define GC 6                      /* columns shown: the categories and 5 series */
static Object *edwin, *edcell[GR][GC], *edrowlab[GR], *edcollab[GC], *edrows, *edcols;
static CGrid *edgrid;
static int edr0, edc0;
static void col_letters(int c, char *out)
{
  char t[8];
  int n = 0, i;
  c++;
  while (c > 0 && n < 7) { t[n++] = (char)('A' + (c - 1) % 26); c = (c - 1) / 26; }
  for (i = 0; i < n; i++) out[i] = t[n - 1 - i];
  out[n] = 0;
}
/* the gadgets' text into the grid */
static void ed_store(void)
{
  int r, c;
  char u[200];
  if (!edgrid) return;
  for (r = 0; r < GR; r++)
    for (c = 0; c < GC; c++) {
      if (edr0 + r >= edgrid->rows || edc0 + c >= edgrid->cols || (edr0 + r == 0 && edc0 + c == 0)) continue;
      wp_latin1_utf8(sget(edcell[r][c]), u, sizeof u);
      cgrid_set(edgrid, edr0 + r, edc0 + c, u);
    }
}
/* the grid into the gadgets, from row edr0 and column edc0 */
static void ed_show(void)
{
  int r, c;
  char t[200];
  for (c = 0; c < GC; c++) { col_letters(edc0 + c, t); set(edcollab[c], MUIA_Text_Contents, (ULONG)t); }
  for (r = 0; r < GR; r++) {
    snprintf(t, sizeof t, "%d", edr0 + r + 1);
    set(edrowlab[r], MUIA_Text_Contents, (ULONG)t);
    for (c = 0; c < GC; c++) {
      int on = edgrid && edr0 + r < edgrid->rows && edc0 + c < edgrid->cols && !(edr0 + r == 0 && edc0 + c == 0);
      to_latin(on ? cgrid_get(edgrid, edr0 + r, edc0 + c) : "", t, sizeof t);
      nnset(edcell[r][c], MUIA_String_Contents, (ULONG)t);
      set(edcell[r][c], MUIA_Disabled, !on);
    }
  }
}
static void open_editdata(void)
{
  const Chart *ch = sel_chart();
  int r, c;
  if (!ch) { beep(); return; }
  if (!edwin) {
    Object *grid = ColGroup(GC + 1), MUIA_Group_Spacing, 1, End, *cont;
    if (!grid) return;
    DoMethod(grid, OM_ADDMEMBER, (ULONG)HSpace(0));
    for (c = 0; c < GC; c++) DoMethod(grid, OM_ADDMEMBER, (ULONG)(edcollab[c] = TextObject, MUIA_Text_PreParse, (ULONG)"\33c", MUIA_Text_Contents, (ULONG)"A", End));
    for (r = 0; r < GR; r++) {
      DoMethod(grid, OM_ADDMEMBER, (ULONG)(edrowlab[r] = TextObject, MUIA_Text_PreParse, (ULONG)"\33r", MUIA_Text_Contents, (ULONG)"1", MUIA_HorizWeight, 0, MUIA_FixWidthTxt, (ULONG)"000", End));
      for (c = 0; c < GC; c++)
        DoMethod(grid, OM_ADDMEMBER, (ULONG)(edcell[r][c] = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"Category 0", End));
    }
    {
      Object *bl, *br, *bu, *bd;
      cont = VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Row 1: the series' names. Column A: the categories (scatter: the X values)."), MUIA_Font, MUIV_Font_Tiny, End,
        Child, grid,
        Child, HGroup,
          Child, Label2(tr("Rows")), Child, edrows = SliderObject, MUIA_Numeric_Min, 2, MUIA_Numeric_Max, 200, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Columns")), Child, edcols = SliderObject, MUIA_Numeric_Min, 2, MUIA_Numeric_Max, 60, MUIA_CycleChain, 1, End,
          Child, bl = QButton("<"), Child, br = QButton(">"), Child, bu = QButton("^"), Child, bd = QButton("v"),
        End,
      End;
      if (!cont || !(edwin = wp_dialog("Edit Data", MAKE_ID('W','1','5','D'), cont, "_OK", ID_CH_DATAGO))) return;
      DoMethod(bl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_DATAL);
      DoMethod(br, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_DATAR);
      DoMethod(bu, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_DATAU);
      DoMethod(bd, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_DATAD);
      DoMethod(edrows, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_DATASIZE);
      DoMethod(edcols, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_DATASIZE);
    }
  }
  cgrid_free(edgrid);
  if (!(edgrid = chart_to_grid(ch))) { beep(); return; }
  edr0 = edc0 = 0;
  nnset(edrows, MUIA_Numeric_Value, edgrid->rows);
  nnset(edcols, MUIA_Numeric_Value, edgrid->cols);
  ed_show();
  set(edwin, MUIA_Window_Open, TRUE);
  set(edwin, MUIA_Window_ActiveObject, (ULONG)edcell[1][1]);
}
static void ed_scroll(int dr, int dc)
{
  if (!edgrid) return;
  ed_store();
  edr0 += dr; edc0 += dc;
  if (edr0 > edgrid->rows - GR) edr0 = edgrid->rows - GR;
  if (edc0 > edgrid->cols - GC) edc0 = edgrid->cols - GC;
  if (edr0 < 0) edr0 = 0;
  if (edc0 < 0) edc0 = 0;
  ed_show();
}
static void ed_size(void)
{
  if (!edgrid) return;
  ed_store();
  if (!cgrid_resize(edgrid, (int)xget(edrows, MUIA_Numeric_Value), (int)xget(edcols, MUIA_Numeric_Value))) beep();
  ed_scroll(0, 0);
}
static void editdata_go(void)
{
  const Chart *c = sel_chart();
  Chart *n;
  if (!edgrid) return;
  ed_store();
  set(edwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (!c || !(n = chart_copy(c))) { beep(); return; }
  if (chart_from_grid(n, edgrid)) apply(n); else beep();
  chart_free(n);
}

/* ------------------------------------------------------------------ Select Data: the series */
static Object *sdwin, *sdlist, *sdname;
static Chart *sdchart;
static void sd_fill(int active)
{
  int k;
  set(sdlist, MUIA_List_Quiet, TRUE);
  DoMethod(sdlist, MUIM_List_Clear);
  for (k = 0; sdchart && k < sdchart->ns; k++) {
    char t[120], u[100];
    if (sdchart->s[k].name) to_latin(sdchart->s[k].name, u, sizeof u); else snprintf(u, sizeof u, "%s %d", tr("Series"), k + 1);
    snprintf(t, sizeof t, "%s", u);
    DoMethod(sdlist, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom);
  }
  set(sdlist, MUIA_List_Quiet, FALSE);
  set(sdlist, MUIA_List_Active, active);
}
static int sd_active(void) { LONG a = (LONG)xget(sdlist, MUIA_List_Active); return sdchart && a >= 0 && a < sdchart->ns ? (int)a : -1; }
static void sd_pick(void)
{
  int k = sd_active();
  char t[100] = "";
  if (k >= 0 && sdchart->s[k].name) to_latin(sdchart->s[k].name, t, sizeof t);
  nnset(sdname, MUIA_String_Contents, (ULONG)t);
}
static void open_select(void)
{
  const Chart *c = sel_chart();
  if (!c) { beep(); return; }
  if (!sdwin) {
    Object *up, *down, *del, *sw, *cont = VGroup,
      Child, LLabel(tr("Legend entries (series):")),
      Child, ListviewObject, MUIA_Listview_List, sdlist = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
      Child, HGroup, Child, Label2(tr("Series name")), Child, sdname = StringObject, StringFrame, MUIA_String_MaxLen, 90, MUIA_CycleChain, 1, End, End,
      Child, HGroup, Child, up = QButton(tr("Move _Up")), Child, down = QButton(tr("Move _Down")), Child, del = QButton(tr("_Remove")),
        Child, sw = QButton(tr("S_witch Row/Column")), End,
    End;
    if (!cont || !(sdwin = wp_dialog("Select Data", MAKE_ID('W','1','5','S'), cont, "_OK", ID_CH_SELGO))) return;
    DoMethod(sdlist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_SELPICK);
    DoMethod(sdname, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_SELNAME);
    DoMethod(up, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_SELUP);
    DoMethod(down, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_SELDOWN);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_SELDEL);
    DoMethod(sw, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_SELSWITCH);
  }
  chart_free(sdchart);
  if (!(sdchart = chart_copy(c))) { beep(); return; }
  sd_fill(0);
  sd_pick();
  set(sdwin, MUIA_Window_Open, TRUE);
}
static void sd_move(int k, int to)
{
  CSeries t;
  if (k < 0 || to < 0 || to >= sdchart->ns) { beep(); return; }
  t = sdchart->s[k]; sdchart->s[k] = sdchart->s[to]; sdchart->s[to] = t;
  sd_fill(to);
}
static void sd_command(ULONG id)
{
  int k = sd_active();
  if (!sdchart) return;
  switch (id) {
  case ID_CH_SELPICK: sd_pick(); break;
  case ID_CH_SELNAME:
    if (k >= 0) {
      char u[200];
      wp_latin1_utf8(sget(sdname), u, sizeof u);
      free(sdchart->s[k].name);
      sdchart->s[k].name = u[0] ? str_dup(u) : NULL;
      DoMethod(sdlist, MUIM_List_Remove, k);
      DoMethod(sdlist, MUIM_List_InsertSingle, (ULONG)sget(sdname), k);
      nnset(sdlist, MUIA_List_Active, k);
    }
    break;
  case ID_CH_SELUP: sd_move(k, k - 1); break;
  case ID_CH_SELDOWN: sd_move(k, k + 1); break;
  case ID_CH_SELDEL:
    if (k < 0 || sdchart->ns < 2) { beep(); break; }
    {
      CSeries *s = &sdchart->s[k];
      int i;
      free(s->name); for (i = 0; i < s->ncat; i++) free(s->cat[i]);
      free(s->cat); free(s->x); free(s->v); free(s->size);
      memmove(&sdchart->s[k], &sdchart->s[k + 1], (size_t)(sdchart->ns - k - 1) * sizeof(CSeries));
      sdchart->ns--;
      sd_fill(k < sdchart->ns ? k : sdchart->ns - 1);
      sd_pick();
    }
    break;
  case ID_CH_SELSWITCH: if (!chart_switch_rowcol(sdchart)) beep(); sd_fill(0); sd_pick(); break;
  case ID_CH_SELGO:
    set(sdwin, MUIA_Window_Open, FALSE);
    set(wp_win(), MUIA_Window_Activate, TRUE);
    apply(sdchart);
    break;
  }
}

/* ------------------------------------------------------------------ Format */
static const char *const colnames[] = { "(as it is)", "Automatic", "None", "Black", "White", "Grey", "Light Grey", "Blue", "Light Blue", "Dark Blue",
  "Orange", "Gold", "Green", "Red", "Purple" };
#define NCOL 15
static const ULONG colvals[NCOL] = { 0, 0, 0, 0x000000, 0xFFFFFF, 0x808080, 0xD9D9D9, 0x4472C4, 0x9DC3E6, 0x1F3864, 0xED7D31, 0xFFC000, 0x70AD47, 0xC00000, 0x7030A0 };
static const char *const lgnames[] = { "None", "Bottom", "Top", "Left", "Right" };
static const char *const palnames[] = { "Colorful", "Monochromatic", "Accent 1", "Accent 2", "Accent 3", "Accent 4", "Accent 5", "Accent 6" };
static const char *const stylenames[] = { "Style 1", "Style 2", "Style 3", "Style 4", "Style 5", "Style 6", "Style 7", "Style 8" };
static const char *const quicknames[] = { "Layout 1", "Layout 2", "Layout 3", "Layout 4", "Layout 5", "Layout 6", "Layout 7", "Layout 8", "Layout 9", "Layout 10" };
static Object *fmwin, *fmelem, *fmfill, *fmline, *fmtitle, *fmxt, *fmyt, *fmmin, *fmmax, *fmmaj, *fmfmt, *fmlegend, *fmlabels, *fmhgrid, *fmvgrid, *fmquick, *fmpal, *fmstyle;
static Chart *fmchart;
static int fmcur;                         /* the element shown: 0 chart area, 1 plot area, 2 title, 3 legend, 4.. the series */
static const char *elnames[64 + 1];
static char elbuf[60][48];
static u32 *el_fill(Chart *c, int e) { return e == 0 ? &c->efill[CE_CHART] : e == 1 ? &c->efill[CE_PLOT] : e == 2 ? &c->efill[CE_TITLE] : e == 3 ? &c->efill[CE_LEGEND] : &c->s[e - 4].color; }
static u32 *el_line(Chart *c, int e) { return e == 0 ? &c->eline[CE_CHART] : e == 1 ? &c->eline[CE_PLOT] : e == 2 ? &c->eline[CE_TITLE] : e == 3 ? &c->eline[CE_LEGEND] : &c->s[e - 4].line; }
/* a colour choice into a fill or outline value */
static void col_take(Object *cyc, u32 *v, int outline)
{
  ULONG a = xget(cyc, MUIA_Cycle_Active);
  if (a == 0 || a >= NCOL) return;
  if (a == 1) *v = 0;
  else if (a == 2) *v = outline ? CE_NOLINE : 0;
  else *v = PRIM_ON | colvals[a];
}
static void fm_store(void)
{
  if (!fmchart || fmcur < 0 || fmcur >= 4 + fmchart->ns) return;
  col_take(fmfill, el_fill(fmchart, fmcur), 0);
  col_take(fmline, el_line(fmchart, fmcur), 1);
  nnset(fmfill, MUIA_Cycle_Active, 0); nnset(fmline, MUIA_Cycle_Active, 0);
}
static void str_set(Object *o, const char *u) { char t[200]; to_latin(u ? u : "", t, sizeof t); nnset(o, MUIA_String_Contents, (ULONG)t); }
static void num_set(Object *o, int set_, double v) { char t[64] = ""; if (set_) chart_fmt_value(v, "General", t, sizeof t); nnset(o, MUIA_String_Contents, (ULONG)t); }
static void fm_show(void)
{
  Chart *c = fmchart;
  str_set(fmtitle, c->title); str_set(fmxt, c->xtitle); str_set(fmyt, c->ytitle);
  num_set(fmmin, c->va.minset, c->va.min); num_set(fmmax, c->va.maxset, c->va.max); num_set(fmmaj, c->va.majset, c->va.major);
  str_set(fmfmt, c->fmt);
  nnset(fmlegend, MUIA_Cycle_Active, c->legend <= LG_RIGHT ? c->legend : LG_NONE);
  nnset(fmlabels, MUIA_Selected, c->labels); nnset(fmhgrid, MUIA_Selected, c->hgrid); nnset(fmvgrid, MUIA_Selected, c->vgrid);
  nnset(fmpal, MUIA_Cycle_Active, c->palette < 8 ? c->palette : 0);
  nnset(fmstyle, MUIA_Cycle_Active, c->style >= 1 && c->style <= 8 ? c->style - 1 : 0);
}
static void open_format(void)
{
  static const char *tcol[NCOL + 1], *tlg[6], *tpal[9], *tsty[9], *tq[12];
  const Chart *c = sel_chart();
  int k;
  if (!c) { beep(); return; }
  chart_free(fmchart);
  if (!(fmchart = chart_copy(c))) { beep(); return; }
  for (k = 0; k < 4; k++) elnames[k] = tr(k == 0 ? "Chart Area" : k == 1 ? "Plot Area" : k == 2 ? "Chart Title" : "Legend");
  for (k = 0; k < fmchart->ns && k < 56; k++) {
    char u[40];
    if (fmchart->s[k].name) to_latin(fmchart->s[k].name, u, sizeof u); else snprintf(u, sizeof u, "%d", k + 1);
    snprintf(elbuf[k], sizeof elbuf[k], "%s \"%s\"", tr("Series"), u);
    elnames[4 + k] = elbuf[k];
  }
  elnames[4 + k] = NULL;
  if (fmwin) { DoMethod(wp_app(), OM_REMMEMBER, (ULONG)fmwin); MUI_DisposeObject(fmwin); fmwin = NULL; }   /* (made afresh: the series differ) */
  {
    Object *cont;
    tq[0] = tr("(as it is)"); trl(quicknames, 10, tq + 1);
    trl(colnames, NCOL, tcol);        /* (filled before both cycles are made: argument order is unspecified) */
    cont = HGroup,
      Child, VGroup, GroupFrameT(tr("Format Selection")),
        Child, ColGroup(2),
          Child, Label1(tr("Element")), Child, fmelem = CycleObject, MUIA_Cycle_Entries, (ULONG)elnames, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Fill")), Child, fmfill = CycleObject, MUIA_Cycle_Entries, (ULONG)tcol, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Outline")), Child, fmline = CycleObject, MUIA_Cycle_Entries, (ULONG)tcol, MUIA_CycleChain, 1, End,
        End,
        Child, ColGroup(2), GroupFrameT(tr("Vertical (Value) Axis")),
          Child, Label2(tr("Minimum")), Child, fmmin = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Maximum")), Child, fmmax = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Major unit")), Child, fmmaj = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Number format")), Child, fmfmt = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Empty: automatic."), MUIA_Font, MUIV_Font_Tiny, End,
      End,
      Child, VGroup, GroupFrameT(tr("Chart Elements")),
        Child, ColGroup(2),
          Child, Label2(tr("Chart Title")), Child, fmtitle = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Horizontal axis title")), Child, fmxt = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Vertical axis title")), Child, fmyt = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Legend")), Child, fmlegend = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(lgnames, 5, tlg), MUIA_CycleChain, 1, End,
          Child, Label1(tr("Quick Layout")), Child, fmquick = CycleObject, MUIA_Cycle_Entries, (ULONG)tq, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Colors")), Child, fmpal = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(palnames, 8, tpal), MUIA_CycleChain, 1, End,
          Child, Label1(tr("Chart Styles")), Child, fmstyle = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(stylenames, 8, tsty), MUIA_CycleChain, 1, End,
        End,
        Child, HGroup,
          Child, fmlabels = check_obj(), Child, LLabel(tr("Data Labels")),
          Child, fmhgrid = check_obj(), Child, LLabel(tr("Horizontal gridlines")),
        End,
        Child, HGroup, Child, fmvgrid = check_obj(), Child, LLabel(tr("Vertical gridlines")), Child, HSpace(0), End,
      End,
    End;
    if (!cont || !(fmwin = wp_dialog("Format Chart", MAKE_ID('W','1','5','F'), cont, "_OK", ID_CH_FMTGO))) return;
    DoMethod(fmelem, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_FMTELEM);
    DoMethod(fmquick, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CH_FMTQUICK);
  }
  fmcur = 0;
  fm_show();
  set(fmwin, MUIA_Window_Open, TRUE);
}
static char *str_utf8(Object *o)
{
  char u[300];
  wp_latin1_utf8(sget(o), u, sizeof u);
  return u[0] ? str_dup(u) : NULL;
}
static void num_take(Object *o, u8 *set_, double *v)
{
  double d = chart_atof(sget(o));
  *set_ = (u8)!CV_IS_NONE(d);
  if (*set_) *v = d;
}
static void format_go(void)
{
  Chart *c = fmchart;
  int k;
  if (!c) return;
  k = (int)xget(fmpal, MUIA_Cycle_Active);
  if (k != c->palette) { int j; c->palette = (u8)k; for (j = 0; j < c->ns; j++) c->s[j].color = 0; }   /* (the new colours over the old ones) */
  fm_store();
  free(c->title); c->title = str_utf8(fmtitle);
  free(c->xtitle); c->xtitle = str_utf8(fmxt);
  free(c->ytitle); c->ytitle = str_utf8(fmyt);
  num_take(fmmin, &c->va.minset, &c->va.min); num_take(fmmax, &c->va.maxset, &c->va.max); num_take(fmmaj, &c->va.majset, &c->va.major);
  if (c->va.majset && !(c->va.major > 0)) c->va.majset = 0;
  snprintf(c->fmt, sizeof c->fmt, "%s", sget(fmfmt)[0] ? sget(fmfmt) : "General");
  c->legend = (u8)xget(fmlegend, MUIA_Cycle_Active);
  c->labels = (u8)(xget(fmlabels, MUIA_Selected) != 0); c->hgrid = (u8)(xget(fmhgrid, MUIA_Selected) != 0); c->vgrid = (u8)(xget(fmvgrid, MUIA_Selected) != 0);
  c->style = (u8)(xget(fmstyle, MUIA_Cycle_Active) + 1);
  if (c->title && !c->has_title) c->has_title = 1;
  set(fmwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  apply(c);
}

/* ------------------------------------------------------------------ the package */
int wp15b_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_CH_INSERT: open_insert(0); return WPR_KEEPFOCUS;
  case ID_CH_TYPE: open_insert(1); return WPR_KEEPFOCUS;
  case ID_CH_PICK: ic_show(); break;
  case ID_CH_INSGO: insert_go(); break;
  case ID_CH_EDITDATA: open_editdata(); return WPR_KEEPFOCUS;
  case ID_CH_DATAGO: editdata_go(); break;
  case ID_CH_DATAL: ed_scroll(0, -(GC - 1)); return WPR_KEEPFOCUS;
  case ID_CH_DATAR: ed_scroll(0, GC - 1); return WPR_KEEPFOCUS;
  case ID_CH_DATAU: ed_scroll(-(GR - 1), 0); return WPR_KEEPFOCUS;
  case ID_CH_DATAD: ed_scroll(GR - 1, 0); return WPR_KEEPFOCUS;
  case ID_CH_DATASIZE: ed_size(); return WPR_KEEPFOCUS;
  case ID_CH_SWITCH: change(f_switch, 0); break;
  case ID_CH_SELECT: open_select(); return WPR_KEEPFOCUS;
  case ID_CH_SELGO: case ID_CH_SELPICK: case ID_CH_SELUP: case ID_CH_SELDOWN: case ID_CH_SELDEL: case ID_CH_SELNAME: case ID_CH_SELSWITCH:
    sd_command(id); return id == ID_CH_SELGO ? WPR_DONE : WPR_KEEPFOCUS;
  case ID_CH_ELEMENT: if (value >= 0 && value < CEL_N) change(f_element, value); break;
  case ID_CH_QUICK: if (value >= 0 && value < CHART_QUICK_LAYOUTS) change(f_quick, value); break;
  case ID_CH_COLORS: if (value >= 0 && value < 8) change(f_colors, value); break;
  case ID_CH_STYLE: if (value >= 0 && value < 8) change(f_style, value); break;
  case ID_CH_FORMAT: open_format(); return WPR_KEEPFOCUS;
  case ID_CH_FMTGO: format_go(); break;
  case ID_CH_FMTELEM: fm_store(); fmcur = (int)xget(fmelem, MUIA_Cycle_Active); return WPR_KEEPFOCUS;
  case ID_CH_FMTQUICK: {
    ULONG q = xget(fmquick, MUIA_Cycle_Active);
    if (q > 0 && fmchart) { fm_store(); chart_quick_layout(fmchart, (int)q - 1); fm_show(); }
    return WPR_KEEPFOCUS; }
  }
  return WPR_DONE;
}
static void wp15b_state(ULONG *on, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (im && im->kind == OBJ_CHART && im->pack && im->pack->chart && c->vmode != VM_READ) *on |= (1UL << WPS_TAB) | (1UL << ST_CHART);
}
static void wp15b_ctxmenu(Object *menu, const WpCtx *c)
{
  const Image *im = c->where == WPC_OBJECT && c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (!im || im->kind != OBJ_CHART || !im->pack || !im->pack->chart) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr("Edit Data..."), WP_CTX_ID(ID_CH_EDITDATA), 1);
  wp_ctx_item(menu, tr("Change Chart Type..."), WP_CTX_ID(ID_CH_TYPE), 1);
  wp_ctx_item(menu, tr("Format Chart..."), WP_CTX_ID(ID_CH_FORMAT), 1);
}
static void wp15b_cleanup(void)
{
  chart_free(sdchart); sdchart = NULL;
  chart_free(fmchart); fmchart = NULL;
  cgrid_free(edgrid); edgrid = NULL;
  primview_cleanup();
}

/* the Chart Design tab */
static const ULONG palvals[8] = { 0xFF000000UL, 0x4472C4, 0x4472C4, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5 };
static const RibbonList l_element = { chart_element_names, CEL_N, PL_TEXT, NULL };
static const RibbonList l_quick = { quicknames, CHART_QUICK_LAYOUTS, PL_TEXT, NULL };
static const RibbonList l_colors = { palnames, 8, PL_COLOR, palvals };
static const RibbonList l_style = { stylenames, 8, PL_TEXT, NULL };
#define EN RB_WP(WPI_15B, ST_CHART)
static const Item g_layouts[] = {
  { K_DROP, S_LARGE, 0, 0, "Add Chart Element", ID_CH_ELEMENT, 0, EN, 0, 0, "Add a title, legend, data labels, axis titles or gridlines, or take them away", &l_element,
    "ci R 1 3 12 14 ca F 3 10 4 13 F 6 7 7 13 F 9 9 10 13 cg L 13 1 13 7 L 10 4 15 4", 0 },
  { K_DROP, S_LARGE, 0, 0, "Quick Layout", ID_CH_QUICK, 0, EN, 0, 0, "A ready-made arrangement of the chart's title, legend, labels and axes", &l_quick,
    "ci R 1 1 14 14 ca F 3 3 7 6 F 9 3 12 6 cl F 3 9 12 12", 0 } };
static const Item g_styles[] = {
  { K_DROP, S_LARGE, 0, 0, "Change Colors", ID_CH_COLORS, 0, EN, 0, 0, "The colours of the chart's series", &l_colors, "cr F 2 3 5 13 cg F 6 3 9 13 ca F 10 3 13 13", 0 },
  { K_DROP, S_LARGE, 0, 0, "Chart Styles", ID_CH_STYLE, 0, EN, 0, 0, "The look of the chart's bars, lines and background", &l_style, "ci R 1 1 14 14 ca F 3 8 5 12 cy F 7 5 9 12 cg F 11 3 13 12", 0 } };
static const Item g_data[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Switch Row/Column", ID_CH_SWITCH, 0, EN, 0, 0, "The series become the categories and the categories the series", NULL,
    "ci L 3 3 12 3 L 10 1 12 3 L 10 5 12 3 L 3 12 3 4 L 1 6 3 4 L 5 6 3 4", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Select Data", ID_CH_SELECT, 0, EN, 0, 0, "The chart's series: their names, order, and which are shown", NULL, "ci R 1 2 14 13 L 1 6 14 6 L 1 9 14 9 L 6 2 6 13 ca F 7 7 13 8", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Edit Data", ID_CH_EDITDATA, 0, EN, 0, 0, "The chart's numbers and names in a sheet", NULL, "ci R 1 2 11 13 L 1 6 11 6 L 1 9 11 9 L 5 2 5 13 co L 9 14 15 8 L 10 15 15 10", 0 } };
static const Item g_type[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Change Chart Type", ID_CH_TYPE, 0, EN, 0, 0, "Another kind of chart for the same data", NULL, "ca F 2 8 4 14 F 5 5 7 14 co P 4 9 10 11 6 13 9 15 4 ci L 1 15 15 15", 0 },
  { K_BUTTON, S_LARGE, 0, 0, "Format", ID_CH_FORMAT, 0, EN, 0, 0, "Each part's fill and outline, the titles, the value axis and the chart's elements", NULL, "ci R 2 2 13 13 ca F 4 4 11 11", 0 } };
static const WpGroup groups[] = {
  { TAB_CHARTDESIGN, -1, { "Chart Layouts", G_COLS, g_layouts, 2 } },
  { TAB_CHARTDESIGN, -1, { "Chart Styles", G_COLS, g_styles, 2 } },
  { TAB_CHARTDESIGN, -1, { "Data", G_COLS, g_data, 3 } },
  { TAB_CHARTDESIGN, -1, { "Type", G_COLS, g_type, 2 } } };
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Chart...", 0, 0, 0, (APTR)ID_CH_INSERT } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Chart", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Change Chart Type...", 0, 0, 0, (APTR)ID_CH_TYPE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Edit Data...", 0, 0, 0, (APTR)ID_CH_EDITDATA } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Select Data...", 0, 0, 0, (APTR)ID_CH_SELECT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Switch Row/Column", 0, 0, 0, (APTR)ID_CH_SWITCH } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Format Chart...", 0, 0, 0, (APTR)ID_CH_FORMAT } },
};
/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp15b_package = {
  "WP15b", ID_WP15B_FIRST, ID_WP15B_LAST,
  wp15b_command, wp15b_state, NULL, wp15b_ctxmenu, NULL, NULL, wp15b_cleanup,
  groups, 4,
  menus, (int)(sizeof menus / sizeof menus[0])
};
