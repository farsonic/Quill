/*
 * wp_fields.c - WP1 Fields (docs/wordcraft-gap.md). This file belongs to WP1: only that
 * package edits it. Its commands are ID_WP1_FIRST .. ID_WP1_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The field model itself is core/field.h. Here: Insert > Field (the Field window, also Edit Field),
 * Insert > Document Property (a list on the ribbon and a window), Insert > Date and Time (formats, as
 * text or as a DATE field that updates), and the Fields group of the References tab: Update Fields
 * (F9), Toggle Field Codes (Shift+F9), Show Field Codes (Alt+F9), Edit Field Code, Unlink
 * (Ctrl+Shift+F9, Ctrl+6), Lock (Ctrl+3), Unlock (Ctrl+4), Previous and Next Field (F11 where the
 * keyboard has one), field shading (never, always, when selected); the field menu on the right button.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "theme.h"
#include "../core/field.h"
#include "../core/info.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#define FLabel(l) QLabel(l, MUIO_Label_SingleFrame)

enum {
  ID_FLD_INSERT = ID_WP1_FIRST,       /* Insert > Field... (Ctrl+F9) */
  ID_FLD_INSERT_OK,                   /* the Field window's OK */
  ID_FLD_DLG_CAT,                     /* its category changed */
  ID_FLD_DLG_CHANGE,                  /* a field or an option chosen: the code made afresh */
  ID_FLD_DOCPROP,                     /* the Document Property list (value: the property) */
  ID_FLD_PROPWIN,                     /* Document Property... */
  ID_FLD_PROP_OK,
  ID_FLD_DATETIME,                    /* Date and Time... */
  ID_FLD_DATE_OK,
  ID_FLD_UPDATE,                      /* F9 */
  ID_FLD_TOGGLE,                      /* Shift+F9 */
  ID_FLD_SHOWCODES,                   /* Alt+F9 */
  ID_FLD_EDIT,                        /* Edit Field Code (the Field window with the field's code) */
  ID_FLD_UNLINK,                      /* Ctrl+Shift+F9 */
  ID_FLD_LOCK,                        /* Ctrl+F11 */
  ID_FLD_UNLOCK,                      /* Ctrl+Shift+F11 */
  ID_FLD_NEXT,                        /* F11 */
  ID_FLD_PREV,                        /* Shift+F11 */
  ID_FLD_SHADE,                       /* the Field Shading list (value: FS_) */
  ID_FLD_SHADE0, ID_FLD_SHADE1, ID_FLD_SHADE2   /* the same from the menu */
};
/* the package's state bits: the caret is in a field; every field shows its code; shading */
enum { ST_INFIELD = 1, ST_CODES = 2, ST_SHADE0 = 3 };

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
/* the Amiga's clock for DATE and TIME */
static void amiga_clock(FieldTime *t)
{
  struct DateStamp ds;
  long days, y = 1978;
  static const int md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int m;
  DateStamp(&ds);
  days = ds.ds_Days;
  for (;;) { long yl = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365; if (days < yl) break; days -= yl; y++; }
  for (m = 0; m < 12; m++) {
    long ml = md[m] + (m == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (days < ml) break;
    days -= ml;
  }
  t->year = (int)y; t->month = m + 1; t->day = (int)days + 1;
  t->hour = (int)(ds.ds_Minute / 60); t->min = (int)(ds.ds_Minute % 60); t->sec = (int)(ds.ds_Tick / TICKS_PER_SECOND);
}
static void env(FieldEnv *e, int laid)
{
  Layout *L = NULL;
  field_env(e);
  if (laid) { get(view(), MUIA_Quill_Layout, &L); e->L = L; }
}
/* the month and day names in the interface's language (UTF-8, for the documents) */
static char mon_u[12][24], mon3_u[12][12], day_u[7][24], day3_u[7][12];
static const char *monp[12], *mon3p[12], *dayp[7], *day3p[7];
static const char *const en_mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const en_day[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
/* the first three letters of a UTF-8 name */
static void short3(const char *s, char *out, size_t cap)
{
  size_t i = 0;
  int n = 0;
  while (s[i] && n < 3 && i + 4 < cap) { i = utf8_next(s, strlen(s), i); n++; }
  memcpy(out, s, i); out[i] = 0;
}
static void names_init(void)
{
  int i;
  for (i = 0; i < 12; i++) { wp_latin1_utf8(tr(en_mon[i]), mon_u[i], sizeof mon_u[i]); short3(mon_u[i], mon3_u[i], sizeof mon3_u[i]); monp[i] = mon_u[i]; mon3p[i] = mon3_u[i]; }
  for (i = 0; i < 7; i++) { wp_latin1_utf8(tr(en_day[i]), day_u[i], sizeof day_u[i]); short3(day_u[i], day3_u[i], sizeof day3_u[i]); dayp[i] = day_u[i]; day3p[i] = day3_u[i]; }
  field_months = monp; field_months3 = mon3p; field_days = dayp; field_days3 = day3p;
}
/* a new field with code at the caret (in place of the selection), its result worked out (text: the
   result given instead): one undo step; the caret after it */
static int put_field(const char *code, const char *text)
{
  Doc *d = doc();
  Pos lo, hi, at;
  FieldEnv e;
  int k;
  sel(&lo, &hi);
  env(&e, 0);
  doc_begin(d, lo);
  if (pos_cmp(lo, hi)) doc_delete(d, lo, hi);
  at = lo;
  k = text ? field_insert_result(d, &at, code, text, &e, NULL) : field_insert(d, &at, code, &e, NULL);
  doc_end(d, at);
  changed();
  select_range(at, at);
  return k;
}
/* text at the caret in place of the selection */
static void put_text(const char *utf8)
{
  DoMethod(view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)utf8);
}

/* ------------------------------------------------------------------ */
/* the Field window: a field by category, its options, and the code they make (which can be typed) */
typedef struct { const char *name, *desc; u8 cat, kind; } FieldDef;
enum { FC_ALL, FC_DATE, FC_AUTO, FC_INFO, FC_EQ, FC_INDEX, FC_LINKS, FC_MERGE, FC_NUM, FC_USER, FC_NCAT };
enum { K_PLAIN, K_DATEF, K_NUMF, K_TEXTF, K_ARG, K_ARGQ, K_NUMARG };   /* the options a field has */
static const char *const cat_names[FC_NCAT] = { "(All)", "Date and Time", "Document Automation", "Document Information", "Equations and Formulas",
  "Index and Tables", "Links and References", "Mail Merge", "Numbering", "User Information" };
static const FieldDef defs[] = {
  { "=", "Calculates the result of an expression", FC_EQ, K_ARG },
  { "ADDRESSBLOCK", "Inserts a mail merge address block", FC_MERGE, K_PLAIN },
  { "ADVANCE", "Moves the text that follows right, left, up or down", FC_EQ, K_PLAIN },
  { "ASK", "Asks for text to be given to a bookmark", FC_MERGE, K_ARG },
  { "AUTHOR", "The name of the document's author", FC_INFO, K_TEXTF },
  { "AUTONUM", "Inserts an automatic number", FC_NUM, K_PLAIN },
  { "BIBLIOGRAPHY", "Inserts the bibliography", FC_LINKS, K_PLAIN },
  { "CITATION", "Inserts a citation", FC_LINKS, K_ARG },
  { "COMMENTS", "The comments in the document's properties", FC_INFO, K_TEXTF },
  { "COMPARE", "Compares two values", FC_AUTO, K_ARG },
  { "CREATEDATE", "The date the document was created", FC_DATE, K_DATEF },
  { "DATE", "Today's date", FC_DATE, K_DATEF },
  { "DOCPROPERTY", "A property of the document", FC_INFO, K_ARG },
  { "DOCVARIABLE", "The value of a document variable", FC_AUTO, K_ARG },
  { "EDITTIME", "The total editing time of the document", FC_DATE, K_NUMF },
  { "EQ", "An equation", FC_EQ, K_PLAIN },
  { "FILENAME", "The document's file name", FC_INFO, K_TEXTF },
  { "FILESIZE", "The size of the document's file", FC_INFO, K_NUMF },
  { "FILLIN", "Asks for text to put in", FC_MERGE, K_ARGQ },
  { "GREETINGLINE", "Inserts a mail merge greeting line", FC_MERGE, K_PLAIN },
  { "HYPERLINK", "Opens and jumps to a file or address", FC_LINKS, K_ARGQ },
  { "IF", "Shows one of two texts depending on a comparison", FC_AUTO, K_ARG },
  { "INCLUDEPICTURE", "Inserts a picture from a file", FC_LINKS, K_ARGQ },
  { "INCLUDETEXT", "Inserts text from a file", FC_LINKS, K_ARGQ },
  { "INDEX", "Builds an index", FC_INDEX, K_PLAIN },
  { "INFO", "Data from the document's properties", FC_INFO, K_ARG },
  { "KEYWORDS", "The keywords in the document's properties", FC_INFO, K_TEXTF },
  { "LASTSAVEDBY", "The name of the last person who saved the document", FC_INFO, K_TEXTF },
  { "LISTNUM", "Inserts a number in a list", FC_NUM, K_PLAIN },
  { "MERGEFIELD", "Inserts a mail merge field", FC_MERGE, K_ARG },
  { "MERGEREC", "The number of the current merge record", FC_MERGE, K_NUMF },
  { "NEXT", "Goes to the next record in a mail merge", FC_MERGE, K_PLAIN },
  { "NOTEREF", "Inserts the number of a footnote or endnote", FC_LINKS, K_ARG },
  { "NUMCHARS", "The number of characters in the document", FC_INFO, K_NUMF },
  { "NUMPAGES", "The number of pages in the document", FC_INFO, K_NUMF },
  { "NUMWORDS", "The number of words in the document", FC_INFO, K_NUMF },
  { "PAGE", "The number of the current page", FC_NUM, K_NUMF },
  { "PAGEREF", "The page number of a bookmark", FC_LINKS, K_ARG },
  { "PRINTDATE", "The date the document was last printed", FC_DATE, K_DATEF },
  { "QUOTE", "Inserts literal text", FC_LINKS, K_ARGQ },
  { "RD", "Includes another file in the index or tables", FC_INDEX, K_ARGQ },
  { "REF", "Inserts the text of a bookmark", FC_LINKS, K_ARG },
  { "REVNUM", "The number of times the document was saved", FC_NUM, K_NUMF },
  { "SAVEDATE", "The date the document was last saved", FC_DATE, K_DATEF },
  { "SECTION", "The number of the current section", FC_NUM, K_NUMF },
  { "SECTIONPAGES", "The number of pages in the section", FC_NUM, K_NUMF },
  { "SEQ", "A sequence number (figures, tables...)", FC_NUM, K_NUMARG },
  { "SET", "Gives a bookmark a value", FC_MERGE, K_ARG },
  { "SKIPIF", "Skips a record in a mail merge if a comparison is true", FC_MERGE, K_ARG },
  { "STYLEREF", "Inserts the text of the nearest paragraph in a style", FC_LINKS, K_ARGQ },
  { "SUBJECT", "The subject in the document's properties", FC_INFO, K_TEXTF },
  { "SYMBOL", "Inserts a symbol by its character code", FC_EQ, K_ARG },
  { "TA", "Marks an entry for a table of authorities", FC_INDEX, K_ARGQ },
  { "TC", "Marks an entry for a table of contents", FC_INDEX, K_ARGQ },
  { "TEMPLATE", "The name of the document's template", FC_INFO, K_TEXTF },
  { "TIME", "The time now", FC_DATE, K_DATEF },
  { "TITLE", "The title in the document's properties", FC_INFO, K_TEXTF },
  { "TOA", "Builds a table of authorities", FC_INDEX, K_PLAIN },
  { "TOC", "Builds a table of contents", FC_INDEX, K_PLAIN },
  { "USERADDRESS", "Your postal address", FC_USER, K_TEXTF },
  { "USERINITIALS", "Your initials", FC_USER, K_TEXTF },
  { "USERNAME", "Your name", FC_USER, K_TEXTF },
  { "XE", "Marks an entry for the index", FC_INDEX, K_ARGQ }
};
#define NDEFS ((int)(sizeof defs / sizeof defs[0]))
/* the \* formats offered, and their switches */
static const char *const fmt_names[] = { "(none)", "Uppercase", "Lowercase", "First capital", "Title case", "1, 2, 3, ...", "a, b, c, ...", "A, B, C, ...",
  "i, ii, iii, ...", "I, II, III, ...", "1st, 2nd, 3rd, ...", "One, Two, Three, ...", "First, Second, Third, ...", "hex", NULL };
#define NPICS 16
static const char *pics[NPICS + 1];        /* (field_date_pics, NULL ended for the cycle gadget) */
static Object *fwin, *f_cat, *f_list, *f_desc, *f_arg, *f_fmt, *f_date, *f_keep, *f_code;
static const char *catl[FC_NCAT + 1], *fmtl[16];
static int f_idx[NDEFS], f_n;           /* the list's entries (defs indexes) */
static int f_edit;                      /* editing the field whose start mark is at f_at */
static Pos f_at;
static int f_busy;                      /* (setting the gadgets: their notifications ignored) */
/* the gadgets as last seen: a notification that arrives when nothing changed (those queued while the
   window was being filled) is ignored, so it can't overwrite a code being edited */
typedef struct { LONG act; ULONG cat, fmt, dt, keep; char arg[256]; } FSnap;
static FSnap f_last;
static void f_snapshot(FSnap *n)
{
  char *a = NULL;
  memset(n, 0, sizeof *n);
  n->act = -1;
  get(f_list, MUIA_List_Active, &n->act); get(f_cat, MUIA_Cycle_Active, &n->cat);
  get(f_fmt, MUIA_Cycle_Active, &n->fmt); get(f_date, MUIA_Cycle_Active, &n->dt); get(f_keep, MUIA_Selected, &n->keep);
  get(f_arg, MUIA_String_Contents, &a);
  if (a) { strncpy(n->arg, a, sizeof n->arg - 1); n->arg[sizeof n->arg - 1] = 0; }
}
static int f_changed(void)
{
  FSnap n;
  f_snapshot(&n);
  if (!memcmp(&n, &f_last, sizeof n)) return 0;
  f_last = n;
  return 1;
}
static void f_fill(int cat)
{
  int i;
  f_n = 0;
  set(f_list, MUIA_List_Quiet, TRUE);
  DoMethod(f_list, MUIM_List_Clear);
  for (i = 0; i < NDEFS; i++)
    if (cat == FC_ALL || defs[i].cat == cat) { f_idx[f_n++] = i; DoMethod(f_list, MUIM_List_InsertSingle, (ULONG)defs[i].name, MUIV_List_Insert_Bottom); }
  set(f_list, MUIA_List_Quiet, FALSE);
}
static int f_chosen(void)
{
  LONG a = -1;
  get(f_list, MUIA_List_Active, &a);
  return a >= 0 && a < f_n ? f_idx[a] : -1;
}
/* the gadgets for the field chosen: its description, which options it has */
static void f_options(void)
{
  int k = f_chosen(), kind = k >= 0 ? defs[k].kind : K_PLAIN;
  set(f_desc, MUIA_Text_Contents, (ULONG)(k >= 0 ? tr(defs[k].desc) : ""));
  set(f_arg, MUIA_Disabled, kind != K_ARG && kind != K_ARGQ && kind != K_NUMARG);
  set(f_fmt, MUIA_Disabled, kind != K_NUMF && kind != K_TEXTF && kind != K_NUMARG);
  set(f_date, MUIA_Disabled, kind != K_DATEF);
}
/* the code the gadgets make, into the code gadget */
static void f_make(void)
{
  int k = f_chosen();
  ULONG fmt = 0, dt = 0, keep = 0;
  char *arg = NULL, code[600], latin[600];
  size_t n;
  if (k < 0) return;
  get(f_arg, MUIA_String_Contents, &arg);
  get(f_fmt, MUIA_Cycle_Active, &fmt); get(f_date, MUIA_Cycle_Active, &dt); get(f_keep, MUIA_Selected, &keep);
  snprintf(code, sizeof code, " %s ", defs[k].name);
  n = strlen(code);
  if ((defs[k].kind == K_ARG || defs[k].kind == K_ARGQ || defs[k].kind == K_NUMARG) && arg && arg[0]) {
    int q = defs[k].kind == K_ARGQ || strchr(arg, ' ');
    snprintf(code + n, sizeof code - n, q && arg[0] != '"' ? "\"%s\" " : "%s ", arg);
    n = strlen(code);
  }
  if (defs[k].kind == K_DATEF && dt < NPICS) { sprintf(code + n, "\\@ \"%s\" ", field_date_pics[dt]); n = strlen(code); }
  if ((defs[k].kind == K_NUMF || defs[k].kind == K_TEXTF || defs[k].kind == K_NUMARG) && fmt > 0 && (int)fmt < field_nformats) {
    sprintf(code + n, "\\* %s ", field_formats[fmt]); n = strlen(code);
  }
  if (keep) sprintf(code + n, "\\* MERGEFORMAT ");
  snprintf(latin, sizeof latin, "%s", code);
  f_busy = 1;
  set(f_code, MUIA_String_Contents, (ULONG)latin);
  f_busy = 0;
}
static void f_open(int edit, Pos at)
{
  if (!fwin) {
    Object *c;
    int i;
    for (i = 0; i < FC_NCAT; i++) catl[i] = tr(cat_names[i]);
    catl[FC_NCAT] = NULL;
    for (i = 0; fmt_names[i]; i++) fmtl[i] = tr(fmt_names[i]);
    fmtl[i] = NULL;
    c = HGroup,
      Child, VGroup, MUIA_HorizWeight, 40,
        Child, HGroup, Child, FLabel(tr("Categories:")), Child, f_cat = CycleObject, MUIA_Cycle_Entries, (ULONG)catl, MUIA_CycleChain, 1, End, End,
        Child, FLabel(tr("Field names:")),
        Child, f_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
      End,
      Child, VGroup, MUIA_HorizWeight, 60,
        Child, f_desc = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
        Child, ColGroup(2),
          Child, FLabel(tr("Argument:")), Child, f_arg = StringObject, StringFrame, MUIA_String_MaxLen, 256, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Format:")), Child, f_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmtl, MUIA_CycleChain, 1, End,
          Child, FLabel(tr("Date formats:")), Child, f_date = CycleObject, MUIA_Cycle_Entries, (ULONG)pics, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, f_keep = CheckMark(TRUE), Child, FLabel(tr("Preserve formatting during updates")), Child, HSpace(0), End,
        Child, FLabel(tr("Field codes:")),
        Child, f_code = StringObject, StringFrame, MUIA_String_MaxLen, 4000, MUIA_CycleChain, 1, End,
        Child, VSpace(0),
      End,
    End;
    if (!(fwin = wp_dialog("Field", MAKE_ID('F','L','D','1'), c, "_OK", ID_FLD_INSERT_OK))) return;
    DoMethod(f_cat, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DLG_CAT);
    DoMethod(f_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DLG_CHANGE);
    DoMethod(f_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_INSERT_OK);
    DoMethod(f_fmt, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DLG_CHANGE);
    DoMethod(f_date, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DLG_CHANGE);
    DoMethod(f_keep, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DLG_CHANGE);
    DoMethod(f_arg, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DLG_CHANGE);
    set(f_list, MUIA_Listview_DefClickColumn, 0);
  }
  f_edit = edit; f_at = at;
  f_busy = 1;
  set(f_cat, MUIA_Cycle_Active, FC_ALL);
  f_fill(FC_ALL);
  set(fwin, MUIA_Window_Title, (ULONG)tr(edit ? "Edit Field" : "Field"));
  if (edit) {
    /* the field's own code, its name chosen in the list */
    const Field *f = field_get(doc(), field_at(doc(), at, NULL, NULL));
    char nm[32], shown[600], latin[600];
    int i;
    field_name(f ? f->code : "", nm, sizeof nm);
    for (i = 0; i < f_n; i++) if (!strcmp(defs[f_idx[i]].name, nm)) { set(f_list, MUIA_List_Active, i); break; }
    field_code_text(f ? f->code : "", shown, sizeof shown);
    utf8_to_latin1(shown, strlen(shown), latin, sizeof latin);
    set(f_arg, MUIA_String_Contents, (ULONG)"");
    set(f_code, MUIA_String_Contents, (ULONG)latin);
  } else {
    set(f_list, MUIA_List_Active, 11);          /* DATE */
    set(f_arg, MUIA_String_Contents, (ULONG)"");
  }
  f_busy = 0;
  f_options();
  if (!edit) f_make();
  f_snapshot(&f_last);
  set(fwin, MUIA_Window_Open, TRUE);
  set(fwin, MUIA_Window_ActiveObject, (ULONG)f_list);
}
static void f_ok(void)
{
  char *code = NULL, utf[4000], *stored;
  get(f_code, MUIA_String_Contents, &code);
  set(fwin, MUIA_Window_Open, FALSE);
  if (!code || !code[0]) return;
  wp_latin1_utf8(code, utf, sizeof utf);
  if (!(stored = field_code_parse(utf))) return;
  if (f_edit) {
    Doc *d = doc();
    FieldEnv e;
    Pos s, en;
    if (field_at(d, f_at, &s, &en) >= 0 && field_set_code(d, s, stored)) {
      env(&e, 0);
      field_update(d, s, s, &e);
      changed();
      field_end(d, s, &en); en.off += 4;
      select_range(s, en);
    }
  } else put_field(stored, NULL);
  free(stored);
  set(wp_win(), MUIA_Window_Activate, TRUE);
}

/* ------------------------------------------------------------------ */
/* Document Property: on the ribbon, a list of Word's; the window lists them all, the file's own too */
static const char *const prop_list[] = { "Author", "Category", "Comments", "Company", "Keywords", "Manager", "Status", "Subject", "Title" };
#define NPROPL 9
static const char *prop_lab[NPROPL];
static const RibbonList prop_rl = { prop_lab, NPROPL, PL_TEXT, NULL };
static Object *pwin, *p_list, *p_val;
static char p_names[64][64];
static int p_n;
static void insert_prop(const char *name)
{
  char code[160];
  sprintf(code, strchr(name, ' ') ? " DOCPROPERTY \"%.60s\" \\* MERGEFORMAT " : " DOCPROPERTY %.60s \\* MERGEFORMAT ", name);
  if (put_field(code, NULL) >= 0) {
    /* a property the document hasn't got shows nothing, as in Word (its name in the window says so) */
  }
}
static void p_show(void)
{
  LONG a = -1;
  char v[256] = "", lat[256];
  get(p_list, MUIA_List_Active, &a);
  if (a >= 0 && a < p_n) field_prop(doc(), p_names[a], v, sizeof v);
  utf8_to_latin1(v, strlen(v), lat, sizeof lat);
  set(p_val, MUIA_Text_Contents, (ULONG)lat);
}
static void p_open(void)
{
  int i;
  DocInfo *fi = doc()->info;
  if (!pwin) {
    Object *c = VGroup,
      Child, FLabel(tr("Property:")),
      Child, p_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_AdjustWidth, TRUE, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
      Child, HGroup, Child, FLabel(tr("Value:")), Child, p_val = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End, End,
    End;
    if (!(pwin = wp_dialog("Document Property", MAKE_ID('F','L','D','2'), c, "_Insert", ID_FLD_PROP_OK))) return;
    DoMethod(p_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_PROPWIN);
    DoMethod(p_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_PROP_OK);
  }
  p_n = 0;
  set(p_list, MUIA_List_Quiet, TRUE);
  DoMethod(p_list, MUIM_List_Clear);
  for (i = 0; i < field_nprop_names && p_n < 64; i++) {
    snprintf(p_names[p_n++], sizeof p_names[0], "%s", field_prop_names[i]);
    DoMethod(p_list, MUIM_List_InsertSingle, (ULONG)tr(field_prop_names[i]), MUIV_List_Insert_Bottom);
  }
  for (i = 0; fi && i < fi->ncust && p_n < 64; i++) {
    char lat[64];
    if (!fi->cust[i].name) continue;
    snprintf(p_names[p_n++], sizeof p_names[0], "%s", fi->cust[i].name);
    utf8_to_latin1(fi->cust[i].name, strlen(fi->cust[i].name), lat, sizeof lat);
    DoMethod(p_list, MUIM_List_InsertSingle, (ULONG)lat, MUIV_List_Insert_Bottom);
  }
  set(p_list, MUIA_List_Quiet, FALSE);
  set(p_list, MUIA_List_Active, 0);
  p_show();
  set(pwin, MUIA_Window_Open, TRUE);
  set(pwin, MUIA_Window_ActiveObject, (ULONG)p_list);
}

/* ------------------------------------------------------------------ */
/* Date and Time: today's date (or the time) in a format, as text or as a field that updates */
static Object *dwin, *d_list, *d_upd;
static char d_shown[NPICS][80];
static void d_open(void)
{
  FieldTime t;
  int i;
  if (!dwin) {
    Object *c = VGroup,
      Child, FLabel(tr("Available formats:")),
      Child, d_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_AdjustWidth, TRUE, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
      Child, HGroup, Child, d_upd = CheckMark(FALSE), Child, FLabel(tr("Update automatically")), Child, HSpace(0), End,
    End;
    if (!(dwin = wp_dialog("Date and Time", MAKE_ID('F','L','D','3'), c, "_OK", ID_FLD_DATE_OK))) return;
    DoMethod(d_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FLD_DATE_OK);
  }
  field_time_now(&t);
  set(d_list, MUIA_List_Quiet, TRUE);
  DoMethod(d_list, MUIM_List_Clear);
  for (i = 0; i < NPICS; i++) {
    char u[80];
    field_format_date(field_date_pics[i], &t, u, sizeof u);
    utf8_to_latin1(u, strlen(u), d_shown[i], sizeof d_shown[i]);
    DoMethod(d_list, MUIM_List_InsertSingle, (ULONG)d_shown[i], MUIV_List_Insert_Bottom);
  }
  set(d_list, MUIA_List_Quiet, FALSE);
  set(d_list, MUIA_List_Active, 0);
  set(dwin, MUIA_Window_Open, TRUE);
  set(dwin, MUIA_Window_ActiveObject, (ULONG)d_list);
}
static void d_ok(void)
{
  LONG a = 0;
  ULONG upd = 0;
  get(d_list, MUIA_List_Active, &a); get(d_upd, MUIA_Selected, &upd);
  set(dwin, MUIA_Window_Open, FALSE);
  if (a < 0 || a >= NPICS) return;
  if (upd) {
    char code[80];
    /* a picture with only a time in it: TIME, as Word does */
    int timeonly = !strchr(field_date_pics[a], 'd') && !strchr(field_date_pics[a], 'y');
    sprintf(code, " %s \\@ \"%s\" \\* MERGEFORMAT ", timeonly ? "TIME" : "DATE", field_date_pics[a]);
    put_field(code, NULL);
  } else {
    FieldTime t;
    char u[80];
    field_time_now(&t);
    field_format_date(field_date_pics[a], &t, u, sizeof u);
    put_text(u);
  }
  set(wp_win(), MUIA_Window_Activate, TRUE);
}

/* ------------------------------------------------------------------ */
/* the field commands on the selection, or the field at the caret */
static void shading_save(void)
{
  char v[4];
  sprintf(v, "%d", field_shading);
  SetVar((STRPTR)"Quill/FieldShading", (STRPTR)v, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}
static int fields_cmd(ULONG id)
{
  Doc *d = doc();
  Pos lo, hi, s, e;
  FieldEnv fe;
  int n = 0;
  sel(&lo, &hi);
  switch (id) {
  case ID_FLD_UPDATE:
    env(&fe, 1);
    if (!pos_cmp(lo, hi) && field_at(d, lo, NULL, NULL) < 0) n = field_update_all(d, &fe);     /* nothing chosen: all of it */
    else n = field_update(d, lo, hi, &fe);
    break;
  case ID_FLD_UNLINK: env(&fe, 1); n = field_unlink(d, lo, hi, &fe); break;
  case ID_FLD_LOCK: n = field_lock(d, lo, hi, 1); break;
  case ID_FLD_UNLOCK: n = field_lock(d, lo, hi, 0); break;
  case ID_FLD_TOGGLE: n = field_toggle(d, lo, hi); break;
  case ID_FLD_SHOWCODES: field_show_codes(d, !d->fieldcodes); n = 1; break;
  case ID_FLD_NEXT: case ID_FLD_PREV:
    if (field_next(d, id == ID_FLD_NEXT ? hi : lo, id == ID_FLD_NEXT ? 1 : -1, &s, &e) < 0) { DisplayBeep(NULL); return WPR_DONE; }
    e.off += 4;
    select_range(s, e);
    return WPR_DONE;
  case ID_FLD_EDIT:
    if (field_at(d, lo, &s, NULL) < 0) { DisplayBeep(NULL); return WPR_DONE; }
    f_open(1, s);
    return WPR_KEEPFOCUS;
  }
  if (!n && id != ID_FLD_UPDATE) DisplayBeep(NULL);
  changed();
  if (id != ID_FLD_UNLINK) select_range(lo, hi);
  return WPR_DONE;
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp1_command(ULONG id, LONG value)
{
  if (!doc()) return WPR_DONE;
  switch (id) {
  case ID_FLD_INSERT: { Pos z; z.para = 0; z.off = 0; f_open(0, z); return WPR_KEEPFOCUS; }
  case ID_FLD_INSERT_OK: if (fwin) f_ok(); return WPR_DONE;
  case ID_FLD_DLG_CAT:
    if (fwin && !f_busy) {
      ULONG c = 0;
      get(f_cat, MUIA_Cycle_Active, &c);
      if (c == f_last.cat) return WPR_KEEPFOCUS;
      f_busy = 1; f_fill((int)c); set(f_list, MUIA_List_Active, 0); f_busy = 0;
      f_options(); f_make(); f_snapshot(&f_last);
    }
    return WPR_KEEPFOCUS;
  case ID_FLD_DLG_CHANGE: if (fwin && !f_busy && f_changed()) { f_options(); f_make(); } return WPR_KEEPFOCUS;
  case ID_FLD_DOCPROP:
    if (value >= 0 && value < NPROPL) insert_prop(prop_list[value]);
    else p_open();
    return WPR_DONE;
  case ID_FLD_PROPWIN:
    if (pwin) { ULONG o = 0; get(pwin, MUIA_Window_Open, &o); if (o) { p_show(); return WPR_KEEPFOCUS; } }
    p_open();
    return WPR_KEEPFOCUS;
  case ID_FLD_PROP_OK: {
    LONG a = -1;
    get(p_list, MUIA_List_Active, &a);
    set(pwin, MUIA_Window_Open, FALSE);
    if (a >= 0 && a < p_n) insert_prop(p_names[a]);
    set(wp_win(), MUIA_Window_Activate, TRUE);
    return WPR_DONE; }
  case ID_FLD_DATETIME: d_open(); return WPR_KEEPFOCUS;
  case ID_FLD_DATE_OK: if (dwin) d_ok(); return WPR_DONE;
  case ID_FLD_SHADE: case ID_FLD_SHADE0: case ID_FLD_SHADE1: case ID_FLD_SHADE2:
    field_shading = id == ID_FLD_SHADE ? (value >= 0 && value <= FS_SELECTED ? (int)value : field_shading) : (int)(id - ID_FLD_SHADE0);
    shading_save();
    DoMethod(view(), MUIM_Quill_Relayout);
    return WPR_DONE;
  }
  if (id >= ID_FLD_UPDATE && id <= ID_FLD_PREV) return fields_cmd(id);
  return WPR_DONE;
}

/* ------------------------------------------------------------------ */
/* the ribbon: Insert's Fields group (Field, Document Property, Date and Time) and References' Fields */
#define ICON_FIELD "ci P 7 5 1 4 2 4 7 2 8 4 9 4 14 5 15 P 7 10 1 11 2 11 7 13 8 11 9 11 14 10 15 ca F 7 7 8 9"
#define ICON_PROP "ci R 2 1 13 15 ca L 4 4 11 4 L 4 7 11 7 cd L 4 10 11 10 L 4 13 9 13"
#define ICON_CODES "ci P 7 5 1 4 2 4 7 2 8 4 9 4 14 5 15 P 7 10 1 11 2 11 7 13 8 11 9 11 14 10 15 ca L 6 5 9 5 L 6 8 9 8 L 6 11 9 11"
#define ICON_TOGGLE "ci P 7 5 1 4 2 4 7 2 8 4 9 4 14 5 15 P 7 10 1 11 2 11 7 13 8 11 9 11 14 10 15 ca L 6 6 9 10 L 9 6 6 10"
#define ICON_EDIT "ci P 7 5 1 4 2 4 7 2 8 4 9 4 14 5 15 ca L 7 13 14 6 L 6 14 7 13 L 13 5 15 7"
#define ICON_UNLINK "ci O 4 8 3 O 12 8 3 cr L 8 2 8 5 L 8 11 8 14"
#define ICON_LOCK "ci R 3 7 12 15 P 6 5 7 5 4 6 2 9 2 10 4 10 7 ca F 7 10 8 12"
#define ICON_UNLOCK "ci R 1 7 10 15 P 5 8 7 8 4 9 2 12 2 13 4 ca F 5 10 6 12"
#define ICON_SHADE "ci R 1 4 14 12 cl F 2 5 13 11 ct L 4 8 11 8"
static const char *const shade_names[3] = { "Never", "Always", "When selected" };
static const char *shade_lab[3];
static const RibbonList shade_rl = { shade_lab, 3, PL_TEXT, NULL };
static const Item ref_items[] = {
  { K_BUTTON, S_LARGE, IC_UPDATE, 0, "Update Fields", ID_FLD_UPDATE, 0, 0, 0, 0, "The selected fields brought up to date, or all of them (F9)" },
  { K_BUTTON, S_SMALL, 0, 0, "Toggle Codes", ID_FLD_TOGGLE, 0, 0, F_NEWCOL, 0, "The selected field's code or result (Shift-F9)", NULL, ICON_TOGGLE },
  { K_BUTTON, S_SMALL, 0, 0, "Show Codes", ID_FLD_SHOWCODES, RB_WP(WPI_1, ST_CODES), 0, 0, 0, "Every field's code instead of its result (Alt-F9)", NULL, ICON_CODES },
  { K_BUTTON, S_SMALL, 0, 0, "Edit Field", ID_FLD_EDIT, 0, RB_WP(WPI_1, ST_INFIELD), 0, 0, "The code of the field at the caret", NULL, ICON_EDIT },
  { K_BUTTON, S_SMALL, 0, 0, "Unlink", ID_FLD_UNLINK, 0, 0, F_NEWCOL, 0, "The selected fields become their results as plain text (Ctrl-6)", NULL, ICON_UNLINK },
  { K_BUTTON, S_SMALL, 0, 0, "Lock", ID_FLD_LOCK, 0, 0, 0, 0, "The selected fields aren't updated (Ctrl-3)", NULL, ICON_LOCK },
  { K_BUTTON, S_SMALL, 0, 0, "Unlock", ID_FLD_UNLOCK, 0, 0, 0, 0, "The selected fields are updated again (Ctrl-4)", NULL, ICON_UNLOCK },
  { K_BUTTON, S_SMALL, IC_PREV, 0, "Previous Field", ID_FLD_PREV, 0, 0, F_NEWCOL, 0, "Select the field before the caret" },
  { K_BUTTON, S_SMALL, IC_NEXT, 0, "Next Field", ID_FLD_NEXT, 0, 0, 0, 0, "Select the next field" },
  { K_DROP, S_SMALL, 0, 0, "Shading", ID_FLD_SHADE, 0, 0, 0, 0, "Fields on grey: never, always, or the one the caret is in", &shade_rl, ICON_SHADE },
  { K_BUTTON, S_SMALL, 0, 0, "Field", ID_FLD_INSERT, 0, 0, F_NEWCOL, 0, "A field: a date, a page number, a document property... (Ctrl-F9)", NULL, ICON_FIELD },
  { K_DROP, S_SMALL, 0, 0, "Document Property", ID_FLD_DOCPROP, 0, 0, 0, 0, "One of the document's properties, as a field", &prop_rl, ICON_PROP },
  { K_BUTTON, S_SMALL, IC_DATE, 0, "Date & Time", ID_FLD_DATETIME, 0, 0, 0, 0, "Today's date or the time in a format, as text or a field that updates" } };
/* one group on References (the Insert tab is full at 640 pixels): the field commands, then Field,
   Document Property and Date & Time (also on the Insert menu) */
static const WpGroup groups[] = { { TAB_REFERENCES, -1, { "Fields", G_COLS, ref_items, 13 } } };
/* the menus: Insert (Field, Document Property, Date and Time, and the field commands), View (codes, shading) */
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Field...", 0, 0, 0, (APTR)ID_FLD_INSERT } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Document Property...", 0, 0, 0, (APTR)ID_FLD_PROPWIN } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Date and Time...", 0, 0, 0, (APTR)ID_FLD_DATETIME } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Fields", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Update Fields", 0, 0, 0, (APTR)ID_FLD_UPDATE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Toggle Field Codes", 0, 0, 0, (APTR)ID_FLD_TOGGLE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Edit Field Code...", 0, 0, 0, (APTR)ID_FLD_EDIT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Unlink Fields", 0, 0, 0, (APTR)ID_FLD_UNLINK } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Lock Fields", 0, 0, 0, (APTR)ID_FLD_LOCK } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Unlock Fields", 0, 0, 0, (APTR)ID_FLD_UNLOCK } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Next Field", 0, 0, 0, (APTR)ID_FLD_NEXT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Previous Field", 0, 0, 0, (APTR)ID_FLD_PREV } },
  { WM_VIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Field Codes", 0, 0, 0, (APTR)ID_FLD_SHOWCODES } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Field Shading", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Never", 0, 0, 0, (APTR)ID_FLD_SHADE0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Always", 0, 0, 0, (APTR)ID_FLD_SHADE1 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"When selected", 0, 0, 0, (APTR)ID_FLD_SHADE2 } } };

