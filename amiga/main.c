/*
 * main.c - Quill: a word processor for the Amiga (68040+, MUI). The application: window, menus,
 * toolbar, files. The page view is view.c; documents, layout and file formats are the portable
 * core in ../core.
 *
 * How it hangs together, for those new to MUI: everything the user does arrives as an ID (ids.h).
 * Menu items carry theirs in menus[] below; buttons and other gadgets are given theirs with a
 * MUIM_Notify that does MUIM_Application_ReturnID. main()'s loop calls MUIM_Application_NewInput,
 * which deals with the Intuition events and returns the next ID (0: none), then Wait()s for more;
 * the ID goes to command(), ribbon_choice() or backstage_command(), which turn most of them into a
 * MUIM_Quill_Command for the page view (view.c does the editing). Dialog windows are made the first
 * time they are opened and after that only hidden (see open_find). Each document has a window of
 * its own: see DocWin, near the end, for how the globals below follow the window being worked in.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "wp_pages.h"
/* the dialogs' labels: Quill's in the dark theme (MUI's own are drawn black) */
#undef Label
#undef Label1
#undef Label2
#undef LLabel1
#undef LLabel
#define Label(l)   QLabel(l, 0)
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)
#define LLabel(l)  QLabel(l, MUIO_Label_LeftAligned)
#include "../core/pdf.h"
#include "../core/docx.h"
#include "../core/template.h"
#include "../core/spell.h"
#include "../core/hyph.h"
#include "../core/table.h"
#include "../core/tstyle.h"

const char verstag[] = "$VER: Quill 0.14 (26.10.2026) a word processor for the Amiga (MUI)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *MUIMasterBase, *LayersBase, *DiskfontBase, *KeymapBase, *UtilityBase;
extern struct WBStartup *_WBenchMsg;

#include "ids.h"
#ifndef MUIA_Window_DisableKeys
#define MUIA_Window_DisableKeys 0x80424c36UL   /* in MUI 3.8, undocumented: MUIKEYF_ keys the window ignores */
#endif
#include "lang.h"
#include "theme.h"
#include "splash.h"
#include "sysfonts.h"
#include "wphooks.h"
#include "wp_charpara.h"                /* (WP9, WP10) */
#include "wp_find.h"                  /* WP12: its ids, for Find's, Select's and Go To's entries */
void wp12_note(ULONG id, LONG v);       /* WP12 (wp_find.c): each command as it starts, for Repeat */
void wp_prefs_keep(const char *line);   /* WP26 (wp_options.c): Quill.prefs lines main.c doesn't read, kept */
void wp_prefs_write(FILE *f);           /* ... and written back */
void wp_ui_set(int metric, int dark, int lang, int large, const char *name);   /* (WP26: below backstage_command) */

static struct NewMenu menus[] = {
  { NM_TITLE, (STRPTR)"File", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"New", (STRPTR)"N", 0, 0, (APTR)ID_NEW },
  { NM_ITEM, (STRPTR)"Open...", (STRPTR)"O", 0, 0, (APTR)ID_OPEN },
  { NM_ITEM, (STRPTR)"Close", (STRPTR)"W", 0, 0, (APTR)ID_CLOSE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)ID_SAVE },
  { NM_ITEM, (STRPTR)"Save As...", (STRPTR)"A", 0, 0, (APTR)ID_SAVEAS },
  { NM_ITEM, (STRPTR)"Save As Text...", 0, 0, 0, (APTR)ID_SAVETXT },
  { NM_ITEM, (STRPTR)"Export PDF...", (STRPTR)"D", 0, 0, (APTR)ID_PDF },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Print...", 0, 0, 0, (APTR)ID_PRINT },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Next Window", (STRPTR)"`", 0, 0, (APTR)ID_NEXTWIN },
  { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)ID_ABOUT },
  { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)ID_QUIT },
  { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Undo", (STRPTR)"Z", 0, 0, (APTR)ID_UNDO },
  { NM_ITEM, (STRPTR)"Redo", (STRPTR)"Y", 0, 0, (APTR)ID_REDO },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Cut", (STRPTR)"X", 0, 0, (APTR)ID_CUT },
  { NM_ITEM, (STRPTR)"Copy", (STRPTR)"C", 0, 0, (APTR)ID_COPY },
  { NM_ITEM, (STRPTR)"Paste", (STRPTR)"V", 0, 0, (APTR)ID_PASTE },
  { NM_ITEM, (STRPTR)"Paste Special", 0, 0, 0, 0 },
  { NM_SUB, (STRPTR)"Keep Source Formatting", 0, 0, 0, (APTR)ID_PASTEKEEP },
  { NM_SUB, (STRPTR)"Merge Formatting", 0, 0, 0, (APTR)ID_PASTEMERGE },
  { NM_SUB, (STRPTR)"Keep Text Only", 0, 0, 0, (APTR)ID_PASTETEXT },
  { NM_ITEM, (STRPTR)"Select All", (STRPTR)"L", 0, 0, (APTR)ID_SELALL },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Find and Replace...", (STRPTR)"F", 0, 0, (APTR)ID_FIND },
  { NM_ITEM, (STRPTR)"Find Next", (STRPTR)"G", 0, 0, (APTR)ID_FINDNEXT },
  { NM_ITEM, (STRPTR)"Go To...", 0, 0, 0, (APTR)ID_GOTO },
  { NM_TITLE, (STRPTR)"Insert", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Picture...", 0, 0, 0, (APTR)ID_PICTURE },
  { NM_ITEM, (STRPTR)"Shape", 0, 0, 0, 0 },
  { NM_SUB, (STRPTR)"Rectangle", 0, 0, 0, (APTR)(ID_MSHAPE + 0) },
  { NM_SUB, (STRPTR)"Rounded Rectangle", 0, 0, 0, (APTR)(ID_MSHAPE + 1) },
  { NM_SUB, (STRPTR)"Oval", 0, 0, 0, (APTR)(ID_MSHAPE + 2) },
  { NM_SUB, (STRPTR)"Line", 0, 0, 0, (APTR)(ID_MSHAPE + 3) },
  { NM_SUB, (STRPTR)"Arrow", 0, 0, 0, (APTR)(ID_MSHAPE + 4) },
  { NM_SUB, (STRPTR)"Triangle", 0, 0, 0, (APTR)(ID_MSHAPE + 5) },
  { NM_SUB, (STRPTR)"Diamond", 0, 0, 0, (APTR)(ID_MSHAPE + 6) },
  { NM_SUB, (STRPTR)"Right Arrow", 0, 0, 0, (APTR)(ID_MSHAPE + 7) },
  { NM_SUB, (STRPTR)"Star", 0, 0, 0, (APTR)(ID_MSHAPE + 8) },
  { NM_ITEM, (STRPTR)"Text Box", 0, 0, 0, (APTR)ID_TEXTBOX },
  { NM_ITEM, (STRPTR)"Link...", 0, 0, 0, (APTR)ID_LINK },
  { NM_ITEM, (STRPTR)"Bookmark...", 0, 0, 0, (APTR)ID_BOOKMARK },
  { NM_ITEM, (STRPTR)"Cross-reference...", 0, 0, 0, (APTR)ID_XREF },
  { NM_ITEM, (STRPTR)"Footnote", 0, 0, 0, (APTR)ID_NOTE },
  { NM_ITEM, (STRPTR)"Table...", (STRPTR)"T", 0, 0, (APTR)ID_TABLE },
  { NM_ITEM, (STRPTR)"Page Break", (STRPTR)"P", 0, 0, (APTR)ID_PAGEBREAK },
  { NM_ITEM, (STRPTR)"Blank Page", 0, 0, 0, (APTR)ID_BLANKPAGE },
  { NM_ITEM, (STRPTR)"Line Break", 0, 0, 0, (APTR)ID_LINEBREAK },
  { NM_ITEM, (STRPTR)"Date", 0, 0, 0, (APTR)ID_DATE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Edit Header", (STRPTR)"H", 0, 0, (APTR)ID_HEADFOOT },
  { NM_ITEM, (STRPTR)"Edit Footer", 0, 0, 0, (APTR)ID_HFFOOTER },
  { NM_TITLE, (STRPTR)"Format", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Font...", 0, 0, 0, (APTR)ID_FONTDLG },
  { NM_ITEM, (STRPTR)"Paragraph...", 0, 0, 0, (APTR)ID_PARADLG },
  { NM_ITEM, (STRPTR)"Borders and Shading...", 0, 0, 0, (APTR)ID_BORDERSDLG },
  { NM_ITEM, (STRPTR)"Styles...", 0, 0, 0, (APTR)ID_STYLESPANE },
  { NM_ITEM, (STRPTR)"New Style...", 0, 0, 0, (APTR)ID_NEWSTYLE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Bold", (STRPTR)"B", 0, 0, (APTR)ID_BOLD },
  { NM_ITEM, (STRPTR)"Italic", (STRPTR)"I", 0, 0, (APTR)ID_ITALIC },
  { NM_ITEM, (STRPTR)"Underline", (STRPTR)"U", 0, 0, (APTR)ID_UNDERLINE },
  { NM_ITEM, (STRPTR)"Strikethrough", 0, 0, 0, (APTR)ID_STRIKE },
  { NM_ITEM, (STRPTR)"Character Border", 0, 0, 0, (APTR)ID_CBORDER },
  { NM_ITEM, (STRPTR)"Superscript", (STRPTR)"+", 0, 0, (APTR)ID_SUPER },
  { NM_ITEM, (STRPTR)"Subscript", (STRPTR)"-", 0, 0, (APTR)ID_SUB },
  { NM_ITEM, (STRPTR)"All Caps", 0, 0, 0, (APTR)ID_CAPS },
  { NM_ITEM, (STRPTR)"Grow Font", (STRPTR)">", 0, 0, (APTR)ID_GROW },
  { NM_ITEM, (STRPTR)"Shrink Font", (STRPTR)"<", 0, 0, (APTR)ID_SHRINK },
  { NM_ITEM, (STRPTR)"Clear Formatting", 0, 0, 0, (APTR)ID_CLEARFMT },
  { NM_ITEM, (STRPTR)"Format Painter", 0, 0, 0, (APTR)ID_BRUSH },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Align Left", (STRPTR)"[", 0, 0, (APTR)ID_LEFT },
  { NM_ITEM, (STRPTR)"Centre", (STRPTR)"E", 0, 0, (APTR)ID_CENTER },
  { NM_ITEM, (STRPTR)"Align Right", (STRPTR)"]", 0, 0, (APTR)ID_RIGHT },
  { NM_ITEM, (STRPTR)"Justify", (STRPTR)"J", 0, 0, (APTR)ID_JUSTIFY },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Line Spacing", 0, 0, 0, 0 },
  { NM_SUB, (STRPTR)"Single", (STRPTR)"1", 0, 0, (APTR)ID_SINGLE },
  { NM_SUB, (STRPTR)"1.5 Lines", (STRPTR)"5", 0, 0, (APTR)ID_ONEHALF },
  { NM_SUB, (STRPTR)"Double", (STRPTR)"2", 0, 0, (APTR)ID_DOUBLE },
  { NM_ITEM, (STRPTR)"Bullets", 0, 0, 0, (APTR)ID_BULLETS },
  { NM_ITEM, (STRPTR)"Numbering", 0, 0, 0, (APTR)ID_NUMBERS },
  { NM_ITEM, (STRPTR)"Define New List...", 0, 0, 0, (APTR)ID_LISTDEF },
  { NM_ITEM, (STRPTR)"Set Numbering Value...", 0, 0, 0, (APTR)ID_NUMVAL },
  { NM_ITEM, (STRPTR)"Increase Indent", (STRPTR)"M", 0, 0, (APTR)ID_INDENT },
  { NM_ITEM, (STRPTR)"Decrease Indent", (STRPTR)"K", 0, 0, (APTR)ID_OUTDENT },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Picture Size...", 0, 0, 0, (APTR)ID_PICSIZE },
  { NM_ITEM, (STRPTR)"Picture or Shape...", 0, 0, 0, (APTR)ID_OBJLAYOUT },
  { NM_TITLE, (STRPTR)"Table", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Insert Table...", 0, 0, 0, (APTR)ID_TABLE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Insert Row Above", 0, 0, 0, (APTR)ID_ROW_ABOVE },
  { NM_ITEM, (STRPTR)"Insert Row Below", 0, 0, 0, (APTR)ID_ROW_BELOW },
  { NM_ITEM, (STRPTR)"Insert Column Left", 0, 0, 0, (APTR)ID_COL_LEFT },
  { NM_ITEM, (STRPTR)"Insert Column Right", 0, 0, 0, (APTR)ID_COL_RIGHT },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Delete Row", 0, 0, 0, (APTR)ID_DEL_ROW },
  { NM_ITEM, (STRPTR)"Delete Column", 0, 0, 0, (APTR)ID_DEL_COL },
  { NM_ITEM, (STRPTR)"Delete Table", 0, 0, 0, (APTR)ID_DEL_TABLE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Heading Row", 0, 0, 0, (APTR)ID_HEADERROW },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Merge Cells", 0, 0, 0, (APTR)ID_MERGE },
  { NM_ITEM, (STRPTR)"Split Cells...", 0, 0, 0, (APTR)ID_SPLIT },
  { NM_ITEM, (STRPTR)"Split Table", 0, 0, 0, (APTR)ID_SPLITTABLE },
  { NM_ITEM, (STRPTR)"Table Properties...", 0, 0, 0, (APTR)ID_TPROPS },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Sort...", 0, 0, 0, (APTR)ID_TSORT },
  { NM_ITEM, (STRPTR)"Formula...", 0, 0, 0, (APTR)ID_FORMULA },
  { NM_ITEM, (STRPTR)"Convert Table to Text...", 0, 0, 0, (APTR)ID_TOTEXT },
  { NM_ITEM, (STRPTR)"Convert Text to Table...", 0, 0, 0, (APTR)ID_TOTABLE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"View Gridlines", 0, 0, 0, (APTR)ID_TGRID },
  { NM_TITLE, (STRPTR)"View", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Read Mode", 0, 0, 0, (APTR)ID_VREAD },
  { NM_ITEM, (STRPTR)"Print Layout", 0, 0, 0, (APTR)ID_VPRINT },
  { NM_ITEM, (STRPTR)"Web Layout", 0, 0, 0, (APTR)ID_VWEB },
  { NM_ITEM, (STRPTR)"Draft", 0, 0, 0, (APTR)ID_VDRAFT },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Fit Page Width", (STRPTR)"0", 0, 0, (APTR)ID_ZOOMFIT },
  { NM_ITEM, (STRPTR)"75%", 0, 0, 0, (APTR)ID_ZOOM75 },
  { NM_ITEM, (STRPTR)"100%", 0, 0, 0, (APTR)ID_ZOOM100 },
  { NM_ITEM, (STRPTR)"125%", 0, 0, 0, (APTR)ID_ZOOM125 },
  { NM_ITEM, (STRPTR)"150%", 0, 0, 0, (APTR)ID_ZOOM150 },
  { NM_ITEM, (STRPTR)"200%", 0, 0, 0, (APTR)ID_ZOOM200 },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Headings", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)ID_OUTLINE },
  { NM_ITEM, (STRPTR)"Ruler", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)ID_RULER },
  { NM_ITEM, (STRPTR)"Formatting Marks", (STRPTR)"*", 0, 0, (APTR)ID_MARKS },
  { NM_ITEM, (STRPTR)"Gridlines", 0, 0, 0, (APTR)ID_GRID },
  { NM_ITEM, (STRPTR)"Dark Mode", 0, 0, 0, (APTR)ID_DARKPAGE },
  { NM_TITLE, (STRPTR)"Review", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Spelling...", (STRPTR)"=", 0, 0, (APTR)ID_SPELL },
  { NM_ITEM, (STRPTR)"Word Count...", 0, 0, 0, (APTR)ID_WORDCOUNT },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"New Comment...", 0, 0, 0, (APTR)ID_COMMENT },
  { NM_ITEM, (STRPTR)"Delete Comment", 0, 0, 0, (APTR)ID_DELCOMMENT },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Track Changes", 0, 0, 0, (APTR)ID_TRACK },
  { NM_ITEM, (STRPTR)"Next Change", 0, 0, 0, (APTR)ID_NEXTCHANGE },
  { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Table of Contents", 0, 0, 0, (APTR)ID_TOC },
  { NM_ITEM, (STRPTR)"Remove Table of Contents", 0, 0, 0, (APTR)ID_REMOVETOC },
  { NM_ITEM, (STRPTR)"Endnote", 0, 0, 0, (APTR)ID_ENDNOTE },
  { NM_TITLE, (STRPTR)"Layout", 0, 0, 0, 0 },
  { NM_ITEM, (STRPTR)"Paper", 0, 0, 0, 0 },
  { NM_SUB, (STRPTR)"A4", 0, CHECKIT | CHECKED, ~1 & 15, (APTR)ID_PAPER0 },
  { NM_SUB, (STRPTR)"US Letter", 0, CHECKIT, ~2 & 15, (APTR)ID_PAPER1 },
  { NM_SUB, (STRPTR)"A5", 0, CHECKIT, ~4 & 15, (APTR)ID_PAPER2 },
  { NM_SUB, (STRPTR)"US Legal", 0, CHECKIT, ~8 & 15, (APTR)ID_PAPER3 },
  { NM_ITEM, (STRPTR)"Orientation", 0, 0, 0, 0 },
  { NM_SUB, (STRPTR)"Portrait", 0, CHECKIT | CHECKED, 2, (APTR)ID_PORTRAIT },
  { NM_SUB, (STRPTR)"Landscape", 0, CHECKIT, 1, (APTR)ID_LANDSCAPE },
  { NM_ITEM, (STRPTR)"Margins", 0, 0, 0, 0 },
  { NM_SUB, (STRPTR)"Normal (2.54 cm)", 0, CHECKIT | CHECKED, ~1 & 15, (APTR)ID_MARGIN0 },
  { NM_SUB, (STRPTR)"Narrow (1.27 cm)", 0, CHECKIT, ~2 & 15, (APTR)ID_MARGIN1 },
  { NM_SUB, (STRPTR)"Moderate (1.91 cm sides)", 0, CHECKIT, ~4 & 15, (APTR)ID_MARGIN2 },
  { NM_SUB, (STRPTR)"Wide (5.08 cm sides)", 0, CHECKIT, ~8 & 15, (APTR)ID_MARGIN3 },
  { NM_ITEM, (STRPTR)"Page Setup...", 0, 0, 0, (APTR)ID_PAGESETUPDLG },
  { NM_END, 0, 0, 0, 0, 0 }
};
static struct NewMenu *menubar = menus;   /* menus with the work packages' items (wp_menus, in main()) */

static Object *app, *win, *view, *findwin, *findstr, *ruler, *vruler, *navgrp, *ribbon, *statusbar;
static Object *tablewin, *rowsl, *colsl, *strip, *backstage, *workarea, *pages;
/* the controls the page view keeps up to date (rulers, ribbon, scroll bars...). Each document
   window has its own (in its DocWin, below); tb is the one of the window being worked in */
static struct Toolbar *tbp;
#define tb (*tbp)
static Object *vscroll, *hscroll;       /* the page's scroll bars, down and across */
/* several documents, each in a window of its own (see DocWin below) */
static int  win_new(Doc *d, const char *path);
static void win_close_current(void);
static void win_reopen_all(void), win_share_units(void);
static void win_next(void);
static int  doc_untouched(void);
static char *title_store(void);
static Object *lkwin;                   /* the Link window (made when first wanted) */
static void open_link(void), link_done(void), link_open(void), link_copy(void);
static Object *gotowin, *dcwin, *lnwin, *mcwin;   /* Go To, Drop Cap, Line Numbers, More Colours (made when first wanted) */
static void open_goto(void), goto_done(void), open_dropcap(void), dropcap_done(void), open_linenum(void);
static void open_morecol(ULONG id), morecol_done(void), remove_headfoot(int footer), add_text(int level);
static void open_pagesetup(void);
static Object *spwin2;                  /* the Selection Pane (made when first wanted) */
static void open_selpane(void), selpane_fill(void), selpane_pick(void);
static Object *bmwin, *xrwin;           /* the Bookmark and Cross-reference windows */
static Object *pfwin;                   /* Page Number Format */
static void page_number(int where), pgfmt_done(void), remove_page_numbers(void), open_pgfmt(void), cover_page(LONG v);
static void open_bookmarks(void), bm_fill(void), bm_command(ULONG id), open_xref(void), xr_fill(void), xr_insert(void);
static ULONG uibits = RBIT(RB_RULER) | RBIT(RB_NAVPANE) | RBIT(RB_SHOWCOMMENTS);   /* what the ribbon shows as on that the application keeps */
static void ui_bit(int b, int on);
static int focus, chrome_bare;           /* focus (the status bar's): the page alone; the tools hidden now */
static void chrome(void);
static void open_watermark(void);
static FontSet fonts;
static Doc *doc;
static char docpath[512];
static char title[600];
static char findtext[256];
FILE *qlog;                             /* DEBUG=file: a trace of commands, for finding problems */

/* the ribbon's choices (its lists, ribbon.c) */
static const LONG spacings[] = { 240, 276, 360, 480, 600, 720 };
static const i32 papers[][2] = { { 11906, 16838 }, { 12240, 15840 }, { 8391, 11906 }, { 12240, 20160 }, { 16838, 23811 },
                                 { 5953, 8391 }, { 9979, 14175 }, { 10440, 15120 }, { 15840, 24480 } };   /* (the Size list's order) */
#define NPAPERS (int)(sizeof papers / sizeof papers[0])
static const i32 margins[][2] = { { 1440, 1440 }, { 720, 720 }, { 1440, 1080 }, { 1440, 2880 }, { 1440, 1797 } };    /* top/bottom, left/right */
#define NMARGINS (int)(sizeof margins / sizeof margins[0])
static const LONG zooms[] = { 0, 50, 75, 100, 125, 150, 200 };
static const i32 indents[] = { 0, 283, 567, 850, 1134, 1417, 1701, 2268 };
static const i32 spaces[] = { 0, 60, 120, 160, 200, 240, 360, 480 };
static const u32 symbols[] = { 0xA9, 0xAE, 0x2122, 0xA7, 0xB6, 0x20AC, 0xA3, 0xB0, 0xB1, 0xD7, 0xF7, 0xBD, 0xBC, 0x2014, 0x2013, 0x2026, 0x2022 };

/* the documents opened or saved lately (PROGDIR:Quill.recent, a path a line) and the options
   (PROGDIR:Quill.prefs) */
#define NRECENT 10
static char recent[NRECENT][256];
static const char *recentp[NRECENT];
static int nrecent;
static char username[64];
static int pref_grid, pref_darkpage, pref_dark, pref_lang = -1, pref_large;   /* gridlines, the dark page, the dark interface, the language (-1: the system's) */
/* the window: nearly filling the screen (0), where it was last closed (1) or where set (2) */
enum { WIN_FILL, WIN_LAST, WIN_FIXED };
static int pref_win, pref_noaa, pref_nohint;          /* (pref_noaa: aatext=off, Quill's fonts not given to AAText) */
static LONG win_box[4];                 /* left, top, width, height (width 0: none yet) */
static signed char pref_look[4] = { -1, -1, -1, -1 };   /* Set as Default: new documents' style set, fonts, spacing, colours */
char pub_screen[64];
/* the recent list read back from PROGDIR:Quill.recent at startup (a missing file: an empty list) */
static void load_recent(void)
{
  FILE *f = fopen("PROGDIR:Quill.recent", "r");
  char line[300];
  nrecent = 0;
  if (!f) return;
  while (nrecent < NRECENT && fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (line[0]) { strncpy(recent[nrecent], line, 255); recent[nrecent][255] = 0; nrecent++; }
  }
  fclose(f);
}
/* path to the top of the recent list (moved up if it is there already, the oldest dropped past
   NRECENT), stored as a full path, and the file written again straight away */
static void add_recent(const char *path)
{
  int i, k;
  char full[256];
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  /* the whole path, so it opens from anywhere */
  if (!l || !NameFromLock(l, (STRPTR)full, sizeof full)) { strncpy(full, path, 255); full[255] = 0; }
  if (l) UnLock(l);
  for (k = 0; k < nrecent && strcmp(recent[k], full); k++) ;
  if (k == nrecent && nrecent < NRECENT) nrecent++;
  if (k >= nrecent) k = nrecent - 1;
  for (i = k; i > 0; i--) strcpy(recent[i], recent[i - 1]);
  strcpy(recent[0], full);
  {
    FILE *f = fopen("PROGDIR:Quill.recent", "w");
    if (f) { for (i = 0; i < nrecent; i++) fprintf(f, "%s\n", recent[i]); fclose(f); }
  }
}
/* PROGDIR:Quill.prefs, one key=value a line; unknown lines are skipped and a missing file leaves the
   defaults (tb.units -1: inches or centimetres guessed from the paper, see units_metric). Runs
   before the first window exists, so it fills the first slot's Toolbar */
static void load_prefs(void)
{
  FILE *f = fopen("PROGDIR:Quill.prefs", "r");
  char line[200];
  tb.units = -1;
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "units=", 6)) tb.units = atoi(line + 6);
    else if (!strncmp(line, "grid=", 5)) pref_grid = atoi(line + 5);
    else if (!strncmp(line, "darkpage=", 9)) pref_darkpage = atoi(line + 9);
    else if (!strncmp(line, "theme=", 6)) pref_dark = !strcmp(line + 6, "dark");
    else if (!strncmp(line, "textsize=", 9)) pref_large = !strcmp(line + 9, "large");
    else if (!strncmp(line, "language=", 9)) pref_lang = lang_by_code(line + 9);
    else if (!strncmp(line, "name=", 5)) { strncpy(username, line + 5, sizeof username - 1); username[sizeof username - 1] = 0; }
    else if (!strcmp(line, "aatext=off")) { pref_noaa = 1; theme_no_aatext(); }
    else if (!strcmp(line, "hinting=off")) { pref_nohint = 1; theme_no_hinting(); }
    else if (!strncmp(line, "screen=", 7)) { strncpy(pub_screen, line + 7, sizeof pub_screen - 1); pub_screen[sizeof pub_screen - 1] = 0; }
    else if (!strncmp(line, "window=", 7)) pref_win = !strcmp(line + 7, "last") ? WIN_LAST : !strcmp(line + 7, "fixed") ? WIN_FIXED : WIN_FILL;
    else if (!strncmp(line, "look=", 5)) {
      int a[4];
      if (sscanf(line + 5, "%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3]) == 4)
        { pref_look[0] = (signed char)a[0]; pref_look[1] = (signed char)a[1]; pref_look[2] = (signed char)a[2]; pref_look[3] = (signed char)a[3]; }
    }
    else if (!strncmp(line, "windowbox=", 10)) {
      long b[4] = { 0, 0, 0, 0 };
      if (sscanf(line + 10, "%ld,%ld,%ld,%ld", &b[0], &b[1], &b[2], &b[3]) == 4 && b[2] >= 100 && b[3] >= 60)
        { win_box[0] = b[0]; win_box[1] = b[1]; win_box[2] = b[2]; win_box[3] = b[3]; }
    }
    else wp_prefs_keep(line);              /* (WP26) the work packages' settings, kept and written back */
  }
  fclose(f);
}
/* the options written back in full (called whenever one changes; there is no Save button) */
static void save_prefs(void)
{
  FILE *f = fopen("PROGDIR:Quill.prefs", "w");
  if (!f) return;
  fprintf(f, "units=%d\nname=%s\ngrid=%d\ndarkpage=%d\ntheme=%s\n", tb.units, username, pref_grid, pref_darkpage, pref_dark ? "dark" : "light");
  if (pref_lang >= 0) fprintf(f, "language=%s\n", lang_code(pref_lang));
  fprintf(f, "textsize=%s\n", pref_large ? "large" : "normal");
  fprintf(f, "screen=%s\nwindow=%s\n", pub_screen, pref_win == WIN_LAST ? "last" : pref_win == WIN_FIXED ? "fixed" : "fill");
  if (pref_noaa) fprintf(f, "aatext=off\n");
  if (pref_nohint) fprintf(f, "hinting=off\n");
  if (pref_look[0] >= 0 || pref_look[1] >= 0 || pref_look[2] >= 0 || pref_look[3] >= 0)
    fprintf(f, "look=%d,%d,%d,%d\n", pref_look[0], pref_look[1], pref_look[2], pref_look[3]);
  if (win_box[2]) fprintf(f, "windowbox=%ld,%ld,%ld,%ld\n", (long)win_box[0], (long)win_box[1], (long)win_box[2], (long)win_box[3]);
  wp_prefs_write(f);                       /* (WP26) */
  fclose(f);
}
void wp_save_prefs(void) { save_prefs(); }     /* (WP26, wphooks.h) */
/* a new blank document with the look Set as Default chose (nothing to undo, nothing to save) */
static Doc *blank_doc(void)
{
  Doc *d = doc_new();
  if (!d) return NULL;
  if (pref_look[0] >= 0) doc_style_set(d, pref_look[0]);
  if (pref_look[1] >= 0) doc_theme_fonts(d, pref_look[1]);
  if (pref_look[2] >= 0) doc_para_spacing(d, pref_look[2]);
  if (pref_look[3] >= 0) doc_color_set(d, pref_look[3]);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}
/* the window's title: the file's name (or Untitled) and " *" while there are unsaved changes */
static void set_title(void)
{
  const char *name = docpath[0] ? (const char *)FilePart((STRPTR)docpath) : tr("Untitled");
  ULONG changed = 0;
  get(view, MUIA_Quill_Changed, &changed);
  char *t = title_store();
  sprintf(title, "Quill - %.400s%s", name, changed ? " *" : "");
  if (t != title) strcpy(t, title);
  set(win, MUIA_Window_Title, (ULONG)t);  /* (MUI keeps the pointer: each window needs its own string) */
}
/* menu items by their ids: checkmarks and ghosting */
static void menu_set(ULONG id, ULONG attr, ULONG val)
{
  Object *o = strip ? (Object *)DoMethod(strip, MUIM_FindUData, id) : NULL;
  if (o) SetAttrs(o, MUIA_NoNotify, TRUE, attr, val, TAG_DONE);
}
/* the Layout menu shows the document's page; a size or margins of its own checks none */
/* a header, footer, note, comment or text box edited in the text's place (below) */
static struct { int kind, index, sect; u32 page; Pos caret; LONG scroll; int psect; ULONG tab; } story;   /* tab: the ribbon's before */   /* psect: the section format of the page
                                                     it was opened from (Doc.sects number, 0: the last section) */
static void story_close(int keep);
static void edit_headfoot(int footer, long page);
static int cur_paper, cur_land, cur_margin;
static const PageSetup *caret_page(void);
/* the Layout menu's paper, orientation and margin ticks from the caret's page setup; a size or
   margins not on the menu ticks none (cur_paper or cur_margin -1), which page_setup relies on */
static void page_cycles(void)
{
  const PageSetup *pg = caret_page();
  int k, land = pg->width > pg->height;
  i32 w = land ? pg->height : pg->width, h = land ? pg->width : pg->height;
  cur_paper = cur_margin = -1; cur_land = land;
  for (k = 0; k < NPAPERS; k++) if (papers[k][0] == w && papers[k][1] == h) cur_paper = k;
  for (k = 0; k < NMARGINS; k++) if (margins[k][0] == pg->top && margins[k][1] == pg->left) cur_margin = k;
  for (k = 0; k < 4; k++) menu_set(ID_PAPER0 + k, MUIA_Menuitem_Checked, k == cur_paper);
  for (k = 0; k < 4; k++) menu_set(ID_MARGIN0 + k, MUIA_Menuitem_Checked, k == cur_margin);
  menu_set(ID_PORTRAIT, MUIA_Menuitem_Checked, !land);
  menu_set(ID_LANDSCAPE, MUIA_Menuitem_Checked, land);
}
/* the navigation pane: the headings looked at again (force: a new document) */
static void rebuild_nav(int force)
{
  struct MUIP_Nav_Update nu;
  Pos *c = NULL;
  memset(&nu, 0, sizeof nu);
  nu.MethodID = MUIM_Nav_Update;
  nu.doc = doc; nu.headings = force ? 2 : 1;
  get(view, MUIA_Quill_Caret, &c);
  if (c) nu.caretpara = c->para;
  DoMethodA(navgrp, (Msg)&nu);
  DoMethod(view, MUIM_Quill_Command, CMD_REFRESH, 0);
}
/* d shown in the current window in place of its document, which is freed (the window owns d now);
   the document counts as unchanged */
static void show_doc(Doc *d)
{
  Doc *old = doc;
  doc = d;
  page_cycles();
  set(view, MUIA_Quill_Doc, (ULONG)d);
  set(view, MUIA_Quill_Changed, FALSE);
  if (old && old != d) doc_free(old);
  rebuild_nav(1);
  set_title();
}
/* a requester over the window: gadgets "Save|Don't Save|Cancel" style, fmt with one %s. As
   MUI_Request does, it returns 1 for the first button, 2, 3... and 0 for the last (rightmost) one */
static int ask(const char *gadgets, const char *fmt, const char *arg)
{
  return (int)MUI_Request(app, win, 0, (STRPTR)"Quill", (STRPTR)tr(gadgets), (STRPTR)tr(fmt), arg);
}
/* the ASL file requester, starting in path's drawer with its file name; path is replaced by the
   choice. docs: a document's, with a pattern of the files Quill can read (or write), from
   core/docx.c's table of formats; pattern: another pattern (NULL none). 1 if a file was chosen, 0
   if cancelled (or no name given) */
static int file_request_pat(int save, const char *titletext, char *path, size_t cap, int docs, const char *pattern)
{
  struct FileRequester *fr;
  char dir[400], file[108], pat[160];
  int ok = 0;
  if (docs) { doc_format_pattern(save, pat, sizeof pat); pattern = pat; }
  strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, path[0] ? (const char *)FilePart((STRPTR)path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(titletext),
        ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
        ASLFR_DoPatterns, pattern != NULL, ASLFR_InitialPattern, (ULONG)(pattern ? pattern : "#?"), TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, 0, TAG_DONE)) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    ok = AddPart((STRPTR)path, fr->fr_File, cap) && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}
static int file_request(int save, const char *titletext, char *path, size_t cap, int docs) { return file_request_pat(save, titletext, path, cap, docs, NULL); }
/* (wphooks.h) */
int wp_file_request(int save, const char *title, char *path, size_t cap, const char *pattern) { return file_request_pat(save, title, path, cap, 0, pattern); }
void wp24_saved(Doc *d, const char *path);   /* (WP24, wp_info.c) */
int  wp24_alt_pane(void);
/* the document written to path (.docx or text, by its name: doc_save_file decides). On success the
   document counts as saved and joins the recent list: 1; a failure is reported and returns 0 */
/* (while saving: a cross-reference's page, from the layout, so Word shows the right one) */
static int pageref_page(void *ctx, const char *bookmark) { return ctx ? lay_bmark_page((Layout *)ctx, bookmark) : 0; }
static int save_to(const char *path)
{
  Layout *L = NULL;
  set(app, MUIA_Application_Sleep, TRUE);
  get(view, MUIA_Quill_Layout, &L);
  docx_pageref = pageref_page; docx_pageref_ctx = L;
  if (!doc_save_file(doc, path)) {
    set(app, MUIA_Application_Sleep, FALSE);
    ask("OK", "Quill could not save\n%s", path);
    return 0;
  }
  set(app, MUIA_Application_Sleep, FALSE);
  set(view, MUIA_Quill_Changed, FALSE);
  set_title();
  add_recent(path);
  wp24_saved(doc, path);                 /* (WP24: a version kept, the recovery copy dropped) */
  return 1;
}
/* Save As (text: Save As Text). Asks for a name, adds .docx if none was typed, asks before replacing
   a file. A plain-text copy doesn't become the document's path. 1 if saved, 0 if not */
static int save_as(int text)
{
  char path[512];
  strcpy(path, docpath);
  if (text) { char *dot = strrchr(path, '.'); if (dot && dot > (char *)FilePart((STRPTR)path)) strcpy(dot, ".txt"); }
  if (!file_request(1, text ? "Save as plain text" : "Save the document", path, sizeof path, 1)) return 0;
  if (!text && !strchr((const char *)FilePart((STRPTR)path), '.')) strcat(path, ".docx");
  { BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) { UnLock(l); if (ask("Replace|Cancel", "%s exists.\nReplace it?", path) != 1) return 0; } }
  if (!save_to(path)) return 0;
  if (!text) strcpy(docpath, path);
  set_title();
  return 1;
}
/* Save: to the document's own path if its format keeps everything (.docx: DF_NATIVE in core/docx.c's
   table of formats), else through Save As. 1 if saved */
static int save(void)
{
  const DocFormat *f = docpath[0] ? doc_format_for_name(docpath, 1) : NULL;
  if (!f || !(f->flags & DF_NATIVE)) return save_as(0);   /* text files are saved as .docx */
  return save_to(docpath);
}
/* 1: go ahead (saved or discarded), 0: cancelled */
static int may_discard(void)
{
  ULONG changed = 0;
  get(view, MUIA_Quill_Changed, &changed);
  if (!changed) return 1;
  switch (ask("Save|Don't Save|Cancel", "Save the changes to %s?", docpath[0] ? (const char *)FilePart((STRPTR)docpath) : tr("Untitled"))) {
  case 1: return save();
  case 2: return 1;
  default: return 0;
  }
}
/* after a load, what the reader had to leave out (doc_load_file's LOST_ bits), so the user knows
   before saving over the file */
static void report_lost(unsigned lost)
{
  char msg[400] = "";
  if (!lost) return;
  strcat(msg, tr("Quill read this document, but some parts\nare not shown yet:\n"));
  if (lost & LOST_TABLES) strcat(msg, tr("\n - tables inside tables (their text is kept in the outer cell)"));
  if (lost & LOST_PICTURES) strcat(msg, tr("\n - pictures it cannot read (drawings, old WMF ones)"));
  if (lost & LOST_NOTES) strcat(msg, tr("\n - notes whose text is missing"));
  if (lost & LOST_COMMENTS) strcat(msg, tr("\n - comments whose text is missing"));
  strcat(msg, tr("\n\nSaving would leave them out."));
  ask("OK", "%s", msg);
}
/* a clock in 1/50 s for the DEBUG log's timings (from midnight: it wraps then, which is harmless here) */
static LONG ticks(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return ds.ds_Minute * 3000 + ds.ds_Tick;
}
static int win_find(const char *path);
/* a document opened from a file: brought to the front if it is open already, else loaded into the
   untouched blank window or a new window of its own. Load errors are shown to the user */
static void open_path(const char *path)
{
  char err[300];
  unsigned lost = 0;
  Doc *d;
  LONG t0 = ticks(), t1;
  if (win_find(path)) return;            /* open already: its window comes to the front */
  set(app, MUIA_Application_Sleep, TRUE);
  d = doc_load_file(path, &lost, err, sizeof err);
  t1 = ticks();
  set(app, MUIA_Application_Sleep, FALSE);
  if (!d) { char e2[400]; utf8_to_latin1(err, strlen(err), e2, sizeof e2); ask("OK", "%s", e2); return; }
  if (doc_untouched()) {
    /* the blank document Quill started with (or New made) and nobody typed in: the file takes its
       window, as in Word */
    strncpy(docpath, path, sizeof docpath - 1);
    show_doc(d);
  } else if (!win_new(d, path)) return;
  add_recent(path);
  if (qlog) { fprintf(qlog, "opened %s: %lu paragraphs; read in %ld ticks, laid out and drawn in %ld (1/50 s)\n", path,
                      (unsigned long)d->np, (long)(t1 - t0), (long)(ticks() - t1)); fflush(qlog); }
  report_lost(lost);
}
/* the Find and Replace window */
static Object *replstr, *casecb, *wordcb, *findmsg;
static void latin1_utf8(const char *s, char *u, size_t cap);
/* the dialogs below are all made the same way, the first time they are wanted (a dialog nobody
   opens costs no memory, and MUI works out its size only then). The window joins the application
   (OM_ADDMEMBER) and afterwards is only hidden and shown through MUIA_Window_Open. Each
   MUIM_Notify is MUI's "when this attribute takes this value, do that": the close gadget and Cancel
   just hide the window, while the other buttons do MUIM_Application_ReturnID, which makes
   MUIM_Application_NewInput in main()'s loop hand back that ID, so command() runs it like a menu
   item. Here: the Find and Replace window, opened with the cursor in the Find field */
static void open_find(void)
{
  if (!findwin) {
    Object *next, *prev, *repl, *all, *close;
    findwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Find and Replace"), MUIA_Window_ID, MAKE_ID('F','I','N','D'),
      WindowContents, VGroup, QDLG,
        Child, ColGroup(2),
          Child, Label2(tr("Find")), Child, findstr = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Replace with")), Child, replstr = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup,
          Child, casecb = CheckMark(FALSE), Child, Label1(tr("Match case")),
          Child, wordcb = CheckMark(FALSE), Child, Label1(tr("Whole words")),
          Child, HSpace(0),
        End,
        Child, findmsg = TextObject, MUIA_Text_Contents, (ULONG)"", End,
        Child, HGroup,
          Child, next = QButton(tr("Find _Next")),
          Child, prev = QButton(tr("_Previous")),
          Child, repl = QButton(tr("_Replace")),
          Child, all = QButton(tr("Replace _All")),
          Child, close = QButton(tr("_Close")),
        End,
      End, End;
    if (!findwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)findwin);
    DoMethod(findwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)findwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)findwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(findstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDGO);
    DoMethod(next, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDGO);
    DoMethod(prev, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FINDPREV);
    DoMethod(repl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_REPLACE1);
    DoMethod(all, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_REPLACEALL);
  }
  set(findwin, MUIA_Window_Open, TRUE);
  set(findwin, MUIA_Window_ActiveObject, (ULONG)findstr);
}
/* how: ID_FINDGO, ID_FINDNEXT (Amiga-G), ID_FINDPREV, ID_REPLACE1, ID_REPLACEALL */
static void find_next(ULONG how)
{
  static char u[1024], w[1024];
  static char msg[64];
  char *s = NULL, *r = NULL;
  ULONG cs = 0, ww = 0;
  struct QFind q;
  if (findstr) get(findstr, MUIA_String_Contents, &s);
  if (s && *s) { strncpy(findtext, s, sizeof findtext - 1); findtext[sizeof findtext - 1] = 0; }
  if (!findtext[0]) { open_find(); return; }
  if (replstr) get(replstr, MUIA_String_Contents, &r);
  if (casecb) get(casecb, MUIA_Selected, &cs);
  if (wordcb) get(wordcb, MUIA_Selected, &ww);
  latin1_utf8(findtext, u, sizeof u);
  latin1_utf8(r ? r : "", w, sizeof w);
  memset(&q, 0, sizeof q);
  q.what = u; q.with = w;
  q.flags = (cs ? FIND_CASE : 0) | (ww ? FIND_WORD : 0) | (how == ID_FINDPREV ? FIND_BACK : 0);
  DoMethod(view, MUIM_Quill_Command, how == ID_REPLACE1 ? CMD_REPLACE1 : how == ID_REPLACEALL ? CMD_REPLACEALL : CMD_FINDX, (ULONG)&q);
  if (how == ID_REPLACEALL) sprintf(msg, tr("%lu replaced."), (unsigned long)q.count);
  else strcpy(msg, q.found ? "" : tr("Not found."));
  if (findmsg) set(findmsg, MUIA_Text_Contents, (ULONG)msg);
}
/* the page setup: the whole document's, or (with more than one section) the caret's section's */
static u32 body_caret(void)
{
  Pos *c = NULL;
  if (doc->edit.kind) return story.caret.para < doc->edit.np ? story.caret.para : 0;
  get(view, MUIA_Quill_Caret, &c);
  return c && c->para < doc->np ? c->para : 0;
}
/* how many sections the text being edited has (a story's own while one is open) */
static int sections(void)
{
  Para **pp = doc->edit.kind ? doc->edit.p : doc->p;
  u32 i, np = doc->edit.kind ? doc->edit.np : doc->np;
  int n = 1;
  for (i = 0; i + 1 < np; i++) if (pp[i]->pf.sect) n++;
  return n;
}
/* the page setup at the caret (in a header or footer: the page it was opened from). NULL before
   there is a document */
static const PageSetup *caret_page(void)
{
  u32 pi;
  if (!doc) return NULL;
  if (doc->edit.kind) return story.psect && doc->sects[story.psect - 1].haspage ? &doc->sects[story.psect - 1].page : &doc->edit.page;
  pi = body_caret();
  return sect_page(doc, doc_sect(doc, pi), doc_last_sect(doc, pi));
}
/* the page setup to change (body text being edited): the document's, every section's own dropped
   (whole), or the caret's section's, which becomes its own (the last section's is the document's:
   the others then keep theirs) */
static PageSetup *page_to_change(int whole)
{
  u32 pi = body_caret(), e;
  int k;
  SectFmt *s;
  if (whole || sections() < 2) { for (k = 0; k < doc->nsects; k++) doc->sects[k].haspage = 0; return &doc->page; }
  if (doc_last_sect(doc, pi)) {
    for (k = 0; k < doc->nsects; k++) if (!doc->sects[k].haspage) { doc->sects[k].haspage = 1; doc->sects[k].page = doc->page; }
    return &doc->page;
  }
  e = doc_sect_end(doc, pi);
  s = &doc->sects[doc->p[e]->pf.sect - 1];
  if (!s->haspage) { s->haspage = 1; s->page = doc->page; }
  return &s->page;
}
/* after a change: sections set up as the document is have its page setup again */
static void page_changed(void)
{
  int k;
  for (k = 0; k < doc->nsects; k++) if (doc->sects[k].haspage && !memcmp(&doc->sects[k].page, &doc->page, sizeof(PageSetup))) doc->sects[k].haspage = 0;
  doc->dirty = 1;
  doc->ver++;
  set(view, MUIA_Quill_Changed, TRUE);
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);
  page_cycles();
}
/* a header or footer edited is closed while the page changes, and opened again after */
static int paused_kind;
static u32 paused_page;
/* (the header or footer is kept, and remembered for story_resume) */
static void story_pause(void)
{
  paused_kind = doc->edit.kind == SK_HEADER || doc->edit.kind == SK_FOOTER ? doc->edit.kind : 0;
  paused_page = story.page;
  story_close(1);
}
/* (the remembered header or footer opened again, on the same page) */
static void story_resume(void)
{
  if (paused_kind) edit_headfoot(paused_kind == SK_FOOTER, (long)paused_page);
  paused_kind = 0;
}
/* a choice from the Layout menu: paper (0-3), orientation (0, 1) or margins (0-3); -1 keeps one */
static void page_setup(int pa, int land, int m)
{
  PageSetup *pg;
  i32 w, h;
  story_pause();
  page_cycles();
  pg = page_to_change(0);
  if (m >= 0) {
    pg->top = pg->bottom = margins[m][0];
    pg->left = pg->right = margins[m][1];
  }
  if (pa < 0 && cur_paper < 0) { w = pg->width < pg->height ? pg->width : pg->height; h = pg->width + pg->height - w; }
  else { if (pa < 0) pa = cur_paper; w = papers[pa][0]; h = papers[pa][1]; }
  if (land < 0) land = cur_land;
  if (land) { pg->width = h; pg->height = w; } else { pg->width = w; pg->height = h; }
  page_changed();
  story_resume();
}
/* the Table menu's row and column items work in a table only */
static void table_menu(void)
{
  static const ULONG ids[] = { ID_ROW_ABOVE, ID_ROW_BELOW, ID_COL_LEFT, ID_COL_RIGHT, ID_DEL_ROW, ID_DEL_COL, ID_DEL_TABLE, ID_HEADERROW,
                               ID_MERGE, ID_SPLIT, ID_SPLITTABLE, ID_TPROPS, ID_TSORT, ID_FORMULA, ID_TOTEXT };
  ULONG in = 0;
  size_t k;
  get(view, MUIA_Quill_InTable, &in);
  for (k = 0; k < sizeof ids / sizeof ids[0]; k++) menu_set(ids[k], MUIA_Menuitem_Enabled, in != 0);
}
/* Export PDF: the file next to the document, .pdf */
static void export_pdf(const char *given)
{
  char path[512];
  Layout *L = NULL;
  if (given) { strncpy(path, given, sizeof path - 1); path[sizeof path - 1] = 0; }
  else {
    char *dot;
    strcpy(path, docpath[0] ? docpath : "Untitled.docx");
    dot = strrchr(path, '.');
    if (dot && dot > (char *)FilePart((STRPTR)path)) strcpy(dot, ".pdf"); else strcat(path, ".pdf");
    if (!file_request(1, "Export as PDF", path, sizeof path, 0)) return;
  }
  set(app, MUIA_Application_Sleep, TRUE);
  get(view, MUIA_Quill_Layout, &L);
  if (!L || !pdf_save_file(L, path)) { set(app, MUIA_Application_Sleep, FALSE); ask("OK", "Quill could not write\n%s", path); return; }
  set(app, MUIA_Application_Sleep, FALSE);
}
/* Print: every page at 150 dpi through printer.device, after asking (quiet: no questions or error
   requesters, for ARexx). Returns NULL when printed (or cancelled), else what went wrong */
