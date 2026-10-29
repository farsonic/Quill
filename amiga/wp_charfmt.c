/*
 * wp_charfmt.c - WP9 Character formatting (docs/wordcraft-gap.md). This file belongs to WP9: only that
 * package edits it. Its commands are ID_WP9_FIRST .. ID_WP9_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 */
#include "wphooks.h"

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp9_command(ULONG id, LONG value)
{
  return WPR_DONE;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp9_package = {
  "WP9", ID_WP9_FIRST, ID_WP9_LAST,
  wp9_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  NULL, 0
};
