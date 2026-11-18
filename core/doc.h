/*
 * doc.h - Quill's document model: paragraphs of UTF-8 text with runs of character formatting,
 * paragraph formatting, styles, page setup; edits with undo and redo.
 *
 * All lengths are integer twips (1/20 point, 1/1440 inch), as in Word's files, so nothing in the
 * document or its layout needs floating point (68k FPUs are slow or absent; see README).
 * Positions are (paragraph, byte offset), always on a character boundary.
 */
#ifndef QUILL_DOC_H
#define QUILL_DOC_H

#include "util.h"

/* character formatting; every run carries the full effective formatting */
enum { CF_BOLD = 1, CF_ITALIC = 2, CF_UNDERLINE = 4, CF_STRIKE = 8, CF_SUPER = 16, CF_SUB = 32, CF_CAPS = 64 };
#define COLOR_AUTO 0xFF000000UL
typedef struct {
  u8  flags;
  u8  font;                 /* index into Doc.fonts */
  u16 size;                 /* half-points, as in .docx (24 = 12 pt) */
  u32 color;                /* 0xRRGGBB, or COLOR_AUTO */
  u32 hl;                   /* highlight (shading behind the text): 0 none, else HL_ON | 0xRRGGBB */
  u8  rev;                  /* a tracked change: REV_NONE, REV_INS (inserted) or REV_DEL (deleted, still shown) */
  u16 rid;                  /* whose and when: Doc.revs[rid] */
  u8  cstyle;               /* its character style: Doc.styles index (ST_CHAR), 0 none */
  u8  fx;                   /* FX_ effects */
  u8  ul;                   /* the underline's kind (with CF_UNDERLINE): UL_ */
  i16 spacing;              /* between characters: twips more (expanded) or less (condensed) */
  u16 scale;                /* width, per cent (0: 100) */
  i16 position;             /* raised (lowered if negative), half-points */
  u16 link;                 /* a hyperlink: Doc.links index + 1 (0: not a link) */
  u16 bmark;                /* in a bookmark: Doc.bmarks index + 1 (0: none) */
  /* (work packages: new fields at the end) */
  u8  thfont;               /* (WP13) font is the theme's (THF_MAJOR, THF_MINOR; core/doctheme.h), 0 not */
  u32 thcolor;              /* (WP13) color is a theme colour (a THR_ reference, core/doctheme.h), 0 not */
  /* (work packages: new fields go below, at the end) */
  /* WP9 character formatting: */
  u16 fx2;                  /* FX2_ effects and the enclosure (FX2_ENCLOSE bits) */
  u16 fx2mask;              /* (only in a value given to cf_apply: which fx2 bits CM_FX2 changes; 0 all of them) */
  u32 ulcolor;              /* the underline's colour: 0 the text's, else HL_ON | 0xRRGGBB */
  u32 shade;                /* character shading (w:shd, kept apart from the highlight): 0 none, else HL_ON | 0xRRGGBB */
  u16 rx;                   /* run properties kept verbatim (w14 text effects, w:effect): charfx.h rx_text(rx), 0 none */
  /* WP31 proofing languages (core/proflang.h): */
  u16 lang;                 /* the text's language (w:lang w:val): a proflang.h number, 0 not set */
  u16 langea, langbidi;     /* w:lang's w:eastAsia and w:bidi, kept as they were read */
  u8  noproof;              /* not checked for spelling or grammar (w:noProof) */
  u16 frev;                 /* WP5: a tracked formatting change (w:rPrChange): Doc.frevs index + 1, 0 none */
  u8 move;                  /* WP5: a tracked insertion or deletion that is a move (w:moveTo, w:moveFrom):
                               Doc.rvmoves index + 1 (its name), 0 not a move */
} CharFmt;
enum { REV_NONE, REV_INS, REV_DEL };
enum { FX_SMALLCAPS = 1, FX_DSTRIKE = 2, FX_BORDER = 4 };   /* FX_BORDER: a box round the characters (w:bdr) */
/* WP9: CharFmt.fx2. Hidden text (w:vanish: shown with formatting marks, else left out of the layout
   and the PDF), outline (hollow letters), shadow, emboss and engrave (w:imprint), and enclosed
   characters (Word's eq \o\ac field: a circle, square, triangle or diamond round them, the symbol
   enlarged or the text shrunk) */
enum { FX2_HIDDEN = 1, FX2_OUTLINE = 2, FX2_SHADOW = 4, FX2_EMBOSS = 8, FX2_ENGRAVE = 16,
       FX2_ENCLOSE = 0xE0,       /* the enclosure's shape: (fx2 & FX2_ENCLOSE) >> 5, EN_ */
       FX2_ENLARGE = 0x100 };    /* with an enclosure: the symbol enlarged (else the text shrunk) */
enum { EN_NONE, EN_CIRCLE, EN_SQUARE, EN_TRIANGLE, EN_DIAMOND };
#define FX2_RELIEF (FX2_OUTLINE | FX2_SHADOW | FX2_EMBOSS | FX2_ENGRAVE)
#define ENCLOSURE(f) (((f)->fx2 & FX2_ENCLOSE) >> 5)
/* WP9's CM_ bits (CM_FX2 is the bit the CF_ flags leave unused); in CM_LOOK and CM_ALL */
#define CM_FX2     0x80U
#define CM_ULCOLOR 0x40000000U
#define CM_SHADE   0x20000000U
#define CM_RX      0x10000000U
enum { UL_SINGLE, UL_DOUBLE, UL_THICK, UL_DOTTED, UL_DASHED, UL_WAVE, UL_WORDS, UL_N };
#define HL_ON 0x01000000UL
/* which fields a formatting change touches */
enum { CM_BOLD = 1, CM_ITALIC = 2, CM_UNDERLINE = 4, CM_STRIKE = 8, CM_SUPER = 16, CM_SUB = 32, CM_CAPS = 64,
       CM_FONT = 0x100, CM_SIZE = 0x200, CM_COLOR = 0x400, CM_HL = 0x800, CM_REV = 0x1000,
       CM_CSTYLE = 0x2000, CM_SMALLCAPS = 0x4000, CM_DSTRIKE = 0x8000, CM_UL = 0x10000, CM_SPACING = 0x20000, CM_SCALE = 0x40000,
       CM_POSITION = 0x80000,
       CM_LINK = 0x100000,       /* (not in CM_ALL: clearing formatting or painting it keeps links, as in Word) */
       CM_BMARK = 0x200000,      /* (nor this: bookmarks stay where they are) */
       CM_CBORDER = 0x400000,    /* FX_BORDER */
       CM_LOOK = 0x764FCFFF,     /* how it looks (not its character style nor tracked changes; WP9's and WP31's bits too) */
       CM_ALL = 0x764FEFFF };     /* CM_ALL leaves tracked changes alone */
#define CM_FREV 0x08000000U        /* WP5: CharFmt.frev (not in CM_ALL; CM_REV carries move with rev) */

enum { AL_LEFT, AL_CENTER, AL_RIGHT, AL_JUSTIFY, AL_DISTRIBUTE };   /* AL_DISTRIBUTE (WP10): every line spread out, the last too (w:jc distribute) */
enum { LS_AUTO, LS_ATLEAST, LS_EXACT };    /* line spacing rule: line is 240ths of a line, or twips */
/* table cells: a table is a run of paragraphs with the same table number; a cell is a run with
   the same row and column. So positions, editing and undo work in tables as anywhere else. */
enum { VM_NONE, VM_START, VM_CONT };                 /* vertical merging: first cell, cells merged into it */
enum { BD_TOP = 1, BD_LEFT = 2, BD_BOTTOM = 4, BD_RIGHT = 8, BD_ALL = 15 };
/* a line's kind (cell borders and page borders share them) */
enum { PB_NONE, PB_SINGLE, PB_DOUBLE, PB_THICK, PB_DOTTED, PB_DASHED };
enum { E_TOP, E_LEFT, E_BOTTOM, E_RIGHT };           /* a cell's edges in Cell.bstyle...: BD_ bit 1 << E_ */
typedef struct {
  u16 table;                /* 0: not in a table; else Doc.tables[table - 1] */
  u16 row;
  u8  col, span;            /* first grid column, and how many it covers (1 or more) */
  u8  vmerge;
  u8  valign;               /* 0 top, 1 centre, 2 bottom */
  u8  borders;              /* BD_ flags: the cell's edges that have a line */
  u8  header;               /* the row repeats at the top of each page */
  u8  cantsplit;            /* the row stays on one page (Word's "allow row to break across pages" off) */
  u8  bstyle[4];            /* each edge's line (E_ order): PB_ kind, 0 meaning single */
  u8  bwidth[4];            /* eighths of a point, 0 meaning 4 (half a point) */
  i16 rowh;                 /* row height: > 0 at least, < 0 exactly, 0 automatic */
  u32 shade;                /* fill 0xRRGGBB, or COLOR_AUTO */
  u32 bcolor[4];            /* 0xRRGGBB or COLOR_AUTO; 0 (black) is also what a zeroed cell has */
  u32 thshade;              /* (WP13) shade is a theme colour (THR_ reference, core/doctheme.h), 0 not */
  /* WP21b: */
  u8  tdir;                 /* which way its text runs: TD_ (w:textDirection) */
  u8  marset;               /* which of its own margins it has (1 << E_ edge; else the table's) */
  i16 mar[4];               /* its own margins (E_ order), twips (w:tcMar) */
} Cell;
/* a cell's text direction: across (lrTb), turned 90 degrees clockwise so lines read downwards (tbRl),
   or turned 270 degrees so they read upwards (btLr) */
