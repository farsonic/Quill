/*
 * wp_cite.c - WP3 Citations and bibliography (docs/wordcraft-gap.md). This file belongs to WP3: only that
 * package edits it. Its commands are ID_WP3_FIRST .. ID_WP3_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The model is core/cite.h. Here, as in Word's References tab: Insert Citation (a list of the
 * document's sources, Add New Source, Add New Placeholder), Manage Sources (the master list in
 * PROGDIR:Sources.xml and the document's current list), Create Source and Edit Source (Word's source
 * types and fields, Show All Bibliography Fields, the tag, Edit Name for authors and the other
 * contributors), Style (the citation and bibliography style), Bibliography (Bibliography, References,
 * Works Cited, or the field alone), and on a citation's right-button menu Edit Citation (pages,
 * suppress author, year, title), Edit Source, Convert citation to static text, Update Citations and
 * Bibliography.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "theme.h"
#include "../core/field.h"
#include "../core/cite.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#define WLabel(l) QLabel(l, MUIO_Label_SingleFrame)
#define MASTER_FILE "PROGDIR:Sources.xml"
/* lists in the dialogs' font, as their other gadgets */
#define LFONT (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font()
#define W30 (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMM"
#define W50 (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM"

enum {
  ID_CITE_INSERT = ID_WP3_FIRST,      /* the ribbon's Insert Citation list (value: a source, then Add New Source, Add New Placeholder) */
  ID_CITE_INSERTWIN,                  /* Insert Citation... (a window listing the sources) */
  ID_CITE_INSERT_OK,
  ID_CITE_NEWSRC,                     /* Add New Source... */
  ID_CITE_PLACEHOLDER,                /* Add New Placeholder... */
  ID_CITE_PH_OK,
  ID_CITE_MANAGE,                     /* Manage Sources... */
  ID_CITE_MAN_CLOSE,
  ID_CITE_MAN_MASTER,                 /* a master list entry chosen */
  ID_CITE_MAN_CURRENT,                /* a current list entry chosen */
  ID_CITE_MAN_TOCUR,                  /* Copy -> */
  ID_CITE_MAN_TOMASTER,               /* <- Copy */
  ID_CITE_MAN_DELETE,
  ID_CITE_MAN_EDIT,
  ID_CITE_MAN_NEW,
  ID_CITE_MAN_SEARCH,                 /* the search text or the sort changed */
  ID_CITE_SRC_OK,                     /* Create Source / Edit Source */
  ID_CITE_SRC_TYPE,
  ID_CITE_SRC_FIELD,                  /* a field chosen in the list */
  ID_CITE_SRC_VALUE,                  /* its value typed */
  ID_CITE_SRC_CORP,
  ID_CITE_SRC_ALL,                    /* Show All Bibliography Fields */
  ID_CITE_SRC_TAG,
  ID_CITE_SRC_NAMES,                  /* Edit... (Edit Name) */
  ID_CITE_NAME_OK,                    /* Edit Name */
  ID_CITE_NAME_ADD,
  ID_CITE_NAME_UP,
  ID_CITE_NAME_DOWN,
  ID_CITE_NAME_DEL,
  ID_CITE_NAME_PICK,                   /* a name taken back into the fields to change it (double click) */
  ID_CITE_EDIT,                       /* Edit Citation... */
  ID_CITE_EDIT_OK,
  ID_CITE_EDITSRC,                    /* Edit Source... (the citation's) */
  ID_CITE_STYLE,                      /* the Style list (value: CS_) */
  ID_CITE_BIB,                        /* the Bibliography list (value: 0 Bibliography, 1 References, 2 Works Cited, 3 the field alone) */
  ID_CITE_BIB0, ID_CITE_BIB1, ID_CITE_BIB2, ID_CITE_BIB3,   /* the same from the menu */
  ID_CITE_UPDATE,                     /* Update Citations and Bibliography */
  ID_CITE_STATIC,                     /* Convert citation (bibliography) to static text */
  ID_CITE_STYLE0                      /* the menu's styles: ID_CITE_STYLE0 + CS_ */
};
/* the package's state bits: the caret is in a citation, in a bibliography; the document has sources */
enum { ST_INCITE = 1, ST_INBIB = 2, ST_SOURCES = 3 };

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
static void changed(void) { DoMethod(view(), MUIM_Quill_Relayout); wp_refresh(); }
static void lat(const char *utf8, char *out, size_t cap) { utf8_to_latin1(utf8, strlen(utf8), out, cap); }
static char *gstr(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? s : (char *)""; }
static void back_to_doc(void) { set(wp_win(), MUIA_Window_Activate, TRUE); }

/* the master list (PROGDIR:Sources.xml, Word's form), read once */
static CiteList master;
static int master_loaded;
static void master_load(void)
{
  if (master_loaded) return;
  master_loaded = 1;
  cite_load_file(&master, MASTER_FILE);
}
static void master_save(void) { cite_save_file(&master, MASTER_FILE); }
/* the document's current list (made if needed) */
static CiteList *current(void) { CiteDoc *c = cite_doc(doc()); return c ? &c->list : NULL; }
static void doc_sources_changed(void)
{
  Doc *d = doc();
  cite_sync(d);
  d->dirty = 1;
  set(view(), MUIA_Quill_Changed, TRUE);
}
/* how a source is listed: "Author; Title (Year)" ("?" before a placeholder, "»" before one cited) */
static void src_line(const CiteSource *s, int mark, char *out, size_t cap)
{
  char a[200], u[400];
  cite_names_text(s, CITE_AUTHOR, a, sizeof a);
  if (!a[0] && s->nr) cite_names_text(s, s->r[0].role, a, sizeof a);
  if (cite_is_placeholder(s)) snprintf(u, sizeof u, "%s%s", mark ? "? " : "", cite_get(s, CITE_TAG));
  else snprintf(u, sizeof u, "%s%s%s%s%s%s%s", mark == 2 ? "\xC2\xBB " : mark ? "  " : "", a, a[0] ? "; " : "", cite_get(s, CITE_TITLE),
                cite_get(s, CITE_YEAR)[0] ? " (" : "", cite_get(s, CITE_YEAR), cite_get(s, CITE_YEAR)[0] ? ")" : "");
  lat(u, out, cap);
}
/* a source's entry in the document's style, as Latin-1 without italics (a preview) */
static void src_preview(const CiteList *l, int i, char *out, size_t cap)
{
  Buf b;
  int cs = cite_doc_style(doc());
  buf_init(&b);
  if (i >= 0 && i < l->n) cite_entry(l, i, cs, 1, &b);
  if (b.data) { char *c, *o; for (c = o = b.data; *c; c++) if (*c != CITE_I0 && *c != CITE_I1) *o++ = *c == '\t' ? ' ' : *c; *o = 0; }
  lat(b.data ? b.data : "", out, cap);
  buf_free(&b);
}
static FieldEnv *env(void) { static FieldEnv e; field_env(&e); return &e; }

/* a citation of source tag at the caret (in place of the selection) */
static void put_citation(const char *tag)
{
  Doc *d = doc();
  Pos lo, hi, at;
  sel(&lo, &hi);
  doc_begin(d, lo);
  if (pos_cmp(lo, hi)) doc_delete(d, lo, hi);
  at = lo;
  cite_insert(d, &at, tag, NULL, 0, NULL, 0);
  doc_end(d, at);
  changed();
  select_range(at, at);
}

