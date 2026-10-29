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
  u8 valign_set, valign;    /* WP21b: the cell's vertical alignment */
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

/* ---- WP21a: the table style editor (New, Modify and Delete Table Style) */
/* a new style s (k 0; its id made from its name, unique in d and the gallery) or style number k
   changed to s (its id kept): every table in style k is formatted afresh, one undo step (caret: where
   the edit is). Its number, 0 if out of room or k isn't a style */
int  tstyle_define(Doc *d, int k, const TableStyle *s, Pos caret);
/* style k deleted: tables in it keep how they look but have no style, styles based on it take its
   base. One undo step; 0 for Table Grid and Table Normal (or no such style) */
int  tstyle_delete(Doc *d, int k, Pos caret);
/* how many tables (top-level and nested) are in style k */
int  tstyle_uses(Doc *d, int k);
/* a new style's starting point: based on based (0: none), named name, nothing of its own */
void tstyle_blank(TableStyle *s, const Doc *d, int based, const char *name);

#endif