static const char *print_doc(int quiet)
{
  Layout *L = NULL;
  const char *err;
  char msg[80];
  get(view, MUIA_Quill_Layout, &L);
  if (!L) return tr("nothing to print");
  sprintf(msg, tr(L->npages == 1 ? "Print %lu page?" : "Print %lu pages?"), (unsigned long)L->npages);
  if (!quiet && ask("Print|Cancel", "%s\n\n(Through printer.device: Prefs/Printer\nchooses the printer.)", msg) != 1) return NULL;
  set(app, MUIA_Application_Sleep, TRUE);
  err = (const char *)DoMethod(view, MUIM_Quill_Print, 0, -1, 150);
  set(app, MUIA_Application_Sleep, FALSE);
  if (err && !quiet) ask("OK", "%s", err);
  if (err && qlog) { fprintf(qlog, "print: %s\n", err); fflush(qlog); }
  return err;
}
/* the Insert Table window */
static void open_table(void)
{
  if (!tablewin) {
    Object *ok, *cancel;
    tablewin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Insert Table"), MUIA_Window_ID, MAKE_ID('T','A','B','L'),
      WindowContents, VGroup, QDLG,
        Child, ColGroup(2),
          Child, Label2(tr("Columns")), Child, colsl = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 20, MUIA_Numeric_Value, 3, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Rows")), Child, rowsl = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 60, MUIA_Numeric_Value, 3, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, ok = QButton(tr("_Insert")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
      End, End;
    if (!tablewin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)tablewin);
    DoMethod(tablewin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)tablewin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tablewin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABLEGO);
  }
  set(tablewin, MUIA_Window_Open, TRUE);
}
/* a story's text in Latin-1 (fields as {page}, paragraphs joined by " / ") */
static void story_latin1(Story *st, char *out, size_t cap)
{
  char *t = story_text(st);
  out[0] = 0;
  if (t) { utf8_to_latin1(t, (u32)strlen(t), out, cap); free(t); }
}
/* headers, footers, notes, comments and text boxes: edited in the text's place, with everything
   the text can have (core: doc_story_begin) */
static Layout *view_layout(void) { Layout *L = NULL; get(view, MUIA_Quill_LayoutNow, &L); return L; }
/* the ribbon's Header & Footer or Edit Text tab, and its options' states */
static void story_bits(void)
{
  ULONG b = 0;
  int k = doc->edit.kind;
  if (k == SK_HEADER || k == SK_FOOTER) {
    const SectFmt *s = story.psect ? &doc->sects[story.psect - 1] : NULL;
    b |= RBIT(RB_HFEDIT - 32);
    if (sect_titlepg(doc, s, !s)) b |= RBIT(RB_FIRSTPG - 32);
    if (doc->evenodd) b |= RBIT(RB_EVENODD - 32);
    if (s && s->hf) b |= RBIT(RB_OWNHF - 32);
    if (s) b |= RBIT(RB_SECTHF - 32);
  } else if (k) b |= RBIT(RB_STORYEDIT - 32);
  tb.uibits2 = b;
}
/* what the view shows over a story being edited, such as "Footnote 3" or "Header (First Page)" */
static void story_label(char *out, size_t cap)
{
  static const char *const hf[HF_N] = { "", " (First Page)", " (Even Page)" };
  switch (doc->edit.kind) {
  case SK_HEADER: case SK_FOOTER:
    snprintf(out, cap, "%s%s%s", tr(doc->edit.kind == SK_HEADER ? "Header" : "Footer"), doc->edit.index % HF_N ? tr(hf[doc->edit.index % HF_N]) : "",
             story.sect ? tr(" - this section's own") : "");
    break;
  case SK_NOTE: snprintf(out, cap, tr("Footnote %d"), doc->edit.index + 1); break;
  case SK_ENOTE: snprintf(out, cap, tr("Endnote %d"), doc->edit.index + 1); break;
  case SK_COMMENT: snprintf(out, cap, "%s %s", tr("Comment by"), doc->comments[doc->edit.index].author); break;
  case SK_TEXTBOX: snprintf(out, cap, "%s", tr("Text Box")); break;
  default: out[0] = 0;
  }
}
/* a story edited: kind SK_, index (HF_, a note's, comment's or object's), sect (its section format's own
   headers, 0 the document's), page: the page it is for (page fields show it) */
static int story_open(int kind, int index, int sect, u32 page)
{
  Layout *L = view_layout();
  Pos *c = NULL;
  struct QStoryView sv;
  char label[100];
  int psect = story.psect;
  if (doc->edit.kind) story_close(1); else psect = 0;
  if (!doc->edit.kind) {
    get(view, MUIA_Quill_Caret, &c);
    story.caret = c ? *c : doc_start(doc);
    get(view, MUIA_Quill_Scroll, &story.scroll);
    get(ribbon, MUIA_Ribbon_Tab, &story.tab);
  }
  if (L && (kind == SK_HEADER || kind == SK_FOOTER) && page < L->npages) {
    /* the section whose page it is (its options are shown) */
    const PageInfo *pi = &L->pinfo[page];
    psect = !pi->last && pi->sec >= doc->sects && pi->sec < doc->sects + doc->nsects ? (int)(pi->sec - doc->sects) + 1 : 0;
  }
  if (!doc_story_begin(doc, kind, index, sect, page, L && L->npages ? L->npages : 1)) { DisplayBeep(NULL); return 0; }
  story.kind = kind; story.index = index; story.sect = sect; story.page = page; story.psect = psect;
  if (qlog) { fprintf(qlog, "story: editing kind %d, %d (section format %d), page %lu\n", kind, index, sect, (unsigned long)page); fflush(qlog); }
  story_label(label, sizeof label);
  sv.caret = doc_start(doc); sv.scroll = 0; sv.label = label;
  story_bits();
  DoMethod(view, MUIM_Quill_Command, CMD_STORY, (ULONG)&sv);
  set(ribbon, MUIA_Ribbon_Tab, kind == SK_HEADER || kind == SK_FOOTER ? TAB_HEADFOOT : TAB_EDITTEXT);
  set(win, MUIA_Window_ActiveObject, (ULONG)view);
  rebuild_nav(1);
  return 1;
}
/* back to the text: the story kept (keep) or left as it was */
static void story_close(int keep)
{
  struct QStoryView sv;
  if (!doc->edit.kind) return;
  if (qlog) { fprintf(qlog, "story: done (%s)\n", keep ? "kept" : "dropped"); fflush(qlog); }
  doc_story_end(doc, keep);
  sv.caret = story.caret; sv.scroll = story.scroll; sv.label = "";
  story_bits();
  DoMethod(view, MUIM_Quill_Command, CMD_STORY, (ULONG)&sv);
  if (doc->dirty) set(view, MUIA_Quill_Changed, TRUE);
  set(ribbon, MUIA_Ribbon_Tab, story.tab == TAB_HEADFOOT || story.tab == TAB_EDITTEXT ? TAB_HOME : story.tab);
  rebuild_nav(1);
}
/* the header (footer) of page page, or the caret's page */
static void edit_headfoot(int footer, long page)
{
  Layout *L;
  int w, sect;
  if (doc->edit.kind == SK_HEADER || doc->edit.kind == SK_FOOTER) {
    /* already in one: its partner on the same page */
    int idx = doc->edit.index, sc = doc->edit.sect;
    u32 pg = story.page;
    story_close(1);
    story_open(footer ? SK_FOOTER : SK_HEADER, idx, sc, pg);
    return;
  }
  story_close(1);
  if (!(L = view_layout()) || !L->npages) return;
  if (page < 0) { Pos *c = NULL; u32 pg = 0; i32 x, y, h; get(view, MUIA_Quill_Caret, &c); if (c) lay_caret(L, *c, &pg, &x, &y, &h); page = (long)pg; }
  if ((u32)page >= L->npages) page = (long)L->npages - 1;
  lay_page_hf(L, (u32)page, &w, &sect);
  story_open(footer ? SK_FOOTER : SK_HEADER, w, sect, (u32)page);
}
/* the next (dir > 0) or previous section's header or footer: the first page of it */
static void headfoot_section(int dir)
{
  int kind = doc->edit.kind, w, sect;
  u32 pg = story.page, k;
  Layout *L;
  if (kind != SK_HEADER && kind != SK_FOOTER) return;
  story_close(1);
  if (!(L = view_layout()) || !L->npages) return;
  if (pg >= L->npages) pg = L->npages - 1;
  k = pg;
  if (dir > 0) { while (k < L->npages && L->pinfo[k].first == L->pinfo[pg].first) k++; }
  else if (L->pinfo[pg].first > 0) k = L->pinfo[L->pinfo[pg].first - 1].first;
  if (k >= L->npages || k == pg) { DisplayBeep(NULL); k = pg; }
  lay_page_hf(L, k, &w, &sect);
  story_open(kind, w, sect, k);
}
/* the edited header's options: a first page of its own (opt 0), odd and even pages (1), the section's own (2) */
static void headfoot_option(int opt)
{
  int kind = doc->edit.kind, ps = story.psect;
  u32 pg = story.page;
  if (kind != SK_HEADER && kind != SK_FOOTER) return;
  story_close(1);
  if (opt == 0) { if (ps) doc->sects[ps - 1].titlepg = (u8)!doc->sects[ps - 1].titlepg; else doc->titlepg = (u8)!doc->titlepg; }
  else if (opt == 1) doc->evenodd = (u8)!doc->evenodd;
  else if (ps) doc_sect_own_hf(doc, ps, !doc->sects[ps - 1].hf);
  doc->dirty = 1;
  set(view, MUIA_Quill_Changed, TRUE);
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);
  story.psect = ps;
  edit_headfoot(kind == SK_FOOTER, (long)pg);
}
/* pictures: one from a file, and the size of one */
static char picpath[512];
/* Insert > Picture: a PNG or JPEG file chosen (picpath remembers the drawer) and put in at the caret */
static void insert_picture(void)
{
  size_t n;
  u8 *data;
  int k;
  if (!file_request(0, "Insert a picture (PNG or JPEG)", picpath, sizeof picpath, 0)) return;
  set(app, MUIA_Application_Sleep, TRUE);
  data = file_read(picpath, &n);
  k = data ? doc_add_image(doc, data, (u32)n, 0, 0) : -1;       /* takes the data */
  set(app, MUIA_Application_Sleep, FALSE);
  if (k < 0) { ask("OK", "%s\nis not a PNG or JPEG picture Quill can read.", picpath); return; }
  DoMethod(view, MUIM_Quill_Command, CMD_PICTURE, (ULONG)k);
}
/* the Tabs window: the caret's paragraph's tab stops, set and cleared one at a time */
static Object *tabwin, *tablist, *tabpos, *tabkind, *tablead;
static struct QTabs curtabs;
/* centimetres (1) or inches (0): the option, or with none set, inches for US Letter paper */
static int units_metric(void) { return tb.units >= 0 ? tb.units : doc->page.width != 12240; }
/* a length in twips as the user reads it: "2.54 cm" or "1.00\"" (out: 24 bytes is plenty). Used for
   every length field in the dialogs, and parse_len reads it back */
static void tab_text(i32 tw, char *out)
{
  if (units_metric()) sprintf(out, "%ld.%02ld cm", (long)(tw * 254L / 144000L), (long)(tw * 254L / 1440L % 100));
  else sprintf(out, "%ld.%02ld\"", (long)(tw / 1440), (long)(tw % 1440 * 100 / 1440));
}
/* "2.5", "2,5 cm", "1in", "1\"": twips (0 if not a length) */
static i32 parse_len(const char *s)
{
  long whole = 0, frac = 0, div = 1;
  int inch = !units_metric();
  while (*s == ' ') s++;
  while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
  if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9' && div < 10000) { frac = frac * 10 + (*s++ - '0'); div *= 10; } }
  while (*s == ' ') s++;
  if (*s == '"' || !strncmp(s, "in", 2)) inch = 1;
  else if (!strncmp(s, "cm", 2)) inch = 0;
  else if (!strncmp(s, "mm", 2)) { inch = 0; frac = (whole % 10) * div + frac; div *= 10; whole /= 10; }
  return inch ? (i32)(whole * 1440 + frac * 1440 / div) : (i32)((whole * 1440 * 100 + frac * 1440 * 100 / div) / 254);
}
/* the Tabs window's list from curtabs (MUIA_List_Quiet: one redraw at the end, not one a line) */
static void tabs_fill(void)
{
  static const char *const kinds[] = { "Left", "Centre", "Right", "Decimal" }, *const leads[] = { "", "  . . . .", "  - - - -", "  ____" };
  int k;
  char line[64], len[24];
  set(tablist, MUIA_List_Quiet, TRUE);
  DoMethod(tablist, MUIM_List_Clear);
  for (k = 0; k < 8 && curtabs.pos[k]; k++) {
    tab_text(curtabs.pos[k], len);
    sprintf(line, "%s  %s%s", len, tr(kinds[curtabs.kind[k] & 3]), leads[curtabs.lead[k] & 3]);
    DoMethod(tablist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  set(tablist, MUIA_List_Quiet, FALSE);
}
/* the Tabs window, with the caret's paragraph's stops copied into curtabs */
static void open_tabs(void)
{
  static const char *const kinds0[] = { "Left", "Centre", "Right", "Decimal" }, *const leads0[] = { "None", "Dots . . .", "Hyphens - - -", "Line ____" };
  static const char *kinds[5], *leads[5];
  Pos *c = NULL;
  int i;
  for (i = 0; i < 4; i++) { kinds[i] = tr(kinds0[i]); leads[i] = tr(leads0[i]); }
  Para *p;
  if (!tabwin) {
    Object *set_, *clr, *clrall, *close;
    tabwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Tabs"), MUIA_Window_ID, MAKE_ID('T','A','B','S'),
      WindowContents, VGroup, QDLG,
        Child, tablist = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
        Child, ColGroup(2),
          Child, Label2(tr("Position")), Child, tabpos = StringObject, StringFrame, MUIA_String_MaxLen, 20, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Alignment")), Child, tabkind = CycleObject, MUIA_Cycle_Entries, (ULONG)kinds, End,
          Child, Label1(tr("Leader")), Child, tablead = CycleObject, MUIA_Cycle_Entries, (ULONG)leads, End,
        End,
        Child, HGroup, Child, set_ = QButton(tr("_Set")), Child, clr = QButton(tr("C_lear")), Child, clrall = QButton(tr("Clear _All")),
          Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!tabwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)tabwin);
    DoMethod(tabwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)tabwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tabwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(set_, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABSET);
    DoMethod(tabpos, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABSET);
    DoMethod(clr, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABCLEAR);
    DoMethod(clrall, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABCLEARALL);
    DoMethod(tablist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TABPICK);
  }
  get(view, MUIA_Quill_Caret, &c);
  p = doc->p[c && c->para < doc->np ? c->para : 0];
  memcpy(curtabs.pos, p->pf.tabs, sizeof curtabs.pos); memcpy(curtabs.kind, p->pf.tabkind, sizeof curtabs.kind); memcpy(curtabs.lead, p->pf.tablead, sizeof curtabs.lead);
  tabs_fill();
  set(tabwin, MUIA_Window_Open, TRUE);
  set(tabwin, MUIA_Window_ActiveObject, (ULONG)tabpos);
}
/* the Tabs window's buttons: Set (adds a stop, or changes the one within 10 twips), Clear, Clear All,
   and a list line picked (its stop into the fields). Stops are kept in order, 8 at most, a 0
   position ending the list; each change goes to the paragraph through the view at once */
static void tabs_command(ULONG id)
{
  int k, n;
  for (n = 0; n < 8 && curtabs.pos[n]; n++) ;
  if (id == ID_TABPICK) {
    LONG a = -1;
    char len[24];
    get(tablist, MUIA_List_Active, &a);
    if (a < 0 || a >= n) return;
    tab_text(curtabs.pos[a], len);
    set(tabpos, MUIA_String_Contents, (ULONG)len);
    set(tabkind, MUIA_Cycle_Active, curtabs.kind[a]);
    set(tablead, MUIA_Cycle_Active, curtabs.lead[a]);
    return;
  }
  if (id == ID_TABCLEARALL) memset(&curtabs, 0, sizeof curtabs);
  else {
    char *s = NULL;
    ULONG kind = 0, lead = 0;
    i32 pos;
    get(tabpos, MUIA_String_Contents, &s);
    get(tabkind, MUIA_Cycle_Active, &kind); get(tablead, MUIA_Cycle_Active, &lead);
    if (!s || (pos = parse_len(s)) <= 0) { DisplayBeep(NULL); return; }
    for (k = 0; k < n && (curtabs.pos[k] < pos - 10 || curtabs.pos[k] > pos + 10); k++) ;   /* the same stop, near enough */
    if (id == ID_TABCLEAR) {
      if (k == n) return;
      for (; k < 7; k++) { curtabs.pos[k] = curtabs.pos[k + 1]; curtabs.kind[k] = curtabs.kind[k + 1]; curtabs.lead[k] = curtabs.lead[k + 1]; }
      curtabs.pos[7] = 0;
    } else {
      if (k == n) {
        if (n == 8) { DisplayBeep(NULL); return; }
        /* in order */
        for (k = n; k > 0 && curtabs.pos[k - 1] > pos; k--) { curtabs.pos[k] = curtabs.pos[k - 1]; curtabs.kind[k] = curtabs.kind[k - 1]; curtabs.lead[k] = curtabs.lead[k - 1]; }
      }
      curtabs.pos[k] = pos; curtabs.kind[k] = (u8)kind; curtabs.lead[k] = (u8)lead;
    }
  }
  DoMethod(view, MUIM_Quill_Command, CMD_TABS, (ULONG)&curtabs);
  tabs_fill();
}
/* Word Count: pages, words, characters with and without spaces, paragraphs, lines */
static void word_count(void)
{
  Layout *L = NULL;
  u32 i, chars = 0, nospace = 0, paras = 0, lines = 0;
  char msg[300];
  set(app, MUIA_Application_Sleep, TRUE);
  get(view, MUIA_Quill_Layout, &L);
  for (i = 0; i < doc->np; i++) {
    Para *p = doc->p[i];
    u32 k;
    if (p->len) paras++;
    for (k = 0; k < p->len; k++) {
      u8 c = (u8)p->t[k];
      if ((c & 0xC0) == 0x80) continue;
      if (c == 0xF3) continue;                               /* pictures and notes */
      chars++;
      if (c != ' ' && c != '\t' && c != '\v') nospace++;
    }
  }
  if (L) for (i = 0; i < L->npl; i++) lines += L->pl[i].nlines;
  set(app, MUIA_Application_Sleep, FALSE);
  sprintf(msg, tr("\33cWord Count\n\nPages: %lu\nWords: %lu\nCharacters (no spaces): %lu\nCharacters (with spaces): %lu\nParagraphs: %lu\nLines: %lu"),
          L ? (unsigned long)L->npages : 0UL, (unsigned long)doc_count_words(doc), (unsigned long)nospace, (unsigned long)chars,
          (unsigned long)paras, (unsigned long)lines);
  ask("Close", "%s", msg);
}
/* the date and time now, as .docx has them: 2026-10-24T09:30:00Z */
static void iso_now(char *out)
{
  static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  struct DateStamp ds;
  LONG days, y = 1978, m;
  DateStamp(&ds);
  days = ds.ds_Days;
  for (;;) { LONG yl = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365; if (days < yl) break; days -= yl; y++; }
  for (m = 0; m < 12; m++) {
    LONG ml = mdays[m] + (m == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (days < ml) break;
    days -= ml;
  }
  sprintf(out, "%04ld-%02ld-%02ldT%02ld:%02ld:00Z", (long)y, (long)m + 1, (long)days + 1, (long)(ds.ds_Minute / 60), (long)(ds.ds_Minute % 60));
}
/* the Comment window: a new comment on the selection, or the text of the one at the caret */
static Object *cmwin, *cmstr;
static int cm_editing;
/* Review > New Comment. With the caret in a comment (or its range selected) that comment's text is
   opened in the text's place instead and the window is not shown */
static void open_comment(void)
{
  static char cur[512];
  Pos *c = NULL, *an = NULL;
  if (!cmwin) {
    Object *ok, *cancel;
    cmwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Comment"), MUIA_Window_ID, MAKE_ID('C','M','N','T'),
      WindowContents, VGroup, QDLG,
        Child, HGroup, Child, Label2(tr("Comment")), Child, cmstr = StringObject, StringFrame, MUIA_String_MaxLen, 500, MUIA_CycleChain, 1, End, End,
        Child, HGroup, Child, ok = QButton(tr("_OK")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
      End, End;
    if (!cmwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)cmwin);
    DoMethod(cmwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)cmwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)cmwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_COMMENTGO);
    DoMethod(cmstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_COMMENTGO);
  }
  /* inside a comment with nothing (or just its range) selected: its text */
  get(view, MUIA_Quill_Caret, &c); get(view, MUIA_Quill_Anchor, &an);
  cm_editing = -1;
  if (c && an) {
    Pos s, e;
    int k = doc_comment_at(doc, pos_cmp(*c, *an) < 0 ? *c : *an, &s, &e);
    s.off += 4;
    if (k >= 0 && (!pos_cmp(*c, *an) || (!pos_cmp(pos_cmp(*c, *an) < 0 ? *c : *an, s) && !pos_cmp(pos_cmp(*c, *an) < 0 ? *an : *c, e)))) cm_editing = k;
  }
  if (cm_editing >= 0) { set(cmwin, MUIA_Window_Open, FALSE); story_open(SK_COMMENT, cm_editing, 0, 0); return; }   /* its text edited in place */
  cur[0] = 0;
  set(cmwin, MUIA_Window_Title, (ULONG)tr(cm_editing >= 0 ? "Comment: change its text" : "Comment: a new one"));
  set(cmstr, MUIA_String_Contents, (ULONG)cur);
  set(cmwin, MUIA_Window_Open, TRUE);
  set(cmwin, MUIA_Window_ActiveObject, (ULONG)cmstr);
}
/* the Comment window's OK: the comment added over the selection, by the user's name (or "Quill")
   with its initials and the time now, and comments shown if they were hidden */
static void comment_done(void)
{
  char *t = NULL, u[1600], au[200], ini[12], date[24];
  struct QComment q;
  const char *n;
  int k = 0;
  get(cmstr, MUIA_String_Contents, &t);
  set(cmwin, MUIA_Window_Open, FALSE);
  if (!t || !*t) return;
  latin1_utf8(t, u, sizeof u);
  latin1_utf8(username[0] ? username : "Quill", au, sizeof au);
  /* initials: each word's first letter */
  {
    const char *who = username[0] ? username : "Quill";
    for (n = who; *n && k < 4; n++) if (*n != ' ' && (n == who || n[-1] == ' ')) ini[k++] = *n;
    ini[k] = 0;
  }
  iso_now(date);
  q.text = u; q.author = au; q.initials = ini; q.date = date; q.edit = cm_editing;
  DoMethod(view, MUIM_Quill_Command, CMD_COMMENT, (ULONG)&q);
  if (!(uibits & RBIT(RB_SHOWCOMMENTS))) ui_bit(RB_SHOWCOMMENTS, 1);
  set(win, MUIA_Window_Activate, TRUE);
}
/* spelling: the dictionary (PROGDIR:Dict), the user's words (PROGDIR:Quill.words), and the Spelling
   window that goes from mistake to mistake */
static Spell spell;
static Object *spwin, *spword, *splist, *spto;
static Object *spbtn[5];                  /* Change, Change All, Ignore, Ignore All, Add: greyed when there's nothing to act on */
static char spbad[128];                  /* the word found (UTF-8) */
static Pos spa, spb;
/* the dictionary and the user's own words loaded; with no dictionary tb.spell stays NULL and the
   view underlines nothing */
static void load_spelling(void)
{
  FILE *f;
  char line[128];
  if (!spell_load(&spell, "PROGDIR:Dict/british-english")) return;
  if ((f = fopen("PROGDIR:Quill.words", "r"))) {
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) spell_add(&spell, line, (u32)strlen(line)); }
    fclose(f);
  }
  tb.spell = &spell;
}
/* the next word not in the dictionary from the caret (from_end: from the selection's end, past the
   word just dealt with): selected, shown with its suggestions. At the end of the document the
   window says so and its buttons are greyed */
static void spell_find(int from_end)
{
  Pos *c = NULL, *an = NULL, from;
  static char shown[200];
  char *sug[10];
  int n, i;
  get(view, MUIA_Quill_Caret, &c); get(view, MUIA_Quill_Anchor, &an);
  from = c ? *c : doc_start(doc);
  if (from_end && c && an && pos_cmp(*an, *c) > 0) from = *an;
  DoMethod(splist, MUIM_List_Clear);
  if (!spell_next(&spell, doc, from, &spa, &spb)) {
    spbad[0] = 0;
    set(spword, MUIA_Text_Contents, (ULONG)tr("\33cThe spelling check is complete."));
    set(spto, MUIA_String_Contents, (ULONG)"");
    for (i = 0; i < 5; i++) set(spbtn[i], MUIA_Disabled, TRUE);
    return;
  }
  for (i = 0; i < 5; i++) set(spbtn[i], MUIA_Disabled, FALSE);
  {
    Pos ab[2];
    u32 len = spb.off - spa.off;
    ab[0] = spa; ab[1] = spb;
    DoMethod(view, MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
    if (len >= sizeof spbad) len = sizeof spbad - 1;
    memcpy(spbad, doc->p[spa.para]->t + spa.off, len); spbad[len] = 0;
  }
  {
    char lat[130];
    utf8_to_latin1(spbad, (u32)strlen(spbad), lat, sizeof lat);
    sprintf(shown, tr("\33cNot in the dictionary: \33b%s"), lat);
    set(spword, MUIA_Text_Contents, (ULONG)shown);
  }
  n = spell_suggest(&spell, spbad, (u32)strlen(spbad), sug, 10);
  for (i = 0; i < n; i++) {
    char lat[130];
    utf8_to_latin1(sug[i], (u32)strlen(sug[i]), lat, sizeof lat);
    DoMethod(splist, MUIM_List_InsertSingle, (ULONG)lat, MUIV_List_Insert_Bottom);
    free(sug[i]);
  }
  if (!n) {
    /* nothing close: the word itself to edit by hand */
    char lat[130];
    DoMethod(splist, MUIM_List_InsertSingle, (ULONG)tr("(no suggestions)"), MUIV_List_Insert_Bottom);
    utf8_to_latin1(spbad, (u32)strlen(spbad), lat, sizeof lat);
    set(spto, MUIA_String_Contents, (ULONG)lat);
  } else set(splist, MUIA_List_Active, 0);   /* (its notify puts it in "Change to") */
}
/* a suggestion clicked: it goes in the "Change to" field, where it can still be edited */
static void spell_pick(void)
{
  char *pick = NULL;
  DoMethod(splist, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&pick);
  if (pick && pick[0] != '(') set(spto, MUIA_String_Contents, (ULONG)pick);
}
/* Review > Spelling: the window, starting at the caret */
static void open_spelling(void)
{
  if (!spell.n) { ask("OK", "%s", "Quill has no dictionary: it should be in\nthe Dict drawer next to Quill."); return; }
  if (!spwin) {
    Object *chg, *chgall, *ign, *ignall, *add, *close;
    spwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Spelling"), MUIA_Window_ID, MAKE_ID('S','P','E','L'),
      WindowContents, VGroup, QDLG,
        Child, spword = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", End,
        Child, HGroup,
          Child, Label2(tr("Change to:")),
          Child, spto = StringObject, StringFrame, MUIA_String_MaxLen, 128, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup,
          Child, splist = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, VGroup, MUIA_HorizWeight, 0,
            Child, spbtn[0] = chg = QButton(tr("_Change")), Child, spbtn[1] = chgall = QButton(tr("Change A_ll")),
            Child, spbtn[2] = ign = QButton(tr("_Ignore")), Child, spbtn[3] = ignall = QButton(tr("Ig_nore All")),
            Child, spbtn[4] = add = QButton(tr("_Add to Dictionary")), Child, VSpace(0), Child, close = QButton(tr("Cl_ose")),
          End,
        End,
      End, End;
    if (!spwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)spwin);
    DoMethod(spwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)spwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)spwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(chg, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPCHANGE);
    DoMethod(splist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPCHANGE);
    DoMethod(splist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPPICK);
    DoMethod(spto, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPCHANGE);
    DoMethod(chgall, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPCHANGEALL);
    DoMethod(ign, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPIGNORE);
    DoMethod(ignall, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPIGNOREALL);
    DoMethod(add, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SPADD);
  }
  set(spwin, MUIA_Window_Open, TRUE);
  spell_find(0);
}
/* the Spelling window's buttons. Change All replaces whole words, case and all; Ignore All lasts
   until Quill quits, Add also goes in PROGDIR:Quill.words. Then on to the next word */
