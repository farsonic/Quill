/* wp_controls.h - what WP16 (content controls, amiga/wp_controls.c) gives the view: the command a click
   at p runs (a check box toggled, a list or the calendar opened, a picture chosen), 0 for none */
#ifndef QUILL_WP_CONTROLS_H
#define QUILL_WP_CONTROLS_H
#include "../core/doc.h"
ULONG wp16_click(Doc *d, Pos p);
/* the Developer tab: hidden until turned on (View > Developer Tab, File > Options), as in Word; the
   choice is remembered (ENVARC:Quill/DeveloperTab). The ribbon shows the tab while WP16's state bit
   WP16_ST_DEVTAB is on */
#define WP16_ST_DEVTAB 6
int  wp16_devtab_on(void);
void wp16_devtab_set(int on);
#endif
