/*
 * wp_windows.c - WP25 Views and windows (docs/wordcraft-gap.md). This file belongs to WP25: only that
 * package edits it. Its commands are ID_WP25_FIRST .. ID_WP25_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * View > Window: New Window (another window on the same document: main.c keeps a document while any
 * window shows it, titles them Doc:1, Doc:2, and the views lay out again what another one changed),
 * Arrange All, Split (a second pane over the first, each with its own scroll bar; the one clicked
 * into is worked in), and the list of open windows (in the View menu and as Switch Windows on the
 * ribbon). Page movement Vertical or Side to Side, Multiple Pages with how many, a Zoom window, and
 * Read Mode's options (column width, page colour, text size, line spacing): wp_windows_view.h does
 * them in the page view. View Side by Side and Synchronous Scrolling (WordCraft #322), and the screen
 * paper colour (WordCraft #3398: white, soft green, warm cream or soft grey behind the text on screen,
 * kept in Quill.prefs).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/iffparse.h>
#include "wphooks.h"
#include "wp_windows.h"
#include "lang.h"

/* state bits (wpon[WPI_25]): Vertical, Side to Side, the window split */
enum { WS_VERTICAL = 1, WS_SIDE = 2, WS_SPLIT = 3, WS_SBS = 4, WS_SYNC = 5, WS_TWO = 6 };   /* (WS_TWO: two windows or more are open) */

/* ---- lists */
static char wtitles[WP_MAXWIN][72];               /* the open windows' names, for the menu and the ribbon */
static const char *wlabels[WP_MAXWIN];
static RibbonList wlist = { wlabels, 0, PL_TEXT, NULL };
static const char *const many_names[] = { "2 Pages", "3 Pages", "4 Pages", "2 x 2 Pages", "2 x 3 Pages", "2 x 4 Pages" };
static const UWORD many_vals[] = { 0x102, 0x103, 0x104, 0x202, 0x203, 0x204 };     /* rows << 8 | columns */
static const RibbonList many_list = { many_names, 6, PL_TEXT, NULL };
static const char *const rcol_names[] = { "Narrow", "Default", "Wide" };
static const RibbonList rcol_list = { rcol_names, 3, PL_TEXT, NULL };
static const char *const rcolor_names[] = { "None", "Sepia", "Inverse" };
static const RibbonList rcolor_list = { rcolor_names, 3, PL_TEXT, NULL };
static const char *const rsize_names[] = { "Smaller", "Default", "Larger", "Largest" };
static const RibbonList rsize_list = { rsize_names, 4, PL_TEXT, NULL };
static const char *const rspace_names[] = { "Default", "Wider" };
static const RibbonList rspace_list = { rspace_names, 2, PL_TEXT, NULL };
static const char *const paper_names[] = { "Document (White)", "Soft Green", "Warm Cream", "Soft Gray" };
static const ULONG paper_cols[] = { 0xFFFFFF, 0xCCE8CF, 0xFAF0DC, 0xE8EAED };
static const RibbonList paper_list = { paper_names, 4, PL_COLOR, paper_cols };

/* icons (theme.c's icon language) */
#define I_NEWWIN  "ci R 1 3 10 12 cw F 2 4 9 11 ca R 5 1 14 10 cw F 6 2 13 9 ca F 5 1 14 2"
#define I_ARRANGE "ci R 1 1 14 7 R 1 9 14 14 ca F 1 1 14 2 F 1 9 14 10"
#define I_SPLIT   "ci R 1 1 14 14 ca L 1 7 14 7 L 1 8 14 8 ci L 3 4 11 4 L 3 11 11 11"
#define I_SWITCH  "ci R 1 4 10 13 ca R 4 1 13 10 ci L 6 4 11 4"
#define I_VERT    "ci R 4 1 11 6 R 4 9 11 14 ca L 2 7 2 9"
#define I_SIDE    "ci R 1 4 6 11 R 9 4 14 11 ca L 7 2 9 2"
#define I_MANY    "ci R 1 1 6 6 R 9 1 14 6 R 1 9 6 14 R 9 9 14 14"
#define I_ZOOM    "ci O 6 6 4 L 9 9 14 14 L 4 6 8 6 L 6 4 6 8"
#define I_RCOL    "ci L 4 2 4 13 L 11 2 11 13 ca L 6 4 9 4 L 6 7 9 7 L 6 10 9 10"
#define I_RCOLOR  "co F 2 2 13 13 ci R 2 2 13 13 L 4 5 11 5 L 4 8 11 8"
#define I_RSIZE   "ci L 1 13 5 3 L 5 3 9 13 L 3 9 7 9 ca L 10 7 15 7 L 12 5 12 9"
#define I_RSPACE  "ci L 4 3 13 3 L 4 8 13 8 L 4 13 13 13 ca L 1 4 1 12"
#define I_SBS     "ci R 1 2 7 13 R 9 2 14 13 ca F 1 2 7 3 F 9 2 14 3"
#define I_SYNC    "ci R 1 2 7 13 R 9 2 14 13 ca L 4 5 4 10 L 11 5 11 10 L 3 9 4 10 L 5 9 4 10 L 10 9 11 10 L 12 9 11 10"
#define I_PAPER   "co F 3 1 12 14 ci R 3 1 12 14 L 5 4 10 4 L 5 7 10 7"

