/*
 * wp_mailings.c - WP4 Mail merge, envelopes, labels (docs/wordcraft-gap.md). This file belongs to WP4: only that
 * package edits it. Its commands are ID_WP4_FIRST .. ID_WP4_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The model is core/merge.h (recipients, merge fields, previewing, merging) and core/envlabel.h
 * (envelopes, labels). Here: the Mailings tab and the File menu's Mailings submenu, with Word's
 * windows: Envelopes, Labels, Start Mail Merge (and the Step-by-Step wizard), Select Recipients
 * (a new list, a CSV/tab/.xlsx/.docx file), Edit Recipient List with Filter and Sort, Insert Merge
 * Field, Address Block, Greeting Line, Rules, Match Fields, Preview Results with record navigation,
 * Go to Record, Find Recipient, Check for Errors, Finish & Merge to a new document or PDF files.
 * Printing and e-mail are left out on purpose.
 *
 * Buttons and gadgets inside the windows share two ids: each writes its own code into ml_code
 * (MUIM_WriteLong) and returns ID_ML_BTN or ID_ML_EV, so the 64 ids of the package are enough.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "theme.h"
#include "../core/merge.h"
#include "../core/envlabel.h"
#include "../core/field.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#define FLabel(l) QLabel(l, MUIO_Label_SingleFrame)

int  wp_open_doc(Doc *d, const char *path, int changed);     /* main.c */
void wp_replace_doc(Doc *d, int changed);
const char *wp_username(void);
const char *wp_docpath(void);
FontSet *wp_fonts(void);

enum {
  ID_ML_ENVELOPES = ID_WP4_FIRST, ID_ML_LABELS,
  ID_ML_START,                          /* the Start Mail Merge list (value: ST_) */
  ID_ML_ST_LETTERS, ID_ML_ST_ENV, ID_ML_ST_LAB, ID_ML_ST_DIR, ID_ML_ST_NORMAL, ID_ML_WIZARD,
  ID_ML_RECIP,                          /* the Select Recipients list (0 new, 1 existing) */
  ID_ML_NEWLIST, ID_ML_EXISTING, ID_ML_EDITLIST, ID_ML_FILTER,
  ID_ML_HIGHLIGHT, ID_ML_ADDRESS, ID_ML_GREETING,
  ID_ML_INSFIELD,                       /* the Insert Merge Field list (value: the column) */
  ID_ML_INSFIELDWIN, ID_ML_RULES,       /* the Rules list (value: RU_) */
  ID_ML_RULESWIN, ID_ML_MATCH, ID_ML_UPDLABELS,
  ID_ML_PREVIEW, ID_ML_FIRST, ID_ML_PREV, ID_ML_NEXT, ID_ML_LAST, ID_ML_GOTO, ID_ML_FIND, ID_ML_CHECK,
  ID_ML_FINISH,                         /* the Finish & Merge list (0 documents, 1 PDF) */
  ID_ML_FIN_DOC, ID_ML_FIN_PDF,
  ID_ML_BTN, ID_ML_EV,                  /* a window's button or gadget (ml_code says which) */
  /* the windows' OK buttons */
  ID_ML_OK_ENV, ID_ML_OK_LAB, ID_ML_OK_NEW, ID_ML_OK_EDIT, ID_ML_OK_FILT, ID_ML_OK_INS, ID_ML_OK_ADDR, ID_ML_OK_GREET,
  ID_ML_OK_MATCH, ID_ML_OK_RULE, ID_ML_OK_GOTO, ID_ML_OK_FIND, ID_ML_OK_CHECK, ID_ML_OK_FIN, ID_ML_OK_WIZ
};
/* the codes of the windows' buttons and gadgets */
enum {
  BT_NONE, BT_ASKOK, BT_ASKCANCEL,
  BT_E_ADD,                              /* Envelopes: Add to Document */
  BT_L_VENDOR, BT_L_PRODUCT,             /* Labels: the vendor or product changed */
  BT_N_NEW, BT_N_DEL, BT_N_COL, BT_N_PICK, BT_N_EDIT,   /* the list editor */
  BT_R_TOGGLE, BT_R_ALL, BT_R_NONE, BT_R_FILTER, BT_R_DUP, BT_R_FIND, BT_R_VALID, BT_R_EDIT, BT_R_REFRESH,
  BT_F_CLEAR,
  BT_I_KIND,                             /* Insert Merge Field: address or database fields */
  BT_A_CHANGE, BT_A_PREV, BT_A_NEXT, BT_A_MATCH,     /* Address Block and Greeting Line */
  BT_U_KIND,                             /* Rules: the rule changed */
  BT_W_BACK, BT_W_NEXT, BT_W_EXCLUDE,    /* the wizard */
  BT_C_DUMMY
};
static LONG ml_code;
typedef char wp4_ids_fit[ID_ML_OK_WIZ <= ID_WP4_LAST ? 1 : -1];     /* (the 64 ids of the package are enough) */
enum { ST_INPREVIEW = 1, ST_HIGHLIGHT = 2, ST_MAIN = 3, ST_DATA = 4, ST_LABELS = 5 };

/* ------------------------------------------------------------------ helpers */
static Object *view(void) { return wp_view(); }
static Doc *doc(void) { return wp_doc(); }
static MergeInfo *mi(void) { return merge_info(doc()); }
static void changed(void) { DoMethod(view(), MUIM_Quill_Relayout); wp_refresh(); }
/* text for gadgets: UTF-8 to Latin-1, in one of a few static buffers */
static const char *l1(const char *u)
{
  static char b[8][256];
  static int k;
  char *o = b[k++ & 7];
  utf8_to_latin1(u ? u : "", (u32)strlen(u ? u : ""), o, 256);
  return o;
}
static void u8of(Object *str, char *out, size_t cap)
{
  char *s = NULL;
  get(str, MUIA_String_Contents, &s);
  wp_latin1_utf8(s ? s : "", out, cap);
}
static LONG cyc(Object *o) { ULONG v = 0; if (o) get(o, MUIA_Cycle_Active, &v); return (LONG)v; }
static LONG sel(Object *o) { ULONG v = 0; if (o) get(o, MUIA_Selected, &v); return (LONG)v; }
/* a button that returns ID_ML_BTN with code */
static Object *btn(const char *label, LONG code)
{
  Object *b = QButton(tr(label));
  if (b) {
    DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)b, 3, MUIM_WriteLong, code, (ULONG)&ml_code);
    DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_BTN);
  }
  return b;
}
/* a gadget whose attribute changing returns ID_ML_EV with code */
static void notify_ev(Object *o, ULONG attr, LONG code)
{
  if (!o) return;
  DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)o, 3, MUIM_WriteLong, code, (ULONG)&ml_code);
  DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_EV);
}
static Object *str(const char *init, int max)
{
  return StringObject, StringFrame, MUIA_String_MaxLen, max, MUIA_String_Contents, (ULONG)(init ? init : ""), MUIA_CycleChain, 1, End;
}
static Object *cycle(const char **entries) { return CycleObject, MUIA_Cycle_Entries, (ULONG)entries, MUIA_CycleChain, 1, End; }
static int is_open(Object *w) { ULONG o = 0; if (w) get(w, MUIA_Window_Open, &o); return o != 0; }
static void close_win(Object *w) { if (w) set(w, MUIA_Window_Open, FALSE); }
/* a window made afresh each time (its gadgets depend on the recipient list's columns) disposed of */
static void drop_win(Object **w)
{
  if (!*w) return;
  set(*w, MUIA_Window_Open, FALSE);
  DoMethod(wp_app(), OM_REMMEMBER, (ULONG)*w);
  MUI_DisposeObject(*w);
  *w = NULL;
}
static void back_to_doc(void) { set(wp_win(), MUIA_Window_Activate, TRUE); }
/* the columns as cycle entries: first (optional) then each column's name in Latin-1 */
#define MAXCOLS 64
static char colname[MAXCOLS][64];
static const char *colent[MAXCOLS + 2];
static int ncolent;
static void make_cols(const char *first)
{
  MergeInfo *m = doc()->merge;
  int i, n = 0;
  if (first) colent[n++] = tr(first);
  for (i = 0; m && m->data && i < m->data->ncols && i < MAXCOLS; i++) {
    utf8_to_latin1(m->data->cols[i], (u32)strlen(m->data->cols[i]), colname[i], sizeof colname[i]);
    colent[n++] = colname[i];
  }
  if (!n) colent[n++] = "-";
  colent[n] = NULL;
  ncolent = n;
}
/* the recipients, read if needed; 0 (after saying so, unless quiet) if there are none */
static int have_data(int quiet)
{
  char err[160];
  Doc *d = doc();
  if (d->merge && merge_ensure(d, err, sizeof err)) return 1;
  if (!quiet) {
    if (err[0]) wp_ask("OK", "%s", err);
    else wp_ask("OK", tr("Choose a recipient list first (Select Recipients)."), NULL);
  }
  return 0;
}
/* the record previewed: the view index of the active record k */
static int cur_active(void)
{
  MergeInfo *m = doc()->merge;
  int k;
  if (!m) return 0;
  for (k = 0; k < merge_count(m); k++) if (merge_active(m, k) >= m->rec) return k;
  return 0;
}
static void preview_again(void)
{
  Doc *d = doc();
  if (d->merge && d->merge->viewmerged) { merge_preview(d, 1); changed(); }
}
/* a field with code (UTF-8, nested fields in the stored form) at the caret in place of the selection */
static void put_code(const char *code)
{
  Doc *d = doc();
  Pos *c = wp_caret(), *a = NULL, lo, hi, z;
  z.para = 0; z.off = 0;
  get(view(), MUIA_Quill_Anchor, &a);
  lo = c ? *c : z; hi = a ? *a : lo;
  if (pos_cmp(lo, hi) > 0) { Pos t = lo; lo = hi; hi = t; }
  doc_begin(d, lo);
  if (pos_cmp(lo, hi)) doc_delete(d, lo, hi);
  {
    char ph[256];
    if (merge_placeholder(code, ph, sizeof ph)) field_insert_result(d, &lo, code, ph, NULL, NULL);
    else field_insert(d, &lo, code, NULL, NULL);
  }
  doc_end(d, lo);
  if (d->merge && d->merge->viewmerged) merge_preview(d, 1);
  changed();
  { Pos ab[2]; ab[0] = lo; ab[1] = lo; DoMethod(view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab); }
}
static void put_typed(const char *typed)
{
  char *c = field_code_parse(typed);
  if (c) { put_code(c); free(c); }
}
/* a field name for a column: spaces as underscores, as Word writes them */
static void field_for(const char *col, char *out, size_t cap)
{
  size_t i;
  snprintf(out, cap, "%s", col);
  for (i = 0; out[i]; i++) if (out[i] == ' ') out[i] = '_';
}
/* a new document whose text is t (lines separated by '\n') */
static Doc *text_doc(const char *t)
{
  Doc *d = doc_new();
  Pos at;
  if (!d) return NULL;
  at = doc_start(d);
  doc_insert(d, &at, t, (u32)strlen(t), NULL);
  doc_clear_undo(d);
  return d;
}

/* ------------------------------------------------------------------ ASK and FILLIN while merging */
static Object *askwin, *ask_text, *ask_str;
static int ask_prompt(void *ctx, const char *prompt, const char *def, char *out, size_t cap)
{
  ULONG sigs = 0;
  int okd = 0;
  if (!askwin) {
    askwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Mail Merge"), MUIA_Window_ID, MAKE_ID('M','L','A','K'),
      WindowContents, VGroup, QDLG,
        Child, ask_text = TextObject, MUIA_Text_Contents, (ULONG)"", End,
        Child, ask_str = str("", 256),
        Child, HGroup, Child, btn("_OK", BT_ASKOK), Child, HSpace(0), Child, btn("_Cancel", BT_ASKCANCEL), End,
      End, End;
    if (!askwin) return 0;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)askwin);
    DoMethod(ask_str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)ask_str, 3, MUIM_WriteLong, BT_ASKOK, (ULONG)&ml_code);
    DoMethod(ask_str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_BTN);
    DoMethod(askwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)askwin, 3, MUIM_WriteLong, BT_ASKCANCEL, (ULONG)&ml_code);
    DoMethod(askwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_BTN);
  }
  set(ask_text, MUIA_Text_Contents, (ULONG)l1(prompt));
  set(ask_str, MUIA_String_Contents, (ULONG)l1(def));
  set(askwin, MUIA_Window_Open, TRUE);
  set(askwin, MUIA_Window_ActiveObject, (ULONG)ask_str);
  ml_code = 0;
  /* a loop of its own until it is answered (other commands meanwhile are dropped) */
  for (;;) {
    ULONG id = DoMethod(wp_app(), MUIM_Application_NewInput, (ULONG)&sigs);
    if (id == ID_ML_BTN && (ml_code == BT_ASKOK || ml_code == BT_ASKCANCEL)) { okd = ml_code == BT_ASKOK; break; }
    if (id == (ULONG)MUIV_Application_ReturnID_Quit) break;
    if (sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
  }
  set(askwin, MUIA_Window_Open, FALSE);
  if (okd) u8of(ask_str, out, cap);
  return okd;
}