enum { TD_HORZ, TD_DOWN, TD_UP };
#define MAX_COLS 64
/* tblLook: which parts of a table its style formats specially (Word's bits) */
enum { LK_FIRSTROW = 0x20, LK_LASTROW = 0x40, LK_FIRSTCOL = 0x80, LK_LASTCOL = 0x100, LK_NOHBAND = 0x200, LK_NOVBAND = 0x400 };
#define LK_DEFAULT (LK_FIRSTROW | LK_FIRSTCOL | LK_NOVBAND)
typedef struct {
  u8  ncols;
  u8  jc;                   /* the table's place between the margins: AL_LEFT, AL_CENTER, AL_RIGHT */
  u8  tstyle;               /* its table style: Doc.tstyles[tstyle - 1], 0 none */
  u8  autofit;              /* Word may widen columns to fit (else the widths are fixed) */
  u16 look;                 /* LK_ flags */
  i32 grid[MAX_COLS];       /* column widths */
  i32 indent;               /* left edge from the left margin */
  i32 marl, marr;           /* cell margins (Word's default 108 = 0.19 cm) */
  i32 mart, marb;           /* and above and below the text (0) */
  /* WP21b: a floating table (w:tblpPr): placed from its anchor's column (TA_TEXT), the margin or the
     page across (hanchor) and down (vanchor), at tx, ty or aligned (xspec, yspec: PA_ of doc.h's
     objects, PA_NONE for tx/ty), the text dl, dr, dt, db away round it; and its alternative text */
  u8  floating;
  u8  hanchor, vanchor;     /* TA_ */
  u8  xspec, yspec;         /* PA_NONE (use tx, ty), PA_START, PA_CENTER, PA_END */
  u8  nooverlap;            /* w:tblOverlap never */
  i32 tx, ty;
  i32 dl, dr, dt, db;
  char caption[128];        /* w:tblCaption (UTF-8) */
  char descr[256];          /* w:tblDescription (UTF-8) */
} Table;
enum { TA_TEXT, TA_MARGIN, TA_PAGE };

/* table styles: lines and fill for the whole table, and for parts of it (Word's conditional
   formatting: the heading row, banded rows...), each part over the ones before it */
enum { TC_WHOLE, TC_BAND1V, TC_BAND2V, TC_BAND1H, TC_BAND2H, TC_FIRSTCOL, TC_LASTCOL, TC_FIRSTROW, TC_LASTROW,
       TC_NE, TC_NW, TC_SE, TC_SW, TC_N };
enum { TE_TOP, TE_LEFT, TE_BOTTOM, TE_RIGHT, TE_INSIDEH, TE_INSIDEV, TE_N };
typedef struct { u8 set, style, width; u32 color; } TBorder;      /* set: the part says something about it */
typedef struct {
  u8 used;                  /* the part is in the style at all */
  u8 shade_set; u32 shade;
  TBorder b[TE_N];          /* the part's outer edges and the lines inside it (tblBorders) */
  TBorder cb[4];            /* every cell's own edges (tcBorders) */
  unsigned cmask; CharFmt cf;   /* character formatting it sets (CM_ flags) */
  u8 align_set, align;      /* paragraph alignment it sets */
  u8 valign_set, valign;    /* WP21b: the cells' vertical alignment it sets (0 top, 1 centre, 2 bottom) */
} TCond;
typedef struct {
  char id[40], name[48];
  char based[40];
  TCond c[TC_N];
  u8 mar_set; i32 marl, marr;
  /* WP21a: */
  u8 custom;                /* made by the user (w:customStyle): offered in the gallery even when unused */
  u8 gone;                  /* deleted (its place kept: tables and undo refer to styles by number) */
} TableStyle;
#define MAX_TSTYLES 250
typedef struct {
  u8  style;                /* index into Doc.styles */
  u8  align;
  u8  linerule;
  u8  keepnext;
  i32 left, right, first;   /* indents; first is relative to left (negative: hanging) */
  i32 before, after;        /* space above and below */
  i32 line;                 /* LS_AUTO: 240 = single, 360 = 1.5; else twips */
  i32 tabs[8];              /* custom tab stops (twips from the left indent); 0 ends the list */
  u8 tabkind[8];            /* each stop's alignment: TAB_ */
  u8 tablead[8];            /* and what fills up to it: LEAD_ */
  u16 list;                 /* 0: not in a list; else Doc.lists[list - 1] */
  u8 level;                 /* list level, 0-8 */
  u8 pagebreak;             /* starts a new page */
  u8 colbreak;              /* starts a new column (a new page outside columns) */
  u8 dropcap;               /* the first letter dropped over this many lines (0: none) */
  u8 dropmargin;            /* ... and set in the margin instead of the text */
  u16 sect;                 /* this paragraph ends a section: Doc.sects[sect - 1] is its format (0: none) */
  u8 keeplines;             /* its lines on one page */
  u8 nowidow;               /* widow and orphan control off */
  u8 contextual;            /* no space before or after between paragraphs of the same style */
  u8 bstyle[5];             /* borders (PE_ order): PB_ kind, PB_NONE none */
  u8 bwidth[5];             /* eighths of a point */
  u8 bspace[5];             /* points between the border and the text */
  u32 bcolor[5];            /* 0xRRGGBB or COLOR_AUTO */
  u32 shade;                /* the paragraph's fill: 0 none, else HL_ON | 0xRRGGBB */
  Cell cell;                /* the (innermost) table cell it is in */
  Cell outer;               /* a table inside a table cell: the outer table's cell (table 0: not nested) */
  /* (work packages: new fields at the end) */
  u32 thshade;              /* (WP13) shade is a theme colour (THR_ reference, core/doctheme.h), 0 not */
  u32 thbcolor[5];          /* (WP13) and each border's colour */
  u16 prev;                 /* WP5: a tracked paragraph formatting change (w:pPrChange): Doc.frevs index + 1, 0 none */
  u8 outlvl;                /* (WP11) its outline level: 1-9, 0 body text (w:outlineLvl 0-8, 9); "heading N" styles have N */
} ParaFmt;
/* a paragraph's borders: those with the same borders run together in one box, "between" lines
   separating them */
enum { PE_TOP, PE_LEFT, PE_BOTTOM, PE_RIGHT, PE_BETWEEN, PE_N };
enum { TAB_LEFT, TAB_CENTER, TAB_RIGHT, TAB_DECIMAL };
enum { LEAD_NONE, LEAD_DOT, LEAD_HYPHEN, LEAD_LINE };
enum { PM_STYLE = 1, PM_ALIGN = 2, PM_INDENT = 4, PM_SPACING = 8, PM_LINE = 16, PM_TABS = 32, PM_KEEP = 64, PM_LIST = 128,
       PM_BREAK = 256, PM_CELL = 512, PM_SECT = 1024, PM_DROP = 2048,
       PM_BORDER = 0x1000, PM_SHADE = 0x2000, PM_PAGINATION = 0x4000 /* keeplines, nowidow */, PM_CONTEXT = 0x8000,
       PM_OUTLINE = 0x100000,    /* (WP11) outlvl */
       PM_PREV = 0x40000000,     /* WP5: prev (not in PM_ALL) */
       PM_ALL = 0x10F1FF };   /* PM_ALL leaves the cell, the section and drop caps alone */
#define PM_STYLED (PM_ALL & ~PM_STYLE)    /* what a paragraph style can set */
#define in_table(pf) ((pf)->cell.table != 0)
#define is_nested(pf) ((pf)->outer.table != 0)
/* the cell of the outermost table a paragraph is in */
#define top_cell(pf) ((pf)->outer.table ? &(pf)->outer : &(pf)->cell)
/* two paragraphs in the same (innermost) cell, or both outside tables */
int same_cell(const ParaFmt *a, const ParaFmt *b);
/* a paragraph's cell in table t (its own, or the outer one when t holds the table it's in), NULL if
   it isn't in t */
