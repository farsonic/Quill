/*
 * wp_tables.h - what WP21a (wp_tables.c: table styles, quick tables, the Border Painter) and WP21b
 * (wp_tables2.c: drawing tables, cell options) share with each other and with view.c and main.c.
 */
#ifndef QUILL_WP_TABLES_H
#define QUILL_WP_TABLES_H

#include "../core/layout.h"

/* the commands ribbon.c's Insert > Tables group gives (fixed ids in WP21a's and WP21b's blocks), and
   their icons (theme.c's icon language) */
#define WP21_ID_DRAWTABLE   (ID_WP21B_FIRST + 0)     /* Draw Table (a table tool) */
#define WP21_ID_QUICKTABLES (ID_WP21A_FIRST + 60)    /* Quick Tables: the window to choose one */
#define WP21_ID_SPREADSHEET (ID_WP21A_FIRST + 61)    /* a table from a CSV file */
#define WP21_IC_DRAWTABLE "cd R 0 0 9 9 L 0 4 9 4 L 4 0 4 9 ci P 5 4 13 12 5 14 7 6 15 4 13 L 4 13 3 15 L 3 15 6 15 ca L 11 6 13 8"
#define WP21_IC_QUICK "ci R 1 1 14 14 ca F 2 2 13 4 cd L 1 7 14 7 L 1 10 14 10 L 5 4 5 14 L 10 4 10 14"
#define WP21_IC_SHEET "cg R 1 1 14 14 F 2 2 13 4 ci L 1 7 14 7 L 1 10 14 10 L 5 4 5 14 L 10 4 10 14"

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