static void spelling_command(ULONG id)
{
  char *pick = NULL, u[260];
  if (!spbad[0]) { spell_find(0); return; }
  switch (id) {
  case ID_SPCHANGE: case ID_SPCHANGEALL:
    /* what "Change to" says (a suggestion, perhaps edited, or the user's own word) */
    get(spto, MUIA_String_Contents, &pick);
    if (!pick || !pick[0]) { DisplayBeep(NULL); return; }
    latin1_utf8(pick, u, sizeof u);
    if (id == ID_SPCHANGE) DoMethod(view, MUIM_Quill_Command, CMD_REPLACE, (ULONG)u);
    else {
      struct QFind q;
      memset(&q, 0, sizeof q);
      q.what = spbad; q.with = u; q.flags = FIND_CASE | FIND_WORD;
      DoMethod(view, MUIM_Quill_Command, CMD_REPLACEALL, (ULONG)&q);
    }
    break;
  case ID_SPIGNORE: break;
  case ID_SPIGNOREALL: spell_add(&spell, spbad, (u32)strlen(spbad)); break;
  case ID_SPADD: {
    FILE *f = fopen("PROGDIR:Quill.words", "a");
    spell_add(&spell, spbad, (u32)strlen(spbad));
    if (f) { fprintf(f, "%s\n", spbad); fclose(f); }
    break; }
  }
  DoMethod(view, MUIM_Quill_Command, CMD_REFRESH, 0);
  spell_find(1);
}
static Object *pswin, *psslider;
/* the Picture Size window (its OK is ID_PICSIZEGO, done in command()) */
static void open_picsize(void)
{
  if (!pswin) {
    Object *ok, *cancel;
    pswin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Picture Size"), MUIA_Window_ID, MAKE_ID('P','S','I','Z'),
      WindowContents, VGroup, QDLG,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cWidth, as a share of the column's width:"), End,
        Child, psslider = SliderObject, MUIA_Numeric_Min, 5, MUIA_Numeric_Max, 100, MUIA_Numeric_Value, 50, MUIA_Numeric_Format, (ULONG)"%ld%%", MUIA_CycleChain, 1, End,
        Child, HGroup, Child, ok = QButton(tr("_OK")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
      End, End;
    if (!pswin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)pswin);
    DoMethod(pswin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)pswin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)pswin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PICSIZEGO);
  }
  set(pswin, MUIA_Window_Open, TRUE);
}
/* footnotes: a new one at the caret, or the one just before it (or selected) edited */
static int note_end;                     /* an endnote (else a footnote) */
static int note_by_caret(void)
{
  Pos *c = NULL, *an = NULL;
  Para *p;
  get(view, MUIA_Quill_Caret, &c); get(view, MUIA_Quill_Anchor, &an);
  if (!c || !an || c->para >= doc->np) return -1;
  p = doc->p[c->para];
  if (an->para == c->para && (an->off + 4 == c->off || c->off + 4 == an->off)) {
    u32 lo = an->off < c->off ? an->off : c->off;
    if (lo + 4 <= p->len && (note_end ? IS_ENOTE(p->t + lo) : IS_NOTE(p->t + lo))) return note_index(p->t + lo);
  }
  if (pos_cmp(*c, *an)) return -1;
  if (c->off >= 4 && (note_end ? IS_ENOTE(p->t + c->off - 4) : IS_NOTE(p->t + c->off - 4))) return note_index(p->t + c->off - 4);
  return -1;
}
/* a new note at the caret (or the one just before it) edited in the text's place */
static void open_note(void)
{
  int k = note_by_caret();
  Layout *L = view_layout();
  Pos *c = NULL;
  u32 pg = 0;
  i32 x, y, h;
  story_close(1);
  if (k < 0) {
    char ref[8];
    if ((k = note_end ? doc_add_endnote(doc) : doc_add_note(doc)) < 0) { DisplayBeep(NULL); return; }
    if (note_end) enote_utf8(ref, k); else note_utf8(ref, k);
    DoMethod(view, MUIM_Quill_Command, CMD_TEXT, (ULONG)ref);
    L = view_layout();
  }
  get(view, MUIA_Quill_Caret, &c);
  if (L && c) lay_caret(L, *c, &pg, &x, &y, &h);
  story_open(note_end ? SK_ENOTE : SK_NOTE, k, 0, pg);
}
/* today's date, as "24 October 2026", into t (40 bytes) */
static void today_text(char *t)
{
  static const char *const mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
  static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  struct DateStamp ds;
  LONG days, y = 1978, m = 0;
  DateStamp(&ds);
  days = ds.ds_Days;
  for (;;) { LONG yl = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365; if (days < yl) break; days -= yl; y++; }
  for (m = 0; m < 12; m++) {
    LONG ml = mdays[m] + (m == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (days < ml) break;
    days -= ml;
  }
  sprintf(t, "%ld %s %ld", (long)days + 1, mon[m < 12 ? m : 11], (long)y);
}
/* Insert > Date: today's date at the caret */
static void insert_date(void)
{
  char t[40];
  today_text(t);
  DoMethod(view, MUIM_Quill_Command, CMD_TEXT, (ULONG)t);
}
/* ------------------------------------------------------------------ */
/* the table windows: Split Cells, Table Properties, Sort, Convert and Formula */
/* whether a window is open; the caret (NULL if it is off the end of the text); the caret's
   paragraph's formatting (NULL likewise) */
static int xget_open(Object *w) { ULONG o = 0; get(w, MUIA_Window_Open, &o); return o != 0; }
static Pos *caret_pos(void) { Pos *c = NULL; get(view, MUIA_Quill_Caret, &c); return c && c->para < doc->np ? c : NULL; }
static const ParaFmt *caret_pf(void) { Pos *c = caret_pos(); return c ? &doc->p[c->para]->pf : NULL; }
/* a section of a dialog: its title in bold over its contents, without MUI's engraved frame */
#define QFRAME MUIA_Frame, MUIV_Frame_None, MUIA_InnerLeft, 12, MUIA_InnerBottom, 4
#define QTitle(t) QTitleObject(t)
/* a dialog window with OK and Cancel; OK returns id */
static Object *dialog(const char *title, ULONG wid, Object *contents, const char *oklabel, ULONG id)
{
  Object *w, *ok, *cancel;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(title), MUIA_Window_ID, wid,
    WindowContents, VGroup, QDLG,
      Child, contents,
      Child, HGroup, Child, ok = QButton(tr(oklabel)), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
    End, End;
  if (!w) return NULL;
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id);
  return w;
}
/* for the work packages (wphooks.h): the application, the current window's objects and document,
   and the helpers above */
Object *wp_app(void) { return app; }
Object *wp_win(void) { return win; }
Object *wp_view(void) { return view; }
Object *wp_ribbon(void) { return ribbon; }
Doc *wp_doc(void) { return doc; }
Pos *wp_caret(void) { return caret_pos(); }
int wp_ask(const char *gadgets, const char *fmt, const char *arg) { return ask(gadgets, fmt, arg); }
Object *wp_dialog(const char *title, ULONG wid, Object *contents, const char *oklabel, ULONG okid) { return dialog(title, wid, contents, oklabel, okid); }
void wp_latin1_utf8(const char *latin1, char *utf8, size_t cap) { latin1_utf8(latin1, utf8, cap); }
void wp_story_close(void) { if (doc->edit.kind) story_close(1); }
void wp_refresh(void) { DoMethod(view, MUIM_Quill_Command, CMD_REFRESH, 0); }
const char *wp_docpath(void) { return docpath; }     /* (WP29: the document's file, "" untitled) */
void wp_len_text(i32 tw, char *out) { tab_text(tw, out); }      /* (WP22a: lengths in the dialogs) */
i32 wp_len_parse(const char *s) { return parse_len(s); }
static Object *splitwin, *splitcols, *splitrows;
/* Split Cells, for the caret's cell (a merged cell offers the number of cells it covers) */
static void open_split(void)
{
  const ParaFmt *pf = caret_pf();
  if (!pf || !in_table(pf)) { DisplayBeep(NULL); return; }
  if (!splitwin) {
    Object *c = ColGroup(2),
        Child, Label2(tr("Columns")), Child, splitcols = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 20, MUIA_Numeric_Value, 2, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Rows")), Child, splitrows = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 20, MUIA_Numeric_Value, 1, MUIA_CycleChain, 1, End,
      End;
    if (!(splitwin = dialog("Split Cells", MAKE_ID('S','P','L','T'), c, "_Split", ID_SPLITGO))) return;
  }
  /* a merged cell: back into the cells it covered */
  set(splitcols, MUIA_Numeric_Value, pf->cell.span > 1 ? pf->cell.span : 2);
  set(splitrows, MUIA_Numeric_Value, 1);
  set(splitwin, MUIA_Window_Open, TRUE);
}
/* (columns and rows go to the view packed into one value, columns << 8 | rows) */
static void split_done(void)
{
  ULONG c = 2, r = 1;
  get(splitcols, MUIA_Numeric_Value, &c); get(splitrows, MUIA_Numeric_Value, &r);
  set(splitwin, MUIA_Window_Open, FALSE);
  DoMethod(view, MUIM_Quill_Command, CMD_SPLITCELLS, (c << 8) | r);
  set(win, MUIA_Window_Activate, TRUE);
}
static Object *propwin, *propalign, *propind, *propmt, *propmb, *propml, *propmr, *proprowh, *proprule, *propbreak, *prophead, *propfit;
/* Table Properties, filled from the caret's table and row. A row height is stored signed: above 0
   at least, below 0 exactly, 0 automatic */
static void open_tprops(void)
{
  static const char *aligns[4], *rules[4];
  const ParaFmt *pf = caret_pf();
  Table *t = pf ? doc_table(doc, pf) : NULL;
  char b[24];
  if (!t) { DisplayBeep(NULL); return; }
  if (!propwin) {
    Object *c;
    aligns[0] = tr("Left"); aligns[1] = tr("Centre"); aligns[2] = tr("Right"); aligns[3] = NULL;
    rules[0] = tr("Automatic"); rules[1] = tr("At least"); rules[2] = tr("Exactly"); rules[3] = NULL;
    c = VGroup,
      Child, QTitle(tr("Table")), Child, VGroup, QFRAME,
        Child, ColGroup(2),
          Child, Label1(tr("Alignment")), Child, propalign = CycleObject, MUIA_Cycle_Entries, (ULONG)aligns, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Indent from left")), Child, propind = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, propfit = CheckMark(FALSE), Child, LLabel1(tr("Automatically resize to fit contents")), Child, HSpace(0), End,
      End,
      Child, QTitle(tr("Cell margins")), Child, VGroup, QFRAME,
        Child, ColGroup(4),
          Child, Label2(tr("Top")), Child, propmt = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Left")), Child, propml = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Bottom")), Child, propmb = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Right")), Child, propmr = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        End,
      End,
      Child, QTitle(tr("Row")), Child, VGroup, QFRAME,
        Child, ColGroup(3),
          Child, Label2(tr("Height")), Child, proprowh = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, proprule = CycleObject, MUIA_Cycle_Entries, (ULONG)rules, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, propbreak = CheckMark(TRUE), Child, LLabel1(tr("Allow row to break across pages")), Child, HSpace(0), End,
        Child, HGroup, Child, prophead = CheckMark(FALSE), Child, LLabel1(tr("Repeat as header row at the top of each page")), Child, HSpace(0), End,
      End,
    End;
    if (!(propwin = dialog("Table Properties", MAKE_ID('T','P','R','P'), c, "_OK", ID_TPROPSGO))) return;
  }
  set(propalign, MUIA_Cycle_Active, t->jc % 3);
  tab_text(t->indent, b); set(propind, MUIA_String_Contents, (ULONG)b);
  tab_text(t->mart, b); set(propmt, MUIA_String_Contents, (ULONG)b);
  tab_text(t->marb, b); set(propmb, MUIA_String_Contents, (ULONG)b);
  tab_text(t->marl, b); set(propml, MUIA_String_Contents, (ULONG)b);
  tab_text(t->marr, b); set(propmr, MUIA_String_Contents, (ULONG)b);
  tab_text(pf->cell.rowh < 0 ? -pf->cell.rowh : pf->cell.rowh, b); set(proprowh, MUIA_String_Contents, (ULONG)(pf->cell.rowh ? b : ""));
  set(proprule, MUIA_Cycle_Active, pf->cell.rowh > 0 ? 1 : pf->cell.rowh < 0 ? 2 : 0);
  set(propbreak, MUIA_Selected, !pf->cell.cantsplit);
  set(prophead, MUIA_Selected, pf->cell.header != 0);
  set(propfit, MUIA_Selected, t->autofit != 0);
  set(propwin, MUIA_Window_Open, TRUE);
}
/* a string gadget's length in twips (0 when empty or unreadable) */
static i32 str_len(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? parse_len(s) : 0; }
/* Table Properties' OK: everything in it sent as one change */
static void tprops_done(void)
{
  struct QTableProps q;
  ULONG v = 0;
  i32 h;
  memset(&q, 0, sizeof q);
  get(propalign, MUIA_Cycle_Active, &v); q.jc = (u8)v;
  q.indent = str_len(propind);
  q.mart = str_len(propmt); q.marb = str_len(propmb); q.marl = str_len(propml); q.marr = str_len(propmr);
  h = str_len(proprowh);
  if (h > 31000) h = 31000;
  get(proprule, MUIA_Cycle_Active, &v);
  q.rowh = (i16)(v == 0 || !h ? 0 : v == 1 ? h : -h);
  get(propbreak, MUIA_Selected, &v); q.cantsplit = (u8)!v;
  get(prophead, MUIA_Selected, &v); q.header = (u8)(v != 0);
  get(propfit, MUIA_Selected, &v); q.autofit = (u8)(v != 0);
  set(propwin, MUIA_Window_Open, FALSE);
  DoMethod(view, MUIM_Quill_Command, CMD_TPROPS, (ULONG)&q);
  set(win, MUIA_Window_Activate, TRUE);
}
/* ------------------------------------------------------------------ */
/* pictures and objects: the one selected, changed through the view (one undo step each) */
static int sel_obj(void)
{
  LONG k = -1;
  get(view, MUIA_Quill_Object, &k);
  return k >= 0 && k < doc->nimages ? (int)k : -1;
}
/* the selected object replaced by m (one undo step) */
static void obj_set(const Image *m) { DoMethod(view, MUIM_Quill_Command, CMD_OBJSET, (ULONG)m); }
/* the Format Object window: size, crop and turning; position; wrapping; alt text; a text box's text */
#define OBJ_LINES 6
enum { OP_SIZE, OP_POSITION, OP_WRAP, OP_ALT, OP_TEXT };
static Object *objwin, *objreg, *ob_h, *ob_w, *ob_lock, *ob_rot, *ob_fliph, *ob_flipv, *ob_crop[4], *ob_ha, *ob_hx, *ob_hr, *ob_va, *ob_vy, *ob_vr,
              *ob_wrap, *ob_dist[4], *ob_alt, *ob_text[OBJ_LINES];
static int objk = -1;
static char ob_hs[24], ob_ws[24];        /* the size as the window showed it */
/* a crop in 1000ths of a per cent (as .docx keeps it) as "12.5", and back: parse_pct, which
   allows 49.9 at most so the two sides can't meet */
static void pct_text(i32 v, char *out) { sprintf(out, "%ld.%ld", (long)(v / 1000), (long)(v / 100 % 10)); }
static i32 parse_pct(Object *o)
{
  char *s = NULL;
  long whole = 0, tenth = 0;
  get(o, MUIA_String_Contents, &s);
  if (!s) return 0;
  while (*s == ' ') s++;
  while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
  if ((*s == '.' || *s == ',') && s[1] >= '0' && s[1] <= '9') tenth = s[1] - '0';
  if (whole > 49) { whole = 49; tenth = 0; }
  return (i32)(whole * 1000 + tenth * 100);
}
/* Format Picture or Format Shape, for the selected object, on one of its OP_ tabs (a register group:
   MUI's tabbed pages). The object's index is remembered so OK can check it is still the one selected */
static void open_object(int page)
{
  static const char *titles[6], *halign[5], *hrel[4], *valign[5], *vrel[4], *wrapk[7];
  Image *im;
  char b[200];
  int i, k = sel_obj();
  if (k < 0) { DisplayBeep(NULL); return; }
  im = &doc->images[k];
  objk = k;
  if (!objwin) {
    Object *c;
    titles[0] = tr("Size"); titles[1] = tr("Position"); titles[2] = tr("Text Wrapping"); titles[3] = tr("Alt Text"); titles[4] = tr("Text"); titles[5] = NULL;
    halign[0] = tr("Left"); halign[1] = tr("Centre"); halign[2] = tr("Right"); halign[3] = tr("Offset"); halign[4] = NULL;
    hrel[0] = tr("Column"); hrel[1] = tr("Page"); hrel[2] = tr("Margin"); hrel[3] = NULL;
    valign[0] = tr("Top"); valign[1] = tr("Centre"); valign[2] = tr("Bottom"); valign[3] = tr("Offset"); valign[4] = NULL;
    vrel[0] = tr("Paragraph"); vrel[1] = tr("Page"); vrel[2] = tr("Margin"); vrel[3] = NULL;
    wrapk[0] = tr("In Line with Text"); wrapk[1] = tr("Square"); wrapk[2] = tr("Tight"); wrapk[3] = tr("Top and Bottom");
    wrapk[4] = tr("Behind Text"); wrapk[5] = tr("In Front of Text"); wrapk[6] = NULL;
    c = objreg = RegisterGroup(titles), MUIA_CycleChain, 1,
      /* size, turning, crop */
      Child, VGroup,
        Child, QTitle(tr("Size")), Child, VGroup, QFRAME,
          Child, ColGroup(4),
            Child, Label2(tr("Height")), Child, ob_h = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
            Child, Label2(tr("Width")), Child, ob_w = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          End,
          Child, HGroup, Child, ob_lock = CheckMark(TRUE), Child, LLabel1(tr("Lock aspect ratio")), Child, HSpace(0), End,
        End,
        Child, QTitle(tr("Rotate")), Child, HGroup, QFRAME,
          Child, Label2(tr("Rotation")), Child, ob_rot = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,-", MUIA_FixWidthTxt, (ULONG)"0000000", MUIA_CycleChain, 1, End,
          Child, LLabel1(tr("degrees")),
          Child, ob_fliph = CheckMark(FALSE), Child, LLabel1(tr("Flip horizontal")),
          Child, ob_flipv = CheckMark(FALSE), Child, LLabel1(tr("Flip vertical")),
        End,
        Child, QTitle(tr("Crop (per cent cut from each side)")), Child, ColGroup(4), QFRAME,
          Child, Label2(tr("Left")), Child, ob_crop[0] = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
          Child, Label2(tr("Top")), Child, ob_crop[1] = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
          Child, Label2(tr("Right")), Child, ob_crop[2] = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
          Child, Label2(tr("Bottom")), Child, ob_crop[3] = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
        End,
      End,
      /* position */
      Child, VGroup,
        Child, QTitle(tr("Horizontal")), Child, ColGroup(4), QFRAME,
          Child, ob_ha = CycleObject, MUIA_Cycle_Entries, (ULONG)halign, MUIA_CycleChain, 1, End,
          Child, ob_hx = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label1(tr("relative to")), Child, ob_hr = CycleObject, MUIA_Cycle_Entries, (ULONG)hrel, MUIA_CycleChain, 1, End,
        End,
        Child, QTitle(tr("Vertical")), Child, ColGroup(4), QFRAME,
          Child, ob_va = CycleObject, MUIA_Cycle_Entries, (ULONG)valign, MUIA_CycleChain, 1, End,
          Child, ob_vy = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label1(tr("relative to")), Child, ob_vr = CycleObject, MUIA_Cycle_Entries, (ULONG)vrel, MUIA_CycleChain, 1, End,
        End,
        Child, VSpace(0),
      End,
      /* wrapping */
      Child, VGroup,
        Child, QTitle(tr("Wrapping style")), Child, HGroup, QFRAME,
          Child, ob_wrap = CycleObject, MUIA_Cycle_Entries, (ULONG)wrapk, MUIA_CycleChain, 1, End,
        End,
        Child, QTitle(tr("Distance from text")), Child, ColGroup(4), QFRAME,
          Child, Label2(tr("Top")), Child, ob_dist[0] = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Left")), Child, ob_dist[2] = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Bottom")), Child, ob_dist[1] = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Right")), Child, ob_dist[3] = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        End,
        Child, VSpace(0),
      End,
      /* alt text */
      Child, VGroup,
        Child, LLabel(tr("A description of the picture or shape for people who can't see it:")),
        Child, ob_alt = StringObject, StringFrame, MUIA_String_MaxLen, 158, MUIA_CycleChain, 1, End,
        Child, VSpace(0),
      End,
      /* text */
      Child, VGroup,
        Child, LLabel(tr("The text in the box, a paragraph to a line:")),
        Child, ob_text[0] = StringObject, StringFrame, MUIA_String_MaxLen, 400, MUIA_CycleChain, 1, End,
        Child, ob_text[1] = StringObject, StringFrame, MUIA_String_MaxLen, 400, MUIA_CycleChain, 1, End,
        Child, ob_text[2] = StringObject, StringFrame, MUIA_String_MaxLen, 400, MUIA_CycleChain, 1, End,
        Child, ob_text[3] = StringObject, StringFrame, MUIA_String_MaxLen, 400, MUIA_CycleChain, 1, End,
        Child, ob_text[4] = StringObject, StringFrame, MUIA_String_MaxLen, 400, MUIA_CycleChain, 1, End,
        Child, ob_text[5] = StringObject, StringFrame, MUIA_String_MaxLen, 400, MUIA_CycleChain, 1, End,
      End,
    End;
    if (!(objwin = dialog("Format Object", MAKE_ID('O','B','J','F'), c, "_OK", ID_OBJGO))) return;
  }
  set(objwin, MUIA_Window_Title, (ULONG)tr(im->kind == OBJ_PICTURE ? "Format Picture" : "Format Shape"));
  /* its own size (unturned), as Word shows it */
  tab_text(im->cy, ob_hs); set(ob_h, MUIA_String_Contents, (ULONG)ob_hs);
  tab_text(im->cx, ob_ws); set(ob_w, MUIA_String_Contents, (ULONG)ob_ws);
  set(ob_lock, MUIA_Selected, im->kind == OBJ_PICTURE || im->kind == OBJ_GROUP);
  { char deg[16]; if (im->rot % 10) sprintf(deg, "%d.%d", im->rot / 10, im->rot % 10); else sprintf(deg, "%d", im->rot / 10); set(ob_rot, MUIA_String_Contents, (ULONG)deg); }
  set(ob_fliph, MUIA_Selected, (im->flip & 1) != 0); set(ob_flipv, MUIA_Selected, (im->flip & 2) != 0);
  for (i = 0; i < 4; i++) { pct_text(im->crop[i], b); set(ob_crop[i], MUIA_String_Contents, (ULONG)b); set(ob_crop[i], MUIA_Disabled, im->kind != OBJ_PICTURE); }
  set(ob_ha, MUIA_Cycle_Active, im->halign == PA_NONE ? 3 : im->halign - 1);
  tab_text(im->x, b); set(ob_hx, MUIA_String_Contents, (ULONG)b);
  set(ob_hr, MUIA_Cycle_Active, im->hrel == POS_PAGE ? 1 : im->hrel == POS_MARGIN ? 2 : 0);
  set(ob_va, MUIA_Cycle_Active, im->valign == PA_NONE ? 3 : im->valign - 1);
  tab_text(im->y < 0 ? -im->y : im->y, b);
  if (im->y < 0) { memmove(b + 1, b, strlen(b) + 1); b[0] = '-'; }
  set(ob_vy, MUIA_String_Contents, (ULONG)b);
  set(ob_vr, MUIA_Cycle_Active, im->vrel == POS_PAGE ? 1 : im->vrel == POS_MARGIN ? 2 : 0);
  set(ob_wrap, MUIA_Cycle_Active, im->wrap);
  tab_text(im->dt, b); set(ob_dist[0], MUIA_String_Contents, (ULONG)b);
  tab_text(im->db, b); set(ob_dist[1], MUIA_String_Contents, (ULONG)b);
  tab_text(im->dl, b); set(ob_dist[2], MUIA_String_Contents, (ULONG)b);
  tab_text(im->dr, b); set(ob_dist[3], MUIA_String_Contents, (ULONG)b);
  utf8_to_latin1(im->alt, strlen(im->alt), b, sizeof b); set(ob_alt, MUIA_String_Contents, (ULONG)b);
  {
    char *t = im->text ? story_paras(im->text) : NULL, *p = t;
    for (i = 0; i < OBJ_LINES; i++) {
      char line[400] = "";
      if (p && *p) {
        char *e = i + 1 < OBJ_LINES ? strchr(p, '\n') : NULL;
        size_t n = e ? (size_t)(e - p) : strlen(p);
        utf8_to_latin1(p, n, line, sizeof line);
        p = e ? e + 1 : p + n;
      }
      set(ob_text[i], MUIA_String_Contents, (ULONG)line);
      set(ob_text[i], MUIA_Disabled, im->kind == OBJ_PICTURE);
    }
    free(t);
  }
  if (page == OP_TEXT && im->kind == OBJ_PICTURE) page = OP_SIZE;
  set(objreg, MUIA_Group_ActivePage, page);
  set(objwin, MUIA_Window_Open, TRUE);
  set(objwin, MUIA_Window_ActiveObject, (ULONG)(page == OP_TEXT ? ob_text[0] : page == OP_ALT ? ob_alt : page == OP_POSITION ? ob_ha : page == OP_WRAP ? ob_wrap : ob_h));
}
/* as str_len, with a minus sign allowed (positions) */
static i32 str_slen(Object *o)
{
  char *s = NULL;
  get(o, MUIA_String_Contents, &s);
  if (!s) return 0;
  while (*s == ' ') s++;
  return *s == '-' ? -parse_len(s + 1) : parse_len(s);
}
/* Format Object's OK. A size field is only read if it was edited (so tab_text's rounding can't
   change a size nobody touched); a turned or resized floating object keeps its centre; moving in or
   out of the line goes through the view first, which works out where it lands */
static void object_done(void)
{
  Image m;
  Story st;
  ULONG v = 0;
  char *s = NULL;
  i32 h, w;
  int i, k = sel_obj(), text = 0;
  set(objwin, MUIA_Window_Open, FALSE);
  if (qlog) { fprintf(qlog, "object %d (opened on %d)\n", k, objk); fflush(qlog); }
  if (k < 0 || k != objk) { DisplayBeep(NULL); return; }
  m = doc->images[k];
  /* its own size (a changed side scales the other when locked) */
  get(ob_h, MUIA_String_Contents, &s); h = s && strcmp(s, ob_hs) ? parse_len(s) : m.cy;
  get(ob_w, MUIA_String_Contents, &s); w = s && strcmp(s, ob_ws) ? parse_len(s) : m.cx;
  get(ob_lock, MUIA_Selected, &v);
  if (v && m.cx > 0 && m.cy > 0) {
    if (h != m.cy && w == m.cx) w = (i32)((long)h * m.cx / m.cy);
    else if (w != m.cx && h == m.cy) h = (i32)((long)w * m.cy / m.cx);
  }
  if (h < 60) h = 60;
  if (w < 60) w = 60;
  m.cx = w; m.cy = h;
  {
    /* the angle in degrees (a tenth at most), clockwise */
    char *r = NULL;
    long deg = 0, tenth = 0;
    int neg = 0;
    get(ob_rot, MUIA_String_Contents, &r);
    if (r) {
      while (*r == ' ') r++;
      if (*r == '-') { neg = 1; r++; }
      while (*r >= '0' && *r <= '9') deg = deg * 10 + (*r++ - '0');
      if ((*r == '.' || *r == ',') && r[1] >= '0' && r[1] <= '9') tenth = r[1] - '0';
    }
    deg = (deg * 10 + tenth) % 3600;
    if (neg) deg = (3600 - deg) % 3600;
    m.rot = (u16)deg;
  }
  if (OBJ_IS_GRAPHIC(m.kind)) { m.rot = 0; m.flip = 0; }    /* (WP15a) */
  if (m.wrap != WRAP_INLINE && !m.halign && (m.rot != doc->images[k].rot || m.cx != doc->images[k].cx || m.cy != doc->images[k].cy)) {
    /* turned or sized round its centre */
    Image o = doc->images[k];
    m.x += (OBJ_W(&o) - OBJ_W(&m)) / 2;
    if (!m.valign) m.y += (OBJ_H(&o) - OBJ_H(&m)) / 2;
  }
  m.flip = 0;
  get(ob_fliph, MUIA_Selected, &v); if (v) m.flip |= 1;
  get(ob_flipv, MUIA_Selected, &v); if (v) m.flip |= 2;
  if (m.kind == OBJ_PICTURE) for (i = 0; i < 4; i++) m.crop[i] = parse_pct(ob_crop[i]);
  /* the position */
  get(ob_ha, MUIA_Cycle_Active, &v); m.halign = (u8)(v == 3 ? PA_NONE : v + 1);
  m.x = str_slen(ob_hx);
  get(ob_hr, MUIA_Cycle_Active, &v); m.hrel = (u8)(v == 1 ? POS_PAGE : v == 2 ? POS_MARGIN : POS_COLUMN);
  get(ob_va, MUIA_Cycle_Active, &v); m.valign = (u8)(v == 3 ? PA_NONE : v + 1);
  m.y = str_slen(ob_vy);
  get(ob_vr, MUIA_Cycle_Active, &v); m.vrel = (u8)(v == 1 ? POS_PAGE : v == 2 ? POS_MARGIN : POS_PARA);
  /* wrapping */
  get(ob_wrap, MUIA_Cycle_Active, &v);
  if (v != m.wrap && (v == WRAP_INLINE || m.wrap == WRAP_INLINE)) {
    /* in or out of the line: the view works out where it goes */
    DoMethod(view, MUIM_Quill_Command, CMD_OBJWRAP, (ULONG)v);
    if ((k = sel_obj()) < 0) return;
    if (v != WRAP_INLINE) { m.hrel = doc->images[k].hrel; m.halign = doc->images[k].halign; m.x = doc->images[k].x; m.vrel = doc->images[k].vrel; m.valign = doc->images[k].valign; m.y = doc->images[k].y; }
    m.data = doc->images[k].data;
  }
  m.wrap = (u8)v;
  m.dt = str_slen(ob_dist[0]); m.db = str_slen(ob_dist[1]); m.dl = str_slen(ob_dist[2]); m.dr = str_slen(ob_dist[3]);
  get(ob_alt, MUIA_String_Contents, &s);
  latin1_utf8(s ? s : "", m.alt, sizeof m.alt);
  /* a text box's (or shape's) text */
  st.p = NULL; st.np = 0;
  if (m.kind != OBJ_PICTURE) {
    char all[OBJ_LINES * 401] = "";
    int last = -1;
    for (i = 0; i < OBJ_LINES; i++) { get(ob_text[i], MUIA_String_Contents, &s); if (s && *s) last = i; }
    for (i = 0; i <= last; i++) { get(ob_text[i], MUIA_String_Contents, &s); if (i) strcat(all, "\n"); strcat(all, s ? s : ""); }
    {
      char *t = (char *)malloc(sizeof all * 2);
      if (t) {
        latin1_utf8(all, t, sizeof all * 2);
        if (last >= 0 || m.text) text = story_set_paras(doc, &st, t, doc->images[k].text);
        free(t);
      }
    }
    if (text) m.text = &st;
  }
  obj_set(&m);
  story_free(&st);
  set(win, MUIA_Window_Activate, TRUE);
}
/* the ribbon's lists for the selected object */
static void obj_choice(ULONG id, LONG v)
{
  Image m;
  int k = sel_obj();
  if (id == ID_SHAPESDD) { if (v >= 0 && v < SH_N) DoMethod(view, MUIM_Quill_Command, CMD_SHAPE, (ULONG)v); return; }
  if (k < 0) { DisplayBeep(NULL); return; }
  m = doc->images[k];
  switch (id) {
  case ID_OBJFILLDD: m.fill = (ULONG)v == 0xFF000000UL ? COLOR_AUTO : (u32)v; break;
  case ID_OBJLINEDD:
    if ((ULONG)v == 0xFF000000UL) m.lstyle = PB_NONE;
    else { m.lcolor = (u32)v; if (m.lstyle == PB_NONE) m.lstyle = PB_SINGLE; if (!m.lwidth) m.lwidth = 6; }
    break;
  case ID_OBJWEIGHTDD: if (v < 0 || v >= 7) return; m.lwidth = ribbon_penwidths[v]; if (m.lstyle == PB_NONE) m.lstyle = PB_SINGLE; break;
  case ID_OBJDASHDD: if (v < 0 || v >= 5) return; m.lstyle = (u8)(v + PB_SINGLE); if (!m.lwidth) m.lwidth = 6; break;
  case ID_OBJROTDD:
    if (OBJ_IS_GRAPHIC(m.kind)) { DisplayBeep(NULL); return; }      /* (WP15a) charts and SmartArt don't turn */
    if (v == 0) m.rot = (u16)((m.rot + 900) % 3600);
    else if (v == 1) m.rot = (u16)((m.rot + 2700) % 3600);
    else if (v == 2) m.flip ^= 2;
    else if (v == 3) m.flip ^= 1;
    else if (v == 4) { open_object(OP_SIZE); return; }       /* any angle */
    else return;
    if (v < 2 && m.wrap != WRAP_INLINE && !m.halign && !m.valign) {
      /* turned round its centre */
      Image o = doc->images[k];
      m.x += (OBJ_W(&o) - OBJ_W(&m)) / 2;
      if (m.vrel != POS_PARA || !m.valign) m.y += (OBJ_H(&o) - OBJ_H(&m)) / 2;
    }
    break;
  case ID_OBJWRAPDD:
    if (v == 6) { open_object(OP_WRAP); return; }
    if (v >= WRAP_INLINE && v <= WRAP_FRONT) DoMethod(view, MUIM_Quill_Command, CMD_OBJWRAP, (ULONG)v);
    return;
  case ID_OBJPOSDD:
    if (v == 0) { DoMethod(view, MUIM_Quill_Command, CMD_OBJWRAP, WRAP_INLINE); return; }
    if (v < 1 || v > 9) return;
    /* on a 3 x 3 grid of the margins, text wrapping square round it */
    m.hrel = POS_MARGIN; m.halign = (u8)((v - 1) % 3 + 1); m.vrel = POS_MARGIN; m.valign = (u8)((v - 1) / 3 + 1);
    if (m.wrap == WRAP_INLINE) m.wrap = WRAP_SQUARE;
    break;
  default: return;
  }
  obj_set(&m);
}
/* ------------------------------------------------------------------ */
/* styles and formatting (0.6): the Font, Paragraph, Borders and Shading, Style and Styles windows */
static const char *const fx_colnames[] = { "Automatic", "Black", "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue", "Dark Blue", "Purple", "Grey", NULL };
static const u32 fx_colvals[] = { COLOR_AUTO, 0x000000, 0xC00000, 0xFF0000, 0xFFA500, 0xBF9000, 0x00B050, 0x008080, 0x0070C0, 0x002060, 0x7030A0, 0x808080 };
#define NCOLS_FX 12
static const char *fx_tr[64];
/* a NULL-ended list of the English names, in the interface's language (kept while Quill runs) */
static const char **trl(const char *const *en, const char **out)
{
  int i;
  for (i = 0; en[i]; i++) out[i] = tr(en[i]);
  out[i] = NULL;
  return out;
}
/* a colour's place in fx_colvals (black also as 0), Automatic for one not on the list */
static int col_index(u32 c) { int i; for (i = 0; i < NCOLS_FX; i++) if (fx_colvals[i] == c) return i; return c == 0 ? 1 : 0; }
/* twips as points for a field: "6 pt", "10.5 pt" */
static void pt_text(i32 tw, char *out)
{
  if (tw % 20) sprintf(out, "%ld.%ld pt", (long)(tw / 20), (long)((tw < 0 ? -tw : tw) % 20 * 10 / 20)); else sprintf(out, "%ld pt", (long)(tw / 20));
}
/* "6", "6 pt", "1.5": twips; negative allowed */
static i32 parse_pt_s(const char *s)
{
  long whole = 0, frac = 0, div = 1;
  int neg = 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
  if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9' && div < 1000) { frac = frac * 10 + (*s++ - '0'); div *= 10; } }
  whole = whole * 20 + frac * 20 / div;
  return (i32)(neg ? -whole : whole);
}
/* small readers and writers for the dialogs' gadgets: a field in points (as twips), a string set, a
   cycle's choice, a checkmark's state */
