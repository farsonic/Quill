/*
 * hyph.h - automatic hyphenation with Liang's patterns (TeX's method), read from a Hyphen .dic file
 * (as LibreOffice ships them: the character set on the first line, then one pattern a line, digits
 * between the letters; the higher digit wins and odd ones allow a break). Layout asks it for a word
 * too long for the end of a line. Patterns are kept as the file's text (made UTF-8), found through a
 * hash table of their letters.
 *
 * (WP31) One set of patterns for each language: the default set (hyph_load, British English on the
 * Amiga) is for text without a language and English text without a set of its own; a loader gives the
 * others when text in their language is first hyphenated (hyph_set_loader). Letters outside ASCII
 * (German, French, Spanish, Italian, Portuguese, Dutch...) are matched as UTF-8.
 */
#ifndef QUILL_HYPH_H
#define QUILL_HYPH_H

#include "util.h"

int  hyph_load(const char *path);                 /* the default set; 0 if it couldn't be read */
int  hyph_load_mem(const char *data, size_t n);   /* the default set from a copy of data */
void hyph_free(void);                             /* forget every set */
int  hyph_ready(void);                            /* are patterns loaded (or can a loader give some)? */
/* where a word may break: brk[i] = 1 allows a hyphen before byte i (n + 1 entries). The word is
   UTF-8, letters (any script) and apostrophes. Returns how many breaks. hyph_word uses the default
   set; hyph_word_lang the set for that language (proflang.h number; 0: the default) */
int  hyph_word(const char *w, u32 n, u8 *brk);
int  hyph_word_lang(u16 lang, const char *w, u32 n, u8 *brk);

/* (WP31) the other languages' sets: the loader gives a file's contents (malloc'd; the caller frees
   them) for a language, NULL if there is none; asked once per language */
typedef char *(*HyphLoader)(u16 lang, size_t *n);
void hyph_set_loader(HyphLoader fn);
int  hyph_load_lang(u16 lang, const char *data, size_t n);   /* a language's set from memory */

#endif
