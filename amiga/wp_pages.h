/* wp_pages.h - WP22a's command ids (amiga/wp_pages.c), for main.c's ribbon lists and dialogs that
   hand over to it. This file belongs to WP22a */
#ifndef QUILL_WP_PAGES_H
#define QUILL_WP_PAGES_H

#include "ids.h"

enum {
  ID_PG_COLUMNS = ID_WP22A_FIRST,   /* the Columns window */
  ID_PG_COLLIST,                    /* the ribbon's Columns list: value 4 Left, 5 Right, 6 More Columns */
  ID_PG_COLGO, ID_PG_COLPRE, ID_PG_COLNUM,
  ID_PG_SETUP,                      /* Page Setup */
  ID_PG_SETUPGO, ID_PG_SETUPUPD,
  ID_PG_BORDERS,                    /* Page Borders */
  ID_PG_BORDERGO,
  ID_PG_WATERMARK,                  /* Printed Watermark */
  ID_PG_WMGO, ID_PG_WMPICK, ID_PG_WMUPD,
  ID_PG_HYPHOPTS,                   /* Hyphenation options */
  ID_PG_HYPHGO,
  ID_PG_MANUAL,                     /* Manual hyphenation */
  ID_PG_MYES, ID_PG_MNO, ID_PG_MUPD,
  ID_PG_LINENUM,                    /* Line Numbers (with Apply to) */
  ID_PG_LINENUMGO,
  ID_PG_HYPHLIST,                   /* the ribbon's Hyphenation list: value 2 Manual, 3 Options */
  ID_PG_JUSTIFY,                    /* vertical alignment Justified for the caret's section */
  ID_PG_LAST
};
int wp22a_command(ULONG id, LONG value);

#endif
