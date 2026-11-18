/*
 * wp_html.c - WP28c HTML and Markdown (docs/wordcraft-gap.md). This file belongs to WP28c: only that
 * package edits it. Its commands are ID_WP28C_FIRST .. ID_WP28C_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The formats are core/html.c and core/markdown.c (their rows in core/docx.c's doc_formats[] put
 * .html, .htm, .md and .markdown in the Open and Save As requesters). Here: File > Save As Web
 * Page... and Save As Markdown..., copies that don't become the document's file (wp_rtf.c's helper).
 * (WP28e, 2026-11) View > Markdown Source: the document's Markdown shown and edited as text in the
 * window, in the document's place (core/markdown.c md_source_doc); clicked again, the text is read as
 * Markdown and becomes the document (md_apply_source), or the document comes back as it was if the text
 * wasn't changed. Saving meanwhile as Markdown writes the text as it is.
 */
#include <string.h>
#include <proto/intuition.h>
#include "wphooks.h"
#include "../core/fmtio.h"

enum { ID_HTML_SAVE = ID_WP28C_FIRST, ID_MD_SAVE, ID_MD_SOURCE };
enum { MS_SOURCE = 1 };              /* state bit: the window shows a Markdown source */
int wp28_save_copy(const char *title, const char *ext, const char *pattern);     /* (wp_rtf.c) */
Doc *wp_swap_doc(Doc *d, int changed);                                           /* (main.c) */

/* the source views open: the source shown, the document it was made from (kept to come back to), the
   source's version then, and whether the document had unsaved changes */
#define MAXSRC 8
static struct { Doc *src, *orig; u32 ver; ULONG changed; } srcs[MAXSRC];

static void md_source(void)
{
  Doc *d = wp_doc(), *e, *old;
  int k, at = -1;
  if (!d) return;
  for (k = 0; k < MAXSRC; k++) if (srcs[k].src == d && d->mdsource) at = k;
  if (d->mdsource) {
    /* back to the document: unchanged, the one it was; else the text read as Markdown */
    if (at >= 0 && srcs[at].orig && d->ver == srcs[at].ver) {
      if ((old = wp_swap_doc(srcs[at].orig, (int)srcs[at].changed))) doc_free(old);
    } else {
      char err[200];
      unsigned lost = 0;
      if (!(e = md_apply_source(d, &lost, err, sizeof err))) { wp_ask("OK", "%s", err); return; }
      if (!(old = wp_swap_doc(e, 1))) { doc_free(e); DisplayBeep(NULL); return; }
      doc_free(old);
      if (at >= 0 && srcs[at].orig) doc_free(srcs[at].orig);
    }
    if (at >= 0) memset(&srcs[at], 0, sizeof srcs[at]);
    wp_refresh();
    return;
  }
  for (k = 0; k < MAXSRC && srcs[k].src; k++) ;
  if (k == MAXSRC) { DisplayBeep(NULL); return; }
  if (!(e = md_source_doc(d))) { DisplayBeep(NULL); return; }
  {
    ULONG changed = 0;
    int j;
    get(wp_view(), MUIA_Quill_Changed, &changed);
    for (j = 0; j < MAXSRC; j++) if (srcs[j].src == e) memset(&srcs[j], 0, sizeof srcs[j]);   /* (a closed window's, at the same address) */
    if (!(old = wp_swap_doc(e, 0))) { doc_free(e); wp_ask("OK", "This document is open in another window too.\nClose that window first.%s", ""); return; }
    srcs[k].src = e; srcs[k].orig = old; srcs[k].ver = e->ver; srcs[k].changed = changed;
  }
  wp_refresh();
}
static void state(ULONG *on, const WpCtx *c)
{
  if (c->doc && c->doc->mdsource) *on |= 1UL << MS_SOURCE;
}
static void cleanup(void)
{
  int k;
  for (k = 0; k < MAXSRC; k++) if (srcs[k].orig) { doc_free(srcs[k].orig); srcs[k].orig = NULL; }
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp28c_command(ULONG id, LONG value)
{
  if (id == ID_HTML_SAVE) wp28_save_copy("Save as a web page", "html", "#?.(html|htm)");
  else if (id == ID_MD_SAVE) wp28_save_copy("Save as Markdown", "md", "#?.(md|markdown)");
  else if (id == ID_MD_SOURCE) md_source();
  return WPR_DONE;
}

#define I_MDSRC "ci R 1 3 14 12 ca L 3 10 3 5 L 3 5 5 8 L 5 8 7 5 L 7 5 7 10 L 11 5 11 10 L 9 8 11 10 L 13 8 11 10"
static const Item source_items[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Markdown Source", ID_MD_SOURCE, RB_WP(WPI_28C, MS_SOURCE), 0, 0, 0, "The document's Markdown as text to edit (again: read back as the document)", NULL, I_MDSRC } };
static const WpGroup groups[] = {
  { TAB_VIEW, 1, { "Source", G_COLS, source_items, 1 } } };
static const WpMenu menus[] = {
  { WM_FILE, { NM_ITEM, (STRPTR)"Save As Web Page...", 0, 0, 0, (APTR)ID_HTML_SAVE } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Save As Markdown...", 0, 0, 0, (APTR)ID_MD_SAVE } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Markdown Source", 0, 0, 0, (APTR)ID_MD_SOURCE } }
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp28c_package = {
  "WP28c", ID_WP28C_FIRST, ID_WP28C_LAST,
  wp28c_command, state, NULL, NULL, NULL, NULL, cleanup,
  groups, 1,
  menus, 3
};
