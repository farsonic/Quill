/*
 * wp_math.h - WP14a Equations: what the page view calls to draw an equation (amiga/wp_math.c).
 */
#ifndef QUILL_WP_MATH_H
#define QUILL_WP_MATH_H

#include <graphics/rastport.h>
#include "../core/layout.h"

/* equation box b (t: its paragraph's text) of layout L with its origin at pixel x on baseline base,
   at zoom (the view's: pixels = twips * zoom / 2000); pen(rgb) gives the pen for a colour
   (COLOR_AUTO: the text's). Expects JAM1 */
void wp14a_draw(struct RastPort *rp, Layout *L, const Box *b, const char *t, LONG x, LONG base, LONG zoom, LONG (*pen)(ULONG rgb));

#endif
