/*
 * wp_refs.c - WP2 Captions, tables of figures, index, authorities, custom TOC (docs/wordcraft-gap.md). This file belongs to WP2: only that
 * package edits it. Its commands are ID_WP2_FIRST .. ID_WP2_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The model is core/refs.h. Here: the Caption window (labels, position, numbering with chapter
 * numbers, new and deleted labels, AutoCaption), Table of Figures, Custom Table of Contents, Index,
 * Mark Entry (Alt-Shift-X) with Mark All and AutoMark, Mark Citation (Alt-Shift-I) and Table of
 * Authorities windows, the Update commands, and Word's Cross-reference window (in place of Quill's
 * older one, main.c's ID_XREF comes here); the References tab's Captions, Index and Table of
 * Authorities groups and an Insert > References submenu.
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
#include "../core/refs.h"
#include "../core/field.h"
#include "../core/layout.h"
#include "../core/toc.h"
#include "../core/docx.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#define FLabel(l) QLabel(l, MUIO_Label_SingleFrame)

enum {
  ID_RF_XREF = ID_WP2_FIRST,          /* Cross-reference... (main.c's ID_XREF sends it here) */
  ID_RF_CUSTOMTOC,                    /* Custom Table of Contents... (the TOC list's last entry, main.c) */
  ID_RF_CAPTION, ID_RF_CAP_OK, ID_RF_CAP_CHANGE, ID_RF_CAP_LABEL, ID_RF_CAP_NEW, ID_RF_CAP_NEW_OK, ID_RF_CAP_DEL,
  ID_RF_AUTOCAP, ID_RF_AUTOCAP_OK, ID_RF_AUTOCAP_DO,
  ID_RF_TOF, ID_RF_TOF_OK, ID_RF_TOF_UPDATE,
  ID_RF_TOC_OK,
  ID_RF_XR_TYPE, ID_RF_XR_INSERT,
  ID_RF_MARK, ID_RF_MARK_ONE, ID_RF_MARK_ALL,
  ID_RF_INDEX, ID_RF_INDEX_OK, ID_RF_INDEX_UPDATE, ID_RF_AUTOMARK,
  ID_RF_CITE, ID_RF_CITE_ONE, ID_RF_CITE_ALL, ID_RF_CITE_NEXT, ID_RF_CITE_PICK, ID_RF_CITE_CAT,
  ID_RF_TOA, ID_RF_TOA_OK, ID_RF_TOA_UPDATE,
  ID_RF_UPDATE_HERE                   /* the right button in a table or index: brought up to date */
};
/* the package's state bits: the caret is in a table of contents, figures, authorities or an index */
enum { ST_INBLOCK = 1 };

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
static Layout *paged(void) { Layout *L = NULL; get(view(), MUIA_Quill_Layout, &L); return L; }
static void env(FieldEnv *e) { field_env(e); e->L = paged(); }
static void to_latin(const char *u, char *out, size_t cap) { utf8_to_latin1(u, strlen(u), out, cap); }
static void str_get(Object *o, char *utf, size_t cap)
{
  char *s = NULL;
  get(o, MUIA_String_Contents, &s);
  wp_latin1_utf8(s ? s : "", utf, cap);
}
static void str_set(Object *o, const char *utf)
{
  char lat[300];
  to_latin(utf, lat, sizeof lat);
  set(o, MUIA_String_Contents, (ULONG)lat);
}
static ULONG getv(Object *o, ULONG attr) { ULONG v = 0; if (o) get(o, attr, &v); return v; }
static void notify_id(Object *o, ULONG attr, ULONG id) { DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id); }
static void press_id(Object *o, ULONG id) { DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id); }
static void back(void) { set(wp_win(), MUIA_Window_Activate, TRUE); }
static Object *mklist(void)
{
  return ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
    MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End;
}
/* a window with Close (and whatever buttons are in contents) that stays open while it is used */
static Object *tool_window(const char *title, ULONG wid, Object *contents)
{
  Object *w, *close;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(title), MUIA_Window_ID, wid,
    WindowContents, VGroup, QDLG,
      Child, contents,
      Child, HGroup, Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
    End, End;
  if (!w) return NULL;
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  return w;
}
/* a cycle gadget whose entries change (MUI's are fixed once made): made again inside its own group */
static Object *new_cycle(const char **entries)
{
  return CycleObject, MUIA_Cycle_Entries, (ULONG)entries, MUIA_CycleChain, 1, End;
}
static void swap_cycle(Object *grp, Object **cyc, const char **entries, ULONG id, ULONG active)
{
  Object *n = new_cycle(entries);
  if (!n) return;
  DoMethod(grp, MUIM_Group_InitChange);
  if (*cyc) { DoMethod(grp, OM_REMMEMBER, (ULONG)*cyc); MUI_DisposeObject(*cyc); }
  DoMethod(grp, OM_ADDMEMBER, (ULONG)n);
  DoMethod(grp, MUIM_Group_ExitChange);
  *cyc = n;
  set(n, MUIA_Cycle_Active, active);
  if (id) notify_id(n, MUIA_Cycle_Active, id);
}
/* the caption labels as a cycle's entries (Latin-1, NULL ended; "(none)" first when withnone) */
static char lab_lat[REFS_MAXLAB + 1][40];
static const char *lab_ent[REFS_MAXLAB + 2];
static int lab_entries(int withnone)
{
  RefsInfo *ri = refs_info(doc());
  int i, n = 0;
  if (withnone) lab_ent[n++] = tr("(none)");
  for (i = 0; ri && i < ri->nlab; i++) {
    to_latin(ri->lab[i].name, lab_lat[i], sizeof lab_lat[i]);
    lab_ent[n++] = i < 3 ? tr(lab_lat[i]) : lab_lat[i];
  }
  lab_ent[n] = NULL;
  return n;
}
static const char *const leader_names[] = { "(none)", "........", "--------", "________", NULL };
static const char *leader_ent[5];
static const char *const fmt_names[] = { "From template", "Classic", "Distinctive", "Fancy", "Modern", "Formal", "Simple", NULL };
static const char *fmt_ent[8];
static const char *const lang_names[] = { "English", "German", "French", "Spanish", "Italian", NULL };
static const char *lang_ent[6];
static void tr_list(const char *const *src, const char **dst)
{
  int i;
  for (i = 0; src[i]; i++) dst[i] = tr(src[i]);
  dst[i] = NULL;
}
/* bring every table of a kind up to date: twice, so pages the tables themselves move are right */
static int update_kind(int kind)
{
  Doc *d = doc();
  Pos c = wp_caret() ? *wp_caret() : doc_start(d);
  int n;
  doc_begin(d, c);
  n = refs_update_kind(d, kind, paged());
  if (n && (kind == BK_INDEX || kind == BK_TOA)) refs_update_kind(d, kind, paged());
  doc_end(d, c);
  changed();
  if (!n) DisplayBeep(NULL);
  return n;
}
/* a block field with code: the one there of the kind (its code changed when different: the same arg
   for TOF), or a new one at the caret */
static void put_block(int kind, const char *code, const char *title, int leader, const char *tofarg)
{
  Doc *d = doc();
  Pos lo, hi, st, en, c;
  u32 from = 0;
  int found = -1;
  sel(&lo, &hi);
  c = lo;
  while (refs_block_find(d, kind, from, &st, &en) >= 0) {
    char a[64];
    const char *cd = d->fields[fmark_index(d->p[st.para]->t + st.off)].code;
    if (kind != BK_TOF) { found = 1; break; }
    if ((field_switch(cd, 'c', 0, a, sizeof a) || field_switch(cd, 'a', 0, a, sizeof a)) && tofarg && str_ieq(a, tofarg)) { found = 1; break; }
    from = en.para + 1;
  }
  doc_begin(d, c);
  if (found >= 0) {
    if (wp_ask("_Yes|_No", "Do you want to replace the selected %s?", tr(kind == BK_TOC ? "table of contents" : kind == BK_INDEX ? "index" : "table of figures")) != 1) { doc_end(d, c); return; }
    field_set_code(d, st, code);
    refs_block_update(d, st, NULL, paged(), leader);
    if (kind == BK_INDEX) { refs_block_find(d, kind, st.para, &st, &en); refs_block_update(d, st, NULL, paged(), leader); }
  } else {
    if (refs_block_insert(d, &c, code, title, paged(), leader) < 0) DisplayBeep(NULL);
    else if (kind == BK_INDEX && refs_block_find(d, kind, 0, &st, &en) >= 0) refs_block_update(d, st, NULL, paged(), leader);
  }
  doc_end(d, c);
  changed();
}

