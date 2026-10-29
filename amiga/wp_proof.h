/*
 * wp_proof.h - what WP8 (amiga/wp_proof.c: grammar, thesaurus, AutoCorrect, AutoFormat, Read Aloud) and
 * WP31 (amiga/wp_proflang.c: proofing languages) give Quill's own files, each called from one place.
 * These files belong to WP8 and WP31.
 */
#ifndef QUILL_WP_PROOF_H
#define QUILL_WP_PROOF_H

#include "../core/doc.h"
#include "../core/grammar.h"

/* view.c: the grammar issues to mark in paragraph p as you type (kept for each paragraph version);
   how many (0 when grammar isn't checked as you type) */
int  wp8_issues(Para *p, const GramIssue **out);
/* view.c: text was typed before *caret (rid: tracked changes' author, or -1): AutoCorrect and
   AutoFormat as you type */
void wp8_typed(Doc *d, Pos *caret, const char *s, u32 n, int rid);
/* main.c: Review > Spelling & Grammar (and F7) */
void wp8_spelling(void);
/* main.c's: the built-in dictionary (Spell *), NULL if none was loaded */
void *wp_spell(void);

/* panes.c: the language at the caret, for the status bar ("" none), and the command a click on it runs */
const char *wp31_status_lang(void);
#define WP31_LANGWIN_ID ID_WP31_FIRST

#endif
