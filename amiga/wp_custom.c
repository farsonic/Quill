/*
 * wp_custom.c - WP26 Customization and discoverability (docs/wordcraft-gap.md). This file belongs to WP26: only that
 * package edits it. Its commands are ID_WP26_FIRST .. ID_WP26_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * What it does (with the WP26 section of ribbon.c and wp_options.c, the Options window):
 *  - keeps what the user customized in Quill.prefs: tabs and groups hidden ("ribbon.hide"), a tab of
 *    their own ("ribbon.tab", "ribbon.group1".."ribbon.group6"), the Quick Access Toolbar ("qat"), keys
 *    ("key.Ctrl+Shift+D=<command>"), and builds the three customizing panes of the Options window;
 *  - knows every command by name (the ribbon's tools and the menus' items, the packages' too), for
 *    Tell Me (the box on the tab strip, or the Help key: finds commands by their name in the language
 *    shown and runs them) and for the customizing lists;
 *  - KeyTips: F10 shows letters on the tabs, then on the tab's tools (as Word does with Alt or F10;
 *    Alt types characters on the Amiga);
 *  - the mini toolbar over a selection made with the mouse (and after a right-click dismissed);
 *  - Word's keys of section 2.7 that the Amiga allows and no one else has taken (the defaults below),
 *    which the user can change in Customize Keyboard.
 * Command names are kept with their numbers in Quill.prefs ("12|Bold"), so a later Quill that numbers
 * its commands differently still finds them by their English name.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_custom.h"
#include "wp_charpara.h"
#include "wp_info.h"
#include "wp_proof.h"                  /* (WP31_LANGWIN_ID) */
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

enum {
  ID_CU_NOP = ID_WP26_FIRST,      /* (a key KeyTips took) */
  ID_CU_QAADD, ID_CU_QAREMOVE,    /* the ribbon's right-button menu (ribbon.c uses FIRST + 1, + 2) */
  ID_CU_OPTIONS, ID_CU_RIBBON, ID_CU_QAT, ID_CU_KEYS,     /* the Options window, on a pane */
  ID_CU_TELLME, ID_CU_TELLTYPE, ID_CU_TELLRUN, ID_CU_KEYTIPS,
  ID_CU_H1, ID_CU_H2, ID_CU_H3, ID_CU_NORMAL, ID_CU_CASE, ID_CU_GROW1, ID_CU_SHRINK1,
  ID_CU_DBLUL, ID_CU_WORDUL, ID_CU_FONTBOX, ID_CU_SIZEBOX, ID_CU_STYLEBOX,
  ID_CU_COPYR, ID_CU_REGD, ID_CU_TRADE, ID_CU_ELLIPSIS, ID_CU_EMDASH, ID_CU_ENDASH,
  ID_CU_MINIRUN,                  /* a mini toolbar button */
  ID_CU_COLLAPSE,                 /* the ribbon collapsed to its tabs, or shown again (Ctrl-F1) */
  /* the customizing panes' gadgets (wp26_pane_command) */
  ID_CU_PANE_FIRST,
  ID_CU_R_TREE = ID_CU_PANE_FIRST, ID_CU_R_SHOW, ID_CU_R_HIDE, ID_CU_R_CMDS, ID_CU_R_FILTER, ID_CU_R_ADD, ID_CU_R_REMOVE, ID_CU_R_UP,
  ID_CU_R_DOWN, ID_CU_R_NEWGROUP, ID_CU_R_RESET,
  ID_CU_Q_CMDS, ID_CU_Q_FILTER, ID_CU_Q_ADD, ID_CU_Q_REMOVE, ID_CU_Q_UP, ID_CU_Q_DOWN, ID_CU_Q_RESET,
  ID_CU_K_CMDS, ID_CU_K_FILTER, ID_CU_K_ASSIGN, ID_CU_K_REMOVE, ID_CU_K_CAPTURE, ID_CU_K_RESET,
  ID_CU_PANE_LAST = ID_CU_K_RESET
};

/* ------------------------------------------------------------------ */
/* the commands: every tool of the ribbon and item of the menus, once */
#define MAXCMD 900
typedef struct { ULONG id; const char *name, *tab, *group; const Item *it; } Cmd;
static Cmd cmds[MAXCMD];
static int ncmd, cmds_made;
static char cmdname[MAXCMD][48];
/* the menus' items as main.c had them in English (wp_menu_english) */
#define MAXMENU 700
static struct { ULONG id; const char *label, *title; } menuitems[MAXMENU];
static int nmenu;
void wp_menu_english(const struct NewMenu *nm)
{
  const char *title = NULL;
  int i;
  nmenu = 0;
  for (i = 0; nm[i].nm_Type != NM_END && nmenu < MAXMENU; i++) {
    if (nm[i].nm_Type == NM_TITLE) { title = (const char *)nm[i].nm_Label; continue; }
    if (!nm[i].nm_Label || nm[i].nm_Label == NM_BARLABEL || !nm[i].nm_UserData) continue;
    menuitems[nmenu].id = (ULONG)nm[i].nm_UserData; menuitems[nmenu].label = (const char *)nm[i].nm_Label; menuitems[nmenu].title = title;
    nmenu++;
  }
}
static void make_cmds(void)
{
  static RibbonCmd rc[MAXCMD];
  int n, i, k;
  if (cmds_made) return;
  cmds_made = 1;
  n = ribbon_commands(rc, MAXCMD);
  for (i = 0; i < n && ncmd < MAXCMD; i++) {
    ribbon_item_name(rc[i].it, cmdname[ncmd], sizeof cmdname[ncmd]);
    if (!cmdname[ncmd][0]) continue;
    cmds[ncmd].id = rc[i].id; cmds[ncmd].name = cmdname[ncmd]; cmds[ncmd].tab = rc[i].tab; cmds[ncmd].group = rc[i].group; cmds[ncmd].it = rc[i].it;
    ncmd++;
  }
  for (i = 0; i < nmenu && ncmd < MAXCMD; i++) {
    for (k = 0; k < ncmd && cmds[k].id != menuitems[i].id; k++) ;
    if (k < ncmd) continue;
    if (menuitems[i].id >= ID_WINACT) { if (menuitems[i].id < ID_WP_FIRST || menuitems[i].id > ID_WP_LAST) continue; }
    cmds[ncmd].id = menuitems[i].id; cmds[ncmd].name = menuitems[i].label; cmds[ncmd].tab = menuitems[i].title; cmds[ncmd].group = NULL; cmds[ncmd].it = NULL;
    ncmd++;
  }
}
static int cmd_index(ULONG id)
{
  int i;
  make_cmds();
  for (i = 0; i < ncmd; i++) if (cmds[i].id == id) return i;
  return -1;
}
const char *wp26_cmd_name(ULONG id)
{
  int i = cmd_index(id);
  return i >= 0 ? cmds[i].name : "";
}
/* a command from prefs: by its English name if one has it (numbers change between versions), else its number */
static ULONG cmd_resolve(const CmdRef *r)
{
  int i;
  make_cmds();
  if (r->name[0]) for (i = 0; i < ncmd; i++) if (!strcmp(cmds[i].name, r->name)) return cmds[i].id;
  return cmd_index(r->id) >= 0 ? r->id : 0;
}
static void cmd_ref(ULONG id, CmdRef *r)
{
  r->id = id;
  strncpy(r->name, wp26_cmd_name(id), sizeof r->name - 1); r->name[sizeof r->name - 1] = 0;
}
/* a command as a list line: its name in the language shown, then where it is */
static void cmd_line(int i, char *out, size_t cap)
{
  char nm[64];
  strncpy(nm, tr(cmds[i].name), sizeof nm - 1); nm[sizeof nm - 1] = 0;
  if (cmds[i].tab && cmds[i].group) snprintf(out, cap, "%s  (%s \273 %s)", nm, tr(cmds[i].tab), tr(cmds[i].group));
  else if (cmds[i].tab) snprintf(out, cap, "%s  (%s)", nm, tr_ctx("menu", cmds[i].tab));
  else snprintf(out, cap, "%s", nm);
}

/* ------------------------------------------------------------------ */
/* what the user customized (from Quill.prefs, read on first use; core/custom.c reads and writes the
   lines). Each command kept is resolved to this Quill's number once read; those gone are left out */
