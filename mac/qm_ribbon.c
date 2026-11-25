/*
 * mac/qm_ribbon.c - the Amiga's ribbon, status bar and File page in Octavo's window (the
 * experimental ribbon tree, docs/mac.md "The experimental ribbon tree").
 *
 * The classes are Quill's own Amiga files compiled for the Mac against Riptide's MUI for the Mac
 * (amiga/ribbon.c with its tabs, groups, lists, keytips and collapsing; theme.c's colours, fonts and
 * icons; panes.c's status bar; backstage.c's File page; the work packages' groups from amiga/wp_*.c
 * through mac/mkwpgroups.py). This file is what the Amiga's main.c and view.c do for them: it puts
 * them in the window, tells them the document's state (MUIM_Ribbon_State, MUIM_Status_Set) and runs
 * their commands, each mapped to the Octavo command that does it (mac/qm_ids.h) or done here on
 * the core. A command the Mac has no way to do yet is dimmed on the ribbon (qr_can, ribbon.c's
 * RIBBON_CAN). GPL-2.0-only, as Quill.
 */
#include "qm_amiga.h"
#include "quill.h"
#include "ids.h"
#include "theme.h"
#include "lang.h"
#include "wphooks.h"
#include "wp_custom.h"
#include "qm_ribbon.h"
#include "../core/layout.h"
#define QM_ID(n) OCT_##n
#include "qm_ids.h"

/* Octavo's side (mac/qm_main.c, qm_view.c), by number */
unsigned qm_state(unsigned long id);
void qm_command(unsigned long id);
const char *qm_label(unsigned long id);
void qv_zoom(Object *view, int percent);
#define QS_ENABLED 1
#define QS_CHECKED 2

static Object *ribbon, *status, *back, *pages, *docgroup, *pageview;
static int in_back;