const Cell *cell_in(const ParaFmt *pf, u16 t);

/* lists: a definition per list (a .docx w:num with its abstract numbering resolved) */
enum { NF_BULLET, NF_DECIMAL, NF_LOWER_LETTER, NF_UPPER_LETTER, NF_LOWER_ROMAN, NF_UPPER_ROMAN, NF_NONE,
       NF_DECIMAL_ZERO, NF_ORDINAL, NF_CARDINAL_TEXT, NF_ORDINAL_TEXT };   /* (WP10: 01 02; 1st 2nd; One Two; First Second) */
typedef struct {
  u8 fmt;
  u16 start;
  char text[32];            /* UTF-8 (room for the legal style's "%1.%2.%3.%4.%5.%6.%7.%8.%9."): "%1." for numbers (%1-%9: the levels' counters), the bullet character */
  char font[32];            /* the label's font ("Symbol" bullets are mapped to Unicode on reading) */
  i32 left, hanging;        /* the indents the level gives its paragraphs */
} ListLevel;
typedef struct { u16 id; ListLevel lvl[9]; } ListDef;
#define MAX_LISTS 256

typedef struct { u32 end; CharFmt f; } Run;       /* covers text up to byte end (exclusive) */

typedef struct Para {
  char *t;                  /* UTF-8, NUL-terminated; no newlines; may hold tabs and line breaks ('\v') */
  u32 len, cap;
  Run *r;                   /* at least one run, even for an empty paragraph (it gives its formatting) */
  u32 nr, rcap;
  ParaFmt pf;
  u32 ver;                  /* bumped by every change: layout caches key on (pointer, ver) */
} Para;

/* (WP13) what else a style says in Word's files: its linked style (a paragraph style and a character
   style shown as one), where it comes in the recommended order, whether it is hidden, its aliases,
   its language, a frame round its paragraphs and the keys that apply it */
enum { SXF_SEMIHIDDEN = 1, SXF_UNHIDE = 2, SXF_LOCKED = 4, SXF_AUTOREDEFINE = 8, SXF_HIDDEN = 16, SXF_PERSONAL = 32 };
typedef struct {
  u8  set;                  /* the frame is given (w:framePr in the style) */
  u8  wrap;                 /* 0 auto, 1 around, 2 none (top and bottom), 3 tight, 4 through, 5 not beside */
  u8  hanchor, vanchor;     /* 0 text (margin for horizontal), 1 margin, 2 page */
  u8  xalign, yalign;       /* 0 none (x, y count), 1 left/top, 2 centre, 3 right/bottom, 4 inside, 5 outside */
  i32 w, h;                 /* twips (0 auto); h < 0 exactly -h */
  i32 x, y;                 /* twips from the anchor */
  i32 hspace, vspace;       /* distance from the text round it */
} StyleFrame;
typedef struct {
  u8  link;                 /* the linked style: Doc.styles index + 1, 0 none */
  u8  priority;             /* w:uiPriority + 1 (0: not given) */
  u8  flags;                /* SXF_ */
  u16 key;                  /* the shortcut key as Word's wne:kcmPrimary (modifiers << 8 | key code), 0 none */
  char lang[12];            /* (unused: a style's language is WP31's CharFmt.lang in its formatting) */
  char aliases[48];         /* other names (w:aliases, UTF-8), "" none */
  StyleFrame frame;
} StyleX;

/* styles: paragraph styles (a paragraph's formatting and its text's) and character styles (some of
   the text's formatting). A style is based on another and sets some things itself (pmask, cmask);
   pf and cf are what it comes to. A deleted style keeps its place (ST_GONE): paragraphs and undo
   refer to styles by index */
enum { ST_PARA, ST_CHAR, ST_GONE };
#define NO_STYLE 0xFF
typedef struct {
  char id[40];              /* styleId in .docx, e.g. "Heading1" */
  char name[48];            /* shown name, e.g. "heading 1" in the file, shown as "Heading 1" */
  u8 type;
  u8 next;                  /* style of the paragraph Return makes */
  u8 builtin;
  u8 based;                 /* the style it is based on, NO_STYLE none (the document's defaults) */
  u8 quick;                 /* shown in the styles gallery */
  unsigned pmask, cmask;    /* what it sets itself (PM_STYLED, CM_LOOK): in opf, ocf */
  ParaFmt opf;
  CharFmt ocf;
  ParaFmt pf;               /* effective formatting (inheritance resolved) */
  CharFmt cf;
  unsigned ecmask;          /* a character style: everything it and those it is based on set */
  /* (work packages: new fields at the end) */
  StyleX x;                 /* (WP13) Word's other style settings (doc_set_style copies them whole) */
} Style;

typedef struct {
  i32 width, height;        /* A4: 11906 x 16838; Letter: 12240 x 15840 */
  i32 top, bottom, left, right;
  i32 header, footer;       /* distance of the header from the page's top edge, the footer from its bottom (708) */
  /* WP22a: */
  i32 gutter;               /* binding room added to the left (inside) margin, or the top (Doc.gutterattop) */
} PageSetup;
struct Story;

/* page borders: one kind of line all round (PB_); with WP22a's fields, each side its own (core/pages.h) */
typedef struct {                /* WP22a: one side of a page border */
  u8 style;                 /* PB_ nearest to it (PB_NONE: no line on this side) */
  u8 width;                 /* eighths of a point (art borders: points) */
  u8 shadow, frame;         /* w:shadow, w:frame */
  u32 color;                /* 0xRRGGBB or COLOR_AUTO */
  i32 space;                /* twips between it and the text (or the page's edge) */
  char val[24];             /* Word's name for its kind ("single", "thinThickSmallGap", "apples"...) */
} PBSide;
typedef struct PageBorder {
  u8 style;                 /* PB_ */
  u8 width;                 /* eighths of a point (4 = half a point) */
  u8 frompage;              /* space measured from the page's edge (else from the text) */
  u32 color;                /* 0xRRGGBB or COLOR_AUTO */
  i32 space;                /* twips between the border and the text (or the page's edge) */
  /* WP22a: */
  u8 own;                   /* the sides are side[] (else all four are style, width, color, space) */
  PBSide side[4];           /* top, left, bottom, right (PBS_) */
  u8 display;               /* PBD_: on every page, the section's first only, all but its first */
  u8 back;                  /* behind the text (w:zOrder="back") */
} PageBorder;
/* sections: the paragraph that ends one holds its format (ParaFmt.sect), the document's last
   paragraph the last section's (none: one column, from a new page). A section may have its own page
   setup, and its own headers and footers; else it has the document's (Doc.page, Doc.hdr...), which are
   the last section's. A section's start says how it begins after the one before */
enum { SB_NEXT, SB_CONT, SB_EVEN, SB_ODD };
enum { VA_TOP, VA_CENTER, VA_BOTTOM };
enum { LN_NONE, LN_PAGE, LN_SECTION, LN_CONT };     /* line numbers, starting again each page, each section, or never */
#define MAX_SECT_COLS 6
/* footnote and endnote options (WP22b, core/notes.h): where the notes go, how they are numbered.
   set: which of the others are given (NPS_); what isn't comes from the document's (Doc.fnpr, Doc.enpr),
   else Word's defaults (footnotes 1, 2, 3 at the page's foot; endnotes i, ii, iii at the end) */
enum { NPS_POS = 1, NPS_FMT = 2, NPS_START = 4, NPS_RESTART = 8 };
typedef struct {
  u8 set;                   /* NPS_ bits */
  u8 pos;                   /* NPOS_ (core/notes.h) */
  u8 fmt;                   /* NNF_ */
  u8 restart;               /* NRS_ */
  u16 start;                /* the first number (1...) */
  char other[22];           /* a w:numFmt Quill doesn't draw (NNF_OTHER, shown as digits), kept */
} NotePr;
typedef struct {
  u8 start;                 /* SB_ */
  u8 cols;                  /* 1 to MAX_SECT_COLS, of equal width */
  u8 colline;               /* a line between the columns */
  u8 valign;                /* where the text sits on its pages: VA_ */
  u8 linenum;               /* LN_ */
  u8 lnby;                  /* line numbers on every lnby'th line (1) */
  u16 lnstart;              /* the first line's number (0 or 1: 1) */
  i32 colgap;               /* between columns (720) */
  i32 lndist;               /* line numbers' right edge from the text (360) */
  u8 haspage;               /* its own page setup (else the document's) */
  u8 titlepg;               /* its first page has the first page's header and footer (HF_FIRST); the
                               last section's is Doc.titlepg */
  PageSetup page;           /* (haspage) */
  struct Story *hf;         /* its own headers and footers: HF_N headers then HF_N footers (malloc'd,
                               the section format owns them); NULL: the document's */
  /* WP22a: (core/pages.h) */
  u8 colunequal;            /* columns of their own widths (colw, colsp) rather than equal ones */
  i32 colw[MAX_SECT_COLS];  /* (colunequal) each column's width, twips */
  i32 colsp[MAX_SECT_COLS]; /* and the space after it */
  u8 pbown;                 /* the page border is pb (else the document's, Doc.border) */
  PageBorder pb;            /* (pbown) its own page border (style PB_NONE and no sides: none) */
  u8 rtlgutter;             /* w:rtlGutter: the gutter on the right */
  NotePr fnpr, enpr;        /* (WP22b) its own footnote and endnote options (w:footnotePr, w:endnotePr) */
} SectFmt;
/* s set to a section with no settings of its own: one column from a new page, 720 twips between
   columns, no line numbers */
