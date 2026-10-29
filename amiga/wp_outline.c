/*
 * wp_outline.c - WP11 Outline view and outline levels (docs/wordcraft-gap.md). This file belongs to WP11: only that
 * package edits it. Its commands are ID_WP11_FIRST .. ID_WP11_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * View > Outline shows the document as an outline (wp_outline_view.h does it in the page view): the
 * contextual Outlining tab promotes and demotes headings (by the Heading styles, as Word), moves
 * paragraphs and whole parts up and down, expands and collapses headings, shows levels down to one,
 * with or without the text's formatting, body text's first lines only. The paragraph's outline level
 * itself is in the Paragraph and style windows (main.c) and .docx (core/docx.c). Keys, everywhere:
 * Ctrl-Alt-Shift with the cursor keys moves paragraphs up and down and promotes and demotes; in
 * Outline view Alt-Shift with them too (Word's keys; elsewhere Alt-Shift selects by paragraph and
 * word in Quill), Tab and Shift-Tab, Alt-Shift-+ and - (expand, collapse), Alt-Shift-1 to 9 and A
 * (show levels).
 */
#include <string.h>
#include <stdio.h>
#include <devices/inputevent.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_outline.h"
#include "lang.h"
#include "../core/layout.h"

/* state bits (wpon[WPI_11]): the tab, Show Text Formatting, Show First Line Only, in Outline view;
   bits 8-11 the caret's level, 12-15 the levels shown */
enum { OS_FORMAT = 1, OS_FIRST = 2, OS_ON = 3 };

static const char *const level_names[] = { "Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6", "Level 7", "Level 8", "Level 9", "Body Text" };
static const RibbonList level_list = { level_names, 10, PL_TEXT, NULL };
static const char *const show_names[] = { "Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6", "Level 7", "Level 8", "Level 9", "All Levels" };
static const RibbonList show_list = { show_names, 10, PL_TEXT, NULL };

/* icons (theme.c's icon language) */
#define I_TOH1   "ci L 2 8 7 3 L 2 8 7 13 L 3 8 7 4 L 3 8 7 12 ca L 8 8 13 3 L 8 8 13 13 L 9 8 13 4 L 9 8 13 12"
#define I_PROM   "ci L 4 8 10 2 L 4 8 10 14 L 5 8 10 3 L 5 8 10 13 ca L 11 8 15 8"
#define I_DEM    "ci L 12 8 6 2 L 12 8 6 14 L 11 8 6 3 L 11 8 6 13 ca L 1 8 5 8"
#define I_TOBODY "ci L 14 8 9 3 L 14 8 9 13 L 13 8 9 4 L 13 8 9 12 ca L 8 8 3 3 L 8 8 3 13 L 7 8 3 4 L 7 8 3 12"
#define I_UP     "ci L 8 2 8 14 L 7 2 7 14 L 8 2 3 7 L 7 2 12 7 ca L 1 15 14 15"
#define I_DOWN   "ci L 8 1 8 13 L 7 1 7 13 L 8 13 3 8 L 7 13 12 8 ca L 1 0 14 0"
#define I_EXPAND "ci O 8 8 6 ca L 5 8 11 8 L 8 5 8 11"
#define I_COLL   "ci O 8 8 6 ca L 5 8 11 8"
#define I_OUTLINE "ca E 3 3 1 E 6 8 1 E 9 13 1 ci L 5 3 14 3 L 8 8 14 8 L 11 13 14 13"
#define I_CLOSE  "cr O 8 8 6 L 5 5 11 11 L 11 5 5 11"