/* ------------------------------------------------------------------ */
/* Edit Name: the people of one role (or a corporate name) */
static Object *nwin, *n_last, *n_first, *n_mid, *n_list, *n_addb, *n_upb, *n_downb, *n_delb;
static CitePerson n_p[64];
static int n_n;
static void n_fill(void)
{
  int i;
  set(n_list, MUIA_List_Quiet, TRUE);
  DoMethod(n_list, MUIM_List_Clear);
  for (i = 0; i < n_n; i++) {
    char u[300], l1[300];
    snprintf(u, sizeof u, "%s%s%s%s%s", n_p[i].last, n_p[i].first[0] || n_p[i].middle[0] ? ", " : "", n_p[i].first, n_p[i].middle[0] ? " " : "", n_p[i].middle);
    lat(u, l1, sizeof l1);
    DoMethod(n_list, MUIM_List_InsertSingle, (ULONG)l1, MUIV_List_Insert_Bottom);
  }
  set(n_list, MUIA_List_Quiet, FALSE);
}
static void n_clear(void) { int i; for (i = 0; i < n_n; i++) { free(n_p[i].last); free(n_p[i].first); free(n_p[i].middle); } n_n = 0; }
static char *u8dup(const char *latin) { char u[400]; wp_latin1_utf8(latin, u, sizeof u); return str_dup(u); }

/* ------------------------------------------------------------------ */
/* Create Source / Edit Source */
static Object *swin, *s_type, *s_fields, *s_flabel, *s_value, *s_names, *s_corp, *s_all, *s_tag, *s_prev;
static CiteSource s_src;               /* the source being made or edited */
static const char *s_keys[96];         /* the fields listed (XML names) */
static int s_n, s_busy, s_tagtouched;
static int s_edit_list, s_edit_i;      /* editing: 0 the current list, 1 the master list, and its index; -1: a new source */
static int s_cite_after;               /* a new source from Insert Citation: cited at the caret when made */
static char s_oldtag[64];
static const char *typel[24];
static const char *s_role;             /* Edit Name's role */

