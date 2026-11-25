/*
 * mac/qm_ribbon.h - between Octavo's own files and mac/qm_ribbon.c, which runs the Amiga's ribbon,
 * status bar and File page (amiga/ribbon.c, panes.c, backstage.c, compiled for the Mac) in the
 * experimental ribbon tree. Only core types here: the two sides number their commands differently
 * (mac/qm_ids.h, amiga/ids.h). GPL-2.0-only, as Quill.
 */
#ifndef QM_RIBBON_H
#define QM_RIBBON_H
#include "../core/doc.h"
#include "../core/fonts.h"

/* the Amiga files' ReturnIDs reach Octavo's loop as QM_AMIGA_ID + their ids.h number (mac/qm_ashim.c) */
#define QM_AMIGA_ID 0x10000UL

#ifdef RT_PREFS_MUI_H
/* the window's contents: the ribbon over the page view and the status bar, and the File page
   (a page group: one or the other); NULL if the classes couldn't be made */
Object *qr_make(Object *view);
#endif
int  qr_command(unsigned long id);      /* a command from them (QM_AMIGA_ID + id): 1 if taken */
void qr_update(void);                   /* the document, caret or a state changed: the ribbon and status bar follow */
int  qr_can(unsigned long amiga_id);    /* whether the Mac has the Amiga command yet (ribbon.c dims it if not) */
void qr_backstage(int on);              /* the File page shown or the document again */

/* qm_main.c, for qm_ribbon.c */
Doc *qm_doc(void);
FontSet *qm_fonts(void);
void qm_sel(Pos *caret, Pos *anchor);
const CharFmt *qm_fmt_here(void);       /* the format at the caret (or what typing goes on with) */
void qm_char_format(unsigned mask, const CharFmt *v);   /* the selection, or what is typed next */
void qm_para_format(unsigned mask, const ParaFmt *pf);  /* the selected paragraphs */
void qm_apply_style(int style);         /* Doc.styles index: the selected paragraphs */
void qm_edit(int cmd);                  /* the Edit menu's: 0 Undo, 2 Cut, 3 Copy, 4 Paste, 5 Clear */
void qm_page_info(unsigned long *page, unsigned long *pages, long *zoom);   /* the page at the top, of how many; the zoom */
#endif