static const Item movement[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Vertical", ID_WW_VERTICAL, RB_WP(WPI_25, WS_VERTICAL), 0, 0, 0, "The pages run down the window", NULL, I_VERT },
  { K_BUTTON, S_SMALL, 0, 0, "Side to Side", ID_WW_SIDE, RB_WP(WPI_25, WS_SIDE), 0, 0, 0, "The pages side by side in one row, a page high (the wheel turns them)", NULL, I_SIDE },
  { K_DROP, S_SMALL, 0, 0, "Many Pages", ID_WW_MANY, 0, 0, 0, 0, "As many pages as you choose fitted in the window", &many_list, I_MANY },
};
static const Item window_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "New Window", ID_WW_NEWWIN, 0, 0, 0, 0, "Another window on this document (its title says which: Doc:1, Doc:2)", NULL, I_NEWWIN },
  { K_BUTTON, S_SMALL, 0, 0, "Arrange All", ID_WW_ARRANGE, 0, 0, F_NEWCOL, 0, "The open windows side by side on the screen", NULL, I_ARRANGE },
  { K_BUTTON, S_SMALL, 0, 0, "Split", ID_WW_SPLIT, RB_WP(WPI_25, WS_SPLIT), 0, 0, 0, "Two panes on the document, to see two parts of it at once (again: one)", NULL, I_SPLIT },
  { K_DROP, S_SMALL, 0, 0, "Switch Windows", ID_WW_SWITCH, 0, 0, 0, 0, "Another open window to the front (Amiga-` cycles)", &wlist, I_SWITCH },
  { K_BUTTON, S_SMALL, 0, 0, "Zoom...", ID_WW_ZOOMDLG, 0, 0, F_NEWCOL, 0, "The Zoom window: a zoom, the page width, whole or many pages", NULL, I_ZOOM },
  { K_BUTTON, S_SMALL, 0, 0, "Side by Side", ID_WW_SBS, RB_WP(WPI_25, WS_SBS), RB_WP(WPI_25, WS_TWO), 0, 0, "This window and the next side by side on the screen (again: back as they were)", NULL, I_SBS },
  { K_BUTTON, S_SMALL, 0, 0, "Sync Scrolling", ID_WW_SYNC, RB_WP(WPI_25, WS_SYNC), RB_WP(WPI_25, WS_SBS), 0, 0, "The two windows side by side scroll together", NULL, I_SYNC },
};
static const Item read_items[] = {
  { K_DROP, S_SMALL, 0, 0, "Column Width", ID_WW_RCOL, 0, 0, 0, 0, "Read Mode's text in a narrow or wide column (Default: the pages)", &rcol_list, I_RCOL },
  { K_DROP, S_SMALL, 0, 0, "Page Color", ID_WW_RCOLOR, 0, 0, 0, 0, "Read Mode's page: white, sepia or inverse", &rcolor_list, I_RCOLOR },
  { K_DROP, S_SMALL, 0, 0, "Text Size", ID_WW_RSIZE, 0, 0, 0, 0, "Read Mode's text size in a column", &rsize_list, I_RSIZE },
  { K_DROP, S_SMALL, 0, 0, "Line Spacing", ID_WW_RSPACE, 0, 0, F_NEWCOL, 0, "Read Mode's lines further apart", &rspace_list, I_RSPACE },
  { K_DROP, S_SMALL, 0, 0, "Screen Paper", ID_WW_PAPER, 0, 0, 0, 0, "The paper's colour on screen only, for every document (kept for next time)", &paper_list, I_PAPER },
};
static const WpGroup groups[] = {
  { TAB_VIEW, 1, { "Page Movement", G_COLS, movement, 3 } },
  { TAB_VIEW, -1, { "Window", G_COLS, window_items, 7 } },
  { TAB_VIEW, -1, { "Read Mode", G_COLS, read_items, 5 } },
};

