/*
 * mac/qm_main.c - Quill on the classic Mac OS: the application, its window and its menus.
 * GPL-2.0-only, as Quill.
 *
 * One document window (the page view, mac/qm_view.c) and the menu bar. The menus are a NewMenu
 * table, as on the Amiga; Riptide's MUI for the Mac arranges it the way the Human Interface
 * Guidelines (1992) want: About Quill in the Apple menu, Close and Quit last in File, the
 * standard Edit menu, then Quill's Font, Size, Style, Format and View, with their check marks
 * kept to the text at the caret. Command keys are the Amiga's where the Mac does not reserve the
 * key for something else (docs/mac.md lists them).
 *
 * Files are opened and saved with Standard File and read and written by Quill's core, which
 * knows the format by the name's ending (.docx, .odt, .rtf, .html, .md, .txt); a saved file gets
 * the Mac file type for its format and Quill's creator, 'QUIL' (mac/qm_mac.c). The fonts are
 * Quill's own TrueType fonts, from the Fonts folder beside the application.
 *
 * Printing is left out: Quill draws pages for PDF, and Export PDF writes one; a Mac of the time
 * prints through a printer driver's QuickDraw, which this port does not drive yet.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mui/mm.h"
#include "quill_mac.h"
#include "qm_mac.h"
#include "../core/docx.h"
#include "../core/pdf.h"
#include "../amiga/lang.h"

QDoc qd;
static Object *app, *win, *view, *strip;

/* the format typing continues with after a Style, Font or Size command with no text selected
   (Guidelines p. 239: it applies to what is typed next at the insertion point) */
static CharFmt typing;
static int typing_set;

const CharFmt *qm_typing_fmt(void) { return typing_set ? &typing : NULL; }
void qm_typing_reset(void) { typing_set = 0; }

/* ---- the menus ---- */
enum {
	ID_NEW = 1, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_PDF, ID_ABOUT, ID_QUIT,
	ID_REDO, ID_SELALL,
	ID_PLAIN, ID_BOLD, ID_ITALIC, ID_UNDERLINE, ID_STRIKE, ID_SUPER, ID_SUB,
	ID_GROW, ID_SHRINK,
	ID_LEFT, ID_CENTER, ID_RIGHT, ID_JUSTIFY, ID_SINGLE, ID_ONEHALF, ID_DOUBLE, ID_PAGEBREAK,
	ID_ZOOMFIT, ID_ZOOM50, ID_ZOOM75, ID_ZOOM100, ID_ZOOM150, ID_ZOOM200,
	ID_CLOSEWIN,
	ID_SIZE0 = 100,             /* + the Size menu's row */
	ID_FONT0 = 200              /* + the Font menu's family */
};
static const int sizes[] = { 9, 10, 12, 14, 18, 24, 36 };       /* points: the Size menu (p. 112) */
#define NSIZES ((int)(sizeof sizes / sizeof sizes[0]))
static const int zooms[] = { 0, 50, 75, 100, 150, 200 };

/* the font families Quill has (the Fonts folder's), each once */
#define MAXFAM 24
static char fam[MAXFAM][48];
static int nfam;

static struct NewMenu menu[128];
static int nmenu;
static char size_label[NSIZES][8];

static void nm_add(UBYTE type, const char *label, const char *key, UWORD flags, ULONG id)
{
	struct NewMenu *e = &menu[nmenu++];
	e->nm_Type = type;
	e->nm_Label = (CONST_STRPTR)label;
	e->nm_CommKey = (CONST_STRPTR)key;
	e->nm_Flags = flags;
	e->nm_MutualExclude = 0;
	e->nm_UserData = (APTR)id;
}
#define BAR() nm_add(NM_ITEM, (const char *)NM_BARLABEL, NULL, 0, 0)