/* ---- the commands: the Amiga's id, the Octavo command that does the same ---- */
static const struct { UWORD a, m; } direct[] = {
	{ ID_NEW, OCT_NEW }, { ID_OPEN, OCT_OPEN }, { ID_SAVE, OCT_SAVE }, { ID_SAVEAS, OCT_SAVEAS }, { ID_PDF, OCT_PDF },
	{ ID_ABOUT, OCT_ABOUT }, { ID_QUIT, OCT_QUIT }, { ID_REDO, OCT_REDO }, { ID_SELALL, OCT_SELALL },
	{ ID_FIND, OCT_FIND }, { ID_FINDNEXT, OCT_FINDAGAIN }, { ID_REPLACE, OCT_REPLACE },
	{ ID_BOLD, OCT_BOLD }, { ID_ITALIC, OCT_ITALIC }, { ID_UNDERLINE, OCT_UNDERLINE }, { ID_STRIKE, OCT_STRIKE },
	{ ID_SUPER, OCT_SUPER }, { ID_SUB, OCT_SUB }, { ID_GROW, OCT_GROW }, { ID_SHRINK, OCT_SHRINK },
	{ ID_LEFT, OCT_LEFT }, { ID_CENTER, OCT_CENTER }, { ID_RIGHT, OCT_RIGHT }, { ID_JUSTIFY, OCT_JUSTIFY },
	{ ID_SINGLE, OCT_SINGLE }, { ID_ONEHALF, OCT_ONEHALF }, { ID_DOUBLE, OCT_DOUBLE },
	{ ID_BULLETS, OCT_BULLETS }, { ID_NUMBERS, OCT_NUMBERS }, { ID_INDENT, OCT_INDENT }, { ID_OUTDENT, OCT_OUTDENT },
	{ ID_PAGEBREAK, OCT_BREAK0 + 0 }, { ID_COLBREAK, OCT_BREAK0 + 1 }, { ID_SECTBREAK, OCT_BREAK0 + 2 },
	{ ID_ZOOMFIT, OCT_ZOOMFIT }, { ID_PAGEWIDTH, OCT_ZOOMFIT }, { ID_ZOOM75, OCT_ZOOM75 }, { ID_ZOOM100, OCT_ZOOM100 },
	{ ID_ZOOM100B, OCT_ZOOM100 }, { ID_ZOOM150, OCT_ZOOM150 }, { ID_ZOOM200, OCT_ZOOM200 },
	{ ID_PORTRAIT, OCT_PORTRAIT }, { ID_LANDSCAPE, OCT_LANDSCAPE }, { ID_TABLE, OCT_TABLE }, { ID_DATE, OCT_DATE },
	{ ID_ROW_ABOVE, OCT_ROW_ABOVE }, { ID_ROW_BELOW, OCT_ROW_BELOW }, { ID_COL_LEFT, OCT_COL_LEFT },
	{ ID_COL_RIGHT, OCT_COL_RIGHT }, { ID_DEL_ROW, OCT_DEL_ROW }, { ID_DEL_COL, OCT_DEL_COL }, { ID_DEL_TABLE, OCT_DEL_TABLE },
	{ ID_MERGE, OCT_MERGE }, { ID_SPLIT, OCT_SPLIT }, { ID_PICTURE, OCT_PICTURE }, { ID_RULER, OCT_RULER },
	{ ID_TOC, OCT_TOC }, { ID_TOCUPDATE, OCT_TOC }, { ID_ENDNOTE, OCT_ENDNOTE }, { ID_NEXTNOTE, OCT_NEXTNOTE },
	{ ID_SPELL, OCT_SPELL }, { ID_WORDCOUNT, OCT_WORDCOUNT }, { ID_COMMENT, OCT_COMMENT }, { ID_DELCOMMENT, OCT_DELCOMMENT },
	{ ID_PREVCOMMENT, OCT_PREVCOMMENT }, { ID_NEXTCOMMENT, OCT_NEXTCOMMENT }, { ID_TRACK, OCT_TRACK },
	{ ID_PREVCHANGE, OCT_PREVCHANGE }, { ID_NEXTCHANGE, OCT_NEXTCHANGE }, { ID_PAGENUM, OCT_PAGENUM },
	{ ID_PAGESETUPDLG, OCT_PAGESETUP }, { ID_PARADLG, OCT_PARAGRAPH }, { ID_FONTDLG, OCT_FONTDLG },
	{ ID_BOOKMARK, OCT_BOOKMARK }, { ID_XREF, OCT_XREF }, { ID_NOTE, OCT_FOOTNOTE },
	{ ID_CAPS, OCT_CAPS }, { ID_CLEARFMT, OCT_CLEARFMT },
};
#define NDIRECT ((int)(sizeof direct / sizeof direct[0]))
/* the lists and commands done here (with the list's value) */
static const UWORD here_ids[] = {
	ID_UNDO, ID_CUT, ID_COPY, ID_PASTE, ID_FONTDD, ID_SIZEDD, ID_STYLEPICK, ID_ZOOMDD, ID_BREAKSDD, ID_ORIENTDD,
	ID_COLUMNSDD, ID_LINENUMDD, ID_ACCEPTDD, ID_REJECTDD, ID_DELTABLEDD, ID_SPACINGDD, ID_FONTCOLOR, ID_HIGHLIGHT,
	ID_CLEARFMT, ID_INDENTL, ID_INDENTR, ID_SPACEB, ID_SPACEA, ID_FILETAB, ID_BACKSTAGE, ID_ZOOMSET, ID_CAPS,
	ID_MARGINSDD, ID_PAPERDD, ID_LINEBREAK,
};
#define NHERE ((int)(sizeof here_ids / sizeof here_ids[0]))

static int mac_of(unsigned long a)
{
	int i;
	for (i = 0; i < NDIRECT; i++)
		if (direct[i].a == a)
			return direct[i].m;
	return 0;
}

int qr_can(unsigned long a)
{
	int i;
	if (mac_of(a))
		return 1;
	for (i = 0; i < NHERE; i++)
		if (here_ids[i] == a)
			return 1;
	return 0;
}