static i32 str_pt(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? parse_pt_s(s) : 0; }
static void set_str(Object *o, const char *s) { set(o, MUIA_String_Contents, (ULONG)s); }
static ULONG cyc(Object *o) { ULONG v = 0; get(o, MUIA_Cycle_Active, &v); return v; }
static ULONG chk(Object *o) { ULONG v = 0; get(o, MUIA_Selected, &v); return v != 0; }
/* a family field: a string with the list of every family to choose from (Latin-1 for MUI) */
static const char *fam_l1[201];
static struct { Object *pop, *list, *str; Object **owner; } fams[4];
static int nfam;
/* a font family field (a string with a pop-up list of every family) for a dialog; *str gets the
   string gadget. Each dialog has its own slot in fams, found by str (the dialog's own variable), so
   a dialog that is built afresh each time it opens (Modify Style) takes its old slot again instead
   of using up a new one, and fam_pick never reaches a field of a window that has gone. NULL if
   all 4 slots belong to other dialogs */
static Object *family_field(Object **str)
{
  if (!fam_l1[0]) {
    const char *const *n = ribbon_font_names(&fonts);
    int i;
    for (i = 0; n[i] && i < 200; i++) {
      char t[64], *c;
      utf8_to_latin1(n[i], (u32)strlen(n[i]), t, sizeof t);
      fam_l1[i] = (c = (char *)malloc(strlen(t) + 1)) ? strcpy(c, t) : "";
    }
    fam_l1[i] = NULL;
  }
  /* (Popobject and a list, as Poplist isn't everywhere; a pick goes into the string: fam_pick) */
  {
    Object *o, *lv;
    int k;
    for (k = 0; k < nfam && fams[k].owner != str; k++) ;
    if (k == 4) return NULL;
    o = PopobjectObject,
      MUIA_Popstring_String, *str = StringObject, StringFrame, MUIA_String_MaxLen, 48, MUIA_CycleChain, 1, End,
      MUIA_Popstring_Button, PopButton(MUII_PopUp),
      MUIA_Popobject_Object, lv = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)fam_l1, End, End,
    End;
    if (!o) { if (k < nfam) fams[k].pop = fams[k].list = fams[k].str = NULL; return NULL; }
    fams[k].pop = o; fams[k].list = lv; fams[k].str = *str; fams[k].owner = str;
    if (k == nfam) nfam++;
    DoMethod(lv, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FAMPICK);
    return o;
  }
}
/* a family picked from a field's list: into its string, and the list closed */
static void fam_pick(void)
{
  int i;
  for (i = 0; i < nfam; i++) {
    LONG a = MUIV_List_Active_Off;
    char *e = NULL;
    if (!fams[i].list) continue;
    get(fams[i].list, MUIA_List_Active, &a);
    if (a == MUIV_List_Active_Off) continue;
    DoMethod(fams[i].list, MUIM_List_GetEntry, a, (ULONG)&e);
    if (e) set_str(fams[i].str, e);
    nnset(fams[i].list, MUIA_List_Active, MUIV_List_Active_Off);
    DoMethod(fams[i].pop, MUIM_Popstring_Close, FALSE);
  }
}
/* a family field set from a UTF-8 name */
static void family_set(Object *str, const char *utf8)
{
  char t[64];
  utf8_to_latin1(utf8, (u32)strlen(utf8), t, sizeof t);
  set_str(str, t);
}
/* the family a field holds (UTF-8); "" if empty */
static const char *family_get(Object *str, char *u, size_t cap)
{
  char *s = NULL;
  get(str, MUIA_String_Contents, &s);
  latin1_utf8(s ? s : "", u, cap);
  return u;
}
/* the character formatting at the caret (of the text just before it) */
static const CharFmt *caret_cf(void)
{
  Pos *c = caret_pos();
  return c ? para_fmt_before(doc->p[c->para], c->off) : &doc->p[0]->r[0].f;
}

static void styles_fill(void);
static int pf_same_g(const ParaFmt *a, const ParaFmt *b, unsigned g);
/* ---- the Font window */
static Object *fontwin, *fo_font, *fo_style, *fo_size, *fo_color, *fo_ul, *fo_strike, *fo_dstrike, *fo_super, *fo_sub, *fo_small, *fo_caps,
              *fo_spc, *fo_spcby, *fo_scale, *fo_pos, *fo_posby;
static CharFmt fo_was;
static const u16 scales[] = { 100, 200, 150, 90, 80, 66, 50, 33 };
/* the Font window, filled from the caret's formatting (kept in fo_was to compare with on OK) */
static void open_font(void)
{
  static const char *const styles[] = { "Regular", "Italic", "Bold", "Bold Italic", NULL }, *const uls[] = { "(none)", "Single", "Double", "Thick", "Dotted", "Dashed", "Wave", "Words only", NULL },
               *const spcs[] = { "Normal", "Expanded", "Condensed", NULL }, *const poss[] = { "Normal", "Raised", "Lowered", NULL },
               *const scl[] = { "100%", "200%", "150%", "90%", "80%", "66%", "50%", "33%", NULL };
  static const char *t_styles[6], *t_uls[10], *t_spcs[4], *t_poss[4], *t_cols[16];
  const CharFmt *f = caret_cf();
  char b[24];
  int i;
  if (!fontwin) {
    Object *c = VGroup,
      Child, QTitle(tr("Font")), Child, ColGroup(4), QFRAME,
        Child, Label1(tr("Font")), Child, family_field(&fo_font),
        Child, Label1(tr("Font style")), Child, fo_style = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(styles, t_styles), MUIA_CycleChain, 1, End,
        Child, Label2(tr("Size")), Child, fo_size = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
        Child, Label1(tr("Colour")), Child, fo_color = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(fx_colnames, t_cols), MUIA_CycleChain, 1, End,
        Child, Label1(tr("Underline")), Child, fo_ul = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(uls, t_uls), MUIA_CycleChain, 1, End,
        Child, HSpace(0), Child, HSpace(0),
      End,
      Child, QTitle(tr("Effects")), Child, ColGroup(6), QFRAME,
        Child, fo_strike = CheckMark(FALSE), Child, LLabel1(tr("Strikethrough")),
        Child, fo_dstrike = CheckMark(FALSE), Child, LLabel1(tr("Double strikethrough")),
        Child, fo_super = CheckMark(FALSE), Child, LLabel1(tr("Superscript")),
        Child, fo_sub = CheckMark(FALSE), Child, LLabel1(tr("Subscript")),
        Child, fo_small = CheckMark(FALSE), Child, LLabel1(tr("Small caps")),
        Child, fo_caps = CheckMark(FALSE), Child, LLabel1(tr("All caps")),
      End,
      Child, HGroup, QFRAME, Child, wp9_font_group(), End,       /* (WP9: hidden, outline, shadow, emboss, engrave, underline colour, shading) */
      Child, QTitle(tr("Character spacing")), Child, ColGroup(3), QFRAME,
        Child, Label1(tr("Spacing")), Child, fo_spc = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(spcs, t_spcs), MUIA_CycleChain, 1, End,
        Child, fo_spcby = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Scale")), Child, fo_scale = CycleObject, MUIA_Cycle_Entries, (ULONG)scl, MUIA_CycleChain, 1, End, Child, HSpace(0),
        Child, Label1(tr("Position")), Child, fo_pos = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(poss, t_poss), MUIA_CycleChain, 1, End,
        Child, fo_posby = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_CycleChain, 1, End,
      End,
    End;
    if (!(fontwin = dialog("Font", MAKE_ID('F','O','N','T'), c, "_OK", ID_FONTGO))) return;
  }
  fo_was = *f;
  family_set(fo_font, f->font < doc->nfonts ? doc->fonts[f->font] : "Times New Roman");
  set(fo_style, MUIA_Cycle_Active, ((f->flags & CF_BOLD) ? 2 : 0) + ((f->flags & CF_ITALIC) ? 1 : 0));
  sprintf(b, f->size & 1 ? "%u.5" : "%u", (unsigned)(f->size / 2)); set_str(fo_size, b);
  set(fo_color, MUIA_Cycle_Active, col_index(f->color));
  set(fo_ul, MUIA_Cycle_Active, (f->flags & CF_UNDERLINE) ? f->ul + 1 : 0);
  set(fo_strike, MUIA_Selected, (f->flags & CF_STRIKE) != 0); set(fo_dstrike, MUIA_Selected, (f->fx & FX_DSTRIKE) != 0);
  set(fo_super, MUIA_Selected, (f->flags & CF_SUPER) != 0); set(fo_sub, MUIA_Selected, (f->flags & CF_SUB) != 0);
  set(fo_small, MUIA_Selected, (f->fx & FX_SMALLCAPS) != 0); set(fo_caps, MUIA_Selected, (f->flags & CF_CAPS) != 0);
  wp9_font_fill(f);                                    /* (WP9) */
  set(fo_spc, MUIA_Cycle_Active, f->spacing > 0 ? 1 : f->spacing < 0 ? 2 : 0);
  pt_text(f->spacing < 0 ? -f->spacing : f->spacing, b); set_str(fo_spcby, f->spacing ? b : "");
  for (i = 0; i < 8 && scales[i] != (f->scale ? f->scale : 100); i++) ;
  set(fo_scale, MUIA_Cycle_Active, i < 8 ? i : 0);
  set(fo_pos, MUIA_Cycle_Active, f->position > 0 ? 1 : f->position < 0 ? 2 : 0);
  pt_text((f->position < 0 ? -f->position : f->position) * 10, b); set_str(fo_posby, f->position ? b : "");
  set(fontwin, MUIA_Window_Open, TRUE);
}
/* the Font window's OK. Sizes are in half-points, spacing in twips, raise or lower in half-points */
static void font_done(void)
{
  struct QCharFmt q;
  CharFmt *n = &q.cf;
  const CharFmt *w = &fo_was;
  char *s = NULL;
  ULONG v;
  i32 sz;
  set(fontwin, MUIA_Window_Open, FALSE);
  memset(&q, 0, sizeof q);
  *n = *w;
  /* only what was changed in the window (a selection of mixed formatting keeps the rest) */
  {
    char fam[100];
    family_get(fo_font, fam, sizeof fam);
    if (fam[0]) {
      n->font = (u8)doc_font(doc, fam);
      if (w->font >= doc->nfonts || !str_ieq(doc->fonts[w->font], fam)) q.mask |= CM_FONT;
    }
  }
  v = cyc(fo_style);
  n->flags = (u8)((n->flags & ~(CF_BOLD | CF_ITALIC)) | (v & 2 ? CF_BOLD : 0) | (v & 1 ? CF_ITALIC : 0));
  get(fo_size, MUIA_String_Contents, &s);
  sz = s ? parse_pt_s(s) / 10 : 0;
  if (sz >= 2 && sz <= 3276 && sz != w->size) { n->size = (u16)sz; q.mask |= CM_SIZE; }
  n->color = fx_colvals[cyc(fo_color)];
  if (n->color != w->color && !(w->color == 0 && cyc(fo_color) == 1)) q.mask |= CM_COLOR;
  v = cyc(fo_ul);
  if (v) { n->flags |= CF_UNDERLINE; n->ul = (u8)(v - 1); } else n->flags &= (u8)~CF_UNDERLINE;
  if (chk(fo_strike)) n->flags |= CF_STRIKE; else n->flags &= (u8)~CF_STRIKE;
  if (chk(fo_super)) n->flags = (u8)((n->flags | CF_SUPER) & ~CF_SUB); else n->flags &= (u8)~CF_SUPER;
  if (chk(fo_sub)) n->flags = (u8)((n->flags | CF_SUB) & ~CF_SUPER); else n->flags &= (u8)~CF_SUB;
  if (chk(fo_caps)) n->flags |= CF_CAPS; else n->flags &= (u8)~CF_CAPS;
  n->fx = (u8)((n->fx & ~(FX_DSTRIKE | FX_SMALLCAPS)) | (chk(fo_dstrike) ? FX_DSTRIKE : 0) | (chk(fo_small) ? FX_SMALLCAPS : 0));
  wp9_font_read(w, n, &q.mask);                        /* (WP9) */
  q.mask |= (n->flags ^ w->flags) & 0x7F;
  if ((n->flags & CF_UNDERLINE) && n->ul != w->ul) q.mask |= CM_UL | CM_UNDERLINE;
  if ((n->fx ^ w->fx) & FX_DSTRIKE) q.mask |= CM_DSTRIKE;
  if ((n->fx ^ w->fx) & FX_SMALLCAPS) q.mask |= CM_SMALLCAPS;
  v = cyc(fo_spc);
  n->spacing = (i16)(v == 0 ? 0 : v == 1 ? str_pt(fo_spcby) : -str_pt(fo_spcby));
  if (v && !str_pt(fo_spcby)) n->spacing = (i16)(v == 1 ? 20 : -20);
  if (n->spacing != w->spacing) q.mask |= CM_SPACING;
  n->scale = scales[cyc(fo_scale)] == 100 ? 0 : scales[cyc(fo_scale)];
  if (n->scale != w->scale) q.mask |= CM_SCALE;
  v = cyc(fo_pos);
  n->position = (i16)(v == 0 ? 0 : (v == 1 ? 1 : -1) * (str_pt(fo_posby) ? str_pt(fo_posby) / 10 : 6));
  if (n->position != w->position) q.mask |= CM_POSITION;
  if (q.mask) DoMethod(view, MUIM_Quill_Command, CMD_CHARFMT, (ULONG)&q);
  set(win, MUIA_Window_Activate, TRUE);
}

/* ---- the Paragraph window */
static Object *parawin, *pa_align, *pa_left, *pa_right, *pa_special, *pa_by, *pa_before, *pa_after, *pa_line, *pa_at, *pa_ctx, *pa_keep, *pa_lines, *pa_break, *pa_widow;
static ParaFmt pa_was;
/* the Paragraph window, filled from the caret's paragraph (kept in pa_was). Line spacing is in
   240ths of a line for the automatic kinds, in twips for At least and Exactly */
static void open_para(void)
{
  static const char *const aligns[] = { "Left", "Centred", "Right", "Justified", "Distributed", NULL }, *const specials[] = { "(none)", "First line", "Hanging", NULL },
               *const lines[] = { "Single", "1.5 lines", "Double", "At least", "Exactly", "Multiple", NULL };
  static const char *t_al[6], *t_sp[5], *t_ln[8];
  const ParaFmt *pf = caret_pf();
  char b[24];
  int ls;
  if (!pf) return;
  if (!parawin) {
    Object *c = VGroup,
      Child, QTitle(tr("General")), Child, HGroup, QFRAME, Child, Label1(tr("Alignment")), Child, pa_align = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(aligns, t_al), MUIA_CycleChain, 1, End, End,
      Child, QTitle(tr("Indentation")), Child, ColGroup(4), QFRAME,
        Child, Label2(tr("Left")), Child, pa_left = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Special")), Child, pa_special = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(specials, t_sp), MUIA_CycleChain, 1, End,
        Child, Label2(tr("Right")), Child, pa_right = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        Child, Label2(tr("By")), Child, pa_by = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
      End,
      Child, QTitle(tr("Spacing")), Child, VGroup, QFRAME,
        Child, ColGroup(4),
          Child, Label2(tr("Before")), Child, pa_before = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Line spacing")), Child, pa_line = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(lines, t_ln), MUIA_CycleChain, 1, End,
          Child, Label2(tr("After")), Child, pa_after = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_CycleChain, 1, End,
          Child, Label2(tr("At")), Child, pa_at = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, pa_ctx = CheckMark(FALSE), Child, LLabel1(tr("Don't add space between paragraphs of the same style")), Child, HSpace(0), End,
      End,
      Child, QTitle(tr("Line and page breaks")), Child, ColGroup(4), QFRAME,
        Child, pa_widow = CheckMark(TRUE), Child, LLabel1(tr("Widow/Orphan control")),
        Child, pa_keep = CheckMark(FALSE), Child, LLabel1(tr("Keep with next")),
        Child, pa_lines = CheckMark(FALSE), Child, LLabel1(tr("Keep lines together")),
        Child, pa_break = CheckMark(FALSE), Child, LLabel1(tr("Page break before")),
      End,
    End;
    if (!(parawin = dialog("Paragraph", MAKE_ID('P','A','R','A'), c, "_OK", ID_PARAGO))) return;
  }
  pa_was = *pf;
  set(pa_align, MUIA_Cycle_Active, pf->align <= AL_DISTRIBUTE ? pf->align : 0);
  tab_text(pf->left, b); set_str(pa_left, b);
  tab_text(pf->right, b); set_str(pa_right, b);
  set(pa_special, MUIA_Cycle_Active, pf->first > 0 ? 1 : pf->first < 0 ? 2 : 0);
  tab_text(pf->first < 0 ? -pf->first : pf->first, b); set_str(pa_by, pf->first ? b : "");
  pt_text(pf->before, b); set_str(pa_before, b);
  pt_text(pf->after, b); set_str(pa_after, b);
  ls = pf->linerule == LS_ATLEAST ? 3 : pf->linerule == LS_EXACT ? 4 : pf->line == 240 ? 0 : pf->line == 360 ? 1 : pf->line == 480 ? 2 : 5;
  set(pa_line, MUIA_Cycle_Active, ls);
  if (ls >= 3 && ls <= 4) pt_text(pf->line, b); else if (ls == 5) sprintf(b, "%ld.%02ld", (long)(pf->line / 240), (long)(pf->line % 240 * 100 / 240)); else b[0] = 0;
  set_str(pa_at, b);
  set(pa_ctx, MUIA_Selected, pf->contextual != 0);
  set(pa_widow, MUIA_Selected, !pf->nowidow);
  set(pa_keep, MUIA_Selected, pf->keepnext != 0);
  set(pa_lines, MUIA_Selected, pf->keeplines != 0);
  set(pa_break, MUIA_Selected, pf->pagebreak != 0);
  set(parawin, MUIA_Window_Open, TRUE);
}
/* the Paragraph window's OK: only the groups changed (PM_ bits) are applied to the selection */
static void para_done(void)
{
  struct QParaFmt q;
  ParaFmt *n = &q.pf;
  const ParaFmt *w = &pa_was;
  ULONG v;
  char *s = NULL;
  set(parawin, MUIA_Window_Open, FALSE);
  memset(&q, 0, sizeof q);
  *n = *w;
  n->align = (u8)cyc(pa_align);
  n->left = str_len(pa_left); n->right = str_len(pa_right);
  v = cyc(pa_special);
  n->first = v == 0 ? 0 : v == 1 ? str_len(pa_by) : -str_len(pa_by);
  if (v && !n->first) n->first = v == 1 ? 720 : -720;
  n->before = str_pt(pa_before); n->after = str_pt(pa_after);
  v = cyc(pa_line);
  get(pa_at, MUIA_String_Contents, &s);
  switch (v) {
  case 0: n->line = 240; n->linerule = LS_AUTO; break;
  case 1: n->line = 360; n->linerule = LS_AUTO; break;
  case 2: n->line = 480; n->linerule = LS_AUTO; break;
  case 3: case 4: n->line = str_pt(pa_at); if (n->line < 20) n->line = 240; n->linerule = (u8)(v == 3 ? LS_ATLEAST : LS_EXACT); break;
  default: n->line = s ? parse_pt_s(s) * 12 : 240; if (n->line < 60) n->line = 240; n->linerule = LS_AUTO;     /* lines: 1.15 is 276 240ths */
  }
  n->contextual = (u8)chk(pa_ctx);
  n->nowidow = (u8)!chk(pa_widow);
  n->keepnext = (u8)chk(pa_keep);
  n->keeplines = (u8)chk(pa_lines);
  n->pagebreak = (u8)chk(pa_break);
  if (n->align != w->align) q.mask |= PM_ALIGN;
  if (n->left != w->left || n->right != w->right || n->first != w->first) q.mask |= PM_INDENT;
  if (n->before != w->before || n->after != w->after) q.mask |= PM_SPACING;
  if (n->line != w->line || n->linerule != w->linerule) q.mask |= PM_LINE;
  if (n->contextual != w->contextual) q.mask |= PM_CONTEXT;
  if (n->nowidow != w->nowidow || n->keeplines != w->keeplines) q.mask |= PM_PAGINATION;
  if (n->keepnext != w->keepnext) q.mask |= PM_KEEP;
  if (n->pagebreak != w->pagebreak) q.mask |= PM_BREAK;
  if (q.mask) DoMethod(view, MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
  set(win, MUIA_Window_Activate, TRUE);
}

/* ---- borders and shading */
static const char *const shade_names[] = { "No Colour", "Light Grey", "Grey", "Light Blue", "Light Green", "Light Yellow", "Light Red", NULL };
static const u32 shade_vals[] = { 0, 0xE7E6E6, 0xBFBFBF, 0xDEEAF6, 0xE2EFD9, 0xFFF2CC, 0xF8CBAD };
static const char *const pen_names[] = { "1/4 pt", "1/2 pt", "3/4 pt", "1 pt", "1 1/2 pt", "2 1/4 pt", "3 pt", NULL };
static Object *bordwin, *bo_edge[PE_N], *bo_style, *bo_width, *bo_color, *bo_shade;
/* the caret's paragraph with border edges (PE_ bits) set in a pen, the others left off */
static void set_borders(ParaFmt *pf, unsigned edges, u8 style, u8 width, u32 color)
{
  int e;
  for (e = 0; e < PE_N; e++) {
    if (edges & (1u << e)) { pf->bstyle[e] = style; pf->bwidth[e] = width; pf->bcolor[e] = color; pf->bspace[e] = (u8)(e == PE_LEFT || e == PE_RIGHT ? 4 : 1); }
    else { pf->bstyle[e] = PB_NONE; pf->bwidth[e] = 0; pf->bspace[e] = 0; pf->bcolor[e] = 0; }
  }
}
/* the ribbon's Borders list: none (0), bottom, top, left, right (1-4, added to the edges the
   paragraph has), outside (5) or all, between too (6), and inside (7: a line between paragraphs,
   added too) */
static void border_preset(int v)
{
  static const unsigned edges[8] = { 0, 1u << PE_BOTTOM, 1u << PE_TOP, 1u << PE_LEFT, 1u << PE_RIGHT,
                                     (1u << PE_TOP) | (1u << PE_BOTTOM) | (1u << PE_LEFT) | (1u << PE_RIGHT), 31, 1u << PE_BETWEEN };
  struct QParaFmt q;
  const ParaFmt *pf = caret_pf();
  if (!pf || v < 0 || v > 7) return;
  memset(&q, 0, sizeof q);
  q.pf = *pf;
  if (v == 0) set_borders(&q.pf, 0, 0, 0, 0);
  else {
    /* added to what the paragraph has (in its pen if it has one) */
    unsigned have = 0;
    int e;
    u8 st = PB_SINGLE, w = 4;
    u32 col = COLOR_AUTO;
    for (e = 0; e < PE_N; e++) if (pf->bstyle[e]) { have |= 1u << e; st = pf->bstyle[e]; w = pf->bwidth[e]; col = pf->bcolor[e]; }
    set_borders(&q.pf, v == 5 || v == 6 ? edges[v] : have | edges[v], st, w, col);
  }
  q.mask = PM_BORDER;
  DoMethod(view, MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
}
/* the Borders and Shading window, from the caret's paragraph (the first edge with a border gives
   the style, width and colour shown) */
static void open_borders(void)
{
  static const char *const styles[] = { "Single line", "Double line", "Thick line", "Dotted line", "Dashed line", NULL };
  static const char *t_st[6], *t_pen[8], *t_col[14], *t_sh[8];
  const ParaFmt *pf = caret_pf();
  int e, any = -1, i;
  if (!pf) return;
  if (!bordwin) {
    Object *c = VGroup,
      Child, QTitle(tr("Borders")), Child, ColGroup(10), QFRAME,
        Child, bo_edge[PE_TOP] = CheckMark(FALSE), Child, LLabel1(tr("Top")),
        Child, bo_edge[PE_BOTTOM] = CheckMark(FALSE), Child, LLabel1(tr("Bottom")),
        Child, bo_edge[PE_LEFT] = CheckMark(FALSE), Child, LLabel1(tr("Left")),
        Child, bo_edge[PE_RIGHT] = CheckMark(FALSE), Child, LLabel1(tr("Right")),
        Child, bo_edge[PE_BETWEEN] = CheckMark(FALSE), Child, LLabel1(tr("Between")),
      End,
      Child, QTitle(tr("Line")), Child, ColGroup(2), QFRAME,
        Child, Label1(tr("Style")), Child, bo_style = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(styles, t_st), MUIA_CycleChain, 1, End,
        Child, Label1(tr("Width")), Child, bo_width = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(pen_names, t_pen), MUIA_CycleChain, 1, End,
        Child, Label1(tr("Colour")), Child, bo_color = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(fx_colnames, t_col), MUIA_CycleChain, 1, End,
      End,
      Child, QTitle(tr("Shading")), Child, HGroup, QFRAME, Child, Label1(tr("Fill")), Child, bo_shade = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(shade_names, t_sh), MUIA_CycleChain, 1, End, End,
    End;
    if (!(bordwin = dialog("Borders and Shading", MAKE_ID('P','B','D','R'), c, "_OK", ID_BORDERSGO))) return;
  }
  for (e = 0; e < PE_N; e++) { set(bo_edge[e], MUIA_Selected, pf->bstyle[e] != 0); if (pf->bstyle[e] && any < 0) any = e; }
  set(bo_style, MUIA_Cycle_Active, any >= 0 && pf->bstyle[any] >= PB_SINGLE ? pf->bstyle[any] - PB_SINGLE : 0);
  for (i = 0; i < 7 && (any < 0 || ribbon_penwidths[i] != pf->bwidth[any]); i++) ;
  set(bo_width, MUIA_Cycle_Active, i < 7 ? i : 1);
  set(bo_color, MUIA_Cycle_Active, any >= 0 ? col_index(pf->bcolor[any]) : 0);
  for (i = 0; i < 7 && shade_vals[i] != (pf->shade ? pf->shade & 0xFFFFFF : 0); i++) ;
  set(bo_shade, MUIA_Cycle_Active, i < 7 ? i : 0);
  set(bordwin, MUIA_Window_Open, TRUE);
}
/* Borders and Shading's OK: the edges ticked in the one pen, and the fill */
static void borders_done(void)
{
  struct QParaFmt q;
  const ParaFmt *pf = caret_pf();
  unsigned edges = 0;
  int e;
  ULONG sh;
  set(bordwin, MUIA_Window_Open, FALSE);
  if (!pf) return;
  memset(&q, 0, sizeof q);
  q.pf = *pf;
  for (e = 0; e < PE_N; e++) if (chk(bo_edge[e])) edges |= 1u << e;
  set_borders(&q.pf, edges, (u8)(cyc(bo_style) + PB_SINGLE), ribbon_penwidths[cyc(bo_width)], fx_colvals[cyc(bo_color)]);
  sh = cyc(bo_shade);
  q.pf.shade = sh ? HL_ON | shade_vals[sh] : 0;
  q.mask = PM_BORDER | PM_SHADE;
  DoMethod(view, MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
  set(win, MUIA_Window_Activate, TRUE);
}
/* the ribbon's paragraph shading: v is 0xRRGGBB, or 0xFF000000 for none */
static void shade_para(LONG v)
{
  struct QParaFmt q;
  const ParaFmt *pf = caret_pf();
  if (!pf) return;
  memset(&q, 0, sizeof q);
  q.pf = *pf;
  q.pf.shade = (ULONG)v == 0xFF000000UL ? 0 : HL_ON | ((u32)v & 0xFFFFFF);
  q.mask = PM_SHADE;
  DoMethod(view, MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
}

/* ---- a style: new or changed */
static Object *stywin, *sy_name, *sy_type, *sy_based, *sy_next, *sy_font, *sy_size, *sy_bold, *sy_italic, *sy_ul, *sy_color, *sy_align,
              *sy_before, *sy_after, *sy_line, *sy_left, *sy_first, *sy_keep, *sy_quick;
static int sy_k;                          /* the style being changed, -1 a new one */
static Style sy_s;                        /* what it starts from */
static const char *sy_bnames[MAX_STYLES + 2], *sy_nnames[MAX_STYLES + 2];
static int sy_bidx[MAX_STYLES + 2], sy_nidx[MAX_STYLES + 2];
static char sy_names[MAX_STYLES][48];
/* paragraph and character styles are listed; table and numbering styles are not */
static int style_visible(int i) { return doc->styles[i].type == ST_PARA || doc->styles[i].type == ST_CHAR; }
/* a style's name as shown (Word's built-in names are lower case in its files: "heading 1"), Latin-1 */
static void style_shown(int i, char *out, size_t cap)
{
  utf8_to_latin1(doc->styles[i].name, strlen(doc->styles[i].name), out, cap);
  if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}
/* Modify Style (k: the style) or New Style (k -1, of type newtype: ST_PARA from the caret's
   paragraph, ST_CHAR from what the caret's text has beyond its paragraph style). Unlike the other
   dialogs it is made afresh each time, as its Based on and Following lists change */
static void open_style(int k, int newtype)
{
  static const char *const types[] = { "Paragraph", "Character", NULL }, *const aligns[] = { "Left", "Centred", "Right", "Justified", "Distributed", NULL },
               *const lines[] = { "Single", "1.15", "1.5 lines", "Double", NULL };
  static const char *t_al[6], *t_ln[6], *t_col[14];
  static const i32 linevals[4] = { 240, 276, 360, 480 };
  Pos *cp = caret_pos();
  Style *s = &sy_s;
  int i, nb = 0, nn = 0, type;
  char b[64];
  if (stywin) { set(stywin, MUIA_Window_Open, FALSE); DoMethod(app, OM_REMMEMBER, (ULONG)stywin); MUI_DisposeObject(stywin); stywin = NULL; }
  sy_k = k;
  if (k >= 0) *s = doc->styles[k];
  else {
    int base = cp ? doc->p[cp->para]->pf.style : 0;
    doc_style_from(doc, cp ? *cp : doc_start(doc), base, s);
    if (newtype == ST_CHAR) {
      /* a character style from the caret's text: what differs from the paragraph style */
      const CharFmt *f = caret_cf();
      memset(s, 0, sizeof *s);
      s->type = ST_CHAR; s->based = NO_STYLE; s->quick = 1;
      s->cf = *f;
      s->cmask = 0;
      s->ocf = *f;
      {
        const CharFmt *pc = &doc->styles[cp ? doc->p[cp->para]->pf.style : 0].cf;
        if (f->font != pc->font) s->cmask |= CM_FONT;
        if (f->size != pc->size) s->cmask |= CM_SIZE;
        if (f->color != pc->color) s->cmask |= CM_COLOR;
        s->cmask |= (f->flags ^ pc->flags) & 0x7F;
      }
    }
    strcpy(s->name, newtype == ST_CHAR ? "Character Style" : "Style");
  }
  type = s->type;
  /* the styles it can be based on, and follow */
  sy_bnames[nb] = tr("(no style)"); sy_bidx[nb++] = NO_STYLE;
  for (i = 0; i < doc->nstyles; i++) {
    if (!style_visible(i)) continue;
    style_shown(i, sy_names[i], sizeof sy_names[i]);
    if (doc->styles[i].type == type && i != k) { sy_bnames[nb] = sy_names[i]; sy_bidx[nb++] = i; }
    if (doc->styles[i].type == ST_PARA) { sy_nnames[nn] = sy_names[i]; sy_nidx[nn++] = i; }
  }
  sy_bnames[nb] = NULL; sy_nnames[nn] = NULL;
  if (!nn) { sy_nnames[0] = "Normal"; sy_nnames[1] = NULL; sy_nidx[0] = 0; }
  {
    Object *c = VGroup,
      Child, QTitle(tr("Properties")), Child, ColGroup(4), QFRAME,
        Child, Label2(tr("Name")), Child, sy_name = StringObject, StringFrame, MUIA_String_MaxLen, 46, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Style type")), Child, sy_type = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)(type == ST_CHAR ? tr(types[1]) : tr(types[0])), End,
        Child, Label1(tr("Based on")), Child, sy_based = CycleObject, MUIA_Cycle_Entries, (ULONG)sy_bnames, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Following paragraph")), Child, sy_next = CycleObject, MUIA_Cycle_Entries, (ULONG)sy_nnames, MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
      End,
      Child, QTitle(tr("Formatting")), Child, ColGroup(4), QFRAME,
        Child, Label1(tr("Font")), Child, family_field(&sy_font),
        Child, Label2(tr("Size")), Child, sy_size = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
        Child, Label1(tr("Colour")), Child, sy_color = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(fx_colnames, t_col), MUIA_CycleChain, 1, End,
        Child, HGroup, Child, sy_bold = CheckMark(FALSE), Child, LLabel1(tr("Bold")), Child, sy_italic = CheckMark(FALSE), Child, LLabel1(tr("Italic")),
          Child, sy_ul = CheckMark(FALSE), Child, LLabel1(tr("Underline")), End, Child, HSpace(0),
        Child, Label1(tr("Alignment")), Child, sy_align = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(aligns, t_al), MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Line spacing")), Child, sy_line = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(lines, t_ln), MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Before")), Child, sy_before = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
        Child, Label2(tr("After")), Child, sy_after = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Left indent")), Child, sy_left = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
        Child, Label2(tr("First line")), Child, sy_first = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_Disabled, type != ST_PARA, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, sy_keep = CheckMark(FALSE), Child, LLabel1(tr("Keep with next")), Child, sy_quick = CheckMark(TRUE), Child, LLabel1(tr("Show in the styles gallery")), Child, HSpace(0), End,
    End;
    if (!(stywin = dialog(k >= 0 ? "Modify Style" : "New Style", MAKE_ID('S','T','Y','L'), c, "_OK", ID_STYLEGO))) return;
  }
  if (k >= 0) style_shown(k, b, sizeof b); else utf8_to_latin1(s->name, strlen(s->name), b, sizeof b);
  set_str(sy_name, b);
  for (i = 0; i < nb && sy_bidx[i] != s->based; i++) ;
  set(sy_based, MUIA_Cycle_Active, i < nb ? i : 0);
  for (i = 0; i < nn && sy_nidx[i] != (k >= 0 ? s->next : (cp ? doc->styles[doc->p[cp->para]->pf.style].next : 0)); i++) ;
  set(sy_next, MUIA_Cycle_Active, i < nn ? i : 0);
  family_set(sy_font, s->cf.font < doc->nfonts ? doc->fonts[s->cf.font] : "Times New Roman");
  sprintf(b, s->cf.size & 1 ? "%u.5" : "%u", (unsigned)(s->cf.size / 2)); set_str(sy_size, b);
  set(sy_color, MUIA_Cycle_Active, col_index(s->cf.color));
  set(sy_bold, MUIA_Selected, (s->cf.flags & CF_BOLD) != 0); set(sy_italic, MUIA_Selected, (s->cf.flags & CF_ITALIC) != 0);
  set(sy_ul, MUIA_Selected, (s->cf.flags & CF_UNDERLINE) != 0);
  set(sy_align, MUIA_Cycle_Active, s->pf.align <= AL_DISTRIBUTE ? s->pf.align : 0);
  for (i = 0; i < 4 && linevals[i] != s->pf.line; i++) ;
  set(sy_line, MUIA_Cycle_Active, i < 4 ? i : 0);
  pt_text(s->pf.before, b); set_str(sy_before, b);
  pt_text(s->pf.after, b); set_str(sy_after, b);
  tab_text(s->pf.left, b); set_str(sy_left, b);
  tab_text(s->pf.first < 0 ? -s->pf.first : s->pf.first, b);
  if (s->pf.first < 0) { memmove(b + 1, b, strlen(b) + 1); b[0] = '-'; }
  set_str(sy_first, b);
  set(sy_keep, MUIA_Selected, s->pf.keepnext != 0);
  set(sy_quick, MUIA_Selected, s->quick != 0);
  set(stywin, MUIA_Window_Open, TRUE);
  set(stywin, MUIA_Window_ActiveObject, (ULONG)sy_name);
}
/* a field set by the style itself only where it differs from its base's */
#define OWN_C(bit, cond, assign) do { if (cond) { s->cmask |= (bit); assign; } else s->cmask &= ~(unsigned)(bit); } while (0)
/* the style window's OK: an empty or taken name beeps and changes nothing. What the style sets
   itself (cmask, pmask) is only what differs from the style it is based on, as Word stores it */
static void style_done(void)
{
  static const i32 linevals[4] = { 240, 276, 360, 480 };
  struct QStyle q;
  Style *s = &q.s;
  const ParaFmt *bp = &doc->defpf;
  const CharFmt *bc = &doc->defcf;
  char *t = NULL;
  ULONG v;
  i32 sz, x;
  set(stywin, MUIA_Window_Open, FALSE);
  q.k = sy_k; *s = sy_s;
  get(sy_name, MUIA_String_Contents, &t);
  latin1_utf8(t ? t : "", s->name, sizeof s->name);
  if (!s->name[0]) { DisplayBeep(NULL); return; }
  if (sy_k >= 0 && str_ieq(s->name, doc->styles[sy_k].name)) strcpy(s->name, doc->styles[sy_k].name);   /* (only shown with a capital) */
  if (sy_k < 0) s->id[0] = 0;
  else if (doc_style_by_name(doc, s->name) >= 0 && doc_style_by_name(doc, s->name) != sy_k) { DisplayBeep(NULL); return; }
  s->based = (u8)sy_bidx[cyc(sy_based)];
  if (s->based != NO_STYLE && s->based < doc->nstyles) { bp = &doc->styles[s->based].pf; bc = &doc->styles[s->based].cf; }
  if (s->type == ST_PARA) s->next = (u8)sy_nidx[cyc(sy_next)];
  s->quick = (u8)chk(sy_quick);
  /* character formatting */
  {
    char fam[100];
    u8 font = (u8)doc_font(doc, family_get(sy_font, fam, sizeof fam)[0] ? fam : "Times New Roman");
    OWN_C(CM_FONT, font != bc->font, s->ocf.font = font);
  }
  get(sy_size, MUIA_String_Contents, &t);
  sz = t ? parse_pt_s(t) / 10 : 0;
  if (sz < 2) sz = s->cf.size;
  OWN_C(CM_SIZE, sz != bc->size, s->ocf.size = (u16)sz);
  OWN_C(CM_COLOR, fx_colvals[cyc(sy_color)] != bc->color, s->ocf.color = fx_colvals[cyc(sy_color)]);
  v = chk(sy_bold) ? CF_BOLD : 0;
  OWN_C(CM_BOLD, v != (bc->flags & CF_BOLD), s->ocf.flags = (u8)((s->ocf.flags & ~CF_BOLD) | v));
  v = chk(sy_italic) ? CF_ITALIC : 0;
  OWN_C(CM_ITALIC, v != (bc->flags & CF_ITALIC), s->ocf.flags = (u8)((s->ocf.flags & ~CF_ITALIC) | v));
  v = chk(sy_ul) ? CF_UNDERLINE : 0;
  OWN_C(CM_UNDERLINE, v != (bc->flags & CF_UNDERLINE), s->ocf.flags = (u8)((s->ocf.flags & ~CF_UNDERLINE) | v));
  /* paragraph formatting */
  if (s->type == ST_PARA) {
    ParaFmt n = s->pf;
    n.align = (u8)cyc(sy_align);
    n.line = linevals[cyc(sy_line)]; n.linerule = LS_AUTO;
    n.before = str_pt(sy_before); n.after = str_pt(sy_after);
    n.left = str_len(sy_left);
    get(sy_first, MUIA_String_Contents, &t);
    while (t && *t == ' ') t++;
    x = t ? (*t == '-' ? -parse_len(t + 1) : parse_len(t)) : 0;
    n.first = x;
    n.keepnext = (u8)chk(sy_keep);
#define OWN_P(bit) do { if (!pf_same_g(&n, bp, bit)) { s->pmask |= (bit); pf_apply(&s->opf, (bit), &n); } else s->pmask &= ~(unsigned)(bit); } while (0)
    OWN_P(PM_ALIGN); OWN_P(PM_LINE); OWN_P(PM_SPACING); OWN_P(PM_INDENT); OWN_P(PM_KEEP);
#undef OWN_P
  }
  if (DoMethod(view, MUIM_Quill_Command, CMD_SETSTYLE, (ULONG)&q) && q.k >= 0 && sy_k < 0 && s->type == ST_PARA)
    DoMethod(view, MUIM_Quill_Command, CMD_STYLE, (ULONG)q.k);     /* a new paragraph style: the caret's paragraph gets it */
  styles_fill();
  set(win, MUIA_Window_Activate, TRUE);
}
#undef OWN_C

/* ---- the Styles window: every style, to apply, change, make or delete */
static Object *styleswin, *st_list;
static int st_idx[MAX_STYLES];
static int st_n;
/* the Styles window's list from the document (st_idx: the style for each line), the caret's style
   selected. Nothing while the window hasn't been made */
static void styles_fill(void)
{
  int i;
  if (!styleswin) return;
  set(st_list, MUIA_List_Quiet, TRUE);
  DoMethod(st_list, MUIM_List_Clear);
  st_n = 0;
  for (i = 0; i < doc->nstyles; i++) {
    char b[80];
    if (!style_visible(i) || !strncmp(doc->styles[i].name, "toc ", 4)) continue;
    style_shown(i, b, 48);
    if (doc->styles[i].type == ST_CHAR) { strcat(b, "  "); strcat(b, tr("(character)")); }
    DoMethod(st_list, MUIM_List_InsertSingle, (ULONG)b, MUIV_List_Insert_Bottom);
    st_idx[st_n++] = i;
  }
  set(st_list, MUIA_List_Quiet, FALSE);
  {
    const ParaFmt *pf = caret_pf();
    for (i = 0; pf && i < st_n; i++) if (st_idx[i] == pf->style) { set(st_list, MUIA_List_Active, i); break; }
  }
}
/* the style selected in the Styles window, -1 if none */
static int styles_sel(void)
{
  LONG a = -1;
  if (!styleswin) return -1;
  get(st_list, MUIA_List_Active, &a);
  return a >= 0 && a < st_n ? st_idx[a] : -1;
}
/* Format > Styles: the window (its buttons all go back to command() as ReturnIDs) */
static void open_styles(void)
{
  if (!styleswin) {
    Object *apply, *nw, *nwc, *mod, *del, *upd, *close;
    styleswin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Styles"), MUIA_Window_ID, MAKE_ID('S','T','Y','S'),
      WindowContents, VGroup, QDLG,
        Child, st_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, apply = QButton(tr("_Apply")), Child, mod = QButton(tr("_Modify...")), Child, del = QButton(tr("_Delete")), End,
        Child, HGroup, Child, nw = QButton(tr("_New Style...")), Child, nwc = QButton(tr("New C_haracter Style...")), End,
        Child, HGroup, Child, upd = QButton(tr("_Update to Match Selection")), Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!styleswin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)styleswin);
    DoMethod(styleswin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)styleswin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)styleswin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(apply, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_STYLEAPPLY);
    DoMethod(st_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_STYLEAPPLY);
    DoMethod(mod, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_MODSTYLE);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_STYLEDELETE);
    DoMethod(nw, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_NEWSTYLE);
    DoMethod(nwc, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_STYLELIST);
    DoMethod(upd, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_STYLEUPDATE);
  }
  styles_fill();
  set(styleswin, MUIA_Window_Open, TRUE);
}
/* a style redefined from the formatting at the caret (its name, base and next style kept) */
static void style_update(int k)
{
  struct QStyle q;
  Pos *cp = caret_pos();
  if (k < 0 || !cp) { DisplayBeep(NULL); return; }
  q.k = k;
  if (doc->styles[k].type == ST_PARA) {
    doc_style_from(doc, *cp, doc->styles[k].based == NO_STYLE ? 0 : doc->styles[k].based, &q.s);
    if (doc->styles[k].based == NO_STYLE) q.s.based = NO_STYLE;
    q.s.pmask |= doc->styles[k].pmask & (PM_LIST | PM_TABS | PM_BORDER | PM_SHADE);    /* what the caret's paragraph can't show differently stays */
  } else {
    const CharFmt *f = caret_cf();
    q.s = doc->styles[k];
    q.s.ocf = *f;
    q.s.cmask = doc->styles[k].ecmask;
  }
  strcpy(q.s.name, doc->styles[k].name); strcpy(q.s.id, doc->styles[k].id);
  q.s.type = doc->styles[k].type; q.s.next = doc->styles[k].next; q.s.quick = doc->styles[k].quick;
  if (doc->styles[k].type == ST_PARA && doc->styles[k].based == NO_STYLE) q.s.based = NO_STYLE;
  DoMethod(view, MUIM_Quill_Command, CMD_SETSTYLE, (ULONG)&q);
}

