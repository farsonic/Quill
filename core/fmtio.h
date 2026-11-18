/*
 * fmtio.h - what the RTF, HTML and Markdown readers and writers share (WP28a, WP28c): building a
 * story paragraph by paragraph, tables from rows and cells, base64, pictures named by a data: URI
 * or a file next to the document, the styles headings and code take, colours as CSS writes them.
 */
#ifndef QUILL_FMTIO_H
#define QUILL_FMTIO_H

#include "docx.h"

/* paragraphs being collected for a story (the body, a header, a note) */
typedef struct { Para **p; u32 np, cap; int oom; } ParaList;
int  pl_add(ParaList *l, Para *p);          /* takes p (freed if there is no room); 0 if out of memory */
void pl_free(ParaList *l);                  /* the paragraphs freed, l empty */
/* d's body becomes l's paragraphs (an empty one if l has none); l is emptied. 0 if out of memory */
int  pl_to_body(Doc *d, ParaList *l);
/* l's paragraphs become story s (whatever s held is freed first); l is emptied */
void pl_to_story(ParaList *l, Story *s);

/* base64 (RFC 4648) out, and in (whitespace skipped): malloc'd, NULL if out of memory */
void b64_encode(Buf *b, const u8 *data, size_t n);
u8  *b64_decode(const char *s, size_t n, u32 *len);

/* a picture's bytes from where an HTML or Markdown file says it is: a data: URI (base64 or
   percent-encoded), or a file named relative to the document being read (doc_load_path), with
   %xx escapes undone. Malloc'd; NULL if it can't be had (web addresses aren't fetched) */
u8  *fmt_fetch(const char *src, u32 *len);

/* heading styles: the level (1-9) of a paragraph style that is a heading (Heading1..9 by id, or
   "heading n" by name; Title counts as 0 = none), and the style for a level, made like Heading 3
   for levels Quill doesn't have yet; -1 if there's no room */
int  fmt_heading_level(const Doc *d, int style);
int  fmt_heading_style(Doc *d, int level);
/* the paragraph style code blocks take ("Code": Courier New 10 pt, no space between its lines),
   made if the document hasn't one; and whether a style is a code style (Code, Plain Text,
   HTML Preformatted, Source Code...) */
int  fmt_code_style(Doc *d);
int  fmt_is_code_style(const Doc *d, int style);
/* a monospaced font (Courier, Consolas, Monaco...) */
int  fmt_mono_font(const char *family);
/* the Quote style (made if needed), -1 if no room */
int  fmt_quote_style(Doc *d);
/* cf made a link to url in the Hyperlink character style's look (blue, underlined) */
void fmt_link_cf(Doc *d, CharFmt *cf, const char *url);

/* tables from rows and cells (HTML's and Markdown's): begin with the index the table's first
   paragraph will have in the list, then for each row tb_row and for each cell tb_cell (columns
   spanned, rows spanned, whether a heading cell) before adding its paragraphs, then tb_end, which
   orders the paragraphs, fills the cells that rows spanning from above cover, gives the table
   columns of equal width across width (twips) and every paragraph its cell. Nested tables are
   flattened into their cell. 0 from tb_end if out of memory */
#define TB_MAXROWS 4096
typedef struct {
  u32 first;                /* the table's first paragraph in the list */
  int row, col, ncols;      /* where the cell being filled is */
  int incell;
  u8 *busy;                 /* rows x MAX_COLS: covered by a cell spanning rows from above */
  int nrows;
  u8 header0;               /* the first row is a heading row */
  u8 borders;               /* cells get lines all round */
} TableBld;
void tb_begin(TableBld *t, u32 first, int borders);
void tb_row(TableBld *t);
void tb_cell(TableBld *t, int colspan, int rowspan, int header);
/* a paragraph added to the list is in the cell being filled (call for each, before pl_add): its cell
   (borders as tb_begin said, no shading), which the caller may change */
void tb_mark(TableBld *t, ParaFmt *pf);
int  tb_end(TableBld *t, Doc *d, ParaList *l, i32 width);
/* the width between the page's margins */
i32  fmt_text_width(const Doc *d);

/* colours: "#rrggbb", "#rgb", "rgb(r, g, b)" or one of CSS's named colours; COLOR_AUTO if none */
u32  css_color(const char *s, size_t n);
/* a length in CSS units (pt, px, in, cm, mm, pc, em = 12 pt, % of 12 pt) to twips; *ok 0 if none */
i32  css_twips(const char *s, size_t n, int *ok);
/* &name; and &#n; entities: the character, *used the bytes taken; 0 if s isn't one */
u32  html_entity(const char *s, size_t n, int *used);

/* (WP28e) Markdown Source: the Markdown d is written as, as a document of its own (a paragraph a line,
   the Code style, Doc.mdsource set, so saving it as Markdown writes its text as it is); NULL if out of
   memory. md_apply_source reads such a document's text as Markdown (err says why if NULL) */
Doc *md_source_doc(Doc *d);
Doc *md_apply_source(Doc *src, unsigned *lost, char *err, size_t errcap);

#endif