static void menus_build(void)
{
	int i;
	nmenu = 0;
	nm_add(NM_TITLE, "File", NULL, 0, 0);
	nm_add(NM_ITEM, "New", "N", 0, ID_NEW);
	nm_add(NM_ITEM, "Open...", "O", 0, ID_OPEN);
	nm_add(NM_ITEM, "Close", "W", 0, ID_CLOSEWIN);
	BAR();
	nm_add(NM_ITEM, "Save", "S", 0, ID_SAVE);
	nm_add(NM_ITEM, "Save As...", NULL, 0, ID_SAVEAS);           /* the Amiga's Amiga-A is Select All here */
	nm_add(NM_ITEM, "Export PDF...", "D", 0, ID_PDF);
	nm_add(NM_ITEM, "About...", NULL, 0, ID_ABOUT);              /* to the Apple menu */
	nm_add(NM_ITEM, "Quit", "Q", 0, ID_QUIT);
	nm_add(NM_TITLE, "Edit", NULL, 0, 0);                        /* after Undo, Cut, Copy, Paste, Clear */
	nm_add(NM_ITEM, "Redo", "Y", 0, ID_REDO);
	nm_add(NM_ITEM, "Select All", "A", 0, ID_SELALL);
	nm_add(NM_TITLE, "Font", NULL, 0, 0);
	for (i = 0; i < nfam; i++)
		nm_add(NM_ITEM, fam[i], NULL, CHECKIT, (ULONG)(ID_FONT0 + i));
	nm_add(NM_TITLE, "Size", NULL, 0, 0);
	for (i = 0; i < NSIZES; i++) {
		snprintf(size_label[i], sizeof size_label[i], "%d", sizes[i]);
		nm_add(NM_ITEM, size_label[i], NULL, CHECKIT, (ULONG)(ID_SIZE0 + i));
	}
	BAR();
	nm_add(NM_ITEM, "Grow Font", ">", 0, ID_GROW);
	nm_add(NM_ITEM, "Shrink Font", "<", 0, ID_SHRINK);
	nm_add(NM_TITLE, "Style", NULL, 0, 0);                       /* the Guidelines' Style menu, p. 113 */
	nm_add(NM_ITEM, "Plain Text", "T", CHECKIT, ID_PLAIN);
	BAR();
	nm_add(NM_ITEM, "Bold", "B", CHECKIT, ID_BOLD);
	nm_add(NM_ITEM, "Italic", "I", CHECKIT, ID_ITALIC);
	nm_add(NM_ITEM, "Underline", "U", CHECKIT, ID_UNDERLINE);
	nm_add(NM_ITEM, "Strikethrough", NULL, CHECKIT, ID_STRIKE);
	BAR();
	nm_add(NM_ITEM, "Superscript", "+", CHECKIT, ID_SUPER);
	nm_add(NM_ITEM, "Subscript", "-", CHECKIT, ID_SUB);
	nm_add(NM_TITLE, tr_ctx("menu", "Format"), NULL, 0, 0);   /* the menu, not the verb */
	nm_add(NM_ITEM, "Align Left", "[", CHECKIT, ID_LEFT);
	nm_add(NM_ITEM, "Centre", "E", CHECKIT, ID_CENTER);
	nm_add(NM_ITEM, "Align Right", "]", CHECKIT, ID_RIGHT);
	nm_add(NM_ITEM, "Justify", "J", CHECKIT, ID_JUSTIFY);
	BAR();
	nm_add(NM_ITEM, "Single", "1", CHECKIT, ID_SINGLE);
	nm_add(NM_ITEM, "1.5 Lines", "5", CHECKIT, ID_ONEHALF);
	nm_add(NM_ITEM, "Double", "2", CHECKIT, ID_DOUBLE);
	BAR();
	nm_add(NM_ITEM, "Page Break", NULL, 0, ID_PAGEBREAK);        /* the Amiga's Amiga-P is Print on the Mac */
	nm_add(NM_TITLE, "View", NULL, 0, 0);
	nm_add(NM_ITEM, "Fit Page Width", "0", CHECKIT, ID_ZOOMFIT);
	BAR();
	nm_add(NM_ITEM, "50%", NULL, CHECKIT, ID_ZOOM50);
	nm_add(NM_ITEM, "75%", NULL, CHECKIT, ID_ZOOM75);
	nm_add(NM_ITEM, "100%", NULL, CHECKIT | CHECKED, ID_ZOOM100);
	nm_add(NM_ITEM, "150%", NULL, CHECKIT, ID_ZOOM150);
	nm_add(NM_ITEM, "200%", NULL, CHECKIT, ID_ZOOM200);
	nm_add(NM_END, NULL, NULL, 0, 0);
}