/* ------------------------------------------------------------------ */
/* Insert Caption */
static Object *cwin, *c_prev, *c_text, *c_labgrp, *c_lab, *c_pos, *c_excl, *c_fmt, *c_chap, *c_head, *c_sep;
static int c_busy;
static const char *pos_ent[3], *cfmt_ent[CN_N + 1], *head_ent[10], *sep_ent[CSEP_N + 1];
static const char *const cfmt_names[CN_N] = { "1, 2, 3, ...", "a, b, c, ...", "A, B, C, ...", "i, ii, iii, ...", "I, II, III, ..." };
static const char *const sep_names[CSEP_N] = { "- (hyphen)", ". (period)", ": (colon)", "(em dash)", "(en dash)" };
static char head_txt[9][16];
static int c_label(void) { return (int)getv(c_lab, MUIA_Cycle_Active); }
/* the gadgets show the label's settings */
static void c_show(void)
{
  RefsInfo *ri = refs_info(doc());
  int l = c_label();
  if (!ri || l >= ri->nlab) return;
  c_busy = 1;
  set(c_pos, MUIA_Cycle_Active, ri->lab[l].pos);
  set(c_excl, MUIA_Selected, ri->lab[l].nolabel);
  set(c_fmt, MUIA_Cycle_Active, ri->lab[l].fmt);
  set(c_chap, MUIA_Selected, ri->lab[l].chap);
  set(c_head, MUIA_Cycle_Active, ri->lab[l].heading ? ri->lab[l].heading - 1 : 0);
  set(c_sep, MUIA_Cycle_Active, ri->lab[l].sep);
  set(c_head, MUIA_Disabled, !ri->lab[l].chap);
  set(c_sep, MUIA_Disabled, !ri->lab[l].chap);
  c_busy = 0;
}
/* the label takes the gadgets' settings; the caption it would make shown */
static void c_take(void)
{
  RefsInfo *ri = refs_info(doc());
  int l = c_label();
  char pv[64], lat[64];
  Pos lo, hi;
  if (!ri || l >= ri->nlab) return;
  if (!c_busy) {
    CapLabel *cl = &ri->lab[l];
    cl->pos = (u8)getv(c_pos, MUIA_Cycle_Active);
    cl->nolabel = (u8)(getv(c_excl, MUIA_Selected) != 0);
    cl->fmt = (u8)getv(c_fmt, MUIA_Cycle_Active);
    cl->chap = (u8)(getv(c_chap, MUIA_Selected) != 0);
    cl->heading = (u8)(getv(c_head, MUIA_Cycle_Active) + 1);
    cl->sep = (u8)getv(c_sep, MUIA_Cycle_Active);
    set(c_head, MUIA_Disabled, !cl->chap);
    set(c_sep, MUIA_Disabled, !cl->chap);
  }
  sel(&lo, &hi);
  refs_caption_preview(doc(), lo, l, pv, sizeof pv);
  to_latin(pv, lat, sizeof lat);
  set(c_prev, MUIA_Text_Contents, (ULONG)lat);
}
static void c_labels(int active)
{
  lab_entries(0);
  swap_cycle(c_labgrp, &c_lab, lab_ent, ID_RF_CAP_LABEL, (ULONG)active);
}
static void c_open(void)
{
  Pos lo, hi;
  Doc *d = doc();
  int intbl, pic, i;
  if (!cwin) {
    Object *c, *newb, *delb, *autob;
    tr_list(cfmt_names, cfmt_ent);
    tr_list(sep_names, sep_ent);
    pos_ent[0] = tr("Below selected item"); pos_ent[1] = tr("Above selected item"); pos_ent[2] = NULL;
    for (i = 0; i < 9; i++) { sprintf(head_txt[i], "%s %d", tr("Heading"), i + 1); head_ent[i] = head_txt[i]; }
    head_ent[9] = NULL;
    c = VGroup,
      Child, FLabel(tr("Caption:")),
      Child, HGroup, Child, c_prev = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, TRUE, MUIA_HorizWeight, 30, End,
        Child, c_text = StringObject, StringFrame, MUIA_String_MaxLen, 200, MUIA_CycleChain, 1, End, End,
      Child, ColGroup(2),
        Child, FLabel(tr("Label:")), Child, c_labgrp = HGroup, End,
        Child, FLabel(tr("Position:")), Child, c_pos = CycleObject, MUIA_Cycle_Entries, (ULONG)pos_ent, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, c_excl = CheckMark(FALSE), Child, FLabel(tr("Exclude label from caption")), Child, HSpace(0), End,
      Child, HGroup, Child, newb = QButton(tr("_New Label...")), Child, delb = QButton(tr("_Delete Label")), Child, autob = QButton(tr("_AutoCaption...")), End,
      Child, VGroup, GroupFrameT(tr("Numbering")),
        Child, ColGroup(2),
          Child, FLabel(tr("Format:")), Child, c_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)cfmt_ent, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, c_chap = CheckMark(FALSE), Child, FLabel(tr("Include chapter number")), Child, HSpace(0), End,
        Child, ColGroup(2),
          Child, FLabel(tr("Chapter starts with style:")), Child, c_head = CycleObject, MUIA_Cycle_Entries, (ULONG)head_ent, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Use separator:")), Child, c_sep = CycleObject, MUIA_Cycle_Entries, (ULONG)sep_ent, MUIA_CycleChain, 1, End,
        End,
      End,
    End;
    if (!(cwin = wp_dialog("Caption", MAKE_ID('R','F','C','P'), c, "_OK", ID_RF_CAP_OK))) return;
    notify_id(c_pos, MUIA_Cycle_Active, ID_RF_CAP_CHANGE);
    notify_id(c_excl, MUIA_Selected, ID_RF_CAP_CHANGE);
    notify_id(c_fmt, MUIA_Cycle_Active, ID_RF_CAP_CHANGE);
    notify_id(c_chap, MUIA_Selected, ID_RF_CAP_CHANGE);
    notify_id(c_head, MUIA_Cycle_Active, ID_RF_CAP_CHANGE);
    notify_id(c_sep, MUIA_Cycle_Active, ID_RF_CAP_CHANGE);
    press_id(newb, ID_RF_CAP_NEW); press_id(delb, ID_RF_CAP_DEL); press_id(autob, ID_RF_AUTOCAP);
  }
  sel(&lo, &hi);
  /* a table or picture chosen: Table, Figure; the position only means something then */
  intbl = d->p[lo.para]->pf.cell.table != 0;
  pic = 0;
  { u32 o; for (o = 0; o + 4 <= d->p[lo.para]->len; o++) if (IS_PIC(d->p[lo.para]->t + o)) pic = 1; }
  c_labels(intbl ? 1 : 0);
  set(c_pos, MUIA_Disabled, !intbl && !pic);
  set(c_text, MUIA_String_Contents, (ULONG)"");
  c_show();
  c_take();
  set(cwin, MUIA_Window_Open, TRUE);
  set(cwin, MUIA_Window_ActiveObject, (ULONG)c_text);
}
static void c_ok(void)
{
  Doc *d = doc();
  Pos lo, hi;
  char text[400];
  FieldEnv e;
  int pos = (int)getv(c_pos, MUIA_Cycle_Active);
  if (getv(c_pos, MUIA_Disabled)) pos = -1;
  c_take();
  str_get(c_text, text, sizeof text);
  set(cwin, MUIA_Window_Open, FALSE);
  sel(&lo, &hi);
  env(&e);
  if (!refs_insert_caption(d, &lo, c_label(), text, pos, &e)) DisplayBeep(NULL);
  changed();
  select_range(lo, lo);
  back();
}
/* New Label: a name of its own */
static Object *nwin, *n_name;
static void new_label(void)
{
  if (!nwin) {
    Object *c = HGroup, Child, FLabel(tr("Label:")), Child, n_name = StringObject, StringFrame, MUIA_String_MaxLen, 38, MUIA_CycleChain, 1, End, End;
    if (!(nwin = wp_dialog("New Label", MAKE_ID('R','F','N','L'), c, "_OK", ID_RF_CAP_NEW_OK))) return;
  }
  set(n_name, MUIA_String_Contents, (ULONG)"");
  set(nwin, MUIA_Window_Open, TRUE);
  set(nwin, MUIA_Window_ActiveObject, (ULONG)n_name);
}
static void new_label_ok(void)
{
  char name[80];
  int k;
  str_get(n_name, name, sizeof name);
  set(nwin, MUIA_Window_Open, FALSE);
  if (!name[0] || (k = refs_add_label(doc(), name)) < 0) { DisplayBeep(NULL); return; }
  c_labels(k);
  c_show(); c_take();
  set(cwin, MUIA_Window_Activate, TRUE);
}

