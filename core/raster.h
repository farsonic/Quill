/*
 * raster.h - a page drawn into pixels from its PDF content stream (WP29: Export Image).
 *
 * pdf.c writes a page's content the same way for a PDF and for a picture, and this draws what it
 * wrote: paths (filled with the non-zero rule, or stroked with their width, caps and dashes), clips,
 * pictures and text, in Quill's own subset of PDF's operators. Shapes are filled with exact coverage
 * across a pixel and four sample rows down it; text is drawn with core/ttraster.c's glyphs. So the
 * picture shows exactly what the PDF does. Integer arithmetic only (fixed point), like ttraster.
 */
#ifndef QUILL_RASTER_H
#define QUILL_RASTER_H

#include "fonts.h"
#include "doc.h"

/* a font of the content (/F0, /F1...): its face, and whether its strings are Windows-1252 bytes
   (simple) or 2-byte glyph ids */
typedef struct { Face *face; int simple; } RFont;

/* the content (len bytes) of a page pw x ph twips drawn at dpi dots per inch onto white, with its
   fonts and pictures (/Im0...); RGB, 3 bytes a pixel, rows top down, malloc'd, *w x *h pixels.
   NULL if out of memory */
u8 *raster_page(const char *content, size_t len, i32 pw, i32 ph, int dpi, const RFont *fonts, int nfonts,
                const Image *const *imgs, int nimgs, u32 *w, u32 *h);
/* the same, only rows y0..y1 of the page (clipped to it): *h is the band's height, its rows exactly
   the page's (a front end draws again only the rows an edit changed); NULL if out of memory or none */
u8 *raster_page_band(const char *content, size_t len, i32 pw, i32 ph, int dpi, const RFont *fonts, int nfonts,
                     const Image *const *imgs, int nimgs, i32 y0, i32 y1, u32 *w, u32 *h);

#endif
