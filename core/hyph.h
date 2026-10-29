/*
 * hyph.h - automatic hyphenation with Liang's patterns (TeX's method), read from a Hyphen .dic file
 * (as LibreOffice ships them: one pattern a line, digits between the letters; the higher digit wins
 * and odd ones allow a break). Layout asks it for a word too long for the end of a line.
 * Patterns are kept as the file's text, found through a hash table of their letters.
 */
#ifndef QUILL_HYPH_H
#define QUILL_HYPH_H

#include "util.h"

int  hyph_load(const char *path);                 /* 0 if it couldn't be read */
int  hyph_load_mem(const char *data, size_t n);   /* the patterns from a copy of data */
void hyph_free(void);                             /* forget the patterns */
int  hyph_ready(void);                            /* are patterns loaded? */
/* where a word may break: brk[i] = 1 allows a hyphen before byte i (n + 1 entries). The word is
   UTF-8; only one of ASCII letters (and apostrophes) is hyphenated. Returns how many breaks */
int  hyph_word(const char *w, u32 n, u8 *brk);

#endif
