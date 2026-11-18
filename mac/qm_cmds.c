/*
 * mac/qm_cmds.c - Quill on the classic Mac OS: the commands of the Insert, Layout, References and
 * Review menus and their tool strips. GPL-2.0-only, as Quill.
 *
 * qm_main.c is the application: the menu bar, files, the Edit menu and the text's formatting. The
 * menus that follow the Amiga's ribbon tabs (Insert, Layout, References, Review) are here: what each
 * command can do now (qc_state, part of qm_state's one answer), what it does (qc_command), and the
 * dialogs it asks with (mac/qm_dialog.c). The document work itself is the core's (core/edcmd.c and
 * the core's tables, notes, fields...), shared with the Amiga's view and tested natively; this file
 * turns the Mac's selection into it and back, and redraws.
 *
 * A command that can't be done here (a table command outside a table) is dimmed in its menu and on
 * its strip; one that fails anyway beeps (Guidelines p. 26: SysBeep for a command that can't run).
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mui/mm.h"
#include "quill_mac.h"
#include "qm_mac.h"
#include "qm_dialog.h"
#include "../core/edcmd.h"

/* the Symbol submenu: characters Mac Roman has (the menu shows them), and each one's UTF-8 */
static const struct { const char *mac, *utf8; } symbols[] = {
	{ "\xA9", "\xC2\xA9" }, { "\xA8", "\xC2\xAE" }, { "\xAA", "\xE2\x84\xA2" },      /* copyright, registered, trade mark */
	{ "\xA4", "\xC2\xA7" }, { "\xA6", "\xC2\xB6" }, { "\xA0", "\xE2\x80\xA0" },      /* section, pilcrow, dagger */
	{ "\xA5", "\xE2\x80\xA2" }, { "\xD0", "\xE2\x80\x93" }, { "\xD1", "\xE2\x80\x94" },   /* bullet, en and em dash */
	{ "\xC9", "\xE2\x80\xA6" }, { "\xA1", "\xC2\xB0" }, { "\xB1", "\xC2\xB1" },      /* ellipsis, degree, plus-minus */
	{ "\xD6", "\xC3\xB7" }, { "\xAD", "\xE2\x89\xA0" }, { "\xB2", "\xE2\x89\xA4" },  /* divide, not equal, less or equal */
	{ "\xB3", "\xE2\x89\xA5" }, { "\xB0", "\xE2\x88\x9E" }, { "\xB5", "\xC2\xB5" },  /* greater or equal, infinity, micro */
	{ "\xB9", "\xCF\x80" }, { "\xBD", "\xCE\xA9" }, { "\xA3", "\xC2\xA3" }, { "\xB4", "\xC2\xA5" },   /* pi, Omega, pound, yen */
};
#define NSYMBOLS ((int)(sizeof symbols / sizeof symbols[0]))

/* Insert > Breaks, in the submenu's order */
static const char *const break_names[NBREAKS] = { "Page Break", "Column Break", "Section Break (Next Page)", "Section Break (Continuous)" };

/* ---- the selection, to the core and back ---- */

static ESel sel_now(void)
{
	ESel s;
	s.anchor = qd.anchor;
	s.caret = qd.caret;
	return s;
}

/* after a command: the Mac's selection from the core's, the page drawn again (ok), or a beep */
static void done(const ESel *s, int ok)
{
	if (!ok) {
		SysBeep(1);
		return;
	}
	qd.anchor = pos_clamp(qd.doc, s->anchor);
	qd.caret = pos_clamp(qd.doc, s->caret);
	qm_typing_reset();
	qv_changed(qm_view(), 1);
}

static int in_table_now(void)
{
	return qd.doc && in_table(&qd.doc->p[qd.caret.para]->pf);
}

/* Mac Roman text from a dialog, as UTF-8 (out: room for three bytes a character) */
static void to_utf8(const char *mac, char *out, int cap)
{
	int n = 0;
	for (; *mac && n + 4 < cap; mac++)
		n += macroman_to_utf8((unsigned char)*mac, out + n);
	out[n] = 0;
}

/* ---- dialogs ---- */