/* ------------------------------------------------------------------ Envelopes */
static Object *envwin, *e_del[5], *e_ret[4], *e_omit, *e_size, *e_addbtn;
static const char *envsizes[16];
static int e_merge;                     /* Start Mail Merge > Envelopes: OK makes the main document */
static void e_lines(Object **s, int n, char *out, size_t cap)
{
  int i;
  size_t k = 0;
  out[0] = 0;
  for (i = 0; i < n; i++) {
    char u[256];
    u8of(s[i], u, sizeof u);
    if (!u[0]) continue;
    k += (size_t)snprintf(out + k, k < cap ? cap - k : 0, "%s%s", k ? "\n" : "", u);
    if (k >= cap) break;
  }
}
static void env_open(int merge)
{
  int i;
  e_merge = merge;
  if (!envwin) {
    Object *c, *dg, *rg;
    for (i = 0; i < env_nsizes && i < 15; i++) envsizes[i] = env_sizes[i].name;
    envsizes[i] = NULL;
    dg = VGroup, GroupFrameT(tr("Delivery address")), End;
    rg = VGroup, GroupFrameT(tr("Return address")), End;
    for (i = 0; i < 5; i++) { e_del[i] = str("", 120); DoMethod(dg, OM_ADDMEMBER, (ULONG)e_del[i]); }
    for (i = 0; i < 4; i++) { e_ret[i] = str("", 120); DoMethod(rg, OM_ADDMEMBER, (ULONG)e_ret[i]); }
    c = VGroup,
      Child, dg,
      Child, rg,
      Child, HGroup, Child, e_omit = CheckMark(FALSE), Child, FLabel(tr("Omit")), Child, HSpace(0), End,
      Child, HGroup, Child, FLabel(tr("Envelope size:")), Child, e_size = cycle(envsizes), End,
      Child, HGroup, Child, e_addbtn = btn("_Add to Document", BT_E_ADD), Child, HSpace(0), End,
    End;
    if (!(envwin = wp_dialog("Envelopes", MAKE_ID('M','L','E','N'), c, "_New Document", ID_ML_OK_ENV))) return;
    /* the return address: the user's name */
    set(e_ret[0], MUIA_String_Contents, (ULONG)l1(wp_username()));
  }
  set(envwin, MUIA_Window_Title, (ULONG)tr(merge ? "Envelope Options" : "Envelopes"));
  set(e_addbtn, MUIA_Disabled, merge);
  for (i = 0; i < 5; i++) set(e_del[i], MUIA_Disabled, merge);
  if (!merge) {
    /* the delivery address from the selection, as Word takes it */
    Pos *c = wp_caret(), *a = NULL;
    get(view(), MUIA_Quill_Anchor, &a);
    if (c && a && pos_cmp(*c, *a)) {
      u32 n;
      char *t = doc_text(doc(), pos_cmp(*c, *a) < 0 ? *c : *a, pos_cmp(*c, *a) < 0 ? *a : *c, &n), *p = t;
      for (i = 0; i < 5; i++) {
        char *e = p ? strchr(p, '\n') : NULL;
        if (e) *e = 0;
        set(e_del[i], MUIA_String_Contents, (ULONG)(p ? l1(p) : ""));
        p = e ? e + 1 : NULL;
      }
      free(t);
    }
  }
  set(envwin, MUIA_Window_Open, TRUE);
}
static void env_spec(EnvSpec *e, char *del, char *ret, size_t cap)
{
  memset(e, 0, sizeof *e);
  e_lines(e_del, 5, del, cap);
  e_lines(e_ret, 4, ret, cap);
  e->size = (int)cyc(e_size);
  e->delivery = e_merge ? "" : del;
  e->ret = sel(e_omit) ? NULL : ret;
}
static void env_done(int add)
{
  EnvSpec e;
  char del[700], ret[600];
  Doc *d;
  env_spec(&e, del, ret, sizeof ret);
  close_win(envwin);
  if (add) {
    if (env_add_to_doc(doc(), &e)) changed();
    back_to_doc();
    return;
  }
  if (!(d = env_doc(&e))) return;
  if (e_merge) {
    /* the main document becomes the envelope (its recipients kept) */
    MergeInfo *m = doc()->merge;
    if (doc()->np > 1 || doc()->p[0]->len) { if (wp_ask(tr("_Continue|_Cancel"), tr("The document's contents will be replaced by the envelope."), NULL) != 1) { doc_free(d); return; } }
    doc()->merge = NULL;
    d->merge = m;
    if (!d->merge) merge_info(d);
    if (d->merge) d->merge->type = MT_ENVELOPES;
    wp_replace_doc(d, 1);
  } else wp_open_doc(d, "", 1);
}

/* ------------------------------------------------------------------ Labels */
static Object *labwin, *l_addr[4], *l_full, *l_row, *l_col, *l_vendor, *l_list, *l_info;
static int l_idx[64], l_n, l_merge;
static void lab_fill(void)
{
  int v = (int)cyc(l_vendor), i;
  l_n = 0;
  set(l_list, MUIA_List_Quiet, TRUE);
  DoMethod(l_list, MUIM_List_Clear);
  for (i = 0; i < label_ndefs && l_n < 64; i++)
    if (!strcmp(label_defs[i].vendor, label_vendors[v])) { l_idx[l_n++] = i; DoMethod(l_list, MUIM_List_InsertSingle, (ULONG)label_defs[i].product, MUIV_List_Insert_Bottom); }
  set(l_list, MUIA_List_Quiet, FALSE);
  set(l_list, MUIA_List_Active, 0);
}
static int lab_chosen(void)
{
  LONG a = -1;
  get(l_list, MUIA_List_Active, &a);
  return a >= 0 && a < l_n ? l_idx[a] : -1;
}
static void lab_info(void)
{
  int k = lab_chosen();
  char t[200], w[16], h[16];
  if (k < 0) { set(l_info, MUIA_Text_Contents, (ULONG)""); return; }
  sprintf(w, "%ld.%02ld", (long)(label_defs[k].w * 254L / 1440 / 100), (long)(label_defs[k].w * 254L / 1440 % 100));
  sprintf(h, "%ld.%02ld", (long)(label_defs[k].h * 254L / 1440 / 100), (long)(label_defs[k].h * 254L / 1440 % 100));
  snprintf(t, sizeof t, "%s\n%s: %s cm x %s cm, %d x %d", tr(label_defs[k].desc), tr("Label size"), w, h, (int)label_defs[k].cols, (int)label_defs[k].rows);
  set(l_info, MUIA_Text_Contents, (ULONG)t);
}
static void lab_open(int merge)
{
  int i;
  l_merge = merge;
  if (!labwin) {
    static const char *vend[8], *mode[3];
    Object *c, *ag;
    for (i = 0; i < label_nvendors && i < 7; i++) vend[i] = tr(label_vendors[i]);
    vend[i] = NULL;
    mode[0] = tr("Full page of the same label"); mode[1] = tr("Single label"); mode[2] = NULL;
    ag = VGroup, GroupFrameT(tr("Address")), End;
    for (i = 0; i < 4; i++) { l_addr[i] = str("", 120); DoMethod(ag, OM_ADDMEMBER, (ULONG)l_addr[i]); }
    c = VGroup,
      Child, ag,
      Child, HGroup,
        Child, l_full = RadioObject, GroupFrameT(tr("Print")), MUIA_Radio_Entries, (ULONG)mode, MUIA_CycleChain, 1, End,
        Child, ColGroup(2), Child, FLabel(tr("Row:")), Child, l_row = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 4, MUIA_String_Contents, (ULONG)"1", MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Column:")), Child, l_col = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 4, MUIA_String_Contents, (ULONG)"1", MUIA_CycleChain, 1, End, End,
      End,
      Child, HGroup, Child, FLabel(tr("Label vendors:")), Child, l_vendor = cycle(vend), End,
      Child, HGroup,
        Child, l_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, l_info = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
      End,
    End;
    if (!(labwin = wp_dialog("Labels", MAKE_ID('M','L','L','B'), c, "_New Document", ID_ML_OK_LAB))) return;
    notify_ev(l_vendor, MUIA_Cycle_Active, BT_L_VENDOR);
    notify_ev(l_list, MUIA_List_Active, BT_L_PRODUCT);
    lab_fill();
  }
  set(labwin, MUIA_Window_Title, (ULONG)tr(merge ? "Label Options" : "Labels"));
  for (i = 0; i < 4; i++) set(l_addr[i], MUIA_Disabled, merge);
  set(l_full, MUIA_Disabled, merge);
  lab_info();
  set(labwin, MUIA_Window_Open, TRUE);
}
static void lab_done(void)
{
  int k = lab_chosen(), i;
  char text[700], *rs = NULL, *cs = NULL;
  Doc *d;
  if (k < 0) { DisplayBeep(NULL); return; }
  e_lines(l_addr, 4, text, sizeof text);
  get(l_row, MUIA_String_Contents, &rs); get(l_col, MUIA_String_Contents, &cs);
  i = rs ? atoi(rs) : 1;
  close_win(labwin);
  d = label_doc(&label_defs[k], text, (int)cyc(l_full) == 1 && !l_merge, (i > 0 ? i : 1) - 1, (cs && atoi(cs) > 0 ? atoi(cs) : 1) - 1, l_merge);
  if (!d) return;
  if (l_merge) {
    MergeInfo *m = doc()->merge;
    if ((doc()->np > 1 || doc()->p[0]->len) && wp_ask(tr("_Continue|_Cancel"), tr("The document's contents will be replaced by the labels."), NULL) != 1) { doc_free(d); return; }
    doc()->merge = NULL;
    d->merge = m;
    if (!d->merge) merge_info(d);
    if (d->merge) d->merge->type = MT_LABELS;
    wp_replace_doc(d, 1);
  } else wp_open_doc(d, "", 1);
}

