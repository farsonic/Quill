/*
 * pdf.h - export a laid-out document as PDF, and its pages as pictures (PNG, IFF ILBM).
 *
 * Every word is placed where the layout put it, in the TrueType font layout measured it with,
 * embedded in the file (so the PDF looks the same everywhere and breaks lines exactly as Quill
 * does). Coordinates stay integer twips (a 1/20 scale in the page's matrix), so no floating point
 * is needed here either.
 *
 * Fonts (WP29): each embeddable face goes in as a subset (core/pdffont.c: only the glyphs used) in
 * a Type 0 font with Identity-H encoding, the text naming glyphs by their ids, so any character the
 * font has prints (Greek, Cyrillic, symbols...), not only Windows-1252's; a ToUnicode CMap maps each
 * glyph back to its character, so copying and searching in a PDF reader find the words. A face whose
 * licence forbids embedding is named only, as a simple font in Windows-1252 (others print as '?').
 *
 * Navigation: links to places in the document (a "#bookmark" hyperlink, a cross-reference, a table
 * of contents entry, a footnote or endnote reference and the note's number back to it) jump inside
 * the PDF; web and mail links open their address. The headings make the document outline (the
 * reader's bookmarks panel), the bookmarks named destinations, and the document information has its
 * title, author, subject and keywords.
 */
#ifndef QUILL_PDF_H
#define QUILL_PDF_H

#include "layout.h"

/* what an export takes in: the pages (from 1, both inclusive; 0 for first: from the first, 0 for
   last: to the end), whether headings make the outline, the subject and keywords for the document
   information (UTF-8, "" none), and for page pictures the resolution (dots per inch, 18-600) and
   format (PIMG_) */
enum { PIMG_PNG, PIMG_ILBM };
typedef struct {
  u32 first, last;
  u8 outline;
  char subject[128], keywords[128];
  u16 dpi;
  u8 format;
} PdfOpts;
/* the options Export PDF and Export Image use (all pages, an outline, 150 dpi, PNG); the options
   window changes them */
extern PdfOpts pdf_options;

/* the whole document (the layout is brought up to date first), with pdf_options; NULL if out of memory */
u8 *pdf_export(Layout *L, size_t *len);
/* the same with other options (NULL: pdf_options) */
u8 *pdf_export_opts(Layout *L, const PdfOpts *o, size_t *len);
/* the same, written to a file; 0 if it can't be */
int pdf_save_file(Layout *L, const char *path);

/* ---- page pictures (core/pngwrite.c, drawn by core/raster.c from the page's PDF content) */
/* page (from 0) drawn as RGB (3 bytes a pixel, rows top down), dpi dots per inch; malloc'd, *w, *h
   its size; NULL if out of memory or no such page */
u8 *pdf_page_rgb(Layout *L, u32 page, int dpi, u32 *w, u32 *h);
/* A page picture whose text a front end draws itself, with its own fonts (Octavo on the classic Mac:
   QuickDraw's bitmap screen fonts, mac/qm_qdtext.c). fn is called for each run of text the page puts
   where text1 would draw it (a word, a word's part in one format, a list label, a field's text, a
   tab leader...) and for each line under or through the spaces between underlined or struck words;
   when it answers 1 the picture leaves the run out (its glyphs and its underline and strike lines),
   when 0 the picture draws it as always. Runs in turned frames (rotated text boxes, vertical text)
   are never offered. Everything else on the page (pictures, shapes, charts, borders, shading,
   highlighting) is drawn as pdf_page_rgb draws it. */
enum { PT_TEXT, PT_LINE };
typedef struct {
  int kind;                 /* PT_TEXT, or PT_LINE: an underline (or strike) line alone */
  const CharFmt *f;         /* its format */
  const char *s; u32 n;     /* PT_TEXT: its text, UTF-8 */
  FaceSize *z;              /* PT_TEXT: the face and size the layout measured it in */
  i32 x, y, w, size;        /* twips, PDF space (y up from the page's foot): start, baseline, the width the
                               layout gave it, the letters' size (super- and subscript applied) */
  u32 color;                /* 0xRRGGBB: its colour (a tracked change's colour) */
  int ul;                   /* the underline's kind (UL_), -1 none; PT_LINE: its kind */
  int strike;               /* struck through: 0, 1 once, 2 twice (PT_LINE: 0) */
  u32 ulcolor;              /* the underline's colour, 0xRRGGBB */
  i32 pageh;                /* the page's height, twips (y from the top is pageh - y) */
} PdfTextRun;
typedef int (*PdfTextFn)(void *ctx, const PdfTextRun *r);
u8 *pdf_page_rgb_text(Layout *L, u32 page, int dpi, u32 *w, u32 *h, PdfTextFn fn, void *ctx);
/* only rows y0..y1 of that picture (*h the band's height): the same pixels as those rows of the whole
   page, drawn in less time and memory (a front end redraws the band an edit changed); fn is offered
   every run of the page as above */
u8 *pdf_page_rgb_band(Layout *L, u32 page, int dpi, i32 y0, i32 y1, u32 *w, u32 *h, PdfTextFn fn, void *ctx);
/* RGB as a PNG file (deflated with core/zip.c), or as an IFF ILBM (24 bit planes, ByteRun1); dpi
   noted in it (PNG's pHYs). 0 if out of memory */
int png_encode(Buf *out, const u8 *rgb, u32 w, u32 h, int dpi);
int ilbm_encode(Buf *out, const u8 *rgb, u32 w, u32 h, int dpi);
/* the pages o names as pictures in o's format and resolution: one page to path itself, several
   to path with "-1", "-2"... (the page numbers) before its ending. Returns how many were written,
   -1 if one could not be (out of memory, or the file) */
int page_images_save(Layout *L, const char *path, const PdfOpts *o);
/* the file name page (from 1) of a multi-page export goes to (path with "-page" before its ending) */
void page_image_name(const char *path, u32 page, char *out, size_t cap);

/* ---- for pdf.c and the tests (core/pdffont.c) */
/* a font file (one face, TrueType or OpenType CFF) cut down to the glyphs whose bits are set in
   used (nglyphs of them; glyph 0 is always kept), glyph ids unchanged; malloc'd, NULL if it can't be */
u8 *font_subset(const u8 *font, size_t len, const u8 *used, u32 nglyphs, size_t *outlen);
/* an OpenType font with CID-keyed CFF outlines: each glyph's CID (malloc'd into *gid2cid, *n
   glyphs); 0 for any other font */
int cff_cid_map(const u8 *font, size_t len, u16 **gid2cid, u32 *n);

#endif