static void check(ULONG id, int on)
{
	Object *it = strip ? (Object *)mui_do(strip, MUIM_FindUData, id) : NULL;
	if (it)
		mui_set(it, MUIA_Menuitem_Checked, on ? TRUE : FALSE);
}

/* ---- the document ---- */

static void set_title(void)
{
	static char t[80];
	if (qd.path[0]) {
		const char *s = strrchr(qd.path, ':');
		strncpy(t, s ? s + 1 : qd.path, sizeof t - 1);
		t[sizeof t - 1] = 0;
	} else
		strcpy(t, tr("Untitled"));
	if (win)
		mui_set(win, MUIA_Window_Title, (ULONG)t);
}

/* the format at the caret, or at the selection's start: what the menus' check marks show */
static const CharFmt *fmt_here(void)
{
	Pos a = pos_cmp(qd.anchor, qd.caret) < 0 ? qd.anchor : qd.caret;
	if (typing_set)
		return &typing;
	if (pos_cmp(qd.anchor, qd.caret) != 0)
		return para_fmt_at(qd.doc->p[a.para], a.off);
	return para_fmt_before(qd.doc->p[a.para], a.off);
}

/* the check marks, as Font, Size, Style and Format apply to the text at the caret (only the ones
   that changed are set: each is a Menu Manager call) */
void qm_status(void)
{
	static int last_font = -2, last_size = -2, last_flags = -1, last_align = -1, last_line = -1;
	const CharFmt *f;
	const ParaFmt *pf;
	int i, font = -1, size = -1, flags, align, line;
	if (!qd.doc || !qd.doc->np)
		return;
	f = fmt_here();
	pf = &qd.doc->p[qd.caret.para]->pf;
	if (f->font < qd.doc->nfonts)
		for (i = 0; i < nfam; i++)
			if (!strcmp(fam[i], qd.doc->fonts[f->font]))
				font = i;
	for (i = 0; i < NSIZES; i++)
		if (f->size == sizes[i] * 2)
			size = i;
	flags = f->flags & (CF_BOLD | CF_ITALIC | CF_UNDERLINE | CF_STRIKE | CF_SUPER | CF_SUB);
	align = pf->align;
	line = pf->linerule == LS_AUTO ? pf->line : 0;
	if (font != last_font) {
		for (i = 0; i < nfam; i++)
			check(ID_FONT0 + i, i == font);
		last_font = font;
	}
	if (size != last_size) {
		for (i = 0; i < NSIZES; i++)
			check(ID_SIZE0 + i, i == size);
		last_size = size;
	}
	if (flags != last_flags) {
		check(ID_PLAIN, !flags);
		check(ID_BOLD, flags & CF_BOLD);
		check(ID_ITALIC, flags & CF_ITALIC);
		check(ID_UNDERLINE, flags & CF_UNDERLINE);
		check(ID_STRIKE, flags & CF_STRIKE);
		check(ID_SUPER, flags & CF_SUPER);
		check(ID_SUB, flags & CF_SUB);
		last_flags = flags;
	}
	if (align != last_align) {
		for (i = 0; i < 4; i++)
			check(ID_LEFT + i, align == i);
		last_align = align;
	}
	if (line != last_line) {
		check(ID_SINGLE, line == 240 || line == 0);
		check(ID_ONEHALF, line == 360);
		check(ID_DOUBLE, line == 480);
		last_line = line;
	}
}

static void doc_take(Doc *d, const char *path)
{
	if (qd.lay)
		lay_free(qd.lay);
	if (qd.doc)
		doc_free(qd.doc);
	qd.doc = d;
	qd.lay = lay_new(d, qd.fonts);
	memset(&qd.caret, 0, sizeof qd.caret);
	qd.anchor = qd.caret;
	typing_set = 0;
	strncpy(qd.path, path ? path : "", sizeof qd.path - 1);
	qd.path[sizeof qd.path - 1] = 0;
	d->dirty = 0;
	set_title();
	if (view)
		qv_reset(view);
	qm_status();
}

/* an alert (Riptide's MUI_RequestA: the Mac's alert box, buttons in the Mac's order): gadgets as
   "Save|Don't Save|Cancel", fmt with one %s; 1 for the first button, 2, 3... and 0 for the last */