/* ---- numbering value, and a list defined level by level */
static Object *numwin, *nu_val;
/* Set Numbering Value, for a numbered paragraph at the caret */
static void open_numval(void)
{
  if (!caret_pf() || !caret_pf()->list) { DisplayBeep(NULL); return; }
  if (!numwin) {
    Object *c = HGroup, Child, Label2(tr("Set value to")), Child, nu_val = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789",
      MUIA_String_Contents, (ULONG)"1", MUIA_CycleChain, 1, End, End;
    if (!(numwin = dialog("Set Numbering Value", MAKE_ID('N','U','M','V'), c, "_OK", ID_NUMVALGO))) return;
    DoMethod(nu_val, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_NUMVALGO);
  }
  set(numwin, MUIA_Window_Open, TRUE);
  set(numwin, MUIA_Window_ActiveObject, (ULONG)nu_val);
}
/* (the list at the caret starts again at the value, 1 at least) */
static void numval_done(void)
{
  char *s = NULL;
  long v = 0;
  set(numwin, MUIA_Window_Open, FALSE);
  get(nu_val, MUIA_String_Contents, &s);
  if (s) v = atol(s);
  if (v < 1) v = 1;
  DoMethod(view, MUIM_Quill_Command, CMD_LISTRESTART, (ULONG)v);
  set(win, MUIA_Window_Activate, TRUE);
}
static Object *ldwin, *ld_level, *ld_fmt, *ld_text, *ld_start, *ld_left, *ld_hang;
static ListDef ld_def;
static int ld_cur;
/* Define New List edits ld_def, a copy, one level at a time: ld_show puts level k in the fields,
   ld_keep the fields back in the level shown (before another level is shown, and on OK) */
static void ld_show(int k)
{
  ListLevel *v = &ld_def.lvl[k];
  char b[40];
  ld_cur = k;
  set(ld_fmt, MUIA_Cycle_Active, v->fmt <= NF_NONE ? v->fmt : NF_DECIMAL);
  utf8_to_latin1(v->text, strlen(v->text), b, sizeof b); set_str(ld_text, b);
  sprintf(b, "%u", (unsigned)v->start); set_str(ld_start, b);
  tab_text(v->left, b); set_str(ld_left, b);
  tab_text(v->hanging, b); set_str(ld_hang, b);
  set(ld_text, MUIA_Disabled, v->fmt == NF_BULLET);
}
static void ld_keep(void)
{
  ListLevel *v = &ld_def.lvl[ld_cur];
  char *s = NULL;
  u8 fmt = (u8)cyc(ld_fmt);
  if (fmt == NF_BULLET && v->fmt != NF_BULLET) strcpy(v->text, "\xE2\x80\xA2");
  else if (fmt != NF_BULLET) {
    get(ld_text, MUIA_String_Contents, &s);
    if (s && *s) latin1_utf8(s, v->text, sizeof v->text);
    else sprintf(v->text, "%%%d.", ld_cur + 1);
  }
  v->fmt = fmt;
  get(ld_start, MUIA_String_Contents, &s);
  v->start = (u16)(s && atol(s) > 0 ? atol(s) : 1);
  v->left = str_len(ld_left); v->hanging = str_len(ld_hang);
}
/* Define New List, starting from the caret's list or 1. a. i. numbering; it gets a list id of its own */
static void open_listdef(void)
{
  static const char *const levels[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", NULL },
               *const fmts[] = { "Bullet", "1, 2, 3", "a, b, c", "A, B, C", "i, ii, iii", "I, II, III", "(none)", NULL };
  static const char *t_fm[9];
  const ParaFmt *pf = caret_pf();
  int k, maxid = 0;
  if (!ldwin) {
    Object *c = VGroup,
      Child, HGroup, Child, Label1(tr("Level")), Child, ld_level = CycleObject, MUIA_Cycle_Entries, (ULONG)levels, MUIA_CycleChain, 1, End, Child, HSpace(0), End,
      Child, QTitle(tr("Number format")), Child, ColGroup(2), QFRAME,
        Child, Label1(tr("Number style")), Child, ld_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(fmts, t_fm), MUIA_CycleChain, 1, End,
        Child, Label2(tr("Number format (%1 for this level)")), Child, ld_text = StringObject, StringFrame, MUIA_String_MaxLen, 20, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Start at")), Child, ld_start = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
      End,
      Child, QTitle(tr("Position")), Child, ColGroup(2), QFRAME,
        Child, Label2(tr("Text indent")), Child, ld_left = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Hanging")), Child, ld_hang = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
      End,
    End;
    if (!(ldwin = dialog("Define New List", MAKE_ID('L','D','E','F'), c, "_OK", ID_LISTDEFGO))) return;
    DoMethod(ld_level, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LISTDEFLVL);
  }
  /* from the caret's list, or numbers */
  if (pf && pf->list && pf->list <= doc->nlists) ld_def = doc->lists[pf->list - 1];
  else {
    static const u8 abc[3] = { NF_DECIMAL, NF_LOWER_LETTER, NF_LOWER_ROMAN };
    memset(&ld_def, 0, sizeof ld_def);
    for (k = 0; k < 9; k++) { ld_def.lvl[k].fmt = abc[k % 3]; sprintf(ld_def.lvl[k].text, "%%%d.", k + 1); ld_def.lvl[k].start = 1; ld_def.lvl[k].left = 720 * (k + 1); ld_def.lvl[k].hanging = 360; }
  }
  for (k = 0; k < doc->nlists; k++) if (doc->lists[k].id > maxid) maxid = doc->lists[k].id;
  ld_def.id = (u16)(maxid + 1);
  set(ld_level, MUIA_Cycle_Active, pf && pf->list ? pf->level % 9 : 0);
  ld_show(pf && pf->list ? pf->level % 9 : 0);
  set(ldwin, MUIA_Window_Open, TRUE);
}
/* (the list defined goes to the selected paragraphs) */
static void listdef_done(void)
{
  ld_keep();
  set(ldwin, MUIA_Window_Open, FALSE);
  DoMethod(view, MUIM_Quill_Command, CMD_LISTDEF, (ULONG)&ld_def);
  set(win, MUIA_Window_Activate, TRUE);
}
/* the ribbon's Multilevel List menu: five ready-made lists (0-4), headings numbered (5) or not (6),
   restart (7), continue (8), Set Numbering Value (9), Define New List (10) */
static void multilist_choice(LONG v)
{
  static const int presets[5] = { LP_BULLETS, LP_NUMBERS, LP_PARENS, LP_OUTLINE, LP_LEGAL };
  if (v >= 0 && v < 5) DoMethod(view, MUIM_Quill_Command, CMD_LISTPRESET, (ULONG)presets[v]);
  else if (v == 5) DoMethod(view, MUIM_Quill_Command, CMD_HEADNUM, LP_HEADINGS);
  else if (v == 6) DoMethod(view, MUIM_Quill_Command, CMD_HEADNUM, (ULONG)-1);
  else if (v == 7) DoMethod(view, MUIM_Quill_Command, CMD_LISTRESTART, 0);
  else if (v == 8) DoMethod(view, MUIM_Quill_Command, CMD_LISTCONTINUE, 0);
  else if (v == 9) open_numval();
  else if (v == 10) open_listdef();
  else if (v == 11 || v == 12) DoMethod(view, MUIM_Quill_Command, v == 11 ? CMD_INDENT : CMD_OUTDENT, 0);   /* (a list's level, as Tab and Shift-Tab) */
}
/* whether a and b agree on one PM_ group (those a style window sets); other groups count as the same */
static int pf_same_g(const ParaFmt *a, const ParaFmt *b, unsigned g)
{
  switch (g) {
  case PM_ALIGN: return a->align == b->align;
  case PM_INDENT: return a->left == b->left && a->right == b->right && a->first == b->first;
  case PM_SPACING: return a->before == b->before && a->after == b->after;
  case PM_LINE: return a->line == b->line && a->linerule == b->linerule;
  case PM_KEEP: return a->keepnext == b->keepnext;
  }
  return 1;
}
static Object *sortwin, *sortcol, *sorttype, *sortorder, *sorthead;
static char *sortcols[MAX_COLS + 1];
/* Table > Sort, for the caret's table. The columns are named by their first row's text, so the
   window is made again each time */
static void open_sort(void)
{
  static const char *types[3], *orders[3];
  const ParaFmt *pf = caret_pf();
  Table *t = pf ? doc_table(doc, pf) : NULL;
  int k;
  if (!t) { DisplayBeep(NULL); return; }
  /* the columns by number, or by their heading's text */
  for (k = 0; k < MAX_COLS && sortcols[k]; k++) { free(sortcols[k]); sortcols[k] = NULL; }
  for (k = 0; k < t->ncols && k < MAX_COLS; k++) if ((sortcols[k] = (char *)malloc(48))) sprintf(sortcols[k], "%s %d", tr("Column"), k + 1);
  {
    Pos *c = caret_pos();
    u32 i, a = tbl_first(doc, c->para), b = tbl_end(doc, c->para);
    for (i = a; i < b; i++) {
      const Cell *x = cell_in(&doc->p[i]->pf, pf->cell.table);
      if (x && x->row == 0 && x->col < t->ncols && sortcols[x->col] && doc->p[i]->len && i == cell_first(doc, i))
        utf8_to_latin1(doc->p[i]->t, doc->p[i]->len > 40 ? 40 : doc->p[i]->len, sortcols[x->col], 48);
    }
  }
  if (sortwin) { DoMethod(app, OM_REMMEMBER, (ULONG)sortwin); MUI_DisposeObject(sortwin); sortwin = NULL; }   /* the columns change */
  types[0] = tr("Text"); types[1] = tr("Number"); types[2] = NULL;
  orders[0] = tr("Ascending (A to Z)"); orders[1] = tr("Descending (Z to A)"); orders[2] = NULL;
  {
    Object *c = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Sort by")), Child, sortcol = CycleObject, MUIA_Cycle_Entries, (ULONG)sortcols, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Type")), Child, sorttype = CycleObject, MUIA_Cycle_Entries, (ULONG)types, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Order")), Child, sortorder = CycleObject, MUIA_Cycle_Entries, (ULONG)orders, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, sorthead = CheckMark(TRUE), Child, LLabel1(tr("My table has a header row")), Child, HSpace(0), End,
    End;
    if (!(sortwin = dialog("Sort", MAKE_ID('S','O','R','T'), c, "_Sort", ID_TSORTGO))) return;
  }
  set(sortcol, MUIA_Cycle_Active, pf->cell.col < t->ncols ? pf->cell.col : 0);
  set(sortwin, MUIA_Window_Open, TRUE);
}
/* (the table sorted by the column and order chosen) */
static void sort_done(void)
{
  struct QSort q;
  ULONG v = 0;
  get(sortcol, MUIA_Cycle_Active, &v); q.col = (int)v;
  get(sorttype, MUIA_Cycle_Active, &v); q.numeric = v == 1;
  get(sortorder, MUIA_Cycle_Active, &v); q.desc = v == 1;
  get(sorthead, MUIA_Selected, &v); q.header = v != 0;
  set(sortwin, MUIA_Window_Open, FALSE);
  DoMethod(view, MUIM_Quill_Command, CMD_TSORT, (ULONG)&q);
  set(win, MUIA_Window_Activate, TRUE);
}
/* Convert: a table to text, or text to a table, separated by tabs, commas, semicolons (or paragraphs) */
static Object *convwin, *convsep;
static int conv_totable;
static const char conv_chars[] = { '\t', ',', ';', 0 };
/* Convert Text to Table (totable) or Table to Text; the window is made again when the direction
   changes, as their choices differ */
static void open_convert(int totable)
{
  static const char *seps[5];
  const ParaFmt *pf = caret_pf();
  if (!pf || (!totable && !in_table(pf)) || (totable && in_table(pf))) { DisplayBeep(NULL); return; }
  if (convwin && conv_totable != totable) { DoMethod(app, OM_REMMEMBER, (ULONG)convwin); MUI_DisposeObject(convwin); convwin = NULL; }
  conv_totable = totable;
  if (!convwin) {
    Object *c;
    seps[0] = tr("Tabs"); seps[1] = tr("Commas"); seps[2] = tr("Semicolons"); seps[3] = totable ? tr("Paragraphs (one cell each)") : NULL; seps[4] = NULL;
    c = ColGroup(2), Child, Label1(tr(totable ? "Separate text at" : "Separate text with")), Child, convsep = CycleObject, MUIA_Cycle_Entries, (ULONG)seps, MUIA_CycleChain, 1, End, End;
    if (!(convwin = dialog(totable ? "Convert Text to Table" : "Convert Table to Text", totable ? MAKE_ID('C','N','V','T') : MAKE_ID('C','N','V','X'), c, "_Convert",
                           totable ? ID_TOTABLEGO : ID_TOTEXTGO))) return;
  }
  set(convwin, MUIA_Window_Open, TRUE);
}
/* (the separator goes to the view as a character: tab, comma, semicolon, or 0 for paragraphs) */
static void convert_done(void)
{
  ULONG v = 0;
  get(convsep, MUIA_Cycle_Active, &v);
  set(convwin, MUIA_Window_Open, FALSE);
  DoMethod(view, MUIM_Quill_Command, conv_totable ? CMD_TOTABLE : CMD_TOTEXT, (ULONG)(u8)conv_chars[v < 4 ? v : 0]);
  set(win, MUIA_Window_Activate, TRUE);
}
/* Formula: worked out for the caret's cell and put in as its result */
static Object *fxwin, *fxstr, *fxfunc;
/* Table > Formula, for the caret's cell, with Word's guess at the formula to start with */
static void open_formula(void)
{
  static const char *funcs[8];
  const ParaFmt *pf = caret_pf();
  if (!pf || !in_table(pf)) { DisplayBeep(NULL); return; }
  if (!fxwin) {
    Object *c;
    funcs[0] = tr("Paste function"); funcs[1] = "SUM()"; funcs[2] = "AVERAGE()"; funcs[3] = "COUNT()"; funcs[4] = "MAX()"; funcs[5] = "MIN()"; funcs[6] = "PRODUCT()"; funcs[7] = NULL;
    c = VGroup,
      Child, ColGroup(2),
        Child, Label2(tr("Formula")), Child, fxstr = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
        Child, Label1(""), Child, fxfunc = CycleObject, MUIA_Cycle_Entries, (ULONG)funcs, MUIA_CycleChain, 1, End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33cABOVE, BELOW, LEFT and RIGHT, or cells by name (A1, B2:C4)"), End,
    End;
    if (!(fxwin = dialog("Formula", MAKE_ID('F','R','M','L'), c, "_OK", ID_FORMULAGO))) return;
    DoMethod(fxstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FORMULAGO);
    DoMethod(fxfunc, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FORMULA);
  }
  /* Word's guess: the numbers above the cell, or those to its left */
  {
    Pos *c = caret_pos();
    const char *guess = "=SUM(ABOVE)";
    if (c && pf->cell.row == 0) guess = "=SUM(LEFT)";
    set(fxstr, MUIA_String_Contents, (ULONG)guess);
  }
  set(fxfunc, MUIA_Cycle_Active, 0);
  set(fxwin, MUIA_Window_Open, TRUE);
  set(fxwin, MUIA_Window_ActiveObject, (ULONG)fxstr);
}
/* a function picked from the cycle: added to the formula */
static void formula_func(void)
{
  ULONG v = 0;
  char *s = NULL, buf[160];
  static const char *const names[] = { "", "SUM(ABOVE)", "AVERAGE(ABOVE)", "COUNT(ABOVE)", "MAX(ABOVE)", "MIN(ABOVE)", "PRODUCT(ABOVE)" };
  get(fxfunc, MUIA_Cycle_Active, &v);
  if (!v || v > 6) return;
  get(fxstr, MUIA_String_Contents, &s);
  snprintf(buf, sizeof buf, "%s%s", s && *s && strcmp(s, "=") ? s : "=", names[v]);
  set(fxstr, MUIA_String_Contents, (ULONG)buf);
  set(fxfunc, MUIA_Cycle_Active, 0);
}
/* Formula's OK: the result goes in the cell; a formula that can't be worked out is explained and
   the window stays open to fix it */
static void formula_done(void)
{
  struct QFormula q;
  char *s = NULL, utf[200];
  get(fxstr, MUIA_String_Contents, &s);
  latin1_utf8(s ? s : "", utf, sizeof utf);
  memset(&q, 0, sizeof q);
  q.expr = utf;
  DoMethod(view, MUIM_Quill_Command, CMD_FORMULA, (ULONG)&q);
  if (!q.ok) {
    char msg[160];
    snprintf(msg, sizeof msg, "%s\n\n%s", tr("The formula can't be worked out:"), q.result[0] ? (const char *)q.result : "!Syntax Error");
    ask("OK", "%s", msg);
    return;
  }
  set(fxwin, MUIA_Window_Open, FALSE);
  set(win, MUIA_Window_Activate, TRUE);
}
/* the borders' pen (Table Design > Borders) */
static struct QBorders pen = { TB_ALL, PB_SINGLE, 4, COLOR_AUTO };

/* ------------------------------------------------------------------ */
/* Window and Screen: the public screen Quill opens on, and where its window opens */
static Object *wpwin, *wp_screen, *wp_list, *wp_mode, *wp_box[4], *wp_now;
/* the window's place now */
static int win_now(LONG *b)
{
  ULONG v[4] = { 0, 0, 0, 0 };
  if (!xget_open(win)) return 0;
  get(win, MUIA_Window_LeftEdge, &v[0]); get(win, MUIA_Window_TopEdge, &v[1]);
  get(win, MUIA_Window_Width, &v[2]); get(win, MUIA_Window_Height, &v[3]);
  if (v[2] < 100 || v[3] < 60) return 0;
  b[0] = (LONG)v[0]; b[1] = (LONG)v[1]; b[2] = (LONG)v[2]; b[3] = (LONG)v[3];
  return 1;
}
/* a window box (left, top, width, height in pixels) into the four fields */
static void wp_show_box(const LONG *b)
{
  int i;
  char t[16];
  for (i = 0; i < 4; i++) { sprintf(t, "%ld", (long)b[i]); set_str(wp_box[i], t); }
}
/* the Window and Screen options: the public screen (with the ones open now to pick from) and where
   the window opens */
static void open_winprefs(void)
{
  static const char *modes[4];
  struct List *l;
  struct Node *n;
  char names[16][MAXPUBSCREENNAME + 1];
  int i, k = 0;
  LONG b[4];
  if (!wpwin) {
    static const char *const boxl[4] = { "Left", "Top", "Width", "Height" };
    Object *c, *boxes;
    modes[0] = tr("Nearly fills the screen"); modes[1] = tr("Where it was last closed"); modes[2] = tr("Where set below"); modes[3] = NULL;
    boxes = ColGroup(4), End;
    for (i = 0; i < 4 && boxes; i++) {
      Object *lab = Label2(tr(boxl[i])), *st = wp_box[i] = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"-0123456789", MUIA_CycleChain, 1, End;
      DoMethod(boxes, OM_ADDMEMBER, (ULONG)lab); DoMethod(boxes, OM_ADDMEMBER, (ULONG)st);
    }
    c = VGroup,
      Child, ColGroup(2),
        Child, Label2(tr("Public screen")), Child, wp_screen = StringObject, StringFrame, MUIA_String_MaxLen, MAXPUBSCREENNAME + 1, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Open screens")), Child, wp_list = ListviewObject, MUIA_CycleChain, 1, MUIA_VertWeight, 30,
          MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33iEmpty: the default public screen (usually Workbench)."), End,
        Child, Label1(tr("Window")), Child, wp_mode = CycleObject, MUIA_Cycle_Entries, (ULONG)modes, MUIA_CycleChain, 1, End,
        Child, HSpace(0), Child, boxes,
        Child, HSpace(0), Child, HGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33iUsed when Quill next starts."), End,
          Child, HSpace(0), Child, wp_now = QButton(tr("Use the window's place now")), End,
      End,
    End;
    if (!c || !(wpwin = dialog("Window and Screen", MAKE_ID('W','I','N','P'), c, "_OK", ID_WINPREFSGO))) return;
    DoMethod(wp_now, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WINCUR);
    DoMethod(wp_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WINSCRPICK);
  }
  /* the public screens open now */
  if ((l = LockPubScreenList())) {
    for (n = l->lh_Head; n->ln_Succ && k < 16; n = n->ln_Succ)
      if (n->ln_Name) { strncpy(names[k], n->ln_Name, MAXPUBSCREENNAME); names[k++][MAXPUBSCREENNAME] = 0; }
    UnlockPubScreenList();
  }
  DoMethod(wp_list, MUIM_List_Clear);
  for (i = 0; i < k; i++) DoMethod(wp_list, MUIM_List_InsertSingle, (ULONG)names[i], MUIV_List_Insert_Bottom);
  set_str(wp_screen, pub_screen);
  set(wp_mode, MUIA_Cycle_Active, pref_win);
  if (win_box[2]) wp_show_box(win_box);
  else if (win_now(b)) wp_show_box(b);
  set(wpwin, MUIA_Window_Open, TRUE);
}
/* (a screen picked from the list goes in the name field) */
static void winprefs_pick(void)
{
  char *e = NULL;
  DoMethod(wp_list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&e);
  if (e) set_str(wp_screen, e);
}
/* ("Use the window's place now": its box into the fields, and Where set below chosen) */
static void winprefs_now(void)
{
  LONG b[4];
  if (win_now(b)) { wp_show_box(b); set(wp_mode, MUIA_Cycle_Active, WIN_FIXED); }
}
/* every window of Quill's on the public screen named ("" the default one); the open main window
   is closed and opened again there */
static void move_to_screen(void)
{
  struct List *wl = NULL;
  APTR st;
  Object *o;
  char name[MAXPUBSCREENNAME + 1];
  int was = xget_open(win);
  if (pub_screen[0]) strcpy(name, pub_screen);
  else GetDefaultPubScreen((STRPTR)name);
  if (was) set(win, MUIA_Window_Open, FALSE);
  get(app, MUIA_Application_WindowList, &wl);
  for (st = wl ? wl->lh_Head : NULL; st && (o = NextObject(&st)); )
    if (!xget_open(o)) set(o, MUIA_Window_PublicScreen, (ULONG)name);
  if (was) {
    set(win, MUIA_Window_Open, TRUE);
    if (!xget_open(win)) {                 /* (it didn't fit there) */
      GetDefaultPubScreen((STRPTR)name);
      set(win, MUIA_Window_PublicScreen, (ULONG)name);
      set(win, MUIA_Window_Open, TRUE);
    }
  }
}
/* Window and Screen's OK, after checking the box and that the screen is open; saved to the prefs
   at once, and Quill's windows move if the screen changed */
static void winprefs_done(void)
{
  char *s = NULL, old[64];
  ULONG m = 0;
  int i;
  LONG b[4];
  get(wp_screen, MUIA_String_Contents, &s);
  get(wp_mode, MUIA_Cycle_Active, &m);
  for (i = 0; i < 4; i++) { char *t = NULL; get(wp_box[i], MUIA_String_Contents, &t); b[i] = t ? atol(t) : 0; }
  if ((int)m == WIN_FIXED && (b[2] < 100 || b[3] < 60)) { ask("OK", "%s", tr("The window needs a width of 100 and a height of 60 or more.")); return; }
  if (s && s[0]) {
    struct Screen *sc = LockPubScreen((STRPTR)s);
    if (!sc) { ask("OK", "%s", tr("There is no public screen of that name open now.")); return; }
    UnlockPubScreen(NULL, sc);
  }
  set(wpwin, MUIA_Window_Open, FALSE);
  pref_win = (int)m;
  if (b[2] >= 100 && b[3] >= 60) for (i = 0; i < 4; i++) win_box[i] = b[i];
  strcpy(old, pub_screen);
  strncpy(pub_screen, s ? s : "", sizeof pub_screen - 1); pub_screen[sizeof pub_screen - 1] = 0;
  save_prefs();
  if (strcmp(old, pub_screen)) move_to_screen();
  set(win, MUIA_Window_Activate, TRUE);
}

/* the Page Setup window: WP22a's (amiga/wp_pages.c: margins with the gutter, multiple pages, paper,
   the section's layout, Apply to) */
static Object *pgwin;
static void open_pagesetup(void) { wp22a_command(ID_PG_SETUP, WPV_NONE); }

/* commands for the whole document: a story being edited goes back first (kept) */
static int story_ends(ULONG id)
{
  static const ULONG ids[] = { ID_QUIT, ID_NEW, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_SAVETXT, ID_PDF, ID_PRINT, ID_FILETAB, ID_BACKSTAGE, ID_TEMPLATE,
    ID_RECENT, ID_OPTIONS, ID_COLUMNSDD, ID_BREAKSDD, ID_LINENUMDD, ID_PAGEALIGNDD, ID_HYPHENDD, ID_WATERMARKDD, ID_BORDERDD, ID_TOC, ID_TOCUPDATE,
    ID_NEXTNOTE, ID_COMMENT, ID_DELCOMMENT, ID_PREVCOMMENT, ID_NEXTCOMMENT, ID_SHOWCOMMENTS, ID_ACCEPTDD, ID_REJECTDD, ID_PREVCHANGE,
    ID_NEXTCHANGE, ID_COLBREAK, ID_SECTBREAK, ID_PAGEBREAK, ID_VREAD, ID_VWEB, ID_VDRAFT, ID_OUTLINE, ID_NAVPICK, ID_NAVSEARCH,
    ID_TOCDD, ID_ADDTEXTDD, ID_REMOVETOC, ID_BLANKPAGE, ID_GOTO, ID_DROPCAPDLG, ID_LINENUMDLG, MUIV_Application_ReturnID_Quit };
  size_t k;
  for (k = 0; k < sizeof ids / sizeof ids[0]; k++) if (ids[k] == id) return 1;
  return 0;
}
/* a menu item, button or dialog by its ID (ids.h), for the current window's document. Most just
   become a MUIM_Quill_Command for the view (view.c does the editing, undo and redraw); the rest
   open dialogs or act on files and windows. main()'s loop hands the ribbon's lists and the File
   page to ribbon_choice and backstage_command instead; ARexx comes here too */