/* AutoCaption: a caption whenever a table or picture is put in */
static Object *awin, *a_tbl, *a_pic, *a_tlab, *a_plab;
static void autocap_open(void)
{
  RefsInfo *ri = refs_info(doc());
  int i;
  if (!awin) {
    Object *c = ColGroup(3),
      Child, a_tbl = CheckMark(FALSE), Child, FLabel(tr("Quill Table")), Child, a_tlab = StringObject, StringFrame, MUIA_String_MaxLen, 38, MUIA_CycleChain, 1, End,
      Child, a_pic = CheckMark(FALSE), Child, FLabel(tr("Picture")), Child, a_plab = StringObject, StringFrame, MUIA_String_MaxLen, 38, MUIA_CycleChain, 1, End,
    End;
    if (!(awin = wp_dialog("AutoCaption", MAKE_ID('R','F','A','C'), c, "_OK", ID_RF_AUTOCAP_OK))) return;
  }
  set(a_tbl, MUIA_Selected, FALSE); set(a_pic, MUIA_Selected, FALSE);
  str_set(a_tlab, "Table"); str_set(a_plab, "Figure");
  for (i = 0; ri && i < ri->nac; i++) {
    int t = strstr(ri->ac[i].name, "Table") != NULL;
    set(t ? a_tbl : a_pic, MUIA_Selected, ri->ac[i].on);
    str_set(t ? a_tlab : a_plab, ri->ac[i].label);
  }
  set(awin, MUIA_Window_Open, TRUE);
}
static void autocap_ok(void)
{
  RefsInfo *ri = refs_info(doc());
  char l[80];
  set(awin, MUIA_Window_Open, FALSE);
  if (!ri) return;
  ri->nac = 0;
  str_get(a_tlab, l, sizeof l);
  snprintf(ri->ac[0].name, sizeof ri->ac[0].name, "Microsoft Word Table");
  snprintf(ri->ac[0].label, sizeof ri->ac[0].label, "%s", l[0] ? l : "Table");
  ri->ac[0].on = (u8)(getv(a_tbl, MUIA_Selected) != 0);
  str_get(a_plab, l, sizeof l);
  snprintf(ri->ac[1].name, sizeof ri->ac[1].name, "Picture");
  snprintf(ri->ac[1].label, sizeof ri->ac[1].label, "%s", l[0] ? l : "Figure");
  ri->ac[1].on = (u8)(getv(a_pic, MUIA_Selected) != 0);
  ri->nac = 2;
  refs_add_label(doc(), ri->ac[0].label);
  refs_add_label(doc(), ri->ac[1].label);
}
/* (the state hook sees one more table or picture; the caption goes in after the view is done) */
static Doc *ac_doc;
static int ac_ntab, ac_nimg, ac_pending;
static void autocap_do(void)
{
  Doc *d = doc();
  Pos lo, hi;
  FieldEnv e;
  if (!ac_pending || !d) return;
  sel(&lo, &hi);
  env(&e);
  if (refs_autocaption(d, lo, ac_pending == 1, &e)) changed();
  ac_pending = 0;
}

