/*
 * wp_equation.h - WP14b Equation editor: what its package file (wp_equation.c) and its part of the
 * page view (wp_equation_view.h, included by view.c) share. The view keeps the caret inside the
 * equation being edited and does the editing (core/mathedit.h); the package has the Equation tab,
 * the menus, the galleries and the right-button menu, and sends the view these commands.
 */
#ifndef QUILL_WP_EQUATION_H
#define QUILL_WP_EQUATION_H

#include "../core/mathedit.h"

#define CMD_EV_BASE 0x57140000UL
enum {
  CMD_EV_INFO = CMD_EV_BASE,    /* arg: EvInfo *: filled in */
  CMD_EV_NEW,                   /* a new equation at the caret (the selected text built up into it), the caret in it */
  CMD_EV_INSERT,                /* arg: MNode * (a row, taken): put in at the equation's caret (a selection goes into its
                                   first structure); outside an equation, a new equation holding it */
  CMD_EV_TEXT,                  /* arg: const char * (UTF-8): typed into the equation (or a new one) */
  CMD_EV_APPLY,                 /* arg: MA_ (mathedit.h) << 8 | depth (0xFF: the innermost it applies to) */
  CMD_EV_CONVERT,               /* arg: 0 this one Professional, 1 Linear, 2 all Professional, 3 all Linear */
  CMD_EV_NORMAL,                /* normal text on or off (the selection's, and what is typed next) */
  CMD_EV_DISPLAY,               /* arg: 1 display (a paragraph of its own), 0 inline */
  CMD_EV_JUSTIFY,               /* arg: MPJC_ */
  CMD_EV_NUMBER,                /* an equation number added or taken away */
  CMD_EV_CLOSE,                 /* the caret out of the equation, after it */
  CMD_EV_END
};
#define IS_EV_CMD(c) ((c) >= CMD_EV_BASE && (c) < CMD_EV_END)

/* the equation being edited (or the one selected), for the ribbon's states and menus */
#define EV_MAXACT 48
typedef struct {
  int active;                   /* the caret is in an equation */
  int selected;                 /* an equation is selected (or edited) */
  int display, jc, numbered, linear, normal;
  MAct act[EV_MAXACT]; int nact;   /* the structure commands that apply at the caret */
  const char *label[EV_MAXACT];    /* their names (English, for tr()) */
  FontSet *fs;                  /* the view's fonts (the galleries' pictures) */
  const Doc *doc;               /* the view's document */
} EvInfo;

/* the view's state for the document, if one of its views edits an equation in it (wp_equation_view.h) */
int wp14b_view_info(const Doc *doc, EvInfo *out);
/* the input format and normal text (wp_equation.c): LaTeX instead of UnicodeMath; typing normal text */
extern int wp14b_latex, wp14b_normal;

#endif
