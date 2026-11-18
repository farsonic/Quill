/*
 * wp_windows.h - WP25's command ids, its commands for the page view (done by amiga/wp_windows_view.h,
 * which view.c includes, like the view's own CMD_s) and the few things main.c gives it about the
 * document windows. This file belongs to WP25.
 */
#ifndef QUILL_WP_WINDOWS_H
#define QUILL_WP_WINDOWS_H

#include "quill.h"
#include "ids.h"

/* WP25's command ids (its block in ids.h) */
enum {
  ID_WW_NEWWIN = ID_WP25_FIRST, ID_WW_ARRANGE, ID_WW_SPLIT, ID_WW_SWITCH, ID_WW_VERTICAL, ID_WW_SIDE, ID_WW_MANY,
  ID_WW_ZOOMDLG, ID_WW_ZOOMOK, ID_WW_RCOL, ID_WW_RCOLOR, ID_WW_RSIZE, ID_WW_RSPACE, ID_WW_FOCUS,
  ID_WW_WIN0,                   /* .. ID_WW_WIN0 + 7: the View menu's Window list */
  ID_WW_MANY0 = ID_WW_WIN0 + 8, /* .. + 5: the View menu's Multiple Pages entries */
  ID_WW_RCOL0 = ID_WW_MANY0 + 6,     /* .. + 2: Read Mode's column widths (narrow, default, wide) */
  ID_WW_RCOLOR0 = ID_WW_RCOL0 + 3,   /* .. + 2: page colours (none, sepia, inverse) */
  ID_WW_RSIZE0 = ID_WW_RCOLOR0 + 3,  /* .. + 3: text sizes (smaller, default, larger, largest) */
  ID_WW_RSPACE0 = ID_WW_RSIZE0 + 4,  /* .. + 1: line spacing (default, wider) */
  ID_WW_PAPER = ID_WW_RSPACE0 + 2,   /* the screen paper colour list (WordCraft #3398) */
  ID_WW_PAPER0,                      /* .. + 3: document (white), soft green, warm cream, soft grey */
  ID_WW_SBS = ID_WW_PAPER0 + 4,      /* View Side by Side (WordCraft #322) */
  ID_WW_SYNC,                        /* Synchronous Scrolling */
  ID_WW_LAST_
};

#define CMD_WW_BASE 0x57250000UL
enum {
  CMD_WW_SIDE = CMD_WW_BASE,    /* arg 1: Side to Side page movement (all pages in one row), 0 Vertical */
  CMD_WW_MANY,                  /* arg (rows << 8) | columns: that many pages fitted in the view; 0 off */
  CMD_WW_READ,                  /* arg: struct WwRead *: Read Mode's options (applied at once in Read Mode) */
  CMD_WW_INFO,                  /* arg: struct WwInfo *: filled in */
  CMD_WW_TWIN,                  /* arg: the other view in the same window (Split), NULL none */
  CMD_WW_PASSIVE,               /* arg 1: not the window's working pane (no keys, the tools left to the other), 0 it is */
  CMD_WW_PAPER,                 /* the screen paper colour (wp25_paper) changed: the page's pens again */
  CMD_WW_SYNC,                  /* arg: the view to scroll along with this one (Synchronous Scrolling), NULL none */
  CMD_WW_SYNCBY,                /* arg: pixels the view it scrolls along with moved: this one moves as far */
  CMD_WW_END
};
#define IS_WW_CMD(c) ((c) >= CMD_WW_BASE && (c) < CMD_WW_END)
/* Read Mode's options: column width (0 narrow, 1 default: the pages, 2 wide), page colour (0 none, 1 sepia,
   2 inverse), text size (-1 smaller, 0 default, 1 larger, 2 largest), line spacing (0 default, 1 wider) */
struct WwRead { int col, color, size, space; };
struct WwInfo { int side, many, rows; struct WwRead rd; int passive; };
/* the screen paper colour, on screen only, for every document without a page colour of its own: 0 white
   (the document's), 1 soft green, 2 warm cream, 3 soft grey (WordCraft #3398; main.c keeps it in
   Quill.prefs as paper=) */
extern int wp25_paper;
void wp25_paper_save(void);                 /* (main.c) Quill.prefs written again */
/* (view.c, wp_windows_view.h) the state of the view the last WpCtx was made for; the view last
   clicked into of a window split in two */
void wp25_view_info(struct WwInfo *wi);
Object *wp25_clicked_view(void);

/* ---- main.c's document windows (its WP25 part) */
typedef struct {
  Object *win, *strip, *view;   /* the window, its menu strip, its view (the working pane) */
  Doc *doc;
  const char *title;            /* the window's title as shown */
  int current;                  /* the window being worked in */
} WpWin;
#define WP_MAXWIN 8
int  wp_win_get(int slot, WpWin *out);      /* slot 0..WP_MAXWIN-1: 0 if unused */
void wp_win_activate(int slot);             /* that window to the front, worked in */
int  wp_win_new_shared(void);               /* New Window: another window on the current document; 0 if none */
void wp_set_view(Object *v);                /* the current window's working pane (a split window's other view) */
struct Toolbar *wp_toolbar(void);           /* the current window's tools (its main view's) */
FontSet *wp_fonts(void);                    /* the fonts the views measure with */
void wp_view_mode_changed(void);            /* the tools round the page shown again for the view mode */
/* (wp_windows.c) main.c tells WP25 when windows come, go, are retitled or switched; and when one is
   about to go (its split view's record dropped) */
void wp25_windows_changed(void);
void wp25_window_gone(Object *win);

#endif
