/*
 * wp_pdf.c - WP29 PDF export depth and page images (docs/wordcraft-gap.md). This file belongs to WP29: only that
 * package edits it. Its commands are ID_WP29_FIRST .. ID_WP29_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 */
#include "wphooks.h"

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp29_command(ULONG id, LONG value)
{
  return WPR_DONE;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp29_package = {
  "WP29", ID_WP29_FIRST, ID_WP29_LAST,
  wp29_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  NULL, 0
};
