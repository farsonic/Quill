/*
 * wp_draw.h - WP20 (the Draw tab): what wp_draw.c (the tools, pens and commands) and wp_draw_view.h (its
 * part of the page view, compiled into view.c: drawing with the mouse, rubbing out, the lasso, Ink
 * Replay) tell each other. This file belongs to WP20.
 */
#ifndef QUILL_WP_DRAW_H
#define QUILL_WP_DRAW_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include "../core/doc.h"

/* what dragging on the page does */
enum { DT_NONE, DT_PEN, DT_LASSO, DT_ERASER, DT_ERASEPT };
typedef struct { u8 kind; u32 color; u8 width; } DrawPen;    /* INK_ kind, 0xRRGGBB, eighths of a point */

/* wp_draw.c's: the tool in use, the pen it draws with, Ink to Shape on, ink hidden */
int  wp20_tool(void);
const DrawPen *wp20_pen(void);
int  wp20_inkshape(void);
int  wp20_hidden(void);
void wp20_set_tool(int tool);                /* (Esc: back to selecting) */

/* the view's (wp_draw_view.h, in view.c): the objects chosen (the lasso's, or the one selected) as
   positions of their references; ink replayed; the chosen ones forgotten */
int  dv_chosen(Object *view, Pos *out, int max);
void dv_replay(Object *view);
void dv_unchoose(Object *view);

#endif