/* ---- the state the ribbon and status bar show ---- */
static u32 style_sig(const Doc *doc)
{
	int i;
	u32 sig = (u32)doc->nstyles;
	for (i = 0; i < doc->nstyles; i++) {
		const Style *st = &doc->styles[i];
		sig = sig * 31 + st->type + st->quick * 7 + st->cf.font * 131 + st->cf.size * 17 + st->cf.flags * 3 + st->cf.color + st->cf.fx * 5 +
		      (u32)st->name[0] * 11;
	}
	return sig;
}

static int checked(unsigned long m) { return (qm_state(m) & QS_CHECKED) != 0; }
static int usable(unsigned long m) { return (qm_state(m) & QS_ENABLED) != 0; }

void qr_update(void)
{
	static struct MUIP_Ribbon_State rs;
	Doc *doc = qm_doc();
	Pos caret, anchor;
	const CharFmt *f;
	const Para *p;
	if (!ribbon || !doc)
		return;
	qm_sel(&caret, &anchor);
	f = qm_fmt_here();
	p = doc->p[caret.para < doc->np ? caret.para : 0];
	memset(&rs, 0, sizeof rs);
	rs.MethodID = MUIM_Ribbon_State;
	if (checked(OCT_BOLD)) rs.on |= RBIT(RB_BOLD);
	if (checked(OCT_ITALIC)) rs.on |= RBIT(RB_ITALIC);
	if (checked(OCT_UNDERLINE)) rs.on |= RBIT(RB_UNDERLINE);
	if (checked(OCT_STRIKE)) rs.on |= RBIT(RB_STRIKE);
	if (checked(OCT_SUB)) rs.on |= RBIT(RB_SUB);
	if (checked(OCT_SUPER)) rs.on |= RBIT(RB_SUPER);
	if (checked(OCT_BULLETS)) rs.on |= RBIT(RB_BULLETS);
	if (checked(OCT_NUMBERS)) rs.on |= RBIT(RB_NUMBERS);
	if (usable(OCT_ROW_ABOVE)) rs.on |= RBIT(RB_INTABLE);
	if (checked(OCT_TRACK)) rs.on |= RBIT(RB_TRACK);
	if (checked(OCT_RULER)) rs.on |= RBIT(RB_RULER);
	if (doc_can_undo(doc)) rs.on |= RBIT(RB_CANUNDO);
	if (doc_can_redo(doc)) rs.on2 |= RBIT(RB_CANREDO - 32);
	if (pos_cmp(caret, anchor)) rs.on |= RBIT(RB_SELECTION);
	if (checked(OCT_LEFT)) rs.on |= RBIT(RB_ALIGN0);
	if (checked(OCT_CENTER)) rs.on |= RBIT(RB_ALIGN1);
	if (checked(OCT_RIGHT)) rs.on |= RBIT(RB_ALIGN2);
	if (checked(OCT_JUSTIFY)) rs.on |= RBIT(RB_ALIGN3);
	if (usable(OCT_NEXTCOMMENT)) rs.on |= RBIT(RB_COMMENTS);
	if (usable(OCT_NEXTCHANGE)) rs.on |= RBIT(RB_CHANGES);
	rs.font = f && f->font < doc->nfonts ? doc->fonts[f->font] : "";
	rs.size = f ? f->size : 22;
	rs.style = p->pf.style;
	rs.left = p->pf.left;
	rs.right = p->pf.right;
	rs.before = p->pf.before;
	rs.after = p->pf.after;
	rs.metric = doc->page.width != 12240;
	rs.doc = doc;
	rs.fs = qm_fonts();
	rs.vmode = VM_PRINT;
	rs.stylesig = style_sig(doc);
	DoMethodA(ribbon, (Msg)&rs);
	if (status) {
		struct MUIP_Status_Set ss;
		unsigned long pg = 0, npg = 1;
		long zoom = 100;
		qm_page_info(&pg, &npg, &zoom);
		ss.MethodID = MUIM_Status_Set;
		ss.page = (u32)(pg ? pg - 1 : 0);
		ss.pages = (u32)npg;
		ss.complete = 1;
		ss.words = ~0UL;
		ss.zoom = zoom;
		ss.vmode = VM_PRINT;
		DoMethodA(status, (Msg)&ss);
	}
}

