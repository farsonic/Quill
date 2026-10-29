/*
 * wp_outline.h - WP11's command ids and its commands for the page view (they reach view.c's
 * MUIM_Quill_Command like its own CMD_s, in a number range of WP11's own, and are done by
 * amiga/wp_outline_view.h, which view.c includes). This file belongs to WP11.
 */
#ifndef QUILL_WP_OUTLINE_H
#define QUILL_WP_OUTLINE_H

#include "ids.h"

/* WP11's command ids (its block in ids.h) */
enum {
  ID_OL_VIEW = ID_WP11_FIRST,   /* View > Outline */
  ID_OL_TOH1, ID_OL_PROMOTE, ID_OL_LEVEL, ID_OL_DEMOTE, ID_OL_TOBODY, ID_OL_UP, ID_OL_DOWN, ID_OL_EXPAND, ID_OL_COLLAPSE,
  ID_OL_SHOWLEVEL, ID_OL_FORMAT, ID_OL_FIRSTLINE, ID_OL_CLOSE,
  ID_OL_SHOW1, ID_OL_SHOW2, ID_OL_SHOW3, ID_OL_SHOW4, ID_OL_SHOW5, ID_OL_SHOW6, ID_OL_SHOW7, ID_OL_SHOW8, ID_OL_SHOW9, ID_OL_SHOWALL,
  ID_OL_LAST_
};

#define CMD_OV_BASE 0x57110000UL
enum {
  CMD_OV_PROMOTE = CMD_OV_BASE, /* arg -1: the selected paragraphs promoted a level, 1 demoted (a collapsed heading's part with it) */
  CMD_OV_SETLEVEL,              /* arg 1-9: the selected paragraphs made Heading arg; 0 body text */
  CMD_OV_MOVE,                  /* arg -1: the selected paragraphs moved up (past the paragraph or part shown above), 1 down */
  CMD_OV_EXPAND,                /* arg 1: the caret's heading expanded a level, 0 collapsed a level (Outline view) */
  CMD_OV_SHOWLEVEL,             /* arg 1-9: headings down to that level shown, 0 everything (Outline view) */
  CMD_OV_FLAGS,                 /* arg: OL_PLAIN and OL_FIRST (core/layout.h) toggled */
  CMD_OV_INFO,                  /* arg: struct OvInfo *: filled in */
  CMD_OV_MOVEPART,              /* arg: const u32 [2]: the heading paragraph [0] and its part moved to before paragraph [1]
                                   (the navigation pane's dragging) */
  CMD_OV_END
};
#define IS_OV_CMD(c) ((c) >= CMD_OV_BASE && (c) < CMD_OV_END)
/* the view's Outline view state, and the caret's paragraph's outline level */
struct OvInfo { int on, levels, plain, first, level; };
/* (view.c, wp_outline_view.h) the Outline view state of the view the last WpCtx was made for (the one whose
   ribbon state, keys or menu a package hook is working out) */
void wp11_view_info(struct OvInfo *oi);

#endif
