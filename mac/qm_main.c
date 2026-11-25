/*
 * mac/qm_main.c - Quill on the classic Mac OS: the application, its window and its menus.
 * GPL-2.0-only, as Quill.
 *
 * One document window (the page view, mac/qm_view.c, with the tool strip and rulers inside it) and
 * the menu bar. The menus are a NewMenu table, as on the Amiga; Riptide's MUI for the Mac arranges
 * it the way the Human Interface Guidelines (1992) want: About Quill in the Apple menu, Close and
 * Quit last in File, the standard Edit menu, then Quill's Font, Size, Style, Format and View, with
 * their check marks kept to the text at the caret. Command keys are the Amiga's where the Mac does
 * not reserve the key for something else (docs/mac.md lists them).
 *
 * Every command has one number (quill_mac.h) and one answer to whether it can be chosen and is on
 * (qm_state): the menus' check marks and dimming and the tool strip's items are all set from it, so
 * a strip button and its menu item never disagree (Cornerstone, "State: one answer for everyone").
 * The strip has no command that is not in a menu. Whether the strip and rulers show is kept in the
 * Preferences folder ("Quill Preferences").
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
#include "qm_ribbon.h"
#include "qm_mac.h"
#include "qm_dialog.h"
#include "../core/docx.h"
#include "../core/pdf.h"
#include "../core/edcmd.h"
#include "../amiga/lang.h"


QDoc qd;
char qm_appdir[256];             /* the folder Octavo is in, a full path ending in ':' */
static Object *app, *win, *view, *strip;

/* the format typing continues with after a Style, Font or Size command with no text selected
   (Guidelines p. 239: it applies to what is typed next at the insertion point) */
static CharFmt typing;
static int typing_set;

/* Review > Track Changes: on or off, and this document's revision for the author (-1: none yet) */
static int tracking, track_rid = -1;
static Doc *track_doc;

/* the System's owner name (Sharing Setup: 'STR ' -16096), UTF-8, else "Octavo": changes' and
   comments' author */
const char *qm_author(void)
{
	static char name[96];
	unsigned char **h;
	if (name[0])
		return name;
	strcpy(name, "Octavo");
	h = (unsigned char **)GetResource(MAC_TYPE('S', 'T', 'R', ' '), -16096);
	if (h && *h && (*h)[0]) {
		int i, n = 0;
		for (i = 1; i <= (*h)[0] && n + 4 < (int)sizeof name; i++)
			n += macroman_to_utf8((*h)[i], name + n);
		name[n] = 0;
	}
	return name;
}

int qm_tracking(void) { return tracking; }

int qm_track_rid(void)
{
	char when[24];
	if (!tracking || !qd.doc)
		return -1;
	if (track_doc != qd.doc) {
		/* the author's changes in this document from now on, formatting changes too (Doc.rvtrack) */
		ec_now(when);
		track_rid = doc_add_rev(qd.doc, qm_author(), when);
		track_doc = qd.doc;
		qd.doc->rvtrack = (u16)(track_rid >= 0 ? track_rid + 1 : 0);
	}
	return track_rid;
}

void qm_track_toggle(void)
{
	tracking = !tracking;
	track_doc = NULL;
	if (qd.doc)
		qd.doc->rvtrack = 0;
	qm_track_rid();
}

/* a..b gone, or marked as deleted while changes are tracked */
void qm_delete(Pos a, Pos b)
{
	ec_delete(qd.doc, a, b, qm_track_rid());
}

const CharFmt *qm_typing_fmt(void)
{
	static CharFmt t;
	int rid = qm_track_rid();
	if (rid < 0)
		return typing_set ? &typing : NULL;
	/* tracking: new text is the author's insertion */
	t = typing_set ? typing : *para_fmt_before(qd.doc->p[qd.caret.para], qd.caret.off);
	ec_track_fmt(&t, rid);
	return &t;
}
void qm_typing_reset(void) { typing_set = 0; }

/* ---- the menus ---- */
static const int sizes[] = { 9, 10, 12, 14, 18, 24, 36 };       /* points: the Size menu (p. 112) */
#define NSIZES ((int)(sizeof sizes / sizeof sizes[0]))
static const int zooms[] = { 0, 50, 75, 100, 150, 200 };
static int zoom_row = 3;                                         /* View's zoom checked: 100% */

/* The paragraph styles of Format > Paragraph Style and the strip's style pop-up: the ones every
   Quill document has (doc_new makes them, and a .docx read starts from it), by their ids, with the
   names the Amiga's styles gallery shows. Plain Text is left out: it would sit beside Style's
   Plain Text, a different command. */
static const struct { const char *id, *name; } pstyles[] = {
	{ "Normal", "Normal" }, { "Title", "Title" }, { "Subtitle", "Subtitle" },
	{ "Heading1", "Heading 1" }, { "Heading2", "Heading 2" }, { "Heading3", "Heading 3" },
	{ "Quote", "Quote" }, { "ListParagraph", "List Paragraph" },
};
#define NPSTYLES ((int)(sizeof pstyles / sizeof pstyles[0]))
static int pstyle_at[NPSTYLES];          /* each one's index in the document's styles, -1 none */

/* the font families Quill has (the Fonts folder's), each once */
#define MAXFAM 24
static char fam[MAXFAM][48];
static int nfam;

static struct NewMenu menu[320];       /* (Riptide's Mac MUI takes up to 320 rows) */
static int nmenu;
static char size_label[NSIZES][8];

/* the tool strips by the Amiga's tab names (View > Tool Strip and the switcher at the strip's left end) */
static const char *const strip_names[QS_NSTRIPS] = { "Home", "Insert", "Layout", "References", "Review" };

/* the preferences: what View shows, kept between sessions (Guidelines p. 37) */
static struct { int ribbon, ruler, strip, smooth; } prefs = { 1, 1, QS_HOME, 0 };

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
/* (the same for mac/qm_cmds.c's menus) */
static void nm_addc(int type, const char *label, const char *key, int flags, unsigned long id)
{
	nm_add((UBYTE)type, label, key, (UWORD)flags, (ULONG)id);
}

/* How far the menus fold together to fit the menu bar. Eleven titles of Octavo's fit a 640-pixel
   screen in English; a compact Mac's 512 pixels hold seven or eight. As Word 5 for the Mac did on
   that screen, the sizes go into the Font menu (above the families) and References' commands into
   Insert's menu (1); with less room still Layout's commands follow Format's (2), and the styles go
   into the Font menu too (3). Every command stays in a menu, with its key. The titles are measured
   in the menu bar's font, each with the Menu Manager's room around it, against the screen's width less
   the room the System's own menus at the right end take (Help, the Application menu, one more). */