/* the document window, open again after Close */
static void win_show(void)
{
	if (win && !mui_get(win, MUIA_Window_Open)) {
		mui_set(win, MUIA_Window_Open, TRUE);
		qm_status();
	}
}

static int ask(const char *gadgets, const char *fmt, const char *arg)
{
	ULONG args[1];
	args[0] = (ULONG)arg;
	return (int)MUI_RequestA(app, win, 0, (CONST_STRPTR)"Quill", (CONST_STRPTR)tr(gadgets), (CONST_STRPTR)tr(fmt), args);
}

int mm_dir_path(short vref, long dir, char *out, int len);       /* riptide src/mac/mui/mm_os.c */

/* a full Mac path from Standard File's reply */
static int reply_path(const StandardFileReply *r, char *out, int cap)
{
	int n;
	if (mm_dir_path(r->sfFile.vRefNum, r->sfFile.parID, out, cap) < 0)
		return 0;
	n = (int)strlen(out);
	if (n + r->sfFile.name[0] + 1 > cap)
		return 0;
	memcpy(out + n, r->sfFile.name + 1, r->sfFile.name[0]);
	out[n + r->sfFile.name[0]] = 0;
	return 1;
}

static int save_to(const char *path)
{
	int ok;
	set_watch_cursor(1);
	ok = doc_save_file(qd.doc, path);
	set_watch_cursor(0);
	if (!ok) {
		ask("OK", "%s couldn't be saved.", path);
		return 0;
	}
	strncpy(qd.path, path, sizeof qd.path - 1);
	set_title();
	return 1;
}

/* Save As: Standard File's Put dialog, the name ending .docx unless another format's is given */
static int save_as(void)
{
	StandardFileReply r;
	unsigned char prompt[256], name[64];
	char path[300], def[64];
	const char *s = strrchr(qd.path, ':');
	strncpy(def, qd.path[0] ? (s ? s + 1 : qd.path) : tr("Untitled"), sizeof def - 6);
	def[sizeof def - 6] = 0;
	if (!strchr(def, '.'))
		strcat(def, ".docx");
	memset(&r, 0, sizeof r);
	mac_stdputfile(mac_pstr(prompt, tr("Save As...")), mac_pstr(name, def), &r);
	if (!r.sfGood || !reply_path(&r, path, sizeof path))
		return 0;
	if (!doc_format_for_name(path, 1) || !strchr(strrchr(path, ':') ? strrchr(path, ':') : path, '.'))
		strncat(path, ".docx", sizeof path - strlen(path) - 1);
	return save_to(path);
}

static int save(void)
{
	return qd.path[0] && doc_format_for_name(qd.path, 1) ? save_to(qd.path) : save_as();
}

/* 1 when the document may go: saved, or not wanted (Guidelines p. 72: Save, Don't Save, Cancel) */
static int may_close(void)
{
	const char *s;
	if (!qd.doc || !qd.doc->dirty)
		return 1;
	s = strrchr(qd.path, ':');
	switch (ask("Save|Don't Save|Cancel", "Save the changes to %s?", qd.path[0] ? (s ? s + 1 : qd.path) : tr("Untitled"))) {
	case 1: return save();
	case 2: return 1;
	default: return 0;
	}
}

static void open_file(const char *path)
{
	unsigned lost = 0;
	char err[200];
	Doc *d;
	set_watch_cursor(1);
	d = doc_load_file(path, &lost, err, sizeof err);
	set_watch_cursor(0);
	if (!d) {
		ask("OK", "%s couldn't be read.", path);
		return;
	}
	doc_clear_undo(d);
	doc_take(d, path);
	win_show();
}

static void cmd_open(void)
{
	StandardFileReply r;
	char path[300];
	if (!may_close())
		return;
	memset(&r, 0, sizeof r);
	mac_stdgetfile(-1, NULL, &r);       /* every file: Word documents from elsewhere come typed '????' */
	if (r.sfGood && reply_path(&r, path, sizeof path))
		open_file(path);
}