/* ------------------------------------------------------------------ */
/* Table of Figures and the Custom Table of Contents */
static Object *fwin, *f_labgrp, *f_lab, *f_inc, *f_pages, *f_right, *f_lead, *f_fmt, *f_link, *f_usestyle, *f_style;
static void f_open(void)
{
  if (!fwin) {
    Object *c = VGroup,
      Child, ColGroup(2),
        Child, FLabel(tr("Caption label:")), Child, f_labgrp = HGroup, End,
        Child, FLabel(tr("Tab leader:")), Child, f_lead = CycleObject, MUIA_Cycle_Entries, (ULONG)leader_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Formats:")), Child, f_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmt_ent, MUIA_CycleChain, 1, End,
      End,
      Child, ColGroup(2),
        Child, f_inc = CheckMark(TRUE), Child, FLabel(tr("Include label and number")),
        Child, f_pages = CheckMark(TRUE), Child, FLabel(tr("Show page numbers")),
        Child, f_right = CheckMark(TRUE), Child, FLabel(tr("Right align page numbers")),
        Child, f_link = CheckMark(TRUE), Child, FLabel(tr("Use hyperlinks instead of page numbers")),
        Child, f_usestyle = CheckMark(FALSE), Child, FLabel(tr("Build from style:")),
      End,
      Child, f_style = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_String_Contents, (ULONG)"Caption", MUIA_CycleChain, 1, End,
    End;
    if (!(fwin = wp_dialog("Table of Figures", MAKE_ID('R','F','T','F'), c, "_OK", ID_RF_TOF_OK))) return;
    set(f_lead, MUIA_Cycle_Active, 1);
  }
  lab_entries(0);
  swap_cycle(f_labgrp, &f_lab, lab_ent, 0, 0);
  set(fwin, MUIA_Window_Open, TRUE);
}
static void f_ok(void)
{
  RefsInfo *ri = refs_info(doc());
  char code[300], id[48], style[80];
  int l = (int)getv(f_lab, MUIA_Cycle_Active), lead = (int)getv(f_lead, MUIA_Cycle_Active);
  size_t i, n;
  set(fwin, MUIA_Window_Open, FALSE);
  if (!ri || l >= ri->nlab) return;
  snprintf(id, sizeof id, "%s", ri->lab[l].name);
  for (i = 0; id[i]; i++) if (id[i] == ' ') id[i] = '_';
  str_get(f_style, style, sizeof style);
  if (getv(f_usestyle, MUIA_Selected) && style[0]) snprintf(code, sizeof code, " TOC %s\\z \\t \"%s,1\" ", getv(f_link, MUIA_Selected) ? "\\h " : "", style);
  else snprintf(code, sizeof code, " TOC %s\\z \\%c \"%s\" ", getv(f_link, MUIA_Selected) ? "\\h " : "", getv(f_inc, MUIA_Selected) ? 'c' : 'a', id);
  n = strlen(code);
  if (!getv(f_pages, MUIA_Selected)) snprintf(code + n, sizeof code - n, "\\n ");
  else if (!getv(f_right, MUIA_Selected)) snprintf(code + n, sizeof code - n, "\\p \" \" ");
  refs_apply_format(doc(), BK_TOF, (int)getv(f_fmt, MUIA_Cycle_Active));
  put_block(BK_TOF, code, NULL, lead, id);
  back();
}
static Object *twin, *t_pages, *t_right, *t_lead, *t_fmt, *t_levels, *t_link, *t_outline, *t_styles, *t_stylelist, *t_tc;
static const char *lev_ent[10];
static char lev_txt[9][4];
static void toc_open(void)
{
  int i;
  if (!twin) {
    Object *c;
    for (i = 0; i < 9; i++) { sprintf(lev_txt[i], "%d", i + 1); lev_ent[i] = lev_txt[i]; }
    lev_ent[9] = NULL;
    c = VGroup,
      Child, ColGroup(2),
        Child, t_pages = CheckMark(TRUE), Child, FLabel(tr("Show page numbers")),
        Child, t_right = CheckMark(TRUE), Child, FLabel(tr("Right align page numbers")),
        Child, t_link = CheckMark(TRUE), Child, FLabel(tr("Use hyperlinks instead of page numbers")),
      End,
      Child, ColGroup(2),
        Child, FLabel(tr("Tab leader:")), Child, t_lead = CycleObject, MUIA_Cycle_Entries, (ULONG)leader_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Formats:")), Child, t_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmt_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Show levels:")), Child, t_levels = CycleObject, MUIA_Cycle_Entries, (ULONG)lev_ent, MUIA_CycleChain, 1, End,
      End,
      Child, VGroup, GroupFrameT(tr("Build table of contents from:")),
        Child, ColGroup(2),
          Child, t_outline = CheckMark(TRUE), Child, FLabel(tr("Outline levels")),
          Child, t_styles = CheckMark(TRUE), Child, FLabel(tr("Styles (and the heading styles):")),
        End,
        Child, t_stylelist = StringObject, StringFrame, MUIA_String_MaxLen, 200, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, t_tc = CheckMark(FALSE), Child, FLabel(tr("Table entry fields")), Child, HSpace(0), End,
      End,
    End;
    if (!(twin = wp_dialog("Table of Contents", MAKE_ID('R','F','T','C'), c, "_OK", ID_RF_TOC_OK))) return;
    set(t_lead, MUIA_Cycle_Active, 1);
    set(t_levels, MUIA_Cycle_Active, 2);
  }
  {
    /* the table there: its code's settings shown */
    Doc *d = doc();
    Pos st, en;
    if (refs_block_find(d, BK_TOC, 0, &st, &en) >= 0) {
      const char *code = d->fields[fmark_index(d->p[st.para]->t + st.off)].code;
      char v[200];
      int lead = refs_block_leader(d, st, en);
      set(t_link, MUIA_Selected, field_switch(code, 'h', 0, NULL, 0));
      set(t_pages, MUIA_Selected, !field_switch(code, 'n', 0, NULL, 0));
      set(t_right, MUIA_Selected, !field_switch(code, 'p', 0, NULL, 0));
      set(t_outline, MUIA_Selected, field_switch(code, 'u', 0, NULL, 0));
      set(t_tc, MUIA_Selected, field_switch(code, 'f', 0, NULL, 0));
      if (field_switch(code, 'o', 0, v, sizeof v)) { const char *dash = strchr(v, '-'); int hi = dash ? atoi(dash + 1) : 9; set(t_levels, MUIA_Cycle_Active, hi >= 1 && hi <= 9 ? hi - 1 : 2); }
      if (field_switch(code, 't', 0, v, sizeof v)) str_set(t_stylelist, v); else set(t_stylelist, MUIA_String_Contents, (ULONG)"");
      if (lead >= 0) set(t_lead, MUIA_Cycle_Active, lead);
    }
  }
  set(twin, MUIA_Window_Open, TRUE);
}
static void toc_ok(void)
{
  char code[400], styles[220];
  size_t n;
  int lv = (int)getv(t_levels, MUIA_Cycle_Active) + 1;
  set(twin, MUIA_Window_Open, FALSE);
  str_get(t_stylelist, styles, sizeof styles);
  snprintf(code, sizeof code, " TOC ");
  n = strlen(code);
  if (getv(t_styles, MUIA_Selected)) { snprintf(code + n, sizeof code - n, "\\o \"1-%d\" ", lv); n = strlen(code); }
  if (getv(t_link, MUIA_Selected)) { snprintf(code + n, sizeof code - n, "\\h "); n = strlen(code); }
  snprintf(code + n, sizeof code - n, "\\z "); n = strlen(code);
  if (getv(t_tc, MUIA_Selected)) { snprintf(code + n, sizeof code - n, "\\f \\l \"1-%d\" ", lv); n = strlen(code); }
  if (!getv(t_pages, MUIA_Selected)) { snprintf(code + n, sizeof code - n, "\\n \"1-%d\" ", lv); n = strlen(code); }
  else if (!getv(t_right, MUIA_Selected)) { snprintf(code + n, sizeof code - n, "\\p \" \" "); n = strlen(code); }
  if (getv(t_styles, MUIA_Selected) && styles[0]) { snprintf(code + n, sizeof code - n, "\\t \"%s\" ", styles); n = strlen(code); }
  if (getv(t_outline, MUIA_Selected)) { snprintf(code + n, sizeof code - n, "\\u "); n = strlen(code); }
  refs_apply_format(doc(), BK_TOC, (int)getv(t_fmt, MUIA_Cycle_Active));
  put_block(BK_TOC, code, refs_word(refs_lang, RW_CONTENTS), (int)getv(t_lead, MUIA_Cycle_Active), NULL);
  back();
}

/* ------------------------------------------------------------------ */
/* Cross-reference */
#define XR_MAX 200
static Object *xwin, *x_typegrp, *x_type, *x_kindgrp, *x_kind, *x_list, *x_link, *x_above, *x_sep;
static const char *xtype_ent[6 + REFS_MAXLAB];
static const char *xkind_ent[12];
static int x_kinds[12], x_nk, x_types;
static RefTarget x_t[XR_MAX];
static int x_n;
static const char *const kind_names[] = { "Text", "Page number", "Paragraph number", "Paragraph number (no context)", "Paragraph number (full context)",
  "Above/below", "Note number", "Note number (formatted)", "Entire caption", "Only label and number", "Only caption text" };