static int wp_focus_kept;                /* a work package's command opened a window that keeps the keyboard */
static int wp_started;                   /* wp_init done (so wp_cleanup at the end) */
static void command(ULONG id)
{
  static const ULONG simple[][2] = {
    { ID_UNDO, CMD_UNDO }, { ID_REDO, CMD_REDO }, { ID_CUT, CMD_CUT }, { ID_COPY, CMD_COPY }, { ID_PASTE, CMD_PASTE },
    { ID_SELALL, CMD_SELALL }, { ID_BOLD, CMD_BOLD }, { ID_ITALIC, CMD_ITALIC }, { ID_UNDERLINE, CMD_UNDERLINE },
    { ID_STRIKE, CMD_STRIKE }, { ID_CBORDER, CMD_CBORDER }, { ID_SUPER, CMD_SUPER }, { ID_SUB, CMD_SUB }, { ID_CAPS, CMD_CAPS }, { ID_BULLETS, CMD_BULLETS },
    { ID_NUMBERS, CMD_NUMBERS }, { ID_INDENT, CMD_INDENT }, { ID_OUTDENT, CMD_OUTDENT }, { ID_PAGEBREAK, CMD_PAGEBREAK },
    { ID_LINEBREAK, CMD_LINEBREAK }, { ID_ROW_ABOVE, CMD_ROW_ABOVE }, { ID_ROW_BELOW, CMD_ROW_BELOW }, { ID_COL_LEFT, CMD_COL_LEFT },
    { ID_COL_RIGHT, CMD_COL_RIGHT }, { ID_DEL_ROW, CMD_DEL_ROW }, { ID_DEL_COL, CMD_DEL_COL }, { ID_DEL_TABLE, CMD_DEL_TABLE }, { ID_HEADERROW, CMD_HEADERROW },
    { ID_TGRID, CMD_TGRID }, { ID_SPLITTABLE, CMD_SPLITTABLE }, { ID_DISTROWS, CMD_DISTROWS }, { ID_DISTCOLS, CMD_DISTCOLS },
  };
  size_t i;
  wp12_note(id, WPV_NONE);               /* WP12: Repeat (F4) */
  /* the work packages' blocks (ids.h): to the package's own wpN_command (wphooks.h) */
  if (id >= ID_WP_FIRST && id <= ID_WP_LAST) { wp_focus_kept = wp_run(id, WPV_NONE) == WPR_KEEPFOCUS; return; }
  for (i = 0; i < sizeof simple / sizeof simple[0]; i++) if (simple[i][0] == id) { DoMethod(view, MUIM_Quill_Command, simple[i][1], 0); return; }
  switch (id) {
  case ID_LEFT: case ID_CENTER: case ID_RIGHT: case ID_JUSTIFY: DoMethod(view, MUIM_Quill_Command, CMD_ALIGN, id - ID_LEFT); break;
  case ID_SINGLE: DoMethod(view, MUIM_Quill_Command, CMD_SPACING, 240); break;
  case ID_ONEHALF: DoMethod(view, MUIM_Quill_Command, CMD_SPACING, 360); break;
  case ID_DOUBLE: DoMethod(view, MUIM_Quill_Command, CMD_SPACING, 480); break;
  case ID_ZOOMFIT: set(view, MUIA_Quill_Zoom, 0); break;
  case ID_ZOOM75: set(view, MUIA_Quill_Zoom, 75); break;
  case ID_ZOOM100: set(view, MUIA_Quill_Zoom, 100); break;
  case ID_ZOOM125: set(view, MUIA_Quill_Zoom, 125); break;
  case ID_ZOOM150: set(view, MUIA_Quill_Zoom, 150); break;
  case ID_ZOOM200: set(view, MUIA_Quill_Zoom, 200); break;
  case ID_PAPER0: case ID_PAPER1: case ID_PAPER2: case ID_PAPER3: page_setup((int)(id - ID_PAPER0), -1, -1); break;
  case ID_PORTRAIT: case ID_LANDSCAPE: page_setup(-1, id == ID_LANDSCAPE, -1); break;
  case ID_MARGIN0: case ID_MARGIN1: case ID_MARGIN2: case ID_MARGIN3: page_setup(-1, -1, (int)(id - ID_MARGIN0)); break;
  case ID_PDF: export_pdf(NULL); break;
  case ID_PRINT: print_doc(0); break;
  case ID_TABLE: open_table(); break;
  case ID_TABLEGO: {
    ULONG r = 3, c = 3;
    get(rowsl, MUIA_Numeric_Value, &r); get(colsl, MUIA_Numeric_Value, &c);
    set(tablewin, MUIA_Window_Open, FALSE);
    DoMethod(view, MUIM_Quill_Command, CMD_TABLE, (ULONG)((r << 8) | c));
    set(win, MUIA_Window_Activate, TRUE);
    break; }
  case ID_DATE: insert_date(); break;
  case ID_HEADFOOT:                      /* (Amiga-H) the caret's page's header, or back to the text */
    if (doc->edit.kind) story_close(1); else edit_headfoot(0, -1);
    break;
  case ID_HFHEADER: case ID_HFFOOTER: edit_headfoot(id == ID_HFFOOTER, -1); break;
  case ID_HFPAGE: case ID_HFPAGES: DoMethod(view, MUIM_Quill_Command, CMD_TEXT, (ULONG)(id == ID_HFPAGE ? FIELD_PAGE : FIELD_PAGES)); break;
  case ID_HFPREV: case ID_HFNEXT: headfoot_section(id == ID_HFNEXT ? 1 : -1); break;
  case ID_HFFIRST: case ID_HFEVEN: case ID_HFOWN: headfoot_option(id == ID_HFFIRST ? 0 : id == ID_HFEVEN ? 1 : 2); break;
  case ID_STORYCLOSE: case ID_STORYDISCARD: story_close(id == ID_STORYCLOSE); break;
  case ID_STORYREQ: {
    /* a double-click on a header, footer, footnote or comment's balloon, or Esc in one */
    struct QStoryReq *r = NULL;
    get(view, MUIA_Quill_EditStory, &r);
    if (!r) break;
    if (doc->edit.kind) { story_close(1); break; }
    if (r->kind == SK_HEADER || r->kind == SK_FOOTER) edit_headfoot(r->kind == SK_FOOTER, (long)r->page);
    else story_open(r->kind, r->index, 0, r->page);
    break; }
  case ID_PAGESETUPDLG: open_pagesetup(); break;
  case ID_PICTURE: insert_picture(); break;
  case ID_NOTE: note_end = 0; open_note(); break;
  case ID_ENDNOTE: note_end = 1; open_note(); break;
  case ID_PICSIZE: open_picsize(); break;
  case ID_PICSIZEGO: {
    ULONG pc = 50;
    get(psslider, MUIA_Numeric_Value, &pc);
    set(pswin, MUIA_Window_Open, FALSE);
    DoMethod(view, MUIM_Quill_Command, CMD_PICSIZE, pc);
    set(win, MUIA_Window_Activate, TRUE);
    break; }
  case ID_OUTLINE: { ULONG on = 0; get(navgrp, MUIA_ShowMe, &on); set(navgrp, MUIA_ShowMe, !on); ui_bit(RB_NAVPANE, !on); break; }
  case ID_RULER: { ULONG on = 0; get(ruler, MUIA_ShowMe, &on); set(ruler, MUIA_ShowMe, !on); set(vruler, MUIA_ShowMe, !on); ui_bit(RB_RULER, !on); break; }
  case ID_ZOOMSET: { LONG z = 100; get(statusbar, MUIA_Status_Zoom, &z); set(view, MUIA_Quill_Zoom, z); break; }
  case ID_FOCUS: focus = !focus; chrome(); break;
  case ID_VREAD: case ID_VPRINT: case ID_VWEB: case ID_VDRAFT:
    set(view, MUIA_Quill_ViewMode, id == ID_VREAD ? VM_READ : id == ID_VPRINT ? VM_PRINT : id == ID_VWEB ? VM_WEB : VM_DRAFT);
    chrome();
    break;
  case ID_VMODECHG: chrome(); break;
  case ID_MULTIPAGE: set(view, MUIA_Quill_ViewMode, VM_PRINT); chrome(); set(view, MUIA_Quill_Zoom, (ULONG)-2); break;
  case ID_GRID: pref_grid = !pref_grid; set(view, MUIA_Quill_Grid, pref_grid); save_prefs(); DoMethod(view, MUIM_Quill_Command, CMD_REFRESH, 0); break;
  case ID_DARKPAGE: pref_darkpage = !pref_darkpage; set(view, MUIA_Quill_DarkPage, pref_darkpage); save_prefs(); DoMethod(view, MUIM_Quill_Command, CMD_REFRESH, 0); break;
  case ID_REPLACE: wp_focus_kept = wp_run(ID_FX_REPLACE, WPV_NONE) == WPR_KEEPFOCUS; break;   /* (WP12's Find and Replace window) */
  case ID_TABS: open_tabs(); break;
  case ID_TOC: case ID_TOCUPDATE: DoMethod(view, MUIM_Quill_Command, CMD_TOC, 0); break;
  case ID_NEXTNOTE: DoMethod(view, MUIM_Quill_Command, CMD_NEXTNOTE, 0); break;
  case ID_WORDCOUNT: word_count(); break;
  case ID_SPELL: open_spelling(); break;
  case ID_SPCHANGE: case ID_SPCHANGEALL: case ID_SPIGNORE: case ID_SPIGNOREALL: case ID_SPADD: spelling_command(id); break;
  case ID_SPPICK: spell_pick(); break;
  case ID_LINK: open_link(); break;
  case ID_LINKGO: link_done(); break;
  case ID_UNLINK: if (lkwin) set(lkwin, MUIA_Window_Open, FALSE); DoMethod(view, MUIM_Quill_Command, CMD_UNLINK, 0); break;
  case ID_LINKOPEN: link_open(); break;
  case ID_LINKCOPY: link_copy(); break;
  case ID_CTXPICK: {
    /* a ribbon list's entry chosen from the right-button menu (the view keeps which) */
    ULONG p = 0;
    get(view, MUIA_Quill_CtxPick, &p);
    if ((p >> 8) >= ID_WP_FIRST && (p >> 8) <= ID_WP_LAST) wp_focus_kept = wp_run(p >> 8, (LONG)(p & 0xFF)) == WPR_KEEPFOCUS;   /* (WP_CTX_LIST) */
    else obj_choice(p >> 8, (LONG)(p & 0xFF));
    break; }
  case ID_COMMENT: open_comment(); break;
  case ID_TRACK: {
    /* tracking on: changes from now on are the user's, dated now */
    static char au[200], when[24];
    int on = !(uibits & RBIT(RB_TRACK));
    latin1_utf8(username[0] ? username : "Quill", au, sizeof au);
    iso_now(when);
    tb.author = au; tb.revdate = when;
    ui_bit(RB_TRACK, on);
    break; }
  case ID_PREVCHANGE: case ID_NEXTCHANGE: DoMethod(view, MUIM_Quill_Command, CMD_NEXTCHANGE, id == ID_NEXTCHANGE ? 1 : (ULONG)-1); break;
  case ID_COMMENTGO: comment_done(); break;
  case ID_DELCOMMENT: DoMethod(view, MUIM_Quill_Command, CMD_DELCOMMENT, 0); break;
  case ID_PREVCOMMENT: case ID_NEXTCOMMENT: DoMethod(view, MUIM_Quill_Command, CMD_NEXTCOMMENT, id == ID_NEXTCOMMENT ? 1 : (ULONG)-1); break;
  case ID_SHOWCOMMENTS: ui_bit(RB_SHOWCOMMENTS, !(uibits & RBIT(RB_SHOWCOMMENTS))); DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0); break;
  case ID_MERGE: DoMethod(view, MUIM_Quill_Command, CMD_MERGE, 0); break;
  case ID_SPLIT: open_split(); break;
  case ID_SPLITGO: split_done(); break;
  case ID_TPROPS: open_tprops(); break;
  case ID_TEXTBOX: DoMethod(view, MUIM_Quill_Command, CMD_SHAPE, (ULONG)-1); break;
  case ID_OBJLAYOUT: open_object(OP_POSITION); break;
  case ID_OBJCROP: case ID_OBJSIZE: open_object(OP_SIZE); break;
  case ID_ALTTEXT: if (!wp24_alt_pane()) open_object(OP_ALT); break;     /* (WP24: the Alt Text pane) */
  case ID_GROUP: DoMethod(view, MUIM_Quill_Command, CMD_GROUP, 0); break;
  case ID_UNGROUP: DoMethod(view, MUIM_Quill_Command, CMD_UNGROUP, 0); break;
  case ID_EDITTEXT: { int k = sel_obj(); if (k >= 0 && doc->images[k].kind != OBJ_PICTURE) story_open(SK_TEXTBOX, k, 0, 0); break; }
  case ID_OBJEDIT: { int k = sel_obj(); if (k >= 0) { if (doc->images[k].kind == OBJ_PICTURE) open_object(OP_SIZE); else story_open(SK_TEXTBOX, k, 0, 0); } break; }
  case ID_OBJGO: object_done(); break;
  case ID_FONTDLG: open_font(); break;
  case ID_FONTGO: font_done(); break;
  case ID_PARADLG: open_para(); break;
  case ID_PARAGO: para_done(); break;
  case ID_BORDERSDLG: open_borders(); break;
  case ID_BORDERSGO: borders_done(); break;
  case ID_STYLESPANE: open_styles(); break;
  case ID_NEWSTYLE: open_style(-1, ST_PARA); break;
  case ID_STYLELIST: open_style(-1, ST_CHAR); break;
  case ID_MODSTYLE: {
    int k = styleswin && styles_sel() >= 0 && (LONG)xget_open(styleswin) ? styles_sel() : (caret_pf() ? caret_pf()->style : 0);
    open_style(k, 0);
    break; }
  case ID_STYLEGO: style_done(); break;
  case ID_STYLEAPPLY: { int k = styles_sel(); if (k >= 0) DoMethod(view, MUIM_Quill_Command, CMD_STYLE, (ULONG)k); break; }
  case ID_STYLEDELETE: {
    int k = styles_sel();
    char nm[64];
    if (k <= 0) { DisplayBeep(NULL); break; }
    style_shown(k, nm, sizeof nm);
    if (ask("_Delete|_Cancel", "Delete the style \"%s\"? Its text takes the style it was based on.", nm) == 1) {
      DoMethod(view, MUIM_Quill_Command, CMD_DELSTYLE, (ULONG)k);
      styles_fill();
    }
    break; }
  case ID_STYLEUPDATE: style_update(styles_sel() >= 0 ? styles_sel() : (caret_pf() ? caret_pf()->style : -1)); styles_fill(); break;
  case ID_NUMVALGO: numval_done(); break;
  case ID_GOTO: wp_focus_kept = wp_run(ID_FX_GOTO, WPV_NONE) == WPR_KEEPFOCUS; break;          /* (WP12's Go To) */
  case ID_FORWARD: case ID_BACKWARD: DoMethod(view, MUIM_Quill_Command, CMD_RESTACK, (ULONG)(LONG)(id == ID_FORWARD ? 1 : -1)); selpane_fill(); break;
  case ID_SELPANE: open_selpane(); break;
  case ID_BOOKMARK: open_bookmarks(); break;
  case ID_BMADD: case ID_BMDEL: case ID_BMGO: case ID_BMPICK: bm_command(id); break;
  case ID_XREF: open_xref(); break;
  case ID_XRINS: xr_insert(); break;
  case ID_XRTYPE: xr_fill(); break;
  case ID_SELPANEPICK: selpane_pick(); break;
  case ID_SELPANEREF: selpane_fill(); break;
  case ID_GOTOGO: goto_done(); break;
  case ID_DROPCAPDLG: open_dropcap(); break;
  case ID_DROPCAPGO: dropcap_done(); break;
  case ID_LINENUMDLG: open_linenum(); break;
  case ID_MORECOLGO: morecol_done(); break;
  case ID_SETDEFAULT:
    if (ask(tr("Yes|No"), tr("Make this document's style set, fonts, paragraph spacing and colours\nthe default for new documents?")) == 1)
      { memcpy(pref_look, doc->look, sizeof pref_look); save_prefs(); }
    break;
  case ID_BLANKPAGE: DoMethod(view, MUIM_Quill_Command, CMD_BLANKPAGE, 0); break;
  case ID_REMOVETOC: DoMethod(view, MUIM_Quill_Command, CMD_TOCREMOVE, 0); break;
  case ID_WINPREFS: open_winprefs(); break;
  case ID_FAMPICK: fam_pick(); break;
  case ID_WINPREFSGO: winprefs_done(); break;
  case ID_WINCUR: winprefs_now(); break;
  case ID_WINSCRPICK: winprefs_pick(); break;
  case ID_NUMVAL: open_numval(); break;
  case ID_LISTDEF: open_listdef(); break;
  case ID_LISTDEFGO: listdef_done(); break;
  case ID_LISTDEFLVL: ld_keep(); ld_show((int)cyc(ld_level)); break;
  case ID_PASTEKEEP: case ID_PASTEMERGE: case ID_PASTETEXT: DoMethod(view, MUIM_Quill_Command, CMD_PASTEAS, (ULONG)(id - ID_PASTEKEEP)); break;
  case ID_MSHAPE + 0: case ID_MSHAPE + 1: case ID_MSHAPE + 2: case ID_MSHAPE + 3: case ID_MSHAPE + 4: case ID_MSHAPE + 5: case ID_MSHAPE + 6:
  case ID_MSHAPE + 7: case ID_MSHAPE + 8: DoMethod(view, MUIM_Quill_Command, CMD_SHAPE, (ULONG)(id - ID_MSHAPE)); break;
  case ID_TPROPSGO: tprops_done(); break;
  case ID_TSORT: open_sort(); break;
  case ID_TSORTGO: sort_done(); break;
  case ID_TOTEXT: open_convert(0); break;
  case ID_TOTABLE: open_convert(1); break;
  case ID_TOTEXTGO: case ID_TOTABLEGO: convert_done(); break;
  case ID_FORMULA: if (fxwin && xget_open(fxwin)) formula_func(); else open_formula(); break;
  case ID_FORMULAGO: formula_done(); break;
  case ID_LK_FIRSTROW: DoMethod(view, MUIM_Quill_Command, CMD_TLOOK, LK_FIRSTROW); break;
  case ID_LK_LASTROW: DoMethod(view, MUIM_Quill_Command, CMD_TLOOK, LK_LASTROW); break;
  case ID_LK_BANDROWS: DoMethod(view, MUIM_Quill_Command, CMD_TLOOK, LK_NOHBAND); break;
  case ID_LK_FIRSTCOL: DoMethod(view, MUIM_Quill_Command, CMD_TLOOK, LK_FIRSTCOL); break;
  case ID_LK_LASTCOL: DoMethod(view, MUIM_Quill_Command, CMD_TLOOK, LK_LASTCOL); break;
  case ID_LK_BANDCOLS: DoMethod(view, MUIM_Quill_Command, CMD_TLOOK, LK_NOVBAND); break;
  case ID_VALIGN0: case ID_VALIGN1: case ID_VALIGN2: DoMethod(view, MUIM_Quill_Command, CMD_VALIGN, id - ID_VALIGN0); break;
  case ID_TABSET: case ID_TABCLEAR: case ID_TABCLEARALL: case ID_TABPICK: tabs_command(id); break;
  case ID_GROW: case ID_SHRINK: DoMethod(view, MUIM_Quill_Command, CMD_GROW, id == ID_GROW ? 1 : (ULONG)-1); break;
  case ID_CLEARFMT: DoMethod(view, MUIM_Quill_Command, CMD_CLEARFMT, 0); break;
  case ID_MARKS: DoMethod(view, MUIM_Quill_Command, CMD_MARKS, 0); break;
  case ID_BRUSH: DoMethod(view, MUIM_Quill_Command, CMD_BRUSH, 0); break;
  case ID_SORT: wp_focus_kept = wp_run(ID_P_SORT, WPV_NONE) == WPR_KEEPFOCUS; break;     /* (WP10: the Sort Text window) */
  case ID_ZOOM100B: set(view, MUIA_Quill_Zoom, 100); break;
  case ID_PAGEWIDTH: set(view, MUIA_Quill_Zoom, 0); break;
  case ID_ONEPAGE: set(view, MUIA_Quill_Zoom, (ULONG)-1); break;
  case ID_PAGENUM: page_number(6); break;
  case ID_PGFMTGO: pgfmt_done(); break;
  case ID_FIND: open_find(); break;
  case ID_FINDNEXT: case ID_FINDGO: case ID_FINDPREV: case ID_REPLACE1: case ID_REPLACEALL: find_next(id); break;
  case ID_NEW: win_new(blank_doc(), ""); break;            /* always a window of its own */
  case ID_CLOSE: break;                                   /* (the input loop closes the window) */
  case ID_NEXTWIN: win_next(); break;
  case ID_OPEN:
    { char path[512]; strcpy(path, docpath); if (file_request(0, "Open a document", path, sizeof path, 1)) open_path(path); }
    break;
  case ID_SAVE: save(); break;
  case ID_SAVEAS: save_as(0); break;
  case ID_SAVETXT: save_as(1); break;
  case ID_ABOUT:
    ask("OK", "\33cQuill 0.14\n\n%s", tr("A word processor for the Amiga.\nReads and writes Word documents (.docx).\n\n"
              "Text is measured with the Liberation and Carlito/Caladea fonts\n(SIL Open Font License), so documents break\n"
              "their lines as they do in Word."));
    break;
  }
}

/* our own fonts drawer joins FONTS: while Quill runs */
static int fonts_assigned;
/* PROGDIR:Fonts added to the FONTS: assign (AssignAdd), so the screen fonts Quill draws its
   interface in are found; remove_fonts takes it out again at the end. pr_WindowPtr -1 stops DOS
   asking for a volume while it looks */
static void add_fonts(void)
{
  BPTR l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ), sys;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR win = me->pr_WindowPtr;
  if (!l) return;
  /* not if it is FONTS: itself (the end would take the system's own drawer out of it) */
  me->pr_WindowPtr = (APTR)-1;
  if ((sys = Lock((STRPTR)"FONTS:", ACCESS_READ))) {
    LONG same = SameLock(l, sys);
    UnLock(sys);
    if (same == LOCK_SAME) { UnLock(l); me->pr_WindowPtr = win; return; }
  }
  me->pr_WindowPtr = win;
  if (AssignAdd((STRPTR)"FONTS", l)) fonts_assigned = 1;
  else UnLock(l);
}
static void remove_fonts(void)
{
  BPTR l;
  if (!fonts_assigned) return;
  if ((l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ))) { RemAssignList((STRPTR)"FONTS", l); UnLock(l); }
  fonts_assigned = 0;
}

/* ------------------------------------------------------------------ */
/* the ARexx port (QUILL.1): commands as the menus have them, a few to read the document */
enum {
  RX_NEW = 1, RX_OPEN, RX_SAVE, RX_SAVEAS, RX_EXPORTPDF, RX_PRINT, RX_TEXT, RX_NEWPARAGRAPH, RX_LINEBREAK, RX_PAGEBREAK,
  RX_BOLD, RX_ITALIC, RX_UNDERLINE, RX_FONT, RX_SIZE, RX_STYLE, RX_ALIGN, RX_COLOR, RX_SELECTALL, RX_CUT, RX_COPY, RX_PASTE,
  RX_UNDO, RX_REDO, RX_DELETE, RX_GOTOTOP, RX_GOTOEND, RX_FIND, RX_GETTEXT, RX_GETWORDS, RX_GETPAGES, RX_GETFILENAME,
  RX_INSERTTABLE, RX_ACTIVATE
};
static char rexxbuf[1024];
/* an ARexx command's RESULT string (copied to rexxbuf, which outlives the call) */
static void rexx_result(const char *s) { strncpy(rexxbuf, s, sizeof rexxbuf - 1); rexxbuf[sizeof rexxbuf - 1] = 0; set(app, MUIA_Application_RexxString, (ULONG)rexxbuf); }
/* MUI's strings (gadgets, ARexx) are Latin-1 and the document is UTF-8: s converted into u, cut
   short to fit cap */
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
/* called through rexx_entry (hook.s), which aligns the stack */
ULONG rexx_dispatch(struct Hook *h, Object *o, LONG *arg)
{
  static char u[2048];
  ULONG id = (ULONG)h->h_Data;
  Pos *c = NULL, *an = NULL;
  if (qlog) { fprintf(qlog, "rexx command %lu\n", (unsigned long)id); fflush(qlog); }
  rexxbuf[0] = 0;                       /* no result unless the command gives one (Zune keeps the last) */
  set(app, MUIA_Application_RexxString, (ULONG)rexxbuf);
  switch (id) {
  case RX_NEW: { Doc *d = blank_doc(); if (!d) return 20; docpath[0] = 0; show_doc(d); return 0; }
  case RX_OPEN: { char path[512]; strncpy(path, (const char *)arg[0], sizeof path - 1); path[sizeof path - 1] = 0; open_path(path); return strcmp(docpath, path) ? 10 : 0; }
  case RX_SAVE: return docpath[0] && save_to(docpath) ? 0 : 10;
  case RX_SAVEAS: if (!save_to((const char *)arg[0])) return 10; strncpy(docpath, (const char *)arg[0], sizeof docpath - 1); set_title(); return 0;
  case RX_EXPORTPDF: export_pdf((const char *)arg[0]); return 0;
  case RX_PRINT: return print_doc(1) ? 10 : 0;
  case RX_TEXT: latin1_utf8((const char *)arg[0], u, sizeof u); DoMethod(view, MUIM_Quill_Command, CMD_TEXT, (ULONG)u); return 0;
  case RX_NEWPARAGRAPH: DoMethod(view, MUIM_Quill_Command, CMD_TEXT, (ULONG)"\n"); return 0;
  case RX_LINEBREAK: command(ID_LINEBREAK); return 0;
  case RX_PAGEBREAK: command(ID_PAGEBREAK); return 0;
  case RX_BOLD: command(ID_BOLD); return 0;
  case RX_ITALIC: command(ID_ITALIC); return 0;
  case RX_UNDERLINE: command(ID_UNDERLINE); return 0;
  case RX_FONT: latin1_utf8((const char *)arg[0], u, sizeof u); DoMethod(view, MUIM_Quill_Command, CMD_FONT, (ULONG)u); return 0;
  case RX_SIZE: { LONG pt = *(LONG *)arg[0]; if (pt < 1 || pt > 1600) return 10; DoMethod(view, MUIM_Quill_Command, CMD_SIZE, (ULONG)(pt * 2)); return 0; }
  case RX_STYLE: {
    int k;
    latin1_utf8((const char *)arg[0], u, sizeof u);
    if ((k = doc_style_by_name(doc, u)) < 0 && (k = doc_style(doc, u)) < 0) return 10;
    DoMethod(view, MUIM_Quill_Command, CMD_STYLE, (ULONG)k);
    return 0; }
  case RX_ALIGN: {
    const char *m = (const char *)arg[0];
    int al = str_ieq(m, "LEFT") ? AL_LEFT : (str_ieq(m, "CENTER") || str_ieq(m, "CENTRE")) ? AL_CENTER : str_ieq(m, "RIGHT") ? AL_RIGHT : str_ieq(m, "JUSTIFY") ? AL_JUSTIFY : -1;
    if (al < 0) return 10;
    DoMethod(view, MUIM_Quill_Command, CMD_ALIGN, (ULONG)al);
    return 0; }
  case RX_COLOR: {
    const char *m = (const char *)arg[0];
    ULONG v = 0;
    if (*m == '#') m++;
    if (str_ieq(m, "AUTO")) v = COLOR_AUTO;
    else for (; *m; m++) v = (v << 4) | (ULONG)(*m >= 'a' ? *m - 'a' + 10 : *m >= 'A' ? *m - 'A' + 10 : *m - '0');
    DoMethod(view, MUIM_Quill_Command, CMD_COLOR, v);
    return 0; }
  case RX_SELECTALL: command(ID_SELALL); return 0;
  case RX_CUT: command(ID_CUT); return 0;
  case RX_COPY: command(ID_COPY); return 0;
  case RX_PASTE: command(ID_PASTE); return 0;
  case RX_UNDO: command(ID_UNDO); return 0;
  case RX_REDO: command(ID_REDO); return 0;
  case RX_DELETE: DoMethod(view, MUIM_Quill_Command, CMD_DELETE, 0); return 0;
  case RX_GOTOTOP: DoMethod(view, MUIM_Quill_Command, CMD_GOTO, 0); return 0;
  case RX_GOTOEND: DoMethod(view, MUIM_Quill_Command, CMD_END, 0); return 0;
  case RX_FIND: latin1_utf8((const char *)arg[0], u, sizeof u); return DoMethod(view, MUIM_Quill_Command, CMD_FIND, (ULONG)u), 0;
  case RX_GETTEXT: {
    /* the selection, or the caret's paragraph */
    char *t;
    u32 n;
    get(view, MUIA_Quill_Caret, &c); get(view, MUIA_Quill_Anchor, &an);
    if (!c || !an) return 10;
    if (!pos_cmp(*c, *an)) { Pos a = *c, b = *c; a.off = 0; b.off = doc->p[b.para]->len; t = doc_text(doc, a, b, &n); }
    else t = doc_text(doc, *c, *an, &n);
    if (!t) return 20;
    utf8_to_latin1(t, n, rexxbuf, sizeof rexxbuf);
    free(t);
    set(app, MUIA_Application_RexxString, (ULONG)rexxbuf);
    return 0; }
  case RX_GETWORDS: sprintf(u, "%lu", (unsigned long)doc_count_words(doc)); rexx_result(u); return 0;
  case RX_GETPAGES: { Layout *L = NULL; get(view, MUIA_Quill_Layout, &L); sprintf(u, "%lu", L ? (unsigned long)L->npages : 0UL); rexx_result(u); return 0; }
  case RX_GETFILENAME: rexx_result(docpath); return 0;
  case RX_INSERTTABLE: {
    LONG r = *(LONG *)arg[0], k = *(LONG *)arg[1];
    if (r < 1 || k < 1 || k > MAX_COLS || r > 1000) return 10;
    DoMethod(view, MUIM_Quill_Command, CMD_TABLE, (ULONG)((r << 8) | k));
    return 0; }
  case RX_ACTIVATE: set(win, MUIA_Window_Open, TRUE); set(win, MUIA_Window_Activate, TRUE); return 0;
  }
  if (!o) return 20;
  return 20;
}
#define NRX 34
extern ULONG rexx_entry(void);
static struct Hook rxhooks[NRX];
static struct MUI_Command rxcmds[NRX + 1];
/* the command table for MUIA_Application_Commands, so it must run before the application is made.
   MUI parses each command's arguments with its ReadArgs template and calls its hook, which carries
   the RX_ number in h_Data */
static void rexx_setup(void)
{
  static const struct { const char *name, *tmpl; int params; } t[NRX] = {
    { "NEW", NULL, 0 }, { "OPEN", "FILE/A", 1 }, { "SAVE", NULL, 0 }, { "SAVEAS", "FILE/A", 1 }, { "EXPORTPDF", "FILE/A", 1 },
    { "PRINT", NULL, 0 }, { "TEXT", "TEXT/A/F", 1 }, { "NEWPARAGRAPH", NULL, 0 }, { "LINEBREAK", NULL, 0 }, { "PAGEBREAK", NULL, 0 },
    { "BOLD", NULL, 0 }, { "ITALIC", NULL, 0 }, { "UNDERLINE", NULL, 0 }, { "FONT", "NAME/A/F", 1 }, { "SIZE", "POINTS/A/N", 1 },
    { "STYLE", "NAME/A/F", 1 }, { "ALIGN", "MODE/A", 1 }, { "COLOR", "RGB/A", 1 }, { "SELECTALL", NULL, 0 }, { "CUT", NULL, 0 },
    { "COPY", NULL, 0 }, { "PASTE", NULL, 0 }, { "UNDO", NULL, 0 }, { "REDO", NULL, 0 }, { "DELETE", NULL, 0 },
    { "GOTOTOP", NULL, 0 }, { "GOTOEND", NULL, 0 }, { "FIND", "TEXT/A/F", 1 }, { "GETTEXT", NULL, 0 }, { "GETWORDS", NULL, 0 },
    { "GETPAGES", NULL, 0 }, { "GETFILENAME", NULL, 0 }, { "INSERTTABLE", "ROWS/A/N,COLUMNS/A/N", 2 }, { "ACTIVATE", NULL, 0 },
  };
  int i;
  memset(rxhooks, 0, sizeof rxhooks); memset(rxcmds, 0, sizeof rxcmds);
  for (i = 0; i < NRX; i++) {
    rxhooks[i].h_Entry = (HOOKFUNC)rexx_entry;
    rxhooks[i].h_Data = (APTR)(ULONG)(i + 1);
    rxcmds[i].mc_Name = (char *)t[i].name;
    rxcmds[i].mc_Template = (char *)t[i].tmpl;
    rxcmds[i].mc_Parameters = t[i].params;
    rxcmds[i].mc_Hook = &rxhooks[i];
  }
}

/* ------------------------------------------------------------------ */
/* the window, as WordCraft's: the ribbon, the navigation pane beside the rulers and the page, the
   status bar */
/* one of the window's RB_ settings (ruler, navigation pane, comments, tracking...) on or off; the
   ribbon and view read them from tb.uibits, and the view is redrawn */
static void ui_bit(int b, int on)
{
  if (on) uibits |= RBIT(b); else uibits &= ~RBIT(b);
  tb.uibits = uibits;
  DoMethod(view, MUIM_Quill_Command, CMD_REFRESH, 0);
}
/* the tools around the page: hidden in focus and in read mode (the status bar stays, for its view buttons) */
static void chrome(void)
{
  ULONG vm = VM_PRINT, nav = (uibits & RBIT(RB_NAVPANE)) != 0, rl = (uibits & RBIT(RB_RULER)) != 0, bare;
  get(view, MUIA_Quill_ViewMode, &vm);
  bare = focus || vm == VM_READ;
  if (bare == chrome_bare) return;
  chrome_bare = (int)bare;
  DoMethod(win, MUIM_Group_InitChange);
  set(ribbon, MUIA_ShowMe, !bare);
  set(navgrp, MUIA_ShowMe, !bare && nav);
  set(ruler, MUIA_ShowMe, !bare && rl); set(vruler, MUIA_ShowMe, !bare && rl);
  DoMethod(win, MUIM_Group_ExitChange);
}
/* the watermark window: WP22a's Printed Watermark (amiga/wp_pages.c: a picture or words) */
static Object *wmwin;
static void open_watermark(void) { wp22a_command(ID_PG_WATERMARK, WPV_NONE); }
/* ------------------------------------------------------------------ */
/* hyperlinks (0.13): the Link window (the text to show and the address), opening a link, copying
   its address. The view does the editing (CMD_LINK, CMD_UNLINK) and says what link the caret is
   on (MUIA_Quill_LinkInfo) */
static Object *lktext, *lkurl, *lkremove;
/* Insert > Link (or Edit Link... on a link's menu): the window made the first time, then filled from
   the link at the caret, or the selected text for a new one. Remove Link shows only for a link */
static void open_link(void)
{
  const struct QLink *q = NULL;
  char lat[512];
  if (!lkwin) {
    Object *ok, *cancel;
    lkwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Link"), MUIA_Window_ID, MAKE_ID('L','I','N','K'),
      WindowContents, VGroup, QDLG,
        Child, ColGroup(2),
          Child, Label2(tr("Text to display")), Child, lktext = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
          Child, Label2(tr("Address")), Child, lkurl = StringObject, StringFrame, MUIA_String_MaxLen, 500, MUIA_CycleChain, 1, End,
        End,
        Child, LLabel(tr("A web page, an e-mail address or #name in this document")),
        Child, HGroup, Child, ok = QButton(tr("_OK")), Child, lkremove = QButton(tr("_Remove Link")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
      End, End;
    if (!lkwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)lkwin);
    DoMethod(lkwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)lkwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)lkwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LINKGO);
    DoMethod(lkurl, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LINKGO);
    DoMethod(lkremove, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_UNLINK);
  }
  get(view, MUIA_Quill_LinkInfo, &q);
  utf8_to_latin1(q ? q->text : "", q ? (u32)strlen(q->text) : 0, lat, sizeof lat);
  set(lktext, MUIA_String_Contents, (ULONG)lat);
  utf8_to_latin1(q ? q->url : "", q ? (u32)strlen(q->url) : 0, lat, sizeof lat);
  set(lkurl, MUIA_String_Contents, (ULONG)lat);
  set(lkremove, MUIA_ShowMe, q && q->url[0]);              /* (only for a link already there) */
  set(lkwin, MUIA_Window_Open, TRUE);
  set(lkwin, MUIA_Window_ActiveObject, (ULONG)(q && q->text[0] ? lkurl : lktext));
}
/* OK in the Link window: the address tidied up as people type them (www.amiga.org, me@home) */
static void link_done(void)
{
  char *t = NULL, *u = NULL, ut[512], uu[600], full[620];
  struct QLink q;
  get(lktext, MUIA_String_Contents, &t); get(lkurl, MUIA_String_Contents, &u);
  latin1_utf8(t ? t : "", ut, sizeof ut);
  latin1_utf8(u ? u : "", uu, sizeof uu);
  if (!uu[0]) { DisplayBeep(NULL); return; }
  if (uu[0] != '#' && !strstr(uu, "://") && strncmp(uu, "mailto:", 7)) {
    if (strchr(uu, '@') && !strchr(uu, '/')) snprintf(full, sizeof full, "mailto:%s", uu);
    else snprintf(full, sizeof full, "https://%s", uu);
  } else snprintf(full, sizeof full, "%s", uu);
  q.url = full; q.text = ut[0] ? ut : full;
  set(lkwin, MUIA_Window_Open, FALSE);
  DoMethod(view, MUIM_Quill_Command, CMD_LINK, (ULONG)&q);
  set(win, MUIA_Window_Activate, TRUE);
}
/* a link opened in the web browser (or mail program) through openurl.library, which most Amiga
   browsers install; without it, the address is shown so it can be typed in. A link to a place in
   the document (#name) goes to that bookmark instead */
static ULONG URL_OpenA_(__reg("a6") struct Library *base, __reg("a0") STRPTR url, __reg("a1") struct TagItem *tags) = "\tjsr\t-30(a6)";
static void link_open(void)
{
  const struct QLink *q = NULL;
  struct Library *ou;
  char lat[512];
  get(view, MUIA_Quill_LinkInfo, &q);
  if (!q || !q->url[0]) { DisplayBeep(NULL); return; }
  utf8_to_latin1(q->url, (u32)strlen(q->url), lat, sizeof lat);
  if (q->url[0] == '#') {
    /* a place in the document: its bookmark, if it is there */
    char name[200];
    strncpy(name, q->url + 1, sizeof name - 1); name[sizeof name - 1] = 0;
    if (!doc_bmark_find(doc, name, NULL, NULL)) { ask("OK", "This link goes to \"%s\" in the document,\nbut there's no bookmark of that name.", lat + 1); return; }
    DoMethod(view, MUIM_Quill_Command, CMD_GOTOBM, (ULONG)name);
    return;
  }
  if ((ou = OpenLibrary((STRPTR)"openurl.library", 1))) {
    ULONG ok = URL_OpenA_(ou, (STRPTR)lat, NULL);
    CloseLibrary(ou);
    if (ok) return;
  }
  ask("OK", "Quill couldn't open the link, which goes to\n\n%s\n\n(openurl.library and a web browser open links.)", lat);
}
/* the address of the link at the caret, onto the clipboard */
static void link_copy(void)
{
  const struct QLink *q = NULL;
  char lat[512];
  get(view, MUIA_Quill_LinkInfo, &q);
  if (!q || !q->url[0]) { DisplayBeep(NULL); return; }
  utf8_to_latin1(q->url, (u32)strlen(q->url), lat, sizeof lat);
  clip_write(lat, (long)strlen(lat));
}
/* a choice from one of the ribbon's lists or galleries: id says which list, MUIA_Ribbon_Value which
   entry (or, for the colour lists, the colour as 0xRRGGBB) */
