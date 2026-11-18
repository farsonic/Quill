/*
 * testutil.h - what the native tests share (tests/coretest.c defines it): the check that counts
 * and reports, and helpers for documents. Each work package has its own test file,
 * tests/test_wp<n>.c, with its checks in test_wp<n>() (coretest.c's main() calls them all, and
 * build.sh compiles every tests/test_wp*.c): put yours there, not in coretest.c.
 */
#ifndef QUILL_TESTUTIL_H
#define QUILL_TESTUTIL_H

#include "../core/doc.h"
#include "../core/docx.h"
#include "../core/fonts.h"

extern int fails, total;
void  ok(const char *what, int cond);        /* one check: counted, "FAIL what" printed if cond is 0 */
Pos   P(u32 para, u32 off);                  /* a position */
char *all(Doc *d);                           /* the whole text (malloc'd) */
int   text_is(Doc *d, const char *want);     /* the whole text is want (else it is printed) */
int   runs_sane(Doc *d);                     /* the runs cover each paragraph's text, none empty or mergeable */
int   same_doc(Doc *a, Doc *b);              /* the same text, paragraph, cell, section and character formatting */
Doc  *docx_again(Doc *d, unsigned *lost);    /* d saved as .docx and read back (NULL if that failed) */
extern FontSet fs;                           /* the test fonts (tests/data), loaded before any test runs */

/* the work packages' tests, each in its own tests/test_wp<n>.c */
void test_wp0(void);
void test_wp1(void), test_wp2(void), test_wp3(void), test_wp4(void), test_wp5(void), test_wp6(void), test_wp7(void),
     test_wp8(void), test_wp9(void), test_wp10(void), test_wp11(void), test_wp12(void), test_wp13(void),
     test_wp14a(void), test_wp14b(void), test_wp15a(void), test_wp15b(void), test_wp15c(void), test_wp16(void),
     test_wp17(void), test_wp18(void), test_wp19(void), test_wp20(void), test_wp21a(void), test_wp21b(void),
     test_wp22a(void), test_wp22b(void), test_wp23(void), test_wp24(void), test_wp25(void), test_wp26(void),
     test_wp27(void), test_wp28a(void), test_wp28b(void), test_wp28c(void), test_wp28d(void), test_wp28e(void), test_wp29(void),
     test_wp30(void), test_wp31(void);
void test_wpmac(void);                       /* the classic Mac OS port's (tests/test_wpmac.c) */

#endif
