/*
 * quill.h - declarations shared by Quill's Amiga front end (MUI): the page view class, the
 * application, the clipboard.
 *
 * For newcomers to MUI: each custom class here is a BOOPSI class made with MUI_CreateCustomClass.
 * Its attributes (MUIA_) are tags read with get() and written with set(), and "notify" means
 * other objects can watch the attribute with MUIM_Notify. Its methods (MUIM_) are messages sent
 * with DoMethod(obj, MUIM_..., ...), the struct MUIP_ beside each one being the message's layout.
 * All of Quill's tags sit in the TAG_USER range at QTAG, away from MUI's own.
 */
#ifndef QUILL_AMIGA_H
#define QUILL_AMIGA_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include "../core/docx.h"
#include "../core/layout.h"

extern struct Library *MUIMasterBase;

/* the page view's attributes and methods */
#define QTAG (TAG_USER | 0x51560000UL)
/* a colour list's "More Colours..." entry (main.c opens the colour window instead) */
#define COLOR_MORE 0xFE000000UL
#define MUIA_Quill_Doc       (QTAG + 1)     /* Doc *: the document shown (the view owns its layout, not the document) */
#define MUIA_Quill_Toolbar   (QTAG + 2)     /* struct Toolbar *: controls the view keeps up to date */
#define MUIA_Quill_Fonts     (QTAG + 3)     /* FontSet * */
#define MUIA_Quill_Zoom      (QTAG + 4)     /* percent; 0: fit the page width, -1: a whole page, -2: two pages side by side */
#define MUIA_Quill_Scroll    (QTAG + 5)     /* pixels from the top */
#define MUIA_Quill_Changed   (QTAG + 6)     /* (get, notify) the document changed: the window title shows it */
#define MUIA_Quill_Layout    (QTAG + 7)     /* (get) Layout *, brought fully up to date */
#define MUIA_Quill_Caret     (QTAG + 8)     /* (get) Pos *: the caret; (set) Pos *: move it there */
#define MUIA_Quill_Anchor    (QTAG + 9)     /* (get) Pos *: the other end of the selection */
#define MUIA_Quill_Outline   (QTAG + 10)    /* (notify) typing paused after an edit: headings may have changed */
#define MUIA_Quill_InTable   (QTAG + 11)    /* (get, notify) the caret is in a table */
#define MUIA_Quill_ScrollX   (QTAG + 13)    /* pixels scrolled right (a page wider than the view) */
#define MUIA_Quill_LayoutNow (QTAG + 12)   /* (get) Layout * as it is (perhaps not complete), or NULL */
#define MUIA_Quill_ViewMode  (QTAG + 14)    /* (set, get, notify) VM_: how the document is shown */
#define MUIA_Quill_Grid      (QTAG + 15)    /* gridlines over the text */
#define MUIA_Quill_DarkPage  (QTAG + 16)    /* the page dark (colours' lightness inverted on screen) */
#define MUIA_Quill_Object    (QTAG + 17)    /* (get) the selected object's Doc.images index (picture, shape or text box), -1 if none */
#define MUIA_Quill_EditObject (QTAG + 18)   /* (notify) an object double-clicked: its Doc.images index */
#define MUIA_Quill_LinkInfo  (QTAG + 21)    /* (get) struct QLink *: the link at the caret (url "" if none) and its text, or the selected text */
#define MUIA_Quill_CtxPick   (QTAG + 20)    /* (get) the right-button menu's last choice for a ribbon list: (ID_ << 8) | entry (main.c runs it as if picked on the ribbon) */
#define MUIA_Quill_EditStory (QTAG + 19)    /* (notify) a header, footer, footnote or comment double-clicked (SK_ kind),
                                               or Esc while one is edited (0x100); (get) struct QStoryReq * */