/* the kinds' names as Word words them for the type */
static const char *kind_name(int type, int k)
{
  if (k == XK_TEXT) return tr(type == RT_HEADING ? "Heading text" : type == RT_BOOKMARK ? "Bookmark text" : "Paragraph text");
  if (type == RT_HEADING && (k == XK_NUMBER || k == XK_NUMNOCTX || k == XK_NUMFULL))
    return tr(k == XK_NUMBER ? "Heading number" : k == XK_NUMNOCTX ? "Heading number (no context)" : "Heading number (full context)");
  if (k == XK_NOTENUM) return tr(type == RT_ENDNOTE ? "Endnote number" : "Footnote number");
  if (k == XK_NOTEFMT) return tr(type == RT_ENDNOTE ? "Endnote number (formatted)" : "Footnote number (formatted)");
  return tr(kind_names[k]);
}
static void x_fill(void)
{
  int type = (int)getv(x_type, MUIA_Cycle_Active), i;
  if (type >= 5) type = RT_LABEL + (type - 5);
  x_nk = refs_kinds(type, x_kinds);
  for (i = 0; i < x_nk; i++) xkind_ent[i] = kind_name(type, x_kinds[i]);
  xkind_ent[x_nk] = NULL;
  swap_cycle(x_kindgrp, &x_kind, xkind_ent, 0, 0);
  x_n = refs_targets(doc(), type, x_t, XR_MAX);
  set(x_list, MUIA_List_Quiet, TRUE);
  DoMethod(x_list, MUIM_List_Clear);
  for (i = 0; i < x_n; i++) {
    char lat[120], line[130];
    to_latin(x_t[i].text, lat, sizeof lat);
    sprintf(line, "%*s%s", type == RT_HEADING && x_t[i].level > 1 ? (x_t[i].level - 1) * 2 : 0, "", lat);
    DoMethod(x_list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  set(x_list, MUIA_List_Quiet, FALSE);
  set(x_sep, MUIA_Disabled, type != RT_NUMBERED && type != RT_HEADING && type != RT_BOOKMARK);
}
static void x_open(void)
{
  int i, n;
  if (!xwin) {
    Object *c, *ins;
    c = VGroup,
      Child, ColGroup(2),
        Child, FLabel(tr("Reference type:")), Child, x_typegrp = HGroup, End,
        Child, FLabel(tr("Insert reference to:")), Child, x_kindgrp = HGroup, End,
      End,
      Child, ColGroup(4),
        Child, x_link = CheckMark(TRUE), Child, FLabel(tr("Insert as hyperlink")),
        Child, x_above = CheckMark(FALSE), Child, FLabel(tr("Include above/below")),
      End,
      Child, HGroup, Child, FLabel(tr("Separate numbers with")), Child, x_sep = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_CycleChain, 1, End, End,
      Child, FLabel(tr("For which:")),
      Child, x_list = mklist(),
      Child, HGroup, Child, ins = QButton(tr("_Insert")), Child, HSpace(0), End,
    End;
    if (!(xwin = tool_window("Cross-reference", MAKE_ID('R','F','X','R'), c))) return;
    press_id(ins, ID_RF_XR_INSERT);
    DoMethod(x_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RF_XR_INSERT);
  }
  xtype_ent[0] = tr("Numbered item"); xtype_ent[1] = tr("Heading"); xtype_ent[2] = tr("Bookmark"); xtype_ent[3] = tr("Footnote"); xtype_ent[4] = tr("Endnote");
  n = lab_entries(0);
  for (i = 0; i < n; i++) xtype_ent[5 + i] = lab_ent[i];
  xtype_ent[5 + n] = NULL;
  x_types = 5 + n;
  swap_cycle(x_typegrp, &x_type, xtype_ent, ID_RF_XR_TYPE, getv(x_type, MUIA_Cycle_Active) < (ULONG)x_types ? getv(x_type, MUIA_Cycle_Active) : 1);
  x_fill();
  set(xwin, MUIA_Window_Open, TRUE);
}
static void x_insert(void)
{
  Doc *d = doc();
  LONG a = -1;
  int type = (int)getv(x_type, MUIA_Cycle_Active), kind = (int)getv(x_kind, MUIA_Cycle_Active);
  Pos lo, hi;
  char sep[24];
  FieldEnv e;
  get(x_list, MUIA_List_Active, &a);
  if (a < 0 || a >= x_n || kind >= x_nk) { DisplayBeep(NULL); return; }
  if (type >= 5) type = RT_LABEL + (type - 5);
  str_get(x_sep, sep, sizeof sep);
  sel(&lo, &hi);
  doc_begin(d, lo);
  if (pos_cmp(lo, hi)) doc_delete(d, lo, hi);
  env(&e);
  if (refs_insert_xref(d, &lo, type, &x_t[a], x_kinds[kind], getv(x_link, MUIA_Selected) != 0, getv(x_above, MUIA_Selected) != 0, sep, &e) < 0)
    wp_ask("OK", "%s", tr("The reference can't be made: another bookmark covers part of that text."));
  doc_end(d, lo);
  changed();
  select_range(lo, lo);
}

/* ------------------------------------------------------------------ */
/* Mark Entry, Index, AutoMark */
static Object *mwin, *m_main, *m_sub, *m_opt, *m_xref, *m_bm, *m_bold, *m_ital;
static const char *opt_ent[4];
static void sel_text(char *out, size_t cap)
{
  Pos lo, hi;
  out[0] = 0;
  sel(&lo, &hi);
  if (pos_cmp(lo, hi) && lo.para == hi.para) refs_plain(doc(), lo.para, lo.off, hi.off, out, cap);
}
static void m_open(void)
{
  char t[200];
  if (!mwin) {
    Object *c, *one, *all;
    opt_ent[0] = tr("Current page"); opt_ent[1] = tr("Cross-reference:"); opt_ent[2] = tr("Page range"); opt_ent[3] = NULL;
    c = VGroup,
      Child, ColGroup(2),
        Child, FLabel(tr("Main entry:")), Child, m_main = StringObject, StringFrame, MUIA_String_MaxLen, 200, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Subentry:")), Child, m_sub = StringObject, StringFrame, MUIA_String_MaxLen, 200, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Options:")), Child, m_opt = CycleObject, MUIA_Cycle_Entries, (ULONG)opt_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Cross-reference:")), Child, m_xref = StringObject, StringFrame, MUIA_String_MaxLen, 150, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Bookmark:")), Child, m_bm = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, FLabel(tr("Page number format:")), Child, m_bold = CheckMark(FALSE), Child, FLabel(tr("Bold")),
        Child, m_ital = CheckMark(FALSE), Child, FLabel(tr("Italic")), Child, HSpace(0), End,
      Child, HGroup, Child, one = QButton(tr("_Mark")), Child, all = QButton(tr("Mark _All")), Child, HSpace(0), End,
    End;
    if (!(mwin = tool_window("Mark Index Entry", MAKE_ID('R','F','M','E'), c))) return;
    press_id(one, ID_RF_MARK_ONE); press_id(all, ID_RF_MARK_ALL);
  }
  sel_text(t, sizeof t);
  str_set(m_main, t);
  set(m_sub, MUIA_String_Contents, (ULONG)"");
  {
    char see[64];
    snprintf(see, sizeof see, "%s ", refs_word(refs_lang, RW_SEE));
    str_set(m_xref, see);
  }
  set(mwin, MUIA_Window_Open, TRUE);
  set(mwin, MUIA_Window_ActiveObject, (ULONG)m_main);
}
static void m_mark(int all)
{
  Doc *d = doc();
  char mn[300], sb[300], xr[200], bm[80], what[300];
  int opt = (int)getv(m_opt, MUIA_Cycle_Active), n;
  Pos lo, hi;
  str_get(m_main, mn, sizeof mn); str_get(m_sub, sb, sizeof sb); str_get(m_xref, xr, sizeof xr); str_get(m_bm, bm, sizeof bm);
  if (!mn[0]) { DisplayBeep(NULL); return; }
  if (opt != 1) xr[0] = 0;
  if (opt != 2) bm[0] = 0;
  else if (!doc_bmark_find(d, bm, NULL, NULL)) { wp_ask("OK", "%s", tr("There is no bookmark of that name.")); return; }
  sel(&lo, &hi);
  if (all) {
    sel_text(what, sizeof what);
    if (!what[0]) snprintf(what, sizeof what, "%s", mn);
    n = refs_mark_all(d, what, mn, sb, xr, bm, getv(m_bold, MUIA_Selected) != 0, getv(m_ital, MUIA_Selected) != 0);
    if (!n) DisplayBeep(NULL);
  } else {
    Pos at = hi;
    if (refs_mark_entry(d, &at, mn, sb, xr, bm, getv(m_bold, MUIA_Selected) != 0, getv(m_ital, MUIA_Selected) != 0) < 0) DisplayBeep(NULL);
  }
  changed();
}
static Object *iwin, *i_type, *i_cols, *i_lang, *i_right, *i_lead, *i_fmt;
static const char *type_ent[3], *cols_ent[6];
static const char *const ifmt_names[] = { "From template", "Classic", "Fancy", "Modern", "Bulleted", "Formal", "Simple", NULL };
static const char *ifmt_ent[8];
static void i_open(void)
{
  if (!iwin) {
    Object *c, *mk, *am;
    type_ent[0] = tr("Indented"); type_ent[1] = tr("Run-in"); type_ent[2] = NULL;
    cols_ent[0] = tr("Auto"); cols_ent[1] = "1"; cols_ent[2] = "2"; cols_ent[3] = "3"; cols_ent[4] = "4"; cols_ent[5] = NULL;
    tr_list(ifmt_names, ifmt_ent);
    c = VGroup,
      Child, ColGroup(2),
        Child, FLabel(tr("Type:")), Child, i_type = CycleObject, MUIA_Cycle_Entries, (ULONG)type_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Columns:")), Child, i_cols = CycleObject, MUIA_Cycle_Entries, (ULONG)cols_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Language:")), Child, i_lang = CycleObject, MUIA_Cycle_Entries, (ULONG)lang_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Tab leader:")), Child, i_lead = CycleObject, MUIA_Cycle_Entries, (ULONG)leader_ent, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("Formats:")), Child, i_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)ifmt_ent, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, i_right = CheckMark(TRUE), Child, FLabel(tr("Right align page numbers")), Child, HSpace(0), End,
      Child, HGroup, Child, mk = QButton(tr("Mar_k Entry...")), Child, am = QButton(tr("A_utoMark...")), End,
    End;
    if (!(iwin = wp_dialog("Index", MAKE_ID('R','F','I','X'), c, "_OK", ID_RF_INDEX_OK))) return;
    press_id(mk, ID_RF_MARK); press_id(am, ID_RF_AUTOMARK);
    set(i_cols, MUIA_Cycle_Active, 2);
    set(i_lead, MUIA_Cycle_Active, 1);
    set(i_fmt, MUIA_Cycle_Active, 1);
  }
  set(i_lang, MUIA_Cycle_Active, refs_lang);
  set(iwin, MUIA_Window_Open, TRUE);
}
static void i_ok(void)
{
  char code[300];
  size_t n;
  int cols = (int)getv(i_cols, MUIA_Cycle_Active), fmt = (int)getv(i_fmt, MUIA_Cycle_Active), lang = (int)getv(i_lang, MUIA_Cycle_Active);
  set(iwin, MUIA_Window_Open, FALSE);
  if (cols == 0) cols = 2;
  snprintf(code, sizeof code, " INDEX ");
  n = strlen(code);
  if (getv(i_type, MUIA_Cycle_Active)) { snprintf(code + n, sizeof code - n, "\\r "); n = strlen(code); }
  if (getv(i_right, MUIA_Selected) && !getv(i_type, MUIA_Cycle_Active)) { snprintf(code + n, sizeof code - n, "\\e \"\t\" "); n = strlen(code); }
  if (fmt == 4) { snprintf(code + n, sizeof code - n, "\\h \"\" "); n = strlen(code); }
  else if (fmt != 0 && fmt != 6) { snprintf(code + n, sizeof code - n, "\\h \"A\" "); n = strlen(code); }
  if (cols > 1) { snprintf(code + n, sizeof code - n, "\\c \"%d\" ", cols); n = strlen(code); }
  snprintf(code + n, sizeof code - n, "\\z \"%d\" ", refs_lang_lcid(lang));
  refs_apply_format(doc(), BK_INDEX, fmt == 4 ? RF_SIMPLE : fmt == 2 ? RF_FANCY : fmt == 5 ? RF_FORMAL : fmt == 3 ? RF_MODERN : fmt == 1 ? RF_CLASSIC : RF_TEMPLATE);
  put_block(BK_INDEX, code, NULL, (int)getv(i_lead, MUIA_Cycle_Active), NULL);
  back();
}
static void automark(void)
{
  static char path[512];
  Doc *c;
  unsigned lost;
  char err[200];
  int n;
  if (!wp_file_request(0, "Open Index AutoMark File", path, sizeof path, "#?.(docx|txt|text)")) return;
  if (!(c = doc_load_file(path, &lost, err, sizeof err))) { wp_ask("OK", "%s", err); return; }
  n = refs_automark(doc(), c);
  doc_free(c);
  changed();
  {
    char msg[80];
    sprintf(msg, "%d", n);
    wp_ask("OK", tr("Entries marked: %s"), msg);
  }
}