static void cmd_pdf(void)
{
	StandardFileReply r;
	unsigned char prompt[256], name[64];
	char path[300], def[64], *dot;
	const char *s = strrchr(qd.path, ':');
	strncpy(def, qd.path[0] ? (s ? s + 1 : qd.path) : tr("Untitled"), sizeof def - 5);
	def[sizeof def - 5] = 0;
	if ((dot = strrchr(def, '.')))
		*dot = 0;
	strcat(def, ".pdf");
	memset(&r, 0, sizeof r);
	mac_stdputfile(mac_pstr(prompt, tr("Export PDF...")), mac_pstr(name, def), &r);
	if (!r.sfGood || !reply_path(&r, path, sizeof path))
		return;
	set_watch_cursor(1);
	if (!pdf_save_file(qd.lay, path)) {
		set_watch_cursor(0);
		ask("OK", "%s couldn't be saved.", path);
		return;
	}
	set_watch_cursor(0);
}

/* ---- formatting ---- */

/* what a character command applies to: the selection, else the word around the caret (as Word
   does), else nothing (the typing format then) */
static int fmt_range(Pos *a, Pos *b)
{
	Para *p = qd.doc->p[qd.caret.para];
	*a = qd.anchor;
	*b = qd.caret;
	if (pos_cmp(*a, *b) > 0) {
		Pos t = *a;
		*a = *b;
		*b = t;
	}
	if (pos_cmp(*a, *b))
		return 1;
	if (qd.caret.off > 0 && qd.caret.off < p->len && p->t[qd.caret.off] != ' ' && p->t[qd.caret.off - 1] != ' ' &&
	    p->t[qd.caret.off] != '\t' && p->t[qd.caret.off - 1] != '\t') {
		*a = pos_word_left(qd.doc, qd.caret);
		*b = qd.caret;
		while (b->off < p->len && p->t[b->off] != ' ' && p->t[b->off] != '\t')
			b->off++;
		return 1;
	}
	return 0;
}

static void typing_from_caret(void)
{
	if (!typing_set) {
		typing = *para_fmt_before(qd.doc->p[qd.caret.para], qd.caret.off);
		typing_set = 1;
	}
}

static void char_flag(unsigned flag)
{
	Pos a, b;
	if (fmt_range(&a, &b)) {
		doc_toggle(qd.doc, a, b, flag);
		qv_changed(view, 0);
		return;
	}
	typing_from_caret();
	typing.flags ^= (u8)flag;
	if (flag == CF_SUPER) typing.flags &= (u8)~CF_SUB;
	if (flag == CF_SUB) typing.flags &= (u8)~CF_SUPER;
	qm_status();
}

static void char_format(unsigned mask, const CharFmt *v)
{
	Pos a, b;
	if (fmt_range(&a, &b)) {
		doc_format(qd.doc, a, b, mask, v);
		qv_changed(view, 0);
		return;
	}
	typing_from_caret();
	if (mask & CM_FONT) typing.font = v->font;
	if (mask & CM_SIZE) typing.size = v->size;
	if (mask & (CM_BOLD | CM_ITALIC | CM_UNDERLINE | CM_STRIKE | CM_SUPER | CM_SUB))
		typing.flags &= (u8)~mask;
	qm_status();
}

static void para_format(unsigned mask, const ParaFmt *pf)
{
	u32 a = qd.anchor.para, b = qd.caret.para;
	if (a > b) {
		u32 t = a;
		a = b;
		b = t;
	}
	doc_para_format(qd.doc, a, b, mask, pf);
	qv_changed(view, 0);
}

/* ---- the Edit menu (Riptide's mm_edit_action and mm_edit_state) ---- */

static Fragment *clip;            /* what Quill cut or copied last, with its formatting */
static short clip_count = -1;     /* the desk scrap's ScrapCount when Quill put it there */
#define LM_SCRAPCOUNT (*(volatile short *)0x968)
#define LM_SCRAPSIZE  (*(volatile long *)0x960)

