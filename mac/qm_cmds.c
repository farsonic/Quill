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
#include "../core/spell.h"

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

/* Layout > Columns and Line Numbers, in the submenus' order (Line Numbers: LN_ order) */
static const char *const column_names[3] = { "One", "Two", "Three" };
static const char *const linenum_names[4] = { "None", "Restart Each Page", "Restart Each Section", "Continuous" };
/* Page Setup's papers: US Letter, A4, US Legal, A5 (portrait, twips) */
static const i32 papers[4][2] = { { 12240, 15840 }, { 11906, 16838 }, { 12240, 20160 }, { 8391, 11906 } };

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

/* the unit people measure in here: centimetres, or inches on a US Letter page (as the ruler) */
static int unit_now(void)
{
	return qd.doc->page.width != 12240 && qd.doc->page.height != 12240 ? EU_CM : EU_INCH;
}

/* Layout > Page Setup...: the paper, its orientation and the margins, for the whole document */
static void page_setup(void)
{
	PageSetup *pg = &qd.doc->page;
	int unit = unit_now(), paper = -1, land = pg->width > pg->height, k, ok;
	i32 w = land ? pg->height : pg->width, h = land ? pg->width : pg->height, m[4];
	char e[4][16];
	DItem it[] = {
		{ DI_TEXT, 0, 0, 13, 13, 274, "Page Setup" },
		{ DI_TEXT, 0, 0, 13, 41, 90, "Paper:" },
		{ DI_RADIO, 0, 0, 110, 41, 170, "US Letter", NULL, 0, &paper, 0 },
		{ DI_RADIO, 0, 0, 110, 59, 170, "A4", NULL, 0, &paper, 1 },
		{ DI_RADIO, 0, 0, 110, 77, 170, "US Legal", NULL, 0, &paper, 2 },
		{ DI_RADIO, 0, 0, 110, 95, 170, "A5", NULL, 0, &paper, 3 },
		{ DI_TEXT, 0, 0, 13, 123, 90, "Orientation:" },
		{ DI_RADIO, 0, 0, 110, 123, 170, "Portrait", NULL, 0, &land, 0 },
		{ DI_RADIO, 0, 0, 110, 141, 170, "Landscape", NULL, 0, &land, 1 },
		{ DI_TEXT, 0, 0, 13, 169, 274, unit == EU_CM ? "Margins (cm):" : "Margins (inches):" },
		{ DI_TEXT, 0, 0, 13, 195, 64, "Top:" },
		{ DI_EDIT, 0, 0, 80, 195, 50, NULL, e[0], 16 },
		{ DI_TEXT, 0, 0, 150, 195, 64, "Bottom:" },
		{ DI_EDIT, 0, 0, 220, 195, 50, NULL, e[1], 16 },
		{ DI_TEXT, 0, 0, 13, 223, 64, "Left:" },
		{ DI_EDIT, 0, 0, 80, 223, 50, NULL, e[2], 16 },
		{ DI_TEXT, 0, 0, 150, 223, 64, "Right:" },
		{ DI_EDIT, 0, 0, 220, 223, 50, NULL, e[3], 16 },
		{ DI_BUTTON, 0, 0, 217, 258, 70, "OK" },
		{ DI_BUTTON, 0, 0, 135, 258, 70, "_Cancel" },
	};
	for (k = 0; k < 4; k++)
		if (papers[k][0] == w && papers[k][1] == h)
			paper = k;
	ec_measure_text(pg->top, unit, e[0], 16);
	ec_measure_text(pg->bottom, unit, e[1], 16);
	ec_measure_text(pg->left, unit, e[2], 16);
	ec_measure_text(pg->right, unit, e[3], 16);
	for (;;) {
		if (qdlg_run(300, 291, it, 20, 18, 19) != 18)
			return;
		for (k = 0, ok = 1; k < 4; k++)
			ok &= ec_measure(e[k], unit, &m[k]) && m[k] >= 0;
		if (paper >= 0) {
			w = papers[paper][0];
			h = papers[paper][1];
		}
		if (ok && (ec_page_setup(qd.doc, land ? h : w, land ? w : h, m[0], m[1], m[2], m[3]) ||
		           (pg->width == (land ? h : w) && pg->top == m[0] && pg->bottom == m[1] && pg->left == m[2] && pg->right == m[3])))
			break;
		qm_alert(QA_STOP, "The margins must be numbers, and leave room for text on the page.", NULL);
	}
	qv_changed(qm_view(), 1);
}