/* ---- the File page ---- */
void qr_backstage(int on)
{
	if (!pages || on == in_back)
		return;
	in_back = on;
	mm_log("the File page %s", on ? "shown" : "left");
	if (on && back) {
		static struct MUIP_Back_Set bs;
		static char info[200];
		Doc *doc = qm_doc();
		unsigned long pg = 0, npg = 1;
		long zoom;
		qm_page_info(&pg, &npg, &zoom);
		snprintf(info, sizeof info, "%s\t%lu\n%s\t%d", tr("Pages"), npg, tr("Paragraphs"), doc ? (int)doc->np : 0);
		memset(&bs, 0, sizeof bs);
		bs.MethodID = MUIM_Back_Set;
		bs.info = info;
		bs.username = "";
		bs.metric = doc && doc->page.width != 12240;
		bs.lang = lang_current();
		DoMethodA(back, (Msg)&bs);
	}
	SetAttrs(pages, MUIA_Group_ActivePage, on ? 1 : 0, TAG_DONE);
}

/* ---- running a command ---- */
static void char_flag(unsigned mask, u32 color, u32 hl)
{
	CharFmt cf;
	memset(&cf, 0, sizeof cf);
	cf.color = color;
	cf.hl = hl;
	qm_char_format(mask, &cf);
}

static i32 list_twips(const char *s, int metric)
{
	/* "1.5 cm", "12 pt": the list's entries as twips */
	long whole = 0, frac = 0, div = 1;
	while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
	if (*s == '.')
		for (s++; *s >= '0' && *s <= '9'; s++) { frac = frac * 10 + (*s - '0'); div *= 10; }
	while (*s == ' ') s++;
	if (*s == 'p')
		return (i32)(whole * 20 + frac * 20 / div);
	(void)metric;
	return (i32)((whole * div + frac) * 567 / div);
}

