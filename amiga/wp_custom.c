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
/* what the user customized (from Quill.prefs, read on first use) */
#define MAXHIDE 64
#define MAXCG 6
#define MAXCI 24
static char hides[MAXHIDE][64]; static int nhide;
static char ctabname[40];
static char cgname[MAXCG][40]; static ULONG cgids[MAXCG][MAXCI]; static int cgn[MAXCG], ncg;
static ULONG qat[24]; static int nqat;
static int custom_loaded;
static Group cgroups[MAXCG]; static Item citems[MAXCG][MAXCI];
static LONG opt_tellme = 1, opt_minibar = 1;
static void load_custom(void)
{
  const char *s;
  CmdRef r[32];
  int i, n, g;
  if (custom_loaded) return;
  custom_loaded = 1;
  nhide = 0;
  if ((s = wp_pref("ribbon.hide")))
    while (*s && nhide < MAXHIDE) {
      size_t l = strcspn(s, ";");
      if (l && l < sizeof hides[0]) { memcpy(hides[nhide], s, l); hides[nhide][l] = 0; nhide++; }
      s += l; if (*s) s++;
    }
  s = wp_pref("ribbon.tab");
  strncpy(ctabname, s ? s : "", sizeof ctabname - 1); ctabname[sizeof ctabname - 1] = 0;
  ncg = 0;
  for (g = 0; g < MAXCG; g++) {
    char key[20];
    const char *colon;
    sprintf(key, "ribbon.group%d", g + 1);
    if (!(s = wp_pref(key)) || !(colon = strchr(s, ':'))) continue;
    n = (int)(colon - s);
    if (n >= (int)sizeof cgname[0]) n = sizeof cgname[0] - 1;
    memcpy(cgname[ncg], s, (size_t)n); cgname[ncg][n] = 0;
    n = cmdlist_parse(colon + 1, r, MAXCI);
    cgn[ncg] = 0;
    for (i = 0; i < n; i++) { ULONG id = cmd_resolve(&r[i]); if (id) cgids[ncg][cgn[ncg]++] = id; }
    ncg++;
  }
  nqat = 0;
  if ((s = wp_pref("qat"))) {
    n = cmdlist_parse(s, r, 24);
    for (i = 0; i < n; i++) { ULONG id = cmd_resolve(&r[i]); if (id) qat[nqat++] = id; }
  } else { qat[0] = ID_INFO_AUTOSAVE; qat[1] = ID_SAVE; qat[2] = ID_UNDO; qat[3] = ID_REDO; nqat = 4; }
}
static void save_custom(void)
{
  char buf[1600], key[20];
  CmdRef r[32];
  int i, g;
  buf[0] = 0;
  for (i = 0; i < nhide; i++) { if (strlen(buf) + strlen(hides[i]) + 2 >= sizeof buf) break; if (i) strcat(buf, ";"); strcat(buf, hides[i]); }
  wp_pref_set("ribbon.hide", nhide ? buf : NULL);
  wp_pref_set("ribbon.tab", ctabname[0] ? ctabname : NULL);
  for (g = 0; g < MAXCG; g++) {
    sprintf(key, "ribbon.group%d", g + 1);
    if (g >= ncg) { wp_pref_set(key, NULL); continue; }
    for (i = 0; i < cgn[g]; i++) cmd_ref(cgids[g][i], &r[i]);
    snprintf(buf, sizeof buf, "%s:", cgname[g]);
    cmdlist_format(r, cgn[g], buf + strlen(buf), sizeof buf - strlen(buf));
    wp_pref_set(key, buf);
  }
  for (i = 0; i < nqat; i++) cmd_ref(qat[i], &r[i]);
  cmdlist_format(r, nqat, buf, sizeof buf);
  wp_pref_set("qat", buf);
}
static int hidden(const char *what)
{
  int i;
  for (i = 0; i < nhide; i++) if (!strcmp(hides[i], what)) return 1;
  return 0;
}
int wp26_tab_hidden(const char *tab) { load_custom(); return hidden(tab); }
int wp26_group_hidden(const char *tab, const char *group)
{
  char t[100];
  load_custom();
  snprintf(t, sizeof t, "%s/%s", tab, group);
  return hidden(t);
}
int wp26_custom_tab(const char **name, const Group **groups)
{
  int g, i, n = 0;
  load_custom();
  for (g = 0; g < ncg; g++) {
    int k = 0;
    for (i = 0; i < cgn[g]; i++) {
      const Item *it = ribbon_item_of(cgids[g][i]);
      Item *c = &citems[n][k];
      if (it) *c = *it;
      else { memset(c, 0, sizeof *c); c->kind = K_BUTTON; c->id = (UWORD)cgids[g][i]; }
      c->size = S_SMALL; c->flags &= (u8)~(F_ROW1 | F_NEWCOL);
      if (c->kind == K_GALLERY) c->kind = K_DROP;
      if (c->kind == K_LABEL) continue;
      if (!c->label) c->label = wp26_cmd_name(cgids[g][i]);
      if (c->kind == K_FIELD && !c->fieldw) c->fieldw = 60;
      k++;
    }
    if (!k) continue;
    cgroups[n].name = cgname[g]; cgroups[n].mode = G_COLS; cgroups[n].items = citems[n]; cgroups[n].n = k;
    n++;
  }
  *name = ctabname[0] ? ctabname : "My Tab";
  *groups = cgroups;
  return n;
}
int wp26_qat(ULONG *ids, int max)
{
  int i;
  load_custom();
  for (i = 0; i < nqat && i < max; i++) ids[i] = qat[i];
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
  int score[12], i, k;
  ntm = 0;
  get(tmstr, MUIA_String_Contents, &q);
  strncpy(ql, q ? q : "", sizeof ql - 1); ql[sizeof ql - 1] = 0;
  make_cmds();
  set(tmlist, MUIA_List_Quiet, TRUE);
  DoMethod(tmlist, MUIM_List_Clear);
  if (!ql[0]) {
    /* nothing typed: the commands used from here lately */
    for (i = 0; i < ntmrecent && ntm < 12; i++) { k = cmd_index(tmrecent[i]); if (k >= 0) tmidx[ntm++] = k; }
  } else
    for (i = 0; i < ncmd; i++) {
      int s = cmd_match(tr(cmds[i].name), ql), j;
      if (!s) s = cmd_match(cmds[i].name, ql) / 2;       /* (the English name too) */
      if (!s) continue;
      for (j = ntm; j > 0 && score[j - 1] < s; j--) if (j < 12) { score[j] = score[j - 1]; tmidx[j] = tmidx[j - 1]; }
      if (j < 12) { score[j] = s; tmidx[j] = i; if (ntm < 12) ntm++; }
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
typedef struct { u16 code; u8 mods; ULONG id; } KeyBind;
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
/* the user's map: the defaults with the user's changes (id 0: a default taken off) */
#define MAXKEYS 160
#define MAXG_NAMES 40
static KeyBind ukeys[MAXKEYS]; static int nukeys, keys_loaded;
void wp26_key_pref(const char *key, const char *val);
static void load_keys(void)
{
  int i;
  if (keys_loaded) return;
  keys_loaded = 1;
  nukeys = 0;
  for (i = 0; i < NDEFKEYS && nukeys < MAXKEYS; i++) ukeys[nukeys++] = defkeys[i];
  wp_prefs_each("key.", wp26_key_pref);        /* the user's changes */
}
/* the user's changes are in Quill.prefs as key.<name>=<command> lines; wp_options.c's store hands
   them over one by one (wp26_key_pref) */
static void key_set(u16 code, u8 mods, ULONG id)
{
  int i;
  load_keys();
  for (i = 0; i < nukeys; i++) if (ukeys[i].code == code && ukeys[i].mods == mods) { ukeys[i].id = id; return; }
  if (nukeys < MAXKEYS) { ukeys[nukeys].code = code; ukeys[nukeys].mods = mods; ukeys[nukeys].id = id; nukeys++; }
}
void wp26_key_pref(const char *key, const char *val)
{
  u16 code; u8 mods;
  CmdRef r;
  if (strncmp(key, "key.", 4) || !key_parse(key + 4, &code, &mods)) return;
  if (cmdlist_parse(val, &r, 1) == 1) key_set(code, mods, cmd_resolve(&r));
  else key_set(code, mods, 0);
}
static int key_default(u16 code, u8 mods)
{
  int i;
  for (i = 0; i < NDEFKEYS; i++) if (defkeys[i].code == code && defkeys[i].mods == mods) return (int)defkeys[i].id;
  return 0;
}
/* the map back into Quill.prefs: each key that differs from the default */
static void save_keys(void)
{
  int i;
  for (i = 0; i < nukeys; i++) {
    char k[48], v[80];
    CmdRef r;
    strcpy(k, "key.");
    key_format(ukeys[i].code, ukeys[i].mods, k + 4, sizeof k - 4);
    if ((ULONG)key_default(ukeys[i].code, ukeys[i].mods) == ukeys[i].id) { wp_pref_set(k, NULL); continue; }
    if (ukeys[i].id) { cmd_ref(ukeys[i].id, &r); cmdlist_format(&r, 1, v, sizeof v); } else strcpy(v, "0");
    wp_pref_set(k, v);
  }
}
static u8 mods_of(UWORD qual)
{
  return (u8)(((qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? KM_SHIFT : 0) | ((qual & IEQUALIFIER_CONTROL) ? KM_CTRL : 0) |
              ((qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? KM_ALT : 0));
}
static ULONG key_lookup(u16 code, u8 mods, int user_only)
{
  int i;
  load_keys();
  for (i = 0; i < nukeys; i++)
    if (ukeys[i].code == code && ukeys[i].mods == mods) {
      if (user_only && (ULONG)key_default(code, mods) == ukeys[i].id) return 0;
      return ukeys[i].id;
    }
  return 0;
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
static char w_hides[MAXHIDE][64]; static int w_nhide;
static char w_cgname[MAXCG][40]; static ULONG w_cgids[MAXCG][MAXCI]; static int w_cgn[MAXCG], w_ncg;
static ULONG w_qat[24]; static int w_nqat;
static KeyBind w_keys[MAXKEYS]; static int w_nkeys;
static u16 cap_code; static u8 cap_mods; static int cap_ok;
/* the rows of the ribbon list: a tab (group -1) or a group of it */
static struct { int tab, group; char line[80]; } rrow[300];
static int nrrow;
static int fil_idx[3][MAXCMD], nfil[3];          /* each pane's command list, filtered */
static int w_hidden(const char *what)
{
  int i;
  for (i = 0; i < w_nhide; i++) if (!strcmp(w_hides[i], what)) return 1;
  return 0;
}
static void w_sethidden(const char *what, int on)
{
  int i;
  for (i = 0; i < w_nhide && strcmp(w_hides[i], what); i++) ;
  if (on && i == w_nhide && w_nhide < MAXHIDE) { strncpy(w_hides[w_nhide], what, 63); w_hides[w_nhide][63] = 0; w_nhide++; }
  if (!on && i < w_nhide) { memmove(w_hides[i], w_hides[i + 1], (size_t)(w_nhide - i - 1) * sizeof w_hides[0]); w_nhide--; }
}
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
    snprintf(rrow[nrrow].line, sizeof rrow[0].line, "[%c] %s%s", w_hidden(tn) ? ' ' : 'x', tr(tn), ribbon_tab_contextual(t) ? "  *" : "");
    rrow[nrrow].tab = t; rrow[nrrow].group = -1; nrrow++;
    n = ribbon_tab_groups(t, gn, MAXG_NAMES);
    for (g = 0; g < n && nrrow < 300; g++) {
      char w[100];
      snprintf(w, sizeof w, "%s/%s", tn, gn[g]);
      snprintf(rrow[nrrow].line, sizeof rrow[0].line, "      [%c] %s", w_hidden(w) ? ' ' : 'x', tr(gn[g]));
      rrow[nrrow].tab = t; rrow[nrrow].group = g; nrrow++;
    }
  }
  for (t = 0; t < nrrow; t++) DoMethod(r_tree, MUIM_List_InsertSingle, (ULONG)rrow[t].line, MUIV_List_Insert_Bottom);
  set(r_tree, MUIA_List_Quiet, FALSE);
  if (keep >= 0 && keep < nrrow) set(r_tree, MUIA_List_Active, keep);
}
static void fill_cust(void)
{
  static char lines[MAXCG * (MAXCI + 1)][80];
  int g, i, n = 0;
  set(r_cust, MUIA_List_Quiet, TRUE);
  DoMethod(r_cust, MUIM_List_Clear);
  for (g = 0; g < w_ncg; g++) {
    snprintf(lines[n], sizeof lines[0], "%s:", w_cgname[g]);
    DoMethod(r_cust, MUIM_List_InsertSingle, (ULONG)lines[n], MUIV_List_Insert_Bottom); n++;
    for (i = 0; i < w_cgn[g]; i++) {
      snprintf(lines[n], sizeof lines[0], "    %s", tr(wp26_cmd_name(w_cgids[g][i])));
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
  for (gg = 0; gg < w_ncg; gg++) {
    if (row == a) { *g = gg; *i = -1; return 1; }
    row++;
    if (a < row + w_cgn[gg]) { *g = gg; *i = (int)(a - row); return 1; }
    row += w_cgn[gg];
  }
  return 0;
}
static void fill_qat(void)
{
  static char lines[24][64];
  int i;
  set(q_list, MUIA_List_Quiet, TRUE);
  DoMethod(q_list, MUIM_List_Clear);
  for (i = 0; i < w_nqat; i++) {
    strncpy(lines[i], tr(wp26_cmd_name(w_qat[i])), sizeof lines[i] - 1); lines[i][sizeof lines[i] - 1] = 0;
    DoMethod(q_list, MUIM_List_InsertSingle, (ULONG)lines[i], MUIV_List_Insert_Bottom);
  }
  set(q_list, MUIA_List_Quiet, FALSE);
}
static void show_keys(void)
{
  static char t[200], nt[160];
  int i = chosen_cmd(2, k_cmds), j;
  t[0] = 0;
  if (i >= 0)
    for (j = 0; j < w_nkeys; j++)
      if (w_keys[j].id == cmds[i].id) {
        char k[32];
        key_format(w_keys[j].code, w_keys[j].mods, k, sizeof k);
        if (strlen(t) + strlen(k) + 3 < sizeof t) { if (t[0]) strcat(t, ", "); strcat(t, k); }
      }
  set(k_cur, MUIA_Text_Contents, (ULONG)(t[0] ? t : tr("(none)")));
  nt[0] = 0;
  if (cap_ok) {
    char k[32];
    key_format(cap_code, cap_mods, k, sizeof k);
    set(k_new, MUIA_Text_Contents, (ULONG)k);
    for (j = 0; j < w_nkeys; j++)
      if (w_keys[j].code == cap_code && w_keys[j].mods == cap_mods && w_keys[j].id) snprintf(nt, sizeof nt, tr("Currently assigned to: %s"), tr(wp26_cmd_name(w_keys[j].id)));
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
  memcpy(w_hides, hides, sizeof hides); w_nhide = nhide;
  memcpy(w_cgname, cgname, sizeof cgname); memcpy(w_cgids, cgids, sizeof cgids); memcpy(w_cgn, cgn, sizeof cgn); w_ncg = ncg;
  memcpy(w_qat, qat, sizeof qat); w_nqat = nqat;
  memcpy(w_keys, ukeys, sizeof ukeys); w_nkeys = nukeys;
  cap_ok = 0;
  if (r_tree) {
    fill_tree(); fill_cmds(0, r_cmds, r_filter); fill_cust();
    set(r_tabname, MUIA_String_Contents, (ULONG)(ctabname[0] ? ctabname : tr("My Tab")));
    set(r_grpname, MUIA_String_Contents, (ULONG)(w_ncg ? w_cgname[w_ncg - 1] : tr("My Group")));
  }
  if (q_list) { fill_cmds(1, q_cmds, q_filter); fill_qat(); }
  if (k_cmds) { fill_cmds(2, k_cmds, k_filter); show_keys(); }
}
void wp26_pane_save(void)
{
  char *s = NULL;
  memcpy(hides, w_hides, sizeof hides); nhide = w_nhide;
  memcpy(cgname, w_cgname, sizeof cgname); memcpy(cgids, w_cgids, sizeof cgids); memcpy(cgn, w_cgn, sizeof cgn); ncg = w_ncg;
  memcpy(qat, w_qat, sizeof qat); nqat = w_nqat;
  memcpy(ukeys, w_keys, sizeof ukeys); nukeys = w_nkeys;
  if (r_tabname) {
    get(r_tabname, MUIA_String_Contents, &s);
    strncpy(ctabname, s ? s : "", sizeof ctabname - 1); ctabname[sizeof ctabname - 1] = 0;
    if (!strcmp(ctabname, tr("My Tab"))) ctabname[0] = 0;
  }
  save_custom();
  save_keys();
  ribbon_rebuild();
}
static void clean_name(char *s) { for (; *s; s++) if (*s == ':' || *s == ';' || *s == ',' || *s == '|' || *s == '=') *s = ' '; }
int wp26_pane_command(ULONG id)
{
  int i, g, e;
  switch (id) {
  case ID_CU_R_TREE: case ID_CU_R_SHOW: case ID_CU_R_HIDE: {
    LONG a = -1;
    char w[100];
    const char *gn[MAXG_NAMES];
    get(r_tree, MUIA_List_Active, &a);
    if (a < 0 || a >= nrrow) break;
    if (rrow[a].group < 0) strcpy(w, ribbon_tab_name(rrow[a].tab));
    else { ribbon_tab_groups(rrow[a].tab, gn, MAXG_NAMES); snprintf(w, sizeof w, "%s/%s", ribbon_tab_name(rrow[a].tab), gn[rrow[a].group]); }
    w_sethidden(w, id == ID_CU_R_HIDE ? 1 : id == ID_CU_R_SHOW ? 0 : !w_hidden(w));
    fill_tree();
    break; }
  case ID_CU_R_FILTER: fill_cmds(0, r_cmds, r_filter); break;
  case ID_CU_R_NEWGROUP: {
    char *s = NULL;
    if (w_ncg >= MAXCG) break;
    get(r_grpname, MUIA_String_Contents, &s);
    strncpy(w_cgname[w_ncg], s && s[0] ? s : tr("My Group"), sizeof w_cgname[0] - 1); w_cgname[w_ncg][sizeof w_cgname[0] - 1] = 0;
    clean_name(w_cgname[w_ncg]);
    w_cgn[w_ncg] = 0; w_ncg++;
    fill_cust();
    set(r_cust, MUIA_List_Active, MUIV_List_Active_Bottom);
    break; }
  case ID_CU_R_ADD:
    if ((i = chosen_cmd(0, r_cmds)) < 0) break;
    if (!w_ncg) { strncpy(w_cgname[0], tr("My Group"), sizeof w_cgname[0] - 1); w_cgn[0] = 0; w_ncg = 1; }
    if (!cust_pos(&g, &e)) g = w_ncg - 1;
    if (w_cgn[g] >= MAXCI) break;
    w_cgids[g][w_cgn[g]++] = cmds[i].id;
    fill_cust();
    break;
  case ID_CU_R_REMOVE:
    if (!cust_pos(&g, &e)) break;
    if (e < 0) { memmove(&w_cgname[g], &w_cgname[g + 1], (size_t)(w_ncg - g - 1) * sizeof w_cgname[0]);
                 memmove(&w_cgids[g], &w_cgids[g + 1], (size_t)(w_ncg - g - 1) * sizeof w_cgids[0]);
                 memmove(&w_cgn[g], &w_cgn[g + 1], (size_t)(w_ncg - g - 1) * sizeof w_cgn[0]); w_ncg--; }
    else { memmove(&w_cgids[g][e], &w_cgids[g][e + 1], (size_t)(w_cgn[g] - e - 1) * sizeof(ULONG)); w_cgn[g]--; }
    fill_cust();
    break;
  case ID_CU_R_UP: case ID_CU_R_DOWN: {
    LONG a = -1;
    int d = id == ID_CU_R_UP ? -1 : 1;
    if (!cust_pos(&g, &e)) break;
    get(r_cust, MUIA_List_Active, &a);
    if (e < 0) {
      char tn[40]; ULONG ti[MAXCI]; int tc;
      if (g + d < 0 || g + d >= w_ncg) break;
      memcpy(tn, w_cgname[g], sizeof tn); memcpy(ti, w_cgids[g], sizeof ti); tc = w_cgn[g];
      memcpy(w_cgname[g], w_cgname[g + d], sizeof tn); memcpy(w_cgids[g], w_cgids[g + d], sizeof ti); w_cgn[g] = w_cgn[g + d];
      memcpy(w_cgname[g + d], tn, sizeof tn); memcpy(w_cgids[g + d], ti, sizeof ti); w_cgn[g + d] = tc;
      fill_cust();
    } else {
      ULONG t;
      if (e + d < 0 || e + d >= w_cgn[g]) break;
      t = w_cgids[g][e]; w_cgids[g][e] = w_cgids[g][e + d]; w_cgids[g][e + d] = t;
      fill_cust();
      set(r_cust, MUIA_List_Active, a + d);
    }
    break; }
  case ID_CU_R_RESET:
    w_nhide = 0; w_ncg = 0;
    set(r_tabname, MUIA_String_Contents, (ULONG)tr("My Tab"));
    fill_tree(); fill_cust();
    break;
  case ID_CU_Q_FILTER: fill_cmds(1, q_cmds, q_filter); break;
  case ID_CU_Q_ADD:
    if ((i = chosen_cmd(1, q_cmds)) < 0 || w_nqat >= 16) break;
    for (g = 0; g < w_nqat; g++) if (w_qat[g] == cmds[i].id) break;
    if (g < w_nqat) break;
    w_qat[w_nqat++] = cmds[i].id;
    fill_qat();
    break;
  case ID_CU_Q_REMOVE: case ID_CU_Q_UP: case ID_CU_Q_DOWN: {
    LONG a = -1;
    get(q_list, MUIA_List_Active, &a);
    if (a < 0 || a >= w_nqat) break;
    if (id == ID_CU_Q_REMOVE) { memmove(&w_qat[a], &w_qat[a + 1], (size_t)(w_nqat - a - 1) * sizeof(ULONG)); w_nqat--; fill_qat(); break; }
    {
      LONG b = a + (id == ID_CU_Q_UP ? -1 : 1);
      ULONG t;
      if (b < 0 || b >= w_nqat) break;
      t = w_qat[a]; w_qat[a] = w_qat[b]; w_qat[b] = t;
      fill_qat();
      set(q_list, MUIA_List_Active, b);
    }
    break; }
  case ID_CU_Q_RESET: w_qat[0] = ID_INFO_AUTOSAVE; w_qat[1] = ID_SAVE; w_qat[2] = ID_UNDO; w_qat[3] = ID_REDO; w_nqat = 4; fill_qat(); break;
  case ID_CU_K_FILTER: fill_cmds(2, k_cmds, k_filter); show_keys(); break;
  case ID_CU_K_CMDS: show_keys(); break;
  case ID_CU_K_ASSIGN:
    if ((i = chosen_cmd(2, k_cmds)) < 0 || !cap_ok) break;
    for (g = 0; g < w_nkeys; g++) if (w_keys[g].code == cap_code && w_keys[g].mods == cap_mods) break;
    if (g == w_nkeys && w_nkeys < MAXKEYS) w_nkeys++;
    if (g < w_nkeys) { w_keys[g].code = cap_code; w_keys[g].mods = cap_mods; w_keys[g].id = cmds[i].id; }
    cap_ok = 0;
    show_keys();
    break;
  case ID_CU_K_REMOVE:
    if ((i = chosen_cmd(2, k_cmds)) < 0) break;
    for (g = 0; g < w_nkeys; g++) if (w_keys[g].id == cmds[i].id) w_keys[g].id = 0;
    show_keys();
    break;
  case ID_CU_K_RESET:
    w_nkeys = 0;
    for (g = 0; g < NDEFKEYS; g++) w_keys[w_nkeys++] = defkeys[g];
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
    for (i = 0; i < nqat && qat[i] != c; i++) ;
    if (id == ID_CU_QAADD && i == nqat && nqat < 16) qat[nqat++] = c;
    if (id == ID_CU_QAREMOVE && i < nqat) { memmove(&qat[i], &qat[i + 1], (size_t)(nqat - i - 1) * sizeof(ULONG)); nqat--; }
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