static const WpMenu menus[] = {
  { WM_VIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Window", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"New Window", 0, 0, 0, (APTR)ID_WW_NEWWIN } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Arrange All", 0, 0, 0, (APTR)ID_WW_ARRANGE } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Split", 0, 0, 0, (APTR)ID_WW_SPLIT } },
  { WM_VIEW, { NM_SUB, (STRPTR)"View Side by Side", 0, 0, 0, (APTR)ID_WW_SBS } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Synchronous Scrolling", 0, 0, 0, (APTR)ID_WW_SYNC } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"1", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"2", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 1) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"3", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 2) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"4", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 3) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"5", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 4) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"6", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 5) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"7", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 6) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"8", 0, CHECKIT, 0, (APTR)(ID_WW_WIN0 + 7) } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Pages", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Vertical", 0, 0, 0, (APTR)ID_WW_VERTICAL } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Side to Side", 0, 0, 0, (APTR)ID_WW_SIDE } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"2 Pages", 0, 0, 0, (APTR)(ID_WW_MANY0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"3 Pages", 0, 0, 0, (APTR)(ID_WW_MANY0 + 1) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"4 Pages", 0, 0, 0, (APTR)(ID_WW_MANY0 + 2) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"2 x 2 Pages", 0, 0, 0, (APTR)(ID_WW_MANY0 + 3) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"2 x 3 Pages", 0, 0, 0, (APTR)(ID_WW_MANY0 + 4) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"2 x 4 Pages", 0, 0, 0, (APTR)(ID_WW_MANY0 + 5) } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Zoom...", 0, 0, 0, (APTR)ID_WW_ZOOMDLG } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Read Mode Options", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Column Width: Narrow", 0, 0, 0, (APTR)(ID_WW_RCOL0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Column Width: Default", 0, 0, 0, (APTR)(ID_WW_RCOL0 + 1) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Column Width: Wide", 0, 0, 0, (APTR)(ID_WW_RCOL0 + 2) } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Page Color: None", 0, 0, 0, (APTR)(ID_WW_RCOLOR0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Page Color: Sepia", 0, 0, 0, (APTR)(ID_WW_RCOLOR0 + 1) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Page Color: Inverse", 0, 0, 0, (APTR)(ID_WW_RCOLOR0 + 2) } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Text Size: Smaller", 0, 0, 0, (APTR)(ID_WW_RSIZE0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Text Size: Default", 0, 0, 0, (APTR)(ID_WW_RSIZE0 + 1) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Text Size: Larger", 0, 0, 0, (APTR)(ID_WW_RSIZE0 + 2) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Text Size: Largest", 0, 0, 0, (APTR)(ID_WW_RSIZE0 + 3) } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Line Spacing: Default", 0, 0, 0, (APTR)(ID_WW_RSPACE0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Line Spacing: Wider", 0, 0, 0, (APTR)(ID_WW_RSPACE0 + 1) } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Screen Paper Color", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Document (White)", 0, 0, 0, (APTR)(ID_WW_PAPER0 + 0) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Soft Green", 0, 0, 0, (APTR)(ID_WW_PAPER0 + 1) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Warm Cream", 0, 0, 0, (APTR)(ID_WW_PAPER0 + 2) } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Soft Gray", 0, 0, 0, (APTR)(ID_WW_PAPER0 + 3) } },
};

static void vcmd(ULONG c, LONG arg) { if (wp_view()) DoMethod(wp_view(), MUIM_Quill_Command, c, (ULONG)arg); }

/* ---- the window list: the View menu's entries in every window's menu strip, and the ribbon's list */
void wp25_windows_changed(void)
{
  static char was[WP_MAXWIN][72];
  static int wasn = -1;
  WpWin w[WP_MAXWIN];
  int slot, n = 0, k, same;
  for (slot = 0; slot < WP_MAXWIN; slot++) if (wp_win_get(slot, &w[n])) n++;
  for (k = 0; k < WP_MAXWIN; k++) {
    if (k < n) {
      const char *t = w[k].title ? w[k].title : "";
      if (!strncmp(t, "Quill - ", 8)) t += 8;
      sprintf(wtitles[k], "%d %.64s", k + 1, t);
    } else strcpy(wtitles[k], "-");
    wlabels[k] = wtitles[k];
  }
  wlist.n = n;
  same = wasn == n;
  for (k = 0; k < WP_MAXWIN && same; k++) if (strcmp(was[k], wtitles[k])) same = 0;
  for (slot = 0; slot < n && !same; slot++) {         /* (only when the list changed: menus are rebuilt for it) */
    if (!w[slot].strip) continue;
    for (k = 0; k < WP_MAXWIN; k++) {
      Object *o = (Object *)DoMethod(w[slot].strip, MUIM_FindUData, (ULONG)(ID_WW_WIN0 + k));
      if (!o) continue;
      SetAttrs(o, MUIA_NoNotify, TRUE, MUIA_Menuitem_Title, (ULONG)wtitles[k], MUIA_Menuitem_Enabled, k < n, MUIA_Menuitem_Checked, k == slot, TAG_DONE);
    }
  }
  memcpy(was, wtitles, sizeof was); wasn = n;
}
/* the k'th open window (in the list's order) to the front */
static void switch_to(int k)
{
  WpWin w;
  int slot, n = 0;
  for (slot = 0; slot < WP_MAXWIN; slot++) if (wp_win_get(slot, &w) && n++ == k) { wp_win_activate(slot); return; }
}

