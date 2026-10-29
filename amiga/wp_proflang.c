/*
 * wp_proflang.c - WP31 Proofing languages (docs/wordcraft-gap.md). This file belongs to WP31: only that
 * package edits it. Its commands are ID_WP31_FIRST .. ID_WP31_LAST (ids.h); how the hooks below are
 * called: wphooks.h.
 *
 * Review > Language > Set Proofing Language: the Language window (Word's), which marks the selection
 * (or what is typed next) as being in a language, or as not to be checked, and sets the language of
 * text without one (Set As Default, kept in PROGDIR:Quill.lang). Dictionaries: Quill's British
 * English list, and any Hunspell pair in the Dict drawer, named by the language (Dict/de_DE.aff and
 * Dict/de_DE.dic, or de.aff and de.dic), loaded when text in that language is first checked; the
 * user's words (Quill.words) go into each. Hyphenation patterns the same way (Dict/hyph_de_DE.dic).
 * The status bar shows the caret's language (panes.c) and a click on it opens the window.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/iffparse.h>
#include "wphooks.h"
#include "lang.h"
#include "wp_proof.h"
#include "../core/spell.h"
#include "../core/proflang.h"
#include "../core/hyph.h"
#include "../core/docx.h"

enum { ID_PL_LANGWIN = ID_WP31_FIRST, ID_PL_LANGDD, ID_PL_OK, ID_PL_NOCHECK, ID_PL_DEFAULT };
#define WP31S_NOCHECK 1                   /* the state bit: the caret's text isn't checked */
#define LANGFILE "PROGDIR:Quill.lang"

/* is there a file? */
static int exists(const char *path)
{
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  if (l) UnLock(l);
  return l != 0;
}
/* a language's Hunspell files in the Dict drawer: xx_YY or xx; 1 with their paths */
static int dict_files(u16 lang, char *aff, char *dic)
{
  const char *t = plang_tag(lang);
  if (strlen(t) == 5) {
    sprintf(aff, "PROGDIR:Dict/%c%c_%c%c.aff", t[0], t[1], t[3], t[4]); sprintf(dic, "PROGDIR:Dict/%c%c_%c%c.dic", t[0], t[1], t[3], t[4]);
    if (exists(aff) && exists(dic)) return 1;
  }
  if (strlen(t) >= 2) {
    sprintf(aff, "PROGDIR:Dict/%c%c.aff", t[0], t[1]); sprintf(dic, "PROGDIR:Dict/%c%c.dic", t[0], t[1]);
    if (exists(aff) && exists(dic)) return 1;
  }
  return 0;
}
/* the spelling loader (proflang.h): a Hunspell pair, with the user's own words */
static int load_dict(u16 lang, Spell *s)
{
  char aff[64], dic[64], line[128];
  FILE *f;
  if (!dict_files(lang, aff, dic) || !spell_load_hunspell_files(s, aff, dic)) return 0;
  if ((f = fopen("PROGDIR:Quill.words", "r"))) {
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) spell_add(s, line, (u32)strlen(line)); }
    fclose(f);
  }
  return 1;
}
/* the hyphenation loader (hyph.h): Dict/hyph_xx_YY.dic or hyph_xx.dic (English other than the
   built-in British set only with a file of its own) */
