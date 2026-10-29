/*
 * tstyle.h - table styles: Quill's own gallery, what a style gives each cell, and applying one.
 *
 * A style's parts (doc.h: TC_) are laid over each other in Word's order: the whole table, banded
 * columns, banded rows, first and last column, heading and total rows, then the corner cells.
 * Applying a style writes what it gives into the cells and their text, so the rest of Quill (layout,
 * drawing, saving) sees plain cell formatting; the table keeps the style's name and look for saving.
 */
#ifndef QUILL_TSTYLE_H
#define QUILL_TSTYLE_H

#include "doc.h"

/* what a style gives one cell */
typedef struct {
  u8 borders, bstyle[4], bwidth[4];
  u32 bcolor[4];
  u32 shade;
  unsigned cmask; CharFmt cf;
  u8 align_set, align;
} CellLook;

/* the gallery: how many styles, and style i (static, built on first use; NULL out of range) */
int  tstyle_builtin_count(void);
const TableStyle *tstyle_builtin(int i);
/* the cell at row, col (spanning span columns) of a nrows x ncols table, style number ts of d (0:
   none: no lines) with the look flags; direct: the table's own lines (TE_N of them) or NULL */
void tstyle_cell(Doc *d, int ts, u16 look, int row, int col, int span, int nrows, int ncols, const TBorder *direct, CellLook *out);
/* the same for a style by itself (not one it's based on): for pictures of styles */
void tstyle_preview(const TableStyle *s, u16 look, int row, int col, int nrows, int ncols, CellLook *out);
/* the character fields any part of style ts sets (CM_ flags) */
unsigned tstyle_cmask(Doc *d, int ts);
/* a built-in style into d (by its index in the gallery): its number, 0 if out of memory */
int  tstyle_use_builtin(Doc *d, int i);

#endif