int qr_command(unsigned long id)
{
	unsigned long a;
	LONG v = 0;
	int m;
	Doc *doc = qm_doc();
	if (id < QM_AMIGA_ID)
		return 0;
	a = id - QM_AMIGA_ID;
	mm_log("ribbon command %lu", a);
	if (ribbon)
		get(ribbon, MUIA_Ribbon_Value, &v);
	if (in_back && a != ID_FILETAB && a != ID_BACKSTAGE && a != ID_INFO && a != ID_OPTIONS && a != ID_TEMPLATE && a != ID_RECENT)
		qr_backstage(0);        /* a command from the File page: the document again first */
	if ((m = mac_of(a))) {
		qm_command((unsigned long)m);
		qr_update();
		return 1;
	}
	switch (a) {
	case ID_UNDO: qm_edit(0); break;
	case ID_CUT: qm_edit(2); break;
	case ID_COPY: qm_edit(3); break;
	case ID_PASTE: qm_edit(4); break;
	case ID_FILETAB: qr_backstage(1); break;
	case ID_BACKSTAGE: qr_backstage(0); break;
	case ID_FONTDD: {
		const char *name = ribbon_font_name((int)v);
		CharFmt cf;
		if (!name || !doc)
			break;
		memset(&cf, 0, sizeof cf);
		cf.font = (u8)doc_font(doc, name);
		qm_char_format(CM_FONT, &cf);
		break;
	}
	case ID_SIZEDD: {
		CharFmt cf;
		memset(&cf, 0, sizeof cf);
		cf.size = (u16)v;           /* half-points */
		if (v > 0)
			qm_char_format(CM_SIZE, &cf);
		break;
	}
	case ID_STYLEPICK:
		if (doc && v >= 0 && v < doc->nstyles)
			qm_apply_style((int)v);
		break;
	case ID_ZOOMDD: {
		static const UWORD z[] = { OCT_ZOOMFIT, OCT_ZOOM50, OCT_ZOOM75, OCT_ZOOM100, 0, OCT_ZOOM150, OCT_ZOOM200 };
		if (v >= 0 && v < 7 && z[v])
			qm_command(z[v]);
		else if (v == 4 && pageview)
			qv_zoom(pageview, 125);
		break;
	}
	case ID_ZOOMSET: {
		LONG z = 100;
		if (status)
			get(status, MUIA_Status_Zoom, &z);
		if (pageview)
			qv_zoom(pageview, (int)z);
		break;
	}
	case ID_BREAKSDD: {
		static const signed char b[] = { 0, 1, -1, 2, 3, 2, 2 };   /* page, column, (line), next page, continuous, even, odd */
		if (v >= 0 && v < 7 && b[v] >= 0)
			qm_command((unsigned long)(OCT_BREAK0 + b[v]));
		break;
	}
	case ID_LINEBREAK:
		break;
	case ID_ORIENTDD: qm_command(v ? OCT_LANDSCAPE : OCT_PORTRAIT); break;
	case ID_COLUMNSDD:
		if (v >= 0 && v < 3)
			qm_command((unsigned long)(OCT_COLS0 + v));
		else if (v == 6)
			qm_command(OCT_PAGESETUP);
		break;
	case ID_LINENUMDD: {
		static const signed char ln[] = { LN_NONE, LN_CONT, LN_PAGE, LN_SECTION };
		if (v >= 0 && v < 4)
			qm_command((unsigned long)(OCT_LINENUM0 + ln[v]));
		break;
	}
	case ID_ACCEPTDD: qm_command(v == 2 ? OCT_ACCEPTALL : OCT_ACCEPT); if (v == 0) qm_command(OCT_NEXTCHANGE); break;
	case ID_REJECTDD: qm_command(v == 2 ? OCT_REJECTALL : OCT_REJECT); if (v == 0) qm_command(OCT_NEXTCHANGE); break;
	case ID_DELTABLEDD:
		if (v >= 0 && v < 3)
			qm_command((unsigned long)(OCT_DEL_ROW + v));
		break;
	case ID_SPACINGDD: {
		static const UWORD s[] = { OCT_SINGLE, 0, OCT_ONEHALF, OCT_DOUBLE };
		if (v >= 0 && v < 4 && s[v])
			qm_command(s[v]);
		else if (v >= 0 && v < 6) {
			ParaFmt pf;
			static const i16 lines[] = { 240, 276, 360, 480, 600, 720 };
			memset(&pf, 0, sizeof pf);
			pf.line = lines[v];
			pf.linerule = LS_AUTO;
			qm_para_format(PM_LINE, &pf);
		}
		break;
	}
	case ID_FONTCOLOR:
		if ((ULONG)v == COLOR_MORE)
			break;
		char_flag(CM_COLOR, (u32)v, 0);
		break;
	case ID_HIGHLIGHT:
		char_flag(CM_HL, 0, (ULONG)v == 0xFF000000UL || (ULONG)v == COLOR_AUTO ? 0 : (HL_ON | ((u32)v & 0xFFFFFF)));
		break;
	case ID_CLEARFMT: {
		CharFmt cf;
		memset(&cf, 0, sizeof cf);
		cf.color = COLOR_AUTO;
		qm_char_format(CM_BOLD | CM_ITALIC | CM_UNDERLINE | CM_STRIKE | CM_SUPER | CM_SUB | CM_COLOR | CM_HL, &cf);
		break;
	}
	case ID_CAPS: {
		const CharFmt *f = qm_fmt_here();
		CharFmt cf;
		memset(&cf, 0, sizeof cf);
		cf.flags = (u8)(f && (f->flags & CF_CAPS) ? 0 : CF_CAPS);
		qm_char_format(CM_CAPS, &cf);
		break;
	}
	case ID_INDENTL: case ID_INDENTR: case ID_SPACEB: case ID_SPACEA: {
		/* the Layout tab's fields: an entry of their lists (indents in cm, spaces in points) */
		static const char *const ind[] = { "0 cm", "0.5 cm", "1 cm", "1.5 cm", "2 cm", "2.5 cm", "3 cm", "4 cm" };
		static const char *const sp[] = { "0 pt", "3 pt", "6 pt", "8 pt", "10 pt", "12 pt", "18 pt", "24 pt" };
		ParaFmt pf;
		Pos c, an;
		const Para *p;
		if (!doc || v < 0 || v > 7)
			break;
		qm_sel(&c, &an);
		p = doc->p[c.para];
		pf = p->pf;
		if (a == ID_INDENTL) pf.left = list_twips(ind[v], 1);
		if (a == ID_INDENTR) pf.right = list_twips(ind[v], 1);
		if (a == ID_SPACEB) pf.before = list_twips(sp[v], 0);
		if (a == ID_SPACEA) pf.after = list_twips(sp[v], 0);
		qm_para_format(a == ID_INDENTL || a == ID_INDENTR ? PM_INDENT : PM_SPACING, &pf);
		break;
	}
	case ID_MARGINSDD: case ID_PAPERDD:
		qm_command(OCT_PAGESETUP);
		break;
	case ID_INFO: case ID_OPTIONS: case ID_TEMPLATE: case ID_RECENT:
		break;
	default:
		return 1;               /* (dimmed on the ribbon: nothing to do) */
	}
	qr_update();
	return 1;
}

