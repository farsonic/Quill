/*
 * wp_controls.c - WP16 Content controls (docs/wordcraft-gap.md). This file belongs to WP16: only that
 * package edits it. Its commands are ID_WP16_FIRST .. ID_WP16_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The model is core/sdt.h. Here: the Developer tab's Controls group (Rich Text, Plain Text, Picture,
 * Building Block Gallery, Check Box, Combo Box, Drop-Down List, Date Picker and Repeating Section
 * content controls, Legacy Tools: text, check box and drop-down form fields and their options, Design
 * Mode, Properties, Group and Ungroup) and its Mapping group (XML Mapping Pane, the custom XML parts
 * read-only with the binding of the control at the caret); a click on a check box toggles it, on a
 * list or a combo box opens its entries, on a date picker opens a calendar, on a picture control asks
 * for a picture; typing in a control showing its placeholder replaces it; the repeating section's
 * items on the right button; with Restrict Editing's "filling in forms" on (WP7), Tab goes from form
 * field to form field.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "theme.h"
#include "wp_controls.h"
#include "../core/sdt.h"
#include "../core/field.h"
#include "../core/keep.h"
#include "../core/xml.h"
#include "../core/docx.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#define FLabel(l) QLabel(l, MUIO_Label_SingleFrame)

enum {
  ID_CC_RICH = ID_WP16_FIRST, ID_CC_PLAIN, ID_CC_PICTURE, ID_CC_GALLERY, ID_CC_CHECK, ID_CC_COMBO, ID_CC_DROP,
  ID_CC_DATE, ID_CC_REPEAT,
  ID_CC_LEGACY,                       /* Legacy Tools (value: the entry) */
  ID_CC_FF_TEXT, ID_CC_FF_CHECK, ID_CC_FF_DROP, ID_CC_FF_OPTIONS,
  ID_CC_DESIGN, ID_CC_PROPS,
  ID_CC_GROUPS,                       /* Group (value: Group, Ungroup) */
  ID_CC_GROUP, ID_CC_UNGROUP,
  ID_CC_XML, ID_CC_REMOVE,
  ID_CC_CLICK,                        /* the control at the caret clicked (wp16_click) */
  ID_CC_PROPS_OK, ID_CC_IT_ADD, ID_CC_IT_MOD, ID_CC_IT_DEL, ID_CC_IT_UP, ID_CC_IT_DOWN, ID_CC_IT_SEL,
  ID_CC_PICK_OK,                      /* the list window's choice */
  ID_CC_CAL_PREV, ID_CC_CAL_NEXT, ID_CC_CAL_DAY, ID_CC_CAL_TODAY,
  ID_CC_ITEM_AFTER, ID_CC_ITEM_BEFORE, ID_CC_ITEM_DEL,
  ID_CC_XML_PART,
  ID_CC_FFO_OK, ID_CC_FFO_ADD, ID_CC_FFO_DEL,
  ID_CC_NEXT, ID_CC_PREV,             /* filling in forms: the next or previous field (Tab) */
  ID_CC_CHOOSE,                       /* an entry chosen on the right button's menu (value) */
  ID_CC_DELETE,                       /* the control and its content */
  ID_CC_DATEFMT,                      /* a date format chosen in the Properties window */
  ID_CC_DEVTAB,                       /* View > Developer Tab: the tab shown or hidden */
  ID_CC_LAST
};
/* the state bits */
enum { ST_DESIGN = 1, ST_INCTL = 2, ST_INGROUP = 3, ST_SEL = 4, ST_INITEM = 5, ST_DEVTAB = WP16_ST_DEVTAB };

