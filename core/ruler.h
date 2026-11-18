/*
 * ruler.h - the ruler's sums, without drawing: what a front end's ruler shows for the caret's
 * paragraph, which marker a click is on, where a dragged marker goes, and the change made when it
 * is let go. The Amiga's ruler (amiga/ruler.c) worked these out inside its MUI class in pixels; the
 * Mac's (mac/qm_ruler.c) needed the same rules, so they are here once, in twips, and tested natively
 * (tests/test_wpmac.c). A front end only turns pixels into twips and back, and draws.
 *
 * The horizontal ruler shows the caret's page: its margins (shaded), the text area the paragraph is
 * in (the page's, a section's column, or a table cell's), the paragraph's first-line, left and right
 * indents and its tab stops, and in a table the column edges. The vertical ruler shows the same
 * page's top and bottom margins. Everything is measured from the page's left (or top) edge.
 *
 * Dragging follows Word, as the Amiga's ruler does: a marker snaps to an eighth of an inch (or a
 * quarter of a centimetre); the left indent keeps the first line where it is (the hanging marker
 * moves alone); a tab stop dragged off the ruler goes; a column edge stays a sixth of an inch from
 * its neighbours. The change is one undoable edit: the indents or tab stops of the paragraphs
 * selected (doc_para_format), or the table's column widths (tbl_set_table).
 */
#ifndef QUILL_RULER_H
#define QUILL_RULER_H

#include "doc.h"
#include "layout.h"

#define RULER_TABS 8              /* tab stops a paragraph has (ParaFmt.tabs) */
#define RULER_COLS 64             /* column edges shown: a table's columns, as the Amiga's ruler */
#define RULER_MINCOL 240          /* a column at least a sixth of an inch wide while its edge is dragged */

typedef struct {
  u32 page;                       /* the caret's page */
  i32 pagew, pageh;               /* its size */
  i32 mleft, mright, mtop, mbottom;   /* its margins */
  i32 x0, width;                  /* the text area the paragraph is in: its left edge from the page's, its width */
  i32 left, first, right;         /* the paragraph's indents (first: from left, negative hanging) */
  i32 tabs[RULER_TABS];           /* its tab stops from the left indent, 0 ends the list; -1 a stop being dragged off */
  u8 tabkind[RULER_TABS], tablead[RULER_TABS];
  int ncolx;                      /* in a table: its column edges (0 none), twips from tblx0 */
  i32 colx[RULER_COLS + 1];
  i32 tblx0;                      /* the table's left edge from the page's */
  u16 table;
  u8 metric;                      /* centimetres, else inches */
} RulerGeom;

/* the parts of the ruler a click can take hold of: ruler_hit's answer and ruler_drag's part */
enum { RP_NONE = 0, RP_FIRST = 1, RP_LEFT = 2, RP_RIGHT = 3, RP_TAB = 10, RP_COL = 100 };

/* The ruler for the caret at p, from L (laid out at least as far as p's paragraph). metric: 1
   centimetres, 0 inches, -1 as the page suggests (anything but US Letter is metric, as on the Amiga) */
void ruler_geom(Doc *d, Layout *L, Pos p, int metric, RulerGeom *g);
/* the scale: twips from one number to the next (an inch or a centimetre), and the ticks between */
void ruler_unit(int metric, i32 *unit, int *nsub);
/* t rounded to the nearest ruler step, as a dragged marker snaps */
i32  ruler_snap(i32 t, int metric);
/* The part under x (twips from the page's left edge): top says the pointer is in the ruler's upper
   half (where the first-line marker is); slop is how near counts (six pixels' worth of twips: tab
   stops take two thirds of it, column edges half). RP_NONE if nothing is there. */
int  ruler_hit(const RulerGeom *g, i32 x, int top, i32 slop);
/* A new tab stop of kind (TAB_) at x, if x is inside the text area and the paragraph has room for
   one more: the part to drag (RP_TAB + k), else RP_NONE */
int  ruler_add_tab(RulerGeom *g, i32 x, int kind);
/* part dragged to x (twips from the page's left edge), snapped; off: the pointer has left the ruler
   (a tab stop then goes) */
void ruler_drag(RulerGeom *g, int part, i32 x, int off);
/* The drag of part finished: the change made to the document as one undoable edit, on paragraphs
   pa..pb (the selection's), or on the caret's table (*caret may move, as tbl_set_table moves it).
   1 if the document changed. */
int  ruler_apply(Doc *d, u32 pa, u32 pb, Pos *caret, const RulerGeom *g, int part);

#endif
