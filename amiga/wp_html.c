/*
 * wp_html.c - WP28c HTML and Markdown (docs/wordcraft-gap.md). This file belongs to WP28c: only that
 * package edits it. Its commands are ID_WP28C_FIRST .. ID_WP28C_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The formats are core/html.c and core/markdown.c (their rows in core/docx.c's doc_formats[] put
 * .html, .htm, .md and .markdown in the Open and Save As requesters). Here: File > Save As Web
 * Page... and Save As Markdown..., copies that don't become the document's file (wp_rtf.c's helper).
 */
#include "wphooks.h"

enum { ID_HTML_SAVE = ID_WP28C_FIRST, ID_MD_SAVE };
int wp28_save_copy(const char *title, const char *ext, const char *pattern);     /* (wp_rtf.c) */

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp28c_command(ULONG id, LONG value)
{
  if (id == ID_HTML_SAVE) wp28_save_copy("Save as a web page", "html", "#?.(html|htm)");
  else if (id == ID_MD_SAVE) wp28_save_copy("Save as Markdown", "md", "#?.(md|markdown)");
  return WPR_DONE;
}

static const WpMenu menus[] = {
  { WM_FILE, { NM_ITEM, (STRPTR)"Save As Web Page...", 0, 0, 0, (APTR)ID_HTML_SAVE } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Save As Markdown...", 0, 0, 0, (APTR)ID_MD_SAVE } }
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp28c_package = {
  "WP28c", ID_WP28C_FIRST, ID_WP28C_LAST,
  wp28c_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  menus, 2
};