/* ------------------------------------------------------------------ the recipient list in a window */
/* the list shows view rows (entries are view index + 1); the first column the check mark */
static char rl_title[MAXCOLS][64];
static ULONG rl_disp(__reg("a0") struct Hook *h, __reg("a2") char **arr, __reg("a1") APTR entry)
{
  static char cell[8][96];
  MergeInfo *m = wp_doc()->merge;
  int v = (int)(ULONG)entry - 1, c, n;
  if (!m || !m->data) { arr[0] = ""; return 0; }
  n = m->data->ncols < 7 ? m->data->ncols : 7;
  if (!entry) { arr[0] = ""; for (c = 0; c < n; c++) arr[c + 1] = rl_title[c]; return 0; }
  if (v < 0 || v >= m->nview) { arr[0] = ""; for (c = 0; c < n; c++) arr[c + 1] = ""; return 0; }
  arr[0] = m->data->on[m->view[v]] ? "[x]" : "[ ]";
  for (c = 0; c < n; c++) {
    utf8_to_latin1(m->data->cell[m->view[v]][c], (u32)strlen(m->data->cell[m->view[v]][c]), cell[c], sizeof cell[c]);
    arr[c + 1] = cell[c];
  }
  return 0;
}
static struct Hook rl_hook = { { NULL, NULL }, (HOOKFUNC)rl_disp, NULL, NULL };
static Object *editwin, *r_list, *r_count;
static void rl_fill(void)
{
  MergeInfo *m = doc()->merge;
  int i, n;
  char fmt[80], cnt[64];
  if (!r_list) return;
  set(r_list, MUIA_List_Quiet, TRUE);
  DoMethod(r_list, MUIM_List_Clear);
  n = m && m->data ? (m->data->ncols < 7 ? m->data->ncols : 7) : 0;
  strcpy(fmt, "BAR");
  for (i = 0; i < n; i++) { strcat(fmt, ",BAR"); utf8_to_latin1(m->data->cols[i], (u32)strlen(m->data->cols[i]), rl_title[i], sizeof rl_title[i]); }
  set(r_list, MUIA_List_Format, (ULONG)fmt);
  for (i = 0; m && i < m->nview; i++) DoMethod(r_list, MUIM_List_InsertSingle, (ULONG)(i + 1), MUIV_List_Insert_Bottom);
  set(r_list, MUIA_List_Quiet, FALSE);
  sprintf(cnt, tr("%d of %d recipients checked"), merge_count(m), m && m->data ? m->data->nrows : 0);
  set(r_count, MUIA_Text_Contents, (ULONG)cnt);
}
static void edit_open(void)
{
  if (!have_data(0)) return;
  if (!editwin) {
    Object *c = VGroup,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("These recipients will be merged: check or uncheck them, filter and sort the list."), End,
      Child, ListviewObject, MUIA_Listview_List, r_list = ListObject, InputListFrame, MUIA_List_DisplayHook, (ULONG)&rl_hook,
        MUIA_List_Title, TRUE, MUIA_List_Format, (ULONG)"BAR", End, MUIA_CycleChain, 1, End,
      Child, r_count = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, ColGroup(5),
        Child, btn("Check/_Uncheck", BT_R_TOGGLE), Child, btn("Check _All", BT_R_ALL), Child, btn("Clear A_ll", BT_R_NONE),
        Child, btn("_Filter and Sort...", BT_R_FILTER), Child, btn("_Refresh", BT_R_REFRESH),
        Child, btn("Find _Duplicates", BT_R_DUP), Child, btn("Find _Recipient...", BT_R_FIND), Child, btn("_Validate Addresses", BT_R_VALID),
        Child, btn("_Edit...", BT_R_EDIT), Child, HSpace(0),
      End,
    End;
    if (!(editwin = wp_dialog("Mail Merge Recipients", MAKE_ID('M','L','R','L'), c, "_OK", ID_ML_OK_EDIT))) return;
    DoMethod(r_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)r_list, 3, MUIM_WriteLong, BT_R_TOGGLE, (ULONG)&ml_code);
    DoMethod(r_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_BTN);
  }
  rl_fill();
  set(editwin, MUIA_Window_Open, TRUE);
}

/* ------------------------------------------------------------------ the list editor (Type a New List, Edit) */
/* a copy of the list being edited; saved to a CSV file on OK */
static MergeData *n_md;
static Object *newwin, *n_list, *n_strs[MAXCOLS], *n_colname;
static int n_cur = -1, n_cols, n_editing;
static ULONG nl_disp(__reg("a0") struct Hook *h, __reg("a2") char **arr, __reg("a1") APTR entry)
{
  static char cell[4][96];
  int r = (int)(ULONG)entry - 1, c;
  if (!n_md) { arr[0] = arr[1] = arr[2] = arr[3] = ""; return 0; }
  for (c = 0; c < 4; c++) {
    const char *s = c >= n_md->ncols ? "" : !entry ? n_md->cols[c] : r >= 0 && r < n_md->nrows ? n_md->cell[r][c] : "";
    utf8_to_latin1(s, (u32)strlen(s), cell[c], sizeof cell[c]);
    arr[c] = cell[c];
  }
  return 0;
}
static struct Hook nl_hook = { { NULL, NULL }, (HOOKFUNC)nl_disp, NULL, NULL };
/* the string gadgets into the entry being edited */
static void n_store(void)
{
  int c;
  if (!n_md || n_cur < 0 || n_cur >= n_md->nrows) return;
  for (c = 0; c < n_md->ncols && c < n_cols; c++) { char u[256]; u8of(n_strs[c], u, sizeof u); md_set(n_md, n_cur, c, u); }
  DoMethod(n_list, MUIM_List_Redraw, n_cur);
}
static void n_show(int r)
{
  int c;
  n_cur = r;
  for (c = 0; c < n_cols; c++) {
    set(n_strs[c], MUIA_String_Contents, (ULONG)(n_md && r >= 0 && r < n_md->nrows && c < n_md->ncols ? l1(n_md->cell[r][c]) : ""));
    set(n_strs[c], MUIA_Disabled, r < 0);
  }
}
static void n_fill(void)
{
  int r;
  set(n_list, MUIA_List_Quiet, TRUE);
  DoMethod(n_list, MUIM_List_Clear);
  for (r = 0; n_md && r < n_md->nrows; r++) DoMethod(n_list, MUIM_List_InsertSingle, (ULONG)(r + 1), MUIV_List_Insert_Bottom);
  set(n_list, MUIA_List_Quiet, FALSE);
}
/* the window, made for the list's columns */
static void new_open(MergeData *start, int editing)
{
  Object *c, *grid;
  int i;
  drop_win(&newwin);
  md_free(n_md);
  n_md = start;
  n_editing = editing;
  if (!n_md) return;
  if (!n_md->nrows) md_add_row(n_md, NULL);
  n_cols = n_md->ncols < 20 ? n_md->ncols : 20;
  grid = ColGroup(4), End;
  for (i = 0; i < n_cols; i++) {
    n_strs[i] = str("", 250);
    DoMethod(grid, OM_ADDMEMBER, (ULONG)FLabel(l1(n_md->cols[i])));
    DoMethod(grid, OM_ADDMEMBER, (ULONG)n_strs[i]);
  }
  if (n_cols & 1) { DoMethod(grid, OM_ADDMEMBER, (ULONG)HSpace(0)); DoMethod(grid, OM_ADDMEMBER, (ULONG)HSpace(0)); }
  c = VGroup,
    Child, ListviewObject, MUIA_Listview_List, n_list = ListObject, InputListFrame, MUIA_List_DisplayHook, (ULONG)&nl_hook,
      MUIA_List_Title, TRUE, MUIA_List_Format, (ULONG)"BAR,BAR,BAR,", End, MUIA_CycleChain, 1, End,
    Child, grid,
    Child, HGroup, Child, btn("_New Entry", BT_N_NEW), Child, btn("_Delete Entry", BT_N_DEL), Child, HSpace(0),
      Child, FLabel(tr("New column:")), Child, n_colname = str("", 60), Child, btn("_Add Column", BT_N_COL), End,
  End;
  if (!(newwin = wp_dialog(editing ? "Edit Data Source" : "New Address List", MAKE_ID('M','L','N','L'), c, editing ? "_OK" : "_Save...", ID_ML_OK_NEW))) return;
  notify_ev(n_list, MUIA_List_Active, BT_N_PICK);
  n_fill();
  set(n_list, MUIA_List_Active, 0);
  n_show(0);
  set(newwin, MUIA_Window_Open, TRUE);
}
static void new_ok(void)
{
  MergeInfo *m;
  char path[512];
  size_t len;
  char *csv;
  n_store();
  /* entries left empty are dropped */
  { int r, c; for (r = n_md->nrows - 1; r >= 0; r--) { for (c = 0; c < n_md->ncols && !n_md->cell[r][c][0]; c++) ; if (c == n_md->ncols) md_delete_row(n_md, r); } }
  m = mi();
  if (!m) return;
  if (n_editing && m->src[0]) snprintf(path, sizeof path, "%s", m->src);
  else {
    snprintf(path, sizeof path, "%s", "Recipients.csv");
    if (!wp_file_request(1, "Save Address List", path, sizeof path, "#?.(csv|txt)")) return;
    if (!strchr(path, '.')) strncat(path, ".csv", sizeof path - strlen(path) - 1);
  }
  if (!(csv = md_csv(n_md, &len)) || !file_write(path, csv, len)) { free(csv); wp_ask("OK", tr("Quill could not write\n%s"), path); return; }
  free(csv);
  close_win(newwin);
  snprintf(m->src, sizeof m->src, "%s", path);
  m->dtype[0] = 0; m->connect[0] = 0; m->table[0] = 0;
  if (m->type == MT_NONE) m->type = MT_LETTERS;
  merge_set_data(doc(), n_md);
  n_md = NULL;
  preview_again();
  wp_refresh();
  if (is_open(editwin)) rl_fill();
}

/* ------------------------------------------------------------------ Filter and Sort */
static Object *filtwin, *f_andor[MERGE_NFILT], *f_col[MERGE_NFILT], *f_op[MERGE_NFILT], *f_val[MERGE_NFILT], *f_scol[MERGE_NSORT], *f_sdir[MERGE_NSORT];
static const char *ops_l[MF_N + 1], *andor_l[3], *dir_l[3];
static const char *const ops_en[MF_N] = { "Equal to", "Not equal to", "Less than", "Greater than", "Less than or equal", "Greater than or equal",
  "Is blank", "Is not blank", "Contains", "Does not contain" };
static void filt_open(void)
{
  MergeInfo *m = doc()->merge;
  Object *c, *fg, *sg;
  int i;
  if (!have_data(0)) return;
  drop_win(&filtwin);
  make_cols("(none)");
  for (i = 0; i < MF_N; i++) ops_l[i] = tr(ops_en[i]);
  ops_l[MF_N] = NULL;
  andor_l[0] = tr("And"); andor_l[1] = tr("Or"); andor_l[2] = NULL;
  dir_l[0] = tr("Ascending"); dir_l[1] = tr("Descending"); dir_l[2] = NULL;
  fg = ColGroup(4), GroupFrameT(tr("Filter Records")), End;
  for (i = 0; i < MERGE_NFILT; i++) {
    DoMethod(fg, OM_ADDMEMBER, (ULONG)(f_andor[i] = cycle(andor_l)));
    DoMethod(fg, OM_ADDMEMBER, (ULONG)(f_col[i] = cycle(colent)));
    DoMethod(fg, OM_ADDMEMBER, (ULONG)(f_op[i] = cycle(ops_l)));
    DoMethod(fg, OM_ADDMEMBER, (ULONG)(f_val[i] = str("", 120)));
    if (!i) set(f_andor[i], MUIA_Disabled, TRUE);
  }
  sg = ColGroup(2), GroupFrameT(tr("Sort Records")), End;
  for (i = 0; i < MERGE_NSORT; i++) {
    DoMethod(sg, OM_ADDMEMBER, (ULONG)(f_scol[i] = cycle(colent)));
    DoMethod(sg, OM_ADDMEMBER, (ULONG)(f_sdir[i] = cycle(dir_l)));
  }
  c = VGroup, Child, fg, Child, sg, Child, HGroup, Child, btn("_Clear All", BT_F_CLEAR), Child, HSpace(0), End, End;
  if (!(filtwin = wp_dialog("Filter and Sort", MAKE_ID('M','L','F','S'), c, "_OK", ID_ML_OK_FILT))) return;
  for (i = 0; i < m->nfilt && i < MERGE_NFILT; i++) {
    int col = md_col(m->data, m->filt[i].col);
    set(f_andor[i], MUIA_Cycle_Active, m->filt[i].or_);
    set(f_col[i], MUIA_Cycle_Active, col + 1);
    set(f_op[i], MUIA_Cycle_Active, m->filt[i].op);
    set(f_val[i], MUIA_String_Contents, (ULONG)l1(m->filt[i].val));
  }
  for (i = 0; i < MERGE_NSORT; i++) {
    int col = m->sortcol[i][0] ? md_col(m->data, m->sortcol[i]) : -1;
    set(f_scol[i], MUIA_Cycle_Active, col + 1);
    set(f_sdir[i], MUIA_Cycle_Active, m->sortdesc[i]);
  }
  set(filtwin, MUIA_Window_Open, TRUE);
}
static void filt_ok(void)
{
  MergeInfo *m = doc()->merge;
  int i;
  if (!m || !m->data) return;
  m->nfilt = 0;
  for (i = 0; i < MERGE_NFILT; i++) {
    LONG c = cyc(f_col[i]) - 1;
    MergeFilter *f = &m->filt[m->nfilt];
    if (c < 0 || c >= m->data->ncols) continue;
    memset(f, 0, sizeof *f);
    snprintf(f->col, sizeof f->col, "%s", m->data->cols[c]);
    f->op = (u8)cyc(f_op[i]);
    f->or_ = (u8)(m->nfilt ? cyc(f_andor[i]) : 0);
    u8of(f_val[i], f->val, sizeof f->val);
    m->nfilt++;
  }
  for (i = 0; i < MERGE_NSORT; i++) {
    LONG c = cyc(f_scol[i]) - 1;
    m->sortcol[i][0] = 0;
    if (c >= 0 && c < m->data->ncols) snprintf(m->sortcol[i], sizeof m->sortcol[i], "%s", m->data->cols[c]);
    m->sortdesc[i] = (u8)cyc(f_sdir[i]);
  }
  merge_apply_view(m);
  close_win(filtwin);
  if (is_open(editwin)) rl_fill();
  preview_again();
}

