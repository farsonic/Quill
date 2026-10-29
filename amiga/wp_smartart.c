/*
 * wp_smartart.c - WP15c Insert SmartArt and SmartArt Design (docs/wordcraft-gap.md). This file belongs to WP15c: only that
 * package edits it. Its commands are ID_WP15C_FIRST .. ID_WP15C_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Insert > SmartArt: a layout chosen by category with a preview, put in at the caret with sample text.
 * With a SmartArt selected, the SmartArt Design tab: Add Shape (after, before, above, below), the Text
 * Pane (the outline of items, typed and rearranged), Promote, Demote, Move Up, Move Down, Right to
 * Left, the layout, colours and style, and Reset Graphic. The item those commands work on is the one
 * chosen in the Text Pane (the last item until one is). Each change lays the graphic out afresh and
 * writes its parts (core/smartedit.c) as a new object in the selected one's place: one undo step.
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
#define Label2(l) QLabel(l, MUIO_Label_DoubleFrame)

enum { ID_SA_INSERT = ID_WP15C_FIRST,       /* (ribbon.c's Illustrations group names it ID_WP15C_FIRST) */
       ID_SA_INSGO, ID_SA_CAT, ID_SA_PICK, ID_SA_LAYOUTDLG, ID_SA_LAYOUT, ID_SA_PANE = ID_WP15C_FIRST + 6 /* (main.c runs it for a double-clicked SmartArt) */, ID_SA_PANESEL, ID_SA_PANETEXT, ID_SA_PANESYNC,
       ID_SA_ADD, ID_SA_ADDAFTER, ID_SA_ADDBEFORE, ID_SA_ADDABOVE, ID_SA_ADDBELOW, ID_SA_DELETE, ID_SA_PROMOTE, ID_SA_DEMOTE, ID_SA_UP, ID_SA_DOWN,
       ID_SA_RTL, ID_SA_COLORS, ID_SA_STYLE, ID_SA_RESET,
       ID_SA_COL0, ID_SA_COL6 = ID_SA_COL0 + 6, ID_SA_STY0, ID_SA_STY4 = ID_SA_STY0 + 4 };
#define ST_ART 1                     /* state bits: a SmartArt is selected; it reads right to left */
#define ST_RTL 2

static void beep(void) { DisplayBeep(NULL); }
static ULONG xget(Object *o, ULONG a) { ULONG v = 0; if (o) get(o, a, &v); return v; }
static const char *sget(Object *o) { const char *s = (const char *)xget(o, MUIA_String_Contents); return s ? s : ""; }
static void to_latin(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? strlen(u) : 0, out, cap); }

/* ------------------------------------------------------------------ the selected SmartArt */
static int curitem = -1;               /* the item Add Shape, Promote... work on (the Text Pane's choice) */
static ObjPack *lastpack;              /* the graphic curitem belongs to (compared, never followed) */
static int sel_index(void)
{
  Doc *d = wp_doc();
  LONG k = (LONG)xget(wp_view(), MUIA_Quill_Object);
  if (!d || k < 0 || k >= d->nimages || d->images[k].kind != OBJ_SMARTART || !d->images[k].pack) return -1;
  return (int)k;
}
/* the selected graphic's model (the caller's), NULL if none */
static SAModel *sel_model(void)
{
  int k = sel_index();
  SAModel *m = k >= 0 ? sa_from_pack(wp_doc()->images[k].pack) : NULL;
  if (m && wp_doc()->images[k].pack != lastpack) { lastpack = wp_doc()->images[k].pack; curitem = m->n - 1; }
  if (m && (curitem < 0 || curitem >= m->n)) curitem = m->n - 1;
  return m;
}
static void pane_fill(const SAModel *m);
/* the selected graphic becomes m (laid out at its size, its parts written afresh): one undo step */
static int apply(const SAModel *m)
{
  Doc *d = wp_doc();
  int k = sel_index();
  Image im;
  if (k < 0 || !m) { beep(); return 0; }
  im = d->images[k];
  if (!(im.pack = sa_pack_make(m, im.cx, im.cy, d->images[k].pack))) { beep(); return 0; }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)&im);
  lastpack = im.pack;
  pack_unref(im.pack);
  pane_fill(m);
  wp_refresh();
  return 1;
}