void sect_default(SectFmt *s);

/* a watermark: words across each page behind the text (Word's text watermark) */
typedef struct {
  char text[64];            /* UTF-8; "" none */
  u32 color;                /* 0xRRGGBB (Word's: silver C0C0C0) */
  u8 horizontal;            /* across (else diagonal, rising to the right) */
  char font[32];            /* "" : Calibri */
  /* WP22a: a picture watermark (Word's WordPictureWatermark) instead of words */
  u8 *pic; u32 piclen;      /* the picture file (PNG or JPEG), malloc'd, owned; NULL none */
  u8 pictype;               /* IMG_ */
  u32 pw, ph;               /* its size in pixels */
  u16 scale;                /* per cent of its natural size (at 96 dpi); 0: fitted to the text area */
  u8 washout;               /* drawn pale (Word's washout: gain 0.3, black level 0.35) */
  u8 opaque;                /* words drawn solid (else half transparent) */
} Watermark;


/* headers and footers: a few paragraphs each, kept beside the body. Fields in their text: */
#define FIELD_PAGE  "\xEE\x80\x80"     /* U+E000: the page number */
#define FIELD_PAGES "\xEE\x80\x81"     /* U+E001: the number of pages */
#define FIELD_PAGEREF "\xEE\x80\x82"   /* U+E002: the page a bookmark is on (its run's link, "#name", names it) */
#define IS_FIELD(t) ((u8)(t)[0] == 0xEE && (u8)(t)[1] == 0x80 && (u8)(t)[2] >= 0x80 && (u8)(t)[2] <= 0x82)
enum { HF_DEFAULT, HF_FIRST, HF_EVEN, HF_N };
struct Story { Para **p; u32 np; };
typedef struct Story Story;
/* editing a story (a header, footer, note, comment or text box's text) as the document: its
   paragraphs take the body's place in Doc.p until it is done, so everything that edits the body
   edits them (with an undo history of their own). Kinds: */
enum { SK_NONE, SK_HEADER, SK_FOOTER, SK_NOTE, SK_ENOTE, SK_COMMENT, SK_TEXTBOX };
struct Undo;
typedef struct {
  int kind;                 /* SK_ (SK_NONE: the body is being edited) */
  int index;                /* which: HF_ for headers and footers, else the note, comment or Doc.images index */
  int sect;                 /* headers and footers: the section's (Doc.sects number), 0 the document's */
  u32 fpage, fpages;        /* what the page fields show while it is edited (page from 0) */
  u32 ver0;                 /* Doc.ver when it began */
  Para **p; u32 np, pcap;   /* the body and its undo history, put aside */
  struct Undo *undo;
  PageSetup page;           /* the document's page setup (Doc.page is the story's text area meanwhile) */
  u8 wm, border;            /* the watermark and page border (Doc.wm.text[0], Doc.border.style), put aside */
  struct Story hdr[HF_N], ftr[HF_N];   /* the document's headers and footers, put aside */
  /* WP22a: */
  u8 *wmpic;                /* the picture watermark, put aside */
} StoryEdit;

/* pictures and other objects (shapes, text boxes): the text holds U+F0000 + n for Doc.images[n]
   (4 bytes of UTF-8: F3 Bx xx xx), so they move, copy and undo as characters do. Each placing has
   its own entry (its own size and settings; a changed object is a new entry put in its place). An
   object is in line with the text, or floats: placed on the page from its anchor paragraph (the one
   holding it), the text wrapping round it, or behind or in front of the text */
#define MAX_IMAGES 16384
enum { OBJ_PICTURE, OBJ_SHAPE, OBJ_TEXTBOX, OBJ_GROUP };
enum { OBJ_CHART = 8, OBJ_SMARTART = 9 };     /* (WP15a) a chart or SmartArt: its parts and model in Image.pack (core/chart.h) */
#define OBJ_IS_GRAPHIC(k) ((k) == OBJ_CHART || (k) == OBJ_SMARTART)
enum { WRAP_INLINE, WRAP_SQUARE, WRAP_TIGHT, WRAP_TOPBOTTOM, WRAP_BEHIND, WRAP_FRONT };
enum { POS_COLUMN, POS_PAGE, POS_MARGIN, POS_PARA };     /* what a float's position is from (x: column, page or margin; y: paragraph, page or margin) */
enum { PA_NONE, PA_START, PA_CENTER, PA_END };            /* or where it is aligned in that: left/top, centre, right/bottom */
enum { SH_RECT, SH_ROUNDRECT, SH_ELLIPSE, SH_LINE, SH_ARROW, SH_TRIANGLE, SH_DIAMOND, SH_RARROW, SH_STAR,
       SH_PENTAGON, SH_HEXAGON, SH_OCTAGON, SH_PARALLELOGRAM, SH_TRAPEZOID, SH_RTRIANGLE, SH_LARROW, SH_UARROW, SH_DARROW,
       SH_PLUS, SH_HEART, SH_CALLOUT, SH_CHEVRON, SH_HOMEPLATE, SH_STAR4, SH_N };
/* WP17/WP18: a picture's adjustments, an object's fill and effects and its text's settings (core/objfx.h
   works with them). All zero: none, as an object always was */