static int cmd_copy(void)
{
	Pos a, b;
	u32 n, i;
	const char *t;
	char *mac;
	if (!pos_cmp(qd.anchor, qd.caret))
		return 0;
	fmt_range(&a, &b);
	fragment_free(clip);
	clip = doc_fragment(qd.doc, a, b);
	if (!clip)
		return 0;
	/* the desk scrap gets the text, Mac Roman with returns between paragraphs, for other programs */
	t = fragment_text(clip, &n);
	if ((mac = malloc(n + 1))) {
		utf8_to_macroman(t, mac, (int)n + 1);
		for (i = 0; mac[i]; i++)
			if (mac[i] == '\n' || mac[i] == '\v')
				mac[i] = '\r';
		ZeroScrap();
		PutScrap((long)strlen(mac), MAC_TYPE('T', 'E', 'X', 'T'), mac);
		free(mac);
	}
	clip_count = LM_SCRAPCOUNT;
	return 1;
}

static void cmd_paste(void)
{
	Pos a, b;
	if (pos_cmp(qd.anchor, qd.caret)) {
		fmt_range(&a, &b);
		doc_delete(qd.doc, a, b);
		qd.caret = qd.anchor = a;
	}
	if (clip && clip_count == LM_SCRAPCOUNT) {
		/* Quill's own: with its formatting */
		doc_paste(qd.doc, &qd.caret, clip, PASTE_KEEP);
	} else {
		/* another program's text: Mac Roman, returns as paragraphs */
		long off = 0, n;
		void **h = mac_newhandle(0);
		if (!h)
			return;
		n = GetScrap(h, MAC_TYPE('T', 'E', 'X', 'T'), &off);
		if (n > 0) {
			char *u = malloc((size_t)n * 3 + 1), *o = u;
			long i;
			mac_hlock(h);
			for (i = 0; u && i < n; i++) {
				unsigned char c = ((unsigned char *)*h)[i];
				if (c == '\r')
					*o++ = '\n';
				else if (c >= 0x20 || c == '\t')
					o += macroman_to_utf8(c, o);
			}
			mac_hunlock(h);
			if (u)
				doc_insert(qd.doc, &qd.caret, u, (u32)(o - u), qm_typing_fmt());
			free(u);
		}
		mac_disposehandle(h);
	}
	qd.anchor = qd.caret;
	qv_changed(view, 1);
}

static int edit_action(int cmd)
{
	Pos a, b;
	switch (cmd) {
	case 0:
		if (doc_undo(qd.doc, &qd.caret)) {
			qd.anchor = qd.caret;
			qv_changed(view, 1);
		}
		return 1;
	case 2:                         /* Cut */
		if (cmd_copy()) {
			fmt_range(&a, &b);
			doc_delete(qd.doc, a, b);
			qd.caret = qd.anchor = a;
			qv_changed(view, 1);
		}
		return 1;
	case 3:
		cmd_copy();
		return 1;
	case 4:
		cmd_paste();
		return 1;
	case 5:                         /* Clear */
		if (fmt_range(&a, &b) && pos_cmp(qd.anchor, qd.caret)) {
			doc_delete(qd.doc, a, b);
			qd.caret = qd.anchor = a;
			qv_changed(view, 1);
		}
		return 1;
	}
	return 0;
}

static unsigned edit_state(void)
{
	unsigned bits = 0;
	int sel = pos_cmp(qd.anchor, qd.caret) != 0;
	if (qd.doc && doc_can_undo(qd.doc))
		bits |= 1;
	if (sel)
		bits |= 1 << 2 | 1 << 3 | 1 << 5;
	if ((clip && clip_count == LM_SCRAPCOUNT) || LM_SCRAPSIZE > 0)
		bits |= 1 << 4;
	return bits;
}

/* ---- commands ---- */