/* ------------------------------------------------------------------ Insert SmartArt and Change Layout */
static Object *iswin, *iscat, *islist, *isprev, *isdesc;
static int ismode;                      /* 0 insert, 1 the selected graphic's layout changed */
static int islay[64], isn;
static const char *catlabels[SAC_N + 2];
static int pick_layout(void) { LONG a = (LONG)xget(islist, MUIA_List_Active); return a >= 0 && a < isn ? islay[a] : -1; }
static void is_fill(int want)
{
  LONG cat = (LONG)xget(iscat, MUIA_List_Active);
  int k, a = 0;
  set(islist, MUIA_List_Quiet, TRUE);
  DoMethod(islist, MUIM_List_Clear);
  for (k = 0, isn = 0; k < sa_layout_count() && isn < 64; k++) {
    if (cat > 0 && sa_layout_cat(k) != cat - 1) continue;
    if (k == want) a = isn;
    islay[isn++] = k;
    DoMethod(islist, MUIM_List_InsertSingle, (ULONG)tr(sa_layout_name(k)), MUIV_List_Insert_Bottom);
  }
  set(islist, MUIA_List_Quiet, FALSE);
  set(islist, MUIA_List_Active, a);
}
static void is_show(void)
{
  int k = pick_layout();
  SAModel *m = NULL;
  if (k < 0) return;
  if (ismode && (m = sel_model())) m->layout = (u8)k;
  else if (!ismode) m = sa_sample(k);
  set(isprev, MUIA_PrimView_Art, (ULONG)m);
  set(isdesc, MUIA_Text_Contents, (ULONG)tr(sa_layout_desc(k)));
}
static void open_insert(int mode)
{
  SAModel *m = NULL;
  int k;
  if (mode && !(m = sel_model())) { beep(); return; }
  if (!iswin) {
    Object *c;
    catlabels[0] = tr("All");
    for (k = 0; k < SAC_N; k++) catlabels[k + 1] = tr(sa_category_names[k]);
    catlabels[SAC_N + 1] = NULL;
    c = HGroup,
      Child, ListviewObject, MUIA_HorizWeight, 25, MUIA_Listview_List, iscat = ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)catlabels, End, MUIA_CycleChain, 1, End,
      Child, ListviewObject, MUIA_HorizWeight, 40, MUIA_Listview_List, islist = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, End,
      Child, VGroup,
        Child, isprev = primview_new(220, 140),
        Child, isdesc = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Font, MUIV_Font_Tiny, End,
      End,
    End;
    if (!c || !isprev || !(iswin = wp_dialog("Choose a SmartArt Graphic", MAKE_ID('W','1','5','C'), c, "_OK", ID_SA_INSGO))) { sa_free(m); return; }
    DoMethod(iscat, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SA_CAT);
    DoMethod(islist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SA_PICK);
    DoMethod(islist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SA_INSGO);
  }
  ismode = mode;
  nnset(iscat, MUIA_List_Active, 0);
  is_fill(m ? m->layout : 0);
  sa_free(m);
  is_show();
  set(iswin, MUIA_Window_Open, TRUE);
  set(iswin, MUIA_Window_ActiveObject, (ULONG)islist);
}
static void insert_go(void)
{
  int k = pick_layout();
  set(iswin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (k < 0) return;
  if (ismode) {
    SAModel *m = sel_model();
    if (!m) { beep(); return; }
    m->layout = (u8)k;
    apply(m);
    sa_free(m);
  } else {
    Doc *d = wp_doc();
    SAModel *m = sa_sample(k);
    /* Word's size, 6 x 3.5 inches */
    ObjPack *pk = m ? sa_pack_make(m, 8640, 5040, NULL) : NULL;
    int im;
    Pos *cp, ab[2];
    sa_free(m);
    wp_story_close();
    if (!pk || (im = doc_add_graphic(d, pk, OBJ_SMARTART, 8640, 5040)) < 0) { beep(); return; }
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_PICTURE, (ULONG)im);
    if ((cp = wp_caret()) && cp->off >= 4) { ab[0] = *cp; ab[0].off -= 4; ab[1] = *cp; DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab); }
    lastpack = d->images[im].pack; curitem = -1;
    wp_refresh();
  }
}