struct QStoryReq { int kind, index; u32 page; };   /* index: the note or comment; page: the page clicked */
enum { VM_PRINT, VM_WEB, VM_DRAFT, VM_READ };    /* view modes: print layout, web layout, draft, read mode */
#define MUIM_Quill_Command   (QTAG + 0x100)   /* struct MUIP_Quill_Command: do a CMD_ (below) on the document */
#define MUIM_Quill_Blink     (QTAG + 0x101)   /* the view's own timer tick (an input handler): the caret blinks, idle work is done */
#define MUIM_Quill_Relayout  (QTAG + 0x102)   /* the document lays out afresh and is redrawn */
#define MUIM_Quill_Background (QTAG + 0x103)  /* lays out more of a long document while idle */
#define MUIM_Quill_Print     (QTAG + 0x104)    /* print pages through printer.device: returns 0 if fine, else a message */
struct MUIP_Quill_Print { ULONG MethodID; LONG from, to; LONG dpi; };

struct MUIP_Quill_Command { ULONG MethodID; ULONG cmd; LONG arg; };

enum {
  CMD_BOLD = 1, CMD_ITALIC, CMD_UNDERLINE, CMD_STRIKE, CMD_SUPER, CMD_SUB, CMD_CAPS,
  CMD_ALIGN,                /* arg: AL_ */
  CMD_STYLE,                /* arg: style index */
  CMD_SIZE,                 /* arg: half-points */
  CMD_FONT,                 /* arg: (const char *) family */
  CMD_BULLETS, CMD_NUMBERS, CMD_INDENT, CMD_OUTDENT,
  CMD_SPACING,              /* arg: line spacing in 240ths (240 single, 360 one and a half, 480 double) */
  CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_SELALL, CMD_DELETE,
  CMD_FIND,                 /* arg: (const char *) text: from the caret on, wrapping */
  CMD_PAGEBREAK,
  CMD_TOP,                  /* scroll to the start (a new document) */
  CMD_LINEBREAK,
  CMD_COLOR,                /* arg: 0xRRGGBB or COLOR_AUTO */
  CMD_TABLE,                /* arg: rows << 8 | columns */
  CMD_ROW_ABOVE, CMD_ROW_BELOW, CMD_COL_LEFT, CMD_COL_RIGHT, CMD_DEL_ROW, CMD_DEL_COL, CMD_DEL_TABLE,
  CMD_GOTO,                 /* arg: paragraph: the caret to its start, near the top of the view */
  CMD_TEXT,                 /* arg: (const char *) UTF-8 text typed at the caret ("\n" a new paragraph) */
  CMD_INDENTS,              /* arg: (const i32 *) left, first, right: the paragraphs' indents (the ruler) */
  CMD_TABS,                 /* arg: struct QTabs *: 8 tab stops (0-terminated), their kinds and leaders */
  CMD_PAGESETUP,            /* the page size or margins changed (main changed Doc.page) */
  CMD_END,                  /* the caret to the end */
  CMD_REPLACE,              /* arg: (const char *) the selection becomes this text */
  CMD_FINDX,                /* arg: struct QFind *: find with options */
  CMD_REPLACE1,             /* arg: struct QFind *: replace the selection if it matches, then find the next */
  CMD_REPLACEALL,           /* arg: struct QFind *: replace every match (count in QFind.count) */
  CMD_HEADERROW,            /* the caret's table row repeats at the top of each page (toggle) */
  CMD_PICTURE,              /* arg: Doc.images index: put it in at the caret (narrowed to fit the column) */
  CMD_PICSIZE,              /* arg: percent of the column's width: the selected picture (or the one by the caret) */
  CMD_REFRESH,              /* the controls (ribbon, status bar...) shown afresh */
  CMD_SELECT,               /* arg: Pos[2]: select from the first to the second, shown in the view */
  CMD_PARAFMT,              /* arg: struct QParaFmt *: the selected paragraphs get the masked fields */
  CMD_HIGHLIGHT,            /* arg: 0xRRGGBB, or COLOR_AUTO for none */
  CMD_GROW,                 /* arg: 1 bigger, -1 smaller (the selection, or the word at the caret) */
  CMD_CLEARFMT,             /* back to the paragraph style's character formatting */
  CMD_CASE,                 /* arg: CASE_ */
  CMD_MARKS,                /* formatting marks shown or not */
  CMD_BRUSH,                /* the format painter: on (the formatting at the caret copied) or off */
  CMD_SORT,                 /* the selected paragraphs sorted A to Z */
  CMD_TOC,                  /* a table of contents at the caret, or the one there is brought up to date */
  CMD_NEXTNOTE,             /* the caret to the next footnote's number */
  CMD_MERGE, CMD_SPLIT,     /* the selected cells (one row) merged; a merged cell split */
  CMD_VALIGN,               /* arg: 0 top, 1 centre, 2 bottom: the selected cells' text */
  CMD_SHADE,                /* arg: 0xRRGGBB or COLOR_AUTO: the selected cells' fill */
  CMD_COMMENT,              /* arg: struct QComment *: a comment on the selection (or the word at the caret), or a comment's new text */
  CMD_DELCOMMENT,           /* the comment at the caret goes */
  CMD_NEXTCOMMENT,          /* arg: 1 the next comment, -1 the previous: its range selected */
  CMD_RESOLVE,              /* arg: bit 0 accept (else reject); arg >> 1: 0 the change at the caret (or selected), 1 all, 2 this one and select the next */
  CMD_NEXTCHANGE,           /* arg: 1 the next tracked change, -1 the previous: selected */
  CMD_COLUMNS,              /* arg: 1-3 the caret's section in that many columns; 0 its line between them on or off */
  CMD_SECTBREAK,            /* arg: SB_: a section break at the caret, the next section starting so */
  CMD_COLBREAK,             /* a column break at the caret */
  CMD_LINENUM,              /* arg: LN_: line numbers in every section */
  CMD_PAGEALIGN,            /* arg: VA_: where the text sits on the pages of every section */
  CMD_HYPHENATE,            /* arg: automatic hyphenation on or off */
  CMD_DROPCAP,              /* arg: 0 none, 1 dropped, 2 in the margin, plus the lines it covers << 8 (0: 3): the caret's paragraph */
  CMD_DESIGN,               /* the watermark or page border changed (main changed Doc.wm or Doc.border) */
  /* tables */
  CMD_TSTYLE,               /* arg: Doc.tstyles number (0: none): the caret's table in that style, its look kept */
  CMD_TLOOK,                /* arg: LK_ flags turned over (the style's heading row, banded rows...) */
  CMD_TBORDERS,             /* arg: struct QBorders *: lines on the selected cells (the caret's cell) */
  CMD_TSELECT,              /* arg: 0 the caret's cell, 1 its column, 2 its row, 3 the table */
  CMD_TGRID,                /* table gridlines (where cells have no lines) shown or not */
  CMD_TPROPS,               /* arg: struct QTableProps *: the table's place, cell margins and layout; the selected rows' height */
  CMD_SPLITCELLS,           /* arg: columns << 8 | rows: the caret's cell split */
  CMD_SPLITTABLE,           /* the table split above the caret's row */
  CMD_AUTOFIT,              /* arg: 0 columns to the contents, 1 the table to the window's width, 2 fixed column widths */
  CMD_ROWH,                 /* arg: (i32) Cell.rowh: the selected rows' height (0 automatic, > 0 at least, < 0 exactly) */
  CMD_COLW,                 /* arg: twips: the selected cells' columns that wide */
  CMD_DISTROWS, CMD_DISTCOLS,   /* the selected rows the same height; the selected columns the same width */
  CMD_TSORT,                /* arg: struct QSort * */
  CMD_TOTEXT,               /* arg: the character between cells' text ('\t', ',' ...) */
  CMD_TOTABLE,              /* arg: the character text is split at (0: each paragraph a row of one cell): the selection a table */
  CMD_FORMULA,              /* arg: struct QFormula *: worked out for the caret's cell, its result put in (result back) */
  CMD_COLEDGE,              /* arg: struct QColEdge *: a column edge of the caret's table dragged (ruler or page) */
  CMD_OBJSET,               /* arg: const Image *: the selected object becomes this (one undo step; still selected) */
  CMD_OBJWRAP,              /* arg: WRAP_: how the selected object sits with the text (floated where it was shown) */
  CMD_SHAPE,                /* arg: SH_, or -1 a text box: a new one at the caret's paragraph, selected */
  CMD_SETSTYLE,             /* arg: struct QStyle *: a style changed (k >= 0) or made (k -1; its index back in k) */
  CMD_DELSTYLE,             /* arg: style index: deleted */
  CMD_CHARSTYLE,            /* arg: character style index (0 none): the selection (or the word at the caret) gets it */
  CMD_CHARFMT,              /* arg: struct QCharFmt *: the selection (or the next typing) gets the masked fields */
  CMD_LISTPRESET,           /* arg: LP_: the selected paragraphs in a new list of that kind */
  CMD_LISTRESTART,          /* arg: the number to start at (0 the list's start): from the caret's paragraph */
  CMD_LISTCONTINUE,         /* the caret's list carries on the numbering of the one before */
  CMD_HEADNUM,              /* arg: LP_ (or -1 none): the heading styles numbered */
  CMD_LISTDEF,              /* arg: const ListDef *: the selected paragraphs in a new list defined so */
  CMD_DOCLOOK,              /* arg: kind << 8 | value: 0 a style set (SS_), 1 fonts (TF_), 2 paragraph spacing (PS_), 3 colours (CS_), 4 a theme (TH_) */
  CMD_PAGECOLOR,
  CMD_CBORDER,              /* a character border on the selection, or off if it has one */            /* arg: Doc.pagecolor (0 none, else HL_ON | 0xRRGGBB) */
  CMD_PASTEAS,              /* arg: PASTE_: paste so */
  CMD_STORY,                /* arg: struct QStoryView *: the document's text changed places with a story's (or back): laid out afresh */
  CMD_GROUP,                /* the floating objects chosen together (Shift-click) made one group */
  CMD_UNGROUP,              /* the selected group taken apart */
  CMD_LINK,                 /* arg: struct QLink *: the selection (or the link at the caret) made a link to url; with
                               nothing selected, text is put in as the link. A link's text changed if text differs */
  CMD_UNLINK,               /* the links in the selection (or the one at the caret) taken off */
  CMD_BLANKPAGE,            /* an empty page at the caret (page breaks before and after an empty paragraph), one undo step */
  CMD_TOCREMOVE,            /* the table of contents taken out */
  CMD_SELUNIT,              /* arg: 0 the caret's sentence, 1 its paragraph, selected */
  CMD_LINEOPTS,             /* arg: struct QLineNum *: line numbering in every section */
  CMD_RESTACK,              /* arg: 1 forward, 2 to the front, -1 backward, -2 to the back: the selected floating object */
  CMD_BOOKMARK,             /* arg: name (UTF-8): the selection made that bookmark (none: the caret's paragraph) */
  CMD_UNBOOKMARK,           /* arg: name: the bookmark taken off */
  CMD_GOTOBM,               /* arg: name: its text selected and shown (a beep if it isn't there) */
  CMD_XREF,                 /* arg: struct QXref *: a cross-reference typed at the caret */
  CMD_OBJALIGN              /* arg: 0-5 align left, centre, right, top, middle, bottom (one object: to the margins;
                               several chosen together: to each other), 6-7 distribute them across, down */
};
/* a hyperlink (UTF-8): its address and the text it shows */
struct QLink { const char *url, *text; };
/* Line Numbering Options: LN_ kind, the first number, every how many lines, twips from the text */
struct QLineNum { u8 linenum, by; u16 start; i32 dist; };
/* a cross-reference: to bookmark name, or (para >= 0) to that heading paragraph (a hidden "_Ref"
   bookmark is put on it if it hasn't one); kind 0 its text, 1 the page it is on */
