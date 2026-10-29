/*
 * wp_rtf.c - WP28a RTF (docs/wordcraft-gap.md). This file belongs to WP28a: only that
 * package edits it. Its commands are ID_WP28A_FIRST .. ID_WP28A_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The RTF format itself is core/rtf.c (its row in core/docx.c's doc_formats[] puts .rtf in the
 * Open and Save As requesters). Here: File > Save As Rich Text..., a copy in RTF that doesn't become
 * the document's file (as Save As Text...), and the helper WP28c's Web Page and Markdown items share.
 */
#include <string.h>
#include <proto/dos.h>
#include "wphooks.h"

enum { ID_RTF_SAVE = ID_WP28A_FIRST };

/* a copy of the document in another format (ext: the name ending a name without one gets): asks
   for the name, and before replacing a file. The document stays as modified as it was. 1 if saved */
int wp28_save_copy(const char *title, const char *ext, const char *pattern)
{
  static char path[512];
  Doc *d;
  int dirty;
  BPTR l;
  wp_story_close();
  d = wp_doc();
  if (!d || !wp_file_request(1, title, path, sizeof path, pattern)) return 0;
  if (!strchr((const char *)FilePart((STRPTR)path), '.') && strlen(path) + strlen(ext) + 2 < sizeof path) { strcat(path, "."); strcat(path, ext); }
  if ((l = Lock((STRPTR)path, ACCESS_READ))) {
    UnLock(l);
    if (wp_ask("Replace|Cancel", "%s exists.\nReplace it?", path) != 1) return 0;
  }
  dirty = d->dirty;
  if (!doc_save_file(d, path)) { d->dirty = dirty; wp_ask("OK", "Quill could not save\n%s", path); return 0; }
  d->dirty = dirty;
  return 1;
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp28a_command(ULONG id, LONG value)
{
  if (id == ID_RTF_SAVE) wp28_save_copy("Save as Rich Text Format", "rtf", "#?.rtf");
  return WPR_DONE;
}

static const WpMenu menus[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Save As Rich Text...", 0, 0, 0, (APTR)ID_RTF_SAVE } }
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp28a_package = {
  "WP28a", ID_WP28A_FIRST, ID_WP28A_LAST,
  wp28a_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  menus, 2
};