static int s_listed(const char *k) { int i; for (i = 0; i < s_n; i++) if (!strcmp(s_keys[i], k)) return 1; return 0; }
static void s_line(int i, char *out, size_t cap)
{
  char v[400], u[480];
  const char *k = s_keys[i];
  if (cite_is_role(k)) cite_names_text(&s_src, k, v, sizeof v);
  else snprintf(v, sizeof v, "%s", cite_get(&s_src, k));
  lat(v, u, sizeof u);
  snprintf(out, cap, "%s: %s", tr(cite_field_label(k)), u);
}
/* the fields for the type (and every one the source has, so nothing is hidden) */
static void s_fill(void)
{
  ULONG t = 0, all = 0;
  int i, keep = -1;
  LONG act = -1;
  const CiteTypeDef *td;
  get(s_type, MUIA_Cycle_Active, &t); get(s_all, MUIA_Selected, &all);
  get(s_fields, MUIA_List_Active, &act);
  if (t >= (ULONG)cite_ntypes) t = 0;
  td = &cite_types[t];
  s_n = 0;
  for (i = 0; td->fields[i] && s_n < 96; i++) s_keys[s_n++] = td->fields[i];
  for (i = 0; all && td->more[i] && s_n < 96; i++) if (!s_listed(td->more[i])) s_keys[s_n++] = td->more[i];
  for (i = 0; i < s_src.nr && s_n < 96; i++) if (!s_listed(s_src.r[i].role)) s_keys[s_n++] = s_src.r[i].role;
  for (i = 0; i < s_src.nkv && s_n < 96; i++) {
    const char *k = s_src.kv[i].k;
    static const char *const hidden[] = { CITE_TAG, CITE_TYPE, CITE_GUID, CITE_LCID, CITE_REFORDER, NULL };
    int h;
    for (h = 0; hidden[h] && strcmp(hidden[h], k); h++) ;
    if (!hidden[h] && !s_listed(k)) s_keys[s_n++] = k;
  }
  s_busy = 1;
  set(s_fields, MUIA_List_Quiet, TRUE);
  DoMethod(s_fields, MUIM_List_Clear);
  for (i = 0; i < s_n; i++) { char l1[500]; s_line(i, l1, sizeof l1); DoMethod(s_fields, MUIM_List_InsertSingle, (ULONG)l1, MUIV_List_Insert_Bottom); }
  set(s_fields, MUIA_List_Quiet, FALSE);
  keep = act >= 0 && act < s_n ? (int)act : 0;
  set(s_fields, MUIA_List_Active, keep);
  s_busy = 0;
}
static void s_preview(void)
{
  CiteList one;
  char p[600];
  memset(&one, 0, sizeof one);
  one.s = &s_src; one.n = 1;
  if (cite_get(&s_src, CITE_TITLE)[0] || s_src.nr) src_preview(&one, 0, p, sizeof p); else p[0] = 0;
  set(s_prev, MUIA_Text_Contents, (ULONG)p);
}
/* the field chosen: its label and value in the gadgets */
static void s_pick(void)
{
  LONG a = -1;
  char v[400], l1[480];
  const char *k;
  get(s_fields, MUIA_List_Active, &a);
  if (a < 0 || a >= s_n) return;
  k = s_keys[a];
  if (cite_is_role(k)) cite_names_text(&s_src, k, v, sizeof v); else snprintf(v, sizeof v, "%s", cite_get(&s_src, k));
  lat(v, l1, sizeof l1);
  s_busy = 1;
  set(s_flabel, MUIA_Text_Contents, (ULONG)tr(cite_field_label(k)));
  set(s_value, MUIA_String_Contents, (ULONG)l1);
  { const CiteRole *r = cite_role_c(&s_src, k); set(s_corp, MUIA_Selected, r && r->corp ? TRUE : FALSE); }
  s_busy = 0;
}
/* a tag made from the author and year while the person hasn't typed one */
static void s_autotag(void)
{
  char t[64], l1[64];
  if (s_tagtouched || s_edit_i >= 0) return;
  cite_make_tag(current(), &s_src, t, sizeof t);
  lat(t, l1, sizeof l1);
  s_busy = 1; set(s_tag, MUIA_String_Contents, (ULONG)l1); s_busy = 0;
}
/* the value typed goes into the source */
static void s_store(void)
{
  LONG a = -1;
  ULONG corp = 0;
  char u[600], l1[600];
  const char *k;
  get(s_fields, MUIA_List_Active, &a);
  if (a < 0 || a >= s_n) return;
  k = s_keys[a];
  wp_latin1_utf8(gstr(s_value), u, sizeof u);
  get(s_corp, MUIA_Selected, &corp);
  if (cite_is_role(k)) cite_set_names(&s_src, k, u, (int)corp); else cite_set(&s_src, k, u);
  s_line((int)a, l1, sizeof l1);
  s_busy = 1;
  DoMethod(s_fields, MUIM_List_Remove, a);
  DoMethod(s_fields, MUIM_List_InsertSingle, (ULONG)l1, a);
  set(s_fields, MUIA_List_Active, a);
  s_busy = 0;
  s_autotag();
  s_preview();
}
/* list: 0 current, 1 master; i: its index (-1 a new source) */
static void s_open(int list, int i, int citeafter)
{
  int t;
  char l1[64];
  if (!swin) {
    Object *c;
    for (t = 0; t < cite_ntypes && t < 23; t++) typel[t] = tr(cite_types[t].label);
    typel[t] = NULL;
    c = VGroup,
      Child, HGroup, Child, WLabel(tr("Type of Source")), Child, s_type = CycleObject, MUIA_Cycle_Entries, (ULONG)typel, MUIA_CycleChain, 1, End, End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Bibliography Fields"), End,
      Child, s_fields = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, LFONT, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, W50, End,
      Child, HGroup,
        Child, s_flabel = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_FixWidthTxt, (ULONG)"Conference Publication Na", End,
        Child, s_value = StringObject, StringFrame, MUIA_String_MaxLen, 500, MUIA_CycleChain, 1, End,
        Child, s_names = QButton(tr("_Edit...")),
      End,
      Child, HGroup,
        Child, s_corp = CheckMark(FALSE), Child, WLabel(tr("Corporate Author")), Child, HSpace(0),
        Child, s_all = CheckMark(FALSE), Child, WLabel(tr("Show All Bibliography Fields")),
      End,
      Child, HGroup, Child, WLabel(tr("Tag name")), Child, s_tag = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End, End,
      Child, s_prev = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
    End;
    if (!(swin = wp_dialog("Create Source", MAKE_ID('C','I','T','2'), c, "_OK", ID_CITE_SRC_OK))) return;
    DoMethod(s_type, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_TYPE);
    DoMethod(s_fields, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_FIELD);
    DoMethod(s_fields, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_NAMES);
    DoMethod(s_value, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_VALUE);
    DoMethod(s_corp, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_CORP);
    DoMethod(s_all, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_ALL);
    DoMethod(s_tag, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_TAG);
    DoMethod(s_names, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_SRC_NAMES);
  }
  cite_source_free(&s_src);
  s_edit_list = list; s_edit_i = i; s_cite_after = citeafter; s_tagtouched = i >= 0;
  s_oldtag[0] = 0;
  if (i >= 0) {
    const CiteList *l = list ? &master : current();
    if (!l || i >= l->n) return;
    cite_source_copy(&s_src, &l->s[i]);
    snprintf(s_oldtag, sizeof s_oldtag, "%s", cite_get(&s_src, CITE_TAG));
    if (!cite_get(&s_src, CITE_TYPE)[0]) cite_set(&s_src, CITE_TYPE, CITE_BOOK);    /* (a placeholder being filled in) */
  } else cite_set(&s_src, CITE_TYPE, CITE_BOOK);
  t = cite_type_index(cite_get(&s_src, CITE_TYPE));
  s_busy = 1;
  set(swin, MUIA_Window_Title, (ULONG)tr(i >= 0 ? "Edit Source" : "Create Source"));
  set(s_type, MUIA_Cycle_Active, t >= 0 ? t : 0);
  set(s_all, MUIA_Selected, FALSE);
  set(s_fields, MUIA_List_Active, 0);
  lat(cite_get(&s_src, CITE_TAG), l1, sizeof l1);
  set(s_tag, MUIA_String_Contents, (ULONG)l1);
  s_busy = 0;
  s_fill();
  s_pick();
  s_autotag();
  s_preview();
  set(swin, MUIA_Window_Open, TRUE);
  set(swin, MUIA_Window_ActiveObject, (ULONG)s_value);
}
static void man_refill(void);
static void s_ok(void)
{
  char tag[64];
  CiteList *cur = current();
  int k;
  wp_latin1_utf8(gstr(s_tag), tag, sizeof tag);
  { char *c = tag; while (*c) { if (*c == ' ') memmove(c, c + 1, strlen(c)); else c++; } }
  if (!tag[0]) cite_make_tag(cur, &s_src, tag, sizeof tag);
  /* a new source's tag must be new (as is an edited one's, if it changed) */
  if ((s_edit_i < 0 || strcmp(tag, s_oldtag)) && cur && cite_find(cur, tag) >= 0 && !(s_edit_i >= 0 && s_edit_list == 0 && cite_find(cur, tag) == s_edit_i)) {
    char t2[64];
    cite_set(&s_src, CITE_TAG, tag);
    cite_make_tag(cur, &s_src, t2, sizeof t2);
    if (!wp_ask("_Use it|_Cancel", "This tag is used already. Use %s instead?", t2)) return;
    snprintf(tag, sizeof tag, "%s", t2);
  }
  cite_set(&s_src, CITE_TAG, tag);
  if (!cite_get(&s_src, CITE_GUID)[0]) { char g[48]; cite_make_guid(g); cite_set(&s_src, CITE_GUID, g); }
  set(swin, MUIA_Window_Open, FALSE);
  if (s_edit_i < 0) {
    /* a new source goes into both lists, as in Word */
    master_load();
    cite_add(&master, &s_src); master_save();
    if (cur) cite_add(cur, &s_src);
    doc_sources_changed();
    if (s_cite_after) put_citation(tag);
  } else if (s_edit_list == 1) {
    if (s_edit_i < master.n) { cite_remove(&master, s_edit_i); }
    cite_add(&master, &s_src); master_save();
    /* the same source in the document changes with it */
    if (cur && (k = cite_find(cur, s_oldtag)) >= 0) { cite_remove(cur, k); cite_add(cur, &s_src); doc_sources_changed(); }
  } else if (cur && s_edit_i < cur->n) {
    CiteSource keep = cur->s[s_edit_i];
    cur->s[s_edit_i] = s_src;
    memset(&s_src, 0, sizeof s_src);
    cite_source_free(&keep);
    if ((k = cite_find(&master, s_oldtag)) >= 0) { cite_remove(&master, k); cite_add(&master, &cur->s[s_edit_i]); master_save(); }
    if (strcmp(s_oldtag, tag)) cite_retag(doc(), s_oldtag, tag);
    doc_sources_changed();
  }
  if (s_edit_i >= 0) { cite_update_all(doc()); changed(); }
  man_refill();
  if (!s_cite_after) back_to_doc();
}