/* ---- Arrange All: the windows one above another down the screen (in a grid when that leaves them
   too small to show) */
static void arrange_all(void)
{
  WpWin w[WP_MAXWIN];
  int slot, n = 0, k, rows, cols;
  struct Window *iw = NULL;
  struct Screen *scr;
  LONG top, sw, sh, minw = 200, minh = 150;
  for (slot = 0; slot < WP_MAXWIN; slot++) if (wp_win_get(slot, &w[n])) n++;
  if (!n) return;
  get(w[0].win, MUIA_Window_Window, &iw);
  if (!iw || !(scr = iw->WScreen)) return;
  top = scr->BarHeight + 1;
  sw = scr->Width; sh = scr->Height - top;
  for (k = 0; k < n; k++) {
    struct Window *x = NULL;
    get(w[k].win, MUIA_Window_Window, &x);
    if (x && x->MinWidth > minw) minw = x->MinWidth;
    if (x && x->MinHeight > minh) minh = x->MinHeight;
  }
  rows = (int)(sh / minh); if (rows < 1) rows = 1; if (rows > n) rows = n;
  cols = (n + rows - 1) / rows;
  if (cols > 1 && sw / cols < minw) cols = (int)(sw / minw) > 0 ? (int)(sw / minw) : 1;
  for (k = 0; k < n; k++) {
    struct Window *x = NULL;
    LONG cw = sw / cols, rh = sh / rows, l = (k % cols) * cw, t = top + ((k / cols) % rows) * rh;
    get(w[k].win, MUIA_Window_Window, &x);
    if (!x) continue;
    if (cw < x->MinWidth) cw = x->MinWidth;
    if (rh < x->MinHeight) rh = x->MinHeight;
    if (l + cw > sw) l = sw - cw;
    if (t + rh > top + sh) t = top + sh - rh;
    if (l < 0) l = 0;
    if (t < top) t = top;
    ChangeWindowBox(x, l, t, cw, rh);
  }
}

/* ---- View Side by Side and Synchronous Scrolling (WordCraft #322): the window worked in and the next one
   open, each half the screen; again, both go back where they were */
