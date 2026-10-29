/*
 * pages.h - WP22a (docs/wordcraft-gap.md): pages, sections and columns. The page as it is laid out
 * (gutter, mirror margins, two pages to a sheet, book fold), columns of their own widths and their
 * presets, page borders side by side in every one of Word's line kinds (and its art borders, drawn as
 * a pattern), per section; picture watermarks; hyphenation's options (zone, capitals, consecutive
 * hyphens) and manual hyphenation; and how all of it is read from and written to .docx (w:sectPr's
 * w:pgMar, w:pgBorders, w:cols, w:vAlign, w:rtlGutter; settings.xml; a header's picture watermark).
 * Written from Word's documented behaviour (ECMA-376 part 1, 17.6 and 17.15).
 */
#ifndef QUILL_PAGES_H
#define QUILL_PAGES_H

#include "layout.h"
#include "xml.h"

#define VA_JUSTIFY 3                     /* SectFmt.valign: the space shared out between the paragraphs */
enum { PBS_TOP, PBS_LEFT, PBS_BOTTOM, PBS_RIGHT };      /* PageBorder.side[] */
enum { PBD_ALL, PBD_FIRST, PBD_NOTFIRST };              /* PageBorder.display */
enum { MP_NORMAL, MP_MIRROR, MP_TWOONONE, MP_BOOKFOLD };    /* Page Setup's "Multiple pages" */

/* ---- the page as it is laid out */
int  pages_multi(const Doc *d);                  /* MP_ */
void pages_set_multi(Doc *d, int mp);
/* the page a section with page setup src has as page number page (from 0) of the document: half the
   sheet with two pages to a sheet (portrait: the upper or lower half) or a book fold (the sheet
   turned landscape, folded down the middle); the gutter added to the left (inside) margin, or the
   top with Doc.gutterattop, or the right with rtl; left and right swapped on even pages with mirror
   margins and book folds. Not while a story is edited (Doc.edit) */
void pages_effective(const Doc *d, const PageSetup *src, int rtl, u32 page, PageSetup *out);
/* the same kept in L (src itself when nothing changes it): for layout.c */
const PageSetup *pages_eff(Layout *L, const PageSetup *src, const SectFmt *s, u32 page);
void pages_layout_free(Layout *L);

/* ---- columns */
/* section s's columns across body twips: how many, each one's left edge from the text area's and its
   width (x, w: MAX_SECT_COLS each). Columns narrower than half an inch, or no s: one */
int  pages_cols(const SectFmt *s, i32 body, i32 *x, i32 *w);
/* Word's presets: one, two, three equal columns; Left (a narrow one, then a wide one twice as wide);
   Right (wide, then narrow). pages_col_preset_of: the preset s is (-1: none of them) */
enum { CP_ONE, CP_TWO, CP_THREE, CP_LEFT, CP_RIGHT, CP_N };
void pages_col_preset(SectFmt *s, int preset, i32 body);
int  pages_col_preset_of(const SectFmt *s, i32 body);
/* s's columns made equal again (n of them, gap apart) or given widths (n widths and spaces after) */
void pages_col_set(SectFmt *s, int n, const i32 *w, const i32 *sp, int equal, i32 gap);

/* ---- page borders */
/* side k of a border (its own, or the border's one kind) */
void pages_pb_side(const PageBorder *pb, int k, PBSide *out);
/* the border of a section (its own, or the document's), and of a page (NULL: none there) */
const PageBorder *pages_sect_border(const Doc *d, const SectFmt *s, int last);
const PageBorder *pages_page_border(Layout *L, u32 page);
/* pb made a border of kind val (Word's name; NULL or "none": no border) on the sides in mask (1 <<
   PBS_), width in eighths of a point (art: points), space in twips */
