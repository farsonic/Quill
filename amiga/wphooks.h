/*
 * wphooks.h - how the work packages of docs/wordcraft-gap.md (WP1 ... WP31) plug into Quill without
 * editing the same lines of the shared files. WP0 made the scaffolding; the full notes are in the gap
 * document's "WP0 conventions" section.
 *
 * HOW TO ADD YOUR PACKAGE (WPn, file amiga/wp_<name>.c, already there with empty hooks)
 *
 *  Commands  Your ids are ID_WPn_FIRST .. ID_WPn_LAST (ids.h, 64 of them). Name them in your own file
 *            (enum { ID_FIELD_INSERT = ID_WP1_FIRST, ID_FIELD_UPDATE, ... }). Every menu item, ribbon
 *            button, list, context-menu item, key and dialog button with one of your ids ends up in
 *            your wpN_command(id, value): value is the entry chosen for a list (a ribbon K_DROP or
 *            K_FIELD item of yours, or a WP_CTX_LIST context submenu), else WPV_NONE. Return
 *            WPR_KEEPFOCUS when you opened a window whose gadgets should keep the keyboard, else
 *            WPR_DONE. Your dialog buttons: wp_dialog(..., okid) with okid one of your ids.
 *  Ribbon    Fill wpN_package.groups with WpGroup rows { TAB_x, before, { "Caption", G_COLS, items,
 *            count } } (ribbondef.h): your groups go into that tab before its own group number
 *            `before` (-1: at the end). Draw, Mailings, Developer and the contextual Equation, Chart
 *            Design, SmartArt Design and Outlining tabs start empty and stay hidden until a package
 *            gives them a group. Items: as ribbon.c's, with xlist (your own RibbonList), xicon (your own
 *            icon drawing, theme.c's icon language) and split (a split button's direct command).
 *  States    wpN_state(on, ctx) sets your 32 bits in *on (from the document and caret in ctx); use
 *            RB_WP(WPI_n, bit) as an item's st (shown checked) or en (needed to be enabled). Bit
 *            WPS_TAB (0) shows your contextual tab (WP11 Outlining, WP14b Equation, WP15b Chart
 *            Design, WP15c SmartArt Design). The view calls it whenever the caret or document changes.
 *            A K_FIELD item of yours shows the text wpN_field(id, st, out, cap) writes.
 *  Menus     Fill wpN_package.menus with WpMenu rows { WM_x, { NM_ITEM, "Label", "key"|0, flags, 0,
 *            (APTR)id } }: they are added at the end of that menu (File ... Layout), in package order.
 *            Begin with an NM_BARLABEL row of your own; prefer one NM_ITEM with NM_SUB rows when you
 *            have several (Intuition allows 63 items a menu). The menu bar keeps its 8 titles: a 9th
 *            does not fit 640 pixels in French, Spanish, Italian (measured with topaz 8 in WP0).
 *  Context   wpN_ctxmenu(menu, ctx) may add items to the right-button menu (ctx->where says what was
 *            clicked) with wp_ctx_item(menu, label, WP_CTX_ID(id), enabled) and wp_ctx_sub(...).
 *  Keys      wpN_key(code, qual, ctx) sees each raw key before the view's own handling (not in read
 *            mode, not with the Amiga keys, which are menu shortcuts): return one of your ids to run
 *            it through wpN_command, or 0 to leave the key alone.
 *  Startup   wpN_init() once after the application and first window exist; wpN_cleanup() at the end.
 *  Words     Every string through tr(), with its row in tools/lang/wp<n>.py (see tools/mklang.py);
 *            run python3 tools/mklang.py (it writes them into your section of amiga/lang_table.h)
 *            and python3 tools/langcheck.py amiga/wp_<name>.c to find strings without a row.
 *  Language  wpN_package.drop (after menus; leave it out if you have no windows of your own): close and
 *            dispose of your windows (OM_REMMEMBER from wp_app(), then MUI_DisposeObject, pointers NULL),
 *            as main.c's drop_windows() does with its own when the interface language changes, so they
 *            are made again in the new language the next time they open.
 *  Options   (WP26) Settings of yours go into the Options window's panes: wp_options_add() a static
 *            table of WpOption rows from your init hook (below); keep your own values in Quill.prefs
 *            with wp_pref() / wp_pref_set(), or let a row's key do it. Every command (yours too) can
 *            be put on the Quick Access Toolbar, a custom ribbon tab and a key by the user, and Tell
 *            Me finds it by its label in the current language: nothing to do for that.
 *  Tests     tests/test_wp<n>.c: your checks in test_wp<n>() with ok() from tests/testutil.h.
 *  Formats   (WP28a-d) core/docx.c's doc_formats[] has your row already; fill in its functions in
 *            your own core file (core/rtf.c, odt.c, html.c, markdown.c, docbin.c).
 *  .docx     core/docx.c: put calls to your own read_ and write_ functions after your package's
 *            marker line in the hook functions (docx_read_block_ext, docx_read_elem_ext, docx_read_parts_ext,
 *            docx_read_setting_ext, docx_write_parts_ext, docx_write_settings_ext); never edit a marker
 *            line itself, insert after it. Parts Quill doesn't read can be kept verbatim (core/keep.h).
 *
 * Only your own files are yours; in shared files (main.c, view.c, ribbon.c, core/doc.h, core/docx.c,
 * core/layout.c) keep edits small, in one place, and new struct fields at the end.
 */
