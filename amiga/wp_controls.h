/* wp_controls.h - what WP16 (content controls, amiga/wp_controls.c) gives the view: the command a click
   at p runs (a check box toggled, a list or the calendar opened, a picture chosen), 0 for none */
#ifndef QUILL_WP_CONTROLS_H
#define QUILL_WP_CONTROLS_H
#include "../core/doc.h"
ULONG wp16_click(Doc *d, Pos p);
#endif