/* ------------------------------------------------------------------ the Text Pane */
static Object *tpwin, *tplist, *tptext;
static void pane_fill(const SAModel *m)
{
  int i;
  if (!tpwin || !xget(tpwin, MUIA_Window_Open)) return;
  set(tplist, MUIA_List_Quiet, TRUE);
  DoMethod(tplist, MUIM_List_Clear);
  for (i = 0; m && i < m->n; i++) {
    char t[300], u[260];
    int l, n = 0;
    for (l = 0; l < m->it[i].level && n < 24; l++) { t[n++] = ' '; t[n++] = ' '; t[n++] = ' '; }
    t[n++] = (char)0xB7; t[n++] = ' ';
    to_latin(m->it[i].text, u, sizeof u);
    snprintf(t + n, sizeof t - (size_t)n, "%s", u);
    DoMethod(tplist, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom);
  }
  set(tplist, MUIA_List_Quiet, FALSE);
  if (m && curitem >= 0 && curitem < m->n) {
    char u[260];
    nnset(tplist, MUIA_List_Active, curitem);
    to_latin(m->it[curitem].text, u, sizeof u);
    nnset(tptext, MUIA_String_Contents, (ULONG)u);
  } else nnset(tptext, MUIA_String_Contents, (ULONG)"");
}
static void open_pane(void)
{
  SAModel *m = sel_model();
  if (!m) { beep(); return; }
  if (!tpwin) {
    Object *b[9];
    static const char *const labels[9] = { "_Add", "_Before", "Ab_ove", "Be_low", "_Delete", "_Promote", "De_mote", "Move _Up", "Move Do_wn" };
    static const ULONG ids[9] = { ID_SA_ADDAFTER, ID_SA_ADDBEFORE, ID_SA_ADDABOVE, ID_SA_ADDBELOW, ID_SA_DELETE, ID_SA_PROMOTE, ID_SA_DEMOTE, ID_SA_UP, ID_SA_DOWN };
    Object *close;
    int i;
    tpwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Type your text here"), MUIA_Window_ID, MAKE_ID('W','1','5','T'),
      WindowContents, VGroup, QDLG,
        Child, ListviewObject, MUIA_Listview_List, tplist = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, tptext = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Return puts the text in the chosen shape."), MUIA_Font, MUIV_Font_Tiny, End,
        Child, HGroup, Child, b[0] = QButton(tr(labels[0])), Child, b[1] = QButton(tr(labels[1])), Child, b[2] = QButton(tr(labels[2])),
          Child, b[3] = QButton(tr(labels[3])), Child, b[4] = QButton(tr(labels[4])), End,
        Child, HGroup, Child, b[5] = QButton(tr(labels[5])), Child, b[6] = QButton(tr(labels[6])), Child, b[7] = QButton(tr(labels[7])),
          Child, b[8] = QButton(tr(labels[8])), Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!tpwin) { sa_free(m); return; }
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)tpwin);
    DoMethod(tpwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)tpwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tpwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    for (i = 0; i < 9; i++) DoMethod(b[i], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ids[i]);
    DoMethod(tplist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SA_PANESEL);
    DoMethod(tptext, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SA_PANETEXT);
  }
  set(tpwin, MUIA_Window_Open, TRUE);
  pane_fill(m);
  sa_free(m);
  set(tpwin, MUIA_Window_ActiveObject, (ULONG)tptext);
}

