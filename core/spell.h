/*
 * spell.h - spelling: a word list (one word a line, UTF-8; SCOWL's British English ships with Quill),
 * the user's own words, words to ignore, and suggestions for a word not in the list (the words one
 * edit away: a letter left out, added, changed, or two swapped).
 */
#ifndef QUILL_SPELL_H
#define QUILL_SPELL_H

#include "doc.h"

typedef struct {
  char *text; u32 *word; u32 n;          /* the list, its words sorted (offsets into text) */
  char **extra; u32 nextra, extracap;    /* the user's words and ignored ones (added at run time) */
  struct Hun *hun;                       /* (WP31) a Hunspell dictionary instead of the list (hunspell.h), or NULL */
} Spell;

int  spell_load(Spell *s, const char *path);       /* 0 if the file can't be read (s stays empty) */
int  spell_load_mem(Spell *s, const char *text, size_t n);  /* from a copy of text; 0 if out of memory */
/* (WP31) a Hunspell dictionary: from the .aff and .dic files' contents, or from the two files; 0 if
   it couldn't be read (s stays empty). Checking and suggestions then go by its rules */
int  spell_load_hunspell(Spell *s, const char *aff, size_t an, const char *dic, size_t dn);
int  spell_load_hunspell_files(Spell *s, const char *affpath, const char *dicpath);
void spell_free(Spell *s);                         /* frees everything; s is left empty */
void spell_add(Spell *s, const char *word, u32 n); /* known from now on */
/* is this word (UTF-8, n bytes) spelt right? Words with digits, and capitals of a known word, are */
int  spell_ok(const Spell *s, const char *word, u32 n);
/* up to max suggestions for a word, best first, each malloc'd into out; returns how many */
int  spell_suggest(const Spell *s, const char *word, u32 n, char **out, int max);
/* the word at or after p in d's text that is not spelt right (from p, wrapping once); 0 if none */
int  spell_next(const Spell *s, Doc *d, Pos p, Pos *a, Pos *b);
/* the extent of the word (letters, inner apostrophes) in t at off..: start and end; 0 if none */
int  spell_word_at(const char *t, u32 len, u32 off, u32 *ws, u32 *we);

#endif