/* ------------------------------------------------------------------ Insert Merge Field (window and list) */
static Object *inswin, *i_kind, *i_list;
static const char *kind_l[3];
static void ins_fill(void)
{
  MergeInfo *m = doc()->merge;
  int i;
  set(i_list, MUIA_List_Quiet, TRUE);
  DoMethod(i_list, MUIM_List_Clear);
  if (cyc(i_kind) == 0) for (i = 0; m && m->data && i < m->data->ncols; i++) DoMethod(i_list, MUIM_List_InsertSingle, (ULONG)l1(m->data->cols[i]), MUIV_List_Insert_Bottom);
  else for (i = 1; i < AF_N; i++) DoMethod(i_list, MUIM_List_InsertSingle, (ULONG)tr(af_names[i]), MUIV_List_Insert_Bottom);
  set(i_list, MUIA_List_Quiet, FALSE);
  set(i_list, MUIA_List_Active, 0);
}
static void ins_open(void)
{
  if (!inswin) {
    Object *c;
    kind_l[0] = tr("Database Fields"); kind_l[1] = tr("Address Fields"); kind_l[2] = NULL;
    c = VGroup,
      Child, HGroup, Child, FLabel(tr("Insert:")), Child, i_kind = RadioObject, MUIA_Radio_Entries, (ULONG)kind_l, MUIA_Group_Horiz, TRUE, MUIA_CycleChain, 1, End, Child, HSpace(0), End,
      Child, FLabel(tr("Fields:")),
      Child, i_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
    End;
    if (!(inswin = wp_dialog("Insert Merge Field", MAKE_ID('M','L','I','F'), c, "_Insert", ID_ML_OK_INS))) return;
    notify_ev(i_kind, MUIA_Radio_Active, BT_I_KIND);
    DoMethod(i_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_OK_INS);
  }
  set(i_kind, MUIA_Radio_Active, doc()->merge && doc()->merge->data ? 0 : 1);
  ins_fill();
  set(inswin, MUIA_Window_Open, TRUE);
}
static void insert_column(const char *utf8name)
{
  char code[160], nm[128];
  field_for(utf8name, nm, sizeof nm);
  snprintf(code, sizeof code, " MERGEFIELD %s ", nm);
  put_code(code);
}
static void ins_ok(void)
{
  LONG a = -1;
  MergeInfo *m = doc()->merge;
  get(i_list, MUIA_List_Active, &a);
  if (a < 0) return;
  if (cyc(i_kind) == 0) { if (m && m->data && a < m->data->ncols) insert_column(m->data->cols[a]); }
  else if (a + 1 < AF_N) {
    /* an address field: the column matched to it, else its own name */
    int c = merge_af_col(m, (int)a + 1);
    insert_column(c >= 0 ? m->data->cols[c] : af_names[a + 1]);
  }
}
/* the ribbon's list: the columns (or Word's address fields while there are none) */
static char fl_names[MAXCOLS][64];
static const char *fl_labels[MAXCOLS];
static RibbonList field_rl = { fl_labels, 0, PL_TEXT, NULL };
static void field_list_update(void)
{
  MergeInfo *m = doc() ? doc()->merge : NULL;
  int i, n = 0;
  if (m && m->data) for (i = 0; i < m->data->ncols && n < MAXCOLS; i++) { utf8_to_latin1(m->data->cols[i], (u32)strlen(m->data->cols[i]), fl_names[n], sizeof fl_names[0]); fl_labels[n] = fl_names[n]; n++; }
  else for (i = 1; i < AF_N && n < MAXCOLS; i++) { fl_labels[n] = af_names[i]; n++; }
  field_rl.n = n;
}

/* ------------------------------------------------------------------ Address Block and Greeting Line */
static Object *addrwin, *a_names, *a_company, *a_postal, *a_country, *a_excl, *a_prev;
static Object *greetwin, *g_before, *g_name, *g_after, *g_invalid, *g_prev;
static int a_rec;                       /* the record shown in the previews (an active index) */
static const char *a_country_l[4], *g_before_l[4], *g_after_l[4], *g_names_l[16];
static void addr_code(char *code, size_t cap)
{
  char ex[128];
  LONG n = -1;
  get(a_names, MUIA_List_Active, &n);
  u8of(a_excl, ex, sizeof ex);
  merge_address_code((int)n, (int)sel(a_company), (int)sel(a_postal), (int)cyc(a_country), ex, code, cap);
}
static void greet_code(char *code, size_t cap)
{
  char inv[128];
  u8of(g_invalid, inv, sizeof inv);
  merge_greeting_code((int)cyc(g_before), (int)cyc(g_name), (int)cyc(g_after), inv, code, cap);
}
static void show_preview(Object *t, const char *code, int greeting)
{
  MergeInfo *m = doc()->merge;
  char out[600], *p, lat[600], head[64];
  int v = m && m->data ? merge_active(m, a_rec) : -1;
  if (greeting) merge_greeting(m, v, code, out, sizeof out); else merge_address(m, v, code, out, sizeof out);
  for (p = out; *p; p++) if (*p == '\v') *p = '\n';
  if (m && m->data) sprintf(head, "%s %d\n", tr("Recipient"), a_rec + 1); else head[0] = 0;
  utf8_to_latin1(out, (u32)strlen(out), lat, sizeof lat);
  { char t2[700]; snprintf(t2, sizeof t2, "%s%s", head, lat); set(t, MUIA_Text_Contents, (ULONG)t2); }
}
static void addr_preview(void) { char code[700]; if (addrwin) { addr_code(code, sizeof code); show_preview(a_prev, code, 0); } }
static void greet_preview(void) { char code[700]; if (greetwin) { greet_code(code, sizeof code); show_preview(g_prev, code, 1); } }
static void addr_open(void)
{
  int i;
  have_data(1);
  if (!addrwin) {
    Object *c, *l;
    a_country_l[0] = tr("Never include the country/region"); a_country_l[1] = tr("Always include the country/region");
    a_country_l[2] = tr("Only if different from:"); a_country_l[3] = NULL;
    c = HGroup,
      Child, VGroup,
        Child, FLabel(tr("Insert recipient's name in this format:")),
        Child, ListviewObject, MUIA_Listview_List, l = a_names = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, a_company = CheckMark(TRUE), Child, FLabel(tr("Insert company name")), Child, HSpace(0), End,
        Child, HGroup, Child, a_postal = CheckMark(TRUE), Child, FLabel(tr("Insert postal address:")), Child, HSpace(0), End,
        Child, a_country = cycle(a_country_l),
        Child, a_excl = str("United Kingdom", 100),
      End,
      Child, VGroup,
        Child, FLabel(tr("Preview")),
        Child, a_prev = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
        Child, HGroup, Child, btn("<", BT_A_PREV), Child, btn(">", BT_A_NEXT), End,
        Child, btn("_Match Fields...", BT_A_MATCH),
        Child, VSpace(0),
      End,
    End;
    if (!(addrwin = wp_dialog("Insert Address Block", MAKE_ID('M','L','A','B'), c, "_OK", ID_ML_OK_ADDR))) return;
    for (i = 0; i < merge_nname; i++) DoMethod(l, MUIM_List_InsertSingle, (ULONG)merge_name_pics[i], MUIV_List_Insert_Bottom);
    set(l, MUIA_List_Active, 5);
    set(a_country, MUIA_Cycle_Active, 2);
    notify_ev(a_names, MUIA_List_Active, BT_A_CHANGE);
    notify_ev(a_company, MUIA_Selected, BT_A_CHANGE);
    notify_ev(a_postal, MUIA_Selected, BT_A_CHANGE);
    notify_ev(a_country, MUIA_Cycle_Active, BT_A_CHANGE);
    notify_ev(a_excl, MUIA_String_Contents, BT_A_CHANGE);
  }
  a_rec = cur_active();
  addr_preview();
  set(addrwin, MUIA_Window_Open, TRUE);
}
static void greet_open(void)
{
  int i;
  have_data(1);
  if (!greetwin) {
    Object *c;
    g_before_l[0] = "Dear"; g_before_l[1] = "To"; g_before_l[2] = tr("(none)"); g_before_l[3] = NULL;
    g_after_l[0] = ","; g_after_l[1] = ":"; g_after_l[2] = tr("(none)"); g_after_l[3] = NULL;
    for (i = 0; i < merge_ngreet && i < 15; i++) g_names_l[i] = merge_greet_pics[i];
    g_names_l[i] = NULL;
    c = VGroup,
      Child, FLabel(tr("Greeting line format:")),
      Child, HGroup, Child, g_before = cycle(g_before_l), Child, g_name = cycle(g_names_l), Child, g_after = cycle(g_after_l), End,
      Child, FLabel(tr("Greeting line for invalid recipient names:")),
      Child, g_invalid = str("Dear Sir or Madam,", 100),
      Child, FLabel(tr("Preview")),
      Child, g_prev = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
      Child, HGroup, Child, btn("<", BT_A_PREV), Child, btn(">", BT_A_NEXT), Child, HSpace(0), Child, btn("_Match Fields...", BT_A_MATCH), End,
    End;
    if (!(greetwin = wp_dialog("Insert Greeting Line", MAKE_ID('M','L','G','L'), c, "_OK", ID_ML_OK_GREET))) return;
    notify_ev(g_before, MUIA_Cycle_Active, BT_A_CHANGE);
    notify_ev(g_name, MUIA_Cycle_Active, BT_A_CHANGE);
    notify_ev(g_after, MUIA_Cycle_Active, BT_A_CHANGE);
    notify_ev(g_invalid, MUIA_String_Contents, BT_A_CHANGE);
  }
  a_rec = cur_active();
  greet_preview();
  set(greetwin, MUIA_Window_Open, TRUE);
}

/* ------------------------------------------------------------------ Match Fields */
static Object *matchwin, *m_cyc[AF_N];
static void match_open(void)
{
  MergeInfo *m;
  Object *c, *g;
  int i;
  if (!have_data(0)) return;
  m = doc()->merge;
  drop_win(&matchwin);
  make_cols("(not matched)");
  g = ColGroup(2), End;
  for (i = 0; i < AF_N; i++) {
    int col = merge_af_col(m, i);
    DoMethod(g, OM_ADDMEMBER, (ULONG)FLabel(tr(af_names[i])));
    DoMethod(g, OM_ADDMEMBER, (ULONG)(m_cyc[i] = cycle(colent)));
    set(m_cyc[i], MUIA_Cycle_Active, col + 1);
  }
  c = VGroup,
    Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Choose the column of your list that holds each of these:"), End,
    Child, ScrollgroupObject, MUIA_Scrollgroup_FreeHoriz, FALSE, MUIA_Scrollgroup_Contents, VirtgroupObject, Child, g, End, End,
  End;
  if (!(matchwin = wp_dialog("Match Fields", MAKE_ID('M','L','M','F'), c, "_OK", ID_ML_OK_MATCH))) return;
  set(matchwin, MUIA_Window_Height, MUIV_Window_Height_Visible(70));
  set(matchwin, MUIA_Window_Open, TRUE);
}
static void match_ok(void)
{
  MergeInfo *m = doc()->merge;
  int i;
  if (!m || !m->data) return;
  for (i = 0; i < AF_N; i++) {
    LONG c = cyc(m_cyc[i]) - 1;
    m->map[i][0] = 0;
    if (c >= 0 && c < m->data->ncols) snprintf(m->map[i], sizeof m->map[i], "%s", m->data->cols[c]);
    else strcpy(m->map[i], "\x01");          /* (matched to nothing on purpose: not found by its name either) */
  }
  close_win(matchwin);
  addr_preview(); greet_preview();
  preview_again();
}