static void ribbon_pick(ULONG id, LONG v)
{
  struct QParaFmt q;
  Para *p;
  Pos *c = NULL;
  if ((ULONG)v == COLOR_MORE) { open_morecol(id); return; }       /* a colour list's More Colours... */
  get(view, MUIA_Quill_Caret, &c);
  p = c && c->para < doc->np ? doc->p[c->para] : doc->p[0];
  memset(&q, 0, sizeof q);
  q.pf = p->pf;
  switch (id) {
  case ID_FONTDD: if (ribbon_font_name((int)v)) DoMethod(view, MUIM_Quill_Command, CMD_FONT, (ULONG)ribbon_font_name((int)v)); break;
  case ID_SIZEDD: if (v > 0) DoMethod(view, MUIM_Quill_Command, CMD_SIZE, (ULONG)v); break;
  case ID_FONTCOLOR: DoMethod(view, MUIM_Quill_Command, CMD_COLOR, (ULONG)v); break;
  case ID_HIGHLIGHT: DoMethod(view, MUIM_Quill_Command, CMD_HIGHLIGHT, (ULONG)v); break;
  case ID_CASE: DoMethod(view, MUIM_Quill_Command, CMD_CASE, (ULONG)v); break;
  case ID_SHADINGDD: DoMethod(view, MUIM_Quill_Command, CMD_SHADE, (ULONG)v); break;
  case ID_TSTYLEDD: {
    int ts = v < 0 ? 0 : v >= 1000 ? (int)(v - 1000) : tstyle_use_builtin(doc, (int)v);
    if (v >= 0 && !ts) { DisplayBeep(NULL); break; }
    DoMethod(view, MUIM_Quill_Command, CMD_TSTYLE, (ULONG)ts);
    break; }
  case ID_PENSTYLE: pen.style = (u8)(v + PB_SINGLE); break;
  case ID_PENWIDTH: if (v >= 0 && v < 7) pen.width = ribbon_penwidths[v]; break;
  case ID_PENCOLOR: pen.color = (u32)v; break;
  case ID_TBORDERSDD: pen.which = (int)v; DoMethod(view, MUIM_Quill_Command, CMD_TBORDERS, (ULONG)&pen); break;
  case ID_TSELECTDD: DoMethod(view, MUIM_Quill_Command, CMD_TSELECT, (ULONG)v); break;
  case ID_AUTOFITDD: DoMethod(view, MUIM_Quill_Command, CMD_AUTOFIT, (ULONG)v); break;
  case ID_ROWH: if (v >= 0 && v < 7) DoMethod(view, MUIM_Quill_Command, CMD_ROWH, (ULONG)ribbon_rowheights[v]); break;
  case ID_COLW: if (v >= 0 && v < 10) DoMethod(view, MUIM_Quill_Command, CMD_COLW, (ULONG)ribbon_colwidths[v]); break;
  case ID_PASTESPECDD: case ID_PASTEDD: if (v >= 0 && v < 3) DoMethod(view, MUIM_Quill_Command, CMD_PASTEAS, (ULONG)v); else if (v >= 3) wp10_list_extra(id, v - 3); break;
  case ID_FINDDD: if (v >= 1) wp_focus_kept = wp_run(v == 1 ? ID_FX_GOTO : ID_FX_ADVFIND, WPV_NONE) == WPR_KEEPFOCUS; else open_find(); break;   /* (WP12) */
  case ID_HEADERDD: case ID_FOOTERDD: if (v == 1) remove_headfoot(id == ID_FOOTERDD); else edit_headfoot(id == ID_FOOTERDD, -1); break;
  case ID_TOCDD: DoMethod(view, MUIM_Quill_Command, v == 1 ? CMD_TOCREMOVE : CMD_TOC, 0); break;
  case ID_ADDTEXTDD: add_text((int)v); break;
  case ID_FORWARDDD: DoMethod(view, MUIM_Quill_Command, CMD_RESTACK, (ULONG)(v == 1 ? 2 : 1)); selpane_fill(); break;
  case ID_BACKWARDDD: DoMethod(view, MUIM_Quill_Command, CMD_RESTACK, (ULONG)(LONG)(v == 1 ? -2 : -1)); selpane_fill(); break;
  case ID_ALIGNOBJDD: if (v >= 0 && v < 8) DoMethod(view, MUIM_Quill_Command, CMD_OBJALIGN, (ULONG)v); break;
  case ID_GROUPDD: command(v == 1 ? ID_UNGROUP : ID_GROUP); selpane_fill(); break;
  case ID_PAGENUMDD: if (v >= 0 && v <= 6) page_number((int)v); else if (v == 7) open_pgfmt(); else if (v == 8) remove_page_numbers(); break;
  case ID_COVERDD: cover_page(v); break;
  case ID_MULTILISTDD: multilist_choice(v); break;
  case ID_PBORDERDD: if (v == 8) open_borders(); else if (v == 9) wp10_list_extra(id, 0); else border_preset((int)v); break;
  case ID_PSHADEDD: shade_para(v); break;
  case ID_STYLESETDD: if (v >= 0 && v < SS_N) DoMethod(view, MUIM_Quill_Command, CMD_DOCLOOK, (ULONG)(0 << 8 | v)); break;
  case ID_THEMEFONTDD: if (v >= 0 && v < TF_N) DoMethod(view, MUIM_Quill_Command, CMD_DOCLOOK, (ULONG)(1 << 8 | v)); break;
  case ID_PSPACINGDD: if (v >= 0 && v < PS_N) DoMethod(view, MUIM_Quill_Command, CMD_DOCLOOK, (ULONG)(2 << 8 | v)); break;
  case ID_UNDERLINEDD:
    if (v >= 0 && v < UL_N) {
      static struct QCharFmt q;
      memset(&q, 0, sizeof q);
      q.mask = CM_UNDERLINE | CM_UL; q.cf.flags = CF_UNDERLINE; q.cf.ul = (u8)v;
      DoMethod(view, MUIM_Quill_Command, CMD_CHARFMT, (ULONG)&q);
    } else if (v == UL_N) command(ID_FONTDLG);
    break;
  case ID_COLORSETDD: if (v >= 0 && v < CS_N) DoMethod(view, MUIM_Quill_Command, CMD_DOCLOOK, (ULONG)(3 << 8 | v)); break;
  case ID_THEMEDD: if (v >= 0 && v < TH_N) DoMethod(view, MUIM_Quill_Command, CMD_DOCLOOK, (ULONG)(4 << 8 | v)); break;
  case ID_PAGECOLORDD: DoMethod(view, MUIM_Quill_Command, CMD_PAGECOLOR, v == COLOR_AUTO ? 0 : HL_ON | (v & 0xFFFFFF)); break;
  case ID_SHAPESDD: case ID_OBJFILLDD: case ID_OBJLINEDD: case ID_OBJWEIGHTDD: case ID_OBJDASHDD: case ID_OBJPOSDD: case ID_OBJWRAPDD: case ID_OBJROTDD:
    obj_choice(id, v); break;
  case ID_ACCEPTDD: case ID_REJECTDD: {
    /* the lists: ... and Move to Next, ... This Change, ... All Changes */
    static const int how[3] = { 2, 0, 1 };
    if (v >= 0 && v < 3) DoMethod(view, MUIM_Quill_Command, CMD_RESOLVE, (ULONG)((how[v] << 1) | (id == ID_ACCEPTDD)));
    break; }
  case ID_SPACINGDD: if (v >= 0 && v < 6) DoMethod(view, MUIM_Quill_Command, CMD_SPACING, (ULONG)spacings[v]); else if (v >= 6) wp10_list_extra(id, v - 6); break;
  case ID_STYLEPICK: DoMethod(view, MUIM_Quill_Command, CMD_STYLE, (ULONG)v); break;
  case ID_SELECTDD: if (v >= 3) { wp_run(v == 3 ? ID_FX_SELOBJ : ID_FX_SIMILAR, WPV_NONE); break; }   /* (WP12) */
    if (v == 1 || v == 2) DoMethod(view, MUIM_Quill_Command, CMD_SELUNIT, (ULONG)(v - 1)); else command(ID_SELALL); break;
  case ID_MARGINSDD: if (v >= NMARGINS) open_pagesetup(); else page_setup(-1, -1, (int)v); break;
  case ID_ORIENTDD: page_setup(-1, (int)v, -1); break;
  case ID_PAPERDD: if (v >= NPAPERS) open_pagesetup(); else page_setup((int)v, -1, -1); break;
  case ID_BREAKSDD:
    /* page, column, line; then sections from a new page, on the same one, from an even or odd page */
    if (v == 0) command(ID_PAGEBREAK);
    else if (v == 1) DoMethod(view, MUIM_Quill_Command, CMD_COLBREAK, 0);
    else if (v == 2) command(ID_LINEBREAK);
    else if (v >= 3 && v <= 6) { static const int sb[4] = { SB_NEXT, SB_CONT, SB_EVEN, SB_ODD }; DoMethod(view, MUIM_Quill_Command, CMD_SECTBREAK, (ULONG)sb[v - 3]); }
    break;
  case ID_COLUMNSDD: if (v >= 0 && v <= 3) DoMethod(view, MUIM_Quill_Command, CMD_COLUMNS, (ULONG)(v < 3 ? v + 1 : 0)); else if (v > 3) wp22a_command(ID_PG_COLLIST, v); break;
  case ID_LINENUMDD: { static const int ln[4] = { LN_NONE, LN_CONT, LN_PAGE, LN_SECTION }; if (v == 4) open_linenum(); else if (v >= 0 && v < 4) DoMethod(view, MUIM_Quill_Command, CMD_LINENUM, (ULONG)ln[v]); break; }
  case ID_HYPHENDD:
    if (v >= 2) { wp22a_command(ID_PG_HYPHLIST, v); break; }      /* (WP22a: Manual, Hyphenation Options) */
    if (v == 1 && !hyph_ready()) hyph_load("PROGDIR:Dict/hyph_en_GB.dic");
    DoMethod(view, MUIM_Quill_Command, CMD_HYPHENATE, (ULONG)(v == 1));
    break;
  case ID_PAGEALIGNDD: if (v >= 0 && v < 4) DoMethod(view, MUIM_Quill_Command, CMD_PAGEALIGN, (ULONG)v); break;     /* (WP22a: 3 Justified) */
  case ID_DROPCAPDD: if (v == 3) open_dropcap(); else if (v >= 0 && v < 3) DoMethod(view, MUIM_Quill_Command, CMD_DROPCAP, (ULONG)v); break;
  case ID_WATERMARKDD: {
    static const char *const wm[] = { "", "DRAFT", "CONFIDENTIAL", "SAMPLE", "DO NOT COPY", "URGENT" };
    if (v == 6) { open_watermark(); break; }
    if (v < 0 || v > 5) break;
    latin1_utf8(tr(wm[v]), doc->wm.text, sizeof doc->wm.text);       /* in the interface's language, as the list shows it */
    free(doc->wm.pic); doc->wm.pic = NULL; doc->wm.piclen = 0;      /* (WP22a: words instead of a picture) */
    doc->wm.color = 0xC0C0C0; doc->wm.horizontal = 0; doc->wm.font[0] = 0;
    DoMethod(view, MUIM_Quill_Command, CMD_DESIGN, 0);
    break; }
  case ID_BORDERDD:
    if (v == 6) { wp22a_command(ID_PG_BORDERS, WPV_NONE); break; }       /* (WP22a: Page Borders...) */
    if (v < 0 || v > 5) break;
    memset(&doc->border, 0, sizeof doc->border);
    if (v) {
      static const u8 st[6] = { PB_NONE, PB_SINGLE, PB_DOUBLE, PB_THICK, PB_DOTTED, PB_DASHED };
      doc->border.style = st[v]; doc->border.width = (u8)(st[v] == PB_THICK ? 18 : 4); doc->border.color = COLOR_AUTO; doc->border.frompage = 1; doc->border.space = 480;
    }
    DoMethod(view, MUIM_Quill_Command, CMD_DESIGN, 0);
    break;
  case ID_INDENTL: case ID_INDENTR:
    if (id == ID_INDENTL) q.pf.left = indents[v & 7]; else q.pf.right = indents[v & 7];
    q.mask = PM_INDENT;
    DoMethod(view, MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
    break;
  case ID_SPACEB: case ID_SPACEA:
    if (id == ID_SPACEB) q.pf.before = spaces[v & 7]; else q.pf.after = spaces[v & 7];
    q.mask = PM_SPACING;
    DoMethod(view, MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
    break;
  case ID_ZOOMDD: if (v >= 0 && v < 7) set(view, MUIA_Quill_Zoom, zooms[v]); break;
  case ID_SYMBOLDD:
    if (v >= 0 && v < (LONG)(sizeof symbols / sizeof symbols[0])) {
      char u[8];
      u[utf8_put(u, symbols[v])] = 0;
      DoMethod(view, MUIM_Quill_Command, CMD_TEXT, (ULONG)u);
    } else if (v == (LONG)(sizeof symbols / sizeof symbols[0])) wp10_list_extra(id, 0);    /* More Symbols... (WP10) */
    break;
  case ID_DELTABLEDD: command(v == 0 ? ID_DEL_ROW : v == 1 ? ID_DEL_COL : ID_DEL_TABLE); break;
  }
}
/* the ribbon's list choice as main()'s loop hands it on: the entry is the ribbon's MUIA_Ribbon_Value */
static void ribbon_choice(ULONG id)
{
  LONG v = 0;
  get(ribbon, MUIA_Ribbon_Value, &v);
  wp12_note(id, v);                      /* WP12: Repeat (F4) */
  if (id >= ID_WP_FIRST && id <= ID_WP_LAST) { wp_focus_kept = wp_run(id, v) == WPR_KEEPFOCUS; return; }   /* a work package's list */
  ribbon_pick(id, v);
}
/* WP12: Repeat (F4) runs the last command that changed the document again (a list's with its entry) */
void wp12_replay(ULONG id, LONG v)
{
  if (v == WPV_NONE) command(id);
  else if (id >= ID_WP_FIRST && id <= ID_WP_LAST) wp_focus_kept = wp_run(id, v) == WPR_KEEPFOCUS;
  else ribbon_pick(id, v);
}

/* ---- Go To, Drop Cap, Line Numbers and More Colours: the WordCraft comparison's small dialogs */
static Object *go_page;
/* Go To (Edit menu, or Find's arrow): a page by its number */
static void open_goto(void)
{
  if (!gotowin) {
    Object *c = HGroup, Child, Label2(tr("Page number")), Child, go_page = StringObject, StringFrame, MUIA_String_MaxLen, 6,
      MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End, End;
    if (!(gotowin = dialog("Go To", MAKE_ID('G','O','T','O'), c, "_Go To", ID_GOTOGO))) return;
    DoMethod(go_page, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_GOTOGO);
  }
  set(gotowin, MUIA_Window_Open, TRUE);
  set(gotowin, MUIA_Window_ActiveObject, (ULONG)go_page);
}
/* (the caret to the first line of that page, which the view scrolls into sight; a page that isn't
   there beeps) */
static void goto_done(void)
{
  char *s = NULL;
  long n = 0;
  Layout *L = view_layout();
  Pos p;
  set(gotowin, MUIA_Window_Open, FALSE);
  get(go_page, MUIA_String_Contents, &s);
  if (s) n = atol(s);
  if (!L || n < 1 || n > (long)L->npages) { DisplayBeep(NULL); return; }
  p = lay_hit(L, (u32)(n - 1), 0, 0);
  set(view, MUIA_Quill_Caret, (ULONG)&p);
  set(win, MUIA_Window_Activate, TRUE);
}
static Object *dc_pos, *dc_lines;
/* Drop Cap Options, for the caret's paragraph: where the big letter goes and how many lines it covers */
static void open_dropcap(void)
{
  static const char *pos[4];
  const ParaFmt *pf = caret_pf();
  if (!pf) return;
  if (!dcwin) {
    Object *c;
    pos[0] = tr("None"); pos[1] = tr("Dropped"); pos[2] = tr("In Margin"); pos[3] = NULL;
    c = ColGroup(2),
      Child, Label1(tr("Position")), Child, dc_pos = CycleObject, MUIA_Cycle_Entries, (ULONG)pos, MUIA_CycleChain, 1, End,
      Child, Label2(tr("Lines to drop")), Child, dc_lines = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 10, MUIA_Numeric_Value, 3, MUIA_CycleChain, 1, End,
    End;
    if (!(dcwin = dialog("Drop Cap", MAKE_ID('D','C','A','P'), c, "_OK", ID_DROPCAPGO))) return;
  }
  set(dc_pos, MUIA_Cycle_Active, !pf->dropcap ? 0 : pf->dropmargin ? 2 : 1);
  set(dc_lines, MUIA_Numeric_Value, pf->dropcap ? pf->dropcap : 3);
  set(dcwin, MUIA_Window_Open, TRUE);
}
/* (OK: the position and the lines go to the view in one argument, the lines in the second byte) */
static void dropcap_done(void)
{
  ULONG pos = 0, lines = 3;
  set(dcwin, MUIA_Window_Open, FALSE);
  get(dc_pos, MUIA_Cycle_Active, &pos);
  get(dc_lines, MUIA_Numeric_Value, &lines);
  DoMethod(view, MUIM_Quill_Command, CMD_DROPCAP, (ULONG)(pos | lines << 8));
  set(win, MUIA_Window_Activate, TRUE);
}
/* Line Numbering Options: WP22a's (amiga/wp_pages.c), with Apply to */
static void open_linenum(void) { wp22a_command(ID_PG_LINENUM, WPV_NONE); }
static Object *mc_adj, *mc_rgb[3];
static ULONG mc_for, mc_last = 0x4472C4;      /* the list More Colours was picked from; the colour last chosen */
/* More Colours...: MUI's colour adjuster (a wheel and red, green and blue sliders), for the colour
   list id; OK runs that list's choice with the colour made. Coloradjust.mui is a class of its own
   that not every MUI has installed: without it, three sliders do the same */
static void open_morecol(ULONG id)
{
  int i;
  mc_for = id;
  if (!mcwin) {
    Object *c = mc_adj = ColoradjustObject, MUIA_CycleChain, 1, End;
    if (!c) {
      static const char *const names[3] = { "Red", "Green", "Blue" };
      c = ColGroup(2), End;
      for (i = 0; c && i < 3; i++) {
        Object *l = Label2(tr(names[i]));
        mc_rgb[i] = SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 255, MUIA_CycleChain, 1, End;
        if (l) DoMethod(c, OM_ADDMEMBER, (ULONG)l);
        if (mc_rgb[i]) DoMethod(c, OM_ADDMEMBER, (ULONG)mc_rgb[i]);
      }
    }
    if (!(mcwin = dialog("Colours", MAKE_ID('M','C','O','L'), c, "_OK", ID_MORECOLGO))) return;
  }
  if (mc_adj)
    SetAttrs(mc_adj, MUIA_Coloradjust_Red, ((mc_last >> 16) & 255) * 0x01010101UL, MUIA_Coloradjust_Green, ((mc_last >> 8) & 255) * 0x01010101UL,
             MUIA_Coloradjust_Blue, (mc_last & 255) * 0x01010101UL, TAG_DONE);
  else for (i = 0; i < 3; i++) if (mc_rgb[i]) set(mc_rgb[i], MUIA_Numeric_Value, (mc_last >> (16 - 8 * i)) & 255);
  set(mcwin, MUIA_Window_Open, TRUE);
}
/* (OK: the colour as 0xRRGGBB, and the list's own choice run with it, as if it had been in the list) */
static void morecol_done(void)
{
  ULONG r = 0, g = 0, b = 0;
  set(mcwin, MUIA_Window_Open, FALSE);
  if (mc_adj) {
    get(mc_adj, MUIA_Coloradjust_Red, &r); get(mc_adj, MUIA_Coloradjust_Green, &g); get(mc_adj, MUIA_Coloradjust_Blue, &b);
    r >>= 24; g >>= 24; b >>= 24;
  } else {
    if (mc_rgb[0]) get(mc_rgb[0], MUIA_Numeric_Value, &r);
    if (mc_rgb[1]) get(mc_rgb[1], MUIA_Numeric_Value, &g);
    if (mc_rgb[2]) get(mc_rgb[2], MUIA_Numeric_Value, &b);
  }
  mc_last = (r & 255) << 16 | (g & 255) << 8 | (b & 255);
  ribbon_pick(mc_for, (LONG)mc_last);
  set(win, MUIA_Window_Activate, TRUE);
}
/* Remove Header (or Footer), from the arrow on its button: the caret's section's headers emptied,
   first-page and even-page ones too. (As Page Number, not an undo step: the header is a story of
   its own, outside the document's undo) */
static void remove_headfoot(int footer)
{
  Layout *L = view_layout();
  Pos *c = NULL;
  int w, sect = 0, k;
  story_close(1);
  get(view, MUIA_Quill_Caret, &c);
  if (L && c && L->npages) { u32 pg; i32 x, y, h; lay_caret(L, *c, &pg, &x, &y, &h); lay_page_hf(L, pg, &w, &sect); }
  if (sect && !doc->sects[sect - 1].hf) sect = 0;
  for (k = 0; k < HF_N; k++)
    story_set_text(doc, sect ? &doc->sects[sect - 1].hf[(footer ? HF_N : 0) + k] : footer ? &doc->ftr[k] : &doc->hdr[k], "", 0, AL_LEFT);
  doc->dirty = 1;
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);
}
/* Add Text (References): the caret's paragraph into the table of contents at level 1 to 3, by
   giving it that Heading style, or (0) out of it, back to Normal. Update Table shows the change */
static void add_text(int level)
{
  static const char *const names[4] = { "Normal", "Heading1", "Heading2", "Heading3" };
  int k;
  if (level < 0 || level > 3) return;
  if ((k = doc_style(doc, names[level])) < 0) { DisplayBeep(NULL); return; }
  DoMethod(view, MUIM_Quill_Command, CMD_STYLE, (ULONG)k);
}

/* ---- page numbers and cover pages */
/* the header (or footer) story of the caret's section, default pages; *footer says which */
static Story *caret_hf(int footer)
{
  Layout *L = view_layout();
  Pos *c = NULL;
  int w, sect = 0;
  get(view, MUIA_Quill_Caret, &c);
  if (L && c && L->npages) { u32 pg; i32 x, y, h; lay_caret(L, *c, &pg, &x, &y, &h); lay_page_hf(L, pg, &w, &sect); }
  if (sect && !doc->sects[sect - 1].hf) sect = 0;
  return sect ? &doc->sects[sect - 1].hf[(footer ? HF_N : 0) + HF_DEFAULT] : footer ? &doc->ftr[HF_DEFAULT] : &doc->hdr[HF_DEFAULT];
}
/* Insert > Page Number's list: the number at the top (0-2) or bottom (3-5) of the page, left,
   centred or right; 6 "Page 1 of 3" at the foot, centred. It is the whole header or footer of the
   caret's section (as Word's plain page numbers are) */
static void page_number(int where)
{
  static const u8 al[3] = { AL_LEFT, AL_CENTER, AL_RIGHT };
  int footer = where >= 3, k = doc_style(doc, footer ? "Footer" : "Header");
  char t[80];
  story_close(1);
  if (where == 6) latin1_utf8(tr("Page {page} of {pages}"), t, sizeof t); else strcpy(t, "{page}");
  story_set_text(doc, caret_hf(footer), t, k < 0 ? 0 : k, where == 6 ? AL_CENTER : al[where % 3]);
  doc->dirty = 1;
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);
}
/* Remove Page Numbers: every header and footer's page fields taken out ("Page 1 of 3" all of it);
   one left empty goes */
static void remove_page_numbers(void)
{
  int i, k;
  char pof[80];
  story_close(1);
  latin1_utf8(tr("Page {page} of {pages}"), pof, sizeof pof);
  for (i = -1; i < doc->nsects; i++)
    for (k = 0; k < 2 * HF_N; k++) {
      Story *st = i < 0 ? (k < HF_N ? &doc->hdr[k] : &doc->ftr[k - HF_N]) : doc->sects[i].hf ? &doc->sects[i].hf[k] : NULL;
      char *t, *q;
      int style, align;
      if (!st || !st->np || !(t = story_text(st))) continue;
      if (!strstr(t, "{page}")) { free(t); continue; }
      style = st->p[0]->pf.style; align = st->p[0]->pf.align;
      while ((q = strstr(t, pof))) memmove(q, q + strlen(pof), strlen(q + strlen(pof)) + 1);
      while ((q = strstr(t, "{pages}"))) memmove(q, q + 7, strlen(q + 7) + 1);
      while ((q = strstr(t, "{page}"))) memmove(q, q + 6, strlen(q + 6) + 1);
      for (q = t; *q == ' '; q++) ;
      story_set_text(doc, st, q, style, align);
      free(t);
    }
  doc->dirty = 1;
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);
}
static Object *pf_fmt, *pf_start;
/* Format Page Numbers: the numbers' style and the first page's number, for the whole document */
static void open_pgfmt(void)
{
  static const char *fmts[6];
  static const u8 nf[5] = { NF_DECIMAL, NF_LOWER_LETTER, NF_UPPER_LETTER, NF_LOWER_ROMAN, NF_UPPER_ROMAN };
  int i, cur = 0;
  char b[16];
  if (!pfwin) {
    Object *c;
    fmts[0] = "1, 2, 3"; fmts[1] = "a, b, c"; fmts[2] = "A, B, C"; fmts[3] = "i, ii, iii"; fmts[4] = "I, II, III"; fmts[5] = NULL;
    c = ColGroup(2),
      Child, Label1(tr("Number format")), Child, pf_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmts, MUIA_CycleChain, 1, End,
      Child, Label2(tr("Start at")), Child, pf_start = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
    End;
    if (!(pfwin = dialog("Page Number Format", MAKE_ID('P','G','F','M'), c, "_OK", ID_PGFMTGO))) return;
  }
  for (i = 0; i < 5; i++) if (nf[i] == doc->pgfmt) cur = i;
  set(pf_fmt, MUIA_Cycle_Active, cur);
  sprintf(b, "%u", (unsigned)(doc->pgstart > 1 ? doc->pgstart : 1)); set_str(pf_start, b);
  set(pfwin, MUIA_Window_Open, TRUE);
}
/* (OK: the document's page number format; not an undo step, like the page setup) */
static void pgfmt_done(void)
{
  static const u8 nf[5] = { NF_DECIMAL, NF_LOWER_LETTER, NF_UPPER_LETTER, NF_LOWER_ROMAN, NF_UPPER_ROMAN };
  ULONG f = 0;
  char *s = NULL;
  long v;
  set(pfwin, MUIA_Window_Open, FALSE);
  get(pf_fmt, MUIA_Cycle_Active, &f);
  get(pf_start, MUIA_String_Contents, &s);
  v = s ? atol(s) : 1;
  doc->pgfmt = nf[f < 5 ? f : 0];
  doc->pgstart = (u16)(v < 1 ? 1 : v > 32000 ? 32000 : v);
  doc->dirty = 1;
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);
  set(win, MUIA_Window_Activate, TRUE);
}
/* Insert > Cover Page's list: a design (0-2), or 3 to take the cover page out. The title is the
   document's (File > Info), else "Document Title"; the author the user's name; today's date */
static void cover_page(LONG v)
{
  char title[200], author[200], date[40], dl[40], sub[80];
  story_close(1);
  if (v == 3) { if (!doc_cover_remove(doc)) DisplayBeep(NULL); }
  else if (v >= 0 && v < 3) {
    if (doc->title[0]) strcpy(title, doc->title); else latin1_utf8(tr("Document Title"), title, sizeof title);
    latin1_utf8(tr("Subtitle"), sub, sizeof sub);
    latin1_utf8(username[0] ? username : "", author, sizeof author);
    today_text(dl); latin1_utf8(dl, date, sizeof date);
    if (!doc_cover_page(doc, (int)v, title, sub, author, date)) DisplayBeep(NULL);
  } else return;
  DoMethod(view, MUIM_Quill_Command, CMD_PAGESETUP, 0);     /* (laid out again: paragraphs came and went) */
}
/* ---- bookmarks and cross-references */
#define BM_MAX 300
static Object *bm_name, *bm_list;
static int bm_idx[BM_MAX], bm_n;        /* each line's bookmark (Doc.bmarks number) */
/* Insert > Bookmark: the document's bookmarks (not the hidden ones Word and cross-references make),
   in the order they come; a name to add, or a bookmark to remove or go to */
static void open_bookmarks(void)
{
  if (!bmwin) {
    Object *add, *del, *go, *close;
    bmwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Bookmark"), MUIA_Window_ID, MAKE_ID('B','M','R','K'),
      WindowContents, VGroup, QDLG,
        Child, HGroup, Child, Label2(tr("Bookmark name")), Child, bm_name = StringObject, StringFrame, MUIA_String_MaxLen, 40, MUIA_CycleChain, 1, End, End,
        Child, bm_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, add = QButton(tr("_Add")), Child, del = QButton(tr("_Delete")), Child, go = QButton(tr("_Go To")), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!bmwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)bmwin);
    DoMethod(bmwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)bmwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)bmwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(add, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BMADD);
    DoMethod(bm_name, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BMADD);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BMDEL);
    DoMethod(go, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BMGO);
    DoMethod(bm_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BMPICK);
    DoMethod(bm_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BMGO);
  }
  bm_fill();
  {
    /* a bookmark at the caret already: its name offered */
    Pos *c = caret_pos();
    int k = c ? doc_bmark_at(doc, *c, NULL, NULL) : 0;
    char lat[64] = "";
    if (k && doc_bmark_name(doc, k)[0] != '_') utf8_to_latin1(doc_bmark_name(doc, k), (u32)strlen(doc_bmark_name(doc, k)), lat, sizeof lat);
    set_str(bm_name, lat);
  }
  set(bmwin, MUIA_Window_Open, TRUE);
  set(bmwin, MUIA_Window_ActiveObject, (ULONG)bm_name);
}
/* the visible bookmarks that are in the text, in the order they come (k's first stretch's start) */
static int bm_collect(int *idx, int cap)
{
  Pos at[BM_MAX], a;
  int n = 0, k, i, j;
  for (k = 1; k <= doc->nbmarks && n < cap && n < BM_MAX; k++) {
    if (doc->bmarks[k - 1][0] == '_' || !doc_bmark_find(doc, doc->bmarks[k - 1], &a, NULL)) continue;
    for (i = n; i > 0 && pos_cmp(at[i - 1], a) > 0; i--) { at[i] = at[i - 1]; idx[i] = idx[i - 1]; }
    at[i] = a; idx[i] = k; n++;
  }
  (void)j;
  return n;
}
/* the list made again */
static void bm_fill(void)
{
  int i;
  if (!bm_list) return;
  set(bm_list, MUIA_List_Quiet, TRUE);
  DoMethod(bm_list, MUIM_List_Clear);
  bm_n = bm_collect(bm_idx, BM_MAX);
  for (i = 0; i < bm_n; i++) {
    char lat[64];
    const char *u = doc->bmarks[bm_idx[i] - 1];
    utf8_to_latin1(u, (u32)strlen(u), lat, sizeof lat);
    DoMethod(bm_list, MUIM_List_InsertSingle, (ULONG)lat, MUIV_List_Insert_Bottom);
  }
  set(bm_list, MUIA_List_Quiet, FALSE);
}
/* the Bookmark window's buttons. A name is made Word's way: letters, digits and underscores, from a
   letter (spaces become underscores); anything else beeps */
static void bm_command(ULONG id)
{
  char *s = NULL, name[64], utf[128];
  int i;
  LONG a = -1;
  if (id == ID_BMPICK) {
    get(bm_list, MUIA_List_Active, &a);
    if (a >= 0 && a < bm_n) { utf8_to_latin1(doc->bmarks[bm_idx[a] - 1], (u32)strlen(doc->bmarks[bm_idx[a] - 1]), name, sizeof name); set_str(bm_name, name); }
    return;
  }
  get(bm_name, MUIA_String_Contents, &s);
  strncpy(name, s ? s : "", sizeof name - 1); name[sizeof name - 1] = 0;
  for (i = 0; name[i]; i++) if (name[i] == ' ') name[i] = '_';
  latin1_utf8(name, utf, sizeof utf);
  if (id == ID_BMADD) {
    int good = isalpha((UBYTE)name[0]) || (UBYTE)name[0] >= 0xC0;
    for (i = 0; good && name[i]; i++) if (!isalnum((UBYTE)name[i]) && name[i] != '_' && (UBYTE)name[i] < 0xC0) good = 0;
    if (!good) { DisplayBeep(NULL); return; }
    DoMethod(view, MUIM_Quill_Command, CMD_BOOKMARK, (ULONG)utf);
    set(bmwin, MUIA_Window_Open, FALSE);
    set(win, MUIA_Window_Activate, TRUE);
    return;
  }
  if (!name[0]) { DisplayBeep(NULL); return; }
  if (id == ID_BMDEL) { DoMethod(view, MUIM_Quill_Command, CMD_UNBOOKMARK, (ULONG)utf); bm_fill(); set_str(bm_name, ""); }
  else { DoMethod(view, MUIM_Quill_Command, CMD_GOTOBM, (ULONG)utf); set(win, MUIA_Window_Activate, TRUE); }
}
static Object *xr_type, *xr_kind, *xr_list;
static int xr_idx[BM_MAX], xr_n;        /* each line's bookmark number, or heading paragraph */
/* Insert > Cross-reference: to a bookmark or a heading, its text or the page it is on. The window
   stays open, so several can be put in one after another */
static void open_xref(void)
{
  if (!xrwin) {
    static const char *types[3], *kinds[3];
    Object *ins, *close;
    types[0] = tr("Bookmark"); types[1] = tr("Heading"); types[2] = NULL;
    kinds[0] = tr("Its text"); kinds[1] = tr("Page number"); kinds[2] = NULL;
    xrwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Cross-reference"), MUIA_Window_ID, MAKE_ID('X','R','E','F'),
      WindowContents, VGroup, QDLG,
        Child, ColGroup(2),
          Child, Label1(tr("Reference type")), Child, xr_type = CycleObject, MUIA_Cycle_Entries, (ULONG)types, MUIA_CycleChain, 1, End,
          Child, Label1(tr("Insert reference to")), Child, xr_kind = CycleObject, MUIA_Cycle_Entries, (ULONG)kinds, MUIA_CycleChain, 1, End,
        End,
        Child, xr_list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, ins = QButton(tr("_Insert")), Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!xrwin) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)xrwin);
    DoMethod(xrwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)xrwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)xrwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ins, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_XRINS);
    DoMethod(xr_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_XRINS);
    DoMethod(xr_type, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_XRTYPE);
  }
  xr_fill();
  set(xrwin, MUIA_Window_Open, TRUE);
}
/* the list for the reference type chosen: the bookmarks, or the headings indented by level */
static void xr_fill(void)
{
  ULONG t = 0;
  u32 i;
  int j;
  if (!xr_list) return;
  get(xr_type, MUIA_Cycle_Active, &t);
  set(xr_list, MUIA_List_Quiet, TRUE);
  DoMethod(xr_list, MUIM_List_Clear);
  if (t == 0) {
    xr_n = bm_collect(xr_idx, BM_MAX);
    for (j = 0; j < xr_n; j++) {
      char lat[64];
      utf8_to_latin1(doc->bmarks[xr_idx[j] - 1], (u32)strlen(doc->bmarks[xr_idx[j] - 1]), lat, sizeof lat);
      DoMethod(xr_list, MUIM_List_InsertSingle, (ULONG)lat, MUIV_List_Insert_Bottom);
    }
  } else {
    xr_n = 0;
    for (i = 0; i < doc->np && xr_n < BM_MAX; i++) {
      const Style *st = &doc->styles[doc->p[i]->pf.style < doc->nstyles ? doc->p[i]->pf.style : 0];
      char line[90], lat[80];
      int lv;
      if (strncmp(st->name, "heading ", 8) && strncmp(st->name, "Heading ", 8)) continue;
      if (!doc->p[i]->len) continue;
      lv = st->name[8] >= '1' && st->name[8] <= '9' ? st->name[8] - '0' : 1;
      utf8_to_latin1(doc->p[i]->t, doc->p[i]->len, lat, sizeof lat);
      sprintf(line, "%*s%s", (lv - 1) * 2, "", lat);
      xr_idx[xr_n++] = (int)i;
      DoMethod(xr_list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
    }
  }
  set(xr_list, MUIA_List_Quiet, FALSE);
}
/* Insert: the cross-reference at the caret */
static void xr_insert(void)
{
  LONG a = -1;
  ULONG t = 0, k = 0;
  struct QXref q;
  get(xr_list, MUIA_List_Active, &a);
  get(xr_type, MUIA_Cycle_Active, &t);
  get(xr_kind, MUIA_Cycle_Active, &k);
  if (a < 0 || a >= xr_n) { DisplayBeep(NULL); return; }
  q.kind = (int)k;
  if (t == 0) { q.name = doc->bmarks[xr_idx[a] - 1]; q.para = -1; }
  else { q.name = NULL; q.para = xr_idx[a]; }
  DoMethod(view, MUIM_Quill_Command, CMD_XREF, (ULONG)&q);
}

/* ---- the Selection Pane: every floating object, page by page, the topmost first */
#define SP_MAX 200
static Object *sp2list;
static Pos sp2at[SP_MAX];               /* each line's object: its reference in the text */
static int sp2n;
static Object *sp2fwd, *sp2back;
/* Layout > Selection Pane: the window made the first time, then filled. It fills itself again
   whenever it is activated, so it follows the document */
static void open_selpane(void)
{
  if (!spwin2) {
    Object *close;
    spwin2 = QWindowObject, MUIA_Window_Title, (ULONG)tr("Selection"), MUIA_Window_ID, MAKE_ID('S','E','L','P'),
      WindowContents, VGroup, QDLG,
        Child, sp2list = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, sp2fwd = QButton(tr("Bring _Forward")), Child, sp2back = QButton(tr("Send _Backward")), End,
        Child, close = QButton(tr("_Close")),
      End, End;
    if (!spwin2) return;
    DoMethod(app, OM_ADDMEMBER, (ULONG)spwin2);
    DoMethod(spwin2, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)spwin2, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)spwin2, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(sp2list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SELPANEPICK);
    DoMethod(sp2fwd, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FORWARD);
    DoMethod(sp2back, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_BACKWARD);
    DoMethod(spwin2, MUIM_Notify, MUIA_Window_Activate, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_SELPANEREF);
  }
  selpane_fill();
  set(spwin2, MUIA_Window_Open, TRUE);
}
/* the list made again from the layout: "Page 2: Oval 3" (or the object's alt text), each page's
   objects topmost first; the selected object's line is made active (without choosing it again) */