/* Layout > Orientation: the page turned, its margins kept */
static void orientation(int land)
{
	PageSetup *pg = &qd.doc->page;
	if ((pg->width > pg->height) == land)
		return;
	if (ec_page_setup(qd.doc, pg->height, pg->width, pg->top, pg->bottom, pg->left, pg->right))
		qv_changed(qm_view(), 1);
}

/* Layout > Paragraph...: the selected paragraphs' indents, spacing and line spacing */
static void paragraph(void)
{
	const ParaFmt *now = &qd.doc->p[qd.caret.para]->pf;
	int unit = unit_now(), line = now->linerule == LS_AUTO ? (now->line == 360 ? 1 : now->line == 480 ? 2 : now->line == 240 ? 0 : -1) : -1, k, ok;
	unsigned mask = PM_INDENT | PM_SPACING;
	char e[5][16];
	i32 v[5];
	ParaFmt pf;
	ESel s = sel_now();
	DItem it[] = {
		{ DI_TEXT, 0, 0, 13, 13, 314, "Paragraph" },
		{ DI_TEXT, 0, 0, 13, 41, 314, unit == EU_CM ? "Indentation (cm):" : "Indentation (inches):" },
		{ DI_TEXT, 0, 0, 13, 67, 64, "Left:" },
		{ DI_EDIT, 0, 0, 80, 67, 50, NULL, e[0], 16 },
		{ DI_TEXT, 0, 0, 160, 67, 90, "Right:" },
		{ DI_EDIT, 0, 0, 257, 67, 50, NULL, e[1], 16 },
		{ DI_TEXT, 0, 0, 160, 95, 90, "First line:" },
		{ DI_EDIT, 0, 0, 257, 95, 50, NULL, e[2], 16 },
		{ DI_TEXT, 0, 0, 13, 123, 314, "Spacing (points):" },
		{ DI_TEXT, 0, 0, 13, 149, 64, "Before:" },
		{ DI_EDIT, 0, 0, 80, 149, 50, NULL, e[3], 16 },
		{ DI_TEXT, 0, 0, 160, 149, 90, "After:" },
		{ DI_EDIT, 0, 0, 257, 149, 50, NULL, e[4], 16 },
		{ DI_TEXT, 0, 0, 13, 177, 110, "Line spacing:" },
		{ DI_RADIO, 0, 0, 130, 177, 190, "Single", NULL, 0, &line, 0 },
		{ DI_RADIO, 0, 0, 130, 195, 190, "1.5 Lines", NULL, 0, &line, 1 },
		{ DI_RADIO, 0, 0, 130, 213, 190, "Double", NULL, 0, &line, 2 },
		{ DI_BUTTON, 0, 0, 237, 246, 70, "OK" },
		{ DI_BUTTON, 0, 0, 155, 246, 70, "_Cancel" },
	};
	ec_measure_text(now->left, unit, e[0], 16);
	ec_measure_text(now->right, unit, e[1], 16);
	ec_measure_text(now->first, unit, e[2], 16);
	ec_measure_text(now->before, EU_PT, e[3], 16);
	ec_measure_text(now->after, EU_PT, e[4], 16);
	for (;;) {
		if (qdlg_run(320, 279, it, 19, 17, 18) != 17)
			return;
		for (k = 0, ok = 1; k < 5; k++)
			ok &= ec_measure(e[k], k < 3 ? unit : EU_PT, &v[k]);
		if (ok && v[0] >= 0 && v[3] >= 0 && v[4] >= 0)
			break;
		qm_alert(QA_STOP, "Indents and spacing must be numbers (spacing not below 0).", NULL);
	}
	memset(&pf, 0, sizeof pf);
	pf.left = v[0]; pf.right = v[1]; pf.first = v[2]; pf.before = v[3]; pf.after = v[4];
	if (line >= 0) {
		pf.line = line == 0 ? 240 : line == 1 ? 360 : 480;
		pf.linerule = LS_AUTO;
		mask |= PM_LINE;
	}
	done(&s, ec_para(qd.doc, &s, mask, &pf));
}

/* Edit > Find..., Replace... and Find Again: one dialog; what was looked for is kept (Mac Roman) */
static char find_what[128], find_with[128];
static int find_case, find_word;

static unsigned find_flags(void)
{
	return (find_case ? FIND_CASE : 0) | (find_word ? FIND_WORD : 0);
}

