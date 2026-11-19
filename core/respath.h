/* respath.h - where a program finds the fonts and dictionaries it shares with the rest of the Bureau
   suite (Quill, Ledger, Easel, Folio, Dispatch). Portable C99, no AmigaOS calls; the same file in
   every program of the suite. For each kind, the first drawer that exists wins:

     RES_FONTS     PROGDIR:Fonts     Bureau:Fonts     FONTS:
     RES_TRUETYPE  PROGDIR:TrueType  Bureau:TrueType
     RES_DICT      PROGDIR:Dict      Bureau:Dict

   So a program unpacked with its own drawers beside it uses those, and one installed as part of the
   suite uses Bureau:'s (and the screen fonts in FONTS:). */
#ifndef RESPATH_H
#define RESPATH_H

#include <stddef.h>

enum { RES_FONTS, RES_TRUETYPE, RES_DICT };

/* dir and name joined into out (n bytes) as an Amiga path: no '/' after a dir ending in ':' or '/'
   ("FONTS:" + "x.font" is "FONTS:x.font"). 1 if it fit; 0 if not, and out is then "" */
int res_join(char *out, size_t n, const char *dir, const char *name);

/* the first drawer of kind's row for which exists(dir, ud) is nonzero, or NULL if none; for
   RES_FONTS the last one, "FONTS:", is taken without asking */
const char *res_pick(int kind, int (*exists)(const char *dir, void *ud), void *ud);

#endif