/* ------------------------------------------------------------------ */
/* helpers */
static Object *view(void) { return wp_view(); }
static Doc *doc(void) { return wp_doc(); }
static void sel(Pos *lo, Pos *hi)
{
  Pos *c = wp_caret(), *a = NULL, z;
  z.para = 0; z.off = 0;
  get(view(), MUIA_Quill_Anchor, &a);
  *lo = c ? *c : z; *hi = a ? *a : *lo;
  if (pos_cmp(*lo, *hi) > 0) { Pos t = *lo; *lo = *hi; *hi = t; }
}
static void select_range(Pos a, Pos b)
{
  Pos ab[2];
  ab[0] = a; ab[1] = b;
  DoMethod(view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
}
static void changed(void) { DoMethod(view(), MUIM_Quill_Relayout); }
static void to_latin1(const char *u, char *out, size_t cap) { utf8_to_latin1(u ? u : "", u ? strlen(u) : 0, out, cap); }
static char *gstr(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? s : (char *)""; }
static char *gutf(Object *o, char *buf, size_t cap) { wp_latin1_utf8(gstr(o), buf, cap); return buf; }
static void sstr(Object *o, const char *utf8) { char l[512]; to_latin1(utf8, l, sizeof l); set(o, MUIA_String_Contents, (ULONG)l); }
static ULONG gnum(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
static int forms_on(Doc *d) { return sdt_forms_protected && sdt_forms_protected(d); }
/* the control at the caret: its record, marks; -1 none */
static int at_caret(Pos *s, Pos *e)
{
  Pos lo, hi;
  if (!doc()) return -1;
  sel(&lo, &hi);
  return sdt_at(doc(), lo, s, e);
}
/* the form field at the caret: its field record, start and end marks; -1 none */
static int ff_at_caret(Pos *s, Pos *e)
{
  Pos lo, hi;
  int k;
  if (!doc() || !doc()->nfields) return -1;
  sel(&lo, &hi);
  k = field_at(doc(), lo, s, e);
  return k >= 0 && ff_kind(doc(), k) != FF_NONE ? k : -1;
}

/* ------------------------------------------------------------------ */
/* inserting */
static void insert_control(int type)
{
  Doc *d = doc();
  Pos lo, hi, s, e;
  int k;
  if (forms_on(d)) { DisplayBeep(NULL); return; }
  sel(&lo, &hi);
  k = sdt_insert(d, lo, hi, type, 0);
  if (k < 0) { DisplayBeep(NULL); return; }
  changed();
  /* the new control's content selected (its placeholder is replaced by typing) */
  {
    Pos from = lo;
    if (from.off) from.off--;
    if (sdt_next(d, from, 1, &s, &e) >= 0 && smark_index(d->p[s.para]->t + s.off) == k) { s.off += 4; select_range(s, e); return; }
  }
  if (sdt_at(d, lo, &s, &e) == k) { s.off += 4; select_range(s, e); }
}
static void insert_ff(int kind)
{
  Doc *d = doc();
  Pos lo, hi, at;
  if (forms_on(d)) { DisplayBeep(NULL); return; }
  sel(&lo, &hi);
  doc_begin(d, lo);
  if (pos_cmp(lo, hi)) doc_delete(d, lo, hi);
  at = lo;
  if (ff_insert(d, &at, kind) < 0) DisplayBeep(NULL);
  doc_end(d, at);
  changed();
  select_range(at, at);
}

/* ------------------------------------------------------------------ */
/* the list of a drop-down list, a combo box or a drop-down form field: a small window to choose from */
static Object *lwin, *l_list;
static Pos l_at;
static int l_ff;                       /* a form field's list (else a control's) */
static char l_txt[64][80];
static int l_n;
static void pick_open(Pos s, int ff)
{
  Doc *d = doc();
  int i, cur = -1;
  if (!lwin) {
    Object *c = VGroup,
      Child, l_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_AdjustWidth, TRUE,
        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
    End;
    if (!(lwin = wp_dialog("Choose an Item", MAKE_ID('C','C','L','S'), c, "_OK", ID_CC_PICK_OK))) return;
    DoMethod(l_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_PICK_OK);
  }
  l_at = s; l_ff = ff; l_n = 0;
  set(l_list, MUIA_List_Quiet, TRUE);
  DoMethod(l_list, MUIM_List_Clear);
  if (ff) {
    int k = fmark_index(d->p[s.para]->t + s.off), n;
    char u[256];
    n = ff_entries(d, k, 0, u, sizeof u);
    for (i = 0; i < n && l_n < 64; i++) { ff_entries(d, k, i, u, sizeof u); to_latin1(u, l_txt[l_n], sizeof l_txt[0]); DoMethod(l_list, MUIM_List_InsertSingle, (ULONG)l_txt[l_n], MUIV_List_Insert_Bottom); l_n++; }
    cur = ff_result(d, k);
  } else {
    Sdt *r = sdt_get(d, smark_index(d->p[s.para]->t + s.off));
    Pos e;
    char t[256];
    t[0] = 0;
    if (sdt_end(d, s, &e)) sdt_text(d, s, e, t, sizeof t);
    for (i = 0; r && i < r->nitems && l_n < 64; i++) {
      to_latin1(r->items[i].text, l_txt[l_n], sizeof l_txt[0]);
      if (!strcmp(r->items[i].text, t)) cur = i;
      DoMethod(l_list, MUIM_List_InsertSingle, (ULONG)l_txt[l_n], MUIV_List_Insert_Bottom); l_n++;
    }
  }
  set(l_list, MUIA_List_Quiet, FALSE);
  set(l_list, MUIA_List_Active, cur >= 0 ? cur : 0);
  set(lwin, MUIA_Window_Open, TRUE);
  set(lwin, MUIA_Window_ActiveObject, (ULONG)l_list);
}
static void pick_ok(void)
{
  LONG a = -1;
  Doc *d = doc();
  get(l_list, MUIA_List_Active, &a);
  set(lwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (a < 0 || a >= l_n || l_at.para >= d->np) return;
  if (l_ff ? !ff_choose(d, l_at, (int)a) : !sdt_choose(d, l_at, (int)a)) { DisplayBeep(NULL); return; }
  changed();
}

/* ------------------------------------------------------------------ */
/* the date picker's calendar: a month of day buttons, Today, the months before and after */
static Object *cwin, *c_title, *c_day[42];
static int c_year, c_month, c_first, c_ndays;
static Pos c_at;
static const char *const mon_en[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const wd_en[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int mdays(int y, int m) { static const int md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return md[m - 1] + (m == 2 && leap(y)); }
static int wday0(int y, int m, int d)      /* 0 Monday */
{
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y--;
  return ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 6) % 7;
}
static char c_lab[42][4];
static void cal_fill(void)
{
  char t[64];
  int i;
  c_first = wday0(c_year, c_month, 1); c_ndays = mdays(c_year, c_month);
  snprintf(t, sizeof t, "\33c\33b%s %d", tr(mon_en[c_month - 1]), c_year);
  set(c_title, MUIA_Text_Contents, (ULONG)t);
  for (i = 0; i < 42; i++) {
    int dd = i - c_first + 1;
    if (dd >= 1 && dd <= c_ndays) sprintf(c_lab[i], "%d", dd); else c_lab[i][0] = 0;
    set(c_day[i], MUIA_Text_Contents, (ULONG)c_lab[i]);
    set(c_day[i], MUIA_Disabled, dd < 1 || dd > c_ndays);
  }
}
static Object *day_button(int i)
{
  return TextObject, ButtonFrame, MUIA_InputMode, MUIV_InputMode_RelVerify, MUIA_Background, MUII_ButtonBack,
    MUIA_Text_PreParse, (ULONG)"\33c", MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, MUIA_UserData, i, End;
}
static void cal_open(Pos s)
{
  Doc *d = doc();
  Sdt *r = sdt_get(d, smark_index(d->p[s.para]->t + s.off));
  FieldTime now;
  int i;
  if (!cwin) {
    Object *grid, *prev, *next, *today;
    Object *c;
    grid = ColGroup(7), MUIA_Group_SameSize, TRUE, End;
    if (!grid) return;
    for (i = 0; i < 7; i++) DoMethod(grid, OM_ADDMEMBER, (ULONG)(TextObject, MUIA_Text_PreParse, (ULONG)"\33c", MUIA_Text_Contents, (ULONG)tr(wd_en[i]), End));
    for (i = 0; i < 42; i++) DoMethod(grid, OM_ADDMEMBER, (ULONG)(c_day[i] = day_button(i)));
    cwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Date Picker"), MUIA_Window_ID, MAKE_ID('C','C','C','L'),
      WindowContents, VGroup, QDLG,
        Child, HGroup,
          Child, prev = QButton("<"),
          Child, c_title = TextObject, MUIA_Text_Contents, (ULONG)"", End,
          Child, next = QButton(">"),
        End,
        Child, grid,
        Child, HGroup, Child, today = QButton(tr("_Today")), Child, HSpace(0), Child, c = QButton(tr("_Cancel")), End,
      End, End;
    if (!cwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)cwin);
    DoMethod(cwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)cwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(c, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)cwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(prev, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_CAL_PREV);
    DoMethod(next, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_CAL_NEXT);
    DoMethod(today, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_CAL_TODAY);
    for (i = 0; i < 42; i++) {
      /* which day: the window's user data, then the command */
      DoMethod(c_day[i], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)cwin, 3, MUIM_Set, MUIA_UserData, i);
      DoMethod(c_day[i], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_CAL_DAY);
    }
  }
  c_at = s;
  field_time_now(&now);
  c_year = now.year; c_month = now.month;
  if (r && r->fulldate) { FieldTime t; if (field_parse_time(r->fulldate, &t)) { c_year = t.year; c_month = t.month; } }
  if (c_month < 1 || c_month > 12) c_month = 1;
  cal_fill();
  set(cwin, MUIA_Window_Open, TRUE);
}
static void cal_pick(int day)
{
  Doc *d = doc();
  set(cwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (c_at.para >= d->np || !sdt_set_date(d, c_at, c_year, c_month, day)) { DisplayBeep(NULL); return; }
  changed();
}

/* ------------------------------------------------------------------ */
/* a picture control's picture: a file chosen, put in its place, no wider than the column */
static char picpath[512];
static void picture_choose(Pos s)
{
  Doc *d = doc();
  size_t n;
  u8 *data;
  int k;
  if (!wp_file_request(0, "Insert a picture (PNG or JPEG)", picpath, sizeof picpath, "#?.(png|jpg|jpeg|gif|bmp|iff|ilbm)")) return;
  data = file_read(picpath, &n);
  k = data ? doc_add_image(d, data, (u32)n, 0, 0) : -1;
  if (k < 0) { wp_ask("OK", "%s\nis not a picture Quill can read.", picpath); return; }
  {
    Image *im = &d->images[k];
    i32 w = d->page.width - d->page.left - d->page.right;
    if (im->cx > w && im->cx > 0) { im->cy = (i32)((long)im->cy * w / im->cx); im->cx = w; }
  }
  if (!sdt_set_picture(d, s, k)) DisplayBeep(NULL);
  changed();
}

/* ------------------------------------------------------------------ */
/* the Properties window: what every control has (title, tag, how it shows, colour, locks), and what
   its kind has (a page each) */
static Object *pwin, *p_title, *p_tag, *p_show, *p_color, *p_temp, *p_nodel, *p_noedit, *p_pages, *p_kind;
static Object *p_multi, *p_on, *p_onfont, *p_off, *p_offfont;
static Object *p_btn[5], *p_list, *p_itext, *p_ival, *p_datefmt, *p_datepick, *p_lid, *p_cal, *p_store, *p_gallery, *p_cat, *p_sect, *p_allow;
static Sdt p_work;
static int p_have;
static Pos p_at;
static char p_items[64][80];
static const char *show_names[] = { "Bounding Box", "Start/End Tag", "None", NULL };
static const char *color_names[] = { "Automatic", "Black", "Blue", "Green", "Orange", "Red", "Purple", "Grey", "Other", NULL };
static const ULONG color_vals[] = { 0, 0x000000, 0x0070C0, 0x00B050, 0xFFC000, 0xFF0000, 0x7030A0, 0x808080, 0 };
static const char *store_names[] = { "Text", "Date", "Date and Time", NULL };
static const char *showl[4], *colorl[10], *storel[4], *picl[17];
static void items_fill(void)
{
  int i;
  set(p_list, MUIA_List_Quiet, TRUE);
  DoMethod(p_list, MUIM_List_Clear);
  for (i = 0; i < p_work.nitems && i < 64; i++) {
    char a[40], b[40];
    to_latin1(p_work.items[i].text, a, sizeof a); to_latin1(p_work.items[i].value, b, sizeof b);
    snprintf(p_items[i], sizeof p_items[i], "%s  [%s]", a, b);
    DoMethod(p_list, MUIM_List_InsertSingle, (ULONG)p_items[i], MUIV_List_Insert_Bottom);
  }
  set(p_list, MUIA_List_Quiet, FALSE);
}
static int page_of(int type)
{
  switch (type) {
  case SDT_TEXT: return 1;
  case SDT_CHECK: return 2;
  case SDT_COMBO: case SDT_DROP: return 3;
  case SDT_DATE: return 4;
  case SDT_GALLERY: case SDT_DOCPART: return 5;
  case SDT_REPEAT: return 6;
  }
  return 0;
}
static void props_open(void)
{
  Doc *d = doc();
  Pos s, e;
  int k = at_caret(&s, &e), i;
  char t[32];
  Sdt *r;
  if (k < 0) { DisplayBeep(NULL); return; }
  if (!pwin) {
    Object *c;
    for (i = 0; show_names[i]; i++) showl[i] = tr(show_names[i]);
    showl[i] = NULL;
    for (i = 0; color_names[i]; i++) colorl[i] = tr(color_names[i]);
    colorl[i] = NULL;
    for (i = 0; store_names[i]; i++) storel[i] = tr(store_names[i]);
    storel[i] = NULL;
    for (i = 0; i < 16 && i < field_ndate_pics; i++) picl[i] = field_date_pics[i];
    picl[i] = NULL;
    c = VGroup,
      Child, p_kind = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, ColGroup(2),
        Child, FLabel(tr("Title:")), Child, p_title = StringObject, StringFrame, MUIA_String_MaxLen, 128, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Tag:")), Child, p_tag = StringObject, StringFrame, MUIA_String_MaxLen, 128, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Show as:")), Child, HGroup,
          Child, p_show = CycleObject, MUIA_Cycle_Entries, (ULONG)showl, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Color:")), Child, p_color = CycleObject, MUIA_Cycle_Entries, (ULONG)colorl, MUIA_CycleChain, 1, End,
        End,
      End,
      Child, HGroup, Child, p_temp = CheckMark(FALSE), Child, FLabel(tr("Remove content control when contents are edited")), Child, HSpace(0), End,
      Child, HGroup, Child, p_nodel = CheckMark(FALSE), Child, FLabel(tr("Content control cannot be deleted")), Child, HSpace(0), End,
      Child, HGroup, Child, p_noedit = CheckMark(FALSE), Child, FLabel(tr("Contents cannot be edited")), Child, HSpace(0), End,
      Child, p_pages = VGroup, MUIA_Group_PageMode, TRUE,
        Child, HVSpace,
        Child, VGroup, GroupFrameT(tr("Plain Text Properties")),
          Child, HGroup, Child, p_multi = CheckMark(FALSE), Child, FLabel(tr("Allow carriage returns (multiple paragraphs)")), Child, HSpace(0), End,
        End,
        Child, ColGroup(4), GroupFrameT(tr("Check Box Properties")),
          Child, FLabel(tr("Checked symbol:")), Child, p_on = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEF", MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Font:")), Child, p_onfont = StringObject, StringFrame, MUIA_String_MaxLen, 31, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Unchecked symbol:")), Child, p_off = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEF", MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Font:")), Child, p_offfont = StringObject, StringFrame, MUIA_String_MaxLen, 31, MUIA_CycleChain, 1, End,
        End,
        Child, VGroup, GroupFrameT(tr("Drop-Down List Properties")),
          Child, HGroup,
            Child, p_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
              MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
          End,
          Child, ColGroup(2),
            Child, FLabel(tr("Display Name:")), Child, p_itext = StringObject, StringFrame, MUIA_String_MaxLen, 255, MUIA_CycleChain, 1, End,
            Child, FLabel(tr("Value:")), Child, p_ival = StringObject, StringFrame, MUIA_String_MaxLen, 255, MUIA_CycleChain, 1, End,
          End,
          Child, HGroup,
            Child, p_btn[0] = QButton(tr("_Add")), Child, p_btn[1] = QButton(tr("_Modify")), Child, p_btn[2] = QButton(tr("_Remove")),
            Child, p_btn[3] = QButton(tr("Move _Up")), Child, p_btn[4] = QButton(tr("Move Do_wn")),
          End,
        End,
        Child, ColGroup(2), GroupFrameT(tr("Date Picker Properties")),
          Child, FLabel(tr("Display the date like this:")), Child, HGroup,
            Child, p_datefmt = StringObject, StringFrame, MUIA_String_MaxLen, 64, MUIA_CycleChain, 1, End,
            Child, p_datepick = CycleObject, MUIA_Cycle_Entries, (ULONG)picl, MUIA_CycleChain, 1, End,
          End,
          Child, FLabel(tr("Locale:")), Child, p_lid = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Calendar type:")), Child, p_cal = CycleObject, MUIA_Cycle_Entries, (ULONG)sdt_calendars, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Store XML contents as:")), Child, p_store = CycleObject, MUIA_Cycle_Entries, (ULONG)storel, MUIA_CycleChain, 1, End,
        End,
        Child, ColGroup(2), GroupFrameT(tr("Document Building Block Properties")),
          Child, FLabel(tr("Gallery:")), Child, p_gallery = StringObject, StringFrame, MUIA_String_MaxLen, 64, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Category:")), Child, p_cat = StringObject, StringFrame, MUIA_String_MaxLen, 64, MUIA_CycleChain, 1, End,
        End,
        Child, VGroup, GroupFrameT(tr("Repeating Section Properties")),
          Child, ColGroup(2), Child, FLabel(tr("Section title:")), Child, p_sect = StringObject, StringFrame, MUIA_String_MaxLen, 128, MUIA_CycleChain, 1, End, End,
          Child, HGroup, Child, p_allow = CheckMark(TRUE), Child, FLabel(tr("Allow users to add and remove sections")), Child, HSpace(0), End,
        End,
      End,
    End;
    if (!(pwin = wp_dialog("Content Control Properties", MAKE_ID('C','C','P','R'), c, "_OK", ID_CC_PROPS_OK))) return;
    {
      static const ULONG ids[5] = { ID_CC_IT_ADD, ID_CC_IT_MOD, ID_CC_IT_DEL, ID_CC_IT_UP, ID_CC_IT_DOWN };
      for (i = 0; i < 5; i++) DoMethod(p_btn[i], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ids[i]);
    }
    DoMethod(p_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_IT_SEL);
    DoMethod(p_datepick, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_DATEFMT);
  }
  r = sdt_get(d, k);
  if (p_have) sdt_rec_free(&p_work);
  sdt_copy(&p_work, r);
  p_have = 1;
  p_at = s;
  {
    char kind[96];
    snprintf(kind, sizeof kind, "\33b%s", tr(sdt_type_name(r->type)));
    set(p_kind, MUIA_Text_Contents, (ULONG)kind);
  }
  sstr(p_title, r->alias); sstr(p_tag, r->tag);
  set(p_show, MUIA_Cycle_Active, r->hasapp ? r->appearance : 0);
  for (i = 1; i < 8 && !(r->hascolor && color_vals[i] == r->color); i++) ;
  set(p_color, MUIA_Cycle_Active, !r->hascolor ? 0 : i < 8 ? i : 8);
  set(p_temp, MUIA_Selected, r->temporary);
  set(p_nodel, MUIA_Selected, (r->lock & SLK_DEL) != 0);
  set(p_noedit, MUIA_Selected, (r->lock & SLK_EDIT) != 0);
  set(p_multi, MUIA_Selected, r->multiline);
  sprintf(t, "%04lX", (unsigned long)r->chk_on); set(p_on, MUIA_String_Contents, (ULONG)t);
  sprintf(t, "%04lX", (unsigned long)r->chk_off); set(p_off, MUIA_String_Contents, (ULONG)t);
  sstr(p_onfont, r->chk_onfont); sstr(p_offfont, r->chk_offfont);
  items_fill();
  sstr(p_itext, ""); sstr(p_ival, "");
  sstr(p_datefmt, r->datefmt ? r->datefmt : field_date_pics[0]); sstr(p_lid, r->lid ? r->lid : "");
  for (i = 0; sdt_calendars[i] && !(r->calendar && !strcmp(r->calendar, sdt_calendars[i])); i++) ;
  set(p_cal, MUIA_Cycle_Active, sdt_calendars[i] ? i : 0);
  for (i = 0; i < 3 && !(r->storeas && !strcmp(r->storeas, sdt_store_as[i])); i++) ;
  set(p_store, MUIA_Cycle_Active, i < 3 ? i : 2);
  sstr(p_gallery, r->gallery); sstr(p_cat, r->category); sstr(p_sect, r->sectitle);
  set(p_allow, MUIA_Selected, !r->nodelitem);
  set(p_pages, MUIA_Group_ActivePage, page_of(r->type));
  set(pwin, MUIA_Window_Open, TRUE);
  set(pwin, MUIA_Window_ActiveObject, (ULONG)p_title);
}
static void set_opt(char **dst, Object *o)
{
  char u[512];
  gutf(o, u, sizeof u);
  free(*dst);
  *dst = u[0] ? str_dup(u) : NULL;
}
static void props_ok(void)
{
  Doc *d = doc();
  Sdt *w = &p_work;
  ULONG c;
  char u[64];
  set(pwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (!p_have || p_at.para >= d->np) return;
  set_opt(&w->alias, p_title); set_opt(&w->tag, p_tag);
  c = gnum(p_show, MUIA_Cycle_Active);
  if (c || w->hasapp) { w->hasapp = 1; w->appearance = (u8)c; }
  c = gnum(p_color, MUIA_Cycle_Active);
  if (c == 0) w->hascolor = 0; else if (c < 8) { w->hascolor = 1; w->color = (u32)color_vals[c]; }
  w->temporary = (u8)(gnum(p_temp, MUIA_Selected) != 0);
  w->lock = (u8)((gnum(p_nodel, MUIA_Selected) ? SLK_DEL : 0) | (gnum(p_noedit, MUIA_Selected) ? SLK_EDIT : 0));
  switch (w->type) {
  case SDT_TEXT: w->multiline = (u8)(gnum(p_multi, MUIA_Selected) != 0); break;
  case SDT_CHECK:
    if (gstr(p_on)[0]) w->chk_on = (u32)strtoul(gstr(p_on), NULL, 16);
    if (gstr(p_off)[0]) w->chk_off = (u32)strtoul(gstr(p_off), NULL, 16);
    gutf(p_onfont, u, sizeof u); snprintf(w->chk_onfont, sizeof w->chk_onfont, "%s", u);
    gutf(p_offfont, u, sizeof u); snprintf(w->chk_offfont, sizeof w->chk_offfont, "%s", u);
    break;
  case SDT_DATE:
    set_opt(&w->datefmt, p_datefmt); set_opt(&w->lid, p_lid);
    free(w->calendar); w->calendar = str_dup(sdt_calendars[gnum(p_cal, MUIA_Cycle_Active)]);
    free(w->storeas); w->storeas = str_dup(sdt_store_as[gnum(p_store, MUIA_Cycle_Active) % 3]);
    break;
  case SDT_GALLERY: case SDT_DOCPART: set_opt(&w->gallery, p_gallery); set_opt(&w->category, p_cat); break;
  case SDT_REPEAT: set_opt(&w->sectitle, p_sect); w->nodelitem = (u8)(gnum(p_allow, MUIA_Selected) == 0); break;
  }
  if (!sdt_set_props(d, p_at, w)) DisplayBeep(NULL);
  changed();
}
/* the list's entries edited in the window */
static void items_cmd(ULONG id)
{
  Sdt *w = &p_work;
  LONG a = -1;
  char t[256], v[256];
  get(p_list, MUIA_List_Active, &a);
  gutf(p_itext, t, sizeof t); gutf(p_ival, v, sizeof v);
  if (id == ID_CC_IT_SEL) {
    if (a >= 0 && a < w->nitems) { sstr(p_itext, w->items[a].text); sstr(p_ival, w->items[a].value); }
    return;
  }
  if (id == ID_CC_IT_ADD) {
    SdtItem *ni;
    int i;
    if (!t[0] || w->nitems >= 64) { DisplayBeep(NULL); return; }
    for (i = 0; i < w->nitems; i++) if (!strcmp(w->items[i].text, t)) { DisplayBeep(NULL); return; }   /* (Word: names are unique) */
    if (!(ni = (SdtItem *)realloc(w->items, (size_t)(w->nitems + 1) * sizeof(SdtItem)))) return;
    w->items = ni;
    ni[w->nitems].text = str_dup(t); ni[w->nitems].value = str_dup(v[0] ? v : t);
    w->nitems++;
    a = w->nitems - 1;
  } else if (a < 0 || a >= w->nitems) { DisplayBeep(NULL); return; }
  else if (id == ID_CC_IT_MOD) {
    free(w->items[a].text); free(w->items[a].value);
    w->items[a].text = str_dup(t); w->items[a].value = str_dup(v[0] ? v : t);
  } else if (id == ID_CC_IT_DEL) {
    int i;
    free(w->items[a].text); free(w->items[a].value);
    for (i = (int)a; i + 1 < w->nitems; i++) w->items[i] = w->items[i + 1];
    w->nitems--;
    if (a >= w->nitems) a = w->nitems - 1;
  } else if (id == ID_CC_IT_UP || id == ID_CC_IT_DOWN) {
    LONG b = id == ID_CC_IT_UP ? a - 1 : a + 1;
    SdtItem tmp;
    if (b < 0 || b >= w->nitems) return;
    tmp = w->items[a]; w->items[a] = w->items[b]; w->items[b] = tmp;
    a = b;
  }
  items_fill();
  set(p_list, MUIA_List_Active, a);
}

/* ------------------------------------------------------------------ */
/* Form Field Options (the legacy form fields): name, and the kind's own settings */
static Object *fwin, *f_name, *f_pages, *f_def, *f_max, *f_cdef, *f_list, *f_entry;
static Pos f_at;
static int f_kind;
static char f_ent[32][80];
static int f_nent;
static const char *cdefl[3];
static void ffo_fill_list(void)
{
  int i;
  set(f_list, MUIA_List_Quiet, TRUE);
  DoMethod(f_list, MUIM_List_Clear);
  for (i = 0; i < f_nent; i++) DoMethod(f_list, MUIM_List_InsertSingle, (ULONG)f_ent[i], MUIV_List_Insert_Bottom);
  set(f_list, MUIA_List_Quiet, FALSE);
}
static void ffo_open(void)
{
  Doc *d = doc();
  Pos s, e;
  int k = ff_at_caret(&s, &e), i, n;
  const Field *f;
  char v[256];
  if (k < 0) { DisplayBeep(NULL); return; }
  if (!fwin) {
    Object *c, *add, *del;
    cdefl[0] = tr("Not checked"); cdefl[1] = tr("Checked"); cdefl[2] = NULL;
    c = VGroup,
      Child, ColGroup(2), Child, FLabel(tr("Bookmark:")), Child, f_name = StringObject, StringFrame, MUIA_String_MaxLen, 40, MUIA_CycleChain, 1, End, End,
      Child, f_pages = VGroup, MUIA_Group_PageMode, TRUE,
        Child, ColGroup(2), GroupFrameT(tr("Text Form Field")),
          Child, FLabel(tr("Default text:")), Child, f_def = StringObject, StringFrame, MUIA_String_MaxLen, 255, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Maximum length:")), Child, f_max = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
        End,
        Child, ColGroup(2), GroupFrameT(tr("Check Box Form Field")),
          Child, FLabel(tr("Default value:")), Child, f_cdef = CycleObject, MUIA_Cycle_Entries, (ULONG)cdefl, MUIA_CycleChain, 1, End,
        End,
        Child, VGroup, GroupFrameT(tr("Drop-Down Form Field")),
          Child, f_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
          Child, HGroup,
            Child, f_entry = StringObject, StringFrame, MUIA_String_MaxLen, 79, MUIA_CycleChain, 1, End,
            Child, add = QButton(tr("_Add")), Child, del = QButton(tr("_Remove")),
          End,
        End,
      End,
    End;
    if (!(fwin = wp_dialog("Form Field Options", MAKE_ID('C','C','F','F'), c, "_OK", ID_CC_FFO_OK))) return;
    DoMethod(add, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_FFO_ADD);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_FFO_DEL);
  }
  f = field_get(d, k);
  f_at = s; f_kind = ff_kind(d, k);
  ff_name(d, k, v, sizeof v); sstr(f_name, v);
  ff_default_text(d, k, v, sizeof v); sstr(f_def, v);
  set(f_cdef, MUIA_Cycle_Active, ff_default_checked(d, k));
  if (ff_maxlen(d, k) > 0) sprintf(v, "%d", ff_maxlen(d, k)); else v[0] = 0;
  set(f_max, MUIA_String_Contents, (ULONG)v);
  f_nent = 0;
  n = ff_entries(d, k, 0, v, sizeof v);
  for (i = 0; i < n && f_nent < 32; i++) { ff_entries(d, k, i, v, sizeof v); to_latin1(v, f_ent[f_nent++], sizeof f_ent[0]); }
  ffo_fill_list();
  set(f_pages, MUIA_Group_ActivePage, f_kind == FF_TEXT ? 0 : f_kind == FF_CHECK ? 1 : 2);
  set(fwin, MUIA_Window_Open, TRUE);
}
static void ffo_ok(void)
{
  Doc *d = doc();
  char name[128];
  const Field *f;
  char *nf;
  int fk;
  set(fwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (f_at.para >= d->np || f_at.off + 4 > d->p[f_at.para]->len || !IS_FSTART(d->p[f_at.para]->t + f_at.off)) return;
  fk = fmark_index(d->p[f_at.para]->t + f_at.off);
  if (!(f = field_get(d, fk))) return;
  gutf(f_name, name, sizeof name);
  {
    char def[256], ents[32][128];
    const char *ep[32];
    int i;
    gutf(f_def, def, sizeof def);
    for (i = 0; i < f_nent; i++) { wp_latin1_utf8(f_ent[i], ents[i], sizeof ents[i]); ep[i] = ents[i]; }
    nf = ff_options(f->ffdata, f_kind, name, def, atoi(gstr(f_max)), gnum(f_cdef, MUIA_Cycle_Active) != 0, ff_checked(d, fk), ep, f_nent, ff_result(d, fk));
  }

  if (!nf || !ff_set_data(d, f_at, nf)) DisplayBeep(NULL);
  free(nf);
  changed();
}

/* ------------------------------------------------------------------ */
/* the XML Mapping pane: the document's custom XML parts, each shown as a tree (read only), and the
   binding of the control at the caret */
static Object *xwin, *x_part, *x_list, *x_bind;
static const char *x_parts[17];
static char x_pname[16][64];
static int x_pk[16], x_np;
static char **x_lines;
static int x_nl;
static void x_free(void)
{
  int i;
  for (i = 0; i < x_nl; i++) free(x_lines[i]);
  free(x_lines); x_lines = NULL; x_nl = 0;
}
static void x_add(int depth, const char *s, size_t n)
{
  char u[200], l[200];
  char **nl;
  int ind = depth * 2 < 40 ? depth * 2 : 40;
  if (x_nl >= 600 || !(nl = (char **)realloc(x_lines, (size_t)(x_nl + 1) * sizeof(char *)))) return;
  x_lines = nl;
  memset(u, ' ', (size_t)ind);
  if (n > sizeof u - (size_t)ind - 1) n = sizeof u - (size_t)ind - 1;
  memcpy(u + ind, s, n); u[ind + n] = 0;
  to_latin1(u, l, sizeof l);
  x_lines[x_nl++] = str_dup(l);
}
static void x_show(void)
{
  Doc *d = doc();
  ULONG a = gnum(x_part, MUIA_Cycle_Active);
  int i;
  x_free();
  set(x_list, MUIA_List_Quiet, TRUE);
  DoMethod(x_list, MUIM_List_Clear);
  if ((int)a < x_np && d->kept) {
    const KeptPart *kp = &d->kept[x_pk[a]];
    Xml x;
    xml_init(&x, (const char *)kp->data, kp->len);
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_START) {
        char t[160];
        int j, n = snprintf(t, sizeof t, "<%s", x.name);
        for (j = 0; j < x.nattr && n < (int)sizeof t - 8; j++) n += snprintf(t + n, sizeof t - (size_t)n, " %s=\"%s\"", x.an[j], x.av[j]);
        snprintf(t + (n < (int)sizeof t - 2 ? n : (int)sizeof t - 2), 2, ">");
        x_add(x.depth - 1, t, strlen(t));
      } else if (x.type == XT_TEXT) {
        const char *s = x.text.data;
        size_t n = x.text.len;
        while (n && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) { s++; n--; }
        while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t')) n--;
        if (n) { char t[160]; snprintf(t, sizeof t, "\"%.*s\"", (int)(n < 150 ? n : 150), s); x_add(x.depth, t, strlen(t)); }
      }
    }
    xml_free(&x);
  }
  for (i = 0; i < x_nl; i++) DoMethod(x_list, MUIM_List_InsertSingle, (ULONG)x_lines[i], MUIV_List_Insert_Bottom);
  set(x_list, MUIA_List_Quiet, FALSE);
  {
    Pos s, e;
    int k = at_caret(&s, &e);
    Sdt *r = sdt_get(d, k);
    char b[300] = "", xp[256] = "";
    if (r && r->binding) {
      const char *p = strstr(r->binding, "xpath=\"");
      if (p) { const char *q = strchr(p + 7, '"'); if (q) snprintf(xp, sizeof xp, "%.*s", (int)(q - p - 7 < 250 ? q - p - 7 : 250), p + 7); }
    }
    snprintf(b, sizeof b, "%s %s", tr("Mapped to:"), xp[0] ? xp : tr("(the control at the cursor is not mapped)"));
    {
      char l[300];
      to_latin1(b, l, sizeof l);
      set(x_bind, MUIA_Text_Contents, (ULONG)l);
    }
  }
}
static void xml_open(void)
{
  Doc *d = doc();
  int i;
  x_np = 0;
  for (i = 0; i < d->nkept && x_np < 16; i++) {
    const KeptPart *kp = &d->kept[i];
    const char *nm = kp->name;
    if (strncmp(nm, "customXml/", 10) || strstr(nm, "itemProps") || strstr(nm, "_rels")) continue;
    {
      /* named by its root element's namespace, as Word's pane does */
      Xml x;
      char ns[64] = "";
      xml_init(&x, (const char *)kp->data, kp->len);
      while (xml_next(&x) != XT_EOF) if (x.type == XT_START) { const char *v = xml_attr(&x, "xmlns"); snprintf(ns, sizeof ns, "%s", v ? v : x.name); break; }
      xml_free(&x);
      snprintf(x_pname[x_np], sizeof x_pname[0], "%s", ns[0] ? ns : nm + 10);
    }
    x_parts[x_np] = x_pname[x_np];
    x_pk[x_np++] = i;
  }
  if (!x_np) { strcpy(x_pname[0], tr("(no custom XML parts)")); x_parts[0] = x_pname[0]; }
  x_parts[x_np ? x_np : 1] = NULL;
  if (xwin) { set(xwin, MUIA_Window_Open, FALSE); DoMethod(wp_app(), OM_REMMEMBER, (ULONG)xwin); MUI_DisposeObject(xwin); xwin = NULL; }
  {
    Object *close;
    xwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("XML Mapping"), MUIA_Window_ID, MAKE_ID('C','C','X','M'),
      WindowContents, VGroup, QDLG,
        Child, HGroup, Child, FLabel(tr("Custom XML Part:")), Child, x_part = CycleObject, MUIA_Cycle_Entries, (ULONG)x_parts, MUIA_CycleChain, 1, End, End,
        Child, x_list = ListviewObject, MUIA_Listview_Input, FALSE, MUIA_Listview_List, ListObject, ReadListFrame,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
        Child, x_bind = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
        Child, HGroup, Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!xwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)xwin);
    DoMethod(xwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)xwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)xwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(x_part, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CC_XML_PART);
  }
  x_show();
  set(xwin, MUIA_Window_Open, TRUE);
}