static Object *sbs_win[2], *sbs_view[2];
static LONG sbs_box[2][4];
static int sbs_sync;
static void sync_set(int on)
{
  sbs_sync = on && sbs_win[0];
  if (sbs_view[0]) DoMethod(sbs_view[0], MUIM_Quill_Command, CMD_WW_SYNC, (ULONG)(sbs_sync ? sbs_view[1] : NULL));
  if (sbs_view[1]) DoMethod(sbs_view[1], MUIM_Quill_Command, CMD_WW_SYNC, (ULONG)(sbs_sync ? sbs_view[0] : NULL));
}
static void sbs_toggle(void)
{
  WpWin w[WP_MAXWIN];
  int slot, n = 0, k, cur = -1;
  struct Window *x[2] = { NULL, NULL };
  struct Screen *scr;
  LONG top, sw, sh;
  if (sbs_win[0]) {
    sync_set(0);
    for (k = 0; k < 2; k++) {
      struct Window *iw = NULL;
      get(sbs_win[k], MUIA_Window_Window, &iw);
      if (iw) ChangeWindowBox(iw, sbs_box[k][0], sbs_box[k][1], sbs_box[k][2], sbs_box[k][3]);
    }
    sbs_win[0] = sbs_win[1] = sbs_view[0] = sbs_view[1] = NULL;
    return;
  }
  for (slot = 0; slot < WP_MAXWIN; slot++) if (wp_win_get(slot, &w[n])) { if (w[n].current) cur = n; n++; }
  if (n < 2) { wp_ask("OK", "Open another window first (View > New Window, or another document).", NULL); return; }
  if (cur < 0) cur = 0;
  sbs_win[0] = w[cur].win; sbs_view[0] = w[cur].view;
  sbs_win[1] = w[(cur + 1) % n].win; sbs_view[1] = w[(cur + 1) % n].view;
  for (k = 0; k < 2; k++) get(sbs_win[k], MUIA_Window_Window, &x[k]);
  if (!x[0] || !x[1] || !(scr = x[0]->WScreen)) { sbs_win[0] = sbs_win[1] = sbs_view[0] = sbs_view[1] = NULL; return; }
  top = scr->BarHeight + 1;
  sw = scr->Width; sh = scr->Height - top;
  for (k = 0; k < 2; k++) {
    LONG cw = sw / 2, l = k * (sw / 2);
    sbs_box[k][0] = x[k]->LeftEdge; sbs_box[k][1] = x[k]->TopEdge; sbs_box[k][2] = x[k]->Width; sbs_box[k][3] = x[k]->Height;
    if (cw < x[k]->MinWidth) cw = x[k]->MinWidth;
    if (l + cw > sw) l = sw - cw;
    if (l < 0) l = 0;
    ChangeWindowBox(x[k], l, top, cw, sh < x[k]->MinHeight ? x[k]->MinHeight : sh);
  }
}
/* (from wp25_window_gone) a window side by side closed: the other stays where it is, scrolling alone */
static void sbs_gone(Object *win)
{
  if (!sbs_win[0] || (win != sbs_win[0] && win != sbs_win[1])) return;
  if (win == sbs_win[0]) sbs_view[0] = NULL; else sbs_view[1] = NULL;
  sync_set(0);
  sbs_win[0] = sbs_win[1] = sbs_view[0] = sbs_view[1] = NULL;
}

