/*
 * autocorrect.h - WP8: AutoCorrect and AutoFormat, as Word does them while typing and with AutoFormat
 * Now. AutoCorrect: a list of replacements ("teh" -> "the", "(c)" -> the copyright sign), TWo INitial
 * CApitals, the first letter of sentences, names of days, and words typed with Caps Lock on by
 * mistake ("hELLO"), with exceptions (abbreviations after which no capital follows, words with two
 * capitals that are meant). AutoFormat as you type: "straight" quotes made “smart”, dashes, 1/2 as ½,
 * 1st with a superscript st, "* " and "1. " starting bulleted and numbered lists, a line of ---, ===
 * or ___ becoming a border, addresses becoming links, and *bold* and _italic_.
 *
 * Written from Word's documented behaviour; the lists are Quill's own.
 */
#ifndef QUILL_AUTOCORRECT_H
#define QUILL_AUTOCORRECT_H

#include "doc.h"

enum {
  AC_TWOCAPS = 1, AC_SENTENCE = 2, AC_DAYS = 4, AC_CAPSLOCK = 8, AC_REPLACE = 16, AC_CELLS = 32,
  AF_QUOTES = 0x100, AF_DASHES = 0x200, AF_FRACTIONS = 0x400, AF_ORDINALS = 0x800, AF_BULLETS = 0x1000,
  AF_NUMBERS = 0x2000, AF_BORDERS = 0x4000, AF_LINKS = 0x8000, AF_BOLDITALIC = 0x10000,
  AC_ALL = 0x1FF3F
};
typedef struct {
  unsigned opts;                       /* AC_ and AF_ */
  char **from, **to; int n, cap;       /* the replacements (UTF-8), sorted by from */
  char **exc; int nexc, exccap;        /* first-letter exceptions ("e.g.", "etc."): no capital after them */
  char **exc2; int nexc2, exc2cap;     /* INitial CAps exceptions ("IDs") */
} AutoCorrect;

void ac_init(AutoCorrect *ac);         /* every option on, Quill's lists */
void ac_free(AutoCorrect *ac);
int  ac_add(AutoCorrect *ac, const char *from, const char *to);   /* added or changed; 0 out of memory */
int  ac_remove(AutoCorrect *ac, const char *from);                /* 0 if it wasn't there */
int  ac_find(const AutoCorrect *ac, const char *from);            /* its index, -1 if none (case as typed) */
int  ac_exc_add(AutoCorrect *ac, int which, const char *word);    /* which 0: first letter, 1: initial caps */
int  ac_exc_remove(AutoCorrect *ac, int which, const char *word);
/* the settings and lists as text (a file of Quill's), and read back (replacing what ac had) */
void ac_save(const AutoCorrect *ac, Buf *out);
int  ac_load(AutoCorrect *ac, const char *text, size_t n);

/* text (typed, n bytes) was just typed before *caret in d (a Return: "\n", the caret at the new
   paragraph's start): AutoCorrect and AutoFormat what it finished, as one undo step of its own. rid:
   tracked changes are on, by that author (Doc.revs index), or -1. *caret is kept on the same text.
   Returns the AC_/AF_ bit of what was done, 0 if nothing */
unsigned ac_typed(AutoCorrect *ac, Doc *d, Pos *caret, const char *typed, u32 n, int rid);
/* Format > AutoFormat Now: paragraphs pa..pb formatted as typing would have (quotes, dashes, fractions,
   ordinals, lists, borders, links, bold and italic), one undo step; how many changes */
int  ac_format_now(AutoCorrect *ac, Doc *d, u32 pa, u32 pb);

#endif