static RibbonCustom rc;
static int custom_loaded;
static Group cgroups[RC_MAXGROUP]; static Item citems[RC_MAXGROUP][RC_MAXCMD];
static LONG opt_tellme = 1, opt_minibar = 1;
static const ULONG qat_default[] = { ID_INFO_AUTOSAVE, ID_SAVE, ID_UNDO, ID_REDO };
static void qat_defaults(RibbonCustom *c)
{
  int i;
  c->nqat = 0;
  for (i = 0; i < (int)(sizeof qat_default / sizeof qat_default[0]); i++) cmd_ref(qat_default[i], &c->qat[c->nqat++]);
}
static int resolve_list(CmdRef *r, int n)
{
  int i, k = 0;
  for (i = 0; i < n; i++) { ULONG id = cmd_resolve(&r[i]); if (id) cmd_ref(id, &r[k++]); }
  return k;
}
static void load_custom(void)
{
  int g;
  if (custom_loaded) return;
  custom_loaded = 1;
  rcustom_read(&rc, wp_prefs_store());
  for (g = 0; g < rc.ngroup; g++) rc.group[g].n = resolve_list(rc.group[g].cmd, rc.group[g].n);
  if (rc.qatset) rc.nqat = resolve_list(rc.qat, rc.nqat); else qat_defaults(&rc);
}
static void save_custom(void)
{
  if (rcustom_write(&rc, wp_prefs_store())) wp_save_prefs();
}
int wp26_tab_hidden(const char *tab) { load_custom(); return rcustom_hidden(&rc, tab, NULL); }
int wp26_group_hidden(const char *tab, const char *group) { load_custom(); return rcustom_hidden(&rc, tab, group); }
int wp26_custom_tab(const char **name, const Group **groups)
{
  int g, i, n = 0;
  load_custom();
  for (g = 0; g < rc.ngroup; g++) {
    int k = 0;
    for (i = 0; i < rc.group[g].n; i++) {
      ULONG id = rc.group[g].cmd[i].id;
      const Item *it = ribbon_item_of(id);
      Item *c = &citems[n][k];
      if (it) *c = *it;
      else { memset(c, 0, sizeof *c); c->kind = K_BUTTON; c->id = (UWORD)id; }
      c->size = S_SMALL; c->flags &= (u8)~(F_ROW1 | F_NEWCOL);
      if (c->kind == K_GALLERY) c->kind = K_DROP;
      if (c->kind == K_LABEL) continue;
      if (!c->label) c->label = wp26_cmd_name(id);
      if (c->kind == K_FIELD && !c->fieldw) c->fieldw = 60;
      k++;
    }
    if (!k) continue;
    cgroups[n].name = rc.group[g].name; cgroups[n].mode = G_COLS; cgroups[n].items = citems[n]; cgroups[n].n = k;
    n++;
  }
  *name = rc.tabname[0] ? rc.tabname : "My Tab";
  *groups = cgroups;
  return n;
}
int wp26_qat(ULONG *ids, int max)
{
  int i;
  load_custom();
  for (i = 0; i < rc.nqat && i < max; i++) ids[i] = rc.qat[i].id;
  return i;
}
int wp26_tellme_box(void) { return opt_tellme != 0; }

