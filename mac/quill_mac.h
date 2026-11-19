/*
 * mac/quill_mac.h - Quill on the classic Mac OS: what the Mac front end's files share.
 * GPL-2.0-only, as Quill.
 *
 * The Mac build is Quill's core (core/, unchanged) with a front end of its own written in MUI,
 * which runs on the Mac through Riptide's MUI for the Mac (riptide src/mac/mui: Application,
 * Window, menus from a NewMenu strip, requesters and Standard File, all following the 1992
 * Human Interface Guidelines). The Amiga front end (amiga/) draws pages with Amiga bitmap fonts,
 * layers and graphics.library; the Mac's page view (qm_view.c) instead asks the core to draw each
 * page exactly as its PDF looks (pdf_page_rgb) and copies it to the window with CopyBits, which
 * fits it to the screen's depth, 1 to 32 bits. docs/mac.md tells the whole story.
 */
#ifndef QUILL_MAC_H
#define QUILL_MAC_H
#include "../core/doc.h"
#include "../core/layout.h"
#include "../core/fonts.h"

/* the document being edited: one window, one document */
typedef struct {
	Doc *doc;
	FontSet *fonts;             /* Quill's fonts (malloc'd: 230 KB the application file need not carry) */
	Layout *lay;
	char path[256];             /* where it was read from or last saved, Mac path; "" untitled */
	char name[64];              /* its name for the window title, Mac Roman */
	Pos caret, anchor;          /* the selection is anchor..caret (equal: none) */
	unsigned long saved_ver;    /* Doc.ver when last saved */
} QDoc;
extern QDoc qd;

/* The commands: the menus' items and the tool strip's (mac/qm_strip.c), one number each. The strip
   never has a command of its own: every item is a menu item's number (Cornerstone, chapter 3) */
enum {
	ID_NEW = 1, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_PDF, ID_ABOUT, ID_QUIT,
	ID_REDO, ID_SELALL,
	ID_PLAIN, ID_BOLD, ID_ITALIC, ID_UNDERLINE, ID_STRIKE, ID_SUPER, ID_SUB,
	ID_GROW, ID_SHRINK,
	ID_LEFT, ID_CENTER, ID_RIGHT, ID_JUSTIFY, ID_SINGLE, ID_ONEHALF, ID_DOUBLE,
	ID_ZOOMFIT, ID_ZOOM50, ID_ZOOM75, ID_ZOOM100, ID_ZOOM150, ID_ZOOM200,
	ID_CLOSEWIN,
	ID_BULLETS, ID_NUMBERS, ID_OUTDENT, ID_INDENT, ID_RIBBON, ID_RULER, ID_REVERT,
	/* Insert (mac/qm_cmds.c) */
	ID_TABLE, ID_PICTURE, ID_FOOTNOTE, ID_ENDNOTE, ID_DATE, ID_PAGENUM,
	/* Layout > Table, in core/edcmd.h's ET_ order */
	ID_ROW_ABOVE, ID_ROW_BELOW, ID_COL_LEFT, ID_COL_RIGHT, ID_DEL_ROW, ID_DEL_COL, ID_DEL_TABLE, ID_MERGE, ID_SPLIT,
	/* Layout */
	ID_PAGESETUP, ID_PORTRAIT, ID_LANDSCAPE, ID_PARAGRAPH,
	ID_BREAK0 = 120,            /* + Insert > Breaks' row (NBREAKS): page, column, section next page, continuous */
	ID_SYM0 = 130,              /* + Insert > Symbol's row */
	ID_COLS0 = 160,             /* + Layout > Columns' row: one, two, three */
	ID_LINENUM0 = 165,          /* + Layout > Line Numbers' row: LN_ */
	ID_SIZE0 = 200,             /* + the Size menu's row */
	ID_FONT0 = 220,             /* + the Font menu's family (24) */
	ID_STYLE0 = 250,            /* + the Paragraph Style submenu's row */
	ID_STRIP0 = 270,            /* + View > Tool Strip's row: the strip shown (QS_) */
	ID_LAST = 400
};
#define NBREAKS 4
/* the tool strips, one per tab of the Amiga's ribbon that the Mac has commands for */
enum { QS_HOME, QS_INSERT, QS_LAYOUT, QS_REFERENCES, QS_REVIEW, QS_NSTRIPS };

/* qm_main.c: the one answer to "can it be chosen now, is it on" that the menus' check marks and
   dimming and the strip's items all take, so they never disagree */
#define QS_ENABLED 1
#define QS_CHECKED 2
unsigned qm_state(unsigned long id);
void qm_command(unsigned long id);                     /* a menu item's command, from the strip */
#ifdef RT_PREFS_MUI_H
Object *qm_view(void);                                 /* the page view */
#endif
/* the strip's pop-up menus show the Paragraph Style, Font and Size menus' items: their first command
   and how many, and what the box shows now (the caret's style, font or size, even one not listed) */