struct QXref { const char *name; long para; int kind; };
struct QBorders { int which; u8 style, width; u32 color; };      /* TB_, PB_, eighths of a point, 0xRRGGBB */
struct QTableProps { u8 jc, autofit, cantsplit, header; i32 indent, marl, marr, mart, marb; i16 rowh; };
struct QSort { int col, desc, numeric, header; };
struct QFormula { const char *expr; char result[48]; int ok; };
struct QColEdge { u16 table; int edge; i32 x; };                  /* edge k: between grid columns k - 1 and k; x: from the table's left, twips */
struct QComment { const char *text, *author, *initials, *date; int edit; };   /* UTF-8; edit: the comment changed, or -1 for a new one */
struct QParaFmt { unsigned mask; ParaFmt pf; };    /* PM_ mask */
struct QStyle { int k; Style s; };
struct QCharFmt { unsigned mask; CharFmt cf; };
struct QTabs { i32 pos[8]; u8 kind[8], lead[8]; };      /* TAB_, LEAD_ */
struct QStoryView { Pos caret; LONG scroll; const char *label; };   /* scroll: -1 to show the caret; label: what is edited ("" the body) */
struct QFind { const char *what, *with; unsigned flags; ULONG count; int found; };   /* UTF-8; FIND_ flags */

