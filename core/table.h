/*
 * table.h - table editing: inserting tables, rows and columns, deleting them, moving between cells.
 *
 * A table is a run of paragraphs that share Cell.table (see doc.h). Each operation rebuilds the
 * table's paragraphs and puts them in with doc_replace, so it is one undo step. Column operations
 * make a new Table (the grid changes); old ones stay for undo.
 */
#ifndef QUILL_TABLE_H
#define QUILL_TABLE_H

#include "doc.h"

u32 tbl_first(Doc *d, u32 pi);          /* the first paragraph of pi's table */
u32 tbl_end(Doc *d, u32 pi);            /* one past its last */
u32 cell_first(Doc *d, u32 pi);         /* the first paragraph of pi's cell */
u32 cell_end(Doc *d, u32 pi);           /* one past its last */

/* a rows x cols table at *at, cols of equal width across width twips; the caret goes into the
   first cell. 0 inside a table (no nested tables) or out of memory */
int tbl_insert(Doc *d, Pos *at, int rows, int cols, i32 width);
/* The operations below work on the innermost table at the caret, return 1 if they changed the
   document (0: not in a table, nothing to do, or out of memory) and usually move the caret. */
/* an empty row above (below) the caret's row, laid out like it; the caret goes into it */
int tbl_row_insert(Doc *d, Pos *caret, int below);
/* the caret's row out (the last row left: the whole table) */
int tbl_row_delete(Doc *d, Pos *caret);
/* a new grid column left (right) of the caret's cell; the table keeps its width */
int tbl_col_insert(Doc *d, Pos *caret, int right);
/* the grid column the caret's cell starts in, out (the last column: the whole table) */
int tbl_col_delete(Doc *d, Pos *caret);
/* the caret's whole table out; the caret goes to what followed it */
int tbl_delete(Doc *d, Pos *caret);
/* merge the block of cells from a's to b's (any rows and columns) into one: the top left keeps the
   text of them all. 0 unless that's at least two cells */
int tbl_merge(Doc *d, Pos a, Pos b);
/* split a merged cell back into the cells it covered */
int tbl_split(Doc *d, Pos *caret);
/* split the caret's cell into cols x rows cells (a merged one comes apart first); one undo step */
int tbl_split_cells(Doc *d, Pos *caret, int cols, int rows);
/* split the table above the caret's row (an empty paragraph between); at the first row, a paragraph
   goes in above the table */
int tbl_split_table(Doc *d, Pos *caret);
/* lines on the block of cells a..b: which (TB_), in this kind (PB_; PB_NONE takes lines away), width
   (eighths of a point) and colour */
enum { TB_ALL, TB_NONE, TB_OUTSIDE, TB_INSIDE, TB_INSIDE_H, TB_INSIDE_V, TB_TOP_ONLY, TB_BOTTOM_ONLY, TB_LEFT_ONLY, TB_RIGHT_ONLY };
int tbl_borders(Doc *d, Pos a, Pos b, int which, u8 style, u8 width, u32 color);
/* the cells a..b touch: their vertical alignment (TF_VALIGN: 0 top, 1 centre, 2 bottom) or fill
   (TF_SHADE: 0xRRGGBB or COLOR_AUTO); or the rows': height (TF_ROWH: Cell.rowh as (u32)(i32)), heading
   rows (TF_HEADER: 0 or 1), rows that mustn't break (TF_CANTSPLIT) */
enum { TF_VALIGN, TF_SHADE, TF_ROWH, TF_HEADER, TF_CANTSPLIT };
int tbl_cell_format(Doc *d, Pos a, Pos b, int what, u32 value);
/* table style ts (Doc.tstyles number, 0: none) with these look flags (LK_) on the caret's table: its
   lines, fills and text formatting, one undo step */
int tbl_apply_style(Doc *d, Pos *caret, int ts, u16 look);
/* the caret's table with new table-wide properties (alignment, margins...: a copy of its Table changed) */
int tbl_set_table(Doc *d, Pos *caret, const Table *nt);
/* paragraphs pa..pb (none in a table) as a table: each one a row, its text split into cells at sep
   (a character such as '\t' or ','; 0: one cell each), width twips wide */