/* ------------------------------------------------------------------ the commands */
static void edit(ULONG id, LONG value)
{
  SAModel *m = sel_model();
  int r = -1;
  if (!m) { beep(); return; }
  switch (id) {
  case ID_SA_ADDAFTER: case ID_SA_ADDBEFORE: case ID_SA_ADDABOVE: case ID_SA_ADDBELOW:
    r = sa_add(m, curitem, id == ID_SA_ADDAFTER ? SA_AFTER : id == ID_SA_ADDBEFORE ? SA_BEFORE : id == ID_SA_ADDABOVE ? SA_ABOVE : SA_BELOW, "");
    break;
  case ID_SA_ADD: r = value >= 0 && value <= 3 ? sa_add(m, curitem, (int)value, "") : -1; break;
  case ID_SA_DELETE: r = m->n > 1 ? sa_delete(m, curitem) : -1; break;
  case ID_SA_PROMOTE: r = sa_promote(m, curitem); break;
  case ID_SA_DEMOTE: r = sa_demote(m, curitem); break;
  case ID_SA_UP: r = sa_move(m, curitem, 0); break;
  case ID_SA_DOWN: r = sa_move(m, curitem, 1); break;
  case ID_SA_RTL: m->rtl = (u8)!m->rtl; r = curitem; break;
  case ID_SA_LAYOUT: if (value >= 0 && value < sa_layout_count()) { m->layout = (u8)value; r = curitem; } break;
  case ID_SA_COLORS: if (value >= 0 && value < SA_COLORS) { m->colors = (u8)value; r = curitem; } break;
  case ID_SA_STYLE: if (value >= 0 && value < SA_STYLES) { m->style = (u8)value; r = curitem; } break;
  case ID_SA_RESET: m->style = 0; m->rtl = 0; r = curitem; break;
  case ID_SA_PANETEXT: {
    char u[600];
    wp_latin1_utf8(sget(tptext), u, sizeof u);
    r = sa_set_text(m, curitem, u);
    break; }
  default:
    if (id >= ID_SA_COL0 && id <= ID_SA_COL6) { m->colors = (u8)(id - ID_SA_COL0); r = curitem; }
    else if (id >= ID_SA_STY0 && id <= ID_SA_STY4) { m->style = (u8)(id - ID_SA_STY0); r = curitem; }
    break;
  }
  if (r < 0) beep();
  else { curitem = r; apply(m); }
  sa_free(m);
}
int wp15c_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_SA_INSERT: open_insert(0); return WPR_KEEPFOCUS;
  case ID_SA_LAYOUTDLG: open_insert(1); return WPR_KEEPFOCUS;
  case ID_SA_CAT: is_fill(-1); is_show(); return WPR_KEEPFOCUS;
  case ID_SA_PICK: is_show(); return WPR_KEEPFOCUS;
  case ID_SA_INSGO: insert_go(); break;
  case ID_SA_PANE: open_pane(); return WPR_KEEPFOCUS;
  case ID_SA_PANESEL: {
    LONG a = (LONG)xget(tplist, MUIA_List_Active);
    SAModel *m = sel_model();
    if (m && a >= 0 && a < m->n) { char u[260]; curitem = (int)a; to_latin(m->it[a].text, u, sizeof u); nnset(tptext, MUIA_String_Contents, (ULONG)u); }
    sa_free(m);
    return WPR_KEEPFOCUS; }
  case ID_SA_PANESYNC: { SAModel *m = sel_model(); pane_fill(m); sa_free(m); return WPR_KEEPFOCUS; }
  case ID_SA_PANETEXT: edit(id, value); if (tpwin) set(tpwin, MUIA_Window_ActiveObject, (ULONG)tptext); return WPR_KEEPFOCUS;
  default:
    edit(id, value);
    if (tpwin && xget(tpwin, MUIA_Window_Open)) return WPR_KEEPFOCUS;     /* (a Text Pane button) */
    break;
  }
  return WPR_DONE;
}
/* (the right-to-left state: the model read again only when the graphic changes) */
static const ObjPack *rtlpack; static int rtlon;
static void wp15c_state(ULONG *on, const WpCtx *c)
{
  const Image *im = c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (!im || im->kind != OBJ_SMARTART || !im->pack || c->vmode == VM_READ) return;
  *on |= (1UL << WPS_TAB) | (1UL << ST_ART);
  if (im->pack != rtlpack) {
    SAModel *m = sa_from_pack(im->pack);
    rtlpack = im->pack; rtlon = m && m->rtl;
    sa_free(m);
    /* another graphic chosen while the Text Pane is open: it shows that one's text (afterwards, not from here) */
    if (tpwin && xget(tpwin, MUIA_Window_Open) && im->pack != lastpack)
      DoMethod(wp_app(), MUIM_Application_PushMethod, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SA_PANESYNC);
  }
  if (rtlon) *on |= 1UL << ST_RTL;
}
static void wp15c_ctxmenu(Object *menu, const WpCtx *c)
{
  const Image *im = c->where == WPC_OBJECT && c->doc && c->obj >= 0 && c->obj < c->doc->nimages ? &c->doc->images[c->obj] : NULL;
  if (!im || im->kind != OBJ_SMARTART || !im->pack) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr("Text Pane..."), WP_CTX_ID(ID_SA_PANE), 1);
  wp_ctx_item(menu, tr("Change Layout..."), WP_CTX_ID(ID_SA_LAYOUTDLG), 1);
  wp_ctx_item(menu, tr("Reset Graphic"), WP_CTX_ID(ID_SA_RESET), 1);
}
static void wp15c_cleanup(void) { primview_cleanup(); }