static int menu_fold;
#define TITLE_PAD   13          /* round each title (System 7's menu bar definition) */
#define MBAR_LEFT   (10 + 16 + TITLE_PAD)   /* the Apple menu */
#define MBAR_RIGHT  80          /* Help and the Application menu (67 pixels at 512 in the testbed), and a little */
static int title_w(const char *t)
{
	unsigned char p[80];
	return StringWidth(mac_pstr(p, t)) + TITLE_PAD;
}
static int menu_room(int fold, int screen_w)
{
	static const char *const all[] = { "File", "Edit", "Font", "Size", "menu:Style", "menu:Format", "Insert", "Layout", "References", "Review", "View" };
	int i, w = MBAR_LEFT;
	for (i = 0; i < (int)(sizeof all / sizeof all[0]); i++) {
		const char *t = all[i];
		if (fold >= 1 && (!strcmp(t, "Size") || !strcmp(t, "References")))
			continue;
		if (fold >= 2 && !strcmp(t, "Layout"))
			continue;
		if (fold >= 3 && !strcmp(t, "menu:Style"))
			continue;
		w += title_w(!strncmp(t, "menu:", 5) ? tr_ctx("menu", t + 5) : tr(t));
	}
	return screen_w - MBAR_RIGHT - w;
}
static void menus_fold(int screen_w)
{
	void *save, *wm;
	GetPort(&save);
	GetWMgrPort(&wm);
	SetPort(wm);
	/* measured in the Window Manager port's font as it is, and that left alone: the menu bar is drawn
	   in it (Chicago 12 at 512 pixels in the testbed, a smaller plain face at 640; setting the port's
	   font to the system font's here once made the 640-pixel menu bar Chicago too) */
	for (menu_fold = 0; menu_fold < 3 && menu_room(menu_fold, screen_w) < 0; menu_fold++)
		;
	mm_log("menu bar: %d pixels, titles folded to level %d (%d pixels spare)", screen_w, menu_fold, menu_room(menu_fold, screen_w));
	SetPort(save);
}

static void menu_sizes(void)
{
	int i;
	for (i = 0; i < NSIZES; i++) {
		snprintf(size_label[i], sizeof size_label[i], "%d", sizes[i]);
		nm_add(NM_ITEM, size_label[i], NULL, CHECKIT, (ULONG)(ID_SIZE0 + i));
	}
	BAR();
	nm_add(NM_ITEM, "Grow Font", ">", 0, ID_GROW);
	nm_add(NM_ITEM, "Shrink Font", "<", 0, ID_SHRINK);
}

static void menu_styles(void)
{
	nm_add(NM_ITEM, "Plain Text", "T", CHECKIT, ID_PLAIN);
	BAR();
	nm_add(NM_ITEM, "Bold", "B", CHECKIT, ID_BOLD);
	nm_add(NM_ITEM, "Italic", "I", CHECKIT, ID_ITALIC);
	nm_add(NM_ITEM, "Underline", "U", CHECKIT, ID_UNDERLINE);
	nm_add(NM_ITEM, "Strikethrough", NULL, CHECKIT, ID_STRIKE);
	BAR();
	nm_add(NM_ITEM, "Superscript", "+", CHECKIT, ID_SUPER);
	nm_add(NM_ITEM, "Subscript", "-", CHECKIT, ID_SUB);
	BAR();
	nm_add(NM_ITEM, "All Caps", NULL, CHECKIT, ID_CAPS);
	nm_add(NM_ITEM, "Clear Formatting", NULL, 0, ID_CLEARFMT);
}

/* Format > Font Colour and Highlight: the Amiga ribbon's colour lists (amiga/ribbon.c), as submenus */
#define NFCOLORS 12
static const char *const fcolor_name[NFCOLORS] = { "Automatic", "Black", "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue",
                                                   "Dark Blue", "Purple", "Grey" };
static const unsigned long fcolor_rgb[NFCOLORS] = { COLOR_AUTO, 0x000000, 0xC00000, 0xFF0000, 0xFFA500, 0xBF9000, 0x00B050, 0x008080,
                                                    0x0070C0, 0x002060, 0x7030A0, 0x808080 };