/* controls in the window the view updates as the caret moves (any may be NULL) */
struct Toolbar {
  Object *style, *size, *font, *bold, *italic, *underline, *align[4], *status, *prop, *bullets, *numbers;
  Object *strike, *super, *sub, *ruler;
  Object *ribbon;                          /* gets MUIM_Ribbon_State */
  Object *vruler;                          /* gets MUIM_Ruler_SetV */
  Object *nav, *statusbar;                 /* get MUIM_Nav_Update, MUIM_Status_Set */
  Object *hprop;                           /* the horizontal scroll bar (shown when needed) */
  int units;                               /* rulers and lengths: -1 as the page suggests (Letter: inches), 0 inches, 1 centimetres */
  const char *author, *revdate;            /* tracked changes are theirs (UTF-8), made then */
  void *spell;                             /* Spell * (../core/spell.h): words not in it are underlined, or NULL */
  ULONG uibits;                            /* RBIT()s the application keeps (ruler, navigation pane...): passed on to the ribbon */
  ULONG uibits2;                           /* and those 32 and up (RBIT(RB_ - 32)): a story edited and its options */
  const int *sizes; int nsizes;            /* half-points of each entry of the size cycle */
  const char *const *fonts; int nfonts;    /* families of the font cycle */
  int *style_map; int nstyle_map;          /* style index of each entry of the style cycle */
};