static void state(ULONG *on, const WpCtx *c)
{
  if (!c->doc) return;
  if (c->doc->nfields && field_at(c->doc, c->caret, NULL, NULL) >= 0) *on |= 1UL << ST_INFIELD;
  if (c->doc->fieldcodes) *on |= 1UL << ST_CODES;
}
/* the right button on a field: Word's field menu */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  if ((c->where != WPC_TEXT && c->where != WPC_LINK) || !c->doc || !c->doc->nfields || field_at(c->doc, c->caret, NULL, NULL) < 0) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr("Update Field"), WP_CTX_ID(ID_FLD_UPDATE), 1);
  wp_ctx_item(menu, tr("Edit Field..."), WP_CTX_ID(ID_FLD_EDIT), 1);
  wp_ctx_item(menu, tr("Toggle Field Codes"), WP_CTX_ID(ID_FLD_TOGGLE), 1);
}
/* the keys: F9 with its qualifiers as in Word, F11 where there is one (raw 0x4B), Ctrl-3, 4, 6 */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  int ctrl = (qual & IEQUALIFIER_CONTROL) != 0;
  (void)c;
  if (code == 0x58) {
    if (ctrl && shift) return ID_FLD_UNLINK;
    if (ctrl) return ID_FLD_INSERT;
    if (alt) return ID_FLD_SHOWCODES;
    if (shift) return ID_FLD_TOGGLE;
    return ID_FLD_UPDATE;
  }
  if (code == 0x4B) {
    if (ctrl && shift) return ID_FLD_UNLOCK;
    if (ctrl) return ID_FLD_LOCK;
    return shift ? ID_FLD_PREV : ID_FLD_NEXT;
  }
  if (ctrl && !alt && !shift) {
    if (code == 0x03) return ID_FLD_LOCK;
    if (code == 0x04) return ID_FLD_UNLOCK;
    if (code == 0x06) return ID_FLD_UNLINK;
  }
  return 0;
}
static void init(void)
{
  char v[8];
  int i;
  field_clock = amiga_clock;
  for (i = 0; i < NPICS && i < field_ndate_pics; i++) pics[i] = field_date_pics[i];
  pics[i] = NULL;
  names_init();
  for (i = 0; i < NPROPL; i++) prop_lab[i] = prop_list[i];
  for (i = 0; i < 3; i++) shade_lab[i] = shade_names[i];
  if (GetVar((STRPTR)"Quill/FieldShading", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0 && v[0] >= '0' && v[0] <= '2') field_shading = v[0] - '0';
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp1_package = {
  "WP1", ID_WP1_FIRST, ID_WP1_LAST,
  wp1_command, state, NULL, ctxmenu, key, init, NULL,
  groups, 1,
  menus, (int)(sizeof menus / sizeof menus[0])
};