/* ---- the window's contents ---- */
Object *qr_make(Object *view)
{
	struct MUI_CustomClass *rc = ribbon_class(), *sc = status_class(), *bc = backstage_class();
	pageview = view;
	if (!rc || !view)
		return NULL;
	ribbon = NewObject(rc->mcc_Class, NULL, TAG_DONE);
	status = sc ? NewObject(sc->mcc_Class, NULL, TAG_DONE) : NULL;
	back = bc ? NewObject(bc->mcc_Class, NULL, TAG_DONE) : NULL;
	if (!ribbon)
		return NULL;
	/* a screen under 400 lines (the compact Macs' 342) starts with the ribbon collapsed to its tabs, as
	   the Amiga's does on demand (a tab double-clicked: WP26); a click on a tab shows its tools again.
	   (The screen's height: the desktop's, below) */
	{
		/* the desktop's height (GrayRgn's box, low memory 0x9EE: rgnSize, then top, left, bottom, right);
		   Riptide's screen says 399 for every screen up to 560 lines, for its own compact layout */
		short **gray = *(short ***)0x9EE;
		if (gray && *gray && (*gray)[3] - (*gray)[1] < 400)
			DoMethod(ribbon, MUIM_Ribbon_Collapse);
	}
	{
		/* how much of the Amiga's ribbon the Mac does (docs/mac.md quotes the log) */
		RibbonCmd *cmds = malloc(700 * sizeof *cmds);
		int i, n = cmds ? ribbon_commands(cmds, 700) : 0, can = 0;
		for (i = 0; i < n; i++)
			can += qr_can(cmds[i].id);
		mm_log("ribbon: %d commands on its tabs, %d of them on the Mac", n, can);
		free(cmds);
	}
	docgroup = MUI_NewObject(MUIC_Group, MUIA_Group_Spacing, 0, MUIA_Group_Child, ribbon, MUIA_Group_Child, view,
	                         status ? MUIA_Group_Child : TAG_IGNORE, status, TAG_DONE);
	if (!back)
		return docgroup;
	pages = MUI_NewObject(MUIC_Group, MUIA_Group_PageMode, TRUE, MUIA_Group_Child, docgroup, MUIA_Group_Child, back, TAG_DONE);
	return pages ? pages : docgroup;
}