/* ------------------------------------------------------------------ */
/* clicks: the view asks what a click at p means (wp_controls.h) */
ULONG wp16_click(Doc *d, Pos p)
{
  Pos s, e;
  int k;
  if (d->nfields) {
    Pos fs, fe;
    int f = field_at(d, p, &fs, &fe);
    if (f >= 0 && (ff_kind(d, f) == FF_CHECK || ff_kind(d, f) == FF_DROP) && field_get(d, f)->nosep) return ID_CC_CLICK;
  }
  if ((k = sdt_at(d, p, &s, &e)) < 0) return 0;
  switch (d->sdt->r[k].type) {
  case SDT_CHECK: case SDT_DROP: case SDT_COMBO: case SDT_DATE: case SDT_PICTURE: return ID_CC_CLICK;
  }
  return 0;
}
static void click(void)
{
  Doc *d = doc();
  Pos s, e, lo, hi;
  int k;
  sel(&lo, &hi);
  if (d->nfields) {
    int f = field_at(d, lo, &s, &e);
    if (f >= 0 && ff_kind(d, f) == FF_CHECK) { if (ff_toggle(d, s)) changed(); select_range(lo, lo); return; }
    if (f >= 0 && ff_kind(d, f) == FF_DROP) { pick_open(s, 1); return; }
  }
  if ((k = sdt_at(d, lo, &s, &e)) < 0) return;
  switch (d->sdt->r[k].type) {
  case SDT_CHECK:
    if (d->sdt->r[k].lock & SLK_EDIT) { DisplayBeep(NULL); return; }
    if (sdt_toggle(d, s)) changed();
    e.off += 4;      /* the caret after the box, so another click toggles again */
    if (e.para < d->np && e.off <= d->p[e.para]->len) select_range(e, e);
    break;
  case SDT_DROP: case SDT_COMBO: if (!(d->sdt->r[k].lock & SLK_EDIT)) pick_open(s, 0); break;
  case SDT_DATE: if (!(d->sdt->r[k].lock & SLK_EDIT)) cal_open(s); break;
  case SDT_PICTURE: if (!(d->sdt->r[k].lock & SLK_EDIT)) picture_choose(s); break;
  }
}
/* filling in forms: Tab and Shift-Tab go to the next or previous field, its content selected */
static void next_field(int dir)
{
  Doc *d = doc();
  Pos lo, hi, a, b;
  sel(&lo, &hi);
  if (!sdt_forms_next(d, dir > 0 ? hi : lo, dir, &a, &b)) { DisplayBeep(NULL); return; }
  select_range(a, b);
}