enum { QP_STYLE, QP_FONT, QP_SIZE, QP_STRIP, QP_BREAKS, QP_SYMBOL, QP_COLUMNS, QP_LINENUM };
int  qm_popup(int which, unsigned long *first, char *now, int cap);
const char *qm_label(unsigned long id);                /* a menu item's words as the menu shows them (Mac Roman) */

/* alerts at their level (Guidelines pp. 193-196): a note informs, a caution warns before something
   that can't be undone, a stop says it can't be done. fmt has at most one %s, arg (Mac Roman) */
enum { QA_STOP, QA_NOTE, QA_CAUTION };
void qm_alert(int level, const char *fmt, const char *arg);
int  qm_get_file(char *path, int cap);                 /* Standard File's Open dialog: a full Mac path, 0 if cancelled */
int  mac_alert(int icon, const char *text, const char *const *btn, int n, int def, int cancel);   /* riptide mac_alert.c */

/* qm_cmds.c: the Insert, Layout, References and Review menus' commands */
unsigned qc_state(unsigned long id, int *mine);        /* qm_state for its commands (*mine: it is one of them) */
int  qc_command(unsigned long id);                     /* 1 if it was one of its commands */
int  qc_popup(int which, unsigned long *first);        /* a strip pop-up button's submenu: its first command, how many */
const char *qc_label(unsigned long id);                /* a submenu item's words, NULL if not its */
void qc_menus(void (*add)(int type, const char *label, const char *key, int flags, unsigned long id));

/* qm_view.c: the page view, a MUI Area subclass (for the files that include MUI) */
#ifdef RT_PREFS_MUI_H
struct MUI_CustomClass *qv_class(void);
void qv_changed(Object *view, int scroll_to_caret);   /* the document or the selection changed: lay out, redraw */
void qv_zoom(Object *view, int percent);
void qv_reset(Object *view);                          /* a new document: back to the top, caches dropped */

/* the page view's own method: its timer (MUIM_Application_AddInputHandler), ten times a second */
#define MUIM_QV_Tick 0xFECA0001UL
void qv_redraw_all(Object *view);                     /* everything again, by an update event */
void qv_chrome(Object *view, int ribbon, int ruler);   /* the tool strip and the rulers shown or not */
int  qm_port_depth(void *port);                       /* the bits per pixel a window is drawn in */
void qv_strip_changed(Object *view);                  /* the commands' states changed: the strip follows */
void qv_strip_switched(Object *view);                 /* another strip chosen: drawn afresh */
#endif

/* qm_strip.c: the tool strip (View > Ribbon), drawn and clicked inside the page view's box */
#define QS_HEIGHT 24                                   /* its height with the line under it */
void qs_layout(short left, short top, short right);    /* its items placed in that width */
void qs_draw(void *port, const void *area);            /* the items in area (a Rect) */
void qs_update(void *port);                            /* the items whose state changed drawn again */
int  qs_click(void *port, int x, int y);               /* a click on it: tracked, the command run; 1 if taken */
void qs_select(int strip);                             /* the strip shown (QS_): laid out again when next drawn */

/* qm_ruler.c: the horizontal and vertical rulers (View > Ruler) and the tab kind box between them */
#define QR_HEIGHT 20                                   /* the horizontal ruler, with its line */
#define QR_WIDTH  17                                   /* the vertical one */
#ifdef QUILL_RULER_H
/* r: the ruler's box; pagex (pagey): where the page's left (top) edge is in the window; dpi */
void qr_draw_h(void *port, const void *r, const RulerGeom *g, long pagex, int dpi);
void qr_draw_v(void *port, const void *r, const RulerGeom *g, long pagey, int dpi);
/* a press at x, y on the horizontal ruler: the marker under it dragged until the button goes up,
   the ruler drawn as it goes and a dotted line over the pages (guide: their box) showing where; the
   part dragged (RP_), g as it ended, or RP_NONE */
int  qr_track_h(void *port, const void *r, const void *guide, RulerGeom *g, long pagex, int dpi, int x, int y);
#endif
void qr_draw_corner(void *port, const void *r);        /* the box choosing the kind of tab stop a click makes */
void qr_click_corner(void *port, const void *r);       /* ... the next kind */

/* qm_main.c */
void qm_status(void);                                  /* the menus' check marks after the caret moved */
const CharFmt *qm_typing_fmt(void);                    /* what typing continues with (NULL: the text's) */
void qm_typing_reset(void);                            /* the caret moved: typing takes the text's format again */
void set_watch_cursor(int on);                         /* the watch while busy, else the arrow */

/* qm_text.c: Mac Roman, Latin-1 and UTF-8 */
int  macroman_to_utf8(unsigned char c, char *out);    /* bytes written (1-3) */
void utf8_to_macroman(const char *s, char *out, int cap);
void latin1_to_macroman(const char *s, char *out, int cap);

/* qm_lang.c: Quill's interface words (amiga/lang_table.h), Latin-1 */
const char *tr(const char *english);
void lang_set(int lang);
int  lang_mac_system(void);                           /* the language the Mac's System speaks, if Quill has it */
const char *tr_ctx(const char *ctx, const char *english);
int  lang_current(void);
#endif