/* ---- Edit Name (opened from the source window for the field chosen) */
static void n_open(void)
{
  LONG a = -1;
  const CiteRole *r;
  int i;
  get(s_fields, MUIA_List_Active, &a);
  if (a < 0 || a >= s_n || !cite_is_role(s_keys[a])) { DisplayBeep(NULL); return; }
  s_role = s_keys[a];
  if (!nwin) {
    Object *c = VGroup,
      Child, ColGroup(2),
        Child, WLabel(tr("Last:")), Child, n_last = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
        Child, WLabel(tr("First:")), Child, n_first = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
        Child, WLabel(tr("Middle:")), Child, n_mid = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, HSpace(0), Child, n_addb = QButton(tr("_Add")), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Names"), End,
      Child, HGroup,
        Child, n_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, LFONT, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n", End,
        Child, VGroup, MUIA_HorizWeight, 0, Child, n_upb = QButton(tr("_Up")), Child, n_downb = QButton(tr("D_own")), Child, n_delb = QButton(tr("_Delete")), Child, VSpace(0), End,
      End,
    End;
    if (!(nwin = wp_dialog("Edit Name", MAKE_ID('C','I','T','3'), c, "_OK", ID_CITE_NAME_OK))) return;
    DoMethod(n_addb, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_NAME_ADD);
    DoMethod(n_upb, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_NAME_UP);
    DoMethod(n_downb, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_NAME_DOWN);
    DoMethod(n_delb, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_NAME_DEL);
    DoMethod(n_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_NAME_PICK);
    DoMethod(n_mid, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_NAME_ADD);
  }
  n_clear();
  r = cite_role_c(&s_src, s_role);
  for (i = 0; r && !r->corp && i < r->np && n_n < 64; i++) {
    n_p[n_n].last = str_dup(r->p[i].last); n_p[n_n].first = str_dup(r->p[i].first); n_p[n_n].middle = str_dup(r->p[i].middle);
    if (n_p[n_n].last && n_p[n_n].first && n_p[n_n].middle) n_n++;
  }
  set(nwin, MUIA_Window_Title, (ULONG)tr(cite_field_label(s_role)));
  set(n_last, MUIA_String_Contents, (ULONG)""); set(n_first, MUIA_String_Contents, (ULONG)""); set(n_mid, MUIA_String_Contents, (ULONG)"");
  n_fill();
  set(nwin, MUIA_Window_Open, TRUE);
  set(nwin, MUIA_Window_ActiveObject, (ULONG)n_last);
}
static void n_add(void)
{
  if (!gstr(n_last)[0] && !gstr(n_first)[0]) { DisplayBeep(NULL); return; }
  if (n_n >= 64) return;
  n_p[n_n].last = u8dup(gstr(n_last)); n_p[n_n].first = u8dup(gstr(n_first)); n_p[n_n].middle = u8dup(gstr(n_mid));
  if (!n_p[n_n].last || !n_p[n_n].first || !n_p[n_n].middle) return;
  n_n++;
  set(n_last, MUIA_String_Contents, (ULONG)""); set(n_first, MUIA_String_Contents, (ULONG)""); set(n_mid, MUIA_String_Contents, (ULONG)"");
  n_fill();
  set(nwin, MUIA_Window_ActiveObject, (ULONG)n_last);
}
static void n_move(int dir)
{
  LONG a = -1;
  get(n_list, MUIA_List_Active, &a);
  if (a < 0 || a >= n_n) return;
  if (dir == 0 || dir == 2) {
    char l1[130];
    if (dir == 0) {
      lat(n_p[a].last, l1, sizeof l1); set(n_last, MUIA_String_Contents, (ULONG)l1);
      lat(n_p[a].first, l1, sizeof l1); set(n_first, MUIA_String_Contents, (ULONG)l1);
      lat(n_p[a].middle, l1, sizeof l1); set(n_mid, MUIA_String_Contents, (ULONG)l1);
    }
    free(n_p[a].last); free(n_p[a].first); free(n_p[a].middle);
    memmove(&n_p[a], &n_p[a + 1], (size_t)(n_n - a - 1) * sizeof n_p[0]);
    n_n--;
    n_fill();
    return;
  }
  if (a + dir < 0 || a + dir >= n_n) return;
  { CitePerson t = n_p[a]; n_p[a] = n_p[a + dir]; n_p[a + dir] = t; }
  n_fill();
  set(n_list, MUIA_List_Active, a + dir);
}
static void n_ok(void)
{
  CiteRole *r;
  int i;
  if (gstr(n_last)[0] || gstr(n_first)[0]) n_add();      /* (a name typed but not added yet) */
  set(nwin, MUIA_Window_Open, FALSE);
  cite_set_names(&s_src, s_role, "", 0);
  if (n_n && (r = cite_role(&s_src, s_role, 1))) {
    for (i = 0; i < n_n; i++) {
      CitePerson *np = (CitePerson *)realloc(r->p, (size_t)(r->np + 1) * sizeof(CitePerson));
      if (!np) break;
      r->p = np;
      np[r->np] = n_p[i];
      r->np++;
    }
    n_n = 0;
  }
  n_clear();
  s_fill(); s_pick(); s_autotag(); s_preview();
  set(swin, MUIA_Window_Activate, TRUE);
}