/* the SmartArt Design tab */
static const char *const addnames[4] = { "Add Shape After", "Add Shape Before", "Add Shape Above", "Add Shape Below" };
static const RibbonList l_add = { addnames, 4, PL_TEXT, NULL };
static const char *const lnames[] = { "Basic Block List", "Vertical Bullet List", "Horizontal Bullet List", "Basic Process", "Vertical Process", "Basic Chevron Process",
  "Basic Cycle", "Text Cycle", "Basic Radial", "Organization Chart", "Hierarchy", "Horizontal Hierarchy", "Basic Venn", "Basic Target", "Linear Venn",
  "Basic Matrix", "Titled Matrix", "Grid Matrix", "Basic Pyramid", "Inverted Pyramid", "Pyramid List" };
static const RibbonList l_layout = { lnames, 21, PL_TEXT, NULL };
static const ULONG colvals[SA_COLORS] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0xFF000000UL };
static const RibbonList l_colors = { sa_color_names, SA_COLORS, PL_COLOR, colvals };
static const RibbonList l_style = { sa_style_names, SA_STYLES, PL_TEXT, NULL };
#define EN RB_WP(WPI_15C, ST_ART)
static const Item g_create[] = {
  { K_DROP, S_LARGE, 0, 0, "Add Shape", ID_SA_ADD, 0, EN, F_SPLIT, 0, "A shape after the chosen one (the arrow: before it, above it or below it)", &l_add,
    "ca F 1 4 7 11 ci R 9 4 15 11 L 12 6 12 10 L 10 8 14 8", ID_SA_ADDAFTER },
  { K_BUTTON, S_SMALL, 0, 0, "Text Pane", ID_SA_PANE, 0, EN, F_NEWCOL, 0, "The graphic's text as an outline, to type and rearrange", NULL,
    "ci R 1 1 14 14 ca L 3 4 12 4 L 5 7 12 7 L 5 10 12 10", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Right to Left", ID_SA_RTL, RB_WP(WPI_15C, ST_RTL), EN, 0, 0, "The graphic laid out from right to left", NULL,
    "ci L 2 8 14 8 L 2 8 6 4 L 2 8 6 12", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Promote", ID_SA_PROMOTE, 0, EN, F_NEWCOL, 0, "The chosen shape a level up", NULL, "ca L 12 8 3 8 L 3 8 7 4 L 3 8 7 12", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Demote", ID_SA_DEMOTE, 0, EN, 0, 0, "The chosen shape a level down", NULL, "ca L 3 8 12 8 L 12 8 8 4 L 12 8 8 12", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Move Up", ID_SA_UP, 0, EN, F_NEWCOL, 0, "The chosen shape before the one before it", NULL, "ca L 8 13 8 3 L 8 3 4 7 L 8 3 12 7", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Move Down", ID_SA_DOWN, 0, EN, 0, 0, "The chosen shape after the one after it", NULL, "ca L 8 3 8 13 L 8 13 4 9 L 8 13 12 9", 0 } };
static const Item g_layouts[] = {
  { K_DROP, S_LARGE, 0, 0, "Layouts", ID_SA_LAYOUT, 0, EN, F_SPLIT, 0, "Another layout for the graphic (the button: with previews)", &l_layout,
    "ca F 1 2 6 6 F 9 2 14 6 F 1 9 6 13 F 9 9 14 13", ID_SA_LAYOUTDLG } };
