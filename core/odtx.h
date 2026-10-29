/*
 * odtx.h - what core/odt.c (reading .odt and Flat ODT) and core/odtw.c (writing them) share. WP28b's;
 * only that package edits it.
 */
#ifndef QUILL_ODTX_H
#define QUILL_ODTX_H

#include "docx.h"

/* the document as an .odt package (flat 0) or a Flat ODT XML file (flat 1); malloc'd, NULL if out of memory */
u8  *odt_write(Doc *d, int flat, size_t *len);

/* lengths: an ODF length (in, cm, mm, pt, pc, px) in twips, rounded; *ok 0 if s isn't one */
i32  odt_len(const char *s, int *ok);
/* twips written as inches with 4 decimals ("1.2500in") */
void odt_put_len(Buf *b, i32 tw);
/* an ODF border ("0.5pt solid #000000", "none"): Quill's PB_ kind (PB_NONE none), width in eighths of a
   point and colour; 0 if it isn't one */
int  odt_border(const char *s, u8 *style, u8 *width, u32 *color);
/* and written (style PB_NONE: "none") */
void odt_put_border(Buf *b, u8 style, u8 width, u32 color);
/* style:name values: _xx_ escapes undone, and made ("Heading 1" <-> "Heading_20_1") */
void odt_unescape(const char *s, char *out, size_t cap);
void odt_escape(const char *s, char *out, size_t cap);

#endif