/* ------------------------------------------------------------------ */
/* Tell Me: a small window under the box, a field and the commands matching what is typed */
static Object *tmwin, *tmstr, *tmlist;
static int tmidx[12], ntm;
static ULONG tmrecent[6]; static int ntmrecent;
static void tell_fill(void)
{
  static char lines[12][120];
  char *q = NULL, ql[80];
  int i, k;
  ntm = 0;
  get(tmstr, MUIA_String_Contents, &q);
  strncpy(ql, q ? q : "", sizeof ql - 1); ql[sizeof ql - 1] = 0;
  make_cmds();
  set(tmlist, MUIA_List_Quiet, TRUE);
  DoMethod(tmlist, MUIM_List_Clear);
  if (!ql[0]) {
    /* nothing typed: the commands used from here lately */
    for (i = 0; i < ntmrecent && ntm < 12; i++) { k = cmd_index(tmrecent[i]); if (k >= 0) tmidx[ntm++] = k; }
  } else {
    static const char *shown[MAXCMD], *eng[MAXCMD];
    for (i = 0; i < ncmd; i++) { shown[i] = tr(cmds[i].name); eng[i] = cmds[i].name; }
    ntm = cmd_search(shown, eng, ncmd, ql, tmidx, 12);
  }
  for (i = 0; i < ntm; i++) { cmd_line(tmidx[i], lines[i], sizeof lines[i]); DoMethod(tmlist, MUIM_List_InsertSingle, (ULONG)lines[i], MUIV_List_Insert_Bottom); }
  if (ntm) set(tmlist, MUIA_List_Active, 0);
  set(tmlist, MUIA_List_Quiet, FALSE);
}
void wp26_tellme_open(LONG x, LONG y)
{
  if (!tmwin) {
    Object *close;
    tmwin = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
      MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE, MUIA_Window_Activate, TRUE,
      WindowContents, VGroup, QDLG, GroupFrame,
        Child, HGroup,
          Child, Label1(tr("Tell me what you want to do")),
          Child, HSpace(0),
          Child, close = QButton("x"),
        End,
        Child, tmstr = StringObject, StringFrame, MUIA_String_MaxLen, 70, MUIA_CycleChain, 1, End,
        Child, tmlist = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, ListObject, InputListFrame,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String,
          MUIA_List_MinLineHeight, 0, End,
          MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
        End,
      End, End;
    if (!tmwin) return;
    set(tmstr, MUIA_String_AttachedList, (ULONG)tmlist);
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)tmwin);
    DoMethod(tmwin, MUIM_Notify, MUIA_Window_Activate, FALSE, (ULONG)tmwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tmwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(tmstr, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CU_TELLTYPE);
    DoMethod(tmstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CU_TELLRUN);
    DoMethod(tmlist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_CU_TELLRUN);
  }
  set(tmwin, MUIA_Window_Open, FALSE);
  if (x < 0 || y < 0) {
    struct Window *w = NULL;
    get(wp_win(), MUIA_Window_Window, &w);
    x = w ? w->LeftEdge + w->Width - 360 : 100; y = w ? w->TopEdge + 30 : 40;
  }
  if (x < 0) x = 0;
  SetAttrs(tmwin, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, TAG_DONE);
  nnset(tmstr, MUIA_String_Contents, (ULONG)"");
  tell_fill();
  set(tmwin, MUIA_Window_Open, TRUE);
  set(tmwin, MUIA_Window_ActiveObject, (ULONG)tmstr);
}
/* the command chosen: Tell Me shuts and the command runs (a tool with a list: its list opens on the ribbon) */
static void run_cmd(ULONG id)
{
  if (ribbon_is_list(id) && wp_ribbon()) DoMethod(wp_ribbon(), MUIM_Ribbon_OpenId, id);
  else DoMethod(wp_app(), MUIM_Application_ReturnID, id);
}
static void tell_run(void)
{
  LONG a = -1;
  ULONG id;
  int i;
  get(tmlist, MUIA_List_Active, &a);
  if (a < 0 && ntm) a = 0;
  if (a < 0 || a >= ntm) return;
  id = cmds[tmidx[a]].id;
  set(tmwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  for (i = 0; i < ntmrecent && tmrecent[i] != id; i++) ;
  if (i == ntmrecent && ntmrecent < 6) ntmrecent++;
  if (i >= ntmrecent) i = ntmrecent - 1;
  for (; i > 0; i--) tmrecent[i] = tmrecent[i - 1];
  tmrecent[0] = id;
  run_cmd(id);
}

/* ------------------------------------------------------------------ */
/* keys: the defaults (Word's, section 2.7, those the Amiga allows and no package took) and the user's */
static const KeyBind defkeys[] = {
  { 0x22, KM_CTRL, ID_FONTDLG },                    /* Ctrl+D Font */
  { 0x20, KM_CTRL | KM_SHIFT, ID_CAPS },            /* Ctrl+Shift+A All Caps */
  { 0x32, KM_CTRL | KM_SHIFT, ID_STRIKE },          /* Ctrl+Shift+X Strikethrough (WordCraft) */
  { 0x22, KM_CTRL | KM_SHIFT, ID_CU_DBLUL },        /* Ctrl+Shift+D Double Underline */
  { 0x11, KM_CTRL | KM_SHIFT, ID_CU_WORDUL },       /* Ctrl+Shift+W Word Underline */
  { 0x25, KM_CTRL | KM_SHIFT, ID_C_HIDDEN },        /* Ctrl+Shift+H Hidden */
  { 0x35, KM_CTRL, ID_BOLD }, { 0x17, KM_CTRL, ID_ITALIC }, { 0x16, KM_CTRL, ID_UNDERLINE },
  { 0x0C, KM_CTRL, ID_SUB }, { 0x0C, KM_CTRL | KM_SHIFT, ID_SUPER },          /* Ctrl+= and Ctrl+Shift+= */
  { 0x39, KM_CTRL | KM_SHIFT, ID_GROW }, { 0x38, KM_CTRL | KM_SHIFT, ID_SHRINK },   /* Ctrl+Shift+. and , */
  { 0x1B, KM_CTRL, ID_CU_GROW1 }, { 0x1A, KM_CTRL, ID_CU_SHRINK1 },          /* Ctrl+] and Ctrl+[ */
  { 0x52, KM_SHIFT, ID_CU_CASE },                   /* Shift+F3 Change Case */
  { 0x23, KM_CTRL | KM_SHIFT, ID_CU_FONTBOX }, { 0x19, KM_CTRL | KM_SHIFT, ID_CU_SIZEBOX },   /* Ctrl+Shift+F, P */
  { 0x21, KM_CTRL | KM_SHIFT, ID_CU_STYLEBOX },     /* Ctrl+Shift+S Apply Styles */
  { 0x21, KM_CTRL | KM_ALT | KM_SHIFT, ID_STYLESPANE },   /* Ctrl+Alt+Shift+S Styles */
  { 0x01, KM_CTRL | KM_ALT, ID_CU_H1 }, { 0x02, KM_CTRL | KM_ALT, ID_CU_H2 }, { 0x03, KM_CTRL | KM_ALT, ID_CU_H3 },
  { 0x36, KM_CTRL | KM_SHIFT, ID_CU_NORMAL },       /* Ctrl+Shift+N Normal */
  { 0x28, KM_CTRL | KM_SHIFT, ID_BULLETS },         /* Ctrl+Shift+L Bullets */
  { 0x28, KM_CTRL, ID_LEFT }, { 0x12, KM_CTRL, ID_CENTER }, { 0x13, KM_CTRL, ID_RIGHT }, { 0x26, KM_CTRL, ID_JUSTIFY },
  { 0x37, KM_CTRL, ID_INDENT }, { 0x37, KM_CTRL | KM_SHIFT, ID_OUTDENT },
  { 0x01, KM_CTRL, ID_SINGLE }, { 0x02, KM_CTRL, ID_DOUBLE }, { 0x05, KM_CTRL, ID_ONEHALF },
  { 0x44, KM_CTRL, ID_PAGEBREAK }, { 0x43, KM_CTRL, ID_PAGEBREAK },          /* Ctrl+Return */
  { 0x44, KM_CTRL | KM_SHIFT, ID_COLBREAK }, { 0x43, KM_CTRL | KM_SHIFT, ID_COLBREAK },
  { 0x27, KM_CTRL, ID_LINK },                       /* Ctrl+K Link */
  { 0x54, KM_CTRL | KM_SHIFT, ID_BOOKMARK },        /* Ctrl+Shift+F5 Bookmark */
  { 0x23, KM_CTRL | KM_ALT, ID_NOTE }, { 0x22, KM_CTRL | KM_ALT, ID_ENDNOTE },   /* Ctrl+Alt+F, D */
  { 0x12, KM_CTRL | KM_SHIFT, ID_TRACK },           /* Ctrl+Shift+E Track Changes */
  { 0x37, KM_CTRL | KM_ALT, ID_COMMENT },           /* Ctrl+Alt+M New Comment */
  { 0x50, KM_CTRL, ID_CU_COLLAPSE },                /* Ctrl+F1 Collapse the Ribbon */
  { 0x24, KM_CTRL | KM_SHIFT, ID_WORDCOUNT },       /* Ctrl+Shift+G Word Count */
  { 0x34, KM_CTRL | KM_ALT, ID_P_PASTESPECIAL },    /* Ctrl+Alt+V Paste Special */
  { 0x34, KM_CTRL | KM_ALT | KM_SHIFT, ID_PASTETEXT },   /* Ctrl+Shift+Alt+V Paste Text Only */
  { 0x25, KM_CTRL, ID_REPLACE },                    /* Ctrl+H Replace */
  { 0x33, KM_CTRL | KM_ALT, ID_CU_COPYR }, { 0x13, KM_CTRL | KM_ALT, ID_CU_REGD }, { 0x14, KM_CTRL | KM_ALT, ID_CU_TRADE },
  { 0x39, KM_CTRL | KM_ALT, ID_CU_ELLIPSIS }, { 0x0B, KM_CTRL | KM_ALT, ID_CU_EMDASH }, { 0x4A, KM_CTRL | KM_ALT, ID_CU_EMDASH },
  { 0x4A, KM_CTRL, ID_CU_ENDASH },                  /* Ctrl+Num- en dash */
  { 0x36, KM_CTRL | KM_ALT, ID_VDRAFT }, { 0x19, KM_CTRL | KM_ALT, ID_VPRINT },   /* Ctrl+Alt+N, P views */
  { 0x18, KM_CTRL | KM_ALT, ID_WP11_FIRST },        /* Ctrl+Alt+O Outline (WP11's ID_OL_VIEW) */
  { 0x21, KM_CTRL | KM_ALT, ID_WP25_FIRST + 2 },    /* Ctrl+Alt+S Split (WP25's ID_WW_SPLIT) */
};
#define NDEFKEYS ((int)(sizeof defkeys / sizeof defkeys[0]))
/* the user's map (core/custom.c's KeyMap): the defaults with the user's changes, from Quill.prefs */
#define MAXG_NAMES 40
static KeyMap km; static int keys_loaded;
static u32 resolve_cb(const CmdRef *r, void *u) { return cmd_resolve(r); }
static void name_cb(u32 id, CmdRef *r, void *u) { cmd_ref(id, r); }
static void load_keys(void)
{
  if (keys_loaded) return;
  keys_loaded = 1;
  keymap_init(&km, defkeys, NDEFKEYS);
  keymap_load(&km, wp_prefs_store(), resolve_cb, NULL);
}
static void save_keys(void)
{
  keymap_save(&km, wp_prefs_store(), name_cb, NULL);
  wp_save_prefs();
}
static u8 mods_of(UWORD qual)
{
  return (u8)(((qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? KM_SHIFT : 0) | ((qual & IEQUALIFIER_CONTROL) ? KM_CTRL : 0) |
              ((qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? KM_ALT : 0));
}
/* the command of a key: user_only, only where the user changed it (those come before every package's keys) */
static ULONG key_lookup(u16 code, u8 mods, int user_only)
{
  load_keys();
  if (user_only && !keymap_changed(&km, code, mods)) return 0;
  if (user_only && !keymap_get(&km, code, mods)) return ID_CU_NOP;     /* (a default the user took off) */
  return keymap_get(&km, code, mods);
}
/* ------------------------------------------------------------------ */
/* the mini toolbar: a row of buttons over the selection, in a borderless window that doesn't take
   the keyboard; it goes when the pointer moves away, a key is pressed or a button used */
typedef struct { int icon; ULONG id; LONG value; const char *help; } MiniBtn;
static const MiniBtn minis[] = {
  { IC_GROW, ID_GROW, -1, "Grow font" }, { IC_SHRINK, ID_SHRINK, -1, "Shrink font" },
  { IC_BOLD, ID_BOLD, -1, "Bold" }, { IC_ITALIC, ID_ITALIC, -1, "Italic" }, { IC_UNDERLINE, ID_UNDERLINE, -1, "Underline" },
  { IC_HIGHLIGHT, ID_HIGHLIGHT, 0xFFFF00, "Text highlight colour" }, { IC_FONTCOLOR, ID_FONTCOLOR, 0xFF0000, "Font colour" },
  { IC_BRUSH, ID_BRUSH, -1, "Format Painter" }, { IC_BULLETS, ID_BULLETS, -1, "Bullets" }, { IC_NUMBERS, ID_NUMBERS, -1, "Numbering" },
  { IC_STYLESPANE, ID_STYLESPANE, -1, "Styles" }, { IC_COMMENT, ID_COMMENT, -1, "New Comment" }, { IC_FONTDLG, ID_FONTDLG, -1, "Font..." } };
#define NMINI ((int)(sizeof minis / sizeof minis[0]))
#define MINIW 22
static struct MUI_CustomClass *mcls;
static Object *mwin, *mobj;
static int mini_open, mini_pending, mini_hot = -1, mini_run = -1;
static LONG mini_px, mini_py;
struct MData { struct MUI_EventHandlerNode ehn; struct MUI_InputHandlerNode ihn; int timer; };
#define MUIM_Mini_Tick (QTAG + 0x3A0)
static void mini_close(void)
{
  if (mwin && mini_open) set(mwin, MUIA_Window_Open, FALSE);
  mini_open = 0; mini_hot = -1;
}
static void mini_timer(Object *obj, int on)
{
  struct MData *m = INST_DATA(mcls->mcc_Class, obj);
  if (on == m->timer) return;
  m->timer = on;
  if (on) DoMethod(wp_app(), MUIM_Application_AddInputHandler, (ULONG)&m->ihn);
  else DoMethod(wp_app(), MUIM_Application_RemInputHandler, (ULONG)&m->ihn);
}
static ULONG mini_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct MData *m;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    m = INST_DATA(cl, obj);
    memset(m, 0, sizeof *m);
    m->ehn.ehn_Object = obj; m->ehn.ehn_Class = cl; m->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    m->ihn.ihn_Object = obj; m->ihn.ihn_Flags = MUIIHNF_TIMER; m->ihn.ihn_Method = MUIM_Mini_Tick; m->ihn.ihn_stuff.ihn_timer.ihn_millis = 200;
    return (ULONG)obj;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *a = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    a->MinMaxInfo->MinWidth += NMINI * MINIW + 4; a->MinMaxInfo->DefWidth += NMINI * MINIW + 4; a->MinMaxInfo->MaxWidth += NMINI * MINIW + 4;
    a->MinMaxInfo->MinHeight += 24; a->MinMaxInfo->DefHeight += 24; a->MinMaxInfo->MaxHeight += 24;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct MData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw: {
    struct RastPort *rp;
    int i;
    DoSuperMethodA(cl, obj, msg);
    rp = _rp(obj);
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_RIBBON));
    theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_BORDER2));
    for (i = 0; i < NMINI; i++) {
      LONG x = _mleft(obj) + 2 + i * MINIW;
      if (i == mini_hot) theme_fill(rp, x, _mtop(obj) + 2, x + MINIW - 2, _mbottom(obj) - 2, tpen(T_HOVER));
      theme_icon(rp, minis[i].icon, x + 3, _mtop(obj) + 4, 16, 0);
    }
    return 0;
  }
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int i = -1;
    if (!im) return 0;
    if (im->MouseX >= _mleft(obj) + 2 && im->MouseX <= _mright(obj) - 2 && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj))
      i = (im->MouseX - _mleft(obj) - 2) / MINIW;
    if (i >= NMINI) i = -1;
    if (im->Class == IDCMP_MOUSEMOVE && i != mini_hot) { mini_hot = i; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP && i >= 0) {
      mini_run = i;
      DoMethod(wp_app(), MUIM_Application_ReturnID, ID_CU_MINIRUN);
    }
    return MUI_EventHandlerRC_Eat;
  }
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *h = (struct MUIP_CreateShortHelp *)msg;
    LONG i = (h->mx - _mleft(obj) - 2) / MINIW;
    return i >= 0 && i < NMINI ? (ULONG)tr(minis[i].help) : 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Mini_Tick: {
    /* the pointer gone far from the toolbar: it goes; a right-click's menu over, the toolbar comes */
    struct Window *dw = NULL;
    struct Screen *sc;
    get(wp_win(), MUIA_Window_Window, &dw);
    if (!dw) { mini_timer(obj, 0); return 0; }
    if (dw->Flags & WFLG_MENUSTATE) return 0;
    sc = dw->WScreen;
    if (mini_pending) {
      LONG dx = sc->MouseX - mini_px, dy = sc->MouseY - mini_py;
      mini_pending = 0;
      if (dx > -24 && dx < 24 && dy > -24 && dy < 24) { wp_minibar(mini_px, mini_py); return 0; }
      if (!mini_open) mini_timer(obj, 0);
      return 0;
    }
    if (mini_open) {
      struct Window *mw = NULL;
      get(mwin, MUIA_Window_Window, &mw);
      if (mw && (sc->MouseX < mw->LeftEdge - 70 || sc->MouseX > mw->LeftEdge + mw->Width + 70 || sc->MouseY < mw->TopEdge - 60 || sc->MouseY > mw->TopEdge + mw->Height + 70))
        { mini_close(); mini_timer(obj, 0); }
    } else mini_timer(obj, 0);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static int mini_make(void)
{
  if (mwin) return 1;
  if (!mcls && !(mcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct MData), (APTR)mini_dispatch))) return 0;
  mobj = NewObject(mcls->mcc_Class, NULL, MUIA_FillArea, FALSE, MUIA_ShortHelp, TRUE, TAG_DONE);
  if (!mobj) return 0;
  mwin = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
    MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE, MUIA_Window_Activate, FALSE, MUIA_Window_NoMenus, TRUE,
    WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, mobj, End, End;
  if (!mwin) { MUI_DisposeObject(mobj); mobj = NULL; return 0; }
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)mwin);
  return 1;
}
void wp_minibar(LONG x, LONG y)
{
  LONG w = NMINI * MINIW + 4 + 8;
  if (!opt_minibar || !mini_make()) return;
  set(mwin, MUIA_Window_Open, FALSE);
  x -= w / 2; y -= 24 + 8 + 18;
  if (x < 0) x = 0;
  if (y < 0) y += 24 + 8 + 36;
  SetAttrs(mwin, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, MUIA_Window_Activate, FALSE, TAG_DONE);
  set(mwin, MUIA_Window_Open, TRUE);
  mini_open = 1; mini_hot = -1;
  mini_timer(mobj, 1);
}
static void mini_do(void)
{
  int i = mini_run;
  mini_run = -1;
  mini_close();
  if (mobj) mini_timer(mobj, 0);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (i < 0 || i >= NMINI) return;
  if (minis[i].value >= 0) { set(wp_ribbon(), MUIA_Ribbon_Value, (ULONG)minis[i].value); DoMethod(wp_app(), MUIM_Application_ReturnID, minis[i].id); }
  else DoMethod(wp_app(), MUIM_Application_ReturnID, minis[i].id);
}
/* the right-button menu: after it goes (nothing chosen: the pointer still by the click) the mini toolbar */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  struct Window *dw = NULL;
  if (!opt_minibar || (c->where != WPC_TEXT && c->where != WPC_SPELLING) || !mini_make()) return;
  get(wp_win(), MUIA_Window_Window, &dw);
  if (!dw) return;
  mini_px = dw->WScreen->MouseX; mini_py = dw->WScreen->MouseY; mini_pending = 1;
  mini_timer(mobj, 1);
}