#ifndef QUILL_WPHOOKS_H
#define QUILL_WPHOOKS_H

#include <libraries/gadtools.h>
#include "quill.h"
#include "ids.h"
#include "ribbondef.h"

#define WPV_NONE (-1L)           /* wpN_command's value when the command didn't come from a list */
enum { WPR_DONE, WPR_KEEPFOCUS };   /* what wpN_command returns */

/* what a hook is told about the document window: the document, the caret and the selection's other
   end (equal: no selection), the view mode (VM_), the floating object selected (Doc.images index,
   -1 none) and, for the right-button menu, what was clicked (WPC_) */
enum { WPC_TEXT, WPC_SPELLING, WPC_OBJECT, WPC_LINK };
typedef struct {
  Doc *doc;
  Pos caret, anchor;
  int hassel;
  int intable;
  int vmode;
  int obj;
  int where;
} WpCtx;

/* a menu item a package adds: which menu (WM_, the menu bar's titles in order) and the item */
enum { WM_FILE, WM_EDIT, WM_INSERT, WM_FORMAT, WM_TABLE, WM_VIEW, WM_REVIEW, WM_LAYOUT, WM_N };
typedef struct { UBYTE menu; struct NewMenu nm; } WpMenu;

/* a package: its tag, its id block, its hooks (any may be NULL) and what it adds to the ribbon and the
   menus. Each amiga/wp_<name>.c defines one, wpN_package; wphooks.c lists them all */
typedef struct {
  const char *tag;                                   /* "WP1" */
  ULONG first, last;                                 /* ID_WPn_FIRST, ID_WPn_LAST */
  int   (*command)(ULONG id, LONG value);            /* WPR_ */
  void  (*state)(ULONG *on, const WpCtx *c);         /* its 32 ribbon state bits */
  void  (*field)(ULONG id, const struct MUIP_Ribbon_State *st, char *out, size_t cap);   /* a K_FIELD's text */
  void  (*ctxmenu)(Object *menu, const WpCtx *c);    /* items for the right-button menu */
  ULONG (*key)(UWORD code, UWORD qual, const WpCtx *c);   /* one of its ids for this key, 0: not its key */
  void  (*init)(void);
  void  (*cleanup)(void);
  const WpGroup *groups; int ngroups;                /* ribbon groups to add */
  const WpMenu *menus; int nmenus;                   /* menu items to add */
  void  (*drop)(void);                               /* (WP26) the interface language changed: dispose of your windows */
} WpPackage;

/* the packages by WPI_ (NULL if out of range) */
const WpPackage *wp_package(int i);

/* ---- called by Quill's own files (each from one place) */
int   wp_is_list(ULONG id);                          /* a ribbon list's id of a package's (its choice has a value) */
int   wp_run(ULONG id, LONG value);                  /* main.c's command() and ribbon_choice(): to the id's package */
void  wp_state(ULONG *wpon, const WpCtx *c);         /* view.c: every package's state word */
void  wp_field_text(ULONG id, const struct MUIP_Ribbon_State *st, char *out, size_t cap);   /* ribbon.c */
void  wp_ctxmenu(Object *menu, const WpCtx *c);      /* view.c: the right-button menu */
ULONG wp_key(UWORD code, UWORD qual, const WpCtx *c);   /* view.c: a package's command for a key, or 0 */
void  wp_init(void), wp_cleanup(void);               /* main.c */
void  wp_drop(void);                                 /* main.c's drop_windows(): every package's drop hook */
/* main.c: the menu bar's items with every package's added (malloc'd once and kept; base itself if
   no package adds any or memory is short) */
struct NewMenu *wp_menus(struct NewMenu *base);