typedef struct {
  /* a picture's adjustments: corrections, colour, transparency, an artistic effect, its background */
  signed char bright, contrast;   /* -100..100 per cent (a:lum) */
  signed char sharp;        /* -100..100: softened .. sharpened (a14:sharpenSoften) */
  u8 cmode;                 /* PCM_ (objfx.h): greyscale, sepia, washout, black and white, recoloured */
  u32 recolor;              /* PCM_RECOLOR's colour, 0xRRGGBB (a:duotone from it to white) */
  u16 sat;                  /* saturation, per cent (0: unchanged) (a14:saturation) */
  u16 temp;                 /* colour temperature, kelvin (0: unchanged) (a14:colorTemperature) */
  u8 alpha;                 /* transparency, per cent (a:alphaModFix) */
  u8 art;                   /* ART_ (objfx.h): an artistic effect (a14:artistic...) */
  u8 bgkey, bgtol;          /* the background removed: colours near bgcolor (bgtol 0-100) made transparent (a:clrChange) */
  u32 bgcolor;
  /* a shape's fill (or a picture's under it): FT_ (objfx.h) solid (Image.fill), gradient, picture */
  u8 filltype, gtype;       /* gtype: GT_ linear, radial, rectangular */
  u16 gangle;               /* a linear gradient's direction, tenths of a degree clockwise from left to right */
  u8 ngs, gpos[4];          /* its stops: how many (2-4), where (per cent) */
  u32 gcol[4];
  u8 falpha;                /* the fill's transparency, per cent */
  /* effects: shadow, glow, soft edges (twips, tenths of a degree, per cent transparency) */
  u8 shadow, shalpha;       /* shadow 1: an outer shadow (a:outerShdw) */
  u16 shdist, shblur, shdir;
  u32 shcolor;
  u16 glow; u8 glalpha;     /* glow: its size, 0 none */
  u32 glcolor;
  u16 soft;                 /* soft edges' size, 0 none */
  /* the text in a text box or shape */
  u8 vert;                  /* TV_ (objfx.h): across, turned a quarter right, a quarter left */
  u8 anchor;                /* TA_ (objfx.h): at the top, middle, bottom */
  u8 hasins;                /* ins (left, top, right, bottom, twips) set; else Word's 0.1 and 0.05 inch */
  i32 ins[4];
  u16 link, seq;            /* linked text boxes: the chain's number (wps:txbx id), the place in it (0 holds the text) */
  u16 icon;                 /* SHX_ICON: which icon of objicons.c, from 1 */
  char *raw;                /* a picture's a14:imgProps kept as read (effects drawn into the picture already), or NULL */
} ObjFx;
struct SigLine;
typedef struct DocImage {
  u8 *data; u32 len;        /* the file as it came (PNG or JPEG); shapes and text boxes: none */
  u8 shared;                /* data belongs to an earlier entry (a copy made by changing the object) */
  u8 type;                  /* IMG_PNG, IMG_JPEG (image.h) */
  u8 kind;                  /* OBJ_ */
  u8 wrap;                  /* WRAP_ */
  u32 pw, ph;               /* the picture's size in pixels */
  i32 cx, cy;               /* the size it is shown at, twips */
  /* a float: where it goes and how far the text keeps away */
  u8 hrel, vrel, halign, valign;   /* POS_, PA_ */
  i32 x, y;                 /* from what hrel and vrel say, twips (when not aligned) */
  i32 dl, dr, dt, db;       /* the text's distance from it */
  /* how it looks */
  i32 crop[4];              /* left, top, right, bottom cut off: thousandths of a percent of the picture (Word's srcRect) */
  u16 rot;                  /* turned clockwise, tenths of a degree (0-3599) */
  u8 flip;                  /* 1 across, 2 upside down */
  u8 lstyle, lwidth;        /* its outline: PB_ kind (PB_NONE: none), eighths of a point */
  u32 lcolor;               /* 0xRRGGBB */
  u8 shape;                 /* shapes and text boxes: SH_ */
  u32 fill;                 /* their fill, 0xRRGGBB or COLOR_AUTO (none) */
  Story *text;              /* a text box's (or shape's) text, NULL if none */
  char alt[160];            /* alternative text (UTF-8) */
  /* a group: the objects in it (its own copies), placed in a space chw x chh that its cx x cy
     stretches over; each one's x, y is its top left there (its other placing fields unused) */
  struct DocImage *kids; int nkids;
  i32 chw, chh;
  /* the outline the text wraps round when wrapped tightly: npoly points, x and y 0-21600 across the
     object's unturned box (Word's wrapPolygon); none: the box itself, or a shape's own outline */
  i32 *poly; u16 npoly;
  i32 z;                    /* floating objects: the order they stack in, higher in front (equal ones in the
                               order their anchors come); Word's relativeHeight */
  struct ObjPack *pack;     /* (WP15a) a chart's or SmartArt's parts and model (core/chart.h), shared between the
                               entries copies make (counted); NULL for other objects */
  /* WP20 (core/ink.h; here rather than at the end so the packages adding fields there merge cleanly) */
  struct Freeform *ff;      /* a freeform shape's or ink stroke's paths (Image.shape SH_FREEFORM), shared between copies (counted); NULL none */
  u8 canvas;                /* CANVAS_GROUP: a drawing canvas (a group, wpc:wpc); CANVAS_BG: its background, its first object */
  u8 decorative;            /* (WP24) marked decorative: a screen reader passes over it (Word's adec:decorative) */
  u32 thfill, thline;       /* (WP13) fill and outline are theme colours (THR_ references, core/doctheme.h), 0 not */
  ObjFx fx;                 /* WP17/WP18: adjustments, fills, effects, text settings */
  /* WP19: */
  u8 warp;                  /* WordArt's Transform: the text bent (core/wordart.h's table of a:prstTxWarp names), 0 none */
  struct SigLine *sig;      /* a signature line's setup (core/wordart.h), its picture Quill's drawing of it; NULL for others */
} Image;
/* an object copied (dst a new entry): its text, objects and outline copied; its picture shared (it
   stays with the entry that brought it), or with own, copied too. 0 if out of memory (dst then holds
   nothing to free) */
int  img_clone(Image *dst, const Image *src, int own);
void img_release(Image *im);     /* what an entry holds freed (its picture if not shared) */
/* an object's box on the page (turned: the box round it), twips */
i32  obj_w(const Image *im);
i32  obj_h(const Image *im);
/* a point of the object's own (unturned, unflipped) w x h space (u, v) where it is in its box, and back */
void obj_point(const Image *im, i32 u, i32 v, i32 *bx, i32 *by);
void obj_unpoint(const Image *im, i32 bx, i32 by, i32 *u, i32 *v);
/* an object as the simple ones it is drawn as (a group: those in it, at their places): each one's
   own object (its settings), its size, its centre from the top left of the whole one's box, and how
   it is turned and flipped. Returns how many (at most max) */
typedef struct { const Image *im; i32 cx, cy, x, y; u16 rot; u8 flip; } ObjPart;
int  obj_parts(const Image *im, ObjPart *out, int max);
#define IS_PIC(t) ((u8)(t)[0] == 0xF3 && ((u8)(t)[1] & 0xFC) == 0xB0)       /* at a picture's first byte */
/* footnotes the same way: U+F4000 + n for Doc.notes[n]; their numbers come from their order */
#define MAX_NOTES 16384
#define IS_NOTE(t) ((u8)(t)[0] == 0xF3 && ((u8)(t)[1] & 0xFC) == 0xB4)
int  note_index(const char *t);          /* the note a reference names (t at its first byte) */
void note_utf8(char *out, int index);    /* a footnote reference: 4 bytes and a NUL */
/* endnotes: U+100000 + n for Doc.enotes[n] (F4 8x xx xx); numbered i, ii, iii... in the order the
   text refers to them, their text after the document's end */
#define IS_ENOTE(t) ((u8)(t)[0] == 0xF4 && ((u8)(t)[1] & 0xFC) == 0x80)
void enote_utf8(char *out, int index);   /* an endnote reference: 4 bytes and a NUL (read it with note_index) */
/* comments: where one starts and ends is marked in the text with U+F8000 + n and U+FC000 + n for
   Doc.comments[n]. The marks have no width; like pictures they move, copy and undo with the text */
#define MAX_COMMENTS 16384
typedef struct {
  char author[64], initials[12], date[24]; Story text;
  /* WP5: replies and Resolve (commentsExtended, commentsIds, commentsExtensible) */
  int parent;               /* a reply: Doc.comments index + 1 of the comment it answers (0: it starts a thread) */
  u8 done;                  /* resolved (w15:done) */
  u32 durable;              /* its durable id (w16cid), 0 none yet */
  char dateutc[24];         /* its time in UTC (w16cex:dateUtc), "" unknown */
} Comment;
#define IS_CSTART(t) ((u8)(t)[0] == 0xF3 && ((u8)(t)[1] & 0xFC) == 0xB8)
#define IS_CEND(t) ((u8)(t)[0] == 0xF3 && ((u8)(t)[1] & 0xFC) == 0xBC)
#define IS_MARK(t) ((u8)(t)[0] == 0xF3 && ((u8)(t)[1] & 0xF8) == 0xB8)       /* either */
void cmark_utf8(char *out, int index, int end);   /* comment index's start mark (end: its end mark), 4 bytes and a NUL */
int  pic_index(const char *t);           /* the picture a reference names (t at its first byte) */
void pic_utf8(char *out, int index);     /* 4 bytes and a NUL */

typedef struct { u32 para, off; } Pos;

#define MAX_FONTS 64
#define MAX_STYLES 128