/* ------------------------------------------------------------------ */
/* KeyTips and the keys: F10 starts KeyTips (the keys then go to the ribbon until they end), Help
   opens Tell Me, the user's own keys come before every package's */
static int kt_on;
ULONG wp26_prekey(UWORD code, UWORD qual, const WpCtx *c)
{
  u8 m = mods_of(qual);
  if (code & IECODE_UP_PREFIX) return 0;
  if (mini_open) { mini_close(); if (mobj) mini_timer(mobj, 0); }
  if (kt_on) {
    Object *rb = wp_ribbon();
    if (!rb || !DoMethod(rb, MUIM_Ribbon_KeyTip, 0)) kt_on = 0;
    else {
      LONG ch = 0;
      if (code >= 0x60 && code <= 0x67) return ID_CU_NOP;               /* a qualifier alone */
      if (code == 0x45) ch = 27;
      else if (code == 0x59) { DoMethod(rb, MUIM_Ribbon_KeyTip, KT_STOP); kt_on = 0; return ID_CU_NOP; }
      else {
        struct InputEvent ie;
        char buf[4];
        memset(&ie, 0, sizeof ie);
        ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = code; ie.ie_Qualifier = qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT);
        if (MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL) == 1) ch = (unsigned char)buf[0];
      }
      if (ch) kt_on = (int)DoMethod(rb, MUIM_Ribbon_KeyTip, ch);
      return ID_CU_NOP;
    }
  }
  if (code == 0x59 && !m) return ID_CU_KEYTIPS;           /* F10 */
  if (code == 0x5F && !m) return ID_CU_TELLME;            /* Help */
  return key_lookup(code, m, 1);
}
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  if (code & IECODE_UP_PREFIX) return 0;
  return key_lookup(code, mods_of(qual), 0);
}