static const Item tools[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Promote to Heading 1", ID_OL_TOH1, 0, 0, 0, 0, "Makes the paragraph a Heading 1", NULL, I_TOH1 },
  { K_BUTTON, S_SMALL, 0, 0, "Promote", ID_OL_PROMOTE, 0, 0, 0, 0, "A heading one level up; body text becomes a heading like the one above it (Alt-Shift-Left)", NULL, I_PROM },
  { K_BUTTON, S_SMALL, 0, 0, "Demote", ID_OL_DEMOTE, 0, 0, 0, 0, "A heading one level down (Alt-Shift-Right)", NULL, I_DEM },
  { K_BUTTON, S_SMALL, 0, 0, "Demote to Body Text", ID_OL_TOBODY, 0, 0, F_NEWCOL, 0, "Makes the paragraph body text (the Normal style)", NULL, I_TOBODY },
  { K_BUTTON, S_SMALL, 0, 0, "Move Up", ID_OL_UP, 0, 0, 0, 0, "Moves the paragraph up, a collapsed heading with what is under it (Alt-Shift-Up)", NULL, I_UP },
  { K_BUTTON, S_SMALL, 0, 0, "Move Down", ID_OL_DOWN, 0, 0, 0, 0, "Moves the paragraph down (Alt-Shift-Down)", NULL, I_DOWN },
  { K_BUTTON, S_SMALL, 0, 0, "Expand", ID_OL_EXPAND, 0, RB_WP(WPI_11, OS_ON), F_NEWCOL, 0, "Shows a level more under the heading (Alt-Shift-+; or double-click its plus)", NULL, I_EXPAND },
  { K_BUTTON, S_SMALL, 0, 0, "Collapse", ID_OL_COLLAPSE, 0, RB_WP(WPI_11, OS_ON), 0, 0, "Hides a level under the heading (Alt-Shift--)", NULL, I_COLL },
  { K_FIELD, S_SMALL, 0, 0, "Outline Level", ID_OL_LEVEL, 0, 0, 0, 76, "The paragraph's outline level", &level_list, NULL },
  { K_FIELD, S_SMALL, 0, 0, "Show Level", ID_OL_SHOWLEVEL, 0, RB_WP(WPI_11, OS_ON), F_NEWCOL, 76, "Shows the headings down to this level (Alt-Shift-1 to 9, A for all)", &show_list, NULL },
  { K_BUTTON, S_SMALL, 0, 0, "Show Text Formatting", ID_OL_FORMAT, RB_WP(WPI_11, OS_FORMAT), RB_WP(WPI_11, OS_ON), F_CHECK, 0, "The text in its fonts and sizes (else all in the Normal style's)" },
  { K_BUTTON, S_SMALL, 0, 0, "Show First Line Only", ID_OL_FIRSTLINE, RB_WP(WPI_11, OS_FIRST), RB_WP(WPI_11, OS_ON), F_CHECK, 0, "Body text's first line only" },
};
static const Item close_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Close Outline View", ID_OL_CLOSE, 0, 0, 0, 0, "Back to Print Layout", NULL, I_CLOSE },
};
static const WpGroup groups[] = {
  { TAB_OUTLINING, -1, { "Outline Tools", G_COLS, tools, (int)(sizeof tools / sizeof tools[0]) } },
  { TAB_OUTLINING, -1, { "Close", G_COLS, close_items, 1 } },
};

