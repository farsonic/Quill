/*
 * wp_chartkeep.c - WP15a Charts and SmartArt: read, draw, keep (docs/wordcraft-gap.md). This file belongs to WP15a: only that
 * package edits it. Its commands are ID_WP15A_FIRST .. ID_WP15A_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 */
#include <proto/exec.h>
#include "wphooks.h"
#include "../core/chart.h"

/* Charts are laid out with doubles (core/chart.c, smartart.c: their values are any real numbers).
   Quill is built without FPU code, so vbcc does double arithmetic through mathieeedoubbas.library
   (in ROM; it uses the FPU where there is one). Its base is opened here by a vbcc constructor, before
   main() runs and so before any document is read; without it charts are drawn as empty frames
   (chart_nofp). Only the basic library is needed: chart.c uses no transcendental functions. */
struct Library *MathIeeeDoubBasBase;
void _INIT_1_wp15a_math(void)
{
  MathIeeeDoubBasBase = OpenLibrary((CONST_STRPTR)"mathieeedoubbas.library", 0);
  chart_nofp = MathIeeeDoubBasBase == NULL;
}
void _EXIT_1_wp15a_math(void)
{
  if (MathIeeeDoubBasBase) CloseLibrary(MathIeeeDoubBasBase);
  MathIeeeDoubBasBase = NULL;
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp15a_command(ULONG id, LONG value)
{
  return WPR_DONE;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp15a_package = {
  "WP15a", ID_WP15A_FIRST, ID_WP15A_LAST,
  wp15a_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  NULL, 0
};