/* ------------------------------------------------------------------ Rules */
enum { RU_ASK, RU_FILLIN, RU_IF, RU_MERGEREC, RU_MERGESEQ, RU_NEXT, RU_NEXTIF, RU_SET, RU_SKIPIF, RU_N };
static const char *const rule_en[RU_N] = { "Ask...", "Fill-in...", "If...Then...Else...", "Merge Record #", "Merge Sequence #", "Next Record",
  "Next Record If...", "Set Bookmark...", "Skip Record If..." };
static const char *rule_l[RU_N + 1];
static const RibbonList rule_rl = { rule_l, RU_N, PL_TEXT, NULL };
static Object *rulewin, *u_kind, *u_field, *u_op, *u_val, *u_then, *u_else, *u_bm, *u_prompt, *u_def, *u_once;
static const char *ops8_l[9];
static void rule_enable(void)
{
  static const int map[] = { RU_ASK, RU_FILLIN, RU_IF, RU_NEXTIF, RU_SET, RU_SKIPIF };
  int k = map[cyc(u_kind) < 6 ? cyc(u_kind) : 0], cmp, ask;
  cmp = k == RU_IF || k == RU_NEXTIF || k == RU_SKIPIF;
  ask = k == RU_ASK || k == RU_FILLIN;
  set(u_field, MUIA_Disabled, !cmp); set(u_op, MUIA_Disabled, !cmp); set(u_val, MUIA_Disabled, !cmp);
  set(u_then, MUIA_Disabled, k != RU_IF); set(u_else, MUIA_Disabled, k != RU_IF);
  set(u_bm, MUIA_Disabled, k != RU_ASK && k != RU_SET);
  set(u_prompt, MUIA_Disabled, !ask); set(u_once, MUIA_Disabled, !ask);
  set(u_def, MUIA_Disabled, !ask && k != RU_SET);
}
/* the window, for one of the rules that ask for settings (kind: RU_) */
static void rule_open(int kind)
{
  static const char *kinds[7];
  static const int back[RU_N] = { 0, 1, 2, 0, 0, 0, 3, 4, 5 };
  Object *c;
  int i;
  have_data(1);
  drop_win(&rulewin);
  make_cols(NULL);
  kinds[0] = tr("Ask"); kinds[1] = tr("Fill-in"); kinds[2] = tr("If...Then...Else"); kinds[3] = tr("Next Record If"); kinds[4] = tr("Set Bookmark");
  kinds[5] = tr("Skip Record If"); kinds[6] = NULL;
  for (i = 0; i < 8; i++) ops8_l[i] = tr(ops_en[i]);
  ops8_l[8] = NULL;
  c = VGroup,
    Child, HGroup, Child, FLabel(tr("Rule:")), Child, u_kind = cycle(kinds), End,
    Child, ColGroup(3), GroupFrameT(tr("Comparison")),
      Child, FLabel(tr("Field name:")), Child, FLabel(tr("Comparison:")), Child, FLabel(tr("Compare to:")),
      Child, u_field = cycle(colent), Child, u_op = cycle(ops8_l), Child, u_val = str("", 120),
    End,
    Child, ColGroup(2),
      Child, FLabel(tr("Insert this text:")), Child, u_then = str("", 250),
      Child, FLabel(tr("Otherwise insert this text:")), Child, u_else = str("", 250),
      Child, FLabel(tr("Bookmark:")), Child, u_bm = str("", 40),
      Child, FLabel(tr("Prompt:")), Child, u_prompt = str("", 200),
      Child, FLabel(tr("Default text:")), Child, u_def = str("", 200),
    End,
    Child, HGroup, Child, u_once = CheckMark(FALSE), Child, FLabel(tr("Ask once")), Child, HSpace(0), End,
  End;
  if (!(rulewin = wp_dialog("Insert Word Field", MAKE_ID('M','L','R','U'), c, "_OK", ID_ML_OK_RULE))) return;
  set(u_kind, MUIA_Cycle_Active, back[kind >= 0 && kind < RU_N ? kind : 0]);
  notify_ev(u_kind, MUIA_Cycle_Active, BT_U_KIND);
  rule_enable();
  set(rulewin, MUIA_Window_Open, TRUE);
}
/* a quoted argument in typed form (" escaped) */
static void q(Buf *b, const char *s)
{
  buf_addc(b, '"');
  for (; *s; s++) { if (*s == '"' || *s == '\\') buf_addc(b, '\\'); buf_addc(b, *s); }
  buf_addc(b, '"');
}
static void rule_ok(void)
{
  static const int map[] = { RU_ASK, RU_FILLIN, RU_IF, RU_NEXTIF, RU_SET, RU_SKIPIF };
  static const char *const sym[8] = { "=", "<>", "<", ">", "<=", ">=", "=", "<>" };
  int k = map[cyc(u_kind) < 6 ? cyc(u_kind) : 0];
  MergeInfo *m = doc()->merge;
  char val[256], th[256], el[256], bm[64], pr[256], df[256], fname[128];
  Buf b;
  u8of(u_val, val, sizeof val); u8of(u_then, th, sizeof th); u8of(u_else, el, sizeof el);
  u8of(u_bm, bm, sizeof bm); u8of(u_prompt, pr, sizeof pr); u8of(u_def, df, sizeof df);
  fname[0] = 0;
  if (m && m->data && cyc(u_field) < m->data->ncols) field_for(m->data->cols[cyc(u_field)], fname, sizeof fname);
  if ((k == RU_ASK || k == RU_SET) && !bm[0]) { DisplayBeep(NULL); return; }
  close_win(rulewin);
  buf_init(&b);
  switch (k) {
  case RU_ASK: buf_adds(&b, " ASK "); buf_adds(&b, bm); buf_addc(&b, ' '); q(&b, pr); buf_adds(&b, " \\d "); q(&b, df); if (sel(u_once)) buf_adds(&b, " \\o"); break;
  case RU_FILLIN: buf_adds(&b, " FILLIN "); q(&b, pr); buf_adds(&b, " \\d "); q(&b, df); if (sel(u_once)) buf_adds(&b, " \\o"); break;
  case RU_SET: buf_adds(&b, " SET "); buf_adds(&b, bm); buf_addc(&b, ' '); q(&b, df); break;
  default: {
    LONG op = cyc(u_op);
    buf_adds(&b, k == RU_IF ? " IF \"{ MERGEFIELD " : k == RU_NEXTIF ? " NEXTIF \"{ MERGEFIELD " : " SKIPIF \"{ MERGEFIELD ");
    buf_adds(&b, fname); buf_adds(&b, " }\" ");
    buf_adds(&b, sym[op >= 0 && op < 8 ? op : 0]); buf_addc(&b, ' ');
    q(&b, op >= MF_BLANK ? "" : val);
    if (k == RU_IF) { buf_addc(&b, ' '); q(&b, th); buf_addc(&b, ' '); q(&b, el); }
    break; }
  }
  buf_addc(&b, ' ');
  buf_addc(&b, 0);
  if (!b.oom) put_typed(b.data);
  buf_free(&b);
  back_to_doc();
}

/* ------------------------------------------------------------------ Go to Record, Find Recipient */
static Object *gotowin, *go_num, *findwin, *fi_text, *fi_where;
static void goto_open(void)
{
  if (!have_data(0)) return;
  if (!gotowin) {
    Object *c = HGroup, Child, FLabel(tr("Record:")), Child, go_num = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789",
      MUIA_String_MaxLen, 7, MUIA_CycleChain, 1, End, End;
    if (!(gotowin = wp_dialog("Go to Record", MAKE_ID('M','L','G','O'), c, "_Go To", ID_ML_OK_GOTO))) return;
    DoMethod(go_num, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_ML_OK_GOTO);
  }
  { char t[16]; sprintf(t, "%d", cur_active() + 1); set(go_num, MUIA_String_Contents, (ULONG)t); }
  set(gotowin, MUIA_Window_Open, TRUE);
  set(gotowin, MUIA_Window_ActiveObject, (ULONG)go_num);
}
static void go_record(int k)
{
  Doc *d = doc();
  MergeInfo *m = d->merge;
  int n = merge_count(m), v;
  if (!n) { DisplayBeep(NULL); return; }
  if (k < 0) k = 0;
  if (k >= n) k = n - 1;
  v = merge_active(m, k);
  if (v >= 0) m->rec = v;
  merge_preview(d, 1);
  changed();
}
static void find_open(void)
{
  if (!have_data(0)) return;
  drop_win(&findwin);
  make_cols("All fields");
  {
    Object *c = ColGroup(2), Child, FLabel(tr("Find:")), Child, fi_text = str("", 100), Child, FLabel(tr("Look in:")), Child, fi_where = cycle(colent), End;
    if (!(findwin = wp_dialog("Find Entry", MAKE_ID('M','L','F','E'), c, "_Find Next", ID_ML_OK_FIND))) return;
  }
  set(findwin, MUIA_Window_Open, TRUE);
  set(findwin, MUIA_Window_ActiveObject, (ULONG)fi_text);
}
static void find_next(void)
{
  Doc *d = doc();
  MergeInfo *m = d->merge;
  char what[200];
  int v;
  u8of(fi_text, what, sizeof what);
  if (!m || !what[0]) return;
  v = merge_find(m, m->rec, what, (int)cyc(fi_where) - 1);
  if (v < 0) { wp_ask("OK", tr("No more recipients have this text."), NULL); return; }
  m->rec = v;
  if (!m->data->on[m->view[v]]) m->data->on[m->view[v]] = 1;       /* (shown, so checked, as Word does) */
  merge_preview(d, 1);
  changed();
  if (is_open(editwin)) { rl_fill(); set(r_list, MUIA_List_Active, v); }
}

