/*
 * wp_compare.c - WP6 Compare and Combine (docs/wordcraft-gap.md). This file belongs to WP6: only that
 * package edits it. Its commands are ID_WP6_FIRST .. ID_WP6_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Review > Compare > Compare / Combine: Word's Compare Documents and Combine Documents window (the
 * original and revised documents, whose name the changes go under, the comparison settings) and the
 * result, made by core/compare.c, in a new window.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <utility/date.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "theme.h"
#include "../core/compare.h"

#undef Label1
#undef Label2
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

const char *wp_username(void);           /* (main.c) */
const char *wp_docpath(void);
int wp_open_doc(Doc *d, const char *path, int changed);

enum { ID_CMP_MENU = ID_WP6_FIRST, ID_CMP_COMPARE, ID_CMP_COMBINE, ID_CMP_GO, ID_CMP_ORIG, ID_CMP_REV, ID_CMP_SWAP };

static int combining;                    /* the window is Combine's */
static Object *cwin, *c_orig, *c_rev, *c_label, *c_labeltxt, *c_level, *c_cb[9], *c_go;
static CompareOpts opts;
static const char *levels[3];
static const char *const cb_names[9] = { "Insertions and deletions", "Moves", "Comments", "Formatting", "Case changes",
                                         "White space", "Tables", "Headers and footers", "Footnotes and endnotes" };

static void bt(Object *o, ULONG id) { if (o) DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id); }
static LONG getv(Object *o, ULONG attr) { LONG v = 0; if (o) get(o, attr, &v); return v; }
static const char *gets_(Object *o) { STRPTR s = NULL; if (o) get(o, MUIA_String_Contents, &s); return s ? (const char *)s : ""; }