/* ------------------------------------------------------------------ */
/* Source Manager */
static Object *mwin, *m_search, *m_sort, *m_master, *m_cur, *m_prev, *m_tocur, *m_tomaster, *m_del, *m_edit, *m_new, *m_mlab, *m_clab;
static int m_mi[1024], m_mn, m_ci[1024], m_cn;    /* the entries shown: list indexes */
static int m_focus;                               /* the list last chosen in: 0 current, 1 master */
static const char *sortl[5];
static const char *const sort_names[4] = { "Sort by Author", "Sort by Tag", "Sort by Title", "Sort by Year" };
/* whether s matches the search text (Latin-1, any case) */
static int matches(const char *line, const char *q)
{
  size_t n = strlen(q), i, j;
  if (!n) return 1;
  for (i = 0; line[i]; i++) {
    for (j = 0; j < n && line[i + j]; j++) {
      char a = line[i + j], b = q[j];
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      if (a != b) break;
    }
    if (j == n) return 1;
  }
  return 0;
}
static int m_sortby;
static const CiteList *m_sortlist;
static void key_of(const CiteSource *s, char *out, size_t cap)
{
  char a[200];
  const char *k;
  switch (m_sortby) {
  case 1: k = cite_get(s, CITE_TAG); break;
  case 2: k = cite_get(s, CITE_TITLE); break;
  case 3: k = cite_get(s, CITE_YEAR); break;
  default: cite_names_text(s, CITE_AUTHOR, a, sizeof a); k = a[0] ? a : cite_get(s, CITE_TITLE); break;
  }
  snprintf(out, cap, "%s", k);
  for (; *out; out++) if (*out >= 'A' && *out <= 'Z') *out += 32;
}
static void sort_idx(int *ix, int n)
{
  int i, j;
  char a[200], b[200];
  for (i = 1; i < n; i++) {
    int v = ix[i];
    key_of(&m_sortlist->s[v], a, sizeof a);
    for (j = i; j > 0; j--) { key_of(&m_sortlist->s[ix[j - 1]], b, sizeof b); if (strcmp(b, a) <= 0) break; ix[j] = ix[j - 1]; }
    ix[j] = v;
  }
}
static void m_fill_one(Object *lv, const CiteList *l, int *ix, int *n, int mark)
{
  char q[100], line[500];
  int i;
  snprintf(q, sizeof q, "%s", gstr(m_search));
  *n = 0;
  for (i = 0; l && i < l->n && *n < 1024; i++) {
    src_line(&l->s[i], 0, line, sizeof line);
    if (matches(line, q)) ix[(*n)++] = i;
  }
  m_sortlist = l;
  if (l) sort_idx(ix, *n);
  set(lv, MUIA_List_Quiet, TRUE);
  DoMethod(lv, MUIM_List_Clear);
  for (i = 0; i < *n; i++) {
    const CiteSource *s = &l->s[ix[i]];
    src_line(s, mark ? (cite_is_cited(doc(), cite_get(s, CITE_TAG)) ? 2 : 1) : 0, line, sizeof line);
    DoMethod(lv, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  set(lv, MUIA_List_Quiet, FALSE);
}
static void m_show(void)
{
  LONG a = -1;
  char p[700];
  const CiteList *l = m_focus ? &master : current();
  int i = -1;
  get(m_focus ? m_master : m_cur, MUIA_List_Active, &a);
  if (a >= 0) i = m_focus ? (a < m_mn ? m_mi[a] : -1) : (a < m_cn ? m_ci[a] : -1);
  if (l && i >= 0) src_preview(l, i, p, sizeof p); else p[0] = 0;
  set(m_prev, MUIA_Text_Contents, (ULONG)p);
}
static void man_refill(void)
{
  char t[120];
  if (!mwin) return;
  { ULONG o = 0; get(mwin, MUIA_Window_Open, &o); if (!o) return; }
  m_sortby = 0; { ULONG v = 0; get(m_sort, MUIA_Cycle_Active, &v); m_sortby = (int)v; }
  m_fill_one(m_master, &master, m_mi, &m_mn, 0);
  m_fill_one(m_cur, current(), m_ci, &m_cn, 1);
  snprintf(t, sizeof t, "%s (%d)", tr("Master List"), master.n); set(m_mlab, MUIA_Text_Contents, (ULONG)t);
  snprintf(t, sizeof t, "%s (%d)", tr("Current List"), current() ? current()->n : 0); set(m_clab, MUIA_Text_Contents, (ULONG)t);
  m_show();
}
static void man_open(void)
{
  int i;
  master_load();
  if (!mwin) {
    Object *c;
    static char m_legend[120];
    snprintf(m_legend, sizeof m_legend, "\xBB %s   ? %s", tr("cited source"), tr("placeholder source"));
    for (i = 0; i < 4; i++) sortl[i] = tr(sort_names[i]);
    sortl[4] = NULL;
    c = VGroup,
      Child, HGroup, Child, WLabel(tr("Search:")), Child, m_search = StringObject, StringFrame, MUIA_String_MaxLen, 90, MUIA_CycleChain, 1, End,
        Child, m_sort = CycleObject, MUIA_Cycle_Entries, (ULONG)sortl, MUIA_CycleChain, 1, End, End,
      Child, HGroup,
        Child, VGroup,
          Child, m_mlab = TextObject, MUIA_Text_Contents, (ULONG)"", End,
          Child, m_master = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, LFONT, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, W30, End,
        End,
        Child, VGroup, MUIA_HorizWeight, 0,
          Child, VSpace(0),
          Child, m_tocur = QButton(tr("_Copy ->")),
          Child, m_tomaster = QButton(tr("<- Co_py")),
          Child, m_del = QButton(tr("_Delete")),
          Child, m_edit = QButton(tr("_Edit...")),
          Child, m_new = QButton(tr("_New...")),
          Child, VSpace(0),
        End,
        Child, VGroup,
          Child, m_clab = TextObject, MUIA_Text_Contents, (ULONG)"", End,
          Child, m_cur = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, LFONT, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, W30, End,
        End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)m_legend, End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Preview:"), End,
      Child, m_prev = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, MUIA_FixHeightTxt, (ULONG)"\n\n", End,
    End;
    if (!(mwin = wp_dialog("Source Manager", MAKE_ID('C','I','T','1'), c, "C_lose", ID_CITE_MAN_CLOSE))) return;
    DoMethod(m_search, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_SEARCH);
    DoMethod(m_sort, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_SEARCH);
    DoMethod(m_master, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_MASTER);
    DoMethod(m_cur, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_CURRENT);
    DoMethod(m_master, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_TOCUR);
    DoMethod(m_cur, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_EDIT);
    DoMethod(m_tocur, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_TOCUR);
    DoMethod(m_tomaster, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_TOMASTER);
    DoMethod(m_del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_DELETE);
    DoMethod(m_edit, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_EDIT);
    DoMethod(m_new, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_MAN_NEW);
  }
  m_focus = 0;
  set(mwin, MUIA_Window_Open, TRUE);
  man_refill();
  set(mwin, MUIA_Window_ActiveObject, (ULONG)m_cur);
}
/* the source chosen in the list last used: its list (0 current, 1 master) and index, -1 none */
static int m_chosen(void)
{
  LONG a = -1;
  get(m_focus ? m_master : m_cur, MUIA_List_Active, &a);
  if (a < 0) return -1;
  return m_focus ? (a < m_mn ? m_mi[a] : -1) : (a < m_cn ? m_ci[a] : -1);
}
static void man_cmd(ULONG id)
{
  CiteList *cur = current();
  int i;
  switch (id) {
  case ID_CITE_MAN_MASTER: m_focus = 1; m_show(); break;
  case ID_CITE_MAN_CURRENT: m_focus = 0; m_show(); break;
  case ID_CITE_MAN_SEARCH: man_refill(); break;
  case ID_CITE_MAN_TOCUR:
    m_focus = 1;
    if ((i = m_chosen()) >= 0 && cur) {
      if (cite_find(cur, cite_get(&master.s[i], CITE_TAG)) >= 0 &&
          !wp_ask("_Yes|_No", "A source with the tag %s is in the current list already. Overwrite it?", cite_get(&master.s[i], CITE_TAG))) break;
      cite_add(cur, &master.s[i]); doc_sources_changed(); cite_update_all(doc()); changed(); man_refill();
    }
    break;
  case ID_CITE_MAN_TOMASTER:
    m_focus = 0;
    if ((i = m_chosen()) >= 0 && cur) {
      if (cite_find(&master, cite_get(&cur->s[i], CITE_TAG)) >= 0 &&
          !wp_ask("_Yes|_No", "A source with the tag %s is in the master list already. Overwrite it?", cite_get(&cur->s[i], CITE_TAG))) break;
      cite_add(&master, &cur->s[i]); master_save(); man_refill();
    }
    break;
  case ID_CITE_MAN_DELETE:
    if ((i = m_chosen()) < 0) break;
    if (m_focus) { cite_remove(&master, i); master_save(); }
    else if (cur) {
      /* a cited source can't go (Word says so too) */
      if (cite_is_cited(doc(), cite_get(&cur->s[i], CITE_TAG))) { wp_ask("_OK", "This source is cited in the document and cannot be deleted.%s", ""); break; }
      cite_remove(cur, i); doc_sources_changed(); cite_update_all(doc()); changed();
    }
    man_refill();
    break;
  case ID_CITE_MAN_EDIT:
    if ((i = m_chosen()) >= 0) s_open(m_focus, i, 0);
    break;
  case ID_CITE_MAN_NEW: s_open(0, -1, 0); break;
  }
}

/* ------------------------------------------------------------------ */
/* Insert Citation (the window from the menu: the current list, then the master list's others) */
static Object *iwin, *i_list;
static int i_from[1024], i_idx[1024], i_n;      /* each entry's list (0 current, 1 master) and index */
static void i_open(void)
{
  CiteList *cur = current();
  int i;
  char line[500];
  master_load();
  if (!iwin) {
    Object *c = VGroup,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Sources"), End,
      Child, i_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, LFONT, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, W50, End,
    End;
    if (!(iwin = wp_dialog("Insert Citation", MAKE_ID('C','I','T','5'), c, "_Insert", ID_CITE_INSERT_OK))) return;
    DoMethod(i_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_INSERT_OK);
  }
  i_n = 0;
  set(i_list, MUIA_List_Quiet, TRUE);
  DoMethod(i_list, MUIM_List_Clear);
  for (i = 0; cur && i < cur->n && i_n < 1024; i++) {
    src_line(&cur->s[i], 0, line, sizeof line);
    i_from[i_n] = 0; i_idx[i_n++] = i;
    DoMethod(i_list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  for (i = 0; i < master.n && i_n < 1024; i++) {
    char l2[520];
    if (cur && cite_find(cur, cite_get(&master.s[i], CITE_TAG)) >= 0) continue;
    src_line(&master.s[i], 0, line, sizeof line);
    snprintf(l2, sizeof l2, "%s [%s]", line, tr("Master List"));
    i_from[i_n] = 1; i_idx[i_n++] = i;
    DoMethod(i_list, MUIM_List_InsertSingle, (ULONG)l2, MUIV_List_Insert_Bottom);
  }
  set(i_list, MUIA_List_Quiet, FALSE);
  set(i_list, MUIA_List_Active, 0);
  set(iwin, MUIA_Window_Open, TRUE);
  set(iwin, MUIA_Window_ActiveObject, (ULONG)i_list);
}
static void i_ok(void)
{
  LONG a = -1;
  CiteList *cur = current();
  char tag[64];
  get(i_list, MUIA_List_Active, &a);
  set(iwin, MUIA_Window_Open, FALSE);
  if (a < 0 || a >= i_n || !cur) { back_to_doc(); return; }
  if (i_from[a]) { cite_add(cur, &master.s[i_idx[a]]); doc_sources_changed(); }
  snprintf(tag, sizeof tag, "%s", cite_get(i_from[a] ? &master.s[i_idx[a]] : &cur->s[i_idx[a]], CITE_TAG));
  put_citation(tag);
  back_to_doc();
}

/* ---- Add New Placeholder */
static Object *pwin, *p_tag;
static void p_open(void)
{
  char t[64];
  CiteList *cur = current();
  int k;
  if (!pwin) {
    Object *c = VGroup,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Type the tag name of the source. You can add more\ninformation to this source later by clicking Edit in the\nSource Manager."), End,
      Child, p_tag = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
    End;
    if (!(pwin = wp_dialog("Placeholder Name", MAKE_ID('C','I','T','6'), c, "_OK", ID_CITE_PH_OK))) return;
    DoMethod(p_tag, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CITE_PH_OK);
  }
  for (k = 1; ; k++) { snprintf(t, sizeof t, CITE_PLACEHOLDER, k); if (!cur || cite_find(cur, t) < 0) break; }
  set(p_tag, MUIA_String_Contents, (ULONG)t);
  set(pwin, MUIA_Window_Open, TRUE);
  set(pwin, MUIA_Window_ActiveObject, (ULONG)p_tag);
}
static void p_ok(void)
{
  char tag[64];
  CiteList *cur = current();
  CiteSource s;
  wp_latin1_utf8(gstr(p_tag), tag, sizeof tag);
  { char *c = tag; while (*c) { if (*c == ' ') memmove(c, c + 1, strlen(c)); else c++; } }
  set(pwin, MUIA_Window_Open, FALSE);
  if (!tag[0] || !cur) { back_to_doc(); return; }
  if (cite_find(cur, tag) < 0) {
    memset(&s, 0, sizeof s);
    cite_set(&s, CITE_TAG, tag);
    cite_add(cur, &s);
    cite_source_free(&s);
    doc_sources_changed();
  }
  put_citation(tag);
  back_to_doc();
}

/* ------------------------------------------------------------------ */
/* Edit Citation: pages, suppress author, year, title */
static Object *ewin, *e_pages, *e_auth, *e_year, *e_title;
static Pos e_at;
static void e_open(void)
{
  Doc *d = doc();
  Pos lo, hi, s;
  int k;
  char pages[64] = "", l1[64];
  int sup = 0;
  sel(&lo, &hi);
  if ((k = cite_field_at(d, lo, 0, &s, NULL)) < 0) { DisplayBeep(NULL); return; }
  e_at = s;
  if (!ewin) {
    Object *c = VGroup,
      Child, HGroup, Child, WLabel(tr("Pages:")), Child, e_pages = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End, End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Suppress"), End,
      Child, HGroup,
        Child, e_auth = CheckMark(FALSE), Child, WLabel(tr("Author")),
        Child, e_year = CheckMark(FALSE), Child, WLabel(tr("Year")),
        Child, e_title = CheckMark(FALSE), Child, WLabel(tr("Title")), Child, HSpace(0),
      End,
    End;
    if (!(ewin = wp_dialog("Edit Citation", MAKE_ID('C','I','T','4'), c, "_OK", ID_CITE_EDIT_OK))) return;
  }
  {
    /* what the code says now (\p, \n, \y, \t) */
    const char *code = d->fields[k].code;
    const char *p = strstr(code, "\\p ");
    if (p) {
      size_t n = 0;
      p += 3;
      while (*p == ' ') p++;
      if (*p == '"') { p++; while (*p && *p != '"' && n + 1 < sizeof pages) pages[n++] = *p++; }
      else while (*p && *p != ' ' && n + 1 < sizeof pages) pages[n++] = *p++;
      pages[n] = 0;
    }
    if (strstr(code, "\\n")) sup |= CITE_NO_AUTHOR;
    if (strstr(code, "\\y")) sup |= CITE_NO_YEAR;
    if (strstr(code, "\\t")) sup |= CITE_NO_TITLE;
  }
  lat(pages, l1, sizeof l1);
  set(e_pages, MUIA_String_Contents, (ULONG)l1);
  set(e_auth, MUIA_Selected, (sup & CITE_NO_AUTHOR) != 0);
  set(e_year, MUIA_Selected, (sup & CITE_NO_YEAR) != 0);
  set(e_title, MUIA_Selected, (sup & CITE_NO_TITLE) != 0);
  set(ewin, MUIA_Window_Open, TRUE);
  set(ewin, MUIA_Window_ActiveObject, (ULONG)e_pages);
}
/* the tags a citation names (its own, then \m ones) */
static int tags_of(const char *code, char tags[][64], int max)
{
  int n = 0;
  const char *p = code;
  char tok[128];
  int first = 1, nextm = 0;
  while (*p && n < max) {
    size_t k = 0;
    while (*p == ' ') p++;
    if (!*p) break;
    if (*p == '"') { p++; while (*p && *p != '"') { if (k + 1 < sizeof tok) tok[k++] = *p; p++; } if (*p) p++; }
    else while (*p && *p != ' ') { if (k + 1 < sizeof tok) tok[k++] = *p; p++; }
    tok[k] = 0;
    if (first) { first = 0; continue; }
    if (tok[0] == '\\') { char c = tok[1] | 32; nextm = c == 'm'; if (strchr("lfspv", c) && c) { while (*p == ' ') p++; if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; } else while (*p && *p != ' ') p++; } continue; }
    if (nextm || n == 0) { snprintf(tags[n++], 64, "%s", tok); nextm = 0; }
  }
  return n;
}
static void e_ok(void)
{
  Doc *d = doc();
  ULONG a = 0, y = 0, t = 0;
  char pages[64], code[600], tags[16][64];
  const char *more[15];
  int k, n, i, sup = 0;
  Pos s, en;
  set(ewin, MUIA_Window_Open, FALSE);
  if ((k = cite_field_at(d, e_at, 0, &s, NULL)) < 0) { back_to_doc(); return; }
  get(e_auth, MUIA_Selected, &a); get(e_year, MUIA_Selected, &y); get(e_title, MUIA_Selected, &t);
  if (a) sup |= CITE_NO_AUTHOR;
  if (y) sup |= CITE_NO_YEAR;
  if (t) sup |= CITE_NO_TITLE;
  wp_latin1_utf8(gstr(e_pages), pages, sizeof pages);
  n = tags_of(d->fields[k].code, tags, 16);
  if (!n) { back_to_doc(); return; }
  for (i = 1; i < n; i++) more[i - 1] = tags[i];
  cite_code(code, sizeof code, tags[0], pages, sup, more, n - 1);
  doc_begin(d, s);
  if (field_set_code(d, s, code)) field_update(d, s, s, env());
  doc_end(d, s);
  changed();
  if (field_end(d, s, &en)) { en.off += 4; select_range(s, en); }
  back_to_doc();
}
/* Edit Source from a citation: the source it names (the first) */
static void edit_src_of_citation(void)
{
  Doc *d = doc();
  Pos lo, hi;
  int k, i;
  char tags[16][64];
  sel(&lo, &hi);
  if ((k = cite_field_at(d, lo, 0, NULL, NULL)) < 0 || !tags_of(d->fields[k].code, tags, 16) || !current() || (i = cite_find(current(), tags[0])) < 0) {
    DisplayBeep(NULL);
    return;
  }
  s_open(0, i, 0);
}

/* ------------------------------------------------------------------ */
/* the other commands */
static void set_style(int cs)
{
  CiteList *cur = current();
  if (!cur || cs < 0 || cs >= CITE_NSTYLES) return;
  cite_list_set_style(cur, cs);
  doc_sources_changed();
  cite_update_all(doc());
  changed();
  man_refill();
}
static void insert_bib(int kind)
{
  Doc *d = doc();
  Pos lo, hi, at;
  static const int kinds[4] = { 1, 2, 3, 0 };
  sel(&lo, &hi);
  at = lo;
  if (kind < 0 || kind > 3) return;
  cite_insert_bib(d, &at, kinds[kind]);
  changed();
  select_range(at, at);
}
static void to_static(void)
{
  Doc *d = doc();
  Pos lo, hi;
  int n;
  sel(&lo, &hi);
  n = cite_to_static(d, lo, hi, 1);
  if (!n) DisplayBeep(NULL);
  changed();
}

/* ------------------------------------------------------------------ */
/* the ribbon: References > Citations & Bibliography */
#define ICON_CITE "ci R 2 1 13 15 ca F 4 3 6 5 L 6 5 5 8 F 9 3 11 5 L 11 5 10 8 ci L 4 11 11 11 L 4 13 9 13"
#define ICON_SOURCES "ci R 1 2 4 14 R 5 4 8 14 ca R 9 1 14 14 L 10 4 13 4 ci L 2 5 3 5 L 6 7 7 7"
#define ICON_BIB "ci R 2 1 13 15 ca L 4 4 11 4 cd L 6 6 11 6 ca L 4 9 11 9 cd L 6 11 11 11"
#define ICON_STYLE "ci R 2 1 13 15 ca B Aa"
static const char *cite_lab[130];
static char cite_lines[128][80];
static RibbonList cite_rl = { cite_lab, 0, PL_TEXT, NULL };
static int cite_list_n;                 /* sources listed (then Add New Source, Add New Placeholder) */
static const char *style_lab[CITE_NSTYLES];
static const RibbonList style_rl = { style_lab, CITE_NSTYLES, PL_TEXT, NULL };
static const char *const bib_names[4] = { "Bibliography", "References", "Works Cited", "Insert Bibliography" };
static const char *bib_lab[4];
static const RibbonList bib_rl = { bib_lab, 4, PL_TEXT, NULL };
static const Item cite_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Insert Citation", ID_CITE_INSERT, 0, 0, 0, 0, "Cite a source: one of the document's, or a new one", &cite_rl, ICON_CITE },
  { K_BUTTON, S_SMALL, 0, 0, "Manage Sources", ID_CITE_MANAGE, 0, 0, F_NEWCOL, 0, "The sources: the master list and the document's", NULL, ICON_SOURCES },
  { K_FIELD, S_SMALL, 0, 0, "Style:", ID_CITE_STYLE, 0, 0, 0, 64, "The citation and bibliography style", &style_rl, NULL },
  { K_DROP, S_SMALL, 0, 0, "Bibliography", ID_CITE_BIB, 0, 0, 0, 0, "A bibliography of the document's sources", &bib_rl, ICON_BIB } };
static const WpGroup groups[] = { { TAB_REFERENCES, -1, { "Citations & Bibliography", G_COLS, cite_items, 4 } } };

/* the menus: Insert > Citations & Bibliography, and Citation Style */
static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Citations & Bibliography", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Insert Citation...", 0, 0, 0, (APTR)ID_CITE_INSERTWIN } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Add New Source...", 0, 0, 0, (APTR)ID_CITE_NEWSRC } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Add New Placeholder...", 0, 0, 0, (APTR)ID_CITE_PLACEHOLDER } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Manage Sources...", 0, 0, 0, (APTR)ID_CITE_MANAGE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Edit Citation...", 0, 0, 0, (APTR)ID_CITE_EDIT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Edit Source...", 0, 0, 0, (APTR)ID_CITE_EDITSRC } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Bibliography", 0, 0, 0, (APTR)ID_CITE_BIB0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"References", 0, 0, 0, (APTR)ID_CITE_BIB1 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Works Cited", 0, 0, 0, (APTR)ID_CITE_BIB2 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Insert Bibliography", 0, 0, 0, (APTR)ID_CITE_BIB3 } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Update Citations and Bibliography", 0, 0, 0, (APTR)ID_CITE_UPDATE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Convert to Static Text", 0, 0, 0, (APTR)ID_CITE_STATIC } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Citation Style", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"APA Sixth Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_APA6) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"APA Seventh Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_APA7) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Chicago Sixteenth Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_CHICAGO16) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Chicago Seventeenth Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_CHICAGO17) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"GB7714 2005", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_GB7714) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"GOST - Name Sort", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_GOSTNAME) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"GOST - Title Sort", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_GOSTTITLE) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Harvard - Anglia 2008", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_HARVARD) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"IEEE 2006", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_IEEE) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"ISO 690 - First Element and Date", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_ISO690) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"ISO 690 - Numerical Reference", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_ISO690NUM) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"MLA Seventh Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_MLA7) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"MLA Eighth Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_MLA8) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"MLA Ninth Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_MLA9) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"SIST02", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_SIST02) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Turabian Sixth Edition", 0, 0, 0, (APTR)(ID_CITE_STYLE0 + CS_TURABIAN) } } };