/* ------------------------------------------------------------------ Check for Errors, Finish & Merge */
static Object *checkwin, *c_how, *finwin, *fin_range, *fin_from, *fin_to, *fin_pdf, *fin_name, *fin_group;
static int fin_topdf;
static const char *how_l[4], *range_l[4], *pdf_l[3];
/* errors shown in a new document */
static void report(const char *head, Buf *e)
{
  Buf t;
  Doc *r;
  buf_init(&t);
  buf_adds(&t, head); buf_addc(&t, '\n');
  if (e->len) buf_add(&t, e->data, e->len);
  buf_addc(&t, 0);
  if (!t.oom && (r = text_doc(t.data))) wp_open_doc(r, "", 1);
  buf_free(&t);
}
static void merge_new_doc(int from, int to)
{
  Doc *d = doc(), *m;
  int n = 0;
  Buf e;
  buf_init(&e);
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  m = merge_build(d, from, to, ask_prompt, NULL, &n, &e);
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  if (!m) wp_ask("OK", tr("No records were merged."), NULL);
  else wp_open_doc(m, "", 1);
  buf_free(&e);
}
static void check_open(void)
{
  if (!checkwin) {
    Object *c;
    how_l[0] = tr("Simulate the merge and report errors in a new document");
    how_l[1] = tr("Complete the merge, pausing to report each error as it occurs");
    how_l[2] = tr("Complete the merge without pausing. Report errors in a new document");
    how_l[3] = NULL;
    c = VGroup, Child, c_how = RadioObject, MUIA_Radio_Entries, (ULONG)how_l, MUIA_CycleChain, 1, End, End;
    if (!(checkwin = wp_dialog("Checking and Reporting Errors", MAKE_ID('M','L','C','E'), c, "_OK", ID_ML_OK_CHECK))) return;
  }
  set(checkwin, MUIA_Window_Open, TRUE);
}
static void check_ok(void)
{
  Doc *d = doc();
  LONG how = 0;
  Buf e;
  int n;
  get(c_how, MUIA_Radio_Active, &how);
  close_win(checkwin);
  have_data(1);
  if (d->merge) d->merge->checkerrors = (u8)(how + 1);
  buf_init(&e);
  n = merge_check(d, &e);
  if (how == 1 && n) {
    /* each error in turn, then the merge if wanted */
    char *p = e.data, *nl;
    buf_addc(&e, 0);
    for (p = e.data; p && *p; p = nl) {
      nl = strchr(p, '\n');
      if (nl) *nl++ = 0;
      if (wp_ask(tr("_Continue|_Stop"), "%s", l1(p)) != 1) { buf_free(&e); return; }
    }
  }
  if (!n) wp_ask("OK", tr("No mail merge errors have been found."), NULL);
  else if (how != 1) report(tr("Mail merge errors"), &e);
  buf_free(&e);
  if (how && d->merge && merge_count(d->merge)) merge_new_doc(0, merge_count(d->merge) - 1);
}
static void fin_open(int topdf)
{
  Object *c;
  if (!have_data(0)) return;
  fin_topdf = topdf;
  drop_win(&finwin);
  make_cols("(numbered)");
  range_l[0] = tr("All records"); range_l[1] = tr("Current record"); range_l[2] = tr("From:"); range_l[3] = NULL;
  pdf_l[0] = tr("One PDF file"); pdf_l[1] = tr("A PDF file for each record"); pdf_l[2] = NULL;
  c = VGroup,
    Child, HGroup, GroupFrameT(tr("Merge records")),
      Child, fin_range = RadioObject, MUIA_Radio_Entries, (ULONG)range_l, MUIA_CycleChain, 1, End,
      Child, VGroup, Child, VSpace(0), Child, HGroup,
        Child, fin_from = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 7, MUIA_CycleChain, 1, End,
        Child, FLabel(tr("To:")),
        Child, fin_to = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 7, MUIA_CycleChain, 1, End, End, End,
    End,
    Child, fin_group = VGroup, GroupFrameT(tr("PDF")),
      Child, fin_pdf = RadioObject, MUIA_Radio_Entries, (ULONG)pdf_l, MUIA_CycleChain, 1, End,
      Child, HGroup, Child, FLabel(tr("File names from:")), Child, fin_name = cycle(colent), End,
    End,
  End;
  if (!(finwin = wp_dialog(topdf ? "Merge to PDF" : "Merge to New Document", MAKE_ID('M','L','F','N'), c, "_OK", ID_ML_OK_FIN))) return;
  set(fin_group, MUIA_ShowMe, topdf);
  { char t[16]; sprintf(t, "%d", merge_count(doc()->merge)); set(fin_from, MUIA_String_Contents, (ULONG)"1"); set(fin_to, MUIA_String_Contents, (ULONG)t); }
  set(finwin, MUIA_Window_Open, TRUE);
}
static void fin_ok(void)
{
  Doc *d = doc();
  MergeInfo *m = d->merge;
  LONG r = 0;
  int from = 0, to, n = merge_count(m);
  char *s = NULL;
  get(fin_range, MUIA_Radio_Active, &r);
  to = n - 1;
  if (r == 1) from = to = cur_active();
  else if (r == 2) {
    get(fin_from, MUIA_String_Contents, &s); from = s && atoi(s) > 0 ? atoi(s) - 1 : 0;
    get(fin_to, MUIA_String_Contents, &s); to = s && atoi(s) > 0 ? atoi(s) - 1 : n - 1;
  }
  close_win(finwin);
  if (!fin_topdf) { merge_new_doc(from, to); return; }
  {
    char path[512];
    const char *dp = wp_docpath(), *dot;
    LONG each = 0;
    int files = 0;
    Buf e;
    get(fin_pdf, MUIA_Radio_Active, &each);
    snprintf(path, sizeof path, "%s", dp && dp[0] ? dp : "Merged.docx");
    if ((dot = strrchr(path, '.'))) strcpy(path + (dot - path), ".pdf");
    if (!wp_file_request(1, "Merge to PDF", path, sizeof path, "#?.pdf")) return;
    buf_init(&e);
    set(wp_app(), MUIA_Application_Sleep, TRUE);
    merge_to_pdf(d, from, to, (int)each, each ? (int)cyc(fin_name) - 1 : -1, path, wp_fonts(), ask_prompt, NULL, &files, &e);
    set(wp_app(), MUIA_Application_Sleep, FALSE);
    if (!files) wp_ask("OK", tr("Quill could not write\n%s"), path);
    else { char t[32]; sprintf(t, "%d", files); wp_ask("OK", tr("PDF files written: %s"), t); }
    buf_free(&e);
  }
}

/* ------------------------------------------------------------------ the Step-by-Step Mail Merge wizard */
static Object *wizwin, *w_pages, *w_type, *w_step, *w_recip, *w_rec;
static int w_page;
static const char *w_type_l[5];
static void wiz_show(void)
{
  MergeInfo *m = doc()->merge;
  char t[120];
  sprintf(t, tr("Step %d of 6"), w_page + 1);
  set(w_step, MUIA_Text_Contents, (ULONG)t);
  set(w_pages, MUIA_Group_ActivePage, w_page);
  sprintf(t, tr("%d of %d recipients checked"), merge_count(m), m && m->data ? m->data->nrows : 0);
  set(w_recip, MUIA_Text_Contents, (ULONG)t);
  sprintf(t, "%s %d", tr("Recipient"), cur_active() + 1);
  set(w_rec, MUIA_Text_Contents, (ULONG)t);
}
static Object *wbtn(const char *label, ULONG id)
{
  Object *b = QButton(tr(label));
  if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id);
  return b;
}
static void wiz_open(void)
{
  if (!wizwin) {
    Object *c;
    w_type_l[0] = tr("Letters"); w_type_l[1] = tr("Envelopes"); w_type_l[2] = tr("Labels"); w_type_l[3] = tr("Directory"); w_type_l[4] = NULL;
    c = VGroup,
      Child, w_step = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, w_pages = VGroup, MUIA_Group_PageMode, TRUE,
        Child, VGroup, Child, FLabel(tr("Select document type")), Child, w_type = RadioObject, MUIA_Radio_Entries, (ULONG)w_type_l, MUIA_CycleChain, 1, End, Child, VSpace(0), End,
        Child, VGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Use the current document as the starting document."), End,
          Child, HGroup, Child, wbtn("Envelope _Options...", ID_ML_ST_ENV), Child, wbtn("_Label Options...", ID_ML_ST_LAB), End, Child, VSpace(0), End,
        Child, VGroup, Child, FLabel(tr("Select recipients")),
          Child, wbtn("Use an _existing list...", ID_ML_EXISTING), Child, wbtn("_Type a new list...", ID_ML_NEWLIST),
          Child, wbtn("_Edit recipient list...", ID_ML_EDITLIST), Child, w_recip = TextObject, MUIA_Text_Contents, (ULONG)"", End, Child, VSpace(0), End,
        Child, VGroup, Child, FLabel(tr("Write your letter: put the caret where each item goes")),
          Child, wbtn("_Address block...", ID_ML_ADDRESS), Child, wbtn("_Greeting line...", ID_ML_GREETING),
          Child, wbtn("_More items...", ID_ML_INSFIELDWIN), Child, wbtn("_Update all labels", ID_ML_UPDLABELS), Child, VSpace(0), End,
        Child, VGroup, Child, FLabel(tr("Preview your letters")),
          Child, HGroup, Child, wbtn("<<", ID_ML_PREV), Child, w_rec = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_PreParse, (ULONG)"\33c", End, Child, wbtn(">>", ID_ML_NEXT), End,
          Child, wbtn("_Find a recipient...", ID_ML_FIND), Child, btn("E_xclude this recipient", BT_W_EXCLUDE), Child, VSpace(0), End,
        Child, VGroup, Child, FLabel(tr("Complete the merge")),
          Child, wbtn("Edit _individual documents...", ID_ML_FIN_DOC), Child, wbtn("Merge to _PDF...", ID_ML_FIN_PDF), Child, VSpace(0), End,
      End,
      Child, HGroup, Child, btn("_Previous", BT_W_BACK), Child, HSpace(0), Child, btn("_Next", BT_W_NEXT), End,
    End;
    if (!(wizwin = wp_dialog("Mail Merge", MAKE_ID('M','L','W','Z'), c, "_Close", ID_ML_OK_WIZ))) return;
  }
  w_page = 0;
  { MergeInfo *m = doc()->merge; int t = m ? m->type : MT_LETTERS; set(w_type, MUIA_Radio_Active, t == MT_ENVELOPES ? 1 : t == MT_LABELS ? 2 : t == MT_DIRECTORY ? 3 : 0); }
  wiz_show();
  set(wizwin, MUIA_Window_Open, TRUE);
}
static void wiz_step(int dir)
{
  MergeInfo *m;
  if (w_page == 0 && dir > 0) {
    LONG t = 0;
    get(w_type, MUIA_Radio_Active, &t);
    if ((m = mi())) m->type = (u8)(t == 1 ? MT_ENVELOPES : t == 2 ? MT_LABELS : t == 3 ? MT_DIRECTORY : MT_LETTERS);
    wp_refresh();
  }
  if (w_page == 3 && dir > 0) { if (have_data(1)) { merge_preview(doc(), 1); changed(); } }
  if (w_page == 4 && dir < 0) { merge_preview(doc(), 0); changed(); }
  w_page += dir;
  if (w_page < 0) w_page = 0;
  if (w_page > 5) w_page = 5;
  wiz_show();
}

