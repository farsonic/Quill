/*
 * wp_smartart.c - WP15c Insert SmartArt and SmartArt Design (docs/wordcraft-gap.md). This file belongs to WP15c: only that
 * package edits it. Its commands are ID_WP15C_FIRST .. ID_WP15C_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 */
#include "wphooks.h"

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp15c_command(ULONG id, LONG value)
{
  return WPR_DONE;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp15c_package = {
  "WP15c", ID_WP15C_FIRST, ID_WP15C_LAST,
  wp15c_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  NULL, 0
};