static void command(ULONG id)
{
	CharFmt cf;
	ParaFmt pf;
	memset(&cf, 0, sizeof cf);
	memset(&pf, 0, sizeof pf);
	if (id >= ID_FONT0 && id < ID_FONT0 + (ULONG)nfam) {
		cf.font = (u8)doc_font(qd.doc, fam[id - ID_FONT0]);
		char_format(CM_FONT, &cf);
		return;
	}
	if (id >= ID_SIZE0 && id < ID_SIZE0 + NSIZES) {
		cf.size = (u16)(sizes[id - ID_SIZE0] * 2);
		char_format(CM_SIZE, &cf);
		return;
	}
	switch (id) {
	case ID_NEW:
		if (may_close()) {
			doc_take(doc_new(), NULL);
			win_show();
		}
		break;
	case ID_OPEN: cmd_open(); break;
	case ID_CLOSEWIN:
		/* Guidelines p. 72: the document asks to be saved, the window goes, Quill stays */
		if (may_close()) {
			mui_set(win, MUIA_Window_Open, FALSE);
			doc_take(doc_new(), NULL);
		}
		break;
	case ID_SAVE: save(); break;
	case ID_SAVEAS: save_as(); break;
	case ID_PDF: cmd_pdf(); break;
	case ID_ABOUT:
		ask("OK", "Quill 0.15 for the Mac\n\n%s", tr("A word processor for the classic Mac OS.\nReads and writes Word documents (.docx).\n\n"
		                               "Text is measured with the Liberation and Carlito/Caladea fonts\n(SIL Open Font License), so documents break\n"
		                               "their lines as they do in Word."));
		break;
	case ID_REDO:
		if (doc_redo(qd.doc, &qd.caret)) {
			qd.anchor = qd.caret;
			qv_changed(view, 1);
		}
		break;
	case ID_SELALL:
		qd.anchor.para = 0;
		qd.anchor.off = 0;
		qd.caret.para = qd.doc->np - 1;
		qd.caret.off = qd.doc->p[qd.caret.para]->len;
		qv_changed(view, 0);
		break;
	case ID_PLAIN:
		char_format(CM_BOLD | CM_ITALIC | CM_UNDERLINE | CM_STRIKE | CM_SUPER | CM_SUB, &cf);
		break;
	case ID_BOLD: char_flag(CF_BOLD); break;
	case ID_ITALIC: char_flag(CF_ITALIC); break;
	case ID_UNDERLINE: char_flag(CF_UNDERLINE); break;
	case ID_STRIKE: char_flag(CF_STRIKE); break;
	case ID_SUPER: char_flag(CF_SUPER); break;
	case ID_SUB: char_flag(CF_SUB); break;
	case ID_GROW:
	case ID_SHRINK: {
		Pos a, b;
		if (fmt_range(&a, &b)) {
			doc_grow(qd.doc, a, b, id == ID_GROW ? 1 : -1);
			qv_changed(view, 0);
		}
		break;
	}
	case ID_LEFT: case ID_CENTER: case ID_RIGHT: case ID_JUSTIFY:
		pf.align = (u8)(AL_LEFT + (id - ID_LEFT));
		para_format(PM_ALIGN, &pf);
		break;
	case ID_SINGLE: case ID_ONEHALF: case ID_DOUBLE:
		pf.line = id == ID_SINGLE ? 240 : id == ID_ONEHALF ? 360 : 480;
		pf.linerule = LS_AUTO;
		para_format(PM_LINE, &pf);
		break;
	case ID_PAGEBREAK: {
		Pos a, b;
		doc_begin(qd.doc, qd.caret);
		if (pos_cmp(qd.anchor, qd.caret)) {
			fmt_range(&a, &b);
			doc_delete(qd.doc, a, b);
			qd.caret = a;
		}
		doc_insert(qd.doc, &qd.caret, "\n", 1, NULL);
		pf.pagebreak = 1;
		doc_para_format(qd.doc, qd.caret.para, qd.caret.para, PM_BREAK, &pf);
		doc_end(qd.doc, qd.caret);
		qd.anchor = qd.caret;
		qv_changed(view, 1);
		break;
	}
	case ID_ZOOMFIT: case ID_ZOOM50: case ID_ZOOM75: case ID_ZOOM100: case ID_ZOOM150: case ID_ZOOM200: {
		int i;
		for (i = 0; i < 6; i++)
			check(ID_ZOOMFIT + i, (ULONG)i == id - ID_ZOOMFIT);
		qv_zoom(view, zooms[id - ID_ZOOMFIT]);
		break;
	}
	}
}

/* ---- the start ---- */

/* the watch while Quill is busy (Guidelines p. 285); the pointer is set again by the next move */
void set_watch_cursor(int on)
{
	if (on)
		SetCursor(*(void **)GetCursor(4));      /* watchCursor */
	else
		InitCursor();
}