/* ------------------------------------------------------------------ the commands */
static void start_type(int t)
{
  MergeInfo *m = mi();
  if (!m) return;
  if (t == MT_ENVELOPES) { env_open(1); return; }
  if (t == MT_LABELS) { lab_open(1); return; }
  if (t == MT_NONE) {
    /* a normal document again: no recipients, the fields stay */
    if (m->viewmerged) merge_preview(doc(), 0);
    merge_free(doc());
    changed();
    return;
  }
  m->type = (u8)t;
  wp_refresh();
}
static void select_existing(void)
{
  char path[512], err[160];
  MergeData *md;
  MergeInfo *m;
  const char *dot;
  m = mi();
  if (!m) return;
  snprintf(path, sizeof path, "%s", m->src);
  if (!wp_file_request(0, "Select Data Source", path, sizeof path, "#?.(csv|txt|tab|xlsx|docx)")) return;
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  md = md_load_file(path, err, sizeof err);
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  if (!md) { wp_ask("OK", "%s", err); return; }
  snprintf(m->src, sizeof m->src, "%s", path);
  m->dtype[0] = 0; m->connect[0] = 0;
  dot = strrchr(path, '.');
  if (dot && str_ieq(dot, ".xlsx")) strcpy(m->table, "Sheet1$"); else m->table[0] = 0;
  m->nfilt = 0; memset(m->sortcol, 0, sizeof m->sortcol); memset(m->map, 0, sizeof m->map);
  if (m->type == MT_NONE) m->type = MT_LETTERS;
  merge_set_data(doc(), md);
  preview_again();
  wp_refresh();
  if (is_open(wizwin)) wiz_show();
}
static void navigate(ULONG id)
{
  Doc *d = doc();
  MergeInfo *m = d->merge;
  int k, n;
  if (!have_data(0)) return;
  n = merge_count(m);
  k = cur_active();
  if (id == ID_ML_FIRST) k = 0;
  else if (id == ID_ML_LAST) k = n - 1;
  else if (id == ID_ML_PREV) k--;
  else k++;
  go_record(k);
  if (is_open(wizwin)) wiz_show();
}
static void button(LONG code)
{
  Doc *d = doc();
  MergeInfo *m = d->merge;
  LONG a = -1;
  switch (code) {
  case BT_E_ADD: env_done(1); break;
  case BT_L_VENDOR: lab_fill(); lab_info(); break;
  case BT_L_PRODUCT: lab_info(); break;
  case BT_N_PICK: get(n_list, MUIA_List_Active, &a); if (a != n_cur) { n_store(); n_show((int)a); } break;
  case BT_N_NEW:
    n_store();
    if (n_md && md_add_row(n_md, NULL) >= 0) { DoMethod(n_list, MUIM_List_InsertSingle, (ULONG)n_md->nrows, MUIV_List_Insert_Bottom); set(n_list, MUIA_List_Active, n_md->nrows - 1); n_show(n_md->nrows - 1); }
    break;
  case BT_N_DEL:
    if (n_md && n_cur >= 0 && n_md->nrows > 1) { md_delete_row(n_md, n_cur); n_fill(); set(n_list, MUIA_List_Active, 0); n_show(0); }
    break;
  case BT_N_COL: {
    char nm[64];
    u8of(n_colname, nm, sizeof nm);
    if (nm[0] && n_md && md_col(n_md, nm) < 0) {
      MergeData *keep;
      n_store();
      md_add_col(n_md, nm);
      keep = n_md; n_md = NULL;
      new_open(keep, n_editing);         /* (the window again, with the new column) */
    }
    break; }
  case BT_R_TOGGLE:
    get(r_list, MUIA_List_Active, &a);
    if (m && m->data && a >= 0 && a < m->nview) { m->data->on[m->view[a]] ^= 1; DoMethod(r_list, MUIM_List_Redraw, a); rl_fill(); set(r_list, MUIA_List_Active, a); }
    break;
  case BT_R_ALL: case BT_R_NONE:
    if (m && m->data) { int i; for (i = 0; i < m->data->nrows; i++) m->data->on[i] = code == BT_R_ALL; rl_fill(); }
    break;
  case BT_R_FILTER: filt_open(); break;
  case BT_R_REFRESH:
    if (m && m->src[0]) { md_free(m->data); m->data = NULL; m->loaded = 0; have_data(0); rl_fill(); }
    break;
  case BT_R_DUP:
    if (m && m->data) {
      u8 *dup = (u8 *)malloc((size_t)m->data->nrows + 1);
      int n = dup ? merge_duplicates(m->data, dup) : 0, i;
      char t[16];
      sprintf(t, "%d", n);
      if (n && wp_ask(tr("_Uncheck Them|_Keep Them"), tr("Duplicate recipients found: %s"), t) == 1) for (i = 0; i < m->data->nrows; i++) if (dup[i]) m->data->on[i] = 0;
      if (!n) wp_ask("OK", tr("Duplicate recipients found: %s"), t);
      free(dup);
      rl_fill();
    }
    break;
  case BT_R_FIND: find_open(); break;
  case BT_R_VALID:
    if (m && m->data) {
      u8 *bad = (u8 *)malloc((size_t)m->data->nrows + 1);
      int n = bad ? merge_validate(m, bad) : 0, i;
      char t[16];
      sprintf(t, "%d", n);
      if (!n) wp_ask("OK", tr("Every recipient has a name and a full address."), NULL);
      else if (wp_ask(tr("_Uncheck Them|_Keep Them"), tr("Recipients without a full address: %s"), t) == 1) for (i = 0; i < m->data->nrows; i++) if (bad[i]) m->data->on[i] = 0;
      free(bad);
      rl_fill();
    }
    break;
  case BT_R_EDIT:
    if (m && m->data) {
      const char *dot = strrchr(m->src, '.');
      MergeData *cp;
      int r;
      if (dot && (str_ieq(dot, ".xlsx") || str_ieq(dot, ".docx"))) { wp_ask("OK", tr("This list is edited in its own program; Quill edits text (CSV) lists."), NULL); break; }
      if (!(cp = md_new(m->data->ncols, (const char *const *)m->data->cols))) break;
      for (r = 0; r < m->data->nrows; r++) { md_add_row(cp, (const char *const *)m->data->cell[r]); }
      for (r = 0; r < m->data->nrows && r < cp->nrows; r++) cp->on[r] = m->data->on[r];
      new_open(cp, 1);
    }
    break;
  case BT_F_CLEAR: { int i; for (i = 0; i < MERGE_NFILT; i++) { set(f_col[i], MUIA_Cycle_Active, 0); set(f_val[i], MUIA_String_Contents, (ULONG)""); } for (i = 0; i < MERGE_NSORT; i++) set(f_scol[i], MUIA_Cycle_Active, 0); break; }
  case BT_I_KIND: ins_fill(); break;
  case BT_A_CHANGE: addr_preview(); greet_preview(); break;
  case BT_A_PREV: case BT_A_NEXT:
    a_rec += code == BT_A_NEXT ? 1 : -1;
    if (a_rec >= merge_count(m)) a_rec = merge_count(m) - 1;
    if (a_rec < 0) a_rec = 0;
    addr_preview(); greet_preview();
    break;
  case BT_A_MATCH: match_open(); break;
  case BT_U_KIND: rule_enable(); break;
  case BT_W_BACK: wiz_step(-1); break;
  case BT_W_NEXT: wiz_step(1); break;
  case BT_W_EXCLUDE:
    if (m && m->data && m->rec < m->nview) { m->data->on[m->view[m->rec]] = 0; navigate(ID_ML_NEXT); }
    break;
  }
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp4_command(ULONG id, LONG value)
{
  Doc *d = doc();
  if (!d) return WPR_DONE;
  if (d->edit.kind) wp_story_close();
  switch (id) {
  case ID_ML_ENVELOPES: env_open(0); return WPR_KEEPFOCUS;
  case ID_ML_LABELS: lab_open(0); return WPR_KEEPFOCUS;
  case ID_ML_START:
    if (value == 5) { wiz_open(); return WPR_KEEPFOCUS; }
    start_type(value == 0 ? MT_LETTERS : value == 1 ? MT_ENVELOPES : value == 2 ? MT_LABELS : value == 3 ? MT_DIRECTORY : MT_NONE);
    return value == 1 || value == 2 ? WPR_KEEPFOCUS : WPR_DONE;
  case ID_ML_ST_LETTERS: start_type(MT_LETTERS); return WPR_DONE;
  case ID_ML_ST_ENV: start_type(MT_ENVELOPES); return WPR_KEEPFOCUS;
  case ID_ML_ST_LAB: start_type(MT_LABELS); return WPR_KEEPFOCUS;
  case ID_ML_ST_DIR: start_type(MT_DIRECTORY); return WPR_DONE;
  case ID_ML_ST_NORMAL: start_type(MT_NONE); return WPR_DONE;
  case ID_ML_WIZARD: wiz_open(); return WPR_KEEPFOCUS;
  case ID_ML_RECIP: if (value == 0) { new_open(md_new(md_nnew_cols, md_new_cols), 0); return WPR_KEEPFOCUS; } select_existing(); return WPR_DONE;
  case ID_ML_NEWLIST: new_open(md_new(md_nnew_cols, md_new_cols), 0); return WPR_KEEPFOCUS;
  case ID_ML_EXISTING: select_existing(); return WPR_DONE;
  case ID_ML_EDITLIST: edit_open(); return WPR_KEEPFOCUS;
  case ID_ML_FILTER: filt_open(); return WPR_KEEPFOCUS;
  case ID_ML_HIGHLIGHT: { MergeInfo *m = mi(); if (m) { m->highlight = !m->highlight; changed(); } return WPR_DONE; }
  case ID_ML_ADDRESS: addr_open(); return WPR_KEEPFOCUS;
  case ID_ML_GREETING: greet_open(); return WPR_KEEPFOCUS;
  case ID_ML_INSFIELD: {
    MergeInfo *m = d->merge;
    if (value < 0) { ins_open(); return WPR_KEEPFOCUS; }
    if (m && m->data) { if (value < m->data->ncols) insert_column(m->data->cols[value]); }
    else if (value + 1 < AF_N) insert_column(af_names[value + 1]);
    return WPR_DONE; }
  case ID_ML_INSFIELDWIN: ins_open(); return WPR_KEEPFOCUS;
  case ID_ML_RULES:
    if (value == RU_MERGEREC) put_code(" MERGEREC ");
    else if (value == RU_MERGESEQ) put_code(" MERGESEQ ");
    else if (value == RU_NEXT) put_code(" NEXT ");
    else { rule_open(value >= 0 ? (int)value : RU_IF); return WPR_KEEPFOCUS; }
    return WPR_DONE;
  case ID_ML_RULESWIN: rule_open(RU_IF); return WPR_KEEPFOCUS;
  case ID_ML_MATCH: match_open(); return WPR_KEEPFOCUS;
  case ID_ML_UPDLABELS: if (label_update(d)) { preview_again(); changed(); } else DisplayBeep(NULL); return WPR_DONE;
  case ID_ML_PREVIEW:
    if (d->merge && d->merge->viewmerged) merge_preview(d, 0);
    else if (have_data(0)) merge_preview(d, 1);
    changed();
    return WPR_DONE;
  case ID_ML_FIRST: case ID_ML_PREV: case ID_ML_NEXT: case ID_ML_LAST: navigate(id); return WPR_DONE;
  case ID_ML_GOTO: goto_open(); return WPR_KEEPFOCUS;
  case ID_ML_FIND: find_open(); return WPR_KEEPFOCUS;
  case ID_ML_CHECK: check_open(); return WPR_KEEPFOCUS;
  case ID_ML_FINISH: fin_open(value == 1); return WPR_KEEPFOCUS;
  case ID_ML_FIN_DOC: fin_open(0); return WPR_KEEPFOCUS;
  case ID_ML_FIN_PDF: fin_open(1); return WPR_KEEPFOCUS;
  case ID_ML_BTN: case ID_ML_EV: { LONG c = ml_code; ml_code = 0; button(c); return WPR_KEEPFOCUS; }
  case ID_ML_OK_ENV: env_done(0); return WPR_DONE;
  case ID_ML_OK_LAB: lab_done(); return WPR_DONE;
  case ID_ML_OK_NEW: new_ok(); return WPR_DONE;
  case ID_ML_OK_EDIT: close_win(editwin); preview_again(); back_to_doc(); return WPR_DONE;
  case ID_ML_OK_FILT: filt_ok(); return WPR_DONE;
  case ID_ML_OK_INS: ins_ok(); close_win(inswin); back_to_doc(); return WPR_DONE;
  case ID_ML_OK_ADDR: { char code[700]; addr_code(code, sizeof code); close_win(addrwin); put_code(code); back_to_doc(); return WPR_DONE; }
  case ID_ML_OK_GREET: { char code[700]; greet_code(code, sizeof code); close_win(greetwin); put_code(code); back_to_doc(); return WPR_DONE; }
  case ID_ML_OK_MATCH: match_ok(); return WPR_DONE;
  case ID_ML_OK_RULE: rule_ok(); return WPR_DONE;
  case ID_ML_OK_GOTO: { char *s = NULL; get(go_num, MUIA_String_Contents, &s); close_win(gotowin); go_record(s ? atoi(s) - 1 : 0); back_to_doc(); return WPR_DONE; }
  case ID_ML_OK_FIND: find_next(); return WPR_KEEPFOCUS;
  case ID_ML_OK_CHECK: check_ok(); return WPR_DONE;
  case ID_ML_OK_FIN: fin_ok(); return WPR_DONE;
  case ID_ML_OK_WIZ: close_win(wizwin); back_to_doc(); return WPR_DONE;
  }
  return WPR_DONE;
}

/* ------------------------------------------------------------------ the ribbon and menus */
#define ICON_ENV "ci R 1 4 14 12 L 1 4 8 9 L 14 4 8 9 ca F 11 5 13 6"
#define ICON_LAB "ci R 1 1 14 14 ca R 3 3 7 6 R 9 3 13 6 R 3 9 7 12 R 9 9 13 12"
#define ICON_START "ci R 3 1 12 14 L 5 4 10 4 L 5 6 10 6 ca R 6 9 13 15 L 7 11 12 11 L 7 13 12 13"
#define ICON_RECIP "ci O 6 5 2 P 5 2 14 3 10 6 9 9 10 10 14 ca O 11 6 2 P 3 8 14 9 11 11 10 14 14"
#define ICON_EDITREC "ci R 1 2 14 13 L 1 5 14 5 L 1 8 14 8 L 1 11 14 11 L 5 2 5 13 ca L 2 3 4 4"
#define ICON_HIGHLIGHT "ci R 1 5 14 11 cl F 2 6 13 10 ct L 4 8 11 8"
#define ICON_ADDR "ci R 2 2 13 13 ca L 4 5 11 5 L 4 8 9 8 L 4 11 10 11"
#define ICON_GREET "ci R 2 4 13 12 ca P 4 4 10 5 8 7 8 8 10"
#define ICON_INSFLD "ci P 7 5 1 4 2 4 7 2 8 4 9 4 14 5 15 P 7 10 1 11 2 11 7 13 8 11 9 11 14 10 15 ca L 6 8 9 8 L 7 6 7 10"
#define ICON_RULES "ci P 3 2 2 8 8 2 14 P 3 14 2 8 8 14 14 ca F 7 7 8 9"
#define ICON_MATCH "ci R 1 2 6 5 R 1 10 6 13 R 10 2 15 5 R 10 10 15 13 ca L 6 3 10 11 L 6 11 10 3"
#define ICON_UPDLAB "ci R 1 1 14 14 ca P 5 4 8 4 4 8 4 11 7 L 8 8 11 8"
#define ICON_PREVIEW "ci R 1 2 14 13 ca B ABC"
#define ICON_FIRST "ci L 3 3 3 12 P 4 12 3 6 8 12 12 12 3"
#define ICON_LAST "ci L 12 3 12 12 P 4 3 3 9 8 3 12 3 3"
#define ICON_GOTO "ci R 1 3 14 12 ca L 4 8 11 8 L 8 5 11 8 L 8 11 11 8"
#define ICON_FINDREC "ci O 6 6 4 L 9 9 14 14 ca O 6 5 1"
#define ICON_CHECK "ci R 2 1 13 14 ca P 3 4 8 7 11 11 4"
#define ICON_FINISH "ci R 3 1 12 14 ca P 3 5 8 7 11 11 5 L 5 13 10 13"
static const char *const start_en[6] = { "Letters", "Envelopes...", "Labels...", "Directory", "Normal Word Document", "Step-by-Step Mail Merge Wizard..." };
static const char *start_l[6];
static const RibbonList start_rl = { start_l, 6, PL_TEXT, NULL };
static const char *const recip_en[2] = { "Type a New List...", "Use an Existing List..." };
static const char *recip_l[2];
static const RibbonList recip_rl = { recip_l, 2, PL_TEXT, NULL };
static const char *const fin_en[2] = { "Edit Individual Documents...", "Merge to PDF..." };
static const char *fin_l[2];
static const RibbonList fin_rl = { fin_l, 2, PL_TEXT, NULL };
static const Item create_items[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Envelopes", ID_ML_ENVELOPES, 0, 0, 0, 0, "An envelope with a delivery and return address", NULL, ICON_ENV },
  { K_BUTTON, S_SMALL, 0, 0, "Labels", ID_ML_LABELS, 0, 0, 0, 0, "A page of address labels (Avery and others)", NULL, ICON_LAB } };
static const Item start_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Start Mail Merge", ID_ML_START, 0, 0, 0, 0, "Letters, envelopes, labels or a directory for many recipients", &start_rl, ICON_START },
  { K_DROP, S_SMALL, 0, 0, "Select Recipients", ID_ML_RECIP, 0, 0, 0, 0, "The list of people to merge: a new one, or a CSV, spreadsheet or document file", &recip_rl, ICON_RECIP },
  { K_BUTTON, S_SMALL, 0, 0, "Edit Recipient List", ID_ML_EDITLIST, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "Check, sort, filter and find recipients", NULL, ICON_EDITREC } };
