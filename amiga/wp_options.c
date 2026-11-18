/*
 * wp_options.c - WP26: the Options window and the settings framework (wphooks.h's wp_options_add,
 * wp_pref...). It belongs to WP26 (with wp_custom.c).
 *
 * Panes as in Word and WordCraft: General, Display, Proofing, Save, Language, Accessibility, Advanced,
 * then Customize Ribbon, Quick Access Toolbar and Keyboard (wp_custom.c makes those three). The
 * settings panes are built from rows: Quill's own (the File page's options: name, theme, text size,
 * language, units; Window and screen; AutoSave) and whatever the work packages add with
 * wp_options_add(). OK writes what changed and saves Quill.prefs; Cancel leaves everything.
 *
 * Quill.prefs: main.c reads the keys it knows and hands every other line here (wp_prefs_keep), and
 * writes them back after its own (wp_prefs_write), so packages keep settings in the same file.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_custom.h"
#include "wp_info.h"
#include "theme.h"
#include "lang.h"
#include "../core/custom.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#undef Label1
#undef LLabel1
#define Label1(l) QLabel(l, MUIO_Label_SingleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_SingleFrame | MUIO_Label_LeftAligned)

/* the window's own ids (WP26's block, after wp_custom.c's) */
enum { ID_OPT_OK = ID_WP26_LAST - 3, ID_OPT_CANCEL, ID_OPT_PANE };

/* ---- Quill.prefs */
static PrefStore store;
void wp_prefs_keep(const char *line) { pstore_line(&store, line); }
void wp_prefs_write(FILE *f) { pstore_write(&store, f); }
const char *wp_pref(const char *key) { return pstore_get(&store, key); }
PrefStore *wp_prefs_store(void) { return &store; }
long wp_pref_num(const char *key, long def) { const char *v = pstore_get(&store, key); return v ? atol(v) : def; }
void wp_pref_set(const char *key, const char *value)
{
  const char *old = pstore_get(&store, key);
  if ((!old && !value) || (old && value && !strcmp(old, value))) return;
  pstore_set(&store, key, value);
  wp_save_prefs();
}
void wp_prefs_each(const char *prefix, void (*fn)(const char *key, const char *val))
{
  int i;
  size_t n = strlen(prefix);
  for (i = 0; i < store.n; i++) if (!strncmp(store.kv[i].k, prefix, n)) fn(store.kv[i].k, store.kv[i].v);
}

/* ---- the rows */
#define MAXROWS 160
typedef struct { const WpOption *o; Object *gad; const char *ch[16]; } Row;
static Row rows[MAXROWS];
static int nrows;
static int built_rows;                 /* rows the window was made with */
static int own_added;
static LONG row_value(const WpOption *o)
{
  if (o->var) return *o->var;
  if (o->getv) return o->getv();
  return o->def;
}
static void row_store(const WpOption *o, LONG v)
{
  char t[16];
  if (o->var) *o->var = v;
  if (o->setv) o->setv(v);
  if (o->key) { sprintf(t, "%ld", (long)v); wp_pref_set(o->key, t); }
}
void wp_options_add(const WpOption *r, int n)
{
  int i;
  for (i = 0; i < n && nrows < MAXROWS; i++) {
    const WpOption *o = &r[i];
    rows[nrows].o = o; rows[nrows].gad = NULL;
    nrows++;
    /* a value kept under a key: read now (else the default) */
    if (o->key && (o->type == OT_CHECK || o->type == OT_CYCLE || o->type == OT_NUMBER)) {
      LONG v = wp_pref_num(o->key, o->def);
      if (o->var) *o->var = v; else if (o->setv) o->setv(v);
    }
    if (o->key && o->type == OT_STRING && o->str && o->cap > 0) {
      const char *v = wp_pref(o->key);
      if (v) { strncpy(o->str, v, (size_t)o->cap - 1); o->str[o->cap - 1] = 0; }
    }
  }
}

/* ---- Quill's own rows: the File page's options (applied together through main.c's wp_ui_set, as a
   new look or language makes the windows again), Window and screen, AutoSave */