/* the page view class (view.c), a subclass of Area made on first call (NULL if MUI couldn't); free it
   at the end, once every view has been disposed of */
struct MUI_CustomClass *view_class(void);
void view_class_free(void);

/* the ruler above the page: margins, indents and tab stops of the caret's paragraph, or a table's columns */
#define MUIM_Ruler_Set (QTAG + 0x200)
struct MUIP_Ruler_Set {
  ULONG MethodID;
  LONG x0;                  /* window x of the text area's (or cell's) left edge */
  LONG zoom;                /* as the view's: pixels = twips * zoom / 2000 */
  i32 width;                /* the text area's width, twips */
  i32 left, first, right;   /* the paragraph's indents */
  const i32 *tabs;          /* 8, 0-terminated */
  const u8 *tabkind, *tablead;   /* theirs: TAB_, LEAD_ */
  LONG page0, pagew;        /* window x of the page's left edge, and its width in pixels */
  i32 mleft, mright;        /* the page margins, twips */
  int metric;               /* centimetres (else inches) */
  Object *view;             /* where indent and tab changes go (MUIM_Quill_Command) */
  int ncolx;                /* in a table: its column edges (0: none), twips from tblx0 */
  i32 colx[65];
  LONG tblx0;               /* window x of the table's left edge */
  u16 table;                /* its number */
};
/* the vertical ruler (made with MUIA_Ruler_Vertical TRUE): a page's top and bottom margins */
#define MUIA_Ruler_Vertical (QTAG + 0x201)
#define MUIM_Ruler_SetV (QTAG + 0x202)
struct MUIP_Ruler_SetV {
  ULONG MethodID;
  LONG y0;                  /* window y of the page's top edge */
  LONG zoom;
  i32 pageh, mtop, mbottom; /* twips */
  int metric;
};
/* the ruler class (ruler.c): made on first call, freed at the end */
struct MUI_CustomClass *ruler_class(void);
void ruler_class_free(void);

/* the ribbon (ribbon.c): WordCraft's tabs and groups, drawn by Quill. A button's click is a
   MUIM_Application_ReturnID of its ID_ (ids.h); a choice from a list or the styles gallery also sets
   MUIA_Ribbon_Value to the entry chosen */