/* the View menu's Outline and Outlining items */
static const WpMenu menus[] = {
  { WM_VIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Outline", 0, 0, 0, (APTR)ID_OL_VIEW } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Outlining", 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Promote to Heading 1", 0, 0, 0, (APTR)ID_OL_TOH1 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Promote (Ctrl-Alt-Shift-Left)", 0, 0, 0, (APTR)ID_OL_PROMOTE } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Demote (Ctrl-Alt-Shift-Right)", 0, 0, 0, (APTR)ID_OL_DEMOTE } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Demote to Body Text", 0, 0, 0, (APTR)ID_OL_TOBODY } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Move Up (Ctrl-Alt-Shift-Up)", 0, 0, 0, (APTR)ID_OL_UP } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Move Down (Ctrl-Alt-Shift-Down)", 0, 0, 0, (APTR)ID_OL_DOWN } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Expand", 0, 0, 0, (APTR)ID_OL_EXPAND } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Collapse", 0, 0, 0, (APTR)ID_OL_COLLAPSE } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 1", 0, 0, 0, (APTR)ID_OL_SHOW1 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 2", 0, 0, 0, (APTR)ID_OL_SHOW2 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 3", 0, 0, 0, (APTR)ID_OL_SHOW3 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 4", 0, 0, 0, (APTR)ID_OL_SHOW4 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 5", 0, 0, 0, (APTR)ID_OL_SHOW5 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 6", 0, 0, 0, (APTR)ID_OL_SHOW6 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 7", 0, 0, 0, (APTR)ID_OL_SHOW7 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 8", 0, 0, 0, (APTR)ID_OL_SHOW8 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Level 9", 0, 0, 0, (APTR)ID_OL_SHOW9 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show All Levels", 0, 0, 0, (APTR)ID_OL_SHOWALL } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show Text Formatting", 0, 0, 0, (APTR)ID_OL_FORMAT } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Show First Line Only", 0, 0, 0, (APTR)ID_OL_FIRSTLINE } },
  { WM_VIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_VIEW, { NM_SUB, (STRPTR)"Close Outline View", 0, 0, 0, (APTR)ID_OL_CLOSE } },
};

static void cmd(ULONG c, LONG arg) { if (wp_view()) DoMethod(wp_view(), MUIM_Quill_Command, c, (ULONG)arg); }
static int in_outline(void)
{
  ULONG vm = VM_PRINT;
  if (wp_view()) get(wp_view(), MUIA_Quill_ViewMode, &vm);
  return vm == VM_OUTLINE;
}
/* Outline view on (the Outlining tab shown) or off (back to Print Layout and the Home tab) */
static void outline_view(int on)
{
  if (!wp_view()) return;
  set(wp_view(), MUIA_Quill_ViewMode, on ? VM_OUTLINE : VM_PRINT);
  wp_refresh();
  if (wp_ribbon()) set(wp_ribbon(), MUIA_Ribbon_Tab, on ? TAB_OUTLINING : TAB_HOME);
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp11_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_OL_VIEW: outline_view(1); break;
  case ID_OL_CLOSE: outline_view(0); break;
  case ID_OL_TOH1: cmd(CMD_OV_SETLEVEL, 1); break;
  case ID_OL_TOBODY: cmd(CMD_OV_SETLEVEL, 0); break;
  case ID_OL_PROMOTE: case ID_OL_DEMOTE: cmd(CMD_OV_PROMOTE, id == ID_OL_PROMOTE ? -1 : 1); break;
  case ID_OL_LEVEL: if (value >= 0 && value < 10) cmd(CMD_OV_SETLEVEL, value < 9 ? value + 1 : 0); break;
  case ID_OL_UP: case ID_OL_DOWN: cmd(CMD_OV_MOVE, id == ID_OL_UP ? -1 : 1); break;
  case ID_OL_EXPAND: case ID_OL_COLLAPSE:
    if (!in_outline()) outline_view(1);
    cmd(CMD_OV_EXPAND, id == ID_OL_EXPAND);
    break;
  case ID_OL_SHOWLEVEL:
    if (value >= 0 && value < 10) { if (!in_outline()) outline_view(1); cmd(CMD_OV_SHOWLEVEL, value < 9 ? value + 1 : 0); }
    break;
  case ID_OL_SHOW1: case ID_OL_SHOW2: case ID_OL_SHOW3: case ID_OL_SHOW4: case ID_OL_SHOW5: case ID_OL_SHOW6: case ID_OL_SHOW7:
  case ID_OL_SHOW8: case ID_OL_SHOW9: case ID_OL_SHOWALL:
    if (!in_outline()) outline_view(1);
    cmd(CMD_OV_SHOWLEVEL, id == ID_OL_SHOWALL ? 0 : (LONG)(id - ID_OL_SHOW1 + 1));
    break;
  case ID_OL_FORMAT: case ID_OL_FIRSTLINE:
    if (!in_outline()) outline_view(1);
    cmd(CMD_OV_FLAGS, id == ID_OL_FORMAT ? OL_PLAIN : OL_FIRST);
    break;
  }
  wp_refresh();
  return WPR_DONE;
}