static char *load_hyph(u16 lang, size_t *n)
{
  const char *t = plang_tag(lang);
  char path[64];
  u8 *d = NULL;
  if (strlen(t) == 5) { sprintf(path, "PROGDIR:Dict/hyph_%c%c_%c%c.dic", t[0], t[1], t[3], t[4]); d = file_read(path, n); }
  if (!d && strlen(t) >= 2 && !plang_primary(lang, "en")) { sprintf(path, "PROGDIR:Dict/hyph_%c%c.dic", t[0], t[1]); d = file_read(path, n); }
  if (!d && plang_primary(lang, "en")) return NULL;
  return (char *)d;
}
static void load_default(void)
{
  FILE *f = fopen(LANGFILE, "r");
  char line[48];
  u16 k;
  if (!f) return;
  if (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n ")] = 0; if ((k = plang_find(line))) plang_default = k; }
  fclose(f);
}

/* ---- the status bar */
const char *wp31_status_lang(void)
{
  static char out[64];
  Doc *d = wp_doc();
  Pos *c = wp_caret();
  const CharFmt *f;
  if (!d || !c || c->para >= d->np) return tr(plang_name(plang_default));
  f = para_fmt_at(d->p[c->para], c->off > 0 ? c->off - 1 : 0);
  strncpy(out, tr(plang_name(plang_of(f))), sizeof out - 1); out[sizeof out - 1] = 0;
  return out;
}

/* ---- the Language window */
static Object *lwin, *l_list, *l_nocheck, *l_info;
static const char **l_names;              /* the list's entries (translated, a mark for a dictionary) */
static int l_n;
static char *l_store;
static u16 caret_lang(int *noproof)
{
  Doc *d = wp_doc();
  Pos *c = wp_caret();
  const CharFmt *f;
  *noproof = 0;
  if (!d || !c || c->para >= d->np) return plang_default;
  f = para_fmt_at(d->p[c->para], c->off > 0 ? c->off - 1 : 0);
  *noproof = f->noproof;
  return plang_of(f);
}
/* has the language a dictionary? (English: the built-in list) */
static int has_dict(u16 id)
{
  char a[64], b[64];
  return plang_primary(id, "en") ? 1 : dict_files(id, a, b);
}
static void open_lang(void)
{
  int i, np;
  u16 cur = caret_lang(&np);
  if (!lwin) {
    Object *def, *c;
    l_n = plang_nlisted();
    l_names = (const char **)calloc((size_t)l_n + 1, sizeof(char *));
    l_store = (char *)malloc((size_t)l_n * 64);
    if (!l_names || !l_store) return;
    c = VGroup, QDLG,
      Child, QLabel(tr("Mark selected text as:"), MUIO_Label_LeftAligned),
      Child, l_list = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE,
        MUIA_Listview_List, ListObject, InputListFrame, End, End,
      Child, l_info = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, HGroup, Child, l_nocheck = CheckMark(FALSE), Child, QLabel(tr("Do not check spelling or grammar"), MUIO_Label_SingleFrame), Child, HSpace(0), End,
      Child, HGroup, Child, def = QButton(tr("Set As _Default")), Child, HSpace(0), End,
    End;
    if (!c || !(lwin = wp_dialog("Language", MAKE_ID('W','3','1','L'), c, "_OK", ID_PL_OK))) return;
    DoMethod(def, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PL_DEFAULT);
    DoMethod(l_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PL_OK);
  }
  /* the entries afresh: a dictionary installed since is marked too */
  set(l_list, MUIA_List_Quiet, TRUE);
  DoMethod(l_list, MUIM_List_Clear);
  for (i = 0; i < l_n; i++) {
    char *e = l_store + i * 64;
    sprintf(e, "%s%s", has_dict((u16)(i + 1)) ? "\267 " : "   ", tr(plang_name((u16)(i + 1))));
    l_names[i] = e;
    DoMethod(l_list, MUIM_List_InsertSingle, (ULONG)e, MUIV_List_Insert_Bottom);
  }
  set(l_list, MUIA_List_Quiet, FALSE);
  if (cur >= 1 && cur <= l_n) set(l_list, MUIA_List_Active, cur - 1);
  {
    static char info[200];
    sprintf(info, tr("\267 a dictionary is installed. Default: %s"), tr(plang_name(plang_default)));
    set(l_info, MUIA_Text_Contents, (ULONG)info);
  }
  set(l_nocheck, MUIA_Selected, np);
  set(lwin, MUIA_Window_Open, TRUE);
}
/* the language (and "do not check") on the selection, or on what is typed next; laid out afresh
   (hyphenation may differ) */
static void set_lang(u16 lang, int noproof)
{
  struct QCharFmt q;
  memset(&q, 0, sizeof q);
  q.mask = CM_LANG | CM_NOPROOF;
  q.cf.lang = lang; q.cf.noproof = (u8)noproof;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARFMT, (ULONG)&q);
  DoMethod(wp_view(), MUIM_Quill_Relayout);
}
static void lang_ok(void)
{
  LONG a = -1;
  ULONG nc = 0;
  get(l_list, MUIA_List_Active, &a);
  get(l_nocheck, MUIA_Selected, &nc);
  set(lwin, MUIA_Window_Open, FALSE);
  if (a < 0 || a >= l_n) return;
  set_lang((u16)(a + 1), nc != 0);
  set(wp_win(), MUIA_Window_Activate, TRUE);
}
static void lang_default(void)
{
  LONG a = -1;
  FILE *f;
  char msg[160];
  get(l_list, MUIA_List_Active, &a);
  if (a < 0 || a >= l_n) return;
  sprintf(msg, "%s", tr(plang_name((u16)(a + 1))));
  if (wp_ask("_Yes|_No", "Make %s the language of text that has none?\nIt is checked in that language from now on.", msg) != 1) return;
  plang_default = (u16)(a + 1);
  if ((f = fopen(LANGFILE, "w"))) { fprintf(f, "%s\n", plang_tag(plang_default)); fclose(f); }
  open_lang();
  DoMethod(wp_view(), MUIM_Quill_Relayout);
}