#define MUIA_Ribbon_Value  (QTAG + 0x300)
#define MUIA_Ribbon_Tab    (QTAG + 0x301)     /* the tab shown */
#define MUIM_Ribbon_State  (QTAG + 0x310)
/* items' states 32 and up are in MUIP_Ribbon_State.on2 */
enum { RB_LK_FIRSTROW = 32, RB_LK_LASTROW, RB_LK_BANDROWS, RB_LK_FIRSTCOL, RB_LK_LASTCOL, RB_LK_BANDCOLS, RB_TGRID, RB_TSTYLED,
       RB_PICSEL, RB_SHAPESEL,           /* a picture selected; a shape or text box selected */
       RB_HFEDIT, RB_STORYEDIT,          /* a header or footer edited; a note, comment or text box's text */
       RB_FIRSTPG, RB_EVENODD, RB_OWNHF, RB_SECTHF,     /* the edited header's section: a first page of its own, odd and even pages
                                                            (the document's), headers of its own; it can have its own (not the last) */
       RB_GROUPSEL, RB_MULTISEL,         /* a group selected; floating objects chosen together (Shift-click) */
       RB_ONESHAPE,                      /* a shape or text box selected, not a group */
       RB_CANREDO,                       /* there is something to redo */
       RB_FLOATSEL };                    /* a floating object selected, or several chosen together */
enum {
  RB_BOLD = 1, RB_ITALIC, RB_UNDERLINE, RB_STRIKE, RB_SUB, RB_SUPER, RB_BULLETS, RB_NUMBERS, RB_MARKS, RB_INTABLE,
  RB_TRACK, RB_RULER, RB_NAVPANE, RB_SHOWCOMMENTS, RB_HEADROW, RB_CANUNDO, RB_GRID, RB_PICTURE, RB_SELECTION,
  RB_ALIGN0, RB_ALIGN1, RB_ALIGN2, RB_ALIGN3, RB_VALIGN0, RB_VALIGN1, RB_VALIGN2, RB_COMMENTS, RB_CHANGES, RB_DARKPAGE, RB_BRUSH
};
#define RBIT(b) (1UL << (b))
struct MUIP_Ribbon_State {
  ULONG MethodID;
  ULONG on;                 /* RBIT(RB_...) */
  const char *font;         /* the font at the caret (UTF-8), its size in half-points */
  int size;
  int style;                /* the paragraph style (Doc.styles index) */
  i32 left, right, before, after;           /* the paragraph's indents and spacing (twips) */
  int metric;               /* show lengths in cm (else inches) */
  Doc *doc;                 /* for the styles gallery */
  FontSet *fs;
  int vmode;                /* VM_ */
  ULONG on2;                /* RBIT(RB_... - 32) */
  i32 rowh, colw;           /* the caret's table row: its height (Cell.rowh), its cell's width (twips) */
  u32 stylesig;             /* the styles as they look (changes when any does: the gallery is drawn again) */
};
/* the ribbon class: made on first call (NULL if MUI couldn't), freed at the end */
struct MUI_CustomClass *ribbon_class(void);
void ribbon_class_free(void);
/* the font family list of the font field */
extern const char *const ribbon_fonts[];
/* the font list: Word's usual families, then the installed ones (made once); one of them by number */
const char *const *ribbon_font_names(FontSet *fs);
const char *ribbon_font_name(int i);
/* what the table lists' entries stand for: pen widths (eighths of a point), row heights and column widths (twips) */
extern const u8 ribbon_penwidths[];
extern const i32 ribbon_rowheights[], ribbon_colwidths[];

