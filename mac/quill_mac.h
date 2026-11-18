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

/* qm_view.c: the page view, a MUI Area subclass (for the files that include MUI) */
#ifdef RT_PREFS_MUI_H
struct MUI_CustomClass *qv_class(void);
void qv_changed(Object *view, int scroll_to_caret);   /* the document or the selection changed: lay out, redraw */
void qv_zoom(Object *view, int percent);
void qv_reset(Object *view);                          /* a new document: back to the top, caches dropped */
int  qv_page_of_caret(Object *view, u32 *pages);      /* for the status line: the caret's page from 1, *pages */

/* the page view's own method: its timer (MUIM_Application_AddInputHandler), ten times a second */
#define MUIM_QV_Tick 0xFECA0001UL
void qv_redraw_all(Object *view);                     /* everything again, by an update event */
#endif

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