/* ---- Split: a second pane (its own view and scroll bar) over the window's own, a balance between */
typedef struct {
  Object *win, *mainview, *grp, *bal, *view2, *sc2;
  struct Toolbar tb2;
} Split;
static Split splits[WP_MAXWIN];
static Split *split_of(Object *win)
{
  int i;
  for (i = 0; i < WP_MAXWIN; i++) if (splits[i].win && splits[i].win == win) return &splits[i];
  return NULL;
}
void wp25_window_gone(Object *win)
{
  Split *s = split_of(win);
  sbs_gone(win);
  if (s) memset(s, 0, sizeof *s);
}
/* the children of group g in order (at most max), how many */
static int children(Object *g, Object **out, int max)
{
  struct List *l = NULL;
  struct Node *st;
  Object *o;
  int n = 0;
  get(g, MUIA_Group_ChildList, &l);
  for (st = l ? l->lh_Head : NULL; st && (o = (Object *)NextObject(&st)) && n < max; ) out[n++] = o;
  return n;
}
static void split_toggle(void)
{
  Object *win = wp_win(), *h = NULL, *v = NULL, *kids[16];
  Split *s = split_of(win);
  ULONG msg[20], vm = VM_PRINT;
  int n, i, k;
  Pos *cp = NULL;
  if (!win) return;
  if (s) {
    /* back to one pane: the window's own view works again */
    Object *grp = s->grp, *bal = s->bal;
    wp_set_view(s->mainview);
    DoMethod(s->mainview, MUIM_Quill_Command, CMD_WW_TWIN, 0);
    get(grp, MUIA_Parent, &v);
    if (v) {
      DoMethod(v, MUIM_Group_InitChange);
      DoMethod(v, OM_REMMEMBER, (ULONG)grp);
      DoMethod(v, OM_REMMEMBER, (ULONG)bal);
      DoMethod(v, MUIM_Group_ExitChange);
      MUI_DisposeObject(grp); MUI_DisposeObject(bal);
    }
    memset(s, 0, sizeof *s);
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_REFRESH, 0);
    return;
  }
  for (i = 0; i < WP_MAXWIN && splits[i].win; i++) ;
  if (i == WP_MAXWIN) return;
  s = &splits[i];
  memset(s, 0, sizeof *s);
  s->mainview = wp_view();
  get(s->mainview, MUIA_Parent, &h);
  if (h) get(h, MUIA_Parent, &v);
  if (!h || !v) { memset(s, 0, sizeof *s); return; }
  s->tb2 = *wp_toolbar();
  s->tb2.hprop = NULL;
  get(s->mainview, MUIA_Quill_ViewMode, &vm);
  s->grp = HGroup, MUIA_Group_Spacing, 0,
    Child, s->view2 = NewObject(view_class()->mcc_Class, NULL, MUIA_Quill_Fonts, (ULONG)wp_fonts(), MUIA_Quill_Toolbar, (ULONG)&s->tb2,
                                MUIA_Quill_Doc, (ULONG)wp_doc(), MUIA_Quill_ViewMode, vm,      /* (its document before it is ever drawn) */
                                MUIA_FillArea, FALSE, MUIA_CycleChain, 1, TAG_DONE),
    Child, s->sc2 = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE),
  End;
  s->bal = BalanceObject, End;
  if (!s->bal) s->bal = RectangleObject, MUIA_FixHeight, 3, MUIA_Background, MUII_FILL, End;   /* (no Balance class: a bar instead) */
  if (!s->grp || !s->bal) { if (s->grp) MUI_DisposeObject(s->grp); if (s->bal) MUI_DisposeObject(s->bal); memset(s, 0, sizeof *s); DisplayBeep(NULL); return; }
  s->tb2.prop = s->sc2;
  s->win = win;
  /* in the group above the window's view (the ruler, the view's row, the sideways scroll bar): the
     new pane and the balance go in just before the view's row */
  n = children(v, kids, 13);
  DoMethod(v, MUIM_Group_InitChange);
  DoMethod(v, OM_ADDMEMBER, (ULONG)s->grp);
  DoMethod(v, OM_ADDMEMBER, (ULONG)s->bal);
  msg[0] = MUIM_Group_Sort;
  for (i = 0, k = 1; i < n; i++) {
    if (kids[i] == h) { msg[k++] = (ULONG)s->grp; msg[k++] = (ULONG)s->bal; }
    msg[k++] = (ULONG)kids[i];
  }
  msg[k] = 0;
  DoMethodA(v, (Msg)msg);
  DoMethod(v, MUIM_Group_ExitChange);
  DoMethod(s->sc2, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)s->view2, 3, MUIM_Set, MUIA_Quill_Scroll, MUIV_TriggerValue);
  DoMethod(s->view2, MUIM_Notify, MUIA_Quill_Changed, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TITLE);
  DoMethod(s->view2, MUIM_Notify, MUIA_Quill_Outline, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_OUTLINECHG);
  DoMethod(s->view2, MUIM_Notify, MUIA_Quill_InTable, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INTABLE);
  DoMethod(s->view2, MUIM_Notify, MUIA_Quill_EditObject, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_OBJEDIT);
  DoMethod(s->view2, MUIM_Notify, MUIA_Quill_EditStory, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_STORYREQ);
  DoMethod(s->view2, MUIM_Notify, MUIA_Quill_ViewMode, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_VMODECHG);
  /* the same place; the window's own pane stays the one worked in */
  get(s->mainview, MUIA_Quill_Caret, &cp);
  if (cp) { Pos c = *cp; set(s->view2, MUIA_Quill_Caret, (ULONG)&c); }
  DoMethod(s->view2, MUIM_Quill_Command, CMD_WW_TWIN, (ULONG)s->mainview);
  DoMethod(s->mainview, MUIM_Quill_Command, CMD_WW_TWIN, (ULONG)s->view2);
  DoMethod(s->view2, MUIM_Quill_Command, CMD_WW_PASSIVE, 1);
  DoMethod(s->mainview, MUIM_Quill_Command, CMD_WW_PASSIVE, 0);
  DoMethod(s->view2, MUIM_Quill_Command, CMD_REFRESH, 0);
}