/* ---- commands, state */
int wp31_command(ULONG id, LONG value)
{
  int np;
  switch (id) {
  case ID_PL_LANGWIN: open_lang(); return WPR_KEEPFOCUS;
  case ID_PL_LANGDD:
    if (value == 1) { u16 l = caret_lang(&np); set_lang(l, !np); break; }
    open_lang(); return WPR_KEEPFOCUS;
  case ID_PL_NOCHECK: { u16 l = caret_lang(&np); set_lang(l, !np); break; }
  case ID_PL_OK: lang_ok(); break;
  case ID_PL_DEFAULT: lang_default(); return WPR_KEEPFOCUS;
  }
  return WPR_DONE;
}
static void wp31_state(ULONG *on, const WpCtx *c)
{
  if (c->doc && c->caret.para < c->doc->np && para_fmt_at(c->doc->p[c->caret.para], c->caret.off > 0 ? c->caret.off - 1 : 0)->noproof)
    *on |= 1UL << WP31S_NOCHECK;
}
static void wp31_init(void)
{
  load_default();
  if (wp_spell()) plang_set_spell(plang_find("en-GB"), (Spell *)wp_spell());
  plang_set_loader(load_dict);
  hyph_set_loader(load_hyph);
}
static void wp31_cleanup(void)
{
  plang_forget();
  plang_set_loader(NULL);
  free(l_names); free(l_store); l_names = NULL; l_store = NULL;
}

static const char *const lang_entries[] = { "Set Proofing Language...", "Do Not Check Spelling or Grammar" };
static const RibbonList lang_list = { lang_entries, 2, PL_TEXT, NULL };
static const Item lang_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Language", ID_PL_LANGDD, RB_WP(WPI_31, WP31S_NOCHECK), 0, F_SPLIT, 0,
    "Language: the language the selected text is checked in, or not checked at all", &lang_list,
    "ci O 8 8 6 L 2 8 14 8 L 8 2 8 14 ca P 5 8 2 5 4 4 8 5 12 8 14 cg L 10 12 12 14 L 12 14 15 9", ID_PL_LANGWIN },
};
static const WpGroup groups31[] = { { TAB_REVIEW, 1, { "Language", G_COLS, lang_items, 1 } } };
static const WpMenu menus31[] = {
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Language", 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Set Proofing Language...", 0, 0, 0, (APTR)ID_PL_LANGWIN } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Do Not Check Spelling or Grammar", 0, 0, 0, (APTR)ID_PL_NOCHECK } },
};
const WpPackage wp31_package = {
  "WP31", ID_WP31_FIRST, ID_WP31_LAST,
  wp31_command, wp31_state, NULL, NULL, NULL, wp31_init, wp31_cleanup,
  groups31, 1,
  menus31, (int)(sizeof menus31 / sizeof menus31[0])
};
