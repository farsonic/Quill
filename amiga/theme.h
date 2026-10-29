/*
 * theme.h - the look Quill's own controls share (ribbon, navigation pane, status bar, rulers, File
 * page): WordCraft's light theme colours as pens, a sans-serif interface font, and icons.
 *
 * The icons are Quill's own, drawn in code from short vector descriptions (see theme.c); no icon
 * from any other product is copied or traced.
 */
#ifndef QUILL_THEME_H
#define QUILL_THEME_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/classusr.h>

enum {
  T_RIBBON, T_TABSTRIP, T_CANVAS, T_PANEL, T_BORDER, T_BORDER2, T_TEXT, T_DIM, T_DISABLED, T_ICON,
  T_HOVER, T_PRESSED, T_CHECKED, T_ACCENT, T_WHITE, T_INPUTBORDER, T_GROUPLABEL, T_RULER, T_RULERMARGIN,
  T_RULERTICK, T_STATUS, T_RED, T_GREEN, T_ORANGE, T_YELLOW, T_NAVHOVER, T_SELECTION, T_BALLOON, T_MARKUP,
  T_ACCENTDARK, T_ACCENTHOVER, T_ACCENTTEXT, T_ONACCENT,
  T_NPENS
};

/* the pens on obj's screen (obtained on first use, kept until theme_release with the last user) */
void theme_setup(Object *obj);
/* one of Quill's bitmap fonts (QuillSans.font...) at a pixel height; NULL if it can't be opened */
struct TextFont *theme_open_font(const char *name, UWORD ysize);
void theme_close_font(struct TextFont *tf);   /* for theme_open_font's */
void theme_free_fonts(void);                  /* at the end: the fonts Quill loaded itself */
void theme_set_large(int on);            /* the interface's text large (when the window next opens) */
int theme_fonts_ok(void);                 /* Quill's interface font could be opened */
const char *theme_fonts_why(void);       /* why it couldn't be, "" if it could */
int theme_aatext(void);                   /* AAText is running (Quill's fonts then drawn smoothly by it) */
void theme_no_aatext(void);               /* Quill's fonts not given to AAText */
void theme_no_hinting(void);              /* the fonts Quill makes itself unhinted */
struct Face;
/* an installed TrueType face at ppem64/64 pixels to the em (close with theme_close_font) */
struct TextFont *theme_open_face(struct Face *f, LONG ppem64);
/* a line of Latin-1 text in a face, without a whole font (previews): its width, and drawn with its
   baseline at y (0 if it can't be) */
LONG theme_preview_width(struct Face *f, const char *text, LONG ppem64);
int theme_preview(struct RastPort *rp, struct Face *f, const char *text, LONG ppem64, LONG x, LONG y, LONG maxw, LONG pen);
/* the dialogs' font (kept until the end) and background (an image spec) */
struct TextFont *theme_dialog_font(void);
const char *theme_dialog_back(void);
/* the dark interface theme (WordCraft's dark tokens) or the light one; objects draw again themselves */
void theme_set_dark(int dark);
int  theme_is_dark(void);
void theme_cleanup(Object *obj);       /* (also removes its event handler) */
void theme_handler(Object *obj, struct MUI_EventHandlerNode *ehn);   /* after theme_setup: the object's event handler */
/* the pen of a T_ colour, for SetAPen and the theme_ drawing calls */
LONG tpen(int which);
/* the interface fonts: regular and bold at the normal size, and a large one (headings) */
struct TextFont *theme_font(void);
struct TextFont *theme_bold(void);
struct TextFont *theme_big(void);
struct TextFont *theme_small(void);      /* the rulers' numbers */
/* text in the interface font: its width, and drawn with its top at y */
LONG theme_textlen(struct RastPort *rp, struct TextFont *tf, const char *s);
void theme_text(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG pen);
void theme_rect(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);    /* outline */
void theme_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);