void pages_pb_set(PageBorder *pb, const char *val, int width, u32 color, i32 space, int mask, int frompage);
int  pages_pb_mask(const PageBorder *pb);        /* the sides it has (1 << PBS_) */
int  pages_pb_style(const char *val);            /* the PB_ nearest to a kind */
int  pages_pb_art(const char *val);              /* 1 for an art border (a picture repeated) */
extern const char *const pages_pb_vals[];        /* Word's line kinds (art-free), for lists */
extern const int pages_pb_nvals;
extern const char *const pages_pb_arts[];        /* a few of Word's art borders */
extern const int pages_pb_narts;
/* the border on a page as rectangles to fill (twips from the page's top left, colour 0xRRGGBB);
   *out malloc'd (free it), the count returned (0: none, or out of memory) */
typedef struct { i32 x0, y0, x1, y1; u32 color; } PBRect;
int  pages_border_rects(Layout *L, u32 page, PBRect **out);

/* ---- watermarks */
int  pages_wm_on(const Doc *d);                  /* a watermark of words or a picture */
/* the picture watermark becomes a copy of data (PNG or JPEG; the words go): 0 if it isn't one or out of memory */
int  pages_wm_picture(Doc *d, const u8 *data, u32 len);
void pages_wm_clear(Doc *d);                     /* no watermark */
/* the picture watermark's box on a page with page setup pg: centred on the text area, at its scale
   (0: as big as fits) */
void pages_wm_box(const Doc *d, const PageSetup *pg, i32 *x, i32 *y, i32 *w, i32 *h);
void pages_washout(u8 *rgb, u32 npix);           /* RGB made pale as Word's washout */
/* the picture file now being drawn washed out (core/pdf.c notes it, core/raster.c asks); NULL none */
void pages_wash_mark(const u8 *data);
int  pages_wash_is(const u8 *data);

/* ---- hyphenation */
i32  pages_hyph_zone(const Doc *d);              /* twips */
/* whether automatic hyphenation may break this word (word: its letters, len bytes) on a new line
   after lines l[0..nl) whose boxes are b: not capitals with Doc.hynocaps, not after hylimit lines
   ending in hyphens */
int  pages_hyph_allowed(const Doc *d, const char *word, u32 len, const Line *l, u32 nl, const Box *b);
/* the paragraphs' layouts made again (after the options change) */
void pages_hyph_changed(Doc *d);
/* manual hyphenation: from p on, the next word that starts a line and could have been broken at the
   end of the line before (it is long enough, the hyphenation zone is left there and part of it fits):
   where it starts, its length, the places a hyphen may go (brk[k]: before byte k, 40 bytes) and the
   one suggested (the longest start that fits). 0 if there is none */
int  pages_next_manual(Layout *L, Pos from, Pos *at, u32 *len, u8 *brk, u32 *suggest);

/* ---- .docx (core/docx.c calls these) */
/* an element of a w:sectPr (x at its START): read to its END and 1 if it is one of these (w:cols,
   w:vAlign, w:pgBorders, w:pgMar, w:rtlGutter); pg: the page setup it sets (the document's for the
   body's) */
int  pages_read_sect(Xml *x, Doc *d, SectFmt *sf, PageSetup *pg, int body);
void pages_read_setting(Doc *d, Xml *x);         /* each start tag of settings.xml */
void pages_read_done(Doc *d);                    /* after the whole document is read */
void pages_write_pgmar(Buf *o, const PageSetup *pg);
void pages_write_pgborders(Buf *o, const Doc *d, const SectFmt *s, int last);
void pages_write_cols(Buf *o, const SectFmt *s);
void pages_write_valign(Buf *o, const SectFmt *s);
void pages_write_sect_tail(Buf *o, const SectFmt *s);   /* after w:titlePg: w:rtlGutter */
/* settings.xml at the places docx_write_settings_ext names (2 after w:autoHyphenation, 4 after
   w:evenAndOddHeaders), and 10 right after w:displayBackgroundShape (docx_save calls that one itself,
   since w:mirrorMargins... come before every other package's slot 1 element in the schema) */
void pages_write_settings(const Doc *d, Buf *b, int where);
/* a header's picture watermark: the run (with a w:pict) and the media part's name ("" none) and type */
void pages_write_wm_picture(Buf *o, const Doc *d);
const char *pages_wm_part(const Doc *d, const char **ctype);
#define PAGES_WM_RELID "rIdWm1"

#endif