static const Item g_styles[] = {
  { K_DROP, S_LARGE, 0, 0, "Change Colors", ID_SA_COLORS, 0, EN, 0, 0, "The graphic's colours", &l_colors, "cr F 2 3 5 13 cg F 6 3 9 13 ca F 10 3 13 13", 0 },
  { K_DROP, S_LARGE, 0, 0, "SmartArt Styles", ID_SA_STYLE, 0, EN, 0, 0, "The look of the graphic's shapes: their outlines and shadows", &l_style,
    "cl F 4 5 14 14 ca F 2 2 12 11 ci R 2 2 12 11", 0 } };
static const Item g_reset[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Reset Graphic", ID_SA_RESET, 0, EN, 0, 0, "The graphic laid out afresh from its text, in the plain style", NULL,
    "ci P 7 12 5 13 9 9 13 4 9 3 5 6 3 ca L 3 5 3 9 L 3 9 7 9", 0 } };
static const WpGroup groups[] = {
  { TAB_SMARTART, -1, { "Create Graphic", G_COLS, g_create, 7 } },
  { TAB_SMARTART, -1, { "Layouts", G_COLS, g_layouts, 1 } },
  { TAB_SMARTART, -1, { "SmartArt Styles", G_COLS, g_styles, 2 } },
  { TAB_SMARTART, -1, { "Reset", G_COLS, g_reset, 1 } } };
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, (STRPTR)"SmartArt...", 0, 0, 0, (APTR)ID_SA_INSERT } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"SmartArt", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Text Pane...", 0, 0, 0, (APTR)ID_SA_PANE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Add Shape After", 0, 0, 0, (APTR)ID_SA_ADDAFTER } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Add Shape Before", 0, 0, 0, (APTR)ID_SA_ADDBEFORE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Add Shape Above", 0, 0, 0, (APTR)ID_SA_ADDABOVE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Add Shape Below", 0, 0, 0, (APTR)ID_SA_ADDBELOW } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Promote", 0, 0, 0, (APTR)ID_SA_PROMOTE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Demote", 0, 0, 0, (APTR)ID_SA_DEMOTE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Move Up", 0, 0, 0, (APTR)ID_SA_UP } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Move Down", 0, 0, 0, (APTR)ID_SA_DOWN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Right to Left", 0, 0, 0, (APTR)ID_SA_RTL } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Change Layout...", 0, 0, 0, (APTR)ID_SA_LAYOUTDLG } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Reset Graphic", 0, 0, 0, (APTR)ID_SA_RESET } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Accent 1", 0, 0, 0, (APTR)(ID_SA_COL0 + 0) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Accent 2", 0, 0, 0, (APTR)(ID_SA_COL0 + 1) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Accent 3", 0, 0, 0, (APTR)(ID_SA_COL0 + 2) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Accent 4", 0, 0, 0, (APTR)(ID_SA_COL0 + 3) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Accent 5", 0, 0, 0, (APTR)(ID_SA_COL0 + 4) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Accent 6", 0, 0, 0, (APTR)(ID_SA_COL0 + 5) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Colorful", 0, 0, 0, (APTR)(ID_SA_COL0 + 6) } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Simple Fill", 0, 0, 0, (APTR)(ID_SA_STY0 + 0) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"White Outline", 0, 0, 0, (APTR)(ID_SA_STY0 + 1) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Subtle Effect", 0, 0, 0, (APTR)(ID_SA_STY0 + 2) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Moderate Effect", 0, 0, 0, (APTR)(ID_SA_STY0 + 3) } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Intense Effect", 0, 0, 0, (APTR)(ID_SA_STY0 + 4) } },
};
/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp15c_package = {
  "WP15c", ID_WP15C_FIRST, ID_WP15C_LAST,
  wp15c_command, wp15c_state, NULL, wp15c_ctxmenu, NULL, NULL, wp15c_cleanup,
  groups, 4,
  menus, (int)(sizeof menus / sizeof menus[0])
};
