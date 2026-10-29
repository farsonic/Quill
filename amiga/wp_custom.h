/*
 * wp_custom.h - WP26 Customization and discoverability: what ribbon.c, wp_custom.c and wp_options.c
 * ask of each other. ribbon.c draws (group collapsing, the custom tab, the Quick Access Toolbar, the
 * Tell Me box, KeyTips, its right-button menu); wp_custom.c keeps the user's choices in Quill.prefs and
 * does Tell Me, the keys and the mini toolbar; wp_options.c is the Options window.
 */
#ifndef QUILL_WP_CUSTOM_H
#define QUILL_WP_CUSTOM_H

#include "wphooks.h"

/* ---- wp_custom.c, asked by ribbon.c (English names: tabs' and groups' as ribbon.c writes them) */
int  wp26_tab_hidden(const char *tab);
int  wp26_group_hidden(const char *tab, const char *group);
int  wp26_custom_tab(const char **name, const Group **groups);   /* the user's tab: its groups (0: none) */
int  wp26_qat(ULONG *ids, int max);                              /* the Quick Access Toolbar's commands */
int  wp26_tellme_box(void);                                      /* the Tell Me box on the tab strip */
void wp26_tellme_open(LONG x, LONG y);                           /* (screen point under the box) */
const char *wp26_cmd_name(ULONG id);                             /* a command's name (English), from the ribbon or the menus */

/* ---- ribbon.c */
void ribbon_rebuild(void);                 /* the tabs again after a customizing change; every ribbon redrawn */
typedef struct { ULONG id; const char *label, *tab, *group; const Item *it; } RibbonCmd;
int  ribbon_commands(RibbonCmd *out, int max);   /* each command of the tabs (Quill's and the packages'), at its first place */
int  ribbon_tab_count(void);
const char *ribbon_tab_name(int t);        /* English; NULL for the File tab and tabs no one has filled */
int  ribbon_tab_contextual(int t);
int  ribbon_tab_groups(int t, const char **names, int max);   /* the groups tab t has before hiding (English) */
const Item *ribbon_item_of(ULONG id);      /* a ribbon item for the command (NULL: only a menu has it) */
void ribbon_item_name(const Item *it, char *out, size_t cap);  /* its name (English): the label, or its help up to "(" or ":" */
int  ribbon_is_list(ULONG id);             /* the command is a list on the ribbon (choosing it opens the list) */
ULONG ribbon_ctx_command(void);            /* the command right-clicked on the ribbon last */
/* methods (QTAG range of quill.h) */
#define MUIM_Ribbon_KeyTip   (QTAG + 0x312)   /* key: KT_START, KT_STOP or a typed character; returns 1 while KeyTips stay */
#define MUIM_Ribbon_OpenId   (QTAG + 0x313)   /* id: its tab shown and its list opened (or the command run) */
#define MUIM_Ribbon_Deferred (QTAG + 0x314)   /* (inside ribbon.c) a list opened after another closed */
#define MUIM_Ribbon_FitCheck (QTAG + 0x315)   /* file: each tab laid out in each language at the ribbon's width, reported */
#define MUIM_Ribbon_Collapse (QTAG + 0x316)   /* the tools hidden (the tab strip alone) or shown again */
#define KT_START (-1)
#define KT_STOP  (-2)
struct MUIP_Ribbon_KeyTip { ULONG MethodID; LONG key; };
struct MUIP_Ribbon_OpenId { ULONG MethodID; ULONG id; };
struct MUIP_Ribbon_FitCheck { ULONG MethodID; APTR file; };

/* ---- wp_options.c: the Options window's customizing panes (wp_custom.c builds them) */
enum { OP_RIBBON = OP_NSETTINGS, OP_QAT, OP_KEYS, OP_NPANES };
Object *wp26_pane(int pane);               /* the pane's contents, made when the window is made */
void wp26_pane_load(void);                 /* the panes show the current choices (the window opening) */
void wp26_pane_save(void);                 /* OK: the panes' choices kept and saved */
int  wp26_pane_command(ULONG id);          /* a gadget of the panes (one of WP26's ids): 1 if it was one */
void wp_prefs_each(const char *prefix, void (*fn)(const char *key, const char *val));   /* Quill.prefs lines starting so */
void wp_options_drop(void);
int  wp_options_command(ULONG id);         /* the window's OK (1 if it was) */                /* the language changed: the Options window goes */

#endif
