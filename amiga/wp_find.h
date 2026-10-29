/*
 * wp_find.h - WP12's commands for the page view (they reach view.c's MUIM_Quill_Command like its
 * own CMD_s, in a number range of WP12's own, and are done by amiga/wp_find_view.h, which view.c
 * includes). This file belongs to WP12.
 */
#ifndef QUILL_WP_FIND_H
#define QUILL_WP_FIND_H

#include "ids.h"

/* WP12's command ids (its block in ids.h; main.c sends Find's, Select's and Go To's list entries here) */
enum {
  ID_FX_ADVFIND = ID_WP12_FIRST, ID_FX_REPLACE, ID_FX_GOTO, ID_FX_GOBACK, ID_FX_REPEAT, ID_FX_SELOBJ, ID_FX_SIMILAR,
  ID_FX_EXTEND, ID_FX_SHRINK, ID_FX_COLUMN, ID_FX_SPIKE, ID_FX_INSSPIKE, ID_FX_UNICODE, ID_FX_UNDOLIST, ID_FX_REDOLIST,
  ID_FX_HILITE, ID_FX_UNHILITE, ID_FX_FINDNEXT, ID_FX_FINDPREV, ID_FX_REPL1, ID_FX_REPLALL, ID_FX_FORMAT, ID_FX_RFORMAT,
  ID_FX_NOFMT, ID_FX_SPECIAL, ID_FX_ACTIVE, ID_FX_FMTOK, ID_FX_GOGO, ID_FX_GONEXT, ID_FX_GOPREV, ID_FX_GOKIND, ID_FX_ULOK,
  ID_FX_REPEATFIND, ID_FX_LAST_
};

#define CMD_FV_BASE 0x57120000UL
enum {
  CMD_FV_EXTEND = CMD_FV_BASE,  /* arg: 1 Extend Selection (F8: on, then a word, sentence, paragraph, section, the document),
                                   -1 shrink it (Shift-F8), 0 off */
  CMD_FV_COLUMN,                /* column (block) selection on, or off if it is on */
  CMD_FV_SIMILAR,               /* Select Text with Similar Formatting (the caret's) */
  CMD_FV_OBJECTS,               /* Select Objects on (drag round floating objects to choose them) or off */
  CMD_FV_HILITE,                /* arg: const FxPattern * (core/findx.h), its matches shown highlighted; NULL: none */
  CMD_FV_PASTEFRAG,             /* arg: const Fragment *: pasted at the caret with its formatting (the selection replaced) */
  CMD_FV_INFO,                  /* arg: struct FvInfo *: filled in */
  CMD_FV_END
};
#define IS_FV_CMD(c) ((c) >= CMD_FV_BASE && (c) < CMD_FV_END)
/* what the view's WP12 state is */
struct FvInfo { int ext, column, similar, objects, nranges, highlights; };

#endif
