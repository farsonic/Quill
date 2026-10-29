/*
 * proflang.h - WP31 proofing languages: the language a run of text is in (CharFmt.lang, Word's w:lang),
 * text marked "do not check spelling or grammar" (CharFmt.noproof, w:noProof), and the dictionary for
 * each language (Quill's own British English list, or an installed Hunspell .aff/.dic pair, loaded
 * when text in that language is first checked).
 *
 * A language is a small number (1 up) standing for its tag ("en-GB", "de-DE"); 0 means "not set" (the
 * text is in the proofing language Quill is set to, plang_default). Word's common languages are in a
 * fixed list in this order (the Language window shows them); any other tag read from a file is added
 * after them for the rest of the session, so it is written back as it was.
 */
#ifndef QUILL_PROFLANG_H
#define QUILL_PROFLANG_H

#include "doc.h"
#include "spell.h"

/* the CM_ bits of the two fields (in CM_LOOK and CM_ALL, so styles, the format painter and clearing
   formatting treat them as Word does) */
#define CM_LANG    0x04000000U
#define CM_NOPROOF 0x08000000U

u16  plang_find(const char *tag);       /* the number for a tag (case and '_' or '-' alike), added if new; 0 for NULL or "" */
u16  plang_known(const char *tag);      /* the same, but 0 if it isn't one already known */
const char *plang_tag(u16 id);          /* "" for 0 or a bad number */
const char *plang_name(u16 id);         /* English name, "English (United Kingdom)"; the tag for an added one */
int  plang_nlisted(void);               /* how many the fixed list has (numbers 1 .. that) */
int  plang_primary(u16 id, const char *two);   /* is it that language ("en", "de")? */
u16  plang_lcid(u16 id);                /* Windows' number for it (0x0809), 0 if not known */
extern u16 plang_default;               /* the language of text without one (Quill's setting): en-GB at first */
u16  plang_of(const CharFmt *f);        /* f's language, or plang_default */

/* spelling and grammar as you type (Quill's settings; the Spelling & Grammar window checks anyway) */
extern int proof_live_spelling, proof_live_grammar;

/* the dictionaries: a loader gives a language's Spell (from files on the Amiga, from memory in tests);
   plang_spell asks it once per language and keeps the answer. plang_set_spell gives one directly (not
   freed by Quill: the built-in list). plang_forget frees what the loader made and asks again later */
typedef int (*PlangLoader)(u16 id, Spell *s);      /* 1 with s filled, 0 if there is none */
void   plang_set_loader(PlangLoader fn);
Spell *plang_spell(u16 id);
void   plang_set_spell(u16 id, Spell *s);
void   plang_forget(void);
/* the dictionary to check text in f with: none for text marked not to be checked; its language's; for
   English without one of its own (or no language), deflt (Quill's British list); else none */
Spell *proof_spell(Spell *deflt, const CharFmt *f);
/* the same for the text at off in paragraph p (as live checking would), and whether spelling is checked
   there at all when typing (proof_live_spelling) */
Spell *proof_spell_at(Spell *deflt, Para *p, u32 off);

/* the language and "do not check" of a range set (one undo step; a..b empty: nothing) */
int doc_set_lang(Doc *d, Pos a, Pos b, u16 lang, int noproof);

#endif
