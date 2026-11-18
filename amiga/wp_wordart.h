/*
 * wp_wordart.h - what view.c asks of WP19 (amiga/wp_wordart.c): text with WordArt effects drawn from
 * its outlines (core/wordart.c's raster, dithered to the view's colour cube), and a text box's text
 * bent by its Transform. This file belongs to WP19.
 */
#ifndef QUILL_WP_WORDART_H
#define QUILL_WP_WORDART_H

#include <graphics/rastport.h>
#include "../core/layout.h"

/* box b (its text in t) at x on baseline base (pixels), the view at zoom: drawn with its WordArt
   effects; 0 if it has none or couldn't be drawn (then it is drawn as before) */
int  wp19_draw_run(struct RastPort *rp, Layout *L, const Box *b, const char *t, LONG x, LONG base, LONG zoom,
                   const LONG *cube, int lv, LONG band0, LONG band1);
/* src's text (its object t as placed: size and turn; centre cx, cy twips on the page whose top left
   is at pl, pt pixels) bent by its Transform; 0 if it has none (the text is then drawn as before) */
int  wp19_draw_warped(struct RastPort *rp, Layout *L, const Image *src, const Image *t, i32 cx, i32 cy, LONG pl, LONG pt, LONG zoom,
                      const LONG *cube, int lv, LONG band0, LONG band1);
void wp19_flush(void);                   /* the drawings kept for the screen forgotten (pens changed) */

#endif
