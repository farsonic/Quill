/*
 * tbldraw.h - WP21b: drawing tables with the mouse (Draw Table and Eraser) and painting their
 * borders (WP21a's Border Painter), from strokes on a laid out page.
 *
 * A stroke goes from (x0, y0) to (x1, y1), twips from the top left corner of the page it was made on
 * (as CellBox's). Each call is one undo step and returns 1 if the document changed (the layout is then
 * out of date), with the caret in the cell it changed; 0 if the stroke meant nothing there.
 */
#ifndef QUILL_TBLDRAW_H
#define QUILL_TBLDRAW_H

#include "layout.h"

/* Draw Table: a box drawn outside tables makes a one-cell table of its size there; a line across or
   down cells splits them (table.h's tbl_hsplit, tbl_vsplit), drawn in the pen (style PB_, width in
   eighths of a point, colour; 0, 0: a half-point single line) as Word draws it */
int tbl_draw_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, u8 style, u8 width, u32 color, Pos *caret);
/* the Eraser: the edge between two cells nearest the stroke goes (the cells merged); an outside edge
   loses its line */
int tbl_erase_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, Pos *caret);
/* the Border Painter: a click puts the pen on the nearest cell edge, a stroke on every edge along it
   (both cells' sides of an inside edge) */
int tbl_paint_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, u8 style, u8 width, u32 color, Pos *caret);

#endif
