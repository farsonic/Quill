#ifndef QUILL_SYSFONTS_H
#define QUILL_SYSFONTS_H
#include "../core/fonts.h"
/* the fonts installed on the system (sysfonts.c): TrueType, OpenType (CFF) and collections (.ttc),
   those with a .otag in FONTS: (as FTManager, ttf.library's tools or AATextManager install them)
   and the files in FONTS:_ttf, _otf and _ttc. Adds each face to fs as an installed one; families
   Quill ships are kept as Quill's. What each face is gets remembered in PROGDIR:Quill.fontcache,
   so later starts open only new or changed files. step(done, total) is told how far it has got.
   Returns how many faces were added */
int sysfonts_scan(FontSet *fs, void (*step)(int done, int total));
#endif