/* ------------------------------------------------------------------ */
/* Mark Citation, Table of Authorities */
static Object *kwin, *k_long, *k_short, *k_catgrp, *k_cat, *k_list, *k_catname;
static const char *cat_ent[REFS_NCAT + 2];
static char cat_lat[REFS_NCAT][40];
static char **k_cits;
static int k_ncit;
static int cat_entries(int withall)
{
  int i, n = 0;
  if (withall) cat_ent[n++] = tr("All");
  for (i = 0; i < REFS_NCAT; i++) { to_latin(refs_category(doc(), i + 1), cat_lat[i], sizeof cat_lat[i]); cat_ent[n++] = cat_lat[i]; }
  cat_ent[n] = NULL;
  return n;
}
static void k_fill(void)
{
  int i;
  for (i = 0; i < k_ncit; i++) free(k_cits[i]);
  free(k_cits);
  k_cits = refs_citations(doc(), &k_ncit);
  set(k_list, MUIA_List_Quiet, TRUE);
  DoMethod(k_list, MUIM_List_Clear);
  for (i = 0; i < k_ncit; i++) { char lat[160]; to_latin(k_cits[i], lat, sizeof lat); DoMethod(k_list, MUIM_List_InsertSingle, (ULONG)lat, MUIV_List_Insert_Bottom); }
  set(k_list, MUIA_List_Quiet, FALSE);
}
static void k_open(void)
{
  char t[300];
  if (!kwin) {
    Object *c, *one, *all, *next, *ren;
    c = VGroup,
      Child, FLabel(tr("Selected text (the long citation):")),
      Child, k_long = StringObject, StringFrame, MUIA_String_MaxLen, 300, MUIA_CycleChain, 1, End,
      Child, ColGroup(2),
        Child, FLabel(tr("Category:")), Child, k_catgrp = HGroup, End,
        Child, FLabel(tr("Short citation:")), Child, k_short = StringObject, StringFrame, MUIA_String_MaxLen, 200, MUIA_CycleChain, 1, End,
      End,
      Child, FLabel(tr("Long citation:")),
      Child, k_list = mklist(),
      Child, HGroup, Child, one = QButton(tr("_Mark")), Child, all = QButton(tr("Mark _All")), Child, next = QButton(tr("_Next Citation")), End,
      Child, HGroup, Child, FLabel(tr("Category name:")), Child, k_catname = StringObject, StringFrame, MUIA_String_MaxLen, 38, MUIA_CycleChain, 1, End,
        Child, ren = QButton(tr("_Replace")), End,
    End;
    if (!(kwin = tool_window("Mark Citation", MAKE_ID('R','F','M','C'), c))) return;
    press_id(one, ID_RF_CITE_ONE); press_id(all, ID_RF_CITE_ALL); press_id(next, ID_RF_CITE_NEXT); press_id(ren, ID_RF_CITE_CAT);
    notify_id(k_list, MUIA_List_Active, ID_RF_CITE_PICK);
  }
  cat_entries(0);
  swap_cycle(k_catgrp, &k_cat, cat_ent, 0, getv(k_cat, MUIA_Cycle_Active));
  sel_text(t, sizeof t);
  str_set(k_long, t);
  str_set(k_short, t);
  k_fill();
  set(kwin, MUIA_Window_Open, TRUE);
  set(kwin, MUIA_Window_ActiveObject, (ULONG)k_long);
}
static void k_mark(int all)
{
  Doc *d = doc();
  char l[400], s[300];
  int cat = (int)getv(k_cat, MUIA_Cycle_Active) + 1, i, known = 0;
  Pos lo, hi;
  str_get(k_long, l, sizeof l); str_get(k_short, s, sizeof s);
  if (!l[0] && !s[0]) { DisplayBeep(NULL); return; }
  for (i = 0; i < k_ncit; i++) if (!strcmp(k_cits[i], l)) known = 1;
  sel(&lo, &hi);
  if (all) { if (!refs_mark_all_citations(d, l, s, cat)) DisplayBeep(NULL); }
  else { Pos at = hi; refs_mark_citation(d, &at, known ? "" : l, s[0] ? s : l, cat); }
  changed();
  k_fill();
}
static void k_next(void)
{
  Pos lo, hi, a, b;
  sel(&lo, &hi);
  if (!refs_next_citation(doc(), hi, &a, &b)) { DisplayBeep(NULL); return; }
  select_range(a, b);
  {
    char t[300];
    refs_plain(doc(), a.para, a.off, b.off, t, sizeof t);
    str_set(k_long, t);
  }
}
static Object *owin, *o_catgrp, *o_cat, *o_passim, *o_keep, *o_lead;
static void o_open(void)
{
  if (!owin) {
    Object *c, *mk;
    c = VGroup,
      Child, ColGroup(2),
        Child, FLabel(tr("Category:")), Child, o_catgrp = HGroup, End,
        Child, FLabel(tr("Tab leader:")), Child, o_lead = CycleObject, MUIA_Cycle_Entries, (ULONG)leader_ent, MUIA_CycleChain, 1, End,
      End,
      Child, ColGroup(2),
        Child, o_passim = CheckMark(TRUE), Child, FLabel(tr("Use passim")),
        Child, o_keep = CheckMark(TRUE), Child, FLabel(tr("Keep original formatting")),
      End,
      Child, HGroup, Child, mk = QButton(tr("Mar_k Citation...")), Child, HSpace(0), End,
    End;
    if (!(owin = wp_dialog("Table of Authorities", MAKE_ID('R','F','T','A'), c, "_OK", ID_RF_TOA_OK))) return;
    press_id(mk, ID_RF_CITE);
    set(o_lead, MUIA_Cycle_Active, 1);
  }
  cat_entries(1);
  swap_cycle(o_catgrp, &o_cat, cat_ent, 0, 0);
  set(owin, MUIA_Window_Open, TRUE);
}
static void o_ok(void)
{
  Doc *d = doc();
  int cat = (int)getv(o_cat, MUIA_Cycle_Active), c, lead = (int)getv(o_lead, MUIA_Cycle_Active);
  char code[100];
  Pos lo, hi, at;
  set(owin, MUIA_Window_Open, FALSE);
  sel(&lo, &hi);
  at = lo;
  doc_begin(d, lo);
  for (c = cat ? cat : 1; c <= (cat ? cat : REFS_NCAT); c++) {
    if (!cat) {
      /* All: a table for each category that has citations */
      int any = 0, k;
      u32 pi, o;
      for (pi = 0; pi < d->np && !any; pi++) for (o = 0; o + 4 <= d->p[pi]->len && !any; o++) {
        char nm[8], cs[8];
        if (!IS_FSTART(d->p[pi]->t + o)) continue;
        k = fmark_index(d->p[pi]->t + o);
        field_name(d->fields[k].code, nm, sizeof nm);
        if (!strcmp(nm, "TA") && field_switch(d->fields[k].code, 'l', 0, NULL, 0) && (field_switch(d->fields[k].code, 'c', 0, cs, sizeof cs) ? atoi(cs) : 1) == c) any = 1;
      }
      if (!any) continue;
    }
    snprintf(code, sizeof code, " TOA \\h \\c \"%d\" %s%s", c, getv(o_passim, MUIA_Selected) ? "\\p " : "", getv(o_keep, MUIA_Selected) ? "" : "\\f ");
    if (refs_block_insert(d, &at, code, NULL, paged(), lead) < 0) break;
    at.para = at.para + 1 < d->np ? at.para + 1 : at.para; at.off = 0;
  }
  doc_end(d, lo);
  update_kind(BK_TOA);
  back();
}