int tbl_from_text(Doc *d, u32 pa, u32 pb, int sep, i32 width, Pos *caret);
/* the caret's table as text: a paragraph per row, the cells' text separated by sep ('\t' if 0) */
int tbl_to_text(Doc *d, Pos *caret, int sep);
/* the rows sorted by grid column col's text (A to Z, or by number), heading rows left at the top;
   0 if merged cells hold rows together */
int tbl_sort(Doc *d, Pos *caret, int col, int descending, int numeric, int header);
/* a formula as Word's (=SUM(ABOVE), =AVERAGE(LEFT), =A1*B2, =MAX(A1:C3), COUNT, MIN, PRODUCT, ABS, INT,
   + - * / and brackets) worked out for the caret's cell: its result as text in out; 0 and an error
   message ("!Syntax Error") if it can't be */
int tbl_formula(Doc *d, Pos caret, const char *expr, char *out, size_t cap);
/* a selection from a to b over several cells of one table: the block of cells it makes (rows r0..r1,
   grid columns c0..c1, of table number *table); 0 if a and b aren't in different cells of one table */
int tbl_block(Doc *d, Pos a, Pos b, u16 *table, int *r0, int *r1, int *c0, int *c1);
/* the block's cells in order: each one's first paragraph and one past its last; how many (up to max) */
int tbl_block_cells(Doc *d, Pos a, Pos b, u32 *first, u32 *end, int max);
/* the start of the next (dir > 0) or previous cell; 0 if there is none */
int tbl_next_cell(Doc *d, Pos *caret, int dir);

/* ---- WP21b: cell options and drawing tables */
/* the paragraph of the cell at row, grid column col of in's table ((u32)-1 if none) */
u32 tbl_cell_para(Doc *d, Pos in, int row, int col);
/* the 9-way alignment of the block a..b's cells: vertical (0 top, 1 centre, 2 bottom) and their
   paragraphs' (AL_; -1 leaves it) */
int tbl_cell_align(Doc *d, Pos a, Pos b, int valign, int align);
/* the block's cells' text direction (TD_) */
int tbl_cell_dir(Doc *d, Pos a, Pos b, int dir);
/* the block's cells' own margins: set (1 << E_ edge) says which of mar[4] (E_ order) they have; the
   others are the table's again (set 0: all of them) */
int tbl_cell_mar(Doc *d, Pos a, Pos b, u8 set, const i16 *mar);
/* Delete Cells on the block a..b: shift the cells right of it left (the rows end short), shift the
   cells below up (empty cells come in at the bottom), or delete its whole rows or grid columns. The
   caret goes to about where the block was */
enum { DC_LEFT, DC_UP, DC_ROW, DC_COL };
int tbl_delete_cells(Doc *d, Pos a, Pos b, int how, Pos *caret);
/* lines on edges (E_) of cells by row and starting grid column, in in's table: style PB_NONE takes
   them away. 0 if none of them is a cell's */
typedef struct { u16 row; u8 col, edge; } TEdge;
int tbl_set_edges(Doc *d, Pos in, const TEdge *e, int n, u8 style, u8 width, u32 color);
/* a line drawn down through rows r0..r1 at x (twips from the table's left edge): the cells it
   crosses split there (the grid gains a column where x isn't on an edge of it); the edge, and an
   existing one drawn along, gets the pen's line (NULL or pen->set 0: lines as they were) */
int tbl_vsplit(Doc *d, Pos in, i32 x, int r0, int r1, const TBorder *pen);
/* a line drawn across row: its cells starting in grid columns cols[0..n) split into an upper and a
   lower cell (a new row below, the row's other cells merged down over it), the line between them in
   the pen (as tbl_vsplit) */
int tbl_hsplit(Doc *d, Pos in, int row, const u8 *cols, int n, const TBorder *pen);

#endif