static void families(void)
{
	int i, k;
	for (i = 0; i < qd.fonts->nfaces && nfam < MAXFAM; i++) {
		for (k = 0; k < nfam && strcmp(fam[k], qd.fonts->faces[i].family); k++)
			;
		if (k == nfam)
			strcpy(fam[nfam++], qd.fonts->faces[i].family);
	}
}

/* The documents the Finder opened Quill with (selected and opened together, or a document of
   Quill's double-clicked): the Segment Loader's application parameters, AppParmHandle (low memory
   0xAEC; Inside Macintosh II-55): a message word (0 open, 1 print), a count, then per file its
   working directory, type, version and name (a Pascal string, padded to an even length). The first
   one is opened: one window, one document. */
#define LM_APPPARMHANDLE (*(unsigned char ***)0xAEC)
static void finder_files(void)
{
	unsigned char **h = LM_APPPARMHANDLE, *p;
	char path[300];
	short vref;
	int n, len;
	if (!h || !*h)
		return;
	p = *h;
	n = *(short *)(p + 2);
	if (n < 1)
		return;
	vref = *(short *)(p + 4);
	if (mm_dir_path(vref, 0, path, sizeof path) < 0)
		return;
	len = (int)strlen(path);
	if (len + p[12] + 1 > (int)sizeof path)
		return;
	memcpy(path + len, p + 13, p[12]);
	path[len + p[12]] = 0;
	mm_log("Quill: the Finder opens %s", path);
	open_file(path);
}

void mm_gfx_lib_init(void);
void mm_screen_init(void *qd_thePort_addr);
void mm_paths_init(void);
void mm_log(const char *fmt, ...);
#define LM_MBARHEIGHT (*(volatile short *)0xBAA)

int main(void)
{
	void *a5;
	short *sb;
	ULONG sigs = 0;
	__asm__ volatile("move.l %%a5,%0" : "=r"(a5));
	mm_gfx_lib_init();
	mm_classes_init();
	sb = (short *)((char *)*(void **)a5 - 116);       /* QuickDraw's screenBits.bounds */
	mm_gfx_init(sb[2] - sb[0] - LM_MBARHEIGHT - 20 < 560);
	mm_screen_init(*(void **)a5);
	mm_paths_init();
	lang_set(lang_mac_system());
	mm_log("Quill for the Mac starts");

	/* the fonts: the Fonts folder beside Quill (the default folder at launch is Quill's own) */
	if (!(qd.fonts = calloc(1, sizeof *qd.fonts)))
		return 20;
	if (!fonts_init(qd.fonts, ":Fonts:"))
		mm_log("Quill: no fonts in :Fonts: (fonts.list)");
	families();
	menus_build();
	mm_edit_action = edit_action;
	mm_edit_state = edit_state;

	app = mui_new(MUIC_Application, MUIA_Application_Title, (ULONG)"Quill", MUIA_Application_Base, (ULONG)"QUILL",
	              MUIA_Application_Version, (ULONG)"$VER: Quill 0.15 (18.11.2026)", TAG_DONE);
	if (!app || !qv_class())
		return 20;
	doc_take(doc_new(), NULL);
	strip = mui_make(MUIO_MenustripNM, (ULONG)menu, 0);
	view = mui_newc(qv_class(), MUIA_Frame, MUIV_Frame_None, TAG_DONE);
	win = mui_new(MUIC_Window, MUIA_Window_Title, (ULONG)tr("Untitled"), MUIA_Window_RootObject, (ULONG)view,
	              MUIA_Window_Menustrip, (ULONG)strip, TAG_DONE);
	if (!view || !win)
		return 20;
	mui_do(app, OM_ADDMEMBER, (ULONG)win);
	mui_do(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_CLOSEWIN);
	set_title();
	mui_set(win, MUIA_Window_Open, TRUE);
	finder_files();
	qm_status();
	for (;;) {
		LONG id = (LONG)mui_do(app, MUIM_Application_NewInput, (ULONG)&sigs);
		if (id == MUIV_Application_ReturnID_Quit || id == ID_QUIT) {
			if (may_close())
				break;
			continue;
		}
		if (id)
			command((ULONG)id);
		if (sigs)
			sigs = Wait(sigs);
	}
	mui_set(win, MUIA_Window_Open, FALSE);
	MUI_DisposeObject(app);
	mm_log("Quill ends");
	return 0;
}