static void open_cmp(int combine)
{
  combining = combine;
  if (!cwin) {
    Object *bo, *br, *sw, *cancel;
    int i;
    compare_defaults(&opts);
    levels[0] = tr("Character level"); levels[1] = tr("Word level"); levels[2] = NULL;
    cwin = QWindowObject, MUIA_Window_Title, (ULONG)"", MUIA_Window_ID, MAKE_ID('W','6','C','M'),
      WindowContents, VGroup, QDLG,
        Child, ColGroup(3),
          Child, Label2(tr("Original document")),
          Child, c_orig = StringObject, StringFrame, MUIA_String_MaxLen, 255, MUIA_CycleChain, 1, End,
          Child, bo = QButton(tr("_Browse...")),
          Child, Label2(tr("Revised document")),
          Child, c_rev = StringObject, StringFrame, MUIA_String_MaxLen, 255, MUIA_CycleChain, 1, End,
          Child, br = QButton(tr("B_rowse...")),
          Child, c_labeltxt = Label2(tr("Label changes with")),
          Child, c_label = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
          Child, sw = QButton(tr("S_wap")),
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33bComparison settings"), End,
        Child, ColGroup(4),
          Child, c_cb[0] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[0])),
          Child, c_cb[6] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[6])),
          Child, c_cb[1] = CheckMark(FALSE), Child, LLabel1(tr(cb_names[1])),
          Child, c_cb[7] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[7])),
          Child, c_cb[2] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[2])),
          Child, c_cb[8] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[8])),
          Child, c_cb[3] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[3])),
          Child, HSpace(0), Child, HSpace(0),
          Child, c_cb[4] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[4])),
          Child, HSpace(0), Child, HSpace(0),
          Child, c_cb[5] = CheckMark(TRUE), Child, LLabel1(tr(cb_names[5])),
          Child, HSpace(0), Child, HSpace(0),
        End,
        Child, HGroup,
          Child, Label1(tr("Show changes at")), Child, c_level = CycleObject, MUIA_Cycle_Entries, (ULONG)levels, MUIA_Cycle_Active, 1, MUIA_CycleChain, 1, End,
          Child, HSpace(0),
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33iThe changes are shown in a new document."), End,
        Child, HGroup, Child, c_go = QButton(tr("_OK")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
      End, End;
    if (!cwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)cwin);
    DoMethod(cwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)cwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)cwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    bt(c_go, ID_CMP_GO); bt(bo, ID_CMP_ORIG); bt(br, ID_CMP_REV); bt(sw, ID_CMP_SWAP);
    set(c_cb[0], MUIA_Disabled, TRUE);      /* (always) */
    set(c_cb[1], MUIA_Disabled, TRUE);      /* (Quill doesn't find moves: they show as deleted and inserted) */
    for (i = 0; i < 9; i++) set(c_cb[i], MUIA_CycleChain, 1);
  }
  set(cwin, MUIA_Window_Title, (ULONG)tr(combine ? "Combine Documents" : "Compare Documents"));
  set(c_labeltxt, MUIA_Text_Contents, (ULONG)tr(combine ? "Label unmarked changes with" : "Label changes with"));
  if (!gets_(c_orig)[0] && wp_docpath()[0]) set(c_orig, MUIA_String_Contents, (ULONG)wp_docpath());
  if (!gets_(c_label)[0]) set(c_label, MUIA_String_Contents, (ULONG)(wp_username()[0] ? wp_username() : "Quill"));
  set(cwin, MUIA_Window_Open, TRUE);
}
static void browse(Object *str, const char *title)
{
  char path[256], pat[200];
  strncpy(path, gets_(str), sizeof path - 1); path[sizeof path - 1] = 0;
  doc_format_pattern(0, pat, sizeof pat);
  if (wp_file_request(0, title, path, sizeof path, pat)) set(str, MUIA_String_Contents, (ULONG)path);
}
static void now_iso(char *out)
{
  struct DateStamp ds;
  struct ClockData cd;
  DateStamp(&ds);
  Amiga2Date((ULONG)ds.ds_Days * 86400UL + (ULONG)ds.ds_Minute * 60UL + (ULONG)ds.ds_Tick / 50UL, &cd);
  sprintf(out, "%04d-%02d-%02dT%02d:%02d:%02dZ", (int)cd.year, (int)cd.month, (int)cd.mday, (int)cd.hour, (int)cd.min, (int)cd.sec);
}
/* both documents read from their files, the result in a window of its own */
static void run_cmp(void)
{
  char op[256], rp[256], err[200];
  unsigned lost;
  Doc *o, *r;
  int n;
  strncpy(op, gets_(c_orig), sizeof op - 1); op[sizeof op - 1] = 0;
  strncpy(rp, gets_(c_rev), sizeof rp - 1); rp[sizeof rp - 1] = 0;
  if (!op[0] || !rp[0]) { wp_ask("OK", "Choose the original and the revised document.", NULL); return; }
  opts.chars = getv(c_level, MUIA_Cycle_Active) == 0;
  opts.comments = getv(c_cb[2], MUIA_Selected) != 0;
  opts.formatting = getv(c_cb[3], MUIA_Selected) != 0;
  opts.casechg = getv(c_cb[4], MUIA_Selected) != 0;
  opts.whitespace = getv(c_cb[5], MUIA_Selected) != 0;
  opts.tables = getv(c_cb[6], MUIA_Selected) != 0;
  opts.headers = getv(c_cb[7], MUIA_Selected) != 0;
  opts.notes = getv(c_cb[8], MUIA_Selected) != 0;
  opts.fields = 1;
  wp_latin1_utf8(gets_(c_label), opts.author, sizeof opts.author);
  if (!opts.author[0]) strcpy(opts.author, "Quill");
  now_iso(opts.date);
  set(cwin, MUIA_Window_Open, FALSE);
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  o = doc_load_file(op, &lost, err, sizeof err);
  r = o ? doc_load_file(rp, &lost, err, sizeof err) : NULL;
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  if (!o || !r) {
    char m[300];
    doc_free(o);
    snprintf(m, sizeof m, "%s\n%s", tr("Quill couldn't read the document:"), err);
    wp_ask("OK", "%s", m);
    return;
  }
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  n = combining ? doc_combine(o, r, &opts) : doc_compare(o, r, &opts);
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  doc_free(o);
  if (n < 0) { doc_free(r); wp_ask("OK", "Not enough memory to compare the documents.", NULL); return; }
  if (!wp_open_doc(r, "", 1)) { doc_free(r); return; }
  if (!n && !combining) wp_ask("OK", "Quill found no differences between the two documents.", NULL);
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp6_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_CMP_MENU: open_cmp(value == 1); return WPR_KEEPFOCUS;
  case ID_CMP_COMPARE: open_cmp(0); return WPR_KEEPFOCUS;
  case ID_CMP_COMBINE: open_cmp(1); return WPR_KEEPFOCUS;
  case ID_CMP_ORIG: browse(c_orig, "Original Document"); break;
  case ID_CMP_REV: browse(c_rev, "Revised Document"); break;
  case ID_CMP_SWAP: {
    char a[256];
    strncpy(a, gets_(c_orig), sizeof a - 1); a[sizeof a - 1] = 0;
    set(c_orig, MUIA_String_Contents, (ULONG)gets_(c_rev));
    set(c_rev, MUIA_String_Contents, (ULONG)a);
    break; }
  case ID_CMP_GO: run_cmp(); break;
  }
  return WPR_DONE;
}

/* the ribbon: Review > Compare (a button with its two choices, as Word's); the menu */
static const char *const cmp_labels[2] = { "Compare...", "Combine..." };
static const RibbonList cmp_list = { cmp_labels, 2, PL_TEXT, NULL };
static const Item cmp_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Compare", ID_CMP_MENU, 0, 0, 0, 0,
    "Compare: two versions of a document, the differences shown as tracked changes; or Combine two reviewers' changes in one",
    &cmp_list, "ci R 1 2 6 13 R 9 2 14 13 ci L 3 5 5 5 L 3 8 5 8 L 3 11 4 11 ca L 11 5 13 5 cr L 11 8 13 8 ca L 11 11 12 11", 0 },
};
static const WpGroup groups6[] = { { TAB_REVIEW, -1, { "Compare", G_COLS, cmp_items, 1 } } };
static const WpMenu menus6[] = {
  { WM_REVIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Compare...", 0, 0, 0, (APTR)ID_CMP_COMPARE } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Combine...", 0, 0, 0, (APTR)ID_CMP_COMBINE } },
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp6_package = {
  "WP6", ID_WP6_FIRST, ID_WP6_LAST,
  wp6_command, NULL, NULL, NULL, NULL, NULL, NULL,
  groups6, 1,
  menus6, (int)(sizeof menus6 / sizeof menus6[0])
};
