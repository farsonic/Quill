/*
 * wp_odt.c - WP28b ODT (docs/wordcraft-gap.md). This file belongs to WP28b: only that
 * package edits it. Its commands are ID_WP28B_FIRST .. ID_WP28B_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The format is core/odt.c (reading) and core/odtw.c (writing); its rows in core/docx.c's
 * doc_formats[] put .odt, .ott and .fodt (Flat ODT) in the Open and Save As requesters. Here: File >
 * Save As OpenDocument Text..., a copy that doesn't become the document's file (wp_rtf.c's helper);
 * a name ending in .fodt gets a Flat ODT.
 */
#include "wphooks.h"

enum { ID_ODT_SAVE = ID_WP28B_FIRST };
int wp28_save_copy(const char *title, const char *ext, const char *pattern);     /* (wp_rtf.c) */

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp28b_command(ULONG id, LONG value)
{
  if (id == ID_ODT_SAVE) wp28_save_copy("Save as OpenDocument Text", "odt", "#?.(odt|fodt)");
  return WPR_DONE;
}

static const WpMenu menus[] = {
  { WM_FILE, { NM_ITEM, (STRPTR)"Save As OpenDocument Text...", 0, 0, 0, (APTR)ID_ODT_SAVE } }
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp28b_package = {
  "WP28b", ID_WP28B_FIRST, ID_WP28B_LAST,
  wp28b_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  menus, 1
};
