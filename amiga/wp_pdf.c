/*
 * wp_pdf.c - WP29 PDF export depth and page images (docs/wordcraft-gap.md). This file belongs to WP29: only that
 * package edits it. Its commands are ID_WP29_FIRST .. ID_WP29_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Export Image (WordCraft's file.exportPng): the pages as pictures, PNG or IFF ILBM, drawn by
 * core/raster.c exactly as the PDF shows them. Export Options: which pages Export PDF and Export
 * Image write, the pictures' resolution and format, whether the headings make the PDF's bookmarks,
 * and the subject and keywords of its document properties (core/pdf.h's pdf_options). Both are on
 * the File page's Export section and at the end of the File menu. The PDF's subset fonts, Unicode
 * text, links and outline are core/pdf.c's.
 */
#include <string.h>
#include <stdio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/iffparse.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "../core/pdf.h"

enum { ID_PDFX_IMAGE = ID_WP29_FIRST, ID_PDFX_OPTIONS, ID_PDFX_OPTOK };
const char *wp_docpath(void);           /* main.c: the document's file ("" untitled) */

static Object *optwin, *o_range, *o_from, *o_to, *o_dpi, *o_fmt, *o_outline, *o_subject, *o_keywords;
static const UWORD dpis[] = { 72, 96, 150, 200, 300 };
#define NDPI (int)(sizeof dpis / sizeof dpis[0])

/* gadget text (Latin-1) to UTF-8 for the options, and back */
static void from_latin1(const char *s, char *out, size_t cap) { wp_latin1_utf8(s, out, cap); }
static void to_latin1(const char *s, char *out, size_t cap) { utf8_to_latin1(s, (u32)strlen(s), out, cap); }

/* the Export Options window, made once, showing pdf_options */
static void open_options(void)
{
  static const char *ranges[3], *fmts[3], *dpinames[NDPI + 1];
  static char dpitext[NDPI][16];
  char lat[130];
  int i, act = 2;
  if (!optwin) {
    Object *c;
    ranges[0] = tr("All pages"); ranges[1] = tr("Pages"); ranges[2] = NULL;
    fmts[0] = "PNG"; fmts[1] = "IFF ILBM"; fmts[2] = NULL;
    for (i = 0; i < NDPI; i++) { sprintf(dpitext[i], "%u dpi", (unsigned)dpis[i]); dpinames[i] = dpitext[i]; }
    dpinames[NDPI] = NULL;
    c = ColGroup(2),
      Child, Label1(tr("Page range")), Child, HGroup,
        Child, o_range = CycleObject, MUIA_Cycle_Entries, (ULONG)ranges, MUIA_CycleChain, 1, End,
        Child, o_from = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 6, MUIA_String_Integer, 1, MUIA_CycleChain, 1, End,
        Child, Label2(tr("to")),
        Child, o_to = StringObject, StringFrame, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 6, MUIA_String_Integer, 1, MUIA_CycleChain, 1, End,
      End,
      Child, Label1(tr("Picture resolution")), Child, o_dpi = CycleObject, MUIA_Cycle_Entries, (ULONG)dpinames, MUIA_CycleChain, 1, End,
      Child, Label1(tr("Picture format")), Child, o_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmts, MUIA_CycleChain, 1, End,
      Child, Label1(tr("Bookmarks from headings")), Child, HGroup, Child, o_outline = CheckMark(TRUE), Child, HSpace(0), End,
      Child, Label2(tr("Subject")), Child, o_subject = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
      Child, Label2(tr("Keywords")), Child, o_keywords = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
    End;
    if (!c || !(optwin = wp_dialog("Export Options", MAKE_ID('P','D','F','O'), c, "_OK", ID_PDFX_OPTOK))) return;
    DoMethod(o_range, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)o_from, 3, MUIM_Set, MUIA_Disabled, MUIV_NotTriggerValue);
    DoMethod(o_range, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)o_to, 3, MUIM_Set, MUIA_Disabled, MUIV_NotTriggerValue);
  }
  for (i = 0; i < NDPI; i++) if (dpis[i] == pdf_options.dpi) act = i;
  set(o_range, MUIA_Cycle_Active, pdf_options.first || pdf_options.last ? 1 : 0);
  set(o_from, MUIA_String_Integer, pdf_options.first ? pdf_options.first : 1);
  set(o_to, MUIA_String_Integer, pdf_options.last ? pdf_options.last : 1);
  set(o_from, MUIA_Disabled, !(pdf_options.first || pdf_options.last));
  set(o_to, MUIA_Disabled, !(pdf_options.first || pdf_options.last));
  set(o_dpi, MUIA_Cycle_Active, act);
  set(o_fmt, MUIA_Cycle_Active, pdf_options.format == PIMG_ILBM ? 1 : 0);
  set(o_outline, MUIA_Selected, pdf_options.outline ? TRUE : FALSE);
  to_latin1(pdf_options.subject, lat, sizeof lat); set(o_subject, MUIA_String_Contents, (ULONG)lat);
  to_latin1(pdf_options.keywords, lat, sizeof lat); set(o_keywords, MUIA_String_Contents, (ULONG)lat);
  set(optwin, MUIA_Window_Open, TRUE);
}
/* OK: the window's choices become pdf_options */
static void options_done(void)
{
  ULONG v = 0;
  STRPTR s = NULL;
  get(o_range, MUIA_Cycle_Active, &v);
  if (v) {
    ULONG a = 1, b = 1;
    get(o_from, MUIA_String_Integer, &a); get(o_to, MUIA_String_Integer, &b);
    if (!a) a = 1;
    if (b < a) b = a;
    pdf_options.first = a; pdf_options.last = b;
  } else pdf_options.first = pdf_options.last = 0;
  get(o_dpi, MUIA_Cycle_Active, &v); pdf_options.dpi = dpis[v < (ULONG)NDPI ? v : 2];
  get(o_fmt, MUIA_Cycle_Active, &v); pdf_options.format = v ? PIMG_ILBM : PIMG_PNG;
  get(o_outline, MUIA_Selected, &v); pdf_options.outline = v != 0;
  get(o_subject, MUIA_String_Contents, &s); from_latin1(s ? (const char *)s : "", pdf_options.subject, sizeof pdf_options.subject);
  s = NULL;
  get(o_keywords, MUIA_String_Contents, &s); from_latin1(s ? (const char *)s : "", pdf_options.keywords, sizeof pdf_options.keywords);
  set(optwin, MUIA_Window_Open, FALSE);
}
/* Export Image: the file next to the document, .png or .iff by the format chosen; several pages
   go to name-1.png, name-2.png... */