#define NHILITES 8
static const char *const hilite_name[NHILITES] = { "No Colour", "Yellow", "Bright Green", "Turquoise", "Pink", "Blue", "Red", "Grey 25%" };
static const unsigned long hilite_rgb[NHILITES] = { 0, 0xFFFF00, 0x00FF00, 0x00FFFF, 0xFF00FF, 0x0000FF, 0xFF0000, 0xC0C0C0 };

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
	nm_add(NM_ITEM, "Revert", NULL, 0, ID_REVERT);               /* the Guidelines' File menu (pp. 99-109) */
	nm_add(NM_ITEM, "Export PDF...", "D", 0, ID_PDF);
	nm_add(NM_ITEM, "About...", NULL, 0, ID_ABOUT);              /* to the Apple menu */
	nm_add(NM_ITEM, "Quit", "Q", 0, ID_QUIT);
	nm_add(NM_TITLE, "Edit", NULL, 0, 0);                        /* after Undo, Cut, Copy, Paste, Clear */
	nm_add(NM_ITEM, "Redo", "Y", 0, ID_REDO);
	nm_add(NM_ITEM, "Select All", "A", 0, ID_SELALL);
	BAR();
	nm_add(NM_ITEM, "Find...", "F", 0, ID_FIND);                 /* F and G as the Guidelines have them (p. 128) */
	nm_add(NM_ITEM, "Find Again", "G", 0, ID_FINDAGAIN);
	nm_add(NM_ITEM, "Replace...", NULL, 0, ID_REPLACE);
	nm_add(NM_TITLE, "Font", NULL, 0, 0);
	if (menu_fold >= 1) {
		/* (a narrow menu bar) the sizes first, as Word 5 for the Mac had them; the styles too */
		menu_sizes();
		BAR();
		if (menu_fold >= 3) {
			menu_styles();
			BAR();
		}
	}
	for (i = 0; i < nfam; i++)
		nm_add(NM_ITEM, fam[i], NULL, CHECKIT, (ULONG)(ID_FONT0 + i));
	if (menu_fold < 1) {
		nm_add(NM_TITLE, "Size", NULL, 0, 0);
		menu_sizes();
	}
	/* the Guidelines' Style menu (p. 113): the text's style, not a paragraph style (the German
	   table's "Style" is Formatvorlage, a paragraph style; the menu is Stil) */
	if (menu_fold < 3) {
		nm_add(NM_TITLE, tr_ctx("menu", "Style"), NULL, 0, 0);
		menu_styles();
	}
	nm_add(NM_TITLE, tr_ctx("menu", "Format"), NULL, 0, 0);   /* the menu, not the verb */
	nm_add(NM_ITEM, "Font...", NULL, 0, ID_FONTDLG);
	nm_add(NM_ITEM, "Font Colour", NULL, 0, 0);
	for (i = 0; i < NFCOLORS; i++)
		nm_add(NM_SUB, fcolor_name[i], NULL, 0, (ULONG)(ID_FCOLOR0 + i));
	nm_add(NM_ITEM, "Highlight", NULL, 0, 0);
	for (i = 0; i < NHILITES; i++)
		nm_add(NM_SUB, hilite_name[i], NULL, 0, (ULONG)(ID_HILITE0 + i));
	nm_add(NM_ITEM, "Paragraph Style", NULL, 0, 0);              /* a hierarchical menu (p. 79): an attribute list */
	for (i = 0; i < NPSTYLES; i++)
		nm_add(NM_SUB, pstyles[i].name, NULL, CHECKIT, (ULONG)(ID_STYLE0 + i));
	BAR();
	nm_add(NM_ITEM, "Align Left", "[", CHECKIT, ID_LEFT);
	nm_add(NM_ITEM, "Centre", "E", CHECKIT, ID_CENTER);
	nm_add(NM_ITEM, "Align Right", "]", CHECKIT, ID_RIGHT);
	nm_add(NM_ITEM, "Justify", "J", CHECKIT, ID_JUSTIFY);
	BAR();
	nm_add(NM_ITEM, "Bullets", NULL, CHECKIT, ID_BULLETS);
	nm_add(NM_ITEM, "Numbering", NULL, CHECKIT, ID_NUMBERS);
	nm_add(NM_ITEM, "Decrease Indent", "K", 0, ID_OUTDENT);      /* the Amiga's keys, free on the Mac */
	nm_add(NM_ITEM, "Increase Indent", "M", 0, ID_INDENT);
	BAR();
	nm_add(NM_ITEM, "Single", "1", CHECKIT, ID_SINGLE);
	nm_add(NM_ITEM, "1.5 Lines", "5", CHECKIT, ID_ONEHALF);
	nm_add(NM_ITEM, "Double", "2", CHECKIT, ID_DOUBLE);
	if (menu_fold >= 2) {
		BAR();
		qc_menu_layout(nm_addc, 0);                          /* (a narrow menu bar) Layout's commands */
	}
	qc_menus(nm_addc, menu_fold >= 1, menu_fold >= 2);           /* Insert, Layout, References, Review */
	nm_add(NM_TITLE, "View", NULL, 0, 0);
	/* toggles with a check mark (p. 75); Ribbon is what Word for the Mac called its formatting bar */
	nm_add(NM_ITEM, "Ribbon", NULL, CHECKIT | (prefs.ribbon ? CHECKED : 0), ID_RIBBON);
	nm_add(NM_ITEM, "Tool Strip", NULL, 0, 0);                   /* which strip: as the strip's own pop-up */
	for (i = 0; i < QS_NSTRIPS; i++)
		nm_add(NM_SUB, strip_names[i], NULL, CHECKIT | (prefs.strip == i ? CHECKED : 0), (ULONG)(ID_STRIP0 + i));
	nm_add(NM_ITEM, "Ruler", NULL, CHECKIT | (prefs.ruler ? CHECKED : 0), ID_RULER);
	/* the text in the Mac's screen fonts (MacWrite's look) unless this is on: as the PDF draws it */
	nm_add(NM_ITEM, "Smooth Text", NULL, CHECKIT | (prefs.smooth ? CHECKED : 0), ID_SMOOTH);
	BAR();
	nm_add(NM_ITEM, "Fit Page Width", "0", CHECKIT, ID_ZOOMFIT);
	BAR();
	nm_add(NM_ITEM, "50%", NULL, CHECKIT, ID_ZOOM50);
	nm_add(NM_ITEM, "75%", NULL, CHECKIT, ID_ZOOM75);
	nm_add(NM_ITEM, "100%", NULL, CHECKIT | CHECKED, ID_ZOOM100);
	nm_add(NM_ITEM, "150%", NULL, CHECKIT, ID_ZOOM150);
	nm_add(NM_ITEM, "200%", NULL, CHECKIT, ID_ZOOM200);
	nm_add(NM_END, NULL, NULL, 0, 0);
}

/* the menu item of a command (a Menuitem object), NULL if it has none */
static Object *item_of(ULONG id)
{
	return strip ? (Object *)mui_do(strip, MUIM_FindUData, id) : NULL;
}

const char *qm_label(unsigned long id)
{
	if (id >= ID_FONT0 && id < ID_FONT0 + (unsigned long)nfam)
		return fam[id - ID_FONT0];
	if (id >= ID_SIZE0 && id < ID_SIZE0 + NSIZES)
		return size_label[id - ID_SIZE0];
	if (id >= ID_STYLE0 && id < ID_STYLE0 + NPSTYLES)
		return tr(pstyles[id - ID_STYLE0].name);
	if (id >= ID_STRIP0 && id < ID_STRIP0 + QS_NSTRIPS)
		return tr(strip_names[id - ID_STRIP0]);
	{
		const char *c = qc_label(id);
		return c ? c : "";
	}
}

/* ---- the preferences file: "Octavo Preferences" in the System Folder's Preferences folder, a line
   per setting ("ribbon 1"), read at the start and written when a setting changes. Octavo was called
   Quill on the Mac at first: a "Quill Preferences" found there is read once, written again under the
   new name and deleted ---- */
static char prefs_path[300];
static void prefs_save(void);

static void prefs_load(void)
{
	short vref;
	long dir;
	char line[64];
	int v;
	FILE *f;
	if (mac_findfolder(-32768, MAC_TYPE('p', 'r', 'e', 'f'), 1, &vref, &dir) != 0 ||
	    qm_dir_path(vref, dir, prefs_path, (int)sizeof prefs_path - 24) < 0) {
		prefs_path[0] = 0;
		return;
	}
	strcat(prefs_path, "Octavo Preferences");
	if (!(f = fopen(prefs_path, "r"))) {
		char old[300];
		strcpy(old, prefs_path);
		strcpy(strrchr(old, ':') + 1, "Quill Preferences");
		if (!(f = fopen(old, "r")))
			return;
		while (fgets(line, sizeof line, f)) {
			if (sscanf(line, "ribbon %d", &v) == 1)
				prefs.ribbon = v != 0;
			else if (sscanf(line, "ruler %d", &v) == 1)
				prefs.ruler = v != 0;
		}
		fclose(f);
		prefs_save();
		qm_fdelete(old);
		mm_log("Octavo: Quill Preferences moved to Octavo Preferences");
		return;
	}
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "ribbon %d", &v) == 1)
			prefs.ribbon = v != 0;
		else if (sscanf(line, "ruler %d", &v) == 1)
			prefs.ruler = v != 0;
		else if (sscanf(line, "strip %d", &v) == 1 && v >= 0 && v < QS_NSTRIPS)
			prefs.strip = v;
		else if (sscanf(line, "smooth %d", &v) == 1)
			prefs.smooth = v != 0;
	}
	fclose(f);
}