/* the ribbon's state: the Outlining tab in Outline view, its check boxes, the caret's level and the
   levels shown (for the two fields) */
static void state(ULONG *on, const WpCtx *c)
{
  struct OvInfo oi;
  wp11_view_info(&oi);
  if (c->vmode == VM_OUTLINE) *on |= RBIT(WPS_TAB) | RBIT(OS_ON);
  if (!oi.plain) *on |= RBIT(OS_FORMAT);
  if (oi.first) *on |= RBIT(OS_FIRST);
  *on |= (ULONG)(oi.level & 15) << 8;
  *on |= (ULONG)(oi.levels & 15) << 12;
}
/* the two fields' text: the caret's paragraph's level, and the levels shown */
static void field(ULONG id, const struct MUIP_Ribbon_State *st, char *out, size_t cap)
{
  ULONG w = st->wpon[WPI_11];
  int k;
  if (id == ID_OL_LEVEL) { k = (int)((w >> 8) & 15); strncpy(out, tr(k >= 1 && k <= 9 ? level_names[k - 1] : "Body Text"), cap - 1); }
  else { k = (int)((w >> 12) & 15); strncpy(out, tr(k >= 1 && k <= 9 ? show_names[k - 1] : "All Levels"), cap - 1); }
  out[cap - 1] = 0;
}
/* the right-button menu in Outline view: promote, demote, expand, collapse */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  if (c->vmode != VM_OUTLINE || c->where != WPC_TEXT) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr("Promote"), WP_CTX_ID(ID_OL_PROMOTE), 1);
  wp_ctx_item(menu, tr("Demote"), WP_CTX_ID(ID_OL_DEMOTE), 1);
  wp_ctx_item(menu, tr("Expand"), WP_CTX_ID(ID_OL_EXPAND), 1);
  wp_ctx_item(menu, tr("Collapse"), WP_CTX_ID(ID_OL_COLLAPSE), 1);
}
/* keys (raw codes): Ctrl-Alt-Shift with the cursor keys everywhere; in Outline view also Alt-Shift with
   them, Tab and Shift-Tab (not in a table), Alt-Shift-+/- (keypad or main keys), Alt-Shift-1..9 and A */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  int sh = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, ct = (qual & IEQUALIFIER_CONTROL) != 0;
  int al = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0, ol = c->vmode == VM_OUTLINE;
  if ((code >= 0x4C && code <= 0x4F) && sh && al && (ct || ol)) {
    switch (code) {
    case 0x4C: return ID_OL_UP;
    case 0x4D: return ID_OL_DOWN;
    case 0x4E: return ID_OL_DEMOTE;
    case 0x4F: return ID_OL_PROMOTE;
    }
  }
  if (!ol) return 0;
  if (code == 0x42 && !al && !ct && !c->intable) return sh ? ID_OL_PROMOTE : ID_OL_DEMOTE;
  if (!(al && sh) || ct) return 0;
  if (code == 0x5E || code == 0x0C) return ID_OL_EXPAND;
  if (code == 0x4A || code == 0x0B) return ID_OL_COLLAPSE;
  if (code >= 0x01 && code <= 0x09) return ID_OL_SHOW1 + (code - 0x01);
  if (code == 0x20) return ID_OL_SHOWALL;
  return 0;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp11_package = {
  "WP11", ID_WP11_FIRST, ID_WP11_LAST,
  wp11_command, state, field, ctxmenu, key, NULL, NULL,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};
