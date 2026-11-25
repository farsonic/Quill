/*
 * mac/qm_ids.h - Octavo's command numbers (the menus' items and the strip's), as QM_ID(NAME): Octavo's
 * own files see them as ID_NAME (mac/quill_mac.h), mac/qm_ribbon.c as OCT_NAME beside the Amiga's
 * ID_NAME of amiga/ids.h, which it maps to these. Include it after defining QM_ID.
 * GPL-2.0-only, as Quill.
 */
enum {
	QM_ID(NEW) = 1, QM_ID(OPEN), QM_ID(SAVE), QM_ID(SAVEAS), QM_ID(PDF), QM_ID(ABOUT), QM_ID(QUIT),
	QM_ID(REDO), QM_ID(SELALL),
	QM_ID(PLAIN), QM_ID(BOLD), QM_ID(ITALIC), QM_ID(UNDERLINE), QM_ID(STRIKE), QM_ID(SUPER), QM_ID(SUB),
	QM_ID(GROW), QM_ID(SHRINK),
	QM_ID(LEFT), QM_ID(CENTER), QM_ID(RIGHT), QM_ID(JUSTIFY), QM_ID(SINGLE), QM_ID(ONEHALF), QM_ID(DOUBLE),
	QM_ID(ZOOMFIT), QM_ID(ZOOM50), QM_ID(ZOOM75), QM_ID(ZOOM100), QM_ID(ZOOM150), QM_ID(ZOOM200),
	QM_ID(CLOSEWIN),
	QM_ID(BULLETS), QM_ID(NUMBERS), QM_ID(OUTDENT), QM_ID(INDENT), QM_ID(RIBBON), QM_ID(RULER), QM_ID(REVERT),
	/* Insert (mac/qm_cmds.c) */
	QM_ID(TABLE), QM_ID(PICTURE), QM_ID(FOOTNOTE), QM_ID(ENDNOTE), QM_ID(DATE), QM_ID(PAGENUM),
	/* Layout > Table, in core/edcmd.h's ET_ order */
	QM_ID(ROW_ABOVE), QM_ID(ROW_BELOW), QM_ID(COL_LEFT), QM_ID(COL_RIGHT), QM_ID(DEL_ROW), QM_ID(DEL_COL), QM_ID(DEL_TABLE), QM_ID(MERGE), QM_ID(SPLIT),
	/* Layout */
	QM_ID(PAGESETUP), QM_ID(PORTRAIT), QM_ID(LANDSCAPE), QM_ID(PARAGRAPH),
	/* Edit > Find, Format > Font... */
	QM_ID(FIND), QM_ID(FINDAGAIN), QM_ID(REPLACE), QM_ID(FONTDLG),
	/* References */
	QM_ID(TOC), QM_ID(NEXTNOTE), QM_ID(CAPTION), QM_ID(BOOKMARK), QM_ID(XREF),
	/* Review */
	QM_ID(SPELL), QM_ID(WORDCOUNT), QM_ID(COMMENT), QM_ID(PREVCOMMENT), QM_ID(NEXTCOMMENT), QM_ID(DELCOMMENT),
	QM_ID(TRACK), QM_ID(PREVCHANGE), QM_ID(NEXTCHANGE), QM_ID(ACCEPT), QM_ID(REJECT), QM_ID(ACCEPTALL), QM_ID(REJECTALL),
	/* View > Smooth Text */
	QM_ID(SMOOTH),
	/* the Amiga ribbon's Font group, in the menus too (the experimental ribbon tree) */
	QM_ID(CAPS), QM_ID(CLEARFMT), QM_ID(SORT), QM_ID(LINEBREAK), QM_ID(ZOOM125),
	QM_ID(BREAK0) = 120,            /* + Insert > Breaks' row (NBREAKS): page, column, section next page, continuous */
	QM_ID(SYM0) = 130,              /* + Insert > Symbol's row */
	QM_ID(COLS0) = 160,             /* + Layout > Columns' row: one, two, three */
	QM_ID(LINENUM0) = 165,          /* + Layout > Line Numbers' row: LN_ */
	QM_ID(SIZE0) = 200,             /* + the Size menu's row */
	QM_ID(FONT0) = 220,             /* + the Font menu's family (24) */
	QM_ID(STYLE0) = 250,            /* + the Paragraph Style submenu's row */
	QM_ID(STRIP0) = 270,            /* + View > Tool Strip's row: the strip shown (QS_) */
	QM_ID(FCOLOR0) = 300,           /* + Format > Font Colour's row (NFCOLORS) */
	QM_ID(HILITE0) = 330,           /* + Format > Highlight's row (NHILITES) */
	QM_ID(CASE0) = 345,             /* + Format > Change Case's row (CASE_ order: sentence, lower, upper, each word, toggle) */
	QM_ID(LAST) = 400
};
