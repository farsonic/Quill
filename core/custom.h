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

#endif