/* icons: drawn in a size x size pixel square at x, y; dim: disabled */
enum {
  IC_NONE, IC_PASTE, IC_CUT, IC_COPY, IC_BRUSH, IC_BOLD, IC_ITALIC, IC_UNDERLINE, IC_STRIKE, IC_SUB, IC_SUPER,
  IC_FONTCOLOR, IC_HIGHLIGHT, IC_GROW, IC_SHRINK, IC_CASE, IC_CLEAR, IC_BULLETS, IC_NUMBERS, IC_OUTDENT,
  IC_INDENT, IC_MARKS, IC_LEFT, IC_CENTER, IC_RIGHT, IC_JUSTIFY, IC_SPACING, IC_FIND, IC_REPLACE, IC_SELECT,
  IC_PAGEBREAK, IC_TABLE, IC_PICTURE, IC_COMMENT, IC_HEADER, IC_FOOTER, IC_PAGENUM, IC_DATE, IC_SYMBOL,
  IC_MARGINS, IC_ORIENT, IC_SIZE, IC_BREAKS, IC_TOC, IC_UPDATE, IC_FOOTNOTE, IC_ENDNOTE, IC_NEXTNOTE,
  IC_SPELLING, IC_WORDCOUNT, IC_DELETE, IC_PREV, IC_NEXT, IC_SHOWCOMMENTS, IC_TRACK, IC_ACCEPT, IC_REJECT,
  IC_RULER, IC_NAVPANE, IC_ZOOM, IC_ZOOM100, IC_PAGEWIDTH, IC_ONEPAGE, IC_ROWABOVE, IC_ROWBELOW, IC_COLLEFT,
  IC_COLRIGHT, IC_DELROW, IC_MERGE, IC_SPLIT, IC_HEADROW, IC_ALIGNTOP, IC_ALIGNMID, IC_ALIGNBOT, IC_UNDO,
  IC_REDO, IC_SAVE, IC_NEW, IC_OPEN, IC_PDF, IC_PRINT, IC_INFO, IC_OPTIONS, IC_BACK, IC_ABOUT, IC_SEARCH,
  IC_PICSIZE, IC_ARROWDOWN, IC_ARROWLEFT, IC_ARROWRIGHT, IC_ARROWUP, IC_FOCUS, IC_PRINTLAYOUT, IC_HOME,
  IC_EXPORT, IC_SHADING, IC_SORT, IC_READ, IC_WEB, IC_DRAFT, IC_GRID, IC_DARK, IC_MULTIPAGE,
  IC_COLUMNS, IC_LINENUM, IC_HYPHEN, IC_DROPCAP, IC_WATERMARK, IC_PAGEBORDER, IC_PAGEALIGN,
  IC_TSTYLES, IC_TBORDERS, IC_PENSTYLE, IC_PENWIDTH, IC_PENCOLOR, IC_TSELECT, IC_TGRID, IC_TPROPS, IC_SPLITTABLE, IC_AUTOFIT,
  IC_DISTROWS, IC_DISTCOLS, IC_CELLMARGINS, IC_TOTEXT, IC_FORMULA, IC_TOTABLE, IC_ROWH, IC_COLW,
  IC_WRAP, IC_POSITION, IC_ROTATE, IC_CROP, IC_ALTTEXT, IC_SHAPES, IC_TEXTBOX, IC_SHAPEFILL, IC_SHAPEOUTLINE, IC_EDITTEXT, IC_LAYOUTOPTS,
  IC_FONTDLG, IC_PARADLG, IC_MULTILIST, IC_PBORDERS, IC_PSHADE, IC_STYLESPANE, IC_NEWSTYLE, IC_MODSTYLE, IC_STYLESET, IC_THEMEFONTS, IC_PSPACING, IC_PASTESPEC,
  IC_PAGECOUNT, IC_CLOSEHF, IC_GROUP, IC_UNGROUP, IC_LINK, IC_BLANKPAGE, IC_GOTO, IC_ADDTEXT, IC_FORWARD, IC_BACKWARD, IC_SELPANE, IC_ALIGNOBJ, IC_BOOKMARK, IC_XREF, IC_COVER, IC_THEMES, IC_COLORSETS, IC_PAGECOLOR, IC_SETDEFAULT, IC_CBORDER, IC_N
};
void theme_icon(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, int dim);
/* the same from a drawing in icons[]' language (theme.c), for icons a work package draws itself */
void theme_icon_src(struct RastPort *rp, const char *src, LONG x, LONG y, LONG size, int dim);
/* the same in one pen (white on the File page's blue; its white parts in the accent colour) */
void theme_icon_pen(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, LONG pen);

/* help bubble text in Quill's style: name in bold on the first line, then body (already translated)
   wrapped at about 46 characters. Returns a static buffer, good until the next call (MUI copies it
   into the bubble straight away) */
const char *theme_bubble(const char *name, const char *body);
#endif