struct Undo;
typedef struct Doc {
  Para **p;
  u32 np, pcap;
  Style styles[MAX_STYLES];
  int nstyles;
  char fonts[MAX_FONTS][48];    /* family names as the document asks for them */
  int nfonts;
  char **links; int nlinks;     /* hyperlinks' targets (UTF-8): a web or mail address, or "#name" for a place in the document */
  char **bmarks; int nbmarks;   /* bookmarks' names (UTF-8; CharFmt.bmark numbers them; names starting "_" are hidden ones) */
  PageSetup page;
  u32 ver;                  /* bumped by every change */
  int dirty;
  struct Undo *undo;
  char title[128], author[128];
  ListDef *lists; int nlists;
  Table *tables; int ntables;   /* only ever added to (undo may bring back paragraphs that use any) */
  Story hdr[HF_N], ftr[HF_N];   /* headers and footers (not in the undo history) */
  u8 titlepg;               /* the first page has its own (HF_FIRST) */
  u8 evenodd;               /* even pages have their own (HF_EVEN) */
  Image *images; int nimages;   /* only ever added to, like tables */
  Story *notes; int nnotes;     /* footnotes' text (only added to; editing replaces a note's paragraphs) */
  Comment *comments; int ncomments;   /* only added to; a comment whose marks are gone is not saved */
  struct { char author[64], date[24]; } *revs; int nrevs;   /* tracked changes' authors and dates */
  Story *enotes; int nenotes;   /* endnotes' text, as footnotes' */
  SectFmt *sects; int nsects;   /* section formats (only added to, so undo can bring any back) */
  PageBorder border;
  Watermark wm;
  u8 hyphenate;             /* automatic hyphenation (settings.xml w:autoHyphenation) */
  u8 pgfmt;                 /* page numbers' format: NF_ (0, NF_BULLET: as NF_DECIMAL) */
  u16 pgstart;              /* the first page's number (0 or 1: 1) */
  u32 pagecolor;            /* the pages' background colour: 0 none (white), else HL_ON | 0xRRGGBB (w:background) */
  signed char look[4];      /* the Design tab's last choices here (style set, fonts, spacing, colours; -1 none),
                               which Set as Default keeps for new documents (not saved in the file) */
  TableStyle *tstyles; int ntstyles;   /* table styles (only added to) */
  ParaFmt defpf; CharFmt defcf;   /* the document's defaults, under every style */
  u32 serial;               /* this document's own number (fragments know where they came from) */
  StoryEdit edit;           /* a story being edited in the body's place */
  struct KeptPart *kept; int nkept;   /* .docx parts kept verbatim, written back on saving (core/keep.h) */
  /* (work packages: new fields go below, at the end) */
  /* WP22a: (core/pages.h) settings.xml's page and hyphenation settings */
  u8 mirror;                /* w:mirrorMargins: left and right are inside and outside */
  u8 twoonone;              /* w:printTwoOnOne: two pages to a sheet */
  u8 bookfold;              /* w:bookFoldPrinting: a booklet (w:bookFoldRevPrinting: bookfold 2) */
  u8 gutterattop;           /* w:gutterAtTop */
  u16 bfsheets;             /* w:bookFoldPrintingSheets: sheets to a booklet (0: all) */
  u8 hynocaps;              /* w:doNotHyphenateCaps */
  u8 hylimit;               /* w:consecutiveHyphenLimit (0: no limit) */
  i32 hyzone;               /* w:hyphenationZone, twips (0: Word's 360) */
  u8 pbnohdr, pbnoftr;      /* w:bordersDoNotSurroundHeader, w:bordersDoNotSurroundFooter */
  NotePr fnpr, enpr;        /* (WP22b) the document's footnote and endnote options (settings.xml) */
  struct NoteSeps *nseps;   /* (WP22b) the document's own note separators (core/notes.h), NULL: Word's */
  struct DocInfo *info;     /* (WP24) the document's properties (core/info.h), NULL: none read or set */
  struct DocTheme *theme;   /* (WP13) the document's Office theme (core/doctheme.h), NULL: Office's own */
  u16 spfilter;             /* (WP13) the Styles pane's choices kept in the file (w:stylePaneFormatFilter), 0 none */
  u8 spsort;                /* (WP13) and its order (w:stylePaneSortMethod: SPS_, core/doctheme.h), 0 none */
  u8 tplkind;               /* (WP23) a template: 1 .dotx, 2 .dotm (core/blocks.h), 0 a document */
  u8 linkstyles;            /* (WP23) styles brought up to date from the attached template on opening (w:linkStyles) */
  char *attached;           /* (WP23) the attached template's file (w:attachedTemplate), malloc'd UTF-8, NULL none */
  struct Glossary *gloss;   /* (WP23) its building blocks (the glossary document, core/blocks.h), NULL none */
  /* WP14a: equations (core/math.h): the entries the text's references name (only ever added to), and
     settings.xml's m:mathPr kept verbatim */
  struct MathEq **maths; int nmaths, mathcap;
  char *mathpr;
  /* WP5 (core/review.h): tracked formatting changes, how changes and comments are shown, the people,
     move names, property changes of tables and sections kept verbatim, tracking formatting now */
  struct FmtRev *frevs; int nfrevs;   /* only added to (undo may bring back runs that refer to any) */
  struct RevView *rview;    /* Display for Review and Show Markup (NULL: All Markup, everything shown) */
  struct Person *people; int npeople;   /* people.xml: authors and how they signed in */
  char (*rvmoves)[40]; int nrvmoves;    /* moves' names (w:name of their ranges) */
  struct KeptChange *kchg; int nkchg;   /* w:tblPrChange, w:trPrChange, w:tcPrChange, w:sectPrChange ... */
  u16 rvtrack;              /* formatting changes are being tracked: Doc.revs index + 1 of their author, 0 not */
  u8 rvsettings;            /* settings.xml's w:revisionView read (bit 0) */
  /* WP1 fields (core/field.h): the records the field marks in the text name (only added to), every
     field showing its code (Alt+F9; not saved), and what the file says for the fields (docProps...) */
  struct Field *fields; int nfields;
  u8 fieldcodes;
  struct FieldInfo *finfo;
  /* WP2 (core/refs.h): the caption labels, AutoCaption and the authorities' categories (settings.xml w:captions) */
  struct RefsInfo *refs;
} Doc;
/* (WP13) styles (and the document's defaults, Doc.defpf and defcf) changed by edit(d, arg) as one undo
   step: every style's effective formatting worked out again and the text that had a style's look
   given its new one, as doc_set_style does (the defaults themselves are not in the undo history).
   0 if out of memory */
int  doc_restyle(Doc *d, void (*edit)(Doc *, void *), void *arg);
/* (WP13, WP23) what their Doc fields hold freed (doc_free calls them) */
void theme_free(Doc *d);
void blocks_free(Doc *d);

/* a new document with Quill's built-in styles and one empty paragraph; NULL if out of memory */
Doc  *doc_new(void);
void  doc_free(Doc *d);                          /* everything freed (a story being edited is dropped); NULL is fine */
int   doc_font(Doc *d, const char *family);      /* index of a font family, added if new */
int   doc_style(Doc *d, const char *id);         /* index of a style by id, -1 if none */
int   doc_style_by_name(Doc *d, const char *name);   /* by its shown name, ignoring case; -1 if none */
int   doc_add_style(Doc *d, const Style *s);     /* index; -1 if the table is full (not undoable: for building documents) */
/* every style's pf, cf (and ecmask) worked out again from what it and its bases set */
void  doc_resolve_styles(Doc *d);
/* a style made from its effective formatting (pf, cf): based on based (-1 none), setting what
   differs from it; its index, -1 if the table is full */
int   doc_add_style_from(Doc *d, const Style *s, int based);
/* every style's own settings worked out again from its effective formatting (for code that sets
   pf and cf directly, as the templates do) */
void  doc_styles_from_effective(Doc *d);
/* style k (-1: a new one) becomes s (its name, type, based, next, quick, pmask, cmask, opf, ocf; the
   id is made from the name when new or empty): the paragraphs and text using it, or styles based on
   it, change with it where they had its formatting (formatting of their own is kept). One undo
   step. Returns its index, -1 if it can't be (a loop of bases, no room) */
int   doc_set_style(Doc *d, int k, const Style *s);
/* style k deleted: its paragraphs take the style it was based on (Normal), its text loses the
   character style (keeping the look), styles based on it take its base. One undo step; 0 if it
   can't be (Normal, or not a style) */
int   doc_delete_style(Doc *d, int k);
/* a new style's settings from the formatting at p: the paragraph's and the character's, as far as
   they differ from base (a paragraph style) */
void  doc_style_from(Doc *d, Pos p, int base, Style *s);
/* a..b get character style k (0: none, back to the paragraph style's look) */
int   doc_char_style(Doc *d, Pos a, Pos b, int k);
/* a new list (bulleted or numbered, the usual indents per level); returns its number for ParaFmt.list, 0 if none */
int   doc_new_list(Doc *d, int numbered);
int   doc_add_list(Doc *d, const ListDef *l);    /* returns its number (1-based), 0 if the table is full */
/* lists ready made: bullets; 1. a. i.; 1) a) i); I. A. 1. a) (1); 1. 1.1. 1.1.1.; and for headings 1 1.1 1.1.1 */
enum { LP_BULLETS, LP_NUMBERS, LP_PARENS, LP_OUTLINE, LP_LEGAL, LP_HEADINGS, LP_N };
int   doc_list_preset(Doc *d, int preset);       /* a new list of that kind: its number, 0 if none */
/* paragraphs pa..pb in list (0: out of any list), each keeping its level, indented as its level is */
int   doc_list_apply(Doc *d, u32 pa, u32 pb, int list);
/* numbering from paragraph pi on (its list's later paragraphs too) starting again: at value (0: the
   list's start), as a list of its own */
int   doc_list_restart(Doc *d, u32 pi, int value);
/* paragraph pi's list (from it on) carrying on the numbering of the list of its kind before it; 0 if none */
int   doc_list_continue(Doc *d, u32 pi);
/* the heading styles numbered (1, 1.1, 1.1.1... with preset LP_HEADINGS, or another kind), or not
   (preset -1): one undo step */