/* Insert > Table...: how many columns and rows (Word's 3 by 2 to start with) */
static int table_dialog(int *cols, int *rows)
{
	char c[8], r[8];
	DItem it[] = {
		{ DI_TEXT, 0, 0, 13, 13, 234, "Insert Table" },
		{ DI_TEXT, 0, 0, 13, 45, 110, "Columns:" },
		{ DI_EDIT, 0, 0, 130, 45, 40, NULL, c, sizeof c },
		{ DI_TEXT, 0, 0, 13, 73, 110, "Rows:" },
		{ DI_EDIT, 0, 0, 130, 73, 40, NULL, r, sizeof r },
		{ DI_BUTTON, 0, 0, 177, 106, 70, "Insert" },
		{ DI_BUTTON, 0, 0, 95, 106, 70, "_Cancel" },
	};
	snprintf(c, sizeof c, "%d", *cols);
	snprintf(r, sizeof r, "%d", *rows);
	for (;;) {
		if (qdlg_run(260, 139, it, 7, 5, 6) != 5)
			return 0;
		if (qdlg_number(c, 1, MAX_COLS, cols) && qdlg_number(r, 1, 500, rows))
			return 1;
		qm_alert(QA_STOP, "Columns are 1 to 64 and rows 1 to 500.", NULL);
	}
}

/* a footnote's, endnote's or comment's text (prompt: what the dialog asks); 0 if cancelled */
static int text_dialog(const char *prompt, const char *button, char *mac, int cap)
{
	DItem it[] = {
		{ DI_TEXT, 0, 0, 13, 13, 334, prompt },
		{ DI_EDIT, 0, 0, 16, 40, 328, NULL, mac, cap },
		{ DI_BUTTON, 0, 0, 277, 75, 70, button },
		{ DI_BUTTON, 0, 0, 195, 75, 70, "_Cancel" },
	};
	mac[0] = 0;
	return qdlg_run(360, 108, it, 4, 2, 3) == 2;
}

/* Insert > Picture...: Standard File, then the core reads it (PNG or JPEG) */
static void insert_picture(void)
{
	char path[300];
	FILE *f;
	long n;
	u8 *data = NULL;
	ESel s = sel_now();
	if (!qm_get_file(path, sizeof path))
		return;
	set_watch_cursor(1);
	if ((f = fopen(path, "rb"))) {
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n > 0 && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) != (size_t)n) {
			free(data);
			data = NULL;
		}
		fclose(f);
	}
	set_watch_cursor(0);
	if (!data || ec_picture(qd.doc, &s, data, (u32)n) < 0) {
		/* (ec_picture frees what it can't use) */
		qm_alert(QA_STOP, "%s\nis not a PNG or JPEG picture Quill can read.", path);
		return;
	}
	done(&s, 1);
}

static void insert_note(int end)
{
	char mac[256], u[768];
	ESel s = sel_now();
	if (!text_dialog(end ? "Endnote text:" : "Footnote text:", "Insert", mac, sizeof mac))
		return;
	to_utf8(mac, u, sizeof u);
	done(&s, ec_note(qd.doc, &s, end, u) >= 0);
}

/* ---- the commands ---- */

unsigned qc_state(unsigned long id, int *mine)
{
	*mine = 1;
	if (id >= ID_SYM0 && id < ID_SYM0 + NSYMBOLS)
		return QS_ENABLED;
	if (id >= ID_BREAK0 && id < ID_BREAK0 + NBREAKS)
		return id >= ID_BREAK0 + 2 && in_table_now() ? 0 : QS_ENABLED;     /* (no section break in a table) */
	switch (id) {
	case ID_TABLE: case ID_PICTURE: case ID_FOOTNOTE: case ID_ENDNOTE: case ID_DATE: case ID_PAGENUM:
		return QS_ENABLED;
	case ID_ROW_ABOVE: case ID_ROW_BELOW: case ID_COL_LEFT: case ID_COL_RIGHT:
	case ID_DEL_ROW: case ID_DEL_COL: case ID_DEL_TABLE: case ID_MERGE: case ID_SPLIT:
		return in_table_now() ? QS_ENABLED : 0;
	}
	*mine = 0;
	return 0;
}