/* ---- the Zoom window */
static Object *zwin, *z_to, *z_pct, *z_many;
static const char *const zto_names[] = { "200%", "100%", "75%", "Page width", "Whole page", "Many pages", "Percent", NULL };
static const char *zto_tr[8], *zmany_tr[7];
static void open_zoom(void)
{
  LONG z = 100;
  char b[16];
  int i;
  if (!zwin) {
    Object *c;
    for (i = 0; zto_names[i]; i++) zto_tr[i] = tr(zto_names[i]);
    zto_tr[i] = NULL;
    for (i = 0; i < 6; i++) zmany_tr[i] = tr(many_names[i]);
    zmany_tr[6] = NULL;
    c = ColGroup(2),
      Child, Label1(tr("Zoom to:")), Child, z_to = CycleObject, MUIA_Cycle_Entries, (ULONG)zto_tr, MUIA_CycleChain, 1, End,
      Child, Label1(tr("Many pages:")), Child, z_many = CycleObject, MUIA_Cycle_Entries, (ULONG)zmany_tr, MUIA_CycleChain, 1, End,
      Child, Label2(tr("Percent:")), Child, z_pct = StringObject, StringFrame, MUIA_String_MaxLen, 5, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
    End;
    if (!(zwin = wp_dialog("Zoom", MAKE_ID('W','2','5','Z'), c, "_OK", ID_WW_ZOOMOK))) { DisplayBeep(NULL); return; }
  }
  if (wp_view()) get(wp_view(), MUIA_Quill_Zoom, &z);
  sprintf(b, "%ld", (long)z);
  set(z_pct, MUIA_String_Contents, (ULONG)b);
  set(z_to, MUIA_Cycle_Active, z == 200 ? 0 : z == 100 ? 1 : z == 75 ? 2 : 6);
  set(zwin, MUIA_Window_Open, TRUE);
}
static void zoom_done(void)
{
  ULONG to = 0, mi = 0;
  char *t = NULL;
  long pct;
  set(zwin, MUIA_Window_Open, FALSE);
  get(z_to, MUIA_Cycle_Active, &to);
  get(z_many, MUIA_Cycle_Active, &mi);
  get(z_pct, MUIA_String_Contents, &t);
  pct = t ? atol(t) : 100;
  if (!wp_view()) return;
  switch (to) {
  case 0: set(wp_view(), MUIA_Quill_Zoom, 200); break;
  case 1: set(wp_view(), MUIA_Quill_Zoom, 100); break;
  case 2: set(wp_view(), MUIA_Quill_Zoom, 75); break;
  case 3: set(wp_view(), MUIA_Quill_Zoom, 0); break;
  case 4: set(wp_view(), MUIA_Quill_Zoom, (ULONG)-1); break;
  case 5: set(wp_view(), MUIA_Quill_ViewMode, VM_PRINT); wp_view_mode_changed(); vcmd(CMD_WW_MANY, many_vals[mi < 6 ? mi : 0]); break;
  default: set(wp_view(), MUIA_Quill_Zoom, (ULONG)(pct < 25 ? 25 : pct > 400 ? 400 : pct)); break;
  }
  wp_refresh();
  set(wp_win(), MUIA_Window_Activate, TRUE);
}

/* Read Mode's options: one changed, and Read Mode shown with them */
static void read_option(int which, int v)
{
  struct WwInfo wi;
  struct WwRead rd;
  memset(&wi, 0, sizeof wi);
  wi.rd.col = 1;
  if (wp_view()) DoMethod(wp_view(), MUIM_Quill_Command, CMD_WW_INFO, (ULONG)&wi);
  rd = wi.rd;
  if (which == 0) rd.col = v; else if (which == 1) rd.color = v; else if (which == 2) rd.size = v - 1; else rd.space = v;
  vcmd(CMD_WW_READ, (LONG)&rd);
  if (wp_view()) set(wp_view(), MUIA_Quill_ViewMode, VM_READ);
  wp_view_mode_changed();
}