/* ------------------------------------------------------------------ */
/* commands */
int wp2_command(ULONG id, LONG value)
{
  Doc *d = doc();
  if (!d) return WPR_DONE;
  if (d->edit.kind && id != ID_RF_AUTOCAP_DO) wp_story_close();
  switch (id) {
  case ID_RF_XREF: x_open(); return WPR_KEEPFOCUS;
  case ID_RF_XR_TYPE: if (xwin) x_fill(); return WPR_KEEPFOCUS;
  case ID_RF_XR_INSERT: if (xwin) x_insert(); return WPR_KEEPFOCUS;
  case ID_RF_CUSTOMTOC: toc_open(); return WPR_KEEPFOCUS;
  case ID_RF_TOC_OK: if (twin) toc_ok(); return WPR_DONE;
  case ID_RF_CAPTION: c_open(); return WPR_KEEPFOCUS;
  case ID_RF_CAP_LABEL: if (cwin) { c_show(); c_take(); } return WPR_KEEPFOCUS;
  case ID_RF_CAP_CHANGE: if (cwin) c_take(); return WPR_KEEPFOCUS;
  case ID_RF_CAP_OK: if (cwin) c_ok(); return WPR_DONE;
  case ID_RF_CAP_NEW: new_label(); return WPR_KEEPFOCUS;
  case ID_RF_CAP_NEW_OK: if (nwin) new_label_ok(); return WPR_KEEPFOCUS;
  case ID_RF_CAP_DEL: {
    RefsInfo *ri = refs_info(d);
    int l = c_label();
    if (!ri || l < 3 || l >= ri->nlab || !refs_delete_label(d, ri->lab[l].name)) { DisplayBeep(NULL); return WPR_KEEPFOCUS; }
    c_labels(0); c_show(); c_take();
    return WPR_KEEPFOCUS; }
  case ID_RF_AUTOCAP: autocap_open(); return WPR_KEEPFOCUS;
  case ID_RF_AUTOCAP_OK: if (awin) autocap_ok(); return WPR_KEEPFOCUS;
  case ID_RF_AUTOCAP_DO: autocap_do(); return WPR_DONE;
  case ID_RF_TOF: f_open(); return WPR_KEEPFOCUS;
  case ID_RF_TOF_OK: if (fwin) f_ok(); return WPR_DONE;
  case ID_RF_TOF_UPDATE: update_kind(BK_TOF); return WPR_DONE;
  case ID_RF_MARK: m_open(); return WPR_KEEPFOCUS;
  case ID_RF_MARK_ONE: if (mwin) m_mark(0); return WPR_KEEPFOCUS;
  case ID_RF_MARK_ALL: if (mwin) m_mark(1); return WPR_KEEPFOCUS;
  case ID_RF_INDEX: i_open(); return WPR_KEEPFOCUS;
  case ID_RF_INDEX_OK: if (iwin) i_ok(); return WPR_DONE;
  case ID_RF_INDEX_UPDATE: update_kind(BK_INDEX); return WPR_DONE;
  case ID_RF_AUTOMARK: automark(); return WPR_DONE;
  case ID_RF_CITE: k_open(); return WPR_KEEPFOCUS;
  case ID_RF_CITE_ONE: if (kwin) k_mark(0); return WPR_KEEPFOCUS;
  case ID_RF_CITE_ALL: if (kwin) k_mark(1); return WPR_KEEPFOCUS;
  case ID_RF_CITE_NEXT: if (kwin) k_next(); return WPR_KEEPFOCUS;
  case ID_RF_CITE_PICK: {
    LONG a = -1;
    get(k_list, MUIA_List_Active, &a);
    if (a >= 0 && a < k_ncit) str_set(k_long, k_cits[a]);
    return WPR_KEEPFOCUS; }
  case ID_RF_CITE_CAT: {
    RefsInfo *ri = refs_info(d);
    int c = (int)getv(k_cat, MUIA_Cycle_Active);
    char nm[80];
    str_get(k_catname, nm, sizeof nm);
    if (!ri || c < 0 || c >= REFS_NCAT) return WPR_KEEPFOCUS;
    snprintf(ri->cat[c], sizeof ri->cat[c], "%s", nm);
    cat_entries(0);
    swap_cycle(k_catgrp, &k_cat, cat_ent, 0, (ULONG)c);
    return WPR_KEEPFOCUS; }
  case ID_RF_TOA: o_open(); return WPR_KEEPFOCUS;
  case ID_RF_TOA_OK: if (owin) o_ok(); return WPR_DONE;
  case ID_RF_TOA_UPDATE: update_kind(BK_TOA); return WPR_DONE;
  case ID_RF_UPDATE_HERE: {
    Pos lo, hi, st, en;
    u32 from = 0;
    sel(&lo, &hi);
    while (refs_block_find(d, BK_NONE, from, &st, &en) >= 0) {
      if (pos_cmp(st, lo) <= 0 && pos_cmp(lo, en) <= 0) {
        doc_begin(d, lo);
        refs_block_update(d, st, &en, paged(), -1);
        refs_block_find(d, BK_NONE, st.para, &st, &en);
        refs_block_update(d, st, &en, paged(), -1);
        doc_end(d, lo);
        changed();
        return WPR_DONE;
      }
      from = en.para + 1;
    }
    DisplayBeep(NULL);
    return WPR_DONE; }
  }

  return WPR_DONE;
}