/* the search once more, from the selection; a note when there is nothing (Guidelines p. 196) */
static void find_again(void)
{
	char u[400];
	ESel s = sel_now();
	if (!find_what[0]) {
		SysBeep(1);
		return;
	}
	to_utf8(find_what, u, sizeof u);
	if (ec_find(qd.doc, &s, u, find_flags()))
		done(&s, 1);
	else
		qm_alert(QA_NOTE, "\"%s\" was not found.", find_what);
}

static void find_dialog(void)
{
	char what[400], with[400], t[24];
	u32 n;
	ESel s = sel_now();
	int b;
	DItem it[] = {
		{ DI_TEXT, 0, 0, 13, 13, 354, "Find and Replace" },
		{ DI_TEXT, 0, 0, 13, 41, 100, "Find:" },
		{ DI_EDIT, 0, 0, 116, 41, 248, NULL, find_what, sizeof find_what },
		{ DI_TEXT, 0, 0, 13, 69, 100, "Replace with:" },
		{ DI_EDIT, 0, 0, 116, 69, 248, NULL, find_with, sizeof find_with },
		{ DI_CHECK, 0, 0, 13, 97, 150, "Match case", NULL, 0, &find_case },
		{ DI_CHECK, 0, 0, 170, 97, 190, "Whole words only", NULL, 0, &find_word },
		{ DI_BUTTON, 0, 0, 13, 128, 90, "Replace All" },
		{ DI_BUTTON, 0, 0, 109, 128, 70, "Replace" },
		{ DI_BUTTON, 0, 0, 215, 128, 70, "_Cancel" },
		{ DI_BUTTON, 0, 0, 297, 128, 70, "Find Next" },
	};
	b = qdlg_run(380, 161, it, 11, 10, 9);
	if (b == 9 || !find_what[0])
		return;
	to_utf8(find_what, what, sizeof what);
	to_utf8(find_with, with, sizeof with);
	if (b == 10) {
		find_again();
	} else if (b == 8) {
		if (ec_replace(qd.doc, &s, what, with, find_flags()))
			done(&s, 1);
		else
			qm_alert(QA_NOTE, "\"%s\" was not found.", find_what);
	} else {
		n = ec_replace_all(qd.doc, &s, what, with, find_flags());
		if (n)
			done(&s, 1);
		snprintf(t, sizeof t, "%lu", (unsigned long)n);
		qm_alert(QA_NOTE, n ? "Replacements made: %s." : "\"%s\" was not found.", n ? t : find_what);
	}
}

/* References > Caption...: the label (Figure, Table, Equation) and the words after its number */
static void caption(void)
{
	char mac[200], u[600];
	int label = 0;
	ESel s = sel_now();
	DItem it[] = {
		{ DI_TEXT, 0, 0, 13, 13, 334, "Caption" },
		{ DI_TEXT, 0, 0, 13, 41, 90, "Label:" },
		{ DI_RADIO, 0, 0, 110, 41, 230, "Figure", NULL, 0, &label, 0 },
		{ DI_RADIO, 0, 0, 110, 59, 230, "Table", NULL, 0, &label, 1 },
		{ DI_RADIO, 0, 0, 110, 77, 230, "Equation", NULL, 0, &label, 2 },
		{ DI_TEXT, 0, 0, 13, 105, 90, "Text:" },
		{ DI_EDIT, 0, 0, 113, 105, 231, NULL, mac, sizeof mac },
		{ DI_BUTTON, 0, 0, 277, 138, 70, "Insert" },
		{ DI_BUTTON, 0, 0, 195, 138, 70, "_Cancel" },
	};
	mac[0] = 0;
	if (qdlg_run(360, 171, it, 9, 7, 8) != 7)
		return;
	u[0] = ' ';                         /* ("Figure 1 Text": a space after the number) */
	to_utf8(mac, u + 1, sizeof u - 1);
	done(&s, ec_caption(qd.doc, &s, label, mac[0] ? u : ""));
}

/* References > Bookmark...: the selection (or the word at the caret) named */
static void bookmark(void)
{
	char mac[64], u[200];
	ESel s = sel_now();
	if (!text_dialog("Bookmark name:", "Add", mac, sizeof mac))
		return;
	to_utf8(mac, u, sizeof u);
	if (ec_bookmark(qd.doc, &s, u))
		done(&s, 1);
	else
		qm_alert(QA_STOP, "A bookmark's name is one word (letters, digits and _), on a word of the text.", NULL);
}