static const Item write_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Highlight Merge Fields", ID_ML_HIGHLIGHT, RB_WP(WPI_4, ST_HIGHLIGHT), 0, 0, 0, "The merge fields on grey", NULL, ICON_HIGHLIGHT },
  { K_BUTTON, S_SMALL, 0, 0, "Address Block", ID_ML_ADDRESS, 0, 0, F_NEWCOL, 0, "The recipient's name and address", NULL, ICON_ADDR },
  { K_BUTTON, S_SMALL, 0, 0, "Greeting Line", ID_ML_GREETING, 0, 0, 0, 0, "Dear Mr Randall, and the like", NULL, ICON_GREET },
  { K_DROP, S_SMALL, 0, 0, "Insert Merge Field", ID_ML_INSFIELD, 0, 0, 0, 0, "A column of the recipient list at the caret", &field_rl, ICON_INSFLD },
  { K_DROP, S_SMALL, 0, 0, "Rules", ID_ML_RULES, 0, 0, F_NEWCOL, 0, "Ask, Fill-in, If...Then...Else, Next Record, Skip Record If...", &rule_rl, ICON_RULES },
  { K_BUTTON, S_SMALL, 0, 0, "Match Fields", ID_ML_MATCH, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "Which column holds the name, the address...", NULL, ICON_MATCH },
  { K_BUTTON, S_SMALL, 0, 0, "Update Labels", ID_ML_UPDLABELS, 0, RB_WP(WPI_4, ST_LABELS), 0, 0, "The first label's fields copied into all labels", NULL, ICON_UPDLAB } };
static const Item preview_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Preview Results", ID_ML_PREVIEW, RB_WP(WPI_4, ST_INPREVIEW), RB_WP(WPI_4, ST_MAIN), 0, 0, "The merge fields show a recipient's data", NULL, ICON_PREVIEW },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_ML_FIRST, 0, RB_WP(WPI_4, ST_DATA), F_NEWCOL, 0, "First Record", NULL, ICON_FIRST },
  { K_BUTTON, S_ICON, IC_PREV, 0, NULL, ID_ML_PREV, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "Previous Record" },
  { K_BUTTON, S_ICON, IC_NEXT, 0, NULL, ID_ML_NEXT, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "Next Record" },
  { K_BUTTON, S_ICON, 0, 0, NULL, ID_ML_LAST, 0, RB_WP(WPI_4, ST_DATA), F_NEWCOL, 0, "Last Record", NULL, ICON_LAST },
  { K_BUTTON, S_SMALL, 0, 0, "Go to Record", ID_ML_GOTO, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "Show the recipient with this number", NULL, ICON_GOTO },
  { K_BUTTON, S_SMALL, 0, 0, "Find Recipient", ID_ML_FIND, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "Show the next recipient with some text", NULL, ICON_FINDREC },
  { K_BUTTON, S_SMALL, 0, 0, "Check for Errors", ID_ML_CHECK, 0, RB_WP(WPI_4, ST_MAIN), F_NEWCOL, 0, "Fields without data, a missing list...", NULL, ICON_CHECK } };
static const Item finish_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Finish & Merge", ID_ML_FINISH, 0, RB_WP(WPI_4, ST_DATA), 0, 0, "A new document with a copy for each recipient, or PDF files", &fin_rl, ICON_FINISH } };
static const WpGroup groups[] = {
  { TAB_MAILINGS, -1, { "Create", G_COLS, create_items, 2 } },
  { TAB_MAILINGS, -1, { "Start Mail Merge", G_COLS, start_items, 3 } },
  { TAB_MAILINGS, -1, { "Write & Insert Fields", G_COLS, write_items, 7 } },
  { TAB_MAILINGS, -1, { "Preview Results", G_COLS, preview_items, 8 } },
  { TAB_MAILINGS, -1, { "Finish", G_COLS, finish_items, 1 } } };
static const WpMenu menus[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Mailings", 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Envelopes...", 0, 0, 0, (APTR)ID_ML_ENVELOPES } },
  { WM_FILE, { NM_SUB, (STRPTR)"Labels...", 0, 0, 0, (APTR)ID_ML_LABELS } },
  { WM_FILE, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Mail Merge: Letters", 0, 0, 0, (APTR)ID_ML_ST_LETTERS } },
  { WM_FILE, { NM_SUB, (STRPTR)"Mail Merge: Envelopes...", 0, 0, 0, (APTR)ID_ML_ST_ENV } },
  { WM_FILE, { NM_SUB, (STRPTR)"Mail Merge: Labels...", 0, 0, 0, (APTR)ID_ML_ST_LAB } },
  { WM_FILE, { NM_SUB, (STRPTR)"Mail Merge: Directory", 0, 0, 0, (APTR)ID_ML_ST_DIR } },
  { WM_FILE, { NM_SUB, (STRPTR)"Normal Word Document", 0, 0, 0, (APTR)ID_ML_ST_NORMAL } },
  { WM_FILE, { NM_SUB, (STRPTR)"Step-by-Step Mail Merge Wizard...", 0, 0, 0, (APTR)ID_ML_WIZARD } },
  { WM_FILE, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Type a New List...", 0, 0, 0, (APTR)ID_ML_NEWLIST } },
  { WM_FILE, { NM_SUB, (STRPTR)"Use an Existing List...", 0, 0, 0, (APTR)ID_ML_EXISTING } },
  { WM_FILE, { NM_SUB, (STRPTR)"Edit Recipient List...", 0, 0, 0, (APTR)ID_ML_EDITLIST } },
  { WM_FILE, { NM_SUB, (STRPTR)"Filter and Sort...", 0, 0, 0, (APTR)ID_ML_FILTER } },
  { WM_FILE, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Highlight Merge Fields", 0, 0, 0, (APTR)ID_ML_HIGHLIGHT } },
  { WM_FILE, { NM_SUB, (STRPTR)"Address Block...", 0, 0, 0, (APTR)ID_ML_ADDRESS } },
  { WM_FILE, { NM_SUB, (STRPTR)"Greeting Line...", 0, 0, 0, (APTR)ID_ML_GREETING } },
  { WM_FILE, { NM_SUB, (STRPTR)"Insert Merge Field...", 0, 0, 0, (APTR)ID_ML_INSFIELDWIN } },
  { WM_FILE, { NM_SUB, (STRPTR)"Rules...", 0, 0, 0, (APTR)ID_ML_RULESWIN } },
  { WM_FILE, { NM_SUB, (STRPTR)"Match Fields...", 0, 0, 0, (APTR)ID_ML_MATCH } },
  { WM_FILE, { NM_SUB, (STRPTR)"Update Labels", 0, 0, 0, (APTR)ID_ML_UPDLABELS } },
  /* (Intuition takes at most 31 items in a submenu: the rest in a second one) */
  { WM_FILE, { NM_ITEM, (STRPTR)"Mailings: Preview and Finish", 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Preview Results", 0, 0, 0, (APTR)ID_ML_PREVIEW } },
  { WM_FILE, { NM_SUB, (STRPTR)"First Record", 0, 0, 0, (APTR)ID_ML_FIRST } },
  { WM_FILE, { NM_SUB, (STRPTR)"Previous Record", 0, 0, 0, (APTR)ID_ML_PREV } },
  { WM_FILE, { NM_SUB, (STRPTR)"Next Record", 0, 0, 0, (APTR)ID_ML_NEXT } },
  { WM_FILE, { NM_SUB, (STRPTR)"Last Record", 0, 0, 0, (APTR)ID_ML_LAST } },
  { WM_FILE, { NM_SUB, (STRPTR)"Go to Record...", 0, 0, 0, (APTR)ID_ML_GOTO } },
  { WM_FILE, { NM_SUB, (STRPTR)"Find Recipient...", 0, 0, 0, (APTR)ID_ML_FIND } },
  { WM_FILE, { NM_SUB, (STRPTR)"Check for Errors...", 0, 0, 0, (APTR)ID_ML_CHECK } },
  { WM_FILE, { NM_SUB, (STRPTR)"Edit Individual Documents...", 0, 0, 0, (APTR)ID_ML_FIN_DOC } },
  { WM_FILE, { NM_SUB, (STRPTR)"Merge to PDF...", 0, 0, 0, (APTR)ID_ML_FIN_PDF } } };

static void state(ULONG *on, const WpCtx *c)
{
  MergeInfo *m = c->doc ? c->doc->merge : NULL;
  if (c->doc && c->doc != doc()) return;
  field_list_update();
  if (!m) return;
  if (m->viewmerged) *on |= 1UL << ST_INPREVIEW;
  if (m->highlight) *on |= 1UL << ST_HIGHLIGHT;
  if (m->type != MT_NONE) *on |= 1UL << ST_MAIN;
  if (m->data || (m->src[0] && !m->loaded)) *on |= 1UL << ST_DATA;
  if (m->type == MT_LABELS) *on |= 1UL << ST_LABELS;
}
static void init(void)
{
  int i;
  for (i = 0; i < 6; i++) start_l[i] = start_en[i];
  for (i = 0; i < 2; i++) { recip_l[i] = recip_en[i]; fin_l[i] = fin_en[i]; }
  for (i = 0; i < RU_N; i++) rule_l[i] = rule_en[i];
}
static void cleanup(void)
{
  md_free(n_md);
  n_md = NULL;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp4_package = {
  "WP4", ID_WP4_FIRST, ID_WP4_LAST,
  wp4_command, state, NULL, NULL, NULL, init, cleanup,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};