/* ------------------------------------------------------------------ */
/* every command of the package */
/* the Developer tab shown (off until the user turns it on, as in Word) */
static int devtab;
int wp16_devtab_on(void) { return devtab; }
void wp16_devtab_set(int on)
{
  devtab = on != 0;
  SetVar((STRPTR)"Quill/DeveloperTab", (STRPTR)(devtab ? "1" : "0"), -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
  if (wp_view()) wp_refresh();                     /* the ribbon (its tab strip) afresh */
}
int wp16_command(ULONG id, LONG value)
{
  Doc *d = doc();
  Pos s, e, lo, hi;
  if (id == ID_CC_DEVTAB) { wp16_devtab_set(!devtab); return WPR_DONE; }
  if (!d) return WPR_DONE;
  switch (id) {
  case ID_CC_RICH: insert_control(SDT_RICH); return WPR_DONE;
  case ID_CC_PLAIN: insert_control(SDT_TEXT); return WPR_DONE;
  case ID_CC_PICTURE: insert_control(SDT_PICTURE); return WPR_DONE;
  case ID_CC_GALLERY: insert_control(SDT_GALLERY); return WPR_DONE;
  case ID_CC_CHECK: insert_control(SDT_CHECK); return WPR_DONE;
  case ID_CC_COMBO: insert_control(SDT_COMBO); return WPR_DONE;
  case ID_CC_DROP: insert_control(SDT_DROP); return WPR_DONE;
  case ID_CC_DATE: insert_control(SDT_DATE); return WPR_DONE;
  case ID_CC_REPEAT: insert_control(SDT_REPEAT); return WPR_DONE;
  case ID_CC_LEGACY:
    if (value == 0) insert_ff(FF_TEXT);
    else if (value == 1) insert_ff(FF_CHECK);
    else if (value == 2) insert_ff(FF_DROP);
    else if (value == 3) { ffo_open(); return WPR_KEEPFOCUS; }
    return WPR_DONE;
  case ID_CC_FF_TEXT: insert_ff(FF_TEXT); return WPR_DONE;
  case ID_CC_FF_CHECK: insert_ff(FF_CHECK); return WPR_DONE;
  case ID_CC_FF_DROP: insert_ff(FF_DROP); return WPR_DONE;
  case ID_CC_FF_OPTIONS: ffo_open(); return WPR_KEEPFOCUS;
  case ID_CC_DESIGN: { SdtSet *ss = sdt_set(d); if (ss) ss->design = (u8)!ss->design; DoMethod(view(), MUIM_Quill_Relayout); wp_refresh(); return WPR_DONE; }
  case ID_CC_PROPS:
    if (ff_at_caret(&s, &e) >= 0 && at_caret(&s, &e) < 0) { ffo_open(); return WPR_KEEPFOCUS; }
    props_open(); return WPR_KEEPFOCUS;
  case ID_CC_PROPS_OK: props_ok(); return WPR_DONE;
  case ID_CC_IT_ADD: case ID_CC_IT_MOD: case ID_CC_IT_DEL: case ID_CC_IT_UP: case ID_CC_IT_DOWN: case ID_CC_IT_SEL: if (p_have) items_cmd(id); return WPR_KEEPFOCUS;
  case ID_CC_DATEFMT: { ULONG a = gnum(p_datepick, MUIA_Cycle_Active); if ((int)a < field_ndate_pics) set(p_datefmt, MUIA_String_Contents, (ULONG)field_date_pics[a]); return WPR_KEEPFOCUS; }
  case ID_CC_GROUPS: id = value == 1 ? ID_CC_UNGROUP : ID_CC_GROUP; /* fall through */
  case ID_CC_GROUP: case ID_CC_UNGROUP:
    sel(&lo, &hi);
    if (id == ID_CC_GROUP ? sdt_group(d, lo, hi) < 0 : !sdt_ungroup(d, lo)) DisplayBeep(NULL);
    changed();
    return WPR_DONE;
  case ID_CC_XML: xml_open(); return WPR_KEEPFOCUS;
  case ID_CC_XML_PART: if (xwin) x_show(); return WPR_KEEPFOCUS;
  case ID_CC_REMOVE: case ID_CC_DELETE:
    if (at_caret(&s, &e) < 0 || !sdt_remove(d, s, id == ID_CC_REMOVE)) { DisplayBeep(NULL); return WPR_DONE; }
    changed();
    select_range(s, s);
    return WPR_DONE;
  case ID_CC_CLICK: click(); return WPR_DONE;
  case ID_CC_PICK_OK: if (lwin) pick_ok(); return WPR_DONE;
  case ID_CC_CAL_PREV: case ID_CC_CAL_NEXT:
    c_month += id == ID_CC_CAL_NEXT ? 1 : -1;
    if (c_month < 1) { c_month = 12; c_year--; } else if (c_month > 12) { c_month = 1; c_year++; }
    cal_fill();
    return WPR_KEEPFOCUS;
  case ID_CC_CAL_DAY: { ULONG i = gnum(cwin, MUIA_UserData); int dd = (int)i - c_first + 1; if (dd >= 1 && dd <= c_ndays) cal_pick(dd); return WPR_DONE; }
  case ID_CC_CAL_TODAY: { FieldTime t; field_time_now(&t); c_year = t.year; c_month = t.month; cal_pick(t.day); return WPR_DONE; }
  case ID_CC_ITEM_AFTER: case ID_CC_ITEM_BEFORE: case ID_CC_ITEM_DEL: {
    int depth;
    sel(&lo, &hi);
    for (depth = 0; depth < 16; depth++) {
      int k = sdt_at_depth(d, lo, depth, &s, &e);
      if (k < 0) break;
      if (d->sdt->r[k].type == SDT_REPITEM) {
        Pos ni;
        int ok = id == ID_CC_ITEM_DEL ? sdt_item_remove(d, s) : sdt_item_add(d, s, id == ID_CC_ITEM_BEFORE, &ni);
        if (!ok) DisplayBeep(NULL);
        changed();
        if (ok && id != ID_CC_ITEM_DEL) { ni.off += 4; select_range(ni, ni); }
        return WPR_DONE;
      }
    }
    DisplayBeep(NULL);
    return WPR_DONE; }
  case ID_CC_FFO_OK: if (fwin) ffo_ok(); return WPR_DONE;
  case ID_CC_FFO_ADD: { char *t = gstr(f_entry); if (t[0] && f_nent < 32) { snprintf(f_ent[f_nent++], sizeof f_ent[0], "%s", t); ffo_fill_list(); set(f_entry, MUIA_String_Contents, (ULONG)""); } return WPR_KEEPFOCUS; }
  case ID_CC_FFO_DEL: {
    LONG a = -1;
    int i;
    get(f_list, MUIA_List_Active, &a);
    if (a >= 0 && a < f_nent) { for (i = (int)a; i + 1 < f_nent; i++) strcpy(f_ent[i], f_ent[i + 1]); f_nent--; ffo_fill_list(); }
    return WPR_KEEPFOCUS; }
  case ID_CC_NEXT: next_field(1); return WPR_DONE;
  case ID_CC_PREV: next_field(-1); return WPR_DONE;
  case ID_CC_CHOOSE: {
    int f = ff_at_caret(&s, &e);
    if (f >= 0 && ff_kind(d, f) == FF_DROP) { if (ff_choose(d, s, (int)value)) changed(); return WPR_DONE; }
    if (at_caret(&s, &e) >= 0 && sdt_choose(d, s, (int)value)) changed(); else DisplayBeep(NULL);
    return WPR_DONE; }
  }
  return WPR_DONE;
}

/* ------------------------------------------------------------------ */
/* the ribbon: the Developer tab's Controls and Mapping groups */
#define ICON_RICH "ci L 1 13 4 3 L 4 3 7 13 L 2 9 6 9 ca L 9 8 13 8 L 13 8 13 13 L 13 13 9 13 L 9 13 9 11 L 9 11 13 11"
#define ICON_PLAIN "ci L 1 13 4 3 L 4 3 7 13 L 2 9 6 9 L 9 8 13 8 L 13 8 13 13 L 13 13 9 13 L 9 13 9 11 L 9 11 13 11"
#define ICON_PICT "ci R 1 2 14 13 cg P 4 2 12 6 7 9 10 14 12 ca O 5 6 1"
#define ICON_GALL "ci R 1 1 14 14 L 1 5 14 5 ca F 3 7 6 12 F 9 7 12 12"
#define ICON_CHECK "ci R 3 3 12 12 ca L 5 8 7 10 L 7 10 11 5 L 5 7 7 9 L 7 9 11 4"
#define ICON_COMBO "ci R 1 4 14 11 L 10 4 10 11 ca L 11 7 13 7 L 11 7 12 9 L 13 7 12 9"
#define ICON_DROP "ci R 1 1 14 6 L 10 1 10 6 ca L 11 3 13 3 L 12 4 12 4 cd L 2 9 13 9 L 2 12 13 12"
#define ICON_DATE "ci R 1 3 14 14 F 1 3 14 5 L 4 1 4 3 L 11 1 11 3 ca F 4 8 5 9 F 8 8 9 9 F 11 8 12 9 F 4 11 5 12"
#define ICON_REPEAT "ci R 1 1 11 6 R 1 9 11 14 ca L 13 9 13 13 L 11 11 15 11"
#define ICON_LEGACY "ci R 1 6 14 14 P 4 4 6 5 3 10 3 11 6 L 1 9 14 9 ca F 7 8 8 10"
#define ICON_DESIGN "ci P 4 2 14 14 14 2 2 2 14 ca L 6 12 13 5 L 5 13 6 12"
#define ICON_PROPS "ci R 2 1 13 14 ca L 4 4 11 4 L 4 7 11 7 L 4 10 9 10"
#define ICON_GROUP "ci R 1 1 14 14 cd R 3 3 8 8 R 7 7 12 12"
#define ICON_XML "ci P 3 6 3 2 8 6 13 P 3 10 3 14 8 10 13 ca L 9 2 7 14"
static const char *const legacy_names[4] = { "Text Form Field", "Check Box Form Field", "Drop-Down Form Field", "Form Field Options..." };
static const char *legacy_lab[4];
static const RibbonList legacy_rl = { legacy_lab, 4, PL_TEXT, NULL };
static const char *const group_names[2] = { "Group", "Ungroup" };
static const char *group_lab[2];
static const RibbonList group_rl = { group_lab, 2, PL_TEXT, NULL };
static const Item ctl_items[] = {
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_RICH, 0, 0, 0, 0, "Rich Text Content Control", NULL, ICON_RICH },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_PLAIN, 0, 0, 0, 0, "Plain Text Content Control", NULL, ICON_PLAIN },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_PICTURE, 0, 0, 0, 0, "Picture Content Control", NULL, ICON_PICT },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_GALLERY, 0, 0, F_NEWCOL, 0, "Building Block Gallery Content Control", NULL, ICON_GALL },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_CHECK, 0, 0, 0, 0, "Check Box Content Control", NULL, ICON_CHECK },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_COMBO, 0, 0, 0, 0, "Combo Box Content Control", NULL, ICON_COMBO },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_DROP, 0, 0, F_NEWCOL, 0, "Drop-Down List Content Control", NULL, ICON_DROP },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_DATE, 0, 0, 0, 0, "Date Picker Content Control", NULL, ICON_DATE },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_CC_REPEAT, 0, 0, 0, 0, "Repeating Section Content Control", NULL, ICON_REPEAT },
  { K_DROP, S_ICON, 0, 0, NULL, ID_CC_LEGACY, 0, 0, F_NEWCOL, 0, "Legacy Tools: form fields", &legacy_rl, ICON_LEGACY },
  { K_BUTTON, S_SMALL, 0, 0, "Design Mode", ID_CC_DESIGN, RB_WP(WPI_16, ST_DESIGN), 0, F_NEWCOL, 0, "Show the controls' tags, to see and change them", NULL, ICON_DESIGN },
  { K_BUTTON, S_SMALL, 0, 0, "Properties", ID_CC_PROPS, 0, 0, 0, 0, "The properties of the control at the cursor", NULL, ICON_PROPS },
  { K_DROP, S_SMALL, 0, 0, "Group", ID_CC_GROUPS, 0, 0, F_SPLIT, 0, "Group the selection, so only its controls can be changed; or ungroup", &group_rl, ICON_GROUP, ID_CC_GROUP } };