void wp_ui_get(int *metric, int *dark, int *lang, int *large, char *name, size_t cap);
void wp_ui_set(int metric, int dark, int lang, int large, const char *name);
static LONG ui_metric, ui_dark, ui_lang, ui_large;
static char ui_name[64];
static const char *const themes[] = { "Light", "Dark", NULL };
static const char *const sizes[] = { "Normal", "Large", NULL };
static const char *const units[] = { "Inches", "Centimetres", NULL };
static const char *langs[UI_NLANG + 1];
static LONG as_get(void) { return wp24_autosave_on(); }
static void as_set(LONG v) { wp24_autosave_set((int)v); }
static const WpOption own[] = {
  { OP_GENERAL, OT_HEAD, "Personalize your copy of Quill" },
  { OP_GENERAL, OT_STRING, "User name:", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, ui_name, sizeof ui_name },
  { OP_GENERAL, OT_CYCLE, "Theme:", NULL, themes, 0, 1, 0, &ui_dark },
  { OP_GENERAL, OT_CYCLE, "Text size:", NULL, sizes, 0, 1, 0, &ui_large },
  { OP_GENERAL, OT_BUTTON, "Window and screen...", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_WINPREFS },
  { OP_GENERAL, OT_HEAD, "User interface options" },
  { OP_SAVE, OT_HEAD, "Save documents" },
  { OP_SAVE, OT_CHECK, "AutoSave a saved document after a pause in typing", NULL, NULL, 0, 1, 0, NULL, as_get, as_set },
  { OP_SAVE, OT_TEXT, "Recovery copies of unsaved documents are kept every minute (File > Info)." },
  { OP_LANGUAGE, OT_HEAD, "Office display language" },
  { OP_LANGUAGE, OT_CYCLE, "Interface language:", NULL, langs, 0, UI_NLANG - 1, 0, &ui_lang },
  { OP_ADVANCED, OT_HEAD, "Display" },
  { OP_ADVANCED, OT_CYCLE, "Show measurements in units of:", NULL, units, 0, 1, 1, &ui_metric },
};

/* ---- the window */
static const char *const panenames[OP_NPANES] = { "General", "Display", "Proofing", "Save", "Language", "Accessibility", "Advanced",
                                                  "Customize Ribbon", "Quick Access Toolbar", "Keyboard" };
static const char *panes_tr[OP_NPANES + 1];
static Object *owin, *opanes, *olist;
static Object *make_pane(int p)
{
  Object *g, *o;
  int i, any = 0;
  if (p >= OP_NSETTINGS) {
    Object *c = wp26_pane(p);
    return c ? c : TextObject, MUIA_Text_Contents, (ULONG)"", End;
  }
  g = VGroup, End;
  if (!g) return NULL;
  for (i = 0; i < nrows; i++) {
    const WpOption *r = rows[i].o;
    if (r->pane != p) continue;
    any = 1;
    o = NULL;
    switch (r->type) {
    case OT_HEAD: o = QTitleObject(tr(r->label)); break;
    case OT_TEXT: o = TextObject, MUIA_Text_Contents, (ULONG)tr(r->label), MUIA_Text_SetMin, FALSE, End; break;
    case OT_CHECK:
      o = HGroup, Child, rows[i].gad = CheckMark(FALSE), Child, TextObject, MUIA_Text_Contents, (ULONG)tr(r->label), MUIA_Text_SetMin, FALSE, End, End;
      break;
    case OT_CYCLE: {
      int k;
      for (k = 0; k < 15 && r->choices && r->choices[k]; k++) rows[i].ch[k] = r->choices == (const char *const *)langs ? r->choices[k] : tr(r->choices[k]);
      rows[i].ch[k] = NULL;
      o = HGroup, Child, Label1(tr(r->label)), Child, rows[i].gad = CycleObject, MUIA_Cycle_Entries, (ULONG)rows[i].ch, MUIA_CycleChain, 1, End, Child, HSpace(0), End;
      break; }
    case OT_NUMBER:
      o = HGroup, Child, Label1(tr(r->label)), Child, rows[i].gad = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"-0123456789", MUIA_String_MaxLen, 10,
            MUIA_FixWidthTxt, (ULONG)"00000000", MUIA_CycleChain, 1, End, Child, HSpace(0), End;
      break;
    case OT_STRING:
      o = HGroup, Child, Label1(tr(r->label)), Child, rows[i].gad = StringObject, StringFrame, MUIA_String_MaxLen, r->cap > 1 ? r->cap : 2, MUIA_CycleChain, 1, End, End;
      break;
    case OT_BUTTON:
      o = HGroup, Child, rows[i].gad = QButton(tr(r->label)), Child, HSpace(0), End;
      if (rows[i].gad) DoMethod(rows[i].gad, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, r->id);
      break;
    }
    if (o) DoMethod(g, OM_ADDMEMBER, (ULONG)o);
  }
  if (!any) DoMethod(g, OM_ADDMEMBER, (ULONG)(TextObject, MUIA_Text_Contents, (ULONG)tr("No settings here yet."), End));
  DoMethod(g, OM_ADDMEMBER, (ULONG)VSpace(0));
  return g;
}
static void make_window(void)
{
  Object *ok, *cancel;
  int p;
  if (owin) return;
  if (!own_added) { own_added = 1; wp_options_add(own, (int)(sizeof own / sizeof own[0])); }
  built_rows = nrows;
  for (p = 0; p < UI_NLANG; p++) langs[p] = ui_languages[p];
  langs[UI_NLANG] = NULL;
  for (p = 0; p < OP_NPANES; p++) panes_tr[p] = tr(panenames[p]);
  panes_tr[OP_NPANES] = NULL;
  opanes = PageGroup, End;
  if (!opanes) return;
  for (p = 0; p < OP_NPANES; p++) { Object *c = make_pane(p); if (c) DoMethod(opanes, OM_ADDMEMBER, (ULONG)c); }
  owin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Quill Options"), MUIA_Window_ID, MAKE_ID('W','2','6','O'),
    WindowContents, VGroup, QDLG,
      Child, HGroup, Child, Label1(tr("Options:")), Child, olist = CycleObject, MUIA_Cycle_Entries, (ULONG)panes_tr, MUIA_CycleChain, 1, End, Child, HSpace(0), End,
      Child, opanes,
      Child, HGroup, Child, HSpace(0), Child, ok = QButton(tr("_OK")), Child, cancel = QButton(tr("_Cancel")), End,
    End, End;
  if (!owin) { MUI_DisposeObject(opanes); opanes = NULL; return; }
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)owin);
  DoMethod(owin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)owin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)owin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_OPT_OK);
  DoMethod(olist, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)opanes, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
}
/* the gadgets show the values as they are now */
static void load_values(void)
{
  int i, m = 1, dk = 0, lg = 0, lgt = 0;
  wp_ui_get(&m, &dk, &lg, &lgt, ui_name, sizeof ui_name);
  ui_metric = m; ui_dark = dk; ui_lang = lg; ui_large = lgt;
  for (i = 0; i < built_rows; i++) {
    const WpOption *o = rows[i].o;
    Object *g = rows[i].gad;
    if (!g) continue;
    switch (o->type) {
    case OT_CHECK: set(g, MUIA_Selected, row_value(o) != 0); break;
    case OT_CYCLE: set(g, MUIA_Cycle_Active, row_value(o)); break;
    case OT_NUMBER: set(g, MUIA_String_Integer, row_value(o)); break;
    case OT_STRING: set(g, MUIA_String_Contents, (ULONG)(o->str ? o->str : "")); break;
    }
  }
  wp26_pane_load();
}
void wp_options_open(int pane)
{
  make_window();
  if (!owin) return;
  /* (a package that added rows after the window was made: made again) */
  if (nrows != built_rows) { wp_options_drop(); make_window(); if (!owin) return; }
  load_values();
  if (pane >= 0 && pane < OP_NPANES) set(olist, MUIA_Cycle_Active, pane);
  set(owin, MUIA_Window_Open, TRUE);
  {
    LONG open = 0;
    get(owin, MUIA_Window_Open, &open);
    if (!open) DisplayBeep(NULL);                /* (couldn't open: too big for the screen?) */
  }
}
/* OK: each changed value written, the rows' changed hooks called, the customizing panes kept; the
   File page's options last, as they may make every window again */
