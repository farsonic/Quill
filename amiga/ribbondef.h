/*
 * ribbondef.h - what the ribbon's tools are made of (ribbon.c's tabs, groups and items), shared so
 * that the work packages (amiga/wp_*.c, see wphooks.h) can describe ribbon groups of their own in
 * their own files. ribbon.c adds them to its tabs when the class is made.
 */
#ifndef QUILL_RIBBONDEF_H
#define QUILL_RIBBONDEF_H

#include <exec/types.h>
#include "../core/util.h"

/* item kinds: a button (a ReturnID of its id when clicked); a button with an arrow that opens its
   list; a field showing a value (font, size, indent...) that opens its list; the styles gallery; a
   plain caption. Sizes: a small button (16-pixel icon and label), a large one (24-pixel icon over
   its label, the group's full height), an icon alone */
enum { K_BUTTON, K_DROP, K_FIELD, K_GALLERY, K_LABEL };
enum { S_SMALL, S_LARGE, S_ICON };
#define F_ROW1 1                 /* rows mode: on the second row */
#define F_NEWCOL 2               /* columns mode: start a new column */
#define F_CHECK 4                /* a check box (ticked while its state is on) instead of an icon */
#define F_SPLIT 8                /* a split button: its icon and label do split_id()'s command (or Item.split), the arrow opens the list */

/* how a drop-down list's entries are drawn (RibbonList.kind) */
enum { PL_TEXT, PL_COLOR, PL_STYLES, PL_TSTYLES, PL_FONTS, PL_UNDERLINE, PL_LOOKSET, PL_LOOKTHEME, PL_LOOKCOLOR };
/* a drop-down list: its entries (Latin-1, shown through tr()), how many, how they are drawn (PL_TEXT
   or PL_COLOR for a package), and for PL_COLOR the 0xRRGGBB beside each (0xFF000000: automatic or
   none). The entry chosen reaches the item's command as its number (the value of wpN_command) */
typedef struct { const char *const *labels; int n; int kind; const ULONG *colors; } RibbonList;

/* item states (Item.st, Item.en): RB_ bits of quill.h (1-63), VM(m) = 100 + a view mode, or a work
   package's own bit: RB_WP(WPI_x, 0..31), set by its wpN_state() (wphooks.h) */
#define RB_WP_BASE 1024
#define RB_WP(pkg, bit) (RB_WP_BASE + 32 * (pkg) + (bit))

/* a tool: its kind and size (K_, S_), its icon (IC_, theme.h), its list (ribbon.c's L_; 0 for a
   package, which gives xlist instead), its label (Latin-1, through tr(); NULL for an icon alone), the
   command it returns (ID_), the states shown as checked and needed to be usable (0: none), F_
   flags, a field's width in pixels, and its help. The last three are for the work packages (leave
   them out, so 0, in ribbon.c's own tables): xlist, its own list; xicon, an icon drawn from a
   string in theme.c's icon language (used when icon is 0); split, a split button's direct command */
typedef struct {
  u8 kind, size, icon, list;
  const char *label;
  UWORD id;
  UWORD st, en;                  /* shown as checked, needed to be enabled (0: none) */
  u8 flags;
  UWORD fieldw;
  const char *help;
  const RibbonList *xlist;
  const char *xicon;
  UWORD split;
} Item;
/* a group's arrangement: items in columns (a large button, or small ones three to a column), in
   two rows (the Font and Paragraph groups) or the styles gallery; a group: its caption, how it is
   arranged, its items */
enum { G_COLS, G_ROWS, G_GALLERY };
typedef struct { const char *name; u8 mode; const Item *items; int n; } Group;

/* the tabs by their place in the strip (MUIA_Ribbon_Tab). File isn't a tab of tools; TAB_MAILINGS,
   TAB_DEVELOPER and TAB_DRAW are left out of the strip while no package has given them a group;
   the contextual TAB_OUTLINING, TAB_EQUATION, TAB_CHARTDESIGN and TAB_SMARTART show while their
   package's state word has WPS_TAB on (WP11, WP14b, WP15b, WP15c) */
enum { TAB_FILE, TAB_HOME, TAB_INSERT, TAB_DRAW, TAB_DESIGN, TAB_LAYOUT, TAB_REFERENCES, TAB_MAILINGS, TAB_REVIEW, TAB_VIEW,
       TAB_DEVELOPER, TAB_TABLEDESIGN, TAB_TABLELAYOUT, TAB_PICTURE, TAB_SHAPE, TAB_HEADFOOT, TAB_EDITTEXT,
       TAB_EQUATION, TAB_CHARTDESIGN, TAB_SMARTART, TAB_OUTLINING, TAB_N };
#define WPS_TAB 0                /* bit 0 of a package's state word: its contextual tab is shown */

/* a group a package adds to a tab (wphooks.h, WpPackage.groups): the tab (TAB_), where (the index of
   the tab's own group it goes before; -1 after the last), and the group */
typedef struct { u8 tab; signed char before; Group g; } WpGroup;

#endif