/* ---- for the packages: the right-button menu's items (user data as view.c's) */
#define WP_CTX_ID(id)      (0x20000000UL | (ULONG)(id))                       /* runs id */
#define WP_CTX_LIST(id, v) (0x40000000UL | ((ULONG)(id) << 8) | (ULONG)(v))   /* runs id with value v (0-255) */
void wp_ctx_item(Object *menu, const char *label, ULONG userdata, int enabled);   /* label NM_BARLABEL: a line */
void wp_ctx_sub(Object *menu, const char *label, ULONG id, const char *const *labels, int n);   /* WP_CTX_LIST(id, i) each */

/* ---- for the packages: main.c's application, the current document window and a few of its helpers */
Object *wp_app(void);                                /* the MUI application */
Object *wp_win(void);                                /* the current document window, its page view, its ribbon */
Object *wp_view(void);
Object *wp_ribbon(void);
Doc    *wp_doc(void);                                /* the current window's document */
Pos    *wp_caret(void);                              /* its caret (NULL off the end) */
int     wp_ask(const char *gadgets, const char *fmt, const char *arg);   /* MUI_Request over the window (tr()'d) */
/* a dialog window with contents and OK/Cancel, OK returning okid (one of yours), added to the
   application; made once and then opened and closed with MUIA_Window_Open */
Object *wp_dialog(const char *title, ULONG windowid, Object *contents, const char *oklabel, ULONG okid);
void    wp_latin1_utf8(const char *latin1, char *utf8, size_t cap);   /* gadget text to document text */
void    wp_story_close(void);                        /* back to the body if a header, note... is edited */
void    wp_refresh(void);                            /* the ribbon, status bar and rulers shown afresh */
/* the ASL file requester (title tr()'d), starting at path, which becomes the choice; pattern: an
   AmigaDOS pattern for the files shown ("#?.(png|jpg)"), NULL none. 1 if a file was chosen */
int     wp_file_request(int save, const char *title, char *path, size_t cap, const char *pattern);

/* ---- (WP26, amiga/wp_options.c) settings in the Options window (File > Options > More Options, or
   Tools in the View menu). A package adds a table of rows, usually from its init hook; the table must
   stay (static const). Rows show in their pane in the order added, each a check box, a cycle of
   choices, a number, a text field, a button or a heading/line of text. The window's OK writes every
   changed value (to *var or through set), then calls changed and saves Quill.prefs */
enum { OP_GENERAL, OP_DISPLAY, OP_PROOFING, OP_SAVE, OP_LANGUAGE, OP_ACCESSIBILITY, OP_ADVANCED, OP_NSETTINGS };
enum { OT_HEAD, OT_TEXT, OT_CHECK, OT_CYCLE, OT_NUMBER, OT_STRING, OT_BUTTON };
typedef struct {
  UBYTE pane, type;                 /* OP_, OT_ */
  const char *label;                /* English, tr()'d when shown (OT_HEAD: a section title, OT_TEXT: a line) */
  const char *key;                  /* the Quill.prefs key the framework keeps the value under (read when the
                                       row is added), or NULL: var or get/set keep it themselves */
  const char *const *choices;       /* OT_CYCLE: the choices, NULL-terminated (tr()'d) */
  LONG min, max, def;               /* OT_NUMBER's range; the default (with key: when Quill.prefs hasn't it) */
  LONG *var;                        /* the value (CHECK 0/1, CYCLE the choice, NUMBER), or NULL ... */
  LONG (*get)(void);                /* ... then these read and write it */
  void (*set)(LONG v);
  char *str; int cap;               /* OT_STRING: the text (Latin-1) and its buffer's size */
  void (*changed)(void);            /* after OK changed it (NULL: nothing more) */
  ULONG id;                         /* OT_BUTTON: the command it runs */
} WpOption;
void wp_options_add(const WpOption *rows, int n);
void wp_options_open(int pane);                      /* the window on a pane (OP_, or 7-9: the customizing panes) */
/* Quill.prefs lines for the packages ("key=value"; main.c keeps the lines it doesn't read itself) */
const char *wp_pref(const char *key);                /* NULL: not set */
void wp_pref_set(const char *key, const char *value);   /* saved at once; NULL removes it */
long wp_pref_num(const char *key, long def);
/* (main.c) Quill.prefs written now; the menu bar's items in English, before main.c translates them */
void wp_save_prefs(void);
void wp_menu_english(const struct NewMenu *nm);
/* (view.c) a selection made with the mouse, let go at screen point x, y: the mini toolbar */
void wp_minibar(LONG x, LONG y);
/* (wphooks.c) before every package's key hook: KeyTips and the user's own keys (0: not one) */
ULONG wp26_prekey(UWORD code, UWORD qual, const WpCtx *c);

#endif