static void export_image(void)
{
  char path[512], *dot;
  const char *dp = wp_docpath(), *ext = pdf_options.format == PIMG_ILBM ? ".iff" : ".png";
  Layout *L = NULL;
  int n;
  strncpy(path, dp && dp[0] ? dp : "Untitled.docx", sizeof path - 8); path[sizeof path - 8] = 0;
  dot = strrchr(path, '.');
  if (dot && dot > (char *)FilePart((STRPTR)path)) strcpy(dot, ext); else strcat(path, ext);
  if (!wp_file_request(1, "Export as Picture", path, sizeof path - 8, "#?.(png|iff|ilbm)")) return;
  dot = strrchr(path, '.');
  if (!dot || dot < (char *)FilePart((STRPTR)path)) strcat(path, ext);
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  get(wp_view(), MUIA_Quill_Layout, &L);
  n = L ? page_images_save(L, path, &pdf_options) : -1;
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  if (n < 0) wp_ask("OK", "Quill could not write\n%s", path);
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp29_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_PDFX_IMAGE: export_image(); break;
  case ID_PDFX_OPTIONS: open_options(); return WPR_KEEPFOCUS;
  case ID_PDFX_OPTOK: options_done(); break;
  }
  return WPR_DONE;
}

/* the File menu: Export Image and Export Options, after a line */
static const WpMenu menus[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Export Image...", 0, 0, 0, (APTR)ID_PDFX_IMAGE } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Export Options...", 0, 0, 0, (APTR)ID_PDFX_OPTIONS } },
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp29_package = {
  "WP29", ID_WP29_FIRST, ID_WP29_LAST,
  wp29_command, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  menus, (int)(sizeof menus / sizeof menus[0])
};