/* the navigation pane and the status bar (panes.c) */
#define MUIA_Nav_View    (QTAG + 0x400)     /* the page view: where clicks go, and its layout for the page pictures */
#define MUIM_Nav_Update  (QTAG + 0x410)
struct MUIP_Nav_Update {
  ULONG MethodID;
  Doc *doc;
  int headings;             /* 1: look for changed headings, 2: a new document (the search done again too), 0: only the caret moved */
  u32 caretpara, page, npages;
};
#define MUIA_Status_Zoom (QTAG + 0x420)     /* (get) the zoom chosen on the slider, after ID_ZOOMSET */
#define MUIM_Status_Hint (QTAG + 0x431)     /* (text): a line shown on the bar's left (Latin-1, copied), "" or NULL: none */
struct MUIP_Status_Hint { ULONG MethodID; const char *text; };
#define MUIM_Status_Set  (QTAG + 0x430)
struct MUIP_Status_Set {
  ULONG MethodID;
  u32 page, pages;          /* the caret's page (from 0), of how many */
  int complete;             /* 0: more pages to come (still being laid out) */
  ULONG words;              /* ~0: not counted yet */
  LONG zoom;
  int vmode;                /* VM_ */
};
/* an object typing goes to instead of the page (the search box while active), or NULL */
extern Object *key_grab;
/* the public screen Quill's windows open on ("": the default one); QWindowObject is a window on it */
extern char pub_screen[64];
/* a dialog's contents in Quill's font and colours (after a group's opening: VGroup, QDLG, ...) */
#define QDLG (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(), \
             (ULONG)(theme_dialog_back() ? MUIA_Background : TAG_IGNORE), (ULONG)theme_dialog_back()
/* (theme.c) the dialogs' font, NULL for MUI's own; the ribbon's colour as an MUI image spec */
struct TextFont *theme_dialog_font(void);
const char *theme_dialog_back(void);
/* (qbutton.c) a dialog button, a label and a section title drawn in Quill's theme (MUI's own
   objects when the classes can't be made) */
Object *QButton(const char *label);
Object *QLabel(const char *label, ULONG flags);   /* (main.c's Label1()... make these) */
Object *QTitleObject(const char *t);
/* the button class (its label class made with it), and both freed at the end */
struct MUI_CustomClass *qbutton_class(void);
void qbutton_class_free(void);
#define QWindowObject WindowObject, (ULONG)(pub_screen[0] ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pub_screen
/* the navigation pane's and status bar's classes, made on first call (NULL if MUI couldn't) */
struct MUI_CustomClass *nav_class(void);
struct MUI_CustomClass *status_class(void);
#define MUIA_FlatScroll_Horiz (QTAG + 0x440)  /* (init) across instead of up and down */
struct MUI_CustomClass *flatscroll_class(void);   /* a flat scroll bar: MUIA_Prop_Entries, _Visible, _First */
void panes_free(void);                            /* the three classes above freed, at the end */

/* the File page (backstage.c) */
#define MUIA_Back_Value    (QTAG + 0x500)   /* (get) the template or recent document chosen */
#define MUIA_Back_Page     (QTAG + 0x501)   /* (set) the page shown: 0 Home, 1 New, 2 Open, 3 Info, 4 Export, 5 Options, 6 About */
#define MUIA_Back_Metric   (QTAG + 0x502)   /* (get) the units chosen in Options: centimetres (else inches) */
#define MUIA_Back_UserName (QTAG + 0x503)   /* (get) the name given in Options (Latin-1) */
#define MUIA_Back_Dark     (QTAG + 0x504)   /* (get) the interface theme chosen: dark (else light) */
#define MUIA_Back_Language (QTAG + 0x505)   /* (get) the interface language chosen (LANG_) */
#define MUIA_Back_Large    (QTAG + 0x506)   /* (get) the interface's text large */
#define MUIM_Back_Set      (QTAG + 0x510)
struct MUIP_Back_Set {
  ULONG MethodID;
  const char *const *recent; int nrecent;   /* paths, the latest first */
  const char *info;                          /* the Info page: "label\tvalue" lines */
  const char *username;
  int metric, dark, lang, large;
};
/* the File page's class: made on first call, freed at the end */
struct MUI_CustomClass *backstage_class(void);
void backstage_class_free(void);

/* the clipboard (unit 0): FTXT text, Latin-1 */
/* clip_write: 1 if written, else 0. clip_read: the clipboard's text, malloc()ed and 0-terminated
   (the caller frees it), its length in *len; NULL if there is none */
int   clip_write(const char *s, long len);
char *clip_read(long *len);

#endif
