/*
 * mac/qm_dialog.h - Quill on the classic Mac OS: modal dialogs built at run time (mac/qm_dialog.c).
 * GPL-2.0-only, as Quill.
 *
 * A dialog is an array of items, each placed by the caller in the dialog's coordinates. Push buttons
 * are 20 high, text fields and lines of text 16; the caller leaves 13 pixels round the edges and puts
 * the action button at the bottom right with Cancel to its left (Guidelines pp. 196, 204-205).
 */
#ifndef QM_DIALOG_H
#define QM_DIALOG_H

enum { DI_BUTTON, DI_TEXT, DI_EDIT, DI_CHECK, DI_RADIO };
typedef struct {
	unsigned char kind;         /* DI_ */
	unsigned char lines;        /* DI_TEXT: lines it takes (0 or 1: one) */
	unsigned char off;          /* DI_BUTTON: dimmed */
	short x, y, w;              /* its top left and width */
	const char *label;          /* the words (English, through tr()) of a button, text, check box or radio button */
	char *text;                 /* DI_EDIT: its text, Mac Roman, in and out (out unless cancelled) */
	int cap;                    /*   and the room there is for it */
	int *value;                 /* DI_CHECK: 0 or 1; DI_RADIO: the group's choice, shared by its buttons */
	int ord;                    /* DI_RADIO: the value this button stands for */
	unsigned char raw;          /* the label is data (a name, a word: Mac Roman), shown as it is */
} DItem;

/* A modal dialog box w x h of n items run until a button is clicked (or Return, Enter, Escape or
   Command-period press def or cancel, indexes of buttons; def -1: no default button). Returns the
   index of the button that closed it. */
int qdlg_run(int w, int h, DItem *it, int n, int def, int cancel);
/* The windows a dialog or alert uncovered drawn now, before the command it asked for changes or
   scrolls the page (ScrollRect would move what is not drawn yet, and the update go to the old place) */
void qdlg_settle(void);
/* s as a whole number from lo to hi into *out: 0 if it isn't one */
int qdlg_number(const char *s, int lo, int hi, int *out);
#endif