int   doc_heading_numbering(Doc *d, int preset);
/* the whole document's look (Design tab), one undo step each: fonts for the body and headings,
   spacing between paragraphs, and style sets (fonts, spacing and the headings' colours) */
enum { TF_TIMES_ARIAL, TF_CALIBRI_CAMBRIA, TF_CALIBRI_CALIBRI, TF_CAMBRIA_CALIBRI, TF_ARIAL, TF_TIMES, TF_N };
enum { PS_NONE, PS_COMPACT, PS_TIGHT, PS_OPEN, PS_RELAXED, PS_DOUBLE, PS_N };
enum { SS_QUILL, SS_MODERN, SS_CLASSIC, SS_ELEGANT, SS_SIMPLE, SS_BOLD, SS_N };
int   doc_theme_fonts(Doc *d, int pair);
int   doc_para_spacing(Doc *d, int preset);
int   doc_style_set(Doc *d, int set);
/* colour sets: the headings', title's and subtitle's colours (one undo step); and themes, a style
   set, fonts and colours together. look_info: what a style set (kind 0), theme (1) or colour set (2)
   looks like, for the galleries' pictures: its heading and body fonts, heading and title colours
   (COLOR_AUTO: automatic); NULLs fine */
enum { CS_QUILL, CS_BLUE, CS_BLUEGREEN, CS_GREEN, CS_ORANGE, CS_RED, CS_VIOLET, CS_GREY, CS_N };
enum { TH_QUILL, TH_AMIGA, TH_WORKBENCH, TH_BOING, TH_KICKSTART, TH_PAPER, TH_LAGOON, TH_VELVET, TH_N };
int   doc_color_set(Doc *d, int set);
int   doc_theme(Doc *d, int theme);
void  look_info(int kind, int which, const char **head, const char **body, u32 *hcolor, u32 *tcolor);
/* copying and pasting with formatting: a fragment of a document (its own copy of everything it needs) */
typedef struct Fragment Fragment;
enum { PASTE_KEEP, PASTE_MERGE, PASTE_TEXT };     /* keep its formatting; take the destination's, keeping bold, italic and underline; text only */
Fragment *doc_fragment(Doc *d, Pos a, Pos b);     /* NULL if empty or out of memory */
void  fragment_free(Fragment *f);               /* NULL is fine */
const char *fragment_text(const Fragment *f, u32 *n);   /* its text, UTF-8 ("\n" between paragraphs) */
/* f pasted at *at (moved after it), one undo step; 0 if out of memory */
int   doc_paste(Doc *d, Pos *at, const Fragment *f, int mode);
int   doc_add_table(Doc *d, const Table *t);     /* its number for Cell.table, 0 if out of memory */
/* a table style: its number for Table.tstyle (an existing one with the same id is replaced), 0 if full */
int   doc_add_tstyle(Doc *d, const TableStyle *s);
int   doc_tstyle(Doc *d, const char *id);        /* by id: its number, 0 if none */
/* a picture (takes the file's buffer, freed even on failure); its index, -1 if it isn't a PNG or JPEG
   or out of memory. cx, cy: shown size in twips (0: its pixels at 96 dpi) */
int   doc_add_image(Doc *d, u8 *data, u32 len, i32 cx, i32 cy);
void  img_defaults(Image *im);                   /* an in-line picture's settings, nothing else set */
/* a copy of an object as a new entry (its picture shared, its text copied); its index or -1 */
int   doc_add_object(Doc *d, const Image *proto);
/* an entry that takes im as it is (what it holds is the document's now); its index, or -1 */
int   doc_take_object(Doc *d, Image *im);
/* the n floating objects whose references are at at[] (box: each one's x, y, w, h on its page, twips)
   made one group, floating from the page where they were, its reference where the first one's was:
   one undo step; its index, or -1 */
int  doc_group(Doc *d, const Pos *at, const i32 *box, int n);
/* the group whose reference is at at (its box's top left at gx, gy on the page) taken apart: the
   objects in it float from the page where they were, their references in its place. One undo step;
   how many, 0 if it isn't a group */
int  doc_ungroup(Doc *d, Pos at, i32 gx, i32 gy);
/* a new shape (SH_), or with shape -1 a text box, cx x cy twips, in front of the text; its index or -1 */
int   doc_add_shape(Doc *d, int shape, i32 cx, i32 cy);
/* the object whose reference is at at changed to im (a new entry put in its place): one undo step; its index or -1 */
int   doc_set_object(Doc *d, Pos at, const Image *im);
Story *story_copy(const Story *s);               /* malloc'd, NULL if out of memory */
/* a new, empty footnote; its index, or -1 */
int   doc_add_note(Doc *d);
/* a new, empty endnote; its index, or -1 */
int   doc_add_endnote(Doc *d);
/* a new comment (no text yet, no marks in the text); its index, or -1 */
int   doc_add_comment(Doc *d, const char *author, const char *initials, const char *date);
/* a comment on a..b with this text (UTF-8, one paragraph): the marks go in (one undo step); its index or -1 */
int   doc_comment(Doc *d, Pos a, Pos b, const char *text, const char *author, const char *initials, const char *date);
/* the comment p is in (the innermost), or -1; where its marks are */
int   doc_comment_at(Doc *d, Pos p, Pos *start, Pos *end);
/* where a comment's start (end: its end) mark is; 0 if it isn't in the text */
int   doc_comment_mark(Doc *d, int k, int end, Pos *at);
/* take a comment's marks out (one undo step) */
int   doc_delete_comment(Doc *d, int k);
Table *doc_table(Doc *d, const ParaFmt *pf);     /* a paragraph's table, NULL if none */
/* the format of the section paragraph pi is in */
const SectFmt *doc_sect(Doc *d, u32 pi);
/* the section paragraph pi is in is the document's last */
int  doc_last_sect(Doc *d, u32 pi);
/* a section's page setup, header or footer (footer, HF_ which), and whether its first page has its
   own: its own, or the document's (last: it is the last section, whose are the document's) */
const PageSetup *sect_page(const Doc *d, const SectFmt *s, int last);
Story *sect_story(Doc *d, const SectFmt *s, int last, int footer, int which);
int  sect_titlepg(const Doc *d, const SectFmt *s, int last);
/* section format number k (Doc.sects) gets headers and footers of its own (copies of the document's), or
   (own 0) has the document's again: 0 if out of memory */
int  doc_sect_own_hf(Doc *d, int k, int own);
/* the story an SK_ kind and index name (sect: a section's own header or footer), NULL if none */
Story *doc_story(Doc *d, int kind, int index, int sect);
/* a story edited in the body's place (fpage, fpages: what page fields show): 0 if one already is, it
   doesn't exist, or out of memory. An empty one starts as one paragraph in its usual style */
int  doc_story_begin(Doc *d, int kind, int index, int sect, u32 fpage, u32 fpages);
/* done: the edited paragraphs go into the story (keep; an empty header or footer is none), or are
   dropped; the body comes back with its undo history. 0 if no story was being edited */
int  doc_story_end(Doc *d, int keep);
/* a new section format (only ever added to); its number for ParaFmt.sect, 0 if out of memory */
int  doc_add_sect(Doc *d, const SectFmt *s);
/* where the section paragraph pi is in ends (the paragraph holding its format, or the last one) */
u32  doc_sect_end(Doc *d, u32 pi);

/* paragraph primitives (no undo; the editing functions below use them) */
Para *para_new(const ParaFmt *pf, const CharFmt *cf);   /* empty, its one run in cf; NULL if out of memory */
void  para_free(Para *p);
Para *para_copy(const Para *p);                  /* a deep copy; NULL if out of memory */
int   para_append(Para *p, const char *s, u32 n, const CharFmt *f);   /* 0 if out of memory */
CharFmt *para_fmt_at(Para *p, u32 off);          /* the formatting a character at off has */
CharFmt *para_fmt_before(Para *p, u32 off);      /* what typing at off continues (the run before it) */
void  para_normalise(Para *p);                   /* merge equal neighbouring runs, drop empty ones */
void  para_touch(Para *p);                       /* (WP13) changed behind the editing functions' back: a fresh version for the layout */
void  cf_apply(CharFmt *c, unsigned mask, const CharFmt *v);   /* the fields mask names, from v */
void  pf_apply(ParaFmt *p, unsigned mask, const ParaFmt *v);   /* the groups mask names (PM_), from v */
int   doc_insert_para(Doc *d, u32 at, Para *p);  /* takes ownership; 0 if out of memory */
void  story_free(Story *s);                      /* its paragraphs freed, s left empty (not freed itself) */
/* a header or footer of one paragraph of text in style (index), aligned; "{page}" and "{pages}" in the
   text become the fields. Empty text: none. 0 if out of memory */