static const Item map_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "XML Mapping Pane", ID_CC_XML, 0, 0, 0, 0, "The custom XML parts of the document and what the controls are mapped to", NULL, ICON_XML } };
static const WpGroup groups[] = {
  { TAB_DEVELOPER, -1, { "Controls", G_COLS, ctl_items, (int)(sizeof ctl_items / sizeof ctl_items[0]) } },
  { TAB_DEVELOPER, -1, { "Mapping", G_COLS, map_items, 1 } } };
/* the menus: Insert > Content Control, Legacy Form Fields, Content Control Tools */
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Content Control", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Rich Text", 0, 0, 0, (APTR)ID_CC_RICH } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Plain Text", 0, 0, 0, (APTR)ID_CC_PLAIN } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Picture", 0, 0, 0, (APTR)ID_CC_PICTURE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Building Block Gallery", 0, 0, 0, (APTR)ID_CC_GALLERY } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Check Box", 0, 0, 0, (APTR)ID_CC_CHECK } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Combo Box", 0, 0, 0, (APTR)ID_CC_COMBO } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Drop-Down List", 0, 0, 0, (APTR)ID_CC_DROP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Date Picker", 0, 0, 0, (APTR)ID_CC_DATE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Repeating Section", 0, 0, 0, (APTR)ID_CC_REPEAT } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Legacy Form Fields", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Text Form Field", 0, 0, 0, (APTR)ID_CC_FF_TEXT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Check Box Form Field", 0, 0, 0, (APTR)ID_CC_FF_CHECK } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Drop-Down Form Field", 0, 0, 0, (APTR)ID_CC_FF_DROP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Form Field Options...", 0, 0, 0, (APTR)ID_CC_FF_OPTIONS } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Content Control Tools", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Design Mode", 0, 0, 0, (APTR)ID_CC_DESIGN } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Properties...", 0, 0, 0, (APTR)ID_CC_PROPS } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Group", 0, 0, 0, (APTR)ID_CC_GROUP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Ungroup", 0, 0, 0, (APTR)ID_CC_UNGROUP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Remove Content Control", 0, 0, 0, (APTR)ID_CC_REMOVE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Insert Item After", 0, 0, 0, (APTR)ID_CC_ITEM_AFTER } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Delete Item", 0, 0, 0, (APTR)ID_CC_ITEM_DEL } },
  { WM_INSERT, { NM_SUB, (STRPTR)"XML Mapping Pane...", 0, 0, 0, (APTR)ID_CC_XML } },
  { WM_VIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Developer Tab", 0, CHECKIT | MENUTOGGLE, 0, (APTR)ID_CC_DEVTAB } } };

static void state(ULONG *on, const WpCtx *c)
{
  Pos s, e;
  int depth;
  Object *strip = NULL, *it;
  if (devtab) *on |= 1UL << ST_DEVTAB;
  if (wp_win()) get(wp_win(), MUIA_Window_Menustrip, &strip);       /* View > Developer Tab's tick kept */
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, ID_CC_DEVTAB))) {
    ULONG ck = 0;
    get(it, MUIA_Menuitem_Checked, &ck);
    if ((ck != 0) != (devtab != 0)) SetAttrs(it, MUIA_NoNotify, TRUE, MUIA_Menuitem_Checked, devtab, TAG_DONE);
  }
  if (!c->doc) return;
  if (c->doc->sdt && c->doc->sdt->design) *on |= 1UL << ST_DESIGN;
  if (c->hassel) *on |= 1UL << ST_SEL;
  if (!sdt_count(c->doc)) return;
  for (depth = 0; depth < 8; depth++) {
    int k = sdt_at_depth(c->doc, c->caret, depth, &s, &e);
    if (k < 0) break;
    if (!depth) *on |= 1UL << ST_INCTL;
    if (c->doc->sdt->r[k].type == SDT_GROUP) *on |= 1UL << ST_INGROUP;
    if (c->doc->sdt->r[k].type == SDT_REPITEM) *on |= 1UL << ST_INITEM;
  }
}
/* the right button in a control or form field */
static const char *ctx_lab[32];
static char ctx_txt[32][64];
static void ctxmenu(Object *menu, const WpCtx *c)
{
  Pos s, e;
  int k, depth, item = 0;
  Doc *d = c->doc;
  if (c->where != WPC_TEXT || !d) return;
  if (d->nfields) {
    int f = field_at(d, c->caret, &s, &e);
    if (f >= 0 && ff_kind(d, f) != FF_NONE) {
      wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
      if (ff_kind(d, f) == FF_DROP) {
        int n = ff_entries(d, f, 0, ctx_txt[0], sizeof ctx_txt[0]), i;
        char u[64];
        for (i = 0; i < n && i < 32; i++) { ff_entries(d, f, i, u, sizeof u); to_latin1(u, ctx_txt[i], sizeof ctx_txt[0]); ctx_lab[i] = ctx_txt[i]; }
        if (n > 0) wp_ctx_sub(menu, tr("Choose an Item"), ID_CC_CHOOSE, ctx_lab, n < 32 ? n : 32);
      }
      if (ff_kind(d, f) == FF_CHECK) wp_ctx_item(menu, tr(ff_checked(d, f) ? "Uncheck" : "Check"), WP_CTX_ID(ID_CC_CLICK), 1);
      wp_ctx_item(menu, tr("Form Field Options..."), WP_CTX_ID(ID_CC_FF_OPTIONS), !forms_on(d));
      return;
    }
  }
  if (!sdt_count(d) || (k = sdt_at(d, c->caret, &s, &e)) < 0) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  {
    Sdt *r = &d->sdt->r[k];
    if ((r->type == SDT_DROP || r->type == SDT_COMBO) && r->nitems) {
      int i;
      for (i = 0; i < r->nitems && i < 32; i++) { to_latin1(r->items[i].text, ctx_txt[i], sizeof ctx_txt[0]); ctx_lab[i] = ctx_txt[i]; }
      wp_ctx_sub(menu, tr("Choose an Item"), ID_CC_CHOOSE, ctx_lab, r->nitems < 32 ? r->nitems : 32);
    }
    if (r->type == SDT_DATE) wp_ctx_item(menu, tr("Date Picker..."), WP_CTX_ID(ID_CC_CLICK), !(r->lock & SLK_EDIT));
    if (r->type == SDT_PICTURE) wp_ctx_item(menu, tr("Change Picture..."), WP_CTX_ID(ID_CC_CLICK), !(r->lock & SLK_EDIT));
    if (r->type == SDT_CHECK) wp_ctx_item(menu, tr(sdt_checked(d, s) == 1 ? "Uncheck" : "Check"), WP_CTX_ID(ID_CC_CLICK), !(r->lock & SLK_EDIT));
  }
  for (depth = 0; depth < 8 && !item; depth++) {
    int kk = sdt_at_depth(d, c->caret, depth, &s, &e);
    if (kk < 0) break;
    if (d->sdt->r[kk].type == SDT_REPITEM) {
      item = 1;
      wp_ctx_item(menu, tr("Insert Item Before"), WP_CTX_ID(ID_CC_ITEM_BEFORE), 1);
      wp_ctx_item(menu, tr("Insert Item After"), WP_CTX_ID(ID_CC_ITEM_AFTER), 1);
      wp_ctx_item(menu, tr("Delete Item"), WP_CTX_ID(ID_CC_ITEM_DEL), 1);
    }
  }
  wp_ctx_item(menu, tr("Remove Content Control"), WP_CTX_ID(ID_CC_REMOVE), !(d->sdt->r[k].lock & SLK_DEL) && !forms_on(d));
  wp_ctx_item(menu, tr("Properties..."), WP_CTX_ID(ID_CC_PROPS), !forms_on(d));
}
/* keys: Tab between form fields while forms are protected; typing over a placeholder; Alt-Down opens a
   list; Space toggles a check box */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  int ctrl = (qual & IEQUALIFIER_CONTROL) != 0;
  Doc *d = c->doc;
  Pos s, e;
  int k;
  if (!d) return 0;
  if (code == 0x42 && !ctrl && !alt && forms_on(d)) return shift ? ID_CC_PREV : ID_CC_NEXT;
  if (!sdt_count(d) && !d->nfields) return 0;
  if (code == 0x4D && alt) { if (wp16_click(d, c->caret)) return ID_CC_CLICK; return 0; }
  if (!sdt_count(d) || (k = sdt_at(d, c->caret, &s, &e)) < 0) return 0;
  if (code == 0x40 && !ctrl && !alt && d->sdt->r[k].type == SDT_CHECK) return ID_CC_CLICK;
  if (!ctrl && !alt && code <= 0x40 && code != 0x00) {          /* (0x40: Space) */
    /* a character typed: a placeholder showing goes first; a temporary control goes, its content kept */
    Pos a = c->caret;
    if (sdt_typing(d, &a)) select_range(a, a);
  }
  return 0;
}
static void init(void)
{
  int i;
  char v[4];
  if (GetVar((STRPTR)"Quill/DeveloperTab", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0) devtab = v[0] == '1';
  for (i = 0; i < 4; i++) legacy_lab[i] = legacy_names[i];
  for (i = 0; i < 2; i++) group_lab[i] = group_names[i];
}
static void cleanup(void)
{
  x_free();
  if (p_have) { sdt_rec_free(&p_work); p_have = 0; }
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp16_package = {
  "WP16", ID_WP16_FIRST, ID_WP16_LAST,
  wp16_command, state, NULL, ctxmenu, key, init, cleanup,
  groups, 2,
  menus, (int)(sizeof menus / sizeof menus[0])
};
