/*
 * hunspell.h - WP31: a reader for Hunspell dictionaries (an .aff file of affix rules and a .dic file of
 * stems with flags), as LibreOffice, Firefox and most Linux systems install them, so Quill checks
 * spelling in any language that has one. Written from Hunspell's documented file format (hunspell(5));
 * no Hunspell code.
 *
 * What it understands: SET (UTF-8, ISO8859-1/-15/-2, Windows-1250/1252), FLAG (single characters,
 * long, num, UTF-8), AF flag aliases, PFX and SFX rules with conditions, cross products of a prefix
 * and a suffix, two suffixes (a suffix whose continuation class allows another), NEEDAFFIX,
 * FORBIDDENWORD, KEEPCASE, ONLYINCOMPOUND, simple compounds (COMPOUNDFLAG, COMPOUNDBEGIN, COMPOUNDMIDDLE,
 * COMPOUNDEND, COMPOUNDMIN), TRY and REP (for suggestions) and IGNORE. Words are checked as Hunspell
 * does for case: as typed, then with the first letter small (Capitalised), then all small or
 * Capitalised (ALL CAPITALS). Morphological fields in the .dic are ignored.
 */
#ifndef QUILL_HUNSPELL_H
#define QUILL_HUNSPELL_H

#include "util.h"

typedef struct Hun Hun;

/* a dictionary from the two files' contents (copied); NULL if out of memory or no words */
Hun *hun_load(const char *aff, size_t an, const char *dic, size_t dn);
void hun_free(Hun *h);                  /* NULL is fine */
u32  hun_count(const Hun *h);           /* how many stems */
/* is the word (UTF-8, n bytes) right? */
int  hun_ok(const Hun *h, const char *w, u32 n);
/* up to max suggestions (malloc'd UTF-8 into out, best first); how many */
int  hun_suggest(const Hun *h, const char *w, u32 n, char **out, int max);

#endif
