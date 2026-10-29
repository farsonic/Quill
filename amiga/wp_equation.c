/*
 * wp_equation.c - WP14b Equation editor (docs/wordcraft-gap.md). This file belongs to WP14b: only that
 * package edits it. Its commands are ID_WP14B_FIRST .. ID_WP14B_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 */
#include "wphooks.h"

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp14b_command(ULONG id, LONG value)
{
  return WPR_DONE;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp14b_package = {
  "WP14b", ID_WP14B_FIRST, ID_WP14B_LAST,
  wp14b_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  NULL, 0
};