static void prefs_save(void)
{
	FILE *f;
	if (!prefs_path[0] || !(f = fopen(prefs_path, "w")))
		return;
	fprintf(f, "ribbon %d\nruler %d\nstrip %d\nsmooth %d\n", prefs.ribbon, prefs.ruler, prefs.strip, prefs.smooth);
	fclose(f);
	qm_flush(prefs_path);
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

/* what the caret's text and paragraph are, worked out once per change for qm_state */
static struct {
	CharFmt f;
	int font, size, style;      /* the Font, Size and Paragraph Style rows that are it (-1 none) */
	int align, line;            /* AL_; 240ths of a line (0: not auto spacing) */
	int list;                   /* 0 none, 1 bullets, 2 numbers */
	int pstyle;                 /* the paragraph's style, the document's index */
} here;

static void here_now(void)
{
	const ParaFmt *pf = &qd.doc->p[qd.caret.para]->pf;
	int i;
	here.f = *fmt_here();
	here.font = here.size = here.style = -1;
	if (here.f.font < qd.doc->nfonts)
		for (i = 0; i < nfam; i++)
			if (!strcmp(fam[i], qd.doc->fonts[here.f.font]))
				here.font = i;
	for (i = 0; i < NSIZES; i++)
		if (here.f.size == sizes[i] * 2)
			here.size = i;
	for (i = 0; i < NPSTYLES; i++)
		if (pstyle_at[i] == pf->style)
			here.style = i;
	here.pstyle = pf->style;
	here.align = pf->align;
	here.line = pf->linerule == LS_AUTO ? pf->line : 0;
	here.list = 0;
	if (pf->list && pf->list <= qd.doc->nlists)
		here.list = qd.doc->lists[pf->list - 1].lvl[pf->level < 9 ? pf->level : 0].fmt == NF_BULLET ? 1 : 2;
}

unsigned qm_state(unsigned long id)
{
	unsigned flags = here.f.flags & (CF_BOLD | CF_ITALIC | CF_UNDERLINE | CF_STRIKE | CF_SUPER | CF_SUB), st;
	int on = 0, mine;
	if (!qd.doc)
		return 0;
	st = qc_state(id, &mine);
	if (mine)
		return st;
	if (id >= ID_FONT0 && id < ID_FONT0 + (unsigned long)nfam)
		on = here.font == (int)(id - ID_FONT0);
	else if (id >= ID_SIZE0 && id < ID_SIZE0 + NSIZES)
		on = here.size == (int)(id - ID_SIZE0);
	else if (id >= ID_STRIP0 && id < ID_STRIP0 + QS_NSTRIPS)
		on = prefs.strip == (int)(id - ID_STRIP0);
	else if (id >= ID_STYLE0 && id < ID_STYLE0 + NPSTYLES) {
		if (pstyle_at[id - ID_STYLE0] < 0)
			return 0;               /* the document has no such style: dimmed */
		on = here.style == (int)(id - ID_STYLE0);
	} else
		switch (id) {
		case ID_PLAIN: on = !flags; break;
		case ID_BOLD: on = (flags & CF_BOLD) != 0; break;
		case ID_ITALIC: on = (flags & CF_ITALIC) != 0; break;
		case ID_UNDERLINE: on = (flags & CF_UNDERLINE) != 0; break;
		case ID_STRIKE: on = (flags & CF_STRIKE) != 0; break;
		case ID_SUPER: on = (flags & CF_SUPER) != 0; break;
		case ID_SUB: on = (flags & CF_SUB) != 0; break;
		case ID_CAPS: on = (here.f.flags & CF_CAPS) != 0; break;
		case ID_LEFT: case ID_CENTER: case ID_RIGHT: case ID_JUSTIFY:
			on = here.align == (int)(id - ID_LEFT);
			break;
		case ID_SINGLE: on = here.line == 240 || here.line == 0; break;
		case ID_ONEHALF: on = here.line == 360; break;
		case ID_DOUBLE: on = here.line == 480; break;
		case ID_BULLETS: on = here.list == 1; break;
		case ID_NUMBERS: on = here.list == 2; break;
		case ID_REVERT:
			/* only a document read from a file and changed since */
			return qd.path[0] && qd.doc->dirty ? QS_ENABLED : 0;
		case ID_RIBBON: on = prefs.ribbon; break;
		case ID_RULER: on = prefs.ruler; break;
		case ID_SMOOTH: on = prefs.smooth; break;
		case ID_ZOOMFIT: case ID_ZOOM50: case ID_ZOOM75: case ID_ZOOM100: case ID_ZOOM150: case ID_ZOOM200:
			on = zoom_row == (int)(id - ID_ZOOMFIT);
			break;
		}
	return QS_ENABLED | (on ? QS_CHECKED : 0);
}

int qm_popup(int which, unsigned long *first, char *now, int cap)
{
	char t[64];
	if (!qd.doc)
		return 0;
	switch (which) {
	case QP_STYLE:
		*first = ID_STYLE0;
		now[0] = 0;
		if (here.style >= 0)
			snprintf(now, (size_t)cap, "%s", tr(pstyles[here.style].name));
		else if (here.pstyle < qd.doc->nstyles) {
			/* a style of the document's own: its name as the file has it, with a capital */
			utf8_to_macroman(qd.doc->styles[here.pstyle].name, t, sizeof t);
			if (t[0] >= 'a' && t[0] <= 'z')
				t[0] = (char)(t[0] - 32);
			snprintf(now, (size_t)cap, "%s", t);
		}
		return NPSTYLES;
	case QP_FONT:
		*first = ID_FONT0;
		now[0] = 0;
		if (here.f.font < qd.doc->nfonts)
			utf8_to_macroman(qd.doc->fonts[here.f.font], now, cap);
		return nfam;
	case QP_STRIP:
		*first = ID_STRIP0;
		snprintf(now, (size_t)cap, "%s", tr(strip_names[prefs.strip]));
		return QS_NSTRIPS;
	case QP_BREAKS: case QP_SYMBOL: case QP_COLUMNS: case QP_LINENUM:
		now[0] = 0;
		return qc_popup(which, first);
	case QP_SIZE:
		*first = ID_SIZE0;
		/* half points: 11, or 10.5 */
		snprintf(now, (size_t)cap, here.f.size & 1 ? "%d.5" : "%d", here.f.size / 2);
		return NSIZES;
	}
	return 0;
}

/* The menus' check marks and dimming, and the strip, after the caret moved or the text changed:
   every command's qm_state, and only the menu items whose answer changed are set (each is a Menu
   Manager call). last[] holds each answer plus one, 0 before the first. */
void qm_status(void)
{
	static unsigned char last[ID_LAST];
	int i;
	if (!qd.doc || !qd.doc->np)
		return;
	here_now();
	for (i = 0; i < nmenu; i++) {
		ULONG id = (ULONG)menu[i].nm_UserData;
		unsigned now, was;
		Object *it;
		if ((menu[i].nm_Type != NM_ITEM && menu[i].nm_Type != NM_SUB) || !id || id >= ID_LAST)
			continue;
		now = qm_state(id);
		was = last[id] ? last[id] - 1u : ~now;
		if (now == was || !(it = item_of(id)))
			continue;
		if (((now ^ was) & QS_CHECKED) && (menu[i].nm_Flags & CHECKIT))
			mui_set(it, MUIA_Menuitem_Checked, now & QS_CHECKED ? TRUE : FALSE);
		if ((now ^ was) & QS_ENABLED)
			mui_set(it, MUIA_Menuitem_Enabled, now & QS_ENABLED ? TRUE : FALSE);
		last[id] = (unsigned char)(now + 1);
	}
	if (view)
		qv_strip_changed(view);
	qr_update();
}

static void doc_take(Doc *d, const char *path)
{
	int i;
	if (qd.lay)
		lay_free(qd.lay);
	if (qd.doc)
		doc_free(qd.doc);
	qd.doc = d;
	qd.lay = lay_new(d, qd.fonts);
	for (i = 0; i < NPSTYLES; i++)
		pstyle_at[i] = doc_style(d, pstyles[i].id);
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

static int reply_path(const StandardFileReply *r, char *out, int cap);

void qm_alert(int level, const char *fmt, const char *arg)
{
	static const char *const ok[] = { "OK" };
	char t[600];
	snprintf(t, sizeof t, tr(fmt), arg ? arg : "");
	mac_alert(level, t, ok, 1, 0, -1);
	qdlg_settle();
}

Object *qm_view(void) { return view; }

int qm_get_file(char *path, int cap)
{
	StandardFileReply r;
	memset(&r, 0, sizeof r);
	mac_stdgetfile(-1, NULL, &r);       /* every file: pictures from elsewhere come typed '????' */
	return r.sfGood && reply_path(&r, path, cap);
}

static int ask(const char *gadgets, const char *fmt, const char *arg)
{
	ULONG args[1];
	args[0] = (ULONG)arg;
	int r = (int)MUI_RequestA(app, win, 0, (CONST_STRPTR)"Octavo", (CONST_STRPTR)tr(gadgets), (CONST_STRPTR)tr(fmt), args);
	qdlg_settle();
	return r;
}

/* a full Mac path from Standard File's reply (its FSSpec: volume, folder, name) */
static int reply_path(const StandardFileReply *r, char *out, int cap)
{
	if (qm_spec_path(r->sfFile.vRefNum, r->sfFile.parID, r->sfFile.name, out, cap) < 0) {
		mm_log("Octavo: no path for \"%.*s\" (folder %ld on volume %d)", r->sfFile.name[0], (const char *)r->sfFile.name + 1,
		       (long)r->sfFile.parID, r->sfFile.vRefNum);
		return 0;
	}
	return 1;
}

static int save_to(const char *path)
{
	int ok;
	set_watch_cursor(1);
	ok = doc_save_file(qd.doc, path);
	set_watch_cursor(0);
	if (!ok) {
		qm_alert(QA_STOP, "%s couldn't be saved.", path);
		return 0;
	}
	qm_flush(path);
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

extern int qm_mem_pending;
void qm_mem_log(const char *when);
static void open_file(const char *path)
{
	unsigned lost = 0;
	char err[200];
	Doc *d;
	set_watch_cursor(1);
	/* the document shown gives up its layout and its pages drawn first (the most of what it holds: a
	   long one's layout is megabytes), so the next is read in their room; they are made again if it
	   can't be read */
	if (view)
		qv_reset(view);
	if (qd.lay) {
		lay_free(qd.lay);
		qd.lay = NULL;
	}
	d = doc_load_file(path, &lost, err, sizeof err);
	set_watch_cursor(0);
	if (!d) {
		qd.lay = lay_new(qd.doc, qd.fonts);
		if (view)
			qv_changed(view, 0);
		qm_alert(QA_STOP, "%s couldn't be read.", path);
		return;
	}
	doc_clear_undo(d);
	doc_take(d, path);
	qm_mem_log("document read");
	qm_mem_pending = 1;
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
		qm_alert(QA_STOP, "%s couldn't be saved.", path);
		return;
	}
	qm_flush(path);
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
		typing.flags = (u8)((typing.flags & ~mask) | (v->flags & mask));     /* (CM_ and CF_ bits are the same) */
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

/* the selection's paragraphs, first and last */
static void sel_paras(u32 *a, u32 *b)
{
	*a = qd.anchor.para;
	*b = qd.caret.para;
	if (*a > *b) {
		u32 t = *a;
		*a = *b;
		*b = t;
	}
}

/* Bullets or Numbering, as the Amiga's view does it (amiga/view.c list_toggle): on, continuing a list
   of the kind just above or starting one; off when the first paragraph is in such a list already,
   back to its style's indents */
static void list_toggle(int numbered)
{
	Doc *doc = qd.doc;
	u32 a, b;
	ParaFmt pf;
	int on;
	sel_paras(&a, &b);
	pf = doc->p[a]->pf;
	on = pf.list && pf.list <= doc->nlists && ((doc->lists[pf.list - 1].lvl[0].fmt == NF_BULLET) != numbered);
	if (on) {
		const Style *st = &doc->styles[pf.style < doc->nstyles ? pf.style : 0];
		pf.list = 0;
		pf.level = 0;
		pf.left = st->pf.left;
		pf.first = st->pf.first;
	} else {
		int l = 0;
		if (a > 0) {
			const ParaFmt *q = &doc->p[a - 1]->pf;
			if (q->list && q->list <= doc->nlists && (doc->lists[q->list - 1].lvl[0].fmt != NF_BULLET) == numbered)
				l = q->list;
		}
		if (!l && !(l = doc_new_list(doc, numbered)))
			return;
		pf.list = (u16)l;
		pf.level = 0;
		pf.left = doc->lists[l - 1].lvl[0].left;
		pf.first = -doc->lists[l - 1].lvl[0].hanging;
	}
	doc_para_format(doc, a, b, PM_LIST | PM_INDENT, &pf);
	qv_changed(view, 0);
}

/* Increase and Decrease Indent (amiga/view.c indent): a list item a level deeper or up, other
   paragraphs half an inch (720 twips) in or out; one undo step */
static void indent(int dir)
{
	Doc *doc = qd.doc;
	u32 i, a, b;
	sel_paras(&a, &b);
	doc_begin(doc, qd.caret);
	for (i = a; i <= b; i++) {
		ParaFmt pf = doc->p[i]->pf;
		if (pf.list && pf.list <= doc->nlists) {
			int lv = pf.level + dir;
			if (lv < 0 || lv > 8)
				continue;
			pf.level = (u8)lv;
			pf.left = doc->lists[pf.list - 1].lvl[lv].left;
			pf.first = -doc->lists[pf.list - 1].lvl[lv].hanging;
			doc_para_format(doc, i, i, PM_LIST | PM_INDENT, &pf);
		} else {
			pf.left += dir * 720;
			if (pf.left < 0)
				pf.left = 0;
			doc_para_format(doc, i, i, PM_INDENT, &pf);
		}
	}
	doc_end(doc, qd.caret);
	qv_changed(view, 0);
}

/* Format > Font...: the family, size and styles of the selection (or of what is typed next) */
static void font_dialog(void)
{
	enum { CHECKS = 6 };
	static const char *const names[CHECKS] = { "Bold", "Italic", "Underline", "Strikethrough", "Superscript", "Subscript" };
	static const unsigned bits[CHECKS] = { CF_BOLD, CF_ITALIC, CF_UNDERLINE, CF_STRIKE, CF_SUPER, CF_SUB };
	DItem it[2 + MAXFAM + 2 + CHECKS + 2];
	int n = 0, k, font = here.font, on[CHECKS], shown = nfam < 8 ? nfam : 8, y, size, b;
	char sz[8];
	CharFmt cf;
	unsigned mask = CM_SIZE | CM_BOLD | CM_ITALIC | CM_UNDERLINE | CM_STRIKE | CM_SUPER | CM_SUB;
	memset(it, 0, sizeof it);
	/* as many families as the screen has room for (a compact Mac's 342 lines: seven) */
	while (shown > 1 && 41 + 18 * shown + 10 + 30 + 18 * 3 + 14 + 33 > qdlg_room())
		shown--;
	if (font >= shown)
		font = -1;
#define ADD(K, X, Y, W, L) (it[n].kind = (K), it[n].x = (X), it[n].y = (Y), it[n].w = (W), it[n].label = (L), &it[n++])
	ADD(DI_TEXT, 13, 13, 304, "Font");
	ADD(DI_TEXT, 13, 41, 60, "Font:");
	for (k = 0; k < shown; k++) {
		DItem *r = ADD(DI_RADIO, 80, 41 + 18 * k, 230, fam[k]);
		r->raw = 1;
		r->value = &font;
		r->ord = k;
	}
	y = 41 + 18 * shown + 10;
	ADD(DI_TEXT, 13, y, 60, "Size:");
	{
		DItem *e = ADD(DI_EDIT, 80, y, 40, NULL);
		snprintf(sz, sizeof sz, here.f.size & 1 ? "%d.5" : "%d", here.f.size / 2);
		e->text = sz;
		e->cap = sizeof sz;
	}
	y += 30;
	for (k = 0; k < CHECKS; k++) {
		DItem *c = ADD(DI_CHECK, k < 3 ? 13 : 165, y + 18 * (k % 3), 150, names[k]);
		on[k] = (here.f.flags & bits[k]) != 0;
		c->value = &on[k];
	}
	y += 18 * 3 + 14;
	ADD(DI_BUTTON, 247, y, 70, "OK");
	ADD(DI_BUTTON, 165, y, 70, "_Cancel");
#undef ADD
	for (;;) {
		i32 tw;
		b = qdlg_run(330, y + 33, it, n, n - 2, n - 1);
		if (b != n - 2)
			return;
		/* the size in points (halves allowed): ec_measure gives twentieths */
		if (ec_measure(sz, EU_PT, &tw) && tw >= 20 && tw <= 32760 && tw % 10 == 0) {
			size = (int)(tw / 10);
			break;
		}
		qm_alert(QA_STOP, "The size must be a number of points from 1 to 1638 (halves allowed).", NULL);
	}
	memset(&cf, 0, sizeof cf);
	cf.size = (u16)size;
	for (k = 0; k < CHECKS; k++)
		if (on[k])
			cf.flags |= (u8)bits[k];
	if (cf.flags & CF_SUPER)
		cf.flags &= (u8)~CF_SUB;           /* (not both) */
	if (font >= 0) {
		cf.font = (u8)doc_font(qd.doc, fam[font]);
		mask |= CM_FONT;
	}
	char_format(mask, &cf);
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
		qm_delete(a, b);
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
			qm_delete(a, b);
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
			qm_delete(a, b);
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
	if (qc_command(id))
		return;
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
	if (id >= ID_STRIP0 && id < ID_STRIP0 + QS_NSTRIPS) {
		/* another strip: shown (with the strip itself, if View > Ribbon had hidden it), remembered */
		prefs.strip = (int)(id - ID_STRIP0);
		qs_select(prefs.strip);
		if (!prefs.ribbon) {
			prefs.ribbon = 1;
			qv_chrome(view, prefs.ribbon, prefs.ruler);
		} else
			qv_strip_switched(view);
		prefs_save();
		qm_status();
		return;
	}
	if (id >= ID_FCOLOR0 && id < ID_FCOLOR0 + NFCOLORS) {
		cf.color = (u32)fcolor_rgb[id - ID_FCOLOR0];
		char_format(CM_COLOR, &cf);
		return;
	}
	if (id >= ID_HILITE0 && id < ID_HILITE0 + NHILITES) {
		cf.hl = id == ID_HILITE0 ? 0 : (u32)(HL_ON | hilite_rgb[id - ID_HILITE0]);
		char_format(CM_HL, &cf);
		return;
	}
	if (id >= ID_STYLE0 && id < ID_STYLE0 + NPSTYLES) {
		u32 a, b;
		if (pstyle_at[id - ID_STYLE0] < 0)
			return;
		sel_paras(&a, &b);
		if (doc_apply_style(qd.doc, a, b, pstyle_at[id - ID_STYLE0])) {
			typing_set = 0;
			qv_changed(view, 0);
		}
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
	case ID_REVERT: {
		/* back to the file as last saved, after asking (a caution: the changes go) */
		char path[sizeof qd.path];
		const char *n = strrchr(qd.path, ':');
		if (!qd.path[0] || !qd.doc->dirty)
			break;
		if (ask("Revert|*Cancel", "Revert to the saved version of %s? The changes made since are lost.", n ? n + 1 : qd.path) != 1)
			break;
		strcpy(path, qd.path);
		open_file(path);
		break;
	}
	case ID_SAVEAS: save_as(); break;
	case ID_PDF: cmd_pdf(); break;
	case ID_ABOUT:
		/* (no line breaks inside a paragraph: the alert wraps the text itself) */
		qm_alert(QA_NOTE, "Octavo 0.15\n\n%s", tr("Octavo is Quill for the classic Mac OS: a word processor that reads and writes Word documents (.docx).\n\n"
		                               "Text is measured with the Liberation and Carlito/Caladea fonts (SIL Open Font License), so documents break "
		                               "their lines as they do in Word; on the screen it is drawn in the Mac's own fonts."));
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
	case ID_CAPS: char_flag(CF_CAPS); break;
	case ID_CLEARFMT:
		/* back to the paragraph style's plain look (the Amiga's Clear All Formatting) */
		cf.color = COLOR_AUTO;
		char_format(CM_BOLD | CM_ITALIC | CM_UNDERLINE | CM_STRIKE | CM_SUPER | CM_SUB | CM_CAPS | CM_COLOR | CM_HL, &cf);
		break;
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
	case ID_BULLETS: case ID_NUMBERS: list_toggle(id == ID_NUMBERS); break;
	case ID_FONTDLG: font_dialog(); break;
	case ID_OUTDENT: case ID_INDENT: indent(id == ID_INDENT ? 1 : -1); break;
	case ID_RIBBON: case ID_RULER:
		if (id == ID_RIBBON)
			prefs.ribbon = !prefs.ribbon;
		else
			prefs.ruler = !prefs.ruler;
		prefs_save();
		qv_chrome(view, prefs.ribbon, prefs.ruler);
		qm_status();
		break;
	case ID_SMOOTH:
		prefs.smooth = !prefs.smooth;
		prefs_save();
		qv_smooth(view, prefs.smooth);
		qm_status();
		break;
	case ID_ZOOMFIT: case ID_ZOOM50: case ID_ZOOM75: case ID_ZOOM100: case ID_ZOOM150: case ID_ZOOM200:
		zoom_row = (int)(id - ID_ZOOMFIT);
		qv_zoom(view, zooms[zoom_row]);
		qm_status();
		break;
	}
}

void qm_command(unsigned long id)
{
	command((ULONG)id);
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
	int n;
	if (!h || !*h)
		return;
	p = *h;
	n = *(short *)(p + 2);
	if (n < 1)
		return;
	vref = *(short *)(p + 4);              /* a working directory: its folder (directory 0) */
	if (qm_spec_path(vref, 0, p + 12, path, (int)sizeof path) < 0) {
		mm_log("Octavo: no path for the Finder's document (working directory %d)", vref);
		return;
	}
	mm_log("Octavo: the Finder opens %s", path);
	open_file(path);
}

/* Apple events (System 7): Octavo is high-level-event aware (its SIZE resource says so, mac/mksize.py),
   so the Finder sends it the required events: Open Application, Open Documents (a document
   double-clicked or dropped on Octavo's icon, whether Octavo runs already or is started by it),
   Print Documents (opened: Octavo does not print) and Quit (Shut Down, Restart). Riptide's event
   loop leaves high-level events alone, and an event not accepted stays in the queue, so the loop
   below takes them before each turn of MUI's: WaitNextEvent for high-level events only, then
   AEProcessAppleEvent, which calls qm_ae_event through the Pascal thunk in qm_thunks.S. */
short qm_ae_install(long cls, long id, void *handler, long refcon);
short qm_ae_process(const void *ev);
short qm_ae_getparamdesc(const void *ev, long key, long type, void *desc);
short qm_ae_countitems(const void *list, long *n);
short qm_ae_getnthptr(const void *list, long i, long type, long *kw, long *typ, void *data, long max, long *actual);
short qm_ae_disposedesc(void *desc);
void qm_ae_handler(void);
long mac_gestalt(long selector, long *response);
#define AE_CLASS     MAC_TYPE('a', 'e', 'v', 't')
#define HLE_EVENT    23                 /* kHighLevelEvent */
#define HLE_MASK     0x0400             /* highLevelEventMask */
static int ae_ready, ae_quit;
static int ae_opened;                   /* documents opened by an Open Documents event */

short qm_ae_event(const void *ev, long refcon);
short qm_ae_event(const void *ev, long refcon)
{
	long kw, typ, n = 0, got;
	struct { void *type_; void *data; } list = { 0, 0 };
	struct { short vref; long dir; unsigned char name[64]; } __attribute__((packed)) fss;
	char path[300];
	unsigned long eid = (unsigned long)refcon;     /* the event's ID, given as the handler's refcon */
	if (eid == MAC_TYPE('o', 'a', 'p', 'p'))
		return 0;
	if (eid == MAC_TYPE('q', 'u', 'i', 't')) {
		ae_quit = 1;            /* the main loop asks to save first, as Quit does */
		return 0;
	}
	/* Open Documents and Print Documents: the direct object, a list of files */
	if (qm_ae_getparamdesc(ev, MAC_TYPE('-', '-', '-', '-'), MAC_TYPE('l', 'i', 's', 't'), &list) != 0)
		return -1701;           /* errAEDescNotFound */
	if (qm_ae_countitems(&list, &n) == 0 && n > 0 &&
	    qm_ae_getnthptr(&list, 1, MAC_TYPE('f', 's', 's', ' '), &kw, &typ, &fss, (long)sizeof fss, &got) == 0) {
		if (qm_spec_path(fss.vref, fss.dir, fss.name, path, (int)sizeof path) == 0) {
			mm_log("Octavo: an Open Documents event for %s (%ld document%s)", path, n, n == 1 ? "" : "s");
			ae_opened++;
			if (!strcmp(path, qd.path) && qd.doc && !qd.doc->dirty)
				win_show();     /* that one is open already */
			else if (may_close())
				open_file(path);
		} else
			mm_log("Octavo: an Open Documents event for a file with no path (folder %ld)", fss.dir);
	}
	qm_ae_disposedesc(&list);
	return 0;
}

/* the handlers: one, told the event by its refcon (the event ID) */
static void ae_init(void)
{
	long r = 0;
	if (mac_gestalt(MAC_TYPE('e', 'v', 'n', 't'), &r) != 0 || !(r & 1))
		return;                 /* no Apple Event Manager (System 6) */
	ae_ready = qm_ae_install(AE_CLASS, MAC_TYPE('o', 'a', 'p', 'p'), (void *)qm_ae_handler, (long)MAC_TYPE('o', 'a', 'p', 'p')) == 0;
	qm_ae_install(AE_CLASS, MAC_TYPE('o', 'd', 'o', 'c'), (void *)qm_ae_handler, (long)MAC_TYPE('o', 'd', 'o', 'c'));
	qm_ae_install(AE_CLASS, MAC_TYPE('p', 'd', 'o', 'c'), (void *)qm_ae_handler, (long)MAC_TYPE('p', 'd', 'o', 'c'));
	qm_ae_install(AE_CLASS, MAC_TYPE('q', 'u', 'i', 't'), (void *)qm_ae_handler, (long)MAC_TYPE('q', 'u', 'i', 't'));
	mm_log("Octavo: Apple event handlers %s", ae_ready ? "installed" : "not installed");
}

/* the high-level events waiting, each handled */
static void ae_take(void)
{
	EventRecord ev;
	int guard = 0;
	if (!ae_ready)
		return;
	/* asked first, without WaitNextEvent: that gives the other programs a turn each time round the
	   loop, and woke Octavo twice as often while idle */
	if (!EventAvail(HLE_MASK, &ev))
		return;
	while (guard++ < 8 && WaitNextEvent(HLE_MASK, &ev, 0, NULL) && ev.what == HLE_EVENT)
		qm_ae_process(&ev);
}

/* ---- for the Amiga's ribbon, status bar and File page (mac/qm_ribbon.c, the experimental ribbon tree) ---- */
Doc *qm_doc(void) { return qd.doc; }
FontSet *qm_fonts(void) { return qd.fonts; }
void qm_sel(Pos *caret, Pos *anchor)
{
	*caret = qd.caret;
	*anchor = qd.anchor;
}
const CharFmt *qm_fmt_here(void) { return fmt_here(); }
void qm_char_format(unsigned mask, const CharFmt *v) { char_format(mask, v); }
void qm_para_format(unsigned mask, const ParaFmt *pf) { para_format(mask, pf); }
void qm_apply_style(int style)
{
	u32 a, b;
	sel_paras(&a, &b);
	if (doc_apply_style(qd.doc, a, b, style)) {
		typing_set = 0;
		qv_changed(view, 0);
	}
}
void qm_edit(int cmd) { edit_action(cmd); }
void qm_page_info(unsigned long *page, unsigned long *pages, long *zoom)
{
	qv_page_info(view, page, pages, zoom);
}

void qm_mem_log(const char *when);
void qm_measure_loop(void);
void mm_gfx_lib_init(void);
void mm_screen_init(void *qd_thePort_addr);
void qm_ashim_init(void);
void mm_paths_init(void);
void mm_log(const char *fmt, ...);
#define LM_MBARHEIGHT (*(volatile short *)0xBAA)

int main(void)
{
	void *a5;
	short *sb;
	ULONG sigs = 0;
	Object *root;
	__asm__ volatile("move.l %%a5,%0" : "=r"(a5));
	mm_gfx_lib_init();
	mm_classes_init();
	qm_ashim_init();
	sb = (short *)((char *)*(void **)a5 - 116);       /* QuickDraw's screenBits.bounds */
	mm_gfx_init(sb[2] - sb[0] - LM_MBARHEIGHT - 20 < 560);
	mm_screen_init(*(void **)a5);
	mm_paths_init();
	lang_set(lang_mac_system());
	mm_log("Octavo starts");

	/* the fonts: the Fonts folder beside Octavo, found from Octavo's own file (not the default
	   folder, which is Octavo's only when the Finder made it so and which Standard File moves) */
	if (qm_app_dir(qm_appdir, (int)sizeof qm_appdir - 40) < 0)
		strcpy(qm_appdir, ":");
	mm_log("Octavo's folder: %s", qm_appdir);
	if (!(qd.fonts = calloc(1, sizeof *qd.fonts)))
		return 20;
	{
		char fdir[300];
		snprintf(fdir, sizeof fdir, "%sFonts:", qm_appdir);
		if (!fonts_init(qd.fonts, fdir))
			mm_log("Octavo: no fonts in %s (fonts.list)", fdir);
	}
	families();
	prefs_load();
	menus_fold(sb[3] - sb[1]);
	menus_build();
	mm_edit_action = edit_action;
	mm_edit_state = edit_state;

	/* Octavo: the Mac's name for Quill (a classic Mac program called Quill exists already) */
	app = mui_new(MUIC_Application, MUIA_Application_Title, (ULONG)"Octavo", MUIA_Application_Base, (ULONG)"OCTAVO",
	              MUIA_Application_Version, (ULONG)"$VER: Octavo 0.15 (19.11.2026)", TAG_DONE);
	if (!app || !qv_class())
		return 20;
	doc_take(doc_new(), NULL);
	strip = mui_make(MUIO_MenustripNM, (ULONG)menu, 0);
	view = mui_newc(qv_class(), MUIA_Frame, MUIV_Frame_None, TAG_DONE);
	qs_select(prefs.strip);
	if (view) {
		/* the experimental ribbon tree: the Amiga's ribbon over the view (mac/qm_ribbon.c), not the strip */
		qv_chrome(view, 0, prefs.ruler);
		qv_smooth(view, prefs.smooth);
	}
	root = view ? qr_make(view) : NULL;
	win = mui_new(MUIC_Window, MUIA_Window_Title, (ULONG)tr("Untitled"), MUIA_Window_RootObject, (ULONG)(root ? root : view),
	              MUIA_Window_Menustrip, (ULONG)strip, TAG_DONE);
	if (!view || !win)
		return 20;
	mui_do(app, OM_ADDMEMBER, (ULONG)win);
	mui_do(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_CLOSEWIN);
	set_title();
	mui_set(win, MUIA_Window_Open, TRUE);
	{
		/* the menu bar as the Menu Manager placed it (MenuList, low memory 0xA1C: the offset of the
		   last entry, the last title's right edge, then a handle and a left edge per menu): logged, so
		   that menus_fold's sums can be checked against it */
		short **ml = *(short ***)0xA1C;
		if (ml && *ml) {
			short *m = *ml;
			int k, n = m[0] / 6;
			char line[200];
			int o = snprintf(line, sizeof line, "menu bar: %d menus, the last title ends at %d; lefts", n, m[1]);
			for (k = 0; k < n && o < (int)sizeof line - 8; k++)
				o += snprintf(line + o, sizeof line - (size_t)o, " %d", *(short *)((char *)m + 6 + 6 * k + 4));
			mm_log("%s", line);
		}
	}
	finder_files();
	ae_init();
	qm_status();
	qm_mem_log("launch");
	for (;;) {
		LONG id;
		qm_measure_loop();
		ae_take();
		id = (LONG)mui_do(app, MUIM_Application_NewInput, (ULONG)&sigs);
		if (ae_quit) {
			ae_quit = 0;
			id = ID_QUIT;
		}
		if (id == MUIV_Application_ReturnID_Quit || id == ID_QUIT) {
			if (may_close())
				break;
			continue;
		}
		if (id && (ULONG)id >= QM_AMIGA_ID)
			qr_command((ULONG)id);
		else if (id)
			command((ULONG)id);
		if (sigs)
			sigs = Wait(sigs);
	}
	mui_set(win, MUIA_Window_Open, FALSE);
	MUI_DisposeObject(app);
	mm_log("Octavo ends");
	return 0;
}