/* References > Cross-reference...: one of the document's bookmarks (the first ten), its text shown */
static void xref(void)
{
	enum { MAXB = 10 };
	DItem it[2 + MAXB + 2];
	char names[MAXB][64];
	int k, n = 0, nb = 0, pick = 0;
	ESel s = sel_now();
	memset(it, 0, sizeof it);
	for (k = 0; k < qd.doc->nbmarks && nb < MAXB; k++) {
		const char *nm = doc_bmark_name(qd.doc, k);
		Pos a, b;
		if (!nm || nm[0] == '_' || !doc_bmark_find(qd.doc, nm, &a, &b))
			continue;                   /* (hidden ones, and those no longer in the text) */
		utf8_to_macroman(nm, names[nb++], 64);
	}
	if (!nb) {
		qm_alert(QA_NOTE, "This document has no bookmarks.", NULL);
		return;
	}
	it[n].kind = DI_TEXT; it[n].x = 13; it[n].y = 13; it[n].w = 274; it[n++].label = "Cross-reference";
	it[n].kind = DI_TEXT; it[n].x = 13; it[n].y = 41; it[n].w = 90; it[n++].label = "Bookmarks:";
	for (k = 0; k < nb; k++) {
		it[n].kind = DI_RADIO; it[n].x = 110; it[n].y = 41 + 18 * k; it[n].w = 177;
		it[n].label = names[k]; it[n].raw = 1; it[n].value = &pick; it[n++].ord = k;
	}
	k = 41 + 18 * nb + 14;
	it[n].kind = DI_BUTTON; it[n].x = 217; it[n].y = k; it[n].w = 70; it[n++].label = "Insert";
	it[n].kind = DI_BUTTON; it[n].x = 135; it[n].y = k; it[n].w = 70; it[n++].label = "_Cancel";
	if (qdlg_run(300, k + 33, it, n, n - 2, n - 1) != n - 2)
		return;
	{
		char u[200];
		to_utf8(names[pick], u, sizeof u);
		done(&s, ec_xref(qd.doc, &s, u));
	}
}

/* ---- Review ---- */

/* the word list: Quill's British English one (dict/british-english), in a folder named dict beside
   Octavo as the Amiga's Quill keeps it; read when the spelling is first checked */
static Spell speller;
static int speller_state;                   /* 0 not read yet, 1 read, -1 couldn't be */

/* text of the document as Mac Roman (a word, a comment) */
static void doc_text_mac(Pos a, Pos b, char *out, int cap)
{
	char u[200];
	u32 n = b.off - a.off;
	if (a.para != b.para || n >= sizeof u)
		n = 0;
	memcpy(u, qd.doc->p[a.para]->t + a.off, n);
	u[n] = 0;
	utf8_to_macroman(u, out, cap);
}

/* Review > Spelling...: each word the list doesn't know, from the caret on, in a dialog: changed to
   a suggestion, or ignored from now on (Ignore All); a note when the document is done */