/* the screen paper colour for every view (each window's, and a split window's other pane) */
static void paper_set(int k)
{
  WpWin w;
  int slot, i;
  wp25_paper = k;
  for (slot = 0; slot < WP_MAXWIN; slot++) if (wp_win_get(slot, &w) && w.view) DoMethod(w.view, MUIM_Quill_Command, CMD_WW_PAPER, 0);
  for (i = 0; i < WP_MAXWIN; i++) {
    if (splits[i].win && splits[i].view2) DoMethod(splits[i].view2, MUIM_Quill_Command, CMD_WW_PAPER, 0);
    if (splits[i].win && splits[i].mainview) DoMethod(splits[i].mainview, MUIM_Quill_Command, CMD_WW_PAPER, 0);
  }
  wp25_paper_save();
}
/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp25_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_WW_NEWWIN: if (!wp_win_new_shared()) DisplayBeep(NULL); break;
  case ID_WW_ARRANGE: arrange_all(); break;
  case ID_WW_SPLIT: split_toggle(); break;
  case ID_WW_SWITCH: if (value >= 0) switch_to((int)value); break;
  case ID_WW_FOCUS: {
    /* the pane clicked into of a split window is the one worked in */
    Object *v = wp25_clicked_view();
    Split *s = split_of(wp_win());
    if (v && s && (v == s->view2 || v == s->mainview)) {
      s->tb2.uibits = wp_toolbar()->uibits; s->tb2.uibits2 = wp_toolbar()->uibits2;
      wp_set_view(v);
    }
    break; }
  case ID_WW_VERTICAL: case ID_WW_SIDE:
    if (wp_view()) { ULONG vm = VM_PRINT; get(wp_view(), MUIA_Quill_ViewMode, &vm); if (vm != VM_PRINT) { set(wp_view(), MUIA_Quill_ViewMode, VM_PRINT); wp_view_mode_changed(); } }
    vcmd(CMD_WW_SIDE, id == ID_WW_SIDE);
    break;
  case ID_WW_MANY: if (value >= 0 && value < 6) { set(wp_view(), MUIA_Quill_ViewMode, VM_PRINT); wp_view_mode_changed(); vcmd(CMD_WW_MANY, many_vals[value]); } break;
  case ID_WW_ZOOMDLG: open_zoom(); return zwin ? WPR_KEEPFOCUS : WPR_DONE;
  case ID_WW_ZOOMOK: zoom_done(); break;
  case ID_WW_RCOL: if (value >= 0 && value < 3) read_option(0, (int)value); break;
  case ID_WW_RCOLOR: if (value >= 0 && value < 3) read_option(1, (int)value); break;
  case ID_WW_RSIZE: if (value >= 0 && value < 4) read_option(2, (int)value); break;
  case ID_WW_RSPACE: if (value >= 0 && value < 2) read_option(3, (int)value); break;
  case ID_WW_PAPER: if (value >= 0 && value < 4) paper_set((int)value); break;
  case ID_WW_SBS: sbs_toggle(); break;
  case ID_WW_SYNC:
    if (!sbs_win[0]) { wp_ask("OK", "Turn on View Side by Side first.", NULL); break; }
    sync_set(!sbs_sync);
    break;
  default:
    if (id >= ID_WW_WIN0 && id < ID_WW_WIN0 + WP_MAXWIN) switch_to((int)(id - ID_WW_WIN0));
    else if (id >= ID_WW_MANY0 && id < ID_WW_MANY0 + 6) return wp25_command(ID_WW_MANY, (LONG)(id - ID_WW_MANY0));
    else if (id >= ID_WW_RCOL0 && id < ID_WW_RCOL0 + 3) read_option(0, (int)(id - ID_WW_RCOL0));
    else if (id >= ID_WW_RCOLOR0 && id < ID_WW_RCOLOR0 + 3) read_option(1, (int)(id - ID_WW_RCOLOR0));
    else if (id >= ID_WW_RSIZE0 && id < ID_WW_RSIZE0 + 4) read_option(2, (int)(id - ID_WW_RSIZE0));
    else if (id >= ID_WW_RSPACE0 && id < ID_WW_RSPACE0 + 2) read_option(3, (int)(id - ID_WW_RSPACE0));
    else if (id >= ID_WW_PAPER0 && id < ID_WW_PAPER0 + 4) paper_set((int)(id - ID_WW_PAPER0));
    break;
  }
  wp_refresh();
  wp25_windows_changed();
  return WPR_DONE;
}

/* the ribbon's state: Vertical or Side to Side, the window split */
static void state(ULONG *on, const WpCtx *c)
{
  struct WwInfo wi;
  wp25_view_info(&wi);
  *on |= RBIT(wi.side ? WS_SIDE : WS_VERTICAL);
  if (split_of(wp_win())) *on |= RBIT(WS_SPLIT);
  if (sbs_win[0]) *on |= RBIT(WS_SBS);
  if (sbs_sync) *on |= RBIT(WS_SYNC);
  if (wlist.n > 1) *on |= RBIT(WS_TWO);
  (void)c;
}
/* the right-button menu in Read Mode: its options */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  static const char *const cols[3] = { "Narrow", "Default", "Wide" }, *const colors[3] = { "None", "Sepia", "Inverse" };
  static const char *tcols[3], *tcolors[3], *tsizes[4], *tspaces[2];
  int i;
  if (c->vmode != VM_READ) return;
  for (i = 0; i < 3; i++) { tcols[i] = tr(cols[i]); tcolors[i] = tr(colors[i]); }
  for (i = 0; i < 4; i++) tsizes[i] = tr(rsize_names[i]);
  for (i = 0; i < 2; i++) tspaces[i] = tr(rspace_names[i]);
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_sub(menu, tr("Column Width"), ID_WW_RCOL, tcols, 3);
  wp_ctx_sub(menu, tr("Page Color"), ID_WW_RCOLOR, tcolors, 3);
  wp_ctx_sub(menu, tr("Text Size"), ID_WW_RSIZE, tsizes, 4);
  wp_ctx_sub(menu, tr("Line Spacing"), ID_WW_RSPACE, tspaces, 2);
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp25_package = {
  "WP25", ID_WP25_FIRST, ID_WP25_LAST,
  wp25_command, state, NULL, ctxmenu, NULL, NULL, NULL,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};