int qc_command(unsigned long id)
{
	ESel s = sel_now();
	if (id >= ID_SYM0 && id < ID_SYM0 + NSYMBOLS) {
		done(&s, ec_text(qd.doc, &s, symbols[id - ID_SYM0].utf8));
		return 1;
	}
	if (id >= ID_BREAK0 && id < ID_BREAK0 + NBREAKS) {
		int k = (int)(id - ID_BREAK0);
		done(&s, k < 2 ? ec_break(qd.doc, &s, k == 1) : ec_section(qd.doc, &s, k == 2 ? SB_NEXT : SB_CONT));
		return 1;
	}
	if (id >= ID_ROW_ABOVE && id <= ID_SPLIT) {
		done(&s, ec_table_op(qd.doc, &s, ET_ROW_ABOVE + (int)(id - ID_ROW_ABOVE)));
		return 1;
	}
	switch (id) {
	case ID_TABLE: {
		static int cols = 3, rows = 2;
		if (table_dialog(&cols, &rows))
			done(&s, ec_table(qd.doc, &s, rows, cols));
		return 1;
	}
	case ID_PICTURE: insert_picture(); return 1;
	case ID_FOOTNOTE: case ID_ENDNOTE: insert_note(id == ID_ENDNOTE); return 1;
	case ID_DATE: done(&s, ec_field(qd.doc, &s, "DATE \\@ \"d MMMM yyyy\"")); return 1;
	case ID_PAGENUM: done(&s, ec_field(qd.doc, &s, "PAGE")); return 1;
	}
	return 0;
}

int qc_popup(int which, unsigned long *first)
{
	switch (which) {
	case QP_BREAKS: *first = ID_BREAK0; return NBREAKS;
	case QP_SYMBOL: *first = ID_SYM0; return NSYMBOLS;
	}
	return 0;
}

const char *qc_label(unsigned long id)
{
	if (id >= ID_SYM0 && id < ID_SYM0 + NSYMBOLS)
		return symbols[id - ID_SYM0].mac;
	if (id >= ID_BREAK0 && id < ID_BREAK0 + NBREAKS)
		return tr(break_names[id - ID_BREAK0]);
	return NULL;
}

void qc_menus(void (*add)(int type, const char *label, const char *key, int flags, unsigned long id))
{
	int i;
	/* Insert: the Amiga's Insert tab, as far as the Mac has its commands */
	add(NM_TITLE, "Insert", NULL, 0, 0);
	add(NM_ITEM, "Table...", NULL, 0, ID_TABLE);
	add(NM_ITEM, "Picture...", NULL, 0, ID_PICTURE);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Breaks", NULL, 0, 0);
	for (i = 0; i < NBREAKS; i++)
		add(NM_SUB, break_names[i], NULL, 0, ID_BREAK0 + (unsigned long)i);
	add(NM_ITEM, "Footnote...", NULL, 0, ID_FOOTNOTE);
	add(NM_ITEM, "Endnote...", NULL, 0, ID_ENDNOTE);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Date", NULL, 0, ID_DATE);
	add(NM_ITEM, "Page Number", NULL, 0, ID_PAGENUM);
	add(NM_ITEM, "Symbol", NULL, 0, 0);
	for (i = 0; i < NSYMBOLS; i++)
		add(NM_SUB, symbols[i].mac, NULL, 0, ID_SYM0 + (unsigned long)i);
	/* Layout: the page and the table the caret is in (the Amiga's Layout and Table Layout tabs) */
	add(NM_TITLE, "Layout", NULL, 0, 0);
	add(NM_ITEM, "Table", NULL, 0, 0);
	add(NM_SUB, "Insert Rows Above", NULL, 0, ID_ROW_ABOVE);
	add(NM_SUB, "Insert Rows Below", NULL, 0, ID_ROW_BELOW);
	add(NM_SUB, "Insert Columns to the Left", NULL, 0, ID_COL_LEFT);
	add(NM_SUB, "Insert Columns to the Right", NULL, 0, ID_COL_RIGHT);
	add(NM_SUB, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_SUB, "Delete Rows", NULL, 0, ID_DEL_ROW);
	add(NM_SUB, "Delete Columns", NULL, 0, ID_DEL_COL);
	add(NM_SUB, "Delete Table", NULL, 0, ID_DEL_TABLE);
	add(NM_SUB, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_SUB, "Merge Cells", NULL, 0, ID_MERGE);
	add(NM_SUB, "Split Cells", NULL, 0, ID_SPLIT);
}