static void spelling(void)
{
	enum { NSUG = 5 };
	ESel s = sel_now();
	Pos from = es_hi(&s), a, b;
	if (!speller_state) {
		char path[300];
		set_watch_cursor(1);
		snprintf(path, sizeof path, "%sdict:british-english", qm_appdir);
		speller_state = spell_load(&speller, path) ? 1 : -1;
		set_watch_cursor(0);
	}
	if (speller_state < 0) {
		qm_alert(QA_STOP, "Octavo's word list (british-english, in a folder named dict beside Octavo) couldn't be read.", NULL);
		return;
	}
	for (;;) {
		char word[64], line[160], *sug[NSUG], labels[NSUG][64];
		int n, k, pick = 0, b0, hit;
		DItem it[3 + NSUG + 3];
		int m = 0;
		if (!spell_next(&speller, qd.doc, from, &a, &b)) {
			qm_alert(QA_NOTE, "The spelling check is complete.", NULL);
			return;
		}
		/* the word selected and shown before the dialog asks about it */
		s.anchor = a;
		s.caret = b;
		done(&s, 1);
		qv_refresh_now(qm_view());
		doc_text_mac(a, b, word, sizeof word);
		snprintf(line, sizeof line, "%s \"%s\"", tr("Not in the dictionary:"), word);
		n = spell_suggest(&speller, qd.doc->p[a.para]->t + a.off, b.off - a.off, sug, NSUG);
		memset(it, 0, sizeof it);
		it[m].kind = DI_TEXT; it[m].x = 13; it[m].y = 13; it[m].w = 314; it[m++].label = "Spelling";
		it[m].kind = DI_TEXT; it[m].x = 13; it[m].y = 41; it[m].w = 314; it[m].raw = 1; it[m++].label = line;
		it[m].kind = DI_TEXT; it[m].x = 13; it[m].y = 69; it[m].w = n ? 90 : 314; it[m++].label = n ? "Suggestions:" : "No suggestions.";
		for (k = 0; k < n; k++) {
			utf8_to_macroman(sug[k], labels[k], 64);
			it[m].kind = DI_RADIO; it[m].x = 110; it[m].y = 69 + 18 * k; it[m].w = 217;
			it[m].label = labels[k]; it[m].raw = 1; it[m].value = &pick; it[m++].ord = k;
		}
		b0 = 69 + 18 * (n ? n : 1) + 14;
		it[m].kind = DI_BUTTON; it[m].x = 13; it[m].y = b0; it[m].w = 90; it[m++].label = "Ignore All";
		it[m].kind = DI_BUTTON; it[m].x = 176; it[m].y = b0; it[m].w = 70; it[m++].label = "_Cancel";
		it[m].kind = DI_BUTTON; it[m].x = 258; it[m].y = b0; it[m].w = 70; it[m].off = !n; it[m++].label = "Change";
		hit = qdlg_run(340, b0 + 33, it, m, n ? m - 1 : m - 3, m - 2);
		if (hit == m - 1 && n) {
			/* (the label shown is Mac Roman; the word put in is the core's UTF-8 suggestion) */
			ec_text(qd.doc, &s, sug[pick]);
			from = s.caret;
			done(&s, 1);
		} else if (hit == m - 3) {
			spell_add(&speller, qd.doc->p[a.para]->t + a.off, b.off - a.off);
			from = b;
		}
		for (k = 0; k < n; k++)
			free(sug[k]);
		if (hit == m - 2)
			return;                     /* Cancel */
	}
}

/* Review > Word Count: the body's words, paragraphs and pages in a note */
static void word_count(void)
{
	char t[160];
	lay_update(qd.lay);
	snprintf(t, sizeof t, tr("Words: %lu\nParagraphs: %lu\nPages: %lu"), (unsigned long)doc_count_words(qd.doc),
	         (unsigned long)qd.doc->np, (unsigned long)qd.lay->npages);
	qm_alert(QA_NOTE, "%s", t);
}

/* a comment shown in a note: the page view does not draw comments' balloons yet */
static void show_comment(int k)
{
	char text[300], author[64], t[400], *u = story_text(&qd.doc->comments[k].text);
	utf8_to_macroman(u ? u : "", text, sizeof text);
	utf8_to_macroman(qd.doc->comments[k].author, author, sizeof author);
	free(u);
	snprintf(t, sizeof t, tr("Comment by %s:\n%s"), author[0] ? author : "?", text);
	qm_alert(QA_NOTE, "%s", t);
}

static void new_comment(void)
{
	char mac[256], u[768], when[24];
	ESel s = sel_now();
	if (!text_dialog("Comment:", "Add", mac, sizeof mac))
		return;
	to_utf8(mac, u, sizeof u);
	ec_now(when);
	done(&s, ec_comment(qd.doc, &s, u, qm_author(), when) >= 0);
}