int   story_set_text(Doc *d, Story *s, const char *text, int style, int align);
/* the plain text of a story (fields as "{page}", "{pages}"; paragraphs joined by " / "), malloc'd */
char *story_text(const Story *s);
/* s's text set to text ('\n' between paragraphs), the paragraphs and characters formatted as like's
   first (style 0 if like is empty or NULL): 0 if out of memory */
int   story_set_paras(Doc *d, Story *s, const char *text, const Story *like);
/* s's text with '\n' between paragraphs (fields as story_text gives them), malloc'd */
char *story_paras(const Story *s);

/* ---- edits, all undoable. Each returns 0 if nothing could be done (out of memory, bad position) */
/* insert UTF-8 at *at (moved past the insertion). "\n" starts a new paragraph (same style, or the
   style's next style at the paragraph's end); "\v" is a line break; other control characters but
   tab are dropped.
   f: the formatting to use, or NULL to continue the formatting at *at */
int  doc_insert(Doc *d, Pos *at, const char *s, u32 n, const CharFmt *f);
int  doc_delete(Doc *d, Pos a, Pos b);           /* the text between a and b (either order) */
/* the CM_ fields mask names set from val on the text between a and b (either order) */
int  doc_format(Doc *d, Pos a, Pos b, unsigned mask, const CharFmt *val);
/* hyperlinks. doc_link: the link number (CharFmt.link) for a target, added to Doc.links if new; 0
   if out of memory. doc_link_url: a link number's target ("" for 0 or a bad number) */
int  doc_link(Doc *d, const char *url);
const char *doc_link_url(const Doc *d, int link);
/* the link at p (the character after it, or before it at a link's end): its number, and in *a, *b
   the stretch of the paragraph it covers; 0 if p isn't on a link */
int  doc_link_at(const Doc *d, Pos p, Pos *a, Pos *b);
/* a..b made a link to url, in the Hyperlink character style (made if the document hasn't one); with
   a == b, text (if any) is put in at a first and becomes the link. url NULL or "": the links on
   a..b removed, and with them the Hyperlink style's look. One undo step; 0 if nothing was done.
   *end (if not NULL) gets the end of the link */
int  doc_set_link(Doc *d, Pos a, Pos b, const char *url, const char *text, Pos *end);
/* bookmarks: named stretches of text (the runs whose CharFmt.bmark numbers the name), so they move
   with the text and its undo. doc_bmark: the number for a name, added if new (0: out of memory);
   doc_bmark_name: a number's name ("" if none). doc_bmark_find: where the named one is (its first
   stretch, which may run on over paragraphs); 0 if it isn't in the text. doc_bmark_at: the bookmark
   at p (or ending just before it) and its stretch, 0 if none */
int  doc_bmark(Doc *d, const char *name);
const char *doc_bmark_name(const Doc *d, int k);
int  doc_bmark_find(const Doc *d, const char *name, Pos *a, Pos *b);
int  doc_bmark_at(const Doc *d, Pos p, Pos *a, Pos *b);
/* a..b made the bookmark name (taken off wherever it was before); name NULL or "" with a..b: the
   bookmarks there taken off. One undo step; 0 if a..b is empty or out of memory */
int  doc_set_bmark(Doc *d, Pos a, Pos b, const char *name);
/* the named bookmark taken off the text (one undo step); 0 if it wasn't there */
int  doc_remove_bmark(Doc *d, const char *name);
/* toggle a flag (bold...) over a range: set if any character lacks it, else clear */
int  doc_toggle(Doc *d, Pos a, Pos b, unsigned flag);
/* the PM_ groups mask names set from val on paragraphs pa..pb (inclusive) */
int  doc_para_format(Doc *d, u32 pa, u32 pb, unsigned mask, const ParaFmt *val);
/* give paragraphs pa..pb (inclusive) a paragraph style: its paragraph formatting, and its character
   formatting wherever the text had the old style's (direct formatting is kept) */
int  doc_apply_style(Doc *d, u32 pa, u32 pb, int style);

/* font sizes one step up (dir > 0) or down, as Word's grow and shrink buttons */
int  doc_grow(Doc *d, Pos a, Pos b, int dir);
/* the text back to its paragraph style's character formatting */
int  doc_clear_format(Doc *d, Pos a, Pos b);
/* change the case of letters (ASCII and Latin-1 ones) */
enum { CASE_SENTENCE, CASE_LOWER, CASE_UPPER, CASE_TITLE, CASE_TOGGLE };
int  doc_change_case(Doc *d, Pos a, Pos b, int mode);
/* paragraphs pa..pb (inclusive, none in a table) sorted A to Z, ignoring case */
int  doc_sort(Doc *d, u32 pa, u32 pb);

/* tracked changes: the author and date of new ones (an index for CharFmt.rid; the same pair gives the same) */
int  doc_add_rev(Doc *d, const char *author, const char *date);
/* a deletion while tracking: inserted text (anyone's) between a and b goes, the rest is marked deleted
   by rid; paragraphs are not joined. One undo step */
int  doc_track_delete(Doc *d, Pos a, Pos b, int rid);
/* accept (else reject) the changes between a and b: an accepted insertion or rejected deletion
   becomes plain text, an accepted deletion or rejected insertion goes. One undo step; 0 if none */
int  doc_resolve(Doc *d, Pos a, Pos b, int accept);
/* the next (dir > 0) or previous tracked change from p: its extent (one run of one kind); 0 if none */
int  doc_next_change(Doc *d, Pos p, int dir, Pos *a, Pos *b);

/* replace paragraphs [p0, p0 + nold) with nnew new ones (taken over, even on failure) */
int  doc_replace(Doc *d, u32 p0, u32 nold, Para **nw, u32 nnew, Pos where);

/* undo: edits between begin and end make one step (typing coalesces by itself) */
void doc_begin(Doc *d, Pos caret);
void doc_end(Doc *d, Pos caret);
int  doc_can_undo(Doc *d);
int  doc_can_redo(Doc *d);
int  doc_undo(Doc *d, Pos *caret);               /* caret: where the edit was */
int  doc_redo(Doc *d, Pos *caret);
void doc_clear_undo(Doc *d);                     /* the history forgotten (after loading a document) */

/* positions */
int  pos_cmp(Pos a, Pos b);                      /* -1, 0 or 1: a before, at or after b */
Pos  pos_clamp(Doc *d, Pos p);                   /* moved into the document (to the end of what it is past) */
Pos  doc_start(Doc *d);
Pos  doc_end_pos(Doc *d);
Pos  pos_left(Doc *d, Pos p);                    /* one character */
Pos  pos_right(Doc *d, Pos p);
Pos  pos_word_left(Doc *d, Pos p);               /* to the start of this word or the one before */
Pos  pos_word_right(Doc *d, Pos p);              /* to the start of the next word */
/* the text between a and b as UTF-8 with "\n" between paragraphs (malloc'd) */
char *doc_text(Doc *d, Pos a, Pos b, u32 *len);
u32  doc_count_words(Doc *d);                    /* in the body only */

/* find: the next match from 'from' (wrapping round once), case-insensitive for Latin letters unless
   FIND_CASE; FIND_WORD: whole words only. 1 and the match in *a..*b, or 0 */
enum { FIND_CASE = 1, FIND_WORD = 2, FIND_BACK = 4 };
int  doc_find(Doc *d, Pos from, const char *what, unsigned flags, Pos *a, Pos *b);
/* replace every match with 'with' (one undo step); returns how many */
u32  doc_replace_all(Doc *d, const char *what, const char *with, unsigned flags);

/* (WP12) the undo history read, for the Undo and Redo lists, Go Back and Repeat. What an action did
   (UK_): typing, inserting or deleting text, replacing, character or paragraph formatting, styles.
   doc_undo_count: how many steps can be undone (redo: redone). doc_undo_step: step i (0 the newest)
   of either list: its kind (-1: no such step), where the caret was before and after it, and its
   serial (from one counter: newer steps have higher ones). doc_undo_kind: the newest step named so.
   doc_undo_typed: the text the typing steps newer than serial since typed (malloc'd), NULL if none */
enum { UK_EDIT = 1, UK_TYPING, UK_INSERT, UK_DELETE, UK_REPLACE, UK_FORMAT, UK_PARA, UK_STYLE, UK_REPLACEALL, UK_N };
int   doc_undo_count(Doc *d, int redo);
int   doc_undo_step(Doc *d, int redo, int i, Pos *before, Pos *after, u32 *serial);
void  doc_undo_kind(Doc *d, int kind);
char *doc_undo_typed(Doc *d, u32 since);

#endif
