/*
 * wp_tables.h - what WP21a (wp_tables.c: table styles, quick tables, the Border Painter) and WP21b
 * (wp_tables2.c: drawing tables, cell options) share with each other and with view.c and main.c.
 */
#ifndef QUILL_WP_TABLES_H
#define QUILL_WP_TABLES_H

#include "../core/layout.h"

/* the table tool the mouse is (WP21_ tools; 0 none): view.c lets a click and drag on the page make a
   stroke for it instead of selecting */
enum { WP21_NONE, WP21_DRAW, WP21_ERASE, WP21_PAINT };
int  wp21_tool(void);
void wp21_set_tool(int tool);             /* (the same tool again: off) */
/* a stroke on page from (x0, y0) to (x1, y1) (twips from its top left) done with the tool: 1 if the
   document changed (the caret is then where it changed it) */
int  wp21_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, Pos *caret);
/* main.c: the borders' pen the Table Design tab's Borders group sets (style, width, colour) */
const struct QBorders *wp_pen(void);
/* lengths in the dialogs: twips as "2.54 cm" or "1.00\"" (as the page suggests), and back (0 for
   nothing readable) */
void wp21_len_text(i32 tw, char *out);
i32  wp21_len_parse(const char *s);
/* what the hooks last heard of the caret and the selection's other end, and whether in a table */
extern Pos wp21_caret, wp21_anchor;
extern int wp21_intable;
/* the view laid out afresh after a package changed the document, the caret (and selection) at at */
void wp21_changed(Pos at);

#endif