static void next_comment(int dir)
{
	ESel s = sel_now();
	int k = ec_comment_next(qd.doc, &s, dir);
	done(&s, k >= 0);
	if (k >= 0) {
		qv_refresh_now(qm_view());
		show_comment(k);
	}
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
		qm_alert(QA_STOP, "%s\nis not a PNG or JPEG picture Octavo can read.", path);
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
	if (id >= ID_COLS0 && id < ID_COLS0 + 3)
		return QS_ENABLED | (doc_sect(qd.doc, qd.caret.para)->cols == id - ID_COLS0 + 1 ||
		                     (id == ID_COLS0 && doc_sect(qd.doc, qd.caret.para)->cols == 0) ? QS_CHECKED : 0);
	if (id >= ID_LINENUM0 && id < ID_LINENUM0 + 4)
		return QS_ENABLED | (doc_sect(qd.doc, qd.doc->np - 1)->linenum == id - ID_LINENUM0 ? QS_CHECKED : 0);
	switch (id) {
	case ID_PAGESETUP: case ID_PARAGRAPH: case ID_FIND: case ID_REPLACE:
		return QS_ENABLED;
	case ID_FINDAGAIN:
		return find_what[0] ? QS_ENABLED : 0;
	case ID_TOC: case ID_CAPTION: case ID_BOOKMARK: case ID_XREF:
		return QS_ENABLED;
	case ID_NEXTNOTE:
		return qd.doc->nnotes || qd.doc->nenotes ? QS_ENABLED : 0;
	case ID_SPELL: case ID_WORDCOUNT: case ID_COMMENT:
	case ID_PREVCHANGE: case ID_NEXTCHANGE: case ID_ACCEPT: case ID_REJECT: case ID_ACCEPTALL: case ID_REJECTALL:
		return QS_ENABLED;
	case ID_PREVCOMMENT: case ID_NEXTCOMMENT: case ID_DELCOMMENT:
		return qd.doc->ncomments ? QS_ENABLED : 0;
	case ID_TRACK:
		return QS_ENABLED | (qm_tracking() ? QS_CHECKED : 0);
	case ID_PORTRAIT: case ID_LANDSCAPE:
		return QS_ENABLED | ((qd.doc->page.width > qd.doc->page.height) == (id == ID_LANDSCAPE) ? QS_CHECKED : 0);
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
	if (id >= ID_COLS0 && id < ID_COLS0 + 3) {
		done(&s, ec_columns(qd.doc, &s, (int)(id - ID_COLS0) + 1));
		return 1;
	}
	if (id >= ID_LINENUM0 && id < ID_LINENUM0 + 4) {
		done(&s, ec_line_numbers(qd.doc, &s, (int)(id - ID_LINENUM0)));
		return 1;
	}
	switch (id) {
	case ID_PAGESETUP: page_setup(); return 1;
	case ID_FIND: case ID_REPLACE: find_dialog(); return 1;
	case ID_FINDAGAIN: find_again(); return 1;
	case ID_TOC: done(&s, ec_toc(qd.doc, qd.lay, &s)); return 1;
	case ID_NEXTNOTE: done(&s, ec_next_note(qd.doc, &s)); return 1;
	case ID_CAPTION: caption(); return 1;
	case ID_BOOKMARK: bookmark(); return 1;
	case ID_XREF: xref(); return 1;
	case ID_SPELL: spelling(); return 1;
	case ID_WORDCOUNT: word_count(); return 1;
	case ID_COMMENT: new_comment(); return 1;
	case ID_PREVCOMMENT: case ID_NEXTCOMMENT: next_comment(id == ID_NEXTCOMMENT ? 1 : -1); return 1;
	case ID_DELCOMMENT: done(&s, ec_comment_delete(qd.doc, &s)); return 1;
	case ID_TRACK: qm_track_toggle(); qm_status(); return 1;
	case ID_PREVCHANGE: case ID_NEXTCHANGE: done(&s, ec_change_next(qd.doc, &s, id == ID_NEXTCHANGE ? 1 : -1)); return 1;
	case ID_ACCEPT: case ID_REJECT: done(&s, ec_resolve(qd.doc, &s, id == ID_ACCEPT, 0)); return 1;
	case ID_ACCEPTALL: case ID_REJECTALL: done(&s, ec_resolve(qd.doc, &s, id == ID_ACCEPTALL, 1)); return 1;
	case ID_PORTRAIT: case ID_LANDSCAPE: orientation(id == ID_LANDSCAPE); return 1;
	case ID_PARAGRAPH: paragraph(); return 1;
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
	case QP_COLUMNS: *first = ID_COLS0; return 3;
	case QP_LINENUM: *first = ID_LINENUM0; return 4;
	}
	return 0;
}

const char *qc_label(unsigned long id)
{
	if (id >= ID_SYM0 && id < ID_SYM0 + NSYMBOLS)
		return symbols[id - ID_SYM0].mac;
	if (id >= ID_BREAK0 && id < ID_BREAK0 + NBREAKS)
		return tr(break_names[id - ID_BREAK0]);
	if (id >= ID_COLS0 && id < ID_COLS0 + 3)
		return tr(column_names[id - ID_COLS0]);
	if (id >= ID_LINENUM0 && id < ID_LINENUM0 + 4)
		return tr(linenum_names[id - ID_LINENUM0]);
	return NULL;
}

/* The menus, as the menu bar has room for them (qm_main.c's menu_fold): on a screen as narrow as a
   compact Mac's (512 pixels) References' commands follow Insert's in its menu, and with less room
   still Layout's follow Format's (qm_main.c puts them there: qc_menu_layout without its title) */
typedef void (*QcAdd)(int type, const char *label, const char *key, int flags, unsigned long id);
void qc_menu_layout(QcAdd add, int titled)
{
	int i;
	/* Layout: the page and the table the caret is in (the Amiga's Layout and Table Layout tabs) */
	if (titled)
		add(NM_TITLE, "Layout", NULL, 0, 0);
	add(NM_ITEM, "Page Setup...", NULL, 0, ID_PAGESETUP);
	add(NM_ITEM, "Portrait", NULL, CHECKIT, ID_PORTRAIT);
	add(NM_ITEM, "Landscape", NULL, CHECKIT, ID_LANDSCAPE);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Columns", NULL, 0, 0);
	for (i = 0; i < 3; i++)
		add(NM_SUB, column_names[i], NULL, CHECKIT, ID_COLS0 + (unsigned long)i);
	add(NM_ITEM, "Line Numbers", NULL, 0, 0);
	for (i = 0; i < 4; i++)
		add(NM_SUB, linenum_names[i], NULL, CHECKIT, ID_LINENUM0 + (unsigned long)i);
	add(NM_ITEM, "Paragraph...", NULL, 0, ID_PARAGRAPH);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
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

/* Insert, Layout (unless layout_in_format), References (in Insert's menu when refs_in_insert), Review */
void qc_menus(QcAdd add, int refs_in_insert, int layout_in_format)
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
	add(NM_SUB, "Line Break", NULL, 0, ID_LINEBREAK);        /* (the Amiga ribbon's, in the ribbon tree) */
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Date", NULL, 0, ID_DATE);
	add(NM_ITEM, "Page Number", NULL, 0, ID_PAGENUM);
	add(NM_ITEM, "Symbol", NULL, 0, 0);
	for (i = 0; i < NSYMBOLS; i++)
		add(NM_SUB, symbols[i].mac, NULL, 0, ID_SYM0 + (unsigned long)i);
	if (!refs_in_insert) {
		if (!layout_in_format)
			qc_menu_layout(add, 1);
		/* References: the Amiga's References tab, where the core does the work */
		add(NM_TITLE, "References", NULL, 0, 0);
	} else
		add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Table of Contents", NULL, 0, ID_TOC);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Footnote...", NULL, 0, ID_FOOTNOTE);
	add(NM_ITEM, "Endnote...", NULL, 0, ID_ENDNOTE);
	add(NM_ITEM, "Next Footnote", NULL, 0, ID_NEXTNOTE);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Caption...", NULL, 0, ID_CAPTION);
	add(NM_ITEM, "Bookmark...", NULL, 0, ID_BOOKMARK);
	add(NM_ITEM, "Cross-reference...", NULL, 0, ID_XREF);
	if (refs_in_insert && !layout_in_format)
		qc_menu_layout(add, 1);
	/* Review: proofing, comments and tracked changes */
	add(NM_TITLE, "Review", NULL, 0, 0);
	add(NM_ITEM, "Spelling...", NULL, 0, ID_SPELL);
	add(NM_ITEM, "Word Count", NULL, 0, ID_WORDCOUNT);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "New Comment...", NULL, 0, ID_COMMENT);
	add(NM_ITEM, "Previous Comment", NULL, 0, ID_PREVCOMMENT);
	add(NM_ITEM, "Next Comment", NULL, 0, ID_NEXTCOMMENT);
	add(NM_ITEM, "Delete Comment", NULL, 0, ID_DELCOMMENT);
	add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0);
	add(NM_ITEM, "Track Changes", NULL, CHECKIT, ID_TRACK);
	add(NM_ITEM, "Previous Change", NULL, 0, ID_PREVCHANGE);
	add(NM_ITEM, "Next Change", NULL, 0, ID_NEXTCHANGE);
	add(NM_ITEM, "Accept Change", NULL, 0, ID_ACCEPT);
	add(NM_ITEM, "Reject Change", NULL, 0, ID_REJECT);
	add(NM_ITEM, "Accept All Changes", NULL, 0, ID_ACCEPTALL);
	add(NM_ITEM, "Reject All Changes", NULL, 0, ID_REJECTALL);
}