static void ok_pressed(void)
{
  int i, m, dk, lg, lgt;
  char nm[64];
  void (*called[MAXROWS])(void);
  int ncalled = 0;
  for (i = 0; i < built_rows; i++) {
    const WpOption *o = rows[i].o;
    Object *g = rows[i].gad;
    LONG v = 0, was;
    if (!g) continue;
    was = row_value(o);
    switch (o->type) {
    case OT_CHECK: get(g, MUIA_Selected, &v); v = v != 0; break;
    case OT_CYCLE: get(g, MUIA_Cycle_Active, &v); break;
    case OT_NUMBER: get(g, MUIA_String_Integer, &v); if (v < o->min) v = o->min; if (o->max > o->min && v > o->max) v = o->max; break;
    case OT_STRING: {
      char *s = NULL;
      get(g, MUIA_String_Contents, &s);
      if (o->str && s && strcmp(o->str, s)) {
        strncpy(o->str, s, (size_t)o->cap - 1); o->str[o->cap - 1] = 0;
        if (o->key) wp_pref_set(o->key, o->str);
        if (o->changed) called[ncalled++] = o->changed;
      }
      continue; }
    default: continue;
    }
    if (v != was) {
      int k;
      row_store(o, v);
      for (k = 0; k < ncalled && called[k] != o->changed; k++) ;
      if (o->changed && k == ncalled) called[ncalled++] = o->changed;
    }
  }
  set(owin, MUIA_Window_Open, FALSE);
  wp26_pane_save();
  for (i = 0; i < ncalled; i++) called[i]();
  wp_ui_get(&m, &dk, &lg, &lgt, nm, sizeof nm);
  if (m != ui_metric || dk != ui_dark || lg != ui_lang || lgt != ui_large || strcmp(nm, ui_name))
    wp_ui_set((int)ui_metric, (int)ui_dark, (int)ui_lang, (int)ui_large, ui_name);    /* (may drop this window) */
}
int wp_options_command(ULONG id)
{
  if (id == ID_OPT_OK && owin) { ok_pressed(); return 1; }
  return 0;
}
void wp_options_drop(void)
{
  int i;
  if (!owin) return;
  set(owin, MUIA_Window_Open, FALSE);
  DoMethod(wp_app(), OM_REMMEMBER, (ULONG)owin);
  MUI_DisposeObject(owin);
  owin = opanes = olist = NULL;
  for (i = 0; i < nrows; i++) rows[i].gad = NULL;
  built_rows = nrows;          /* (own rows stay added) */
}