/* ------------------------------------------------------------------ */
/* the ribbon, menus, state, right button, keys */
#define ICON_CAPTION "ci R 2 1 14 9 ca F 4 3 7 6 L 8 7 12 4 ct L 2 12 14 12 L 2 14 10 14"
#define ICON_TOF "ci R 1 1 6 5 ca F 2 2 5 4 ct L 8 3 15 3 ci R 1 9 6 13 ca F 2 10 5 12 ct L 8 11 15 11"
#define ICON_XREF "ci L 2 3 9 3 L 2 6 7 6 ca P 5 9 9 13 9 13 13 11 15 11 13 L 13 9 9 13"
#define ICON_MARK "ci L 2 3 14 3 L 2 6 14 6 L 2 9 8 9 ca F 10 8 14 14 cw L 12 9 12 13"
#define ICON_INDEX "ci B A ca L 10 4 15 4 L 10 8 15 8 L 10 12 15 12"
#define ICON_AUTOMARK "ci R 1 2 9 14 L 3 5 7 5 L 3 8 7 8 ca P 3 11 8 14 11 11 8 L 11 8 15 4"
#define ICON_CITE "ci P 5 3 2 7 2 7 6 5 6 3 9 P 5 9 2 13 2 13 6 11 6 9 9 ca L 2 12 14 12 L 2 14 14 14"
#define ICON_TOA "ci L 2 3 9 3 L 2 7 9 7 L 2 11 9 11 ca L 11 3 14 3 L 11 7 14 7 L 11 11 14 11 ct L 2 14 14 14"
static const Item cap_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Insert Caption", ID_RF_CAPTION, 0, 0, 0, 0, "A numbered caption for a picture, table or equation", NULL, ICON_CAPTION },
  { K_BUTTON, S_SMALL, 0, 0, "Table of Figures", ID_RF_TOF, 0, 0, F_NEWCOL, 0, "A list of the captions of one label with their pages", NULL, ICON_TOF },
  { K_BUTTON, S_SMALL, IC_UPDATE, 0, "Update Table", ID_RF_TOF_UPDATE, 0, 0, 0, 0, "Bring the tables of figures up to date" },
  { K_BUTTON, S_SMALL, 0, 0, "Cross-reference", ID_RF_XREF, 0, 0, 0, 0, "A reference to a heading, caption, note, numbered paragraph or bookmark", NULL, ICON_XREF } };
static const Item idx_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Mark Entry", ID_RF_MARK, 0, 0, 0, 0, "The selected text marked for the index (Alt-Shift-X)", NULL, ICON_MARK },
  { K_BUTTON, S_SMALL, 0, 0, "Insert Index", ID_RF_INDEX, 0, 0, F_NEWCOL, 0, "An index of the marked entries with their pages", NULL, ICON_INDEX },
  { K_BUTTON, S_SMALL, IC_UPDATE, 0, "Update Index", ID_RF_INDEX_UPDATE, 0, 0, 0, 0, "Bring the index up to date" },
  { K_BUTTON, S_SMALL, 0, 0, "AutoMark", ID_RF_AUTOMARK, 0, 0, 0, 0, "Mark entries from a concordance file", NULL, ICON_AUTOMARK } };
static const Item toa_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Mark Citation", ID_RF_CITE, 0, 0, 0, 0, "The selected text marked as a citation (Alt-Shift-I)", NULL, ICON_CITE },
  { K_BUTTON, S_SMALL, 0, 0, "Insert Table of Authorities", ID_RF_TOA, 0, 0, F_NEWCOL, 0, "A table of the cases, statutes and other authorities cited", NULL, ICON_TOA },
  { K_BUTTON, S_SMALL, IC_UPDATE, 0, "Update Table", ID_RF_TOA_UPDATE, 0, 0, 0, 0, "Bring the tables of authorities up to date" } };
static const WpGroup groups[] = {
  { TAB_REFERENCES, -1, { "Captions", G_COLS, cap_items, 4 } },
  { TAB_REFERENCES, -1, { "Index", G_COLS, idx_items, 4 } },
  { TAB_REFERENCES, -1, { "Table of Authorities", G_COLS, toa_items, 3 } } };
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"References", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Caption...", 0, 0, 0, (APTR)ID_RF_CAPTION } },
  { WM_INSERT, { NM_SUB, (STRPTR)"AutoCaption...", 0, 0, 0, (APTR)ID_RF_AUTOCAP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Table of Figures...", 0, 0, 0, (APTR)ID_RF_TOF } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Update Table of Figures", 0, 0, 0, (APTR)ID_RF_TOF_UPDATE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Custom Table of Contents...", 0, 0, 0, (APTR)ID_RF_CUSTOMTOC } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Mark Entry...", 0, 0, 0, (APTR)ID_RF_MARK } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Index...", 0, 0, 0, (APTR)ID_RF_INDEX } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Update Index", 0, 0, 0, (APTR)ID_RF_INDEX_UPDATE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"AutoMark...", 0, 0, 0, (APTR)ID_RF_AUTOMARK } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Mark Citation...", 0, 0, 0, (APTR)ID_RF_CITE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Table of Authorities...", 0, 0, 0, (APTR)ID_RF_TOA } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Update Table of Authorities", 0, 0, 0, (APTR)ID_RF_TOA_UPDATE } } };

static int in_block(const WpCtx *c)
{
  Pos st, en;
  u32 from = 0;
  if (!c->doc || !c->doc->nfields) return 0;
  while (refs_block_find(c->doc, BK_NONE, from, &st, &en) >= 0) {
    if (pos_cmp(st, c->caret) <= 0 && pos_cmp(c->caret, en) <= 0) return 1;
    if (st.para > c->caret.para) return 0;
    from = en.para + 1;
  }
  return 0;
}
static void state(ULONG *on, const WpCtx *c)
{
  Doc *d = c->doc;
  if (!d) return;
  if (in_block(c)) *on |= 1UL << ST_INBLOCK;
  /* AutoCaption: one more table or picture than last time, at the caret */
  if (d == ac_doc && d->refs && !ac_pending) {
    int i, want = 0;
    for (i = 0; i < d->refs->nac; i++) if (d->refs->ac[i].on) want = 1;
    if (want) {
      if (d->ntables == ac_ntab + 1 && c->intable) ac_pending = 1;
      else if (d->nimages == ac_nimg + 1 && d->images[d->nimages - 1].kind == OBJ_PICTURE && c->caret.para < d->np && c->caret.off >= 4 &&
               IS_PIC(d->p[c->caret.para]->t + c->caret.off - 4) && pic_index(d->p[c->caret.para]->t + c->caret.off - 4) == d->nimages - 1) ac_pending = 2;   /* (just put in at the caret, not a changed one) */
      if (ac_pending) DoMethod(wp_app(), MUIM_Application_PushMethod, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RF_AUTOCAP_DO);
    }
  }
  ac_doc = d; ac_ntab = d->ntables; ac_nimg = d->nimages;
}
static void ctxmenu(Object *menu, const WpCtx *c)
{
  if (!c->doc) return;
  if (c->where == WPC_OBJECT || c->intable) {
    wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
    wp_ctx_item(menu, tr("Insert Caption..."), WP_CTX_ID(ID_RF_CAPTION), 1);
  } else if (c->where == WPC_TEXT && in_block(c)) {
    wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
    wp_ctx_item(menu, tr("Update Table"), WP_CTX_ID(ID_RF_UPDATE_HERE), 1);
  }
}
/* Alt-Shift-X Mark Entry, Alt-Shift-I Mark Citation (Word's) */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;

  if (shift && alt && !(qual & IEQUALIFIER_CONTROL)) {
    if (code == 0x32) return ID_RF_MARK;
    if (code == 0x17) return ID_RF_CITE;
  }
  return 0;
}
static void init(void)
{
  tr_list(leader_names, leader_ent);
  leader_ent[0] = tr("(none)");
  tr_list(fmt_names, fmt_ent);
  tr_list(lang_names, lang_ent);
  refs_lang = lang_current();
  if (refs_lang < 0 || refs_lang >= RL_N) refs_lang = RL_EN;
}
static void cleanup(void)
{
  int i;
  for (i = 0; i < k_ncit; i++) free(k_cits[i]);
  free(k_cits); k_cits = NULL; k_ncit = 0;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp2_package = {
  "WP2", ID_WP2_FIRST, ID_WP2_LAST,
  wp2_command, state, NULL, ctxmenu, key, init, cleanup,
  groups, 3,
  menus, (int)(sizeof menus / sizeof menus[0])
};