/* ------------------------------------------------------------------ */
/* the customizing panes of the Options window: Customize Ribbon, Quick Access Toolbar, Keyboard.
   Each works on a copy of the choices; OK (wp26_pane_save) keeps it */
static Object *r_tree, *r_cmds, *r_filter, *r_tabname, *r_grpname, *r_cust;
static Object *q_cmds, *q_filter, *q_list;
static Object *k_cmds, *k_filter, *k_cur, *k_new, *k_note;
/* the copies */
static RibbonCustom wrc;
static KeyMap wkm; static int wkm_made;
static u16 cap_code; static u8 cap_mods; static int cap_ok;
/* the rows of the ribbon list: a tab (group -1) or a group of it */
static struct { int tab, group; char line[80]; } rrow[300];
static int nrrow;
static int fil_idx[3][MAXCMD], nfil[3];          /* each pane's command list, filtered */
static void fill_cmds(int which, Object *list, Object *filter)
{
  static char lines[3][MAXCMD][90];
  char *q = NULL;
  int i;
  get(filter, MUIA_String_Contents, &q);
  make_cmds();
  set(list, MUIA_List_Quiet, TRUE);
  DoMethod(list, MUIM_List_Clear);
  nfil[which] = 0;
  for (i = 0; i < ncmd; i++) {
    if (q && q[0] && !cmd_match(tr(cmds[i].name), q) && !cmd_match(cmds[i].name, q)) continue;
    cmd_line(i, lines[which][nfil[which]], sizeof lines[which][0]);
    fil_idx[which][nfil[which]] = i;
    DoMethod(list, MUIM_List_InsertSingle, (ULONG)lines[which][nfil[which]], MUIV_List_Insert_Bottom);
    nfil[which]++;
  }
  set(list, MUIA_List_Quiet, FALSE);
}
static int chosen_cmd(int which, Object *list)
{
  LONG a = -1;
  get(list, MUIA_List_Active, &a);
  return a >= 0 && a < nfil[which] ? fil_idx[which][a] : -1;
}
static void fill_tree(void)
{
  int t, g, n;
  const char *gn[MAXG_NAMES];
  LONG keep = 0;
  get(r_tree, MUIA_List_Active, &keep);
  set(r_tree, MUIA_List_Quiet, TRUE);
  DoMethod(r_tree, MUIM_List_Clear);
  nrrow = 0;
  for (t = 0; t < ribbon_tab_count() && nrrow < 300; t++) {
    const char *tn = ribbon_tab_name(t);
    if (!tn) continue;
    snprintf(rrow[nrrow].line, sizeof rrow[0].line, "[%c] %s%s", rcustom_hidden(&wrc, tn, NULL) ? ' ' : 'x', tr(tn), ribbon_tab_contextual(t) ? "  *" : "");
    rrow[nrrow].tab = t; rrow[nrrow].group = -1; nrrow++;
    n = ribbon_tab_groups(t, gn, MAXG_NAMES);
    for (g = 0; g < n && nrrow < 300; g++) {
      snprintf(rrow[nrrow].line, sizeof rrow[0].line, "      [%c] %s", rcustom_hidden(&wrc, tn, gn[g]) ? ' ' : 'x', tr(gn[g]));
      rrow[nrrow].tab = t; rrow[nrrow].group = g; nrrow++;
    }
  }
  for (t = 0; t < nrrow; t++) DoMethod(r_tree, MUIM_List_InsertSingle, (ULONG)rrow[t].line, MUIV_List_Insert_Bottom);
  set(r_tree, MUIA_List_Quiet, FALSE);
  if (keep >= 0 && keep < nrrow) set(r_tree, MUIA_List_Active, keep);
}
static void fill_cust(void)
{
  static char lines[RC_MAXGROUP * (RC_MAXCMD + 1)][80];
  int g, i, n = 0;
  set(r_cust, MUIA_List_Quiet, TRUE);
  DoMethod(r_cust, MUIM_List_Clear);
  for (g = 0; g < wrc.ngroup; g++) {
    snprintf(lines[n], sizeof lines[0], "%s:", wrc.group[g].name);
    DoMethod(r_cust, MUIM_List_InsertSingle, (ULONG)lines[n], MUIV_List_Insert_Bottom); n++;
    for (i = 0; i < wrc.group[g].n; i++) {
      snprintf(lines[n], sizeof lines[0], "    %s", tr(wp26_cmd_name(wrc.group[g].cmd[i].id)));
      DoMethod(r_cust, MUIM_List_InsertSingle, (ULONG)lines[n], MUIV_List_Insert_Bottom); n++;
    }
  }
  set(r_cust, MUIA_List_Quiet, FALSE);
}
/* the custom list's active row as a group and an entry (-1: the group's own line); 0 if none */
static int cust_pos(int *g, int *i)
{
  LONG a = -1;
  int gg, row = 0;
  get(r_cust, MUIA_List_Active, &a);
  if (a < 0) return 0;
  for (gg = 0; gg < wrc.ngroup; gg++) {
    if (row == a) { *g = gg; *i = -1; return 1; }
    row++;
    if (a < row + wrc.group[gg].n) { *g = gg; *i = (int)(a - row); return 1; }
    row += wrc.group[gg].n;
  }
  return 0;
}
static void fill_qat(void)
{
  static char lines[RC_MAXQAT][64];
  int i;
  set(q_list, MUIA_List_Quiet, TRUE);
  DoMethod(q_list, MUIM_List_Clear);
  for (i = 0; i < wrc.nqat; i++) {
    strncpy(lines[i], tr(wp26_cmd_name(wrc.qat[i].id)), sizeof lines[i] - 1); lines[i][sizeof lines[i] - 1] = 0;
    DoMethod(q_list, MUIM_List_InsertSingle, (ULONG)lines[i], MUIV_List_Insert_Bottom);
  }
  set(q_list, MUIA_List_Quiet, FALSE);
}
static void show_keys(void)
{
  static char t[200], nt[160];
  int i = chosen_cmd(2, k_cmds), j;
  t[0] = 0;
  if (i >= 0) {
    KeyBind kb[8];
    int nk = keymap_keys_of(&wkm, cmds[i].id, kb, 8);
    for (j = 0; j < nk; j++) {
      char k[32];
      key_format(kb[j].code, kb[j].mods, k, sizeof k);
      if (strlen(t) + strlen(k) + 3 < sizeof t) { if (t[0]) strcat(t, ", "); strcat(t, k); }
    }
  }
  set(k_cur, MUIA_Text_Contents, (ULONG)(t[0] ? t : tr("(none)")));
  nt[0] = 0;
  if (cap_ok) {
    char k[32];
    key_format(cap_code, cap_mods, k, sizeof k);
    set(k_new, MUIA_Text_Contents, (ULONG)k);
    if (keymap_get(&wkm, cap_code, cap_mods)) snprintf(nt, sizeof nt, tr("Currently assigned to: %s"), tr(wp26_cmd_name(keymap_get(&wkm, cap_code, cap_mods))));
  } else set(k_new, MUIA_Text_Contents, (ULONG)tr("(click here, then press the keys)"));
  set(k_note, MUIA_Text_Contents, (ULONG)nt);
}
/* the key capture field: a text that, while armed by a click, takes the next key */
static struct MUI_CustomClass *kcls;
struct KData { struct MUI_EventHandlerNode ehn; int armed; };
static ULONG kcap_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct KData *k;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    k = INST_DATA(cl, obj);
    memset(k, 0, sizeof *k);
    k->ehn.ehn_Object = obj; k->ehn.ehn_Class = cl; k->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY; k->ehn.ehn_Priority = 1;
    return (ULONG)obj;
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&((struct KData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&((struct KData *)INST_DATA(cl, obj))->ehn);
    break;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    k = INST_DATA(cl, obj);
    if (!im) return 0;
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
      int in = im->MouseX >= _left(obj) && im->MouseX <= _right(obj) && im->MouseY >= _top(obj) && im->MouseY <= _bottom(obj);
      if (in != k->armed) { k->armed = in; set(obj, MUIA_Text_Contents, (ULONG)(in ? tr("Press the keys now...") : "")); if (!in) show_keys(); }
      return 0;
    }
    if (im->Class == IDCMP_RAWKEY && k->armed && !(im->Code & IECODE_UP_PREFIX) && !(im->Code >= 0x60 && im->Code <= 0x67) && key_name(im->Code)) {
      cap_code = im->Code; cap_mods = mods_of(im->Qualifier); cap_ok = 1; k->armed = 0;
      show_keys();
      return MUI_EventHandlerRC_Eat;
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static Object *cmd_column(Object **list, Object **filter, ULONG listid, ULONG filterid, const char *title)
{
  Object *g = VGroup,
    Child, TextObject, MUIA_Text_Contents, (ULONG)tr(title), MUIA_Text_SetMin, FALSE, End,
    Child, HGroup, Child, Label1(tr("Find:")), Child, *filter = StringObject, StringFrame, MUIA_String_MaxLen, 40, MUIA_CycleChain, 1, End, End,
    Child, ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, *list = ListObject, InputListFrame,
      MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
  End;
  if (!g) return NULL;
  DoMethod(*filter, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, filterid);
  if (listid) DoMethod(*list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, listid);
  return g;
}
#define RET(o, a, v, id) DoMethod(o, MUIM_Notify, a, v, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id)
Object *wp26_pane(int pane)
{
  Object *g = NULL, *b1, *b2, *b3, *b4, *b5, *b6, *b7, *b8, *col;
  if (pane == OP_RIBBON) {
    col = cmd_column(&r_cmds, &r_filter, 0, ID_CU_R_FILTER, "Choose commands:");
    g = VGroup,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Tick the tabs and groups to show. Put the commands you use on a tab of your own."), MUIA_Text_SetMin, FALSE, End,
      Child, HGroup,
        Child, VGroup,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Main tabs (* contextual):"), MUIA_Text_SetMin, FALSE, End,
          Child, ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, r_tree = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, HGroup, Child, b1 = QButton(tr("Show")), Child, b2 = QButton(tr("Hide")), End,
        End,
        Child, VGroup,
          Child, col,
          Child, b3 = QButton(tr("Add >>")), Child, b4 = QButton(tr("<< Remove")),
        End,
        Child, VGroup,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("My tab:"), MUIA_Text_SetMin, FALSE, End,
          Child, r_tabname = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
          Child, ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, r_cust = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, HGroup, Child, r_grpname = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End, Child, b5 = QButton(tr("New Group")), End,
          Child, HGroup, Child, b6 = QButton(tr("Up")), Child, b7 = QButton(tr("Down")), End,
        End,
      End,
      Child, HGroup, Child, HSpace(0), Child, b8 = QButton(tr("Reset All")), End,
    End;
    if (!g) return NULL;
    RET(b1, MUIA_Pressed, FALSE, ID_CU_R_SHOW); RET(b2, MUIA_Pressed, FALSE, ID_CU_R_HIDE);
    RET(r_tree, MUIA_Listview_DoubleClick, TRUE, ID_CU_R_TREE);
    RET(b3, MUIA_Pressed, FALSE, ID_CU_R_ADD); RET(b4, MUIA_Pressed, FALSE, ID_CU_R_REMOVE);
    RET(b5, MUIA_Pressed, FALSE, ID_CU_R_NEWGROUP); RET(b6, MUIA_Pressed, FALSE, ID_CU_R_UP); RET(b7, MUIA_Pressed, FALSE, ID_CU_R_DOWN);
    RET(b8, MUIA_Pressed, FALSE, ID_CU_R_RESET);
    RET(r_cmds, MUIA_Listview_DoubleClick, TRUE, ID_CU_R_ADD);
  } else if (pane == OP_QAT) {
    col = cmd_column(&q_cmds, &q_filter, 0, ID_CU_Q_FILTER, "Choose commands:");
    g = VGroup,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("The buttons at the right end of the tab strip. Right-click a ribbon tool to add it too."), MUIA_Text_SetMin, FALSE, End,
      Child, HGroup,
        Child, col,
        Child, VGroup, Child, VSpace(0), Child, b1 = QButton(tr("Add >>")), Child, b2 = QButton(tr("<< Remove")), Child, VSpace(0), End,
        Child, VGroup,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Quick Access Toolbar:"), MUIA_Text_SetMin, FALSE, End,
          Child, ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, q_list = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, HGroup, Child, b3 = QButton(tr("Up")), Child, b4 = QButton(tr("Down")), Child, b5 = QButton(tr("Reset")), End,
        End,
      End,
    End;
    if (!g) return NULL;
    RET(b1, MUIA_Pressed, FALSE, ID_CU_Q_ADD); RET(b2, MUIA_Pressed, FALSE, ID_CU_Q_REMOVE);
    RET(b3, MUIA_Pressed, FALSE, ID_CU_Q_UP); RET(b4, MUIA_Pressed, FALSE, ID_CU_Q_DOWN); RET(b5, MUIA_Pressed, FALSE, ID_CU_Q_RESET);
    RET(q_cmds, MUIA_Listview_DoubleClick, TRUE, ID_CU_Q_ADD); RET(q_list, MUIA_Listview_DoubleClick, TRUE, ID_CU_Q_REMOVE);
  } else if (pane == OP_KEYS) {
    Object *cap;
    if (!kcls) kcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Text, NULL, sizeof(struct KData), (APTR)kcap_dispatch);
    col = cmd_column(&k_cmds, &k_filter, ID_CU_K_CMDS, ID_CU_K_FILTER, "Commands:");
    cap = kcls ? NewObject(kcls->mcc_Class, NULL, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", TAG_DONE) : NULL;
    g = VGroup,
      Child, col,
      Child, ColGroup(2),
        Child, Label1(tr("Current keys:")), Child, k_cur = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
        Child, Label1(tr("New key:")), Child, VGroup, Child, cap ? cap : HSpace(0), Child, k_new = TextObject, MUIA_Text_Contents, (ULONG)"", End, End,
      End,
      Child, k_note = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, HGroup, Child, b1 = QButton(tr("Assign")), Child, b2 = QButton(tr("Remove")), Child, HSpace(0), Child, b3 = QButton(tr("Reset All")), End,
    End;
    if (!g) return NULL;
    RET(b1, MUIA_Pressed, FALSE, ID_CU_K_ASSIGN); RET(b2, MUIA_Pressed, FALSE, ID_CU_K_REMOVE); RET(b3, MUIA_Pressed, FALSE, ID_CU_K_RESET);
  }
  return g;
}
void wp26_pane_load(void)
{
  load_custom(); load_keys();
  wrc = rc;
  if (!wkm_made) { wkm_made = 1; keymap_init(&wkm, defkeys, NDEFKEYS); }
  wkm.n = 0;
  { int i; for (i = 0; i < km.n; i++) keymap_set(&wkm, km.k[i].code, km.k[i].mods, km.k[i].id); }
  cap_ok = 0;
  if (r_tree) {
    fill_tree(); fill_cmds(0, r_cmds, r_filter); fill_cust();
    set(r_tabname, MUIA_String_Contents, (ULONG)(rc.tabname[0] ? rc.tabname : tr("My Tab")));
    set(r_grpname, MUIA_String_Contents, (ULONG)(wrc.ngroup ? wrc.group[wrc.ngroup - 1].name : tr("My Group")));
  }
  if (q_list) { fill_cmds(1, q_cmds, q_filter); fill_qat(); }
  if (k_cmds) { fill_cmds(2, k_cmds, k_filter); show_keys(); }
}
void wp26_pane_save(void)
{
  char *s = NULL;
  rc = wrc;
  km.n = 0;
  { int i; for (i = 0; i < wkm.n; i++) keymap_set(&km, wkm.k[i].code, wkm.k[i].mods, wkm.k[i].id); }
  if (r_tabname) {
    get(r_tabname, MUIA_String_Contents, &s);
    strncpy(rc.tabname, s ? s : "", sizeof rc.tabname - 1); rc.tabname[sizeof rc.tabname - 1] = 0;
    rcustom_clean_name(rc.tabname);
    if (!strcmp(rc.tabname, tr("My Tab"))) rc.tabname[0] = 0;
  }
  save_custom();
  save_keys();
  ribbon_rebuild();
}
int wp26_pane_command(ULONG id)
{
  int i, g, e;
  switch (id) {
  case ID_CU_R_TREE: case ID_CU_R_SHOW: case ID_CU_R_HIDE: {
    LONG a = -1;
    const char *gn[MAXG_NAMES], *tn, *grp = NULL;
    get(r_tree, MUIA_List_Active, &a);
    if (a < 0 || a >= nrrow) break;
    tn = ribbon_tab_name(rrow[a].tab);
    if (!tn) break;
    if (rrow[a].group >= 0) { ribbon_tab_groups(rrow[a].tab, gn, MAXG_NAMES); grp = gn[rrow[a].group]; }
    rcustom_set_hidden(&wrc, tn, grp, id == ID_CU_R_HIDE ? 1 : id == ID_CU_R_SHOW ? 0 : !rcustom_hidden(&wrc, tn, grp));
    fill_tree();
    break; }
  case ID_CU_R_FILTER: fill_cmds(0, r_cmds, r_filter); break;
  case ID_CU_R_NEWGROUP: {
    char *s = NULL;
    RcGroup *ng = &wrc.group[wrc.ngroup];
    if (wrc.ngroup >= RC_MAXGROUP) break;
    get(r_grpname, MUIA_String_Contents, &s);
    strncpy(ng->name, s && s[0] ? s : tr("My Group"), sizeof ng->name - 1); ng->name[sizeof ng->name - 1] = 0;
    rcustom_clean_name(ng->name);
    ng->n = 0; wrc.ngroup++;
    fill_cust();
    set(r_cust, MUIA_List_Active, MUIV_List_Active_Bottom);
    break; }
  case ID_CU_R_ADD:
    if ((i = chosen_cmd(0, r_cmds)) < 0) break;
    if (!wrc.ngroup) { strncpy(wrc.group[0].name, tr("My Group"), sizeof wrc.group[0].name - 1); rcustom_clean_name(wrc.group[0].name); wrc.group[0].n = 0; wrc.ngroup = 1; }
    if (!cust_pos(&g, &e)) g = wrc.ngroup - 1;
    if (wrc.group[g].n >= RC_MAXCMD) break;
    cmd_ref(cmds[i].id, &wrc.group[g].cmd[wrc.group[g].n++]);
    fill_cust();
    break;
  case ID_CU_R_REMOVE:
    if (!cust_pos(&g, &e)) break;
    if (e < 0) { memmove(&wrc.group[g], &wrc.group[g + 1], (size_t)(wrc.ngroup - g - 1) * sizeof wrc.group[0]); wrc.ngroup--; }
    else { memmove(&wrc.group[g].cmd[e], &wrc.group[g].cmd[e + 1], (size_t)(wrc.group[g].n - e - 1) * sizeof(CmdRef)); wrc.group[g].n--; }
    fill_cust();
    break;
  case ID_CU_R_UP: case ID_CU_R_DOWN: {
    LONG a = -1;
    int d = id == ID_CU_R_UP ? -1 : 1;
    if (!cust_pos(&g, &e)) break;
    get(r_cust, MUIA_List_Active, &a);
    if (e < 0) {
      RcGroup t;
      if (g + d < 0 || g + d >= wrc.ngroup) break;
      t = wrc.group[g]; wrc.group[g] = wrc.group[g + d]; wrc.group[g + d] = t;
      fill_cust();
    } else {
      CmdRef t;
      if (e + d < 0 || e + d >= wrc.group[g].n) break;
      t = wrc.group[g].cmd[e]; wrc.group[g].cmd[e] = wrc.group[g].cmd[e + d]; wrc.group[g].cmd[e + d] = t;
      fill_cust();
      set(r_cust, MUIA_List_Active, a + d);
    }
    break; }
  case ID_CU_R_RESET:
    wrc.nhide = 0; wrc.ngroup = 0;
    set(r_tabname, MUIA_String_Contents, (ULONG)tr("My Tab"));
    fill_tree(); fill_cust();
    break;
  case ID_CU_Q_FILTER: fill_cmds(1, q_cmds, q_filter); break;
  case ID_CU_Q_ADD:
    if ((i = chosen_cmd(1, q_cmds)) < 0 || wrc.nqat >= RC_MAXQAT) break;
    for (g = 0; g < wrc.nqat; g++) if (wrc.qat[g].id == cmds[i].id) break;
    if (g < wrc.nqat) break;
    cmd_ref(cmds[i].id, &wrc.qat[wrc.nqat++]); wrc.qatset = 1;
    fill_qat();
    break;
  case ID_CU_Q_REMOVE: case ID_CU_Q_UP: case ID_CU_Q_DOWN: {
    LONG a = -1;
    get(q_list, MUIA_List_Active, &a);
    if (a < 0 || a >= wrc.nqat) break;
    wrc.qatset = 1;
    if (id == ID_CU_Q_REMOVE) { memmove(&wrc.qat[a], &wrc.qat[a + 1], (size_t)(wrc.nqat - a - 1) * sizeof(CmdRef)); wrc.nqat--; fill_qat(); break; }
    {
      LONG b = a + (id == ID_CU_Q_UP ? -1 : 1);
      CmdRef t;
      if (b < 0 || b >= wrc.nqat) break;
      t = wrc.qat[a]; wrc.qat[a] = wrc.qat[b]; wrc.qat[b] = t;
      fill_qat();
      set(q_list, MUIA_List_Active, b);
    }
    break; }
  case ID_CU_Q_RESET: qat_defaults(&wrc); wrc.qatset = 0; fill_qat(); break;
  case ID_CU_K_FILTER: fill_cmds(2, k_cmds, k_filter); show_keys(); break;
  case ID_CU_K_CMDS: show_keys(); break;
  case ID_CU_K_ASSIGN:
    if ((i = chosen_cmd(2, k_cmds)) < 0 || !cap_ok) break;
    keymap_set(&wkm, cap_code, cap_mods, cmds[i].id);
    cap_ok = 0;
    show_keys();
    break;
  case ID_CU_K_REMOVE:
    if ((i = chosen_cmd(2, k_cmds)) < 0) break;
    keymap_unbind(&wkm, cmds[i].id);
    show_keys();
    break;
  case ID_CU_K_RESET:
    keymap_reset(&wkm);
    show_keys();
    break;
  default: return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* the package's settings in the Options window */
static const WpOption options[] = {
  { OP_GENERAL, OT_CHECK, "Show the mini toolbar on a selection", "minibar", NULL, 0, 1, 1, &opt_minibar },
  { OP_DISPLAY, OT_HEAD, "Show on the screen" },
  { OP_DISPLAY, OT_TEXT, "These switch at once, as in the View menu." },
  { OP_DISPLAY, OT_BUTTON, "Formatting Marks", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_MARKS },
  { OP_DISPLAY, OT_BUTTON, "Ruler", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_RULER },
  { OP_DISPLAY, OT_BUTTON, "Dark Mode", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_DARKPAGE },
  { OP_PROOFING, OT_HEAD, "Proofing" },
  { OP_PROOFING, OT_BUTTON, "AutoCorrect Options...", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_WP8_FIRST + 3 },   /* (WP8's ID_PR_ACOPTS) */
  { OP_PROOFING, OT_BUTTON, "Set Proofing Language...", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, WP31_LANGWIN_ID },
  { OP_PROOFING, OT_TEXT, "Spelling and grammar as you type: Review > Proofing." },
  { OP_ACCESSIBILITY, OT_HEAD, "Accessibility" },
  { OP_ACCESSIBILITY, OT_BUTTON, "Check Accessibility", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_INFO_ACC },
  { OP_ADVANCED, OT_HEAD, "Finding commands" },
  { OP_ADVANCED, OT_CHECK, "Tell Me box on the tab strip (the Help key opens it too)", "tellme", NULL, 0, 1, 1, &opt_tellme, NULL, NULL, NULL, 0, ribbon_rebuild },
  { OP_ADVANCED, OT_TEXT, "F10 shows KeyTips: letters for the tabs and tools. Esc goes back." },
  { OP_ADVANCED, OT_BUTTON, "Customize Ribbon...", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_CU_RIBBON },
  { OP_ADVANCED, OT_BUTTON, "Customize Keyboard...", NULL, NULL, 0, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, ID_CU_KEYS },
};

/* ------------------------------------------------------------------ */
/* the commands */
static void caret_size(int delta)
{
  Doc *d = wp_doc();
  Pos *c = wp_caret();
  int sz;
  if (!d || !c || c->para >= d->np) return;
  sz = para_fmt_before(d->p[c->para], c->off)->size + delta;
  if (sz < 2) sz = 2;
  if (sz > 3276) sz = 3276;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SIZE, (ULONG)sz);
}
static void apply_style(const char *name)
{
  int k = doc_style(wp_doc(), name);
  if (k < 0) { DisplayBeep(NULL); return; }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_STYLE, (ULONG)k);
}
static void list_value(ULONG id, LONG v)
{
  set(wp_ribbon(), MUIA_Ribbon_Value, (ULONG)v);
  DoMethod(wp_app(), MUIM_Application_ReturnID, id);
}
static void type_char(u32 cp)
{
  char u[8];
  u[utf8_put(u, cp)] = 0;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_TEXT, (ULONG)u);
}
int wp26_command(ULONG id, LONG value)
{
  static int casestep;
  if (id >= ID_CU_PANE_FIRST && id <= ID_CU_PANE_LAST) { wp26_pane_command(id); return WPR_KEEPFOCUS; }
  if (wp_options_command(id)) return WPR_DONE;
  switch (id) {
  case ID_CU_NOP: break;
  case ID_CU_QAADD: case ID_CU_QAREMOVE: {
    ULONG c = ribbon_ctx_command();
    int i;
    load_custom();
    if (!c) break;
    for (i = 0; i < rc.nqat && rc.qat[i].id != c; i++) ;
    if (id == ID_CU_QAADD && i == rc.nqat && rc.nqat < RC_MAXQAT) cmd_ref(c, &rc.qat[rc.nqat++]);
    if (id == ID_CU_QAREMOVE && i < rc.nqat) { memmove(&rc.qat[i], &rc.qat[i + 1], (size_t)(rc.nqat - i - 1) * sizeof(CmdRef)); rc.nqat--; }
    rc.qatset = 1;
    save_custom();
    ribbon_rebuild();
    break; }
  case ID_CU_OPTIONS: wp_options_open(OP_GENERAL); return WPR_KEEPFOCUS;
  case ID_CU_RIBBON: wp_options_open(OP_RIBBON); return WPR_KEEPFOCUS;
  case ID_CU_QAT: wp_options_open(OP_QAT); return WPR_KEEPFOCUS;
  case ID_CU_KEYS: wp_options_open(OP_KEYS); return WPR_KEEPFOCUS;
  case ID_CU_TELLME: wp26_tellme_open(-1, -1); return WPR_KEEPFOCUS;
  case ID_CU_TELLTYPE: if (tmwin) tell_fill(); return WPR_KEEPFOCUS;
  case ID_CU_TELLRUN: if (tmwin) tell_run(); break;
  case ID_CU_KEYTIPS: if (wp_ribbon()) kt_on = (int)DoMethod(wp_ribbon(), MUIM_Ribbon_KeyTip, KT_START); break;
  case ID_CU_H1: apply_style("Heading1"); break;
  case ID_CU_H2: apply_style("Heading2"); break;
  case ID_CU_H3: apply_style("Heading3"); break;
  case ID_CU_NORMAL: apply_style("Normal"); break;
  case ID_CU_CASE: casestep = (casestep % 3) + 1; list_value(ID_CASE, casestep); break;   /* lower, UPPER, Capitalise in turn */
  case ID_CU_GROW1: caret_size(2); break;
  case ID_CU_SHRINK1: caret_size(-2); break;
  case ID_CU_DBLUL: list_value(ID_UNDERLINEDD, UL_DOUBLE); break;
  case ID_CU_WORDUL: list_value(ID_UNDERLINEDD, UL_WORDS); break;
  case ID_CU_FONTBOX: run_cmd(ID_FONTDD); break;
  case ID_CU_SIZEBOX: run_cmd(ID_SIZEDD); break;
  case ID_CU_STYLEBOX: run_cmd(ID_STYLEPICK); break;
  case ID_CU_COPYR: type_char(0xA9); break;
  case ID_CU_REGD: type_char(0xAE); break;
  case ID_CU_TRADE: type_char(0x2122); break;
  case ID_CU_ELLIPSIS: type_char(0x2026); break;
  case ID_CU_EMDASH: type_char(0x2014); break;
  case ID_CU_ENDASH: type_char(0x2013); break;
  case ID_CU_MINIRUN: mini_do(); break;
  case ID_CU_COLLAPSE: if (wp_ribbon()) DoMethod(wp_ribbon(), MUIM_Ribbon_Collapse); break;
  }
  return WPR_DONE;
}
static void init(void)
{
  wp_options_add(options, (int)(sizeof options / sizeof options[0]));
  {
    /* (SetEnv QUILLFIT 1) the fit check: every tab in every language, into PROGDIR:Quill.fit */
    char v[8];
    if (GetVar((STRPTR)"QUILLFIT", (STRPTR)v, sizeof v, 0) > 0 && wp_ribbon()) {
      FILE *f = fopen("PROGDIR:Quill.fit", "w");
      if (f) { DoMethod(wp_ribbon(), MUIM_Ribbon_FitCheck, (ULONG)f); fclose(f); }
    }
  }
}
/* the language changed: the windows go (made again in the new words) */
static void drop(void)
{
  Object **w[] = { &tmwin, &mwin };
  int i;
  mini_close();
  if (mobj) mini_timer(mobj, 0);
  wp_options_drop();
  for (i = 0; i < 2; i++)
    if (*w[i]) { set(*w[i], MUIA_Window_Open, FALSE); DoMethod(wp_app(), OM_REMMEMBER, (ULONG)*w[i]); MUI_DisposeObject(*w[i]); *w[i] = NULL; }
  mobj = NULL;
}
static void cleanup(void)
{
  if (mcls) { MUI_DeleteCustomClass(mcls); mcls = NULL; }
  if (kcls) { MUI_DeleteCustomClass(kcls); kcls = NULL; }
}

