/*
 * thesaurus.h - WP8: synonyms from a thesaurus in MyThes's .dat format (as LibreOffice ships them: the
 * character set on the first line, then for each word "word|meanings" followed by that many lines
 * "(part of speech)|synonym|synonym..."), the words in sorted order. Quill ships an English one made
 * from WordNet (dict/th_en.dat; see dict/README_thesaurus.txt); an installed th_xx_YY.dat works the
 * same. Looked up by binary search in the file (or in memory), so only the entry is read.
 */
#ifndef QUILL_THESAURUS_H
#define QUILL_THESAURUS_H

#include "util.h"

typedef struct Thes Thes;
#define THES_MEANINGS 8
#define THES_TERMS 12
typedef struct {
  char head[48];                                 /* the word found (perhaps the word's base form) */
  int n;                                         /* meanings */
  struct { char pos[16]; int nterms; char *terms[THES_TERMS]; } m[THES_MEANINGS];
  char *buf;                                     /* (holds the strings) */
} ThesResult;

Thes *thes_open(const char *path);               /* NULL if it can't be read */
Thes *thes_open_mem(const char *data, size_t n); /* from a copy of data */
void  thes_close(Thes *t);                       /* NULL is fine */
/* the word's meanings and synonyms (UTF-8, case ignored); a plural or -ed/-ing form is looked up as
   its base form when it isn't there itself. 0 if not found (r empty) */
int   thes_lookup(Thes *t, const char *word, u32 n, ThesResult *r);
void  thes_result_free(ThesResult *r);

#endif