static void selpane_fill(void)
{
  static const char *const shapes[SH_N] = { "Rectangle", "Rounded Rectangle", "Oval", "Line", "Arrow", "Triangle", "Diamond", "Right Arrow", "Star",
    "Pentagon", "Hexagon", "Octagon", "Parallelogram", "Trapezoid", "Right Triangle", "Left Arrow", "Up Arrow", "Down Arrow",
    "Plus", "Heart", "Speech Bubble", "Chevron", "Pentagon Arrow", "Four-Point Star" };
  Layout *L;
  u32 *ord, i, j, pg;
  Pos *c;
  LONG active = MUIV_List_Active_Off;
  static ULONG last_sig = 1;
  ULONG sig = 0;
  if (!spwin2 || !sp2list) return;
  L = view_layout();
  /* nothing changed since the list was made: left alone (it is asked again each time the window is
     activated, and that comes just before a click on a line, which must not be lost) */
  if (L) { sig = (ULONG)L->nfloats * 977 + (ULONG)doc->nimages; for (i = 0; i < L->nfloats; i++) sig = sig * 31 + (ULONG)L->floats[i].img * 7 + L->floats[i].page; }
  sig ^= (ULONG)doc;
  if (sig == last_sig && sp2n) return;
  last_sig = sig;
  set(sp2list, MUIA_List_Quiet, TRUE);
  DoMethod(sp2list, MUIM_List_Clear);
  sp2n = 0;
  c = caret_pos();
  if (L && (ord = lay_stack_order(L, L->floats, L->nfloats))) {
    for (pg = 0; pg < L->npages; pg++)
      for (j = L->nfloats; j-- > 0 && sp2n < SP_MAX; ) {
        FloatBox *f = &L->floats[ord[j]];
        const Image *im;
        Para *p;
        u32 o;
        char line[200], name[120];
        if (f->page != pg || f->img < 0 || f->img >= doc->nimages || f->para >= doc->np) continue;
        im = &doc->images[f->img];
        p = doc->p[f->para];
        for (o = 0; o + 4 <= p->len && !(IS_PIC(p->t + o) && pic_index(p->t + o) == f->img); o++) ;
        if (o + 4 > p->len) continue;
        if (im->alt[0]) utf8_to_latin1(im->alt, (u32)strlen(im->alt), name, 60);
        else sprintf(name, "%s %d", tr(im->kind == OBJ_PICTURE ? "Picture" : im->kind == OBJ_TEXTBOX ? "Text Box" : im->kind == OBJ_GROUP ? "Group" :
                                    im->shape < SH_N ? shapes[im->shape] : "Shape"), (int)ord[j] + 1);
        sprintf(line, tr("Page %lu: %s"), (unsigned long)pg + 1, name);
        sp2at[sp2n].para = f->para; sp2at[sp2n].off = o;
        if (c && c->para == f->para && (c->off == o || c->off == o + 4)) active = sp2n;
        sp2n++;
        DoMethod(sp2list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
      }
    free(ord);
  }
  if (!sp2n) DoMethod(sp2list, MUIM_List_InsertSingle, (ULONG)tr("No floating objects"), MUIV_List_Insert_Bottom);
  SetAttrs(sp2list, MUIA_NoNotify, TRUE, MUIA_List_Active, active, TAG_DONE);
  set(sp2list, MUIA_List_Quiet, FALSE);
}
/* a line chosen: its object selected in the document (and shown) */
static void selpane_pick(void)
{
  LONG a = -1;
  Pos sel[2];
  get(sp2list, MUIA_List_Active, &a);
  if (a < 0 || a >= sp2n) return;
  sel[0] = sp2at[a]; sel[1] = sp2at[a]; sel[1].off += 4;
  DoMethod(view, MUIM_Quill_Command, CMD_SELECT, (ULONG)sel);
}

/* the windows made so far, gone (made again, in the language now chosen, when next opened) */
static void drop_windows(void)
{
  Object **w[] = { &findwin, &tablewin, &pgwin, &tabwin, &cmwin, &spwin, &pswin, &wmwin, &lkwin, &gotowin, &dcwin, &lnwin, &mcwin, &spwin2, &bmwin, &xrwin, &pfwin };
  int i;
  for (i = 0; i < (int)(sizeof w / sizeof w[0]); i++)
    if (*w[i]) {
      set(*w[i], MUIA_Window_Open, FALSE);
      DoMethod(app, OM_REMMEMBER, (ULONG)*w[i]);
      MUI_DisposeObject(*w[i]);
      *w[i] = NULL;
    }
  wp_drop();                               /* (WP26) the work packages' windows too */
}
/* the File page: shown over the ribbon, the page and the status bar */
static int back_shown;
/* the File page shown (on: its info and recent list filled in, opened on page) or hidden, back to
   the document with the keyboard on the page */
static void backstage_show(int on, int page)
{
  if (on) {
    static char info[1024];
    struct MUIP_Back_Set bs;
    Layout *L = NULL;
    u32 chars = 0, i;
    int land = doc->page.width > doc->page.height;
    char name[120], loc[300], ttl[130], auth[130];
    const char *paper = "";
    i32 w = land ? doc->page.height : doc->page.width;
    for (i = 0; i < doc->np; i++) {
      u32 k;
      for (k = 0; k < doc->p[i]->len; k++) if (((u8)doc->p[i]->t[k] & 0xC0) != 0x80) chars++;
    }
    get(view, MUIA_Quill_LayoutNow, &L);
    paper = w == 11906 ? "A4" : w == 12240 ? (doc->page.height == 20160 || doc->page.width == 20160 ? "Legal" : "Letter") : w == 8391 ? "A5" : tr("Custom");
    strcpy(name, docpath[0] ? (const char *)FilePart((STRPTR)docpath) : tr("Untitled"));
    strncpy(loc, docpath[0] ? docpath : tr("Not saved yet"), sizeof loc - 1); loc[sizeof loc - 1] = 0;
    if (docpath[0]) *((char *)PathPart((STRPTR)loc)) = 0;
    utf8_to_latin1(doc->title, (u32)strlen(doc->title), ttl, sizeof ttl);
    utf8_to_latin1(doc->author, (u32)strlen(doc->author), auth, sizeof auth);
    sprintf(info, "Name\t%.100s\nLocation\t%.250s\nPages\t%lu%s\nWords\t%lu\nCharacters\t%lu\nParagraphs\t%lu\nPaper\t%s, %s\nTitle\t%.100s\nAuthor\t%.100s",
            name, loc[0] ? loc : tr("(current drawer)"), L ? (unsigned long)L->npages : 0UL, L && !L->complete ? "+" : "",
            (unsigned long)doc_count_words(doc), (unsigned long)chars, (unsigned long)doc->np, paper, tr(land ? "landscape" : "portrait"),
            ttl[0] ? ttl : "-", auth[0] ? auth : "-");
    for (i = 0; i < (u32)nrecent; i++) recentp[i] = recent[i];
    memset(&bs, 0, sizeof bs);
    bs.MethodID = MUIM_Back_Set;
    bs.recent = recentp; bs.nrecent = nrecent; bs.info = info; bs.username = username;
    bs.metric = tb.units >= 0 ? tb.units : doc->page.width != 12240;
    bs.dark = pref_dark; bs.lang = lang_current(); bs.large = pref_large;
    DoMethodA(backstage, (Msg)&bs);
    set(backstage, MUIA_Back_Page, page);
  }
  back_shown = on;
  set(pages, MUIA_Group_ActivePage, on ? 1 : 0);
  if (!on) set(win, MUIA_Window_ActiveObject, (ULONG)view);
}
/* what the File page asks for: shown or left, a new document from a template, a recent file
   opened, or the Options. A new look or language rebuilds every window */
static void backstage_command(ULONG id)
{
  LONG v = 0;
  get(backstage, MUIA_Back_Value, &v);
  switch (id) {
  case ID_FILETAB: backstage_show(1, 0); break;
  case ID_BACKSTAGE: backstage_show(0, 0); break;
  case ID_TEMPLATE:
    backstage_show(0, 0);
    win_new(doc_template((int)v), "");
    break;
  case ID_RECENT:
    backstage_show(0, 0);
    if (v >= 0 && v < nrecent) { char path[256]; strcpy(path, recent[v]); open_path(path); }
    break;
  case ID_OPTIONS: {
    ULONG m = 1, dk = 0, lg = 0, lgt = 0;
    char *n = NULL;
    get(backstage, MUIA_Back_Metric, &m); get(backstage, MUIA_Back_UserName, &n);
    get(backstage, MUIA_Back_Dark, &dk); get(backstage, MUIA_Back_Language, &lg); get(backstage, MUIA_Back_Large, &lgt);
    wp_ui_set((int)m, (int)dk, (int)lg, (int)lgt, n);
    break; }
  }
}
/* the File page's options, also set from the Options window (WP26, wp_options.c): units (1 cm, 0
   inches), the dark interface, the language (LANG_), large text, the user's name (Latin-1; NULL as it
   is). A new look or language makes every window again, so the caller mustn't use its own windows after */
void wp_ui_get(int *metric, int *dark, int *lang, int *large, char *name, size_t cap)
{
  *metric = tb.units >= 0 ? tb.units : doc->page.width != 12240;
  *dark = pref_dark; *lang = lang_current(); *large = pref_large;
  if (cap) { strncpy(name, username, cap - 1); name[cap - 1] = 0; }
}
void wp_ui_set(int metric, int dark, int lang, int large, const char *name)
{
  tb.units = metric;
  win_share_units();
  if (name) { strncpy(username, name, sizeof username - 1); username[sizeof username - 1] = 0; }
  if (dark != pref_dark || lang != lang_current() || large != pref_large) {
    /* a new look or language: the window again, its parts set up with the new pens and words */
    pref_dark = dark;
    if (lang != lang_current()) { pref_lang = lang; lang_set(pref_lang); drop_windows(); }
    pref_large = large;
    win_reopen_all();
  }
  save_prefs();
}

/* ------------------------------------------------------------------ */
/* Several documents at once. Each open document has a window of its own, with its own ribbon,
   rulers, page and status bar. Most of main.c was written when Quill had one window, and works on
   the globals (win, view, doc, docpath, story...). Rather than thread a window pointer through
   every function, those globals always belong to the window being worked in; the other windows
   keep theirs in a DocWin. When the user clicks into another window, MUI tells us
   (MUIA_Window_Activate) and win_switch() swaps the two sets over.

   The dialogs (Find, Font, Paragraph, Styles...) are shared: they act on whichever document is
   current. The spelling window holds positions in one document, so it is closed on a switch. */
#define MAXWIN 8
typedef struct {
  int used;
  Object *win, *view, *ruler, *vruler, *navgrp, *ribbon, *statusbar, *strip, *backstage, *workarea, *pages, *vscroll, *hscroll;
  struct Toolbar tools;                 /* the view was given a pointer to this (tb is #defined to the current one's) */
  Doc *doc;
  char docpath[512], title[600];
  int focus, chrome_bare, back_shown, cur_paper, cur_land, cur_margin, paused_kind;
  u32 paused_page;
  unsigned char story[sizeof story];    /* the header, note or text box being edited in it, if any */
} DocWin;
static DocWin wins[MAXWIN];
static int curwin = -1;                 /* the slot whose state is in the globals */

/* the globals into a window's slot, and back */
static void win_save(DocWin *w)
{
  w->win = win; w->view = view; w->ruler = ruler; w->vruler = vruler; w->navgrp = navgrp; w->ribbon = ribbon;
  w->statusbar = statusbar; w->strip = strip; w->backstage = backstage; w->workarea = workarea; w->pages = pages;
  w->vscroll = vscroll; w->hscroll = hscroll;
  w->doc = doc;
  memcpy(w->docpath, docpath, sizeof docpath); memcpy(w->title, title, sizeof title);
  w->focus = focus; w->chrome_bare = chrome_bare; w->back_shown = back_shown;
  w->cur_paper = cur_paper; w->cur_land = cur_land; w->cur_margin = cur_margin;
  w->paused_kind = paused_kind; w->paused_page = paused_page;
  memcpy(w->story, &story, sizeof story);
}
/* (win_load also points tb at the window's own Toolbar) */
static void win_load(DocWin *w)
{
  win = w->win; view = w->view; ruler = w->ruler; vruler = w->vruler; navgrp = w->navgrp; ribbon = w->ribbon;
  statusbar = w->statusbar; strip = w->strip; backstage = w->backstage; workarea = w->workarea; pages = w->pages;
  vscroll = w->vscroll; hscroll = w->hscroll;
  tbp = &w->tools;
  doc = w->doc;
  memcpy(docpath, w->docpath, sizeof docpath); memcpy(title, w->title, sizeof title);
  focus = w->focus; chrome_bare = w->chrome_bare; back_shown = w->back_shown;
  cur_paper = w->cur_paper; cur_land = w->cur_land; cur_margin = w->cur_margin;
  paused_kind = w->paused_kind; paused_page = w->paused_page;
  memcpy(&story, w->story, sizeof story);
  uibits = w->tools.uibits;
}
/* the ruler and navigation pane shown or not as this window's settings say */
static void ui_apply(void)
{
  set(navgrp, MUIA_ShowMe, !chrome_bare && (uibits & RBIT(RB_NAVPANE)) != 0);
  set(ruler, MUIA_ShowMe, !chrome_bare && (uibits & RBIT(RB_RULER)) != 0);
  set(vruler, MUIA_ShowMe, !chrome_bare && (uibits & RBIT(RB_RULER)) != 0);
}
/* make slot i the current window: its document is what menus, dialogs and ARexx now act on */
static void win_switch(int i)
{
  if (i == curwin || i < 0 || i >= MAXWIN || !wins[i].used) return;
  if (curwin >= 0 && wins[curwin].used) win_save(&wins[curwin]);
  if (spwin) set(spwin, MUIA_Window_Open, FALSE);       /* it points into the other document */
  curwin = i;
  win_load(&wins[i]);
  if (qlog) { fprintf(qlog, "window %d: %s\n", i, docpath[0] ? docpath : "(untitled)"); fflush(qlog); }
  table_menu();
  if (styleswin && xget_open(styleswin)) styles_fill();
}
/* how many document windows are open */
static int win_count(void)
{
  int i, n = 0;
  for (i = 0; i < MAXWIN; i++) n += wins[i].used;
  return n;
}
/* a blank document nobody has touched yet (Open puts a file in its window instead of a new one) */
static int doc_untouched(void)
{
  ULONG changed = 0;
  get(view, MUIA_Quill_Changed, &changed);
  return !changed && !docpath[0] && doc->np == 1 && doc->p[0]->len == 0 && !story.kind;
}
/* a new document window showing d (path: where it came from, "" for a new one), which becomes the
   current one. The first window takes its size from the preferences; later ones sit a little down
   and right of the window they were opened from. Returns 0 (and frees d) if there was no room or
   memory. */
static int win_new(Doc *d, const char *path)
{
  int i, slot = -1, has_box;
  LONG box[4];
  Object *w, *menus_obj;
  if (!d) return 0;
  for (i = 0; i < MAXWIN && slot < 0; i++) if (!wins[i].used) slot = i;
  if (slot < 0) {
    doc_free(d);
    if (curwin >= 0) ask("OK", "Quill can have %s documents open at once.\nClose one to open another.", "8");
    return 0;
  }
  has_box = 0;
  if (curwin >= 0 && win_now(box)) { box[0] += 24; box[1] += 20; has_box = 1; }
  else if (pref_win != WIN_FILL && win_box[2] > 0) { memcpy(box, win_box, sizeof box); has_box = 1; }
  if (curwin >= 0) win_save(&wins[curwin]);
  menus_obj = MUI_MakeObject(MUIO_MenustripNM, (ULONG)menubar, 0);
  {
    /* the settings every window shares (units, author, spelling...) come from the current one; the
       very first window's were set up by main() in its slot */
    struct Toolbar t = curwin >= 0 ? wins[curwin].tools : wins[slot].tools;
    memset(&wins[slot], 0, sizeof wins[slot]);
    wins[slot].tools = t;
  }
  tbp = &wins[slot].tools;
  w = QWindowObject,
    MUIA_Window_Title, (ULONG)"Quill",
    MUIA_Window_ID, slot ? 0 : MAKE_ID('Q','U','I','L'),        /* only the first remembers its place */
    MUIA_Window_DisableKeys, MUIKEYF_WINDOW_CLOSE,       /* Esc belongs to the page (read mode, selections), not to closing */
    MUIA_Window_Menustrip, (ULONG)menus_obj,
    MUIA_Window_Width, has_box ? box[2] : MUIV_Window_Width_Visible(98),
    MUIA_Window_Height, has_box ? box[3] : MUIV_Window_Height_Visible(98),
    (ULONG)(has_box ? MUIA_Window_LeftEdge : TAG_IGNORE), box[0],
    (ULONG)(has_box ? MUIA_Window_TopEdge : TAG_IGNORE), box[1],
    /* two pages: the document (ribbon, page and status bar), and the File page over all of it; a
       page group, so the window keeps its size when one changes to the other */
    WindowContents, pages = VGroup, InnerSpacing(0, 0), MUIA_Group_PageMode, TRUE,
     Child, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
      Child, ribbon = NewObject(ribbon_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
      Child, workarea = HGroup, MUIA_Group_Spacing, 0,
        Child, navgrp = NewObject(nav_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),   /* (" ": the class makes its own bubbles) */
        Child, vruler = NewObject(ruler_class()->mcc_Class, NULL, MUIA_Ruler_Vertical, TRUE, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
        Child, VGroup, MUIA_Group_Spacing, 0, MUIA_HorizWeight, 100,
          Child, ruler = NewObject(ruler_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
          Child, HGroup, MUIA_Group_Spacing, 0,
            Child, view = NewObject(view_class()->mcc_Class, NULL, MUIA_Quill_Fonts, (ULONG)&fonts, MUIA_Quill_Toolbar, (ULONG)tbp,
                                    MUIA_FillArea, FALSE, MUIA_CycleChain, 1, TAG_DONE),
            Child, vscroll = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE),
          End,
          Child, hscroll = NewObject(flatscroll_class()->mcc_Class, NULL, MUIA_FlatScroll_Horiz, TRUE, MUIA_ShowMe, FALSE, TAG_DONE),
        End,
      End,
      Child, statusbar = NewObject(status_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
     End,
     Child, backstage = NewObject(backstage_class()->mcc_Class, NULL, TAG_DONE),
    End,
  End;
  if (!w) {
    if (menus_obj) MUI_DisposeObject(menus_obj);
    doc_free(d);
    if (curwin >= 0) { win_load(&wins[curwin]); ask("OK", "%s", "There isn't enough memory for another window."); }
    return 0;
  }
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  win = w; strip = menus_obj;
  tb.prop = vscroll; tb.hprop = hscroll; tb.ruler = ruler; tb.ribbon = ribbon; tb.vruler = vruler; tb.nav = navgrp; tb.statusbar = statusbar;
  set(navgrp, MUIA_Nav_View, (ULONG)view);
  DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WINCLOSE + slot);
  DoMethod(win, MUIM_Notify, MUIA_Window_Activate, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_WINACT + slot);
  DoMethod(vscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)view, 3, MUIM_Set, MUIA_Quill_Scroll, MUIV_TriggerValue);
  DoMethod(hscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)view, 3, MUIM_Set, MUIA_Quill_ScrollX, MUIV_TriggerValue);
  DoMethod(view, MUIM_Notify, MUIA_Quill_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TITLE);
  DoMethod(view, MUIM_Notify, MUIA_Quill_Outline, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_OUTLINECHG);
  DoMethod(view, MUIM_Notify, MUIA_Quill_InTable, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_INTABLE);
  DoMethod(view, MUIM_Notify, MUIA_Quill_EditObject, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_OBJEDIT);
  DoMethod(view, MUIM_Notify, MUIA_Quill_EditStory, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_STORYREQ);
  DoMethod(view, MUIM_Notify, MUIA_Quill_ViewMode, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_VMODECHG);
  set(view, MUIA_Quill_Grid, pref_grid); set(view, MUIA_Quill_DarkPage, pref_darkpage);
  set(win, MUIA_Window_DefaultObject, (ULONG)view);
  wins[slot].used = 1;
  curwin = slot;
  doc = NULL; memset(&story, 0, sizeof story);
  focus = chrome_bare = back_shown = 0; paused_kind = 0; paused_page = 0;
  strncpy(docpath, path ? path : "", sizeof docpath - 1); docpath[sizeof docpath - 1] = 0;
  show_doc(d);
  ui_apply();
  table_menu();
  set(win, MUIA_Window_Open, TRUE);
  {
    /* the scroller's visible part is the view's height */
    LONG h = _mheight(view);
    set(vscroll, MUIA_Prop_Visible, h > 0 ? h : 400);
  }
  set(win, MUIA_Window_ActiveObject, (ULONG)view);
  win_save(&wins[slot]);
  return 1;
}
/* the current window goes, once its document is saved or given up (0: the user cancelled). The
   last window going ends Quill: the caller sees win_count() == 0 */
static int win_close(void)
{
  int i = curwin;
  Object *w = win;
  if (i < 0) return 1;
  if (doc->edit.kind) story_close(1);
  if (!may_discard()) return 0;
  if (pref_win == WIN_LAST && i == 0 && win_now(win_box)) save_prefs();
  if (back_shown) backstage_show(0, 0);
  if (spwin) set(spwin, MUIA_Window_Open, FALSE);
  set(w, MUIA_Window_Open, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)w);
  MUI_DisposeObject(w);                  /* its menus, view and the rest with it */
  doc_free(doc);
  doc = NULL;
  memset(&wins[i], 0, sizeof wins[i]);
  curwin = -1;
  /* on to another one */
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) {
    win_switch(i);
    set(win, MUIA_Window_Activate, TRUE);
    break;
  }
  return 1;
}
/* (win_close without the answer, for the prototype at the top) */
static void win_close_current(void) { win_close(); }
/* where the current window's title lives while it is shown */
static char *title_store(void) { return curwin >= 0 ? wins[curwin].title : title; }
/* a file open in one of the windows already: that window to the front (1), else 0 */
static int win_find(const char *path)
{
  int i;
  if (curwin >= 0) win_save(&wins[curwin]);
  for (i = 0; i < MAXWIN; i++)
    if (wins[i].used && wins[i].docpath[0] && !Stricmp((STRPTR)wins[i].docpath, (STRPTR)path)) {
      win_switch(i);
      DoMethod(win, MUIM_Window_ToFront);
      set(win, MUIA_Window_Activate, TRUE);
      return 1;
    }
  return 0;
}
/* the measuring units (an option on the File page) for every window, not just this one */
static void win_share_units(void)
{
  int i;
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) wins[i].tools.units = tb.units;
}
/* a new look or text size: every window closed and opened again, so MUI builds them afresh with the
   new pens and fonts */
static void win_reopen_all(void)
{
  int i, cur = curwin;
  if (curwin >= 0) win_save(&wins[curwin]);
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) set(wins[i].win, MUIA_Window_Open, FALSE);
  theme_set_dark(pref_dark); theme_set_large(pref_large);
  for (i = 0; i < MAXWIN; i++) if (wins[i].used && i != cur) set(wins[i].win, MUIA_Window_Open, TRUE);
  if (cur >= 0) set(wins[cur].win, MUIA_Window_Open, TRUE);      /* last, so it ends up in front */
}
/* Amiga-` style cycling: the next document window in front and active */
static void win_next(void)
{
  int i, k;
  for (k = 1; k < MAXWIN; k++) {
    i = (curwin + k) % MAXWIN;
    if (wins[i].used) {
      win_switch(i);
      DoMethod(win, MUIM_Window_ToFront);
      set(win, MUIA_Window_Activate, TRUE);
      return;
    }
  }
}
/* every window closed, each asking about unsaved changes first; 0 if the user cancelled one */
static int win_close_all(void)
{
  int i;
  for (i = 0; i < MAXWIN; i++) if (wins[i].used) {
    win_switch(i);
    set(win, MUIA_Window_Activate, TRUE);
    if (!win_close()) return 0;
  }
  return 1;
}

/* PROGDIR: is where Quill finds its Fonts, TrueType and Dict drawers. Workbench and the Shell set
   it; some other shells start programs without it (or with another drawer's), so then the drawer is
   found from the name Quill was started by and made PROGDIR: here (put back at the end) */
static BPTR home_lock, home_old;
/* the installed fonts being looked at (the startup window's bar from 5 to 8) */
static void scan_step(int done, int total)
{
  static char what[80];
  snprintf(what, sizeof what, tr("Looking for installed fonts (%d of %d)"), done, total);
  splash_step(5 + (total ? 3 * done / total : 0), what);
}
/* PUBSCREEN= in Quill's icon (started from Workbench) */
static void wb_tooltypes(char *screen, int size)
{
  struct Library *IconBase;
  struct WBArg *wa = &_WBenchMsg->sm_ArgList[0];
  struct DiskObject *dob;
  BPTR old;
  STRPTR v;
  if (!(IconBase = OpenLibrary((STRPTR)"icon.library", 37))) return;
  old = CurrentDir(wa->wa_Lock);
  if ((dob = GetDiskObject((STRPTR)wa->wa_Name))) {
    if ((v = FindToolType((STRPTR *)dob->do_ToolTypes, (STRPTR)"PUBSCREEN")) && !screen[0]) { strncpy(screen, (char *)v, size - 1); screen[size - 1] = 0; }
    FreeDiskObject(dob);
  }
  CurrentDir(old);
  CloseLibrary(IconBase);
}
/* whether dir is Quill's drawer (it has Fonts/QuillSans.font) */
static int home_has_fonts(BPTR dir)
{
  BPTR old = CurrentDir(dir), l = Lock((STRPTR)"Fonts/QuillSans.font", ACCESS_READ);
  CurrentDir(old);
  if (l) UnLock(l);
  return l != 0;
}
/* PROGDIR: made right if it isn't (see above): tried from argv[0], the program's name, then the
   current drawer */
static void find_home(const char *argv0)
{
  char name[256];
  const char *tries[2];
  int i;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR win = me->pr_WindowPtr;
  BPTR l;
#ifdef QUILL_TESTHOME
  SetProgramDir(0);                      /* (testing: as some shells start programs) */
#ifdef QUILL_NOHOME
  return;
#endif
#endif
  me->pr_WindowPtr = (APTR)-1;           /* no "please insert volume" requesters while looking */
  l = GetProgramDir() ? Lock((STRPTR)"PROGDIR:Fonts/QuillSans.font", ACCESS_READ) : 0;
  if (l) { UnLock(l); me->pr_WindowPtr = win; return; }
  tries[0] = argv0;
  tries[1] = GetProgramName((STRPTR)name, sizeof name) ? name : NULL;
  for (i = 0; i < 2 && !home_lock; i++) {
    BPTR f, d;
    if (!tries[i] || !tries[i][0] || !(f = Lock((STRPTR)tries[i], ACCESS_READ))) continue;
    d = ParentDir(f);
    UnLock(f);
    if (d && home_has_fonts(d)) home_lock = d;
    else if (d) UnLock(d);
  }
  if (!home_lock) {                      /* started by its name alone, from its own drawer */
    BPTR d = Lock((STRPTR)"", ACCESS_READ);
    if (d && home_has_fonts(d)) home_lock = d;
    else if (d) UnLock(d);
  }
  if (home_lock) home_old = SetProgramDir(home_lock);
  me->pr_WindowPtr = win;
}
/* (PROGDIR: as it was, at the end) */
static void free_home(void)
{
  if (!home_lock) return;
  SetProgramDir(home_old);
  UnLock(home_lock);
  home_lock = 0;
}
/* Quill starts: libraries, arguments (a document, DEBUG=file, PUBSCREEN=, CMD=) or a Workbench icon,
   prefs, language and theme, fonts and the dictionary behind a splash window, then the MUI
   application with one document window, and the input loop until the last window closes */
/* (WP24, wp_info.c) the document windows: slot i's document (NULL: no window there), its path and
   whether it has unsaved changes; the current window's document saved where it is (0 if Save would
   ask for a name: no path, or not a .docx); a document shown in a window of its own (or in the
   untouched blank one), its path given (changed: counted as unsaved); the current window's document
   replaced by d (its path kept); the user's name from the options */
Doc *wp_window_doc(int i, const char **path, int *changed)
{
  ULONG c = 0;
  if (i < 0 || i >= MAXWIN || !wins[i].used) return NULL;
  if (i == curwin) { if (path) *path = docpath; if (changed && view) { get(view, MUIA_Quill_Changed, &c); *changed = c != 0; } return doc; }
  if (path) *path = wins[i].docpath;
  if (changed && wins[i].view) { get(wins[i].view, MUIA_Quill_Changed, &c); *changed = c != 0; }
  return wins[i].doc;
}
int wp_save_current(void)
{
  const DocFormat *f = docpath[0] ? doc_format_for_name(docpath, 1) : NULL;
  if (!f || !(f->flags & DF_NATIVE)) return 0;
  return save_to(docpath);
}
int wp_open_doc(Doc *d, const char *path, int changed)
{
  if (!d) return 0;
  if (doc_untouched()) { strncpy(docpath, path ? path : "", sizeof docpath - 1); docpath[sizeof docpath - 1] = 0; show_doc(d); }
  else if (!win_new(d, path ? path : "")) return 0;
  if (changed) { set(view, MUIA_Quill_Changed, TRUE); set_title(); }
  return 1;
}
void wp_replace_doc(Doc *d, int changed)
{
  if (!d) return;
  story_close(1);
  show_doc(d);
  if (changed) { set(view, MUIA_Quill_Changed, TRUE); set_title(); }
}
const char *wp_username(void) { return username; }
int main(int argc, char **argv)
{
  ULONG sigs = 0;
  int rc = 20, i;
  char start[512] = "", arg_screen[64] = "", missing_screen[64] = "";
  ULONG start_cmd = 0;
  if (!(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39)) ||
      !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39)) ||
      !(LayersBase = OpenLibrary((STRPTR)"layers.library", 39)) ||
      !(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39)) ||
      !(KeymapBase = OpenLibrary((STRPTR)"keymap.library", 37))) goto done;
  if (!(MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, 19))) {
    printf("Quill needs MUI (muimaster.library 19 or later).\n");
    goto done;
  }
  find_home(argc > 0 ? argv[0] : NULL);
  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "DEBUG=", 6)) qlog = fopen(argv[i] + 6, "w");
    else if (!strncmp(argv[i], "CMD=", 4)) start_cmd = (ULONG)atol(argv[i] + 4);       /* (testing: a command once the document is open) */
    else if (!Strnicmp((STRPTR)argv[i], (STRPTR)"PUBSCREEN=", 10)) { strncpy(arg_screen, argv[i] + 10, sizeof arg_screen - 1); arg_screen[sizeof arg_screen - 1] = 0; }
    else if (!start[0]) strncpy(start, argv[i], sizeof start - 1);       /* a document to start with */
  }
  /* or a project icon from Workbench */
  if (argc == 0 && _WBenchMsg && _WBenchMsg->sm_NumArgs > 1) {
    struct WBArg *wa = &_WBenchMsg->sm_ArgList[1];
    if (NameFromLock(wa->wa_Lock, (STRPTR)start, sizeof start)) AddPart((STRPTR)start, wa->wa_Name, sizeof start);
  }
  if (argc == 0 && _WBenchMsg) wb_tooltypes(arg_screen, sizeof arg_screen);
  tbp = &wins[0].tools;                     /* the first window's, set up before it is made */
  memset(&tb, 0, sizeof tb);
  tb.uibits = uibits;
  load_prefs();
  /* the screen: PUBSCREEN= from the Shell or the icon, else the one chosen in Options; the default
     one if it isn't open */
  if (arg_screen[0]) strcpy(pub_screen, arg_screen);
  if (pub_screen[0]) {
    struct Screen *sc = LockPubScreen((STRPTR)pub_screen);
    if (sc) UnlockPubScreen(NULL, sc);
    else { strcpy(missing_screen, pub_screen); pub_screen[0] = 0; }
  }
  lang_set(pref_lang >= 0 ? pref_lang : lang_system());
  theme_set_dark(pref_dark);
  theme_set_large(pref_large);
  /* what Quill is doing, while it starts */
  splash_open(pub_screen, "Quill 0.14");
  splash_step(2, tr("Adding Quill's fonts drawer"));
  DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39);
  add_fonts();
  splash_step(4, tr("Reading the TrueType fonts (for measuring text)"));
  if (!fonts_init(&fonts, "PROGDIR:TrueType")) printf("Quill: no fonts in PROGDIR:TrueType (fonts.list); text will be measured roughly.\n");
  splash_step(5, tr("Looking for installed fonts"));
  sysfonts_scan(&fonts, scan_step);
  splash_step(6, tr("Preparing"));
  if (!view_class() || !ruler_class() || !ribbon_class() || !nav_class() || !status_class() || !flatscroll_class() || !backstage_class()) goto done;
  menubar = wp_menus(menus);                        /* the work packages' items added (wphooks.h) */
  wp_menu_english(menubar);                         /* (WP26: Tell Me and the customizing lists know them) */
  for (i = 0; menubar[i].nm_Type != NM_END; i++)    /* the menus in the language chosen (titles have words of their own) */
    if (menubar[i].nm_Label && menubar[i].nm_Label != NM_BARLABEL)
      menubar[i].nm_Label = (STRPTR)(menubar[i].nm_Type == NM_TITLE ? tr_ctx("menu", (const char *)menubar[i].nm_Label) : tr((const char *)menubar[i].nm_Label));
  load_recent();
  splash_step(8, tr("Loading the spelling dictionary"));
  load_spelling();
  splash_step(58, tr("Loading the hyphenation patterns"));
  hyph_load("PROGDIR:Dict/hyph_en_GB.dic");      /* automatic hyphenation (when a document asks for it) */
  rexx_setup();
  splash_step(62, tr("Building the window"));
  app = ApplicationObject,
    MUIA_Application_Title, (ULONG)"Quill",
    MUIA_Application_Version, (ULONG)verstag,
    MUIA_Application_Description, (ULONG)"A word processor",
    MUIA_Application_Base, (ULONG)"QUILL",
    MUIA_Application_Commands, (ULONG)rxcmds,
  End;
  if (!app) {
    printf("Quill: the application could not be made (MUI classes missing?)\n");
    goto done;
  }
  splash_step(70, tr("Opening the window and loading its fonts"));
  if (!win_new(blank_doc(), "")) {
    printf("Quill: the window could not be made (MUI classes missing?)\n");
    goto done;
  }
  wp_init(); wp_started = 1;             /* the work packages' own setting up (wphooks.h) */
  if (start[0]) {
    static char what[300];
    snprintf(what, sizeof what, tr("Opening %s"), (const char *)FilePart((STRPTR)start));
    splash_front();
    splash_step(78, what);
    open_path(start);
  }
  splash_close();
  if (!theme_fonts_ok()) {
    static char fmsg[640];
    snprintf(fmsg, sizeof fmsg, "%s\n\n%s", tr("Quill couldn't open its own screen fonts, so text is drawn in the\nsystem font and won't look as it should.\n\nKeep the Fonts drawer next to Quill (with QuillSans.font,\nQuillSerif.font and the others in it)."), theme_fonts_why());
    ask("OK", "%s", fmsg);
  }
  if (missing_screen[0]) {
    static char smsg[200];
    snprintf(smsg, sizeof smsg, tr("The public screen \"%s\" isn't open, so Quill opened on the default one."), missing_screen);
    ask("OK", "%s", smsg);
  }
  set(win, MUIA_Window_ActiveObject, (ULONG)view);
  if (start_cmd) command(start_cmd);
  /* the input loop. NewInput lets MUI deal with whatever Intuition sent (redraws, typing in the page,
     gadgets) and returns an ID when a menu item or a ReturnID notify fired; sigs gets the signals to
     Wait() for once there is nothing more to do. A window's activation and close gadget carry its slot
     (ID_WINACT + slot, ID_WINCLOSE + slot: see win_new) */
  for (;;) {
    ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs), a = 0;
    wp_focus_kept = 0;
    if (qlog && id) { fprintf(qlog, "return id %lu\n", (unsigned long)id); fflush(qlog); }
    /* another document window clicked into: its document is the one worked on now */
    if (id >= ID_WINACT && id < ID_WINACT + MAXWIN) { win_switch((int)(id - ID_WINACT)); id = 0; }
    else if (id >= ID_WINCLOSE && id < ID_WINCLOSE + MAXWIN) {
      win_switch((int)(id - ID_WINCLOSE));
      win_close();
      if (!win_count()) break;
      id = 0;
    }
    if (id && doc->edit.kind && story_ends(id)) story_close(1);
    if (id == MUIV_Application_ReturnID_Quit || id == ID_QUIT) { if (win_close_all()) break; }
    else if (id == ID_CLOSE) { win_close(); if (!win_count()) break; }
    else if (id == ID_INTABLE) table_menu();
    else if (id == ID_FILETAB || id == ID_BACKSTAGE || id == ID_TEMPLATE || id == ID_RECENT || id == ID_OPTIONS) backstage_command(id);
    else if (id == ID_FONTDD || id == ID_SIZEDD || id == ID_FONTCOLOR || id == ID_HIGHLIGHT || id == ID_CASE || id == ID_SHADINGDD || id == ID_ACCEPTDD || id == ID_REJECTDD || id == ID_SPACINGDD || id == ID_STYLEPICK || id == ID_SELECTDD ||
             id == ID_MARGINSDD || id == ID_ORIENTDD || id == ID_PAPERDD || id == ID_BREAKSDD || id == ID_INDENTL || id == ID_INDENTR ||
             id == ID_SPACEB || id == ID_SPACEA || id == ID_ZOOMDD || id == ID_SYMBOLDD || id == ID_DELTABLEDD ||
             id == ID_COLUMNSDD || id == ID_LINENUMDD || id == ID_HYPHENDD || id == ID_PAGEALIGNDD || id == ID_DROPCAPDD || id == ID_WATERMARKDD || id == ID_BORDERDD ||
             id == ID_TSTYLEDD || id == ID_PENSTYLE || id == ID_PENWIDTH || id == ID_PENCOLOR || id == ID_TBORDERSDD || id == ID_TSELECTDD ||
             id == ID_AUTOFITDD || id == ID_ROWH || id == ID_COLW || (id >= ID_SHAPESDD && id <= ID_OBJROTDD && id != ID_TEXTBOX) ||
             (id >= ID_PASTESPECDD && id <= ID_PSPACINGDD) || (id >= ID_PASTEDD && id <= ID_FINDDD) || (id >= ID_FORWARDDD && id <= ID_COVERDD) || (id >= ID_THEMEDD && id <= ID_PAGECOLORDD) || id == ID_UNDERLINEDD || wp_is_list(id) ||
             id == ID_C_EFFECTS || id == ID_C_ULCOLOR || id == ID_C_SHADE || id == ID_P_BULLETLIB || id == ID_P_NUMLIB) ribbon_choice(id);   /* (WP9/WP10's lists in Quill's own groups) */
    else if (id == ID_TITLE) set_title();
    else if (id == ID_OUTLINECHG) rebuild_nav(0);
    else if (id) {
      /* a command from the File page: back to the document first */
      if (back_shown && (id == ID_OPEN || id == ID_SAVE || id == ID_SAVEAS || id == ID_PRINT || id == ID_PDF || id == ID_SAVETXT || id == ID_NEW)) backstage_show(0, 0);
      command(id);
    }
    /* the keyboard back to the page after a command, except those that open a window whose gadgets
       should have it (Find, Tabs, Spelling, the table and object windows...) */
    if (id && id != ID_TITLE && id != ID_OUTLINECHG && id != ID_FIND && id != ID_TABLE && id != ID_HEADFOOT && id != ID_PICSIZE && id != ID_NOTE && id != ID_ENDNOTE && id != ID_FINDGO && id != ID_FINDPREV && id != ID_REPLACE1 && id != ID_REPLACEALL && (id < ID_TABS || id > ID_TABPICK) && id != ID_COMMENT && id != ID_SPELL && id != ID_SPPICK && (id < ID_SPCHANGE || id > ID_SPADD) &&
        id != ID_SPLIT && id != ID_TPROPS && (id < ID_OBJLAYOUT || id > ID_OBJEDIT || id == ID_OBJGO) && id != ID_OBJWRAPDD && id != ID_TSORT && id != ID_TOTEXT && id != ID_TOTABLE && id != ID_FORMULA && id != ID_FORMULAGO && id != ID_LINK && !wp_focus_kept) set(win, MUIA_Window_ActiveObject, (ULONG)view);
    /* the scroll bar's knob kept the view's height (it changes when the window is resized) */
    {
      LONG h = _mheight(view);
      ULONG v = 0;
      get(vscroll, MUIA_Prop_Visible, &v);
      if (h > 0 && (LONG)v != h) set(vscroll, MUIA_Prop_Visible, h);
    }
    if (sigs) {
      sigs = Wait(sigs | SIGBREAKF_CTRL_C);
      if (sigs & SIGBREAKF_CTRL_C) break;
    }
  }
  rc = 0;
done:
  /* (Ctrl-C leaves windows open: their documents go with them) */
  if (curwin >= 0 && wins[curwin].used) win_save(&wins[curwin]);
  for (i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].doc) { doc_free(wins[i].doc); wins[i].doc = NULL; }
  if (curwin < 0 && doc) doc_free(doc);
  doc = NULL;
  if (app) MUI_DisposeObject(app);
  if (wp_started) wp_cleanup();
  view_class_free();
  ruler_class_free();
  ribbon_class_free();
  panes_free();
  backstage_class_free();
  qbutton_class_free();
  spell_free(&spell);
  fonts_free(&fonts);
  theme_free_fonts();
  remove_fonts();
  free_home();
  splash_close();
  if (qlog) { fclose(qlog); qlog = NULL; }
  if (DiskfontBase) CloseLibrary(DiskfontBase);
  if (MUIMasterBase) CloseLibrary(MUIMasterBase);
  if (KeymapBase) CloseLibrary(KeymapBase);
  if (UtilityBase) CloseLibrary(UtilityBase);
  if (LayersBase) CloseLibrary(LayersBase);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
  return rc;
}
