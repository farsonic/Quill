/* resdirs.h - the drawers Quill takes its shared files from, found once at startup (main.c,
   core/respath.h): the screen fonts (PROGDIR:Fonts, Bureau:Fonts or FONTS:), the TrueType fonts
   (PROGDIR:TrueType or Bureau:TrueType) and the dictionaries, hyphenation patterns and thesauruses
   (PROGDIR:Dict or Bureau:Dict). Quill's own files (prefs, caches, the user's words) stay in PROGDIR: */
#ifndef QUILL_RESDIRS_H
#define QUILL_RESDIRS_H

#include <stddef.h>
#include "../core/respath.h"

extern const char *dir_fonts, *dir_truetype, *dir_dict;   /* never NULL once found */
void resdirs_find(void);
/* name in the drawer (dir_fonts, dir_truetype or dir_dict) into out: out ("" if it didn't fit) */
char *res_file(char *out, size_t n, const char *dir, const char *name);

#endif
