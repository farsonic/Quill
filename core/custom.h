/*
 * custom.h - WP26 Customization and discoverability (docs/wordcraft-gap.md): the portable parts of
 * Options, Customize Ribbon, the Quick Access Toolbar, Customize Keyboard, Tell Me and KeyTips, so
 * the native tests can check them. The Amiga side is amiga/wp_custom.c, amiga/wp_options.c and the
 * WP26 section of amiga/ribbon.c.
 *
 *  - a key=value store, as PROGDIR:Quill.prefs holds it (lines main.c doesn't know are kept and
 *    written back, so the work packages' settings live in the same file);
 *  - keys by name ("Ctrl+Shift+F5") and back, by the Amiga's raw key codes (positional: the names
 *    are the keys' places on an English keyboard, as the raw codes are);
 *  - lists of commands as prefs text: "id|English name,id|English name" (the name finds the command
 *    again if a later Quill numbers its commands differently);
 *  - KeyTips: short unique letter codes for a row of names;
 *  - Tell Me: how well a command's name matches what was typed, accents and case ignored.
 */
#ifndef QUILL_CUSTOM_H
#define QUILL_CUSTOM_H

#include <stdio.h>
#include "util.h"

/* ---- the prefs store */
typedef struct { char *k, *v; } PrefKV;
typedef struct { PrefKV *kv; int n, cap; } PrefStore;
void        pstore_free(PrefStore *ps);
int         pstore_line(PrefStore *ps, const char *line);        /* "key=value" kept (1), else 0 */
const char *pstore_get(const PrefStore *ps, const char *key);    /* NULL: not there */
int         pstore_set(PrefStore *ps, const char *key, const char *val);   /* val NULL: removed. 0 out of memory */
void        pstore_write(const PrefStore *ps, FILE *f);          /* each as a key=value line, in the order first set */

/* ---- keys. Modifier bits; the code is the Amiga raw key code (0-0x7F) */
#define KM_SHIFT 1
#define KM_CTRL  2
#define KM_ALT   4
int  key_parse(const char *s, u16 *code, u8 *mods);              /* "Ctrl+Shift+D", "F5", "Shift+Help": 1 if understood */
void key_format(u16 code, u8 mods, char *out, size_t cap);       /* the other way ("Ctrl+Alt+1") */
const char *key_name(u16 code);                                  /* "D", "F5", "Return"; NULL: a code without a name */

/* ---- command lists in prefs text */
typedef struct { u32 id; char name[48]; } CmdRef;
int  cmdlist_parse(const char *s, CmdRef *out, int max);         /* how many read */
void cmdlist_format(const CmdRef *c, int n, char *out, size_t cap);   /* (names with , | ; = are left out, the number alone kept) */

/* ---- KeyTips: codes[i] (1 or 2 upper-case letters or digits, NUL-terminated) for names[i] (Latin-1).
   A name whose first letter no other shares gets that letter; names sharing a first letter get two,
   that letter and another of the name's, so no code is the start of another. Entries with an empty
   or NULL name get "" */
void keytips_assign(const char *const *names, int n, char (*codes)[3]);
/* how a typed prefix stands against the codes: the index of the one it completes, -1 if it starts
   one or more (keep typing), -2 if it matches none */
int  keytips_match(char (*codes)[3], int n, const char *typed);

/* ---- Tell Me. A Latin-1 letter without its accent, lower case ('\311' -> 'e'); others as they are */
int  fold_latin1(int c);
/* how well name matches query (both Latin-1): 0 not at all; higher is better (every word of the
   query found in the name, a word's start better than its middle, the name's start best) */
int  cmd_match(const char *name, const char *query);
/* the command catalogue searched: names[i] in the language shown, alt[i] (NULL, or an entry NULL:
   none) the English name, which counts half. The best max indexes into out, best first (equal
   scores in catalogue order); how many. Nothing typed: 0 */
int  cmd_search(const char *const *names, const char *const *alt, int n, const char *query, int *out, int max);

/* ---- the key map: Quill's default keys, with the user's changes over them. Kept in prefs as
   "key.<name>=<command>" lines ("key.Ctrl+Shift+D=4512|Double Underline"), one for each key whose
   command differs from the default ("0": the key does nothing) */
typedef struct { u16 code; u8 mods; u32 id; } KeyBind;
typedef struct { const KeyBind *def; int ndef; KeyBind *k; int n, cap; } KeyMap;
int  keymap_init(KeyMap *km, const KeyBind *def, int ndef);      /* the defaults (def is kept: static). 0 out of memory */
void keymap_free(KeyMap *km);
int  keymap_set(KeyMap *km, u16 code, u8 mods, u32 id);          /* id 0: the key does nothing. 0 out of memory */
u32  keymap_get(const KeyMap *km, u16 code, u8 mods);            /* the key's command (0: none) */
u32  keymap_default(const KeyMap *km, u16 code, u8 mods);
int  keymap_changed(const KeyMap *km, u16 code, u8 mods);        /* the user's command differs from the default */
int  keymap_keys_of(const KeyMap *km, u32 id, KeyBind *out, int max);   /* the keys that run id (out may be NULL) */
void keymap_unbind(KeyMap *km, u32 id);                          /* every key of id does nothing */
void keymap_reset(KeyMap *km);                                   /* the defaults again */
/* the key.* lines of ps over the defaults; resolve finds a command from prefs (0: gone), by name first */
void keymap_load(KeyMap *km, const PrefStore *ps, u32 (*resolve)(const CmdRef *r, void *u), void *u);
/* every key.* line of ps replaced by the changed keys; name fills a command's CmdRef (its English name) */
void keymap_save(const KeyMap *km, PrefStore *ps, void (*name)(u32 id, CmdRef *r, void *u), void *u);

/* ---- what the user changed on the ribbon, as prefs lines:
     ribbon.hide=Insert;Home/Editing        tabs and groups (Tab/Group) hidden, English names
     ribbon.tab=My Tab                      the user's own tab's name (none: "My Tab")
     ribbon.group1=Group name:12|Bold,...   its groups (up to RC_MAXGROUP) and their commands
     qat=3|Save,8|Undo                      the Quick Access Toolbar (none: Quill's defaults) */
#define RC_MAXHIDE 64
#define RC_MAXGROUP 6
#define RC_MAXCMD 24
#define RC_MAXQAT 16
typedef struct { char name[40]; CmdRef cmd[RC_MAXCMD]; int n; } RcGroup;
typedef struct {
  char hide[RC_MAXHIDE][64]; int nhide;
  char tabname[40];
  RcGroup group[RC_MAXGROUP]; int ngroup;
  CmdRef qat[RC_MAXQAT]; int nqat, qatset;     /* qatset 0: the default toolbar */
} RibbonCustom;
void rcustom_read(RibbonCustom *rc, const PrefStore *ps);
int  rcustom_write(const RibbonCustom *rc, PrefStore *ps);       /* 0 out of memory */
int  rcustom_hidden(const RibbonCustom *rc, const char *tab, const char *group);   /* group NULL: the tab */
void rcustom_set_hidden(RibbonCustom *rc, const char *tab, const char *group, int hide);
void rcustom_clean_name(char *s);            /* a group's or tab's name made safe for the lines (: ; , | = become spaces) */

#endif