static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Typographic Symbols", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Copyright (Ctrl-Alt-C)", 0, 0, 0, (APTR)ID_CU_COPYR } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Registered (Ctrl-Alt-R)", 0, 0, 0, (APTR)ID_CU_REGD } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Trade Mark (Ctrl-Alt-T)", 0, 0, 0, (APTR)ID_CU_TRADE } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Ellipsis (Ctrl-Alt-.)", 0, 0, 0, (APTR)ID_CU_ELLIPSIS } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Em Dash (Ctrl-Alt-minus)", 0, 0, 0, (APTR)ID_CU_EMDASH } },
  { WM_INSERT, { NM_SUB, (STRPTR)"En Dash (Ctrl-keypad minus)", 0, 0, 0, (APTR)ID_CU_ENDASH } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Quick Formatting", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Heading 1 (Ctrl-Alt-1)", 0, 0, 0, (APTR)ID_CU_H1 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Heading 2 (Ctrl-Alt-2)", 0, 0, 0, (APTR)ID_CU_H2 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Heading 3 (Ctrl-Alt-3)", 0, 0, 0, (APTR)ID_CU_H3 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Normal (Ctrl-Shift-N)", 0, 0, 0, (APTR)ID_CU_NORMAL } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Change Case (Shift-F3)", 0, 0, 0, (APTR)ID_CU_CASE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Double Underline (Ctrl-Shift-D)", 0, 0, 0, (APTR)ID_CU_DBLUL } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Word Underline (Ctrl-Shift-W)", 0, 0, 0, (APTR)ID_CU_WORDUL } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Grow Font 1 pt (Ctrl-])", 0, 0, 0, (APTR)ID_CU_GROW1 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Shrink Font 1 pt (Ctrl-[)", 0, 0, 0, (APTR)ID_CU_SHRINK1 } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Font Box (Ctrl-Shift-F)", 0, 0, 0, (APTR)ID_CU_FONTBOX } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Size Box (Ctrl-Shift-P)", 0, 0, 0, (APTR)ID_CU_SIZEBOX } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Apply Styles (Ctrl-Shift-S)", 0, 0, 0, (APTR)ID_CU_STYLEBOX } },
  { WM_VIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Customize", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Options...", 0, 0, 0, (APTR)ID_CU_OPTIONS } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Customize Ribbon...", 0, 0, 0, (APTR)ID_CU_RIBBON } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Customize Quick Access Toolbar...", 0, 0, 0, (APTR)ID_CU_QAT } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Customize Keyboard...", 0, 0, 0, (APTR)ID_CU_KEYS } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Tell Me... (Help)", 0, 0, 0, (APTR)ID_CU_TELLME } },
  { WM_VIEW, { NM_SUB, (STRPTR)"KeyTips (F10)", 0, 0, 0, (APTR)ID_CU_KEYTIPS } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Collapse the Ribbon (Ctrl-F1)", 0, 0, 0, (APTR)ID_CU_COLLAPSE } },
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp26_package = {
  "WP26", ID_WP26_FIRST, ID_WP26_LAST,
  wp26_command, NULL, NULL, ctxmenu, key, init, cleanup,
  NULL, 0,
  menus, (int)(sizeof menus / sizeof menus[0]),
  drop
};