/* the Insert Citation list made afresh from the document's sources */
static void cite_list_refresh(Doc *d)
{
  int i, n = 0;
  const CiteList *l = d && d->cite ? &d->cite->list : NULL;
  for (i = 0; l && i < l->n && n < 128; i++) { src_line(&l->s[i], 0, cite_lines[n], sizeof cite_lines[n]); cite_lab[n] = cite_lines[n]; n++; }
  cite_list_n = n;
  cite_lab[n++] = "Add New Source...";
  cite_lab[n++] = "Add New Placeholder...";
  cite_rl.n = n;
}
static void state(ULONG *on, const WpCtx *c)
{
  if (!c->doc) return;
  cite_list_refresh(c->doc);
  if (c->doc->nfields && cite_field_at(c->doc, c->caret, 0, NULL, NULL) >= 0) *on |= 1UL << ST_INCITE;
  if (c->doc->nfields && cite_field_at(c->doc, c->caret, 1, NULL, NULL) >= 0) *on |= 1UL << ST_INBIB;
  if (c->doc->cite && c->doc->cite->list.n) *on |= 1UL << ST_SOURCES;
}
static void field(ULONG id, const struct MUIP_Ribbon_State *st, char *out, size_t cap)
{
  Doc *d = doc();
  if (id == ID_CITE_STYLE) snprintf(out, cap, "%s", cite_styles[d ? cite_doc_style(d) : CS_APA6].shortname);
}
/* the right button on a citation or a bibliography: Word's citation menu */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  int incite, inbib;
  if ((c->where != WPC_TEXT && c->where != WPC_LINK) || !c->doc || !c->doc->nfields) return;
  incite = cite_field_at(c->doc, c->caret, 0, NULL, NULL) >= 0;
  inbib = !incite && cite_field_at(c->doc, c->caret, 1, NULL, NULL) >= 0;
  if (!incite && !inbib) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  if (incite) {
    wp_ctx_item(menu, tr("Edit Citation..."), WP_CTX_ID(ID_CITE_EDIT), 1);
    wp_ctx_item(menu, tr("Edit Source..."), WP_CTX_ID(ID_CITE_EDITSRC), 1);
    wp_ctx_item(menu, tr("Convert citation to static text"), WP_CTX_ID(ID_CITE_STATIC), 1);
  } else wp_ctx_item(menu, tr("Convert bibliography to static text"), WP_CTX_ID(ID_CITE_STATIC), 1);
  wp_ctx_item(menu, tr("Update Citations and Bibliography"), WP_CTX_ID(ID_CITE_UPDATE), 1);
}
static void init(void)
{
  int i;
  for (i = 0; i < CITE_NSTYLES; i++) style_lab[i] = cite_styles[i].name;
  for (i = 0; i < 4; i++) bib_lab[i] = bib_names[i];
  cite_list_refresh(NULL);
}
static void cleanup(void) { cite_source_free(&s_src); cite_list_free(&master); n_clear(); }

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp3_command(ULONG id, LONG value)
{
  if (!doc()) return WPR_DONE;
  if (id >= ID_CITE_STYLE0 && id < ID_CITE_STYLE0 + CITE_NSTYLES) { set_style((int)(id - ID_CITE_STYLE0)); return WPR_DONE; }
  switch (id) {
  case ID_CITE_INSERT:
    if (value >= 0 && value < cite_list_n && current() && value < current()->n) {
      char tag[64];
      snprintf(tag, sizeof tag, "%s", cite_get(&current()->s[value], CITE_TAG));
      put_citation(tag);
      return WPR_DONE;
    }
    if (value == cite_list_n) { s_open(0, -1, 1); return WPR_KEEPFOCUS; }
    if (value == cite_list_n + 1) { p_open(); return WPR_KEEPFOCUS; }
    i_open();
    return WPR_KEEPFOCUS;
  case ID_CITE_INSERTWIN: i_open(); return WPR_KEEPFOCUS;
  case ID_CITE_INSERT_OK: if (iwin) i_ok(); return WPR_DONE;
  case ID_CITE_NEWSRC: s_open(0, -1, 1); return WPR_KEEPFOCUS;
  case ID_CITE_PLACEHOLDER: p_open(); return WPR_KEEPFOCUS;
  case ID_CITE_PH_OK: if (pwin) p_ok(); return WPR_DONE;
  case ID_CITE_MANAGE: man_open(); return WPR_KEEPFOCUS;
  case ID_CITE_MAN_CLOSE: if (mwin) set(mwin, MUIA_Window_Open, FALSE); back_to_doc(); return WPR_DONE;
  case ID_CITE_MAN_MASTER: case ID_CITE_MAN_CURRENT: case ID_CITE_MAN_TOCUR: case ID_CITE_MAN_TOMASTER: case ID_CITE_MAN_DELETE:
  case ID_CITE_MAN_EDIT: case ID_CITE_MAN_NEW: case ID_CITE_MAN_SEARCH:
    if (mwin) man_cmd(id);
    return WPR_KEEPFOCUS;
  case ID_CITE_SRC_OK: if (swin) s_ok(); return WPR_DONE;
  case ID_CITE_SRC_TYPE: case ID_CITE_SRC_ALL:
    if (swin && !s_busy) {
      ULONG t = 0;
      get(s_type, MUIA_Cycle_Active, &t);
      if (id == ID_CITE_SRC_TYPE && t < (ULONG)cite_ntypes) cite_set(&s_src, CITE_TYPE, cite_types[t].xml);
      s_fill(); s_pick(); s_preview();
    }
    return WPR_KEEPFOCUS;
  case ID_CITE_SRC_FIELD: if (swin && !s_busy) s_pick(); return WPR_KEEPFOCUS;
  case ID_CITE_SRC_VALUE: case ID_CITE_SRC_CORP: if (swin && !s_busy) s_store(); return WPR_KEEPFOCUS;
  case ID_CITE_SRC_TAG: if (swin && !s_busy) s_tagtouched = 1; return WPR_KEEPFOCUS;
  case ID_CITE_SRC_NAMES: if (swin) n_open(); return WPR_KEEPFOCUS;
  case ID_CITE_NAME_OK: if (nwin) n_ok(); return WPR_KEEPFOCUS;
  case ID_CITE_NAME_ADD: if (nwin) n_add(); return WPR_KEEPFOCUS;
  case ID_CITE_NAME_UP: if (nwin) n_move(-1); return WPR_KEEPFOCUS;
  case ID_CITE_NAME_DOWN: if (nwin) n_move(1); return WPR_KEEPFOCUS;
  case ID_CITE_NAME_DEL: if (nwin) n_move(2); return WPR_KEEPFOCUS;
  case ID_CITE_NAME_PICK: if (nwin) n_move(0); return WPR_KEEPFOCUS;
  case ID_CITE_EDIT: e_open(); return WPR_KEEPFOCUS;
  case ID_CITE_EDIT_OK: if (ewin) e_ok(); return WPR_DONE;
  case ID_CITE_EDITSRC: edit_src_of_citation(); return WPR_KEEPFOCUS;
  case ID_CITE_STYLE: if (value >= 0) set_style((int)value); return WPR_DONE;
  case ID_CITE_BIB: insert_bib(value >= 0 ? (int)value : 0); return WPR_DONE;
  case ID_CITE_BIB0: case ID_CITE_BIB1: case ID_CITE_BIB2: case ID_CITE_BIB3: insert_bib((int)(id - ID_CITE_BIB0)); return WPR_DONE;
  case ID_CITE_UPDATE: cite_update_all(doc()); changed(); return WPR_DONE;
  case ID_CITE_STATIC: to_static(); return WPR_DONE;
  }
  return WPR_DONE;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp3_package = {
  "WP3", ID_WP3_FIRST, ID_WP3_LAST,
  wp3_command, state, field, ctxmenu, NULL, init, cleanup,
  groups, 1,
  menus, (int)(sizeof menus / sizeof menus[0])
};
