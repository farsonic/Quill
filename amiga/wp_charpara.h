/*
 * wp_charpara.h - WP9 (Character formatting, amiga/wp_charfmt.c) and WP10 (Paragraph and Home quick
 * commands, amiga/wp_para.c): their command ids, the ribbon lists Quill's own tabs use, and the few
 * calls Quill's shared files make into them (one line each: see the WP9/WP10 notes there). This
 * file belongs to WP9/WP10.
 */
#ifndef QUILL_WP_CHARPARA_H
#define QUILL_WP_CHARPARA_H

#include "wphooks.h"

/* ---- WP9's commands */
enum {
  ID_C_HIDDEN = ID_WP9_FIRST, ID_C_OUTLINE, ID_C_SHADOW, ID_C_EMBOSS, ID_C_ENGRAVE,   /* toggles on the selection (or the next typing) */
  ID_C_EFFECTS,              /* Text Effects and Typography list (C_EF_ entries) */
  ID_C_ULCOLOR,              /* Underline Colour list (value: 0xRRGGBB, 0xFF000000 automatic) */
  ID_C_SHADE,                /* Character Shading list (a colour, 0xFF000000 none) */
  ID_C_SHADETOGGLE,          /* Character Shading button: grey shading on or off */
  ID_C_ENCLOSE,              /* Enclose Characters window */
  ID_C_ENCGO,                /* its OK */
  ID_C_FONTWIN,              /* the Font window (for Underline Colour... in the menu) */
  ID_C_LAST
};
/* the Text Effects list's entries */
enum { C_EF_OUTLINE, C_EF_SHADOW, C_EF_EMBOSS, C_EF_ENGRAVE, C_EF_HIDDEN, C_EF_SHADE, C_EF_ULCOLOR, C_EF_ENCLOSE, C_EF_LIGSTD, C_EF_LIGNONE,
       C_EF_OLDSTYLE, C_EF_LINING, C_EF_CLEAR, C_EF_N };

/* ---- WP10's commands */
enum {
  ID_P_DISTRIBUTE = ID_WP10_FIRST,   /* Distributed alignment (Ctrl-Shift-J) */
  ID_P_SPACEBEFORE,          /* Add (Remove) Space Before Paragraph (Ctrl-0) */
  ID_P_SPACEAFTER,           /* Remove (Add) Space After Paragraph */
  ID_P_HLINE,                /* Horizontal Line: a bottom border */
  ID_P_HANG, ID_P_UNHANG,    /* Hanging Indent (Ctrl-T), Remove Hanging Indent (Ctrl-Shift-T) */
  ID_P_BULLETLIB,            /* the Bullets list (P_BU_ entries) */
  ID_P_NUMLIB,               /* the Numbering list (P_NU_ entries) */
  ID_P_BULLETDEF, ID_P_BULLETGO, ID_P_BULLETSYM,   /* Define New Bullet window, its OK, its Symbol... button */
  ID_P_NUMDEF, ID_P_NUMGO,   /* Define New Number Format window, its OK */
  ID_P_SORT, ID_P_SORTGO,    /* Sort Text window, its OK */
  ID_P_NBSP, ID_P_NBHYPHEN, ID_P_SOFTHYPHEN,       /* Ctrl-Shift-Space, Ctrl-Shift-minus, Ctrl-minus */
  ID_P_SYMBOL, ID_P_SYMINSERT, ID_P_SYMPICK, ID_P_SYMPAGE, ID_P_SYMCODE, ID_P_SYMSPECIAL,   /* the Symbol window and its parts */
  ID_P_PASTESPECIAL, ID_P_PSGO,                    /* Paste Special window, its OK */
  ID_P_COPYFMT, ID_P_PASTEFMT,                     /* Copy Formatting (Ctrl-Shift-C), Paste Formatting (Ctrl-Shift-V) */
  ID_P_CLIPPANE, ID_P_CLIPPASTE, ID_P_CLIPALL, ID_P_CLIPCLEAR, ID_P_CLIPDEL,   /* the Clipboard pane and its buttons */
  ID_P_PTOOLS,               /* the Paragraph Tools list (WordCraft #5043; P_PT_ entries) */
  ID_P_PT0,                  /* .. + P_PT_N - 1: its entries as menu items */
  ID_P_LAST = ID_P_PT0 + 12
};
/* WP10's state bits (RB_WP(WPI_10, n)) */
enum { P_ST_DISTRIBUTE = 1, P_ST_CLIPPANE = 2 };

/* the Paragraph Tools list's entries: core/paratools.h's nine tools on the selected paragraphs, then three
   of them on the whole document */
enum { P_PT_N = 12 };
/* the lists Quill's Home tab takes from WP9/WP10 (ribbon.c's g_font and g_para) */
extern const RibbonList wp9_effects_list, wp9_ulcolor_list, wp9_shade_list, wp10_bullet_list, wp10_number_list, wp10_ptools_list;
#define WP10_ICON_PTOOLS "ci F 3 2 6 7 L 7 2 7 14 L 10 2 10 14 L 7 2 13 2 ca L 1 14 4 14"
#define WP9_ICON_ENCLOSE "ci O 8 8 7 B A"
#define WP9_ICON_EFFECTS "ca B A ci L 2 14 6 14 L 10 14 14 14 ca O 3 3 1 O 13 4 1"
#define WP9_ICON_ULCOLOR "ci U U cr F 2 13 13 15"
#define WP9_ICON_SHADE "cd F 2 2 13 13 ci B A"
#define WP10_ICON_DISTRIBUTE "ci L 1 3 14 3 L 1 6 14 6 L 1 9 14 9 L 1 12 14 12 L 1 2 1 13 L 14 2 14 13"

/* ---- calls from Quill's shared files */
/* main.c's Font window: WP9's part of it (a group of effects), filled from the caret's format, and
   read on OK into n (the changes into *mask) */
Object *wp9_font_group(void);
void    wp9_font_fill(const CharFmt *f);
void    wp9_font_read(const CharFmt *was, CharFmt *n, unsigned *mask);
/* view.c: text was copied or cut (the Clipboard pane keeps it) */
void    wp10_copied(Doc *d, Pos a, Pos b);
/* main.c: a choice from one of Quill's own lists that WP10 added an entry to: the Paste list's
   Paste Special..., the Line Spacing list's space before and after, the Borders list's Horizontal
   Line, the Symbol list's More Symbols... (the entry's number past Quill's own) */
void    wp10_list_extra(ULONG id, LONG extra);

#endif
