/* docx_intl.h - WP27's properties in .docx (docx_intl.c), for core/docx.c: right-to-left runs,
   paragraphs, sections and tables, complex script and East Asian run properties, Asian typography,
   the document grid, text direction, East Asian settings. This file belongs to WP27.
   wp27_read_rpr: a start tag inside w:rPr: 1 if it was one of WP27's (read; the caller skips the
   element), else 0 (w:rFonts: its eastAsia, cs and hint attributes noted, the caller reads the rest).
   wp27_read_ppr: the same in w:pPr. wp27_read_sect: one in w:sectPr, read and skipped: 1 if it was. */
#ifndef QUILL_DOCX_INTL_H
#define QUILL_DOCX_INTL_H

#include "doc.h"
#include "xml.h"

int  wp27_read_rpr(Xml *x, Doc *d, unsigned *set, CharFmt *cf);
unsigned wp27_rpr_diff(const CharFmt *f, const CharFmt *base);
/* w:rFonts (when the font or WP27's fonts differ from base, or with full any are set), w:b w:bCs w:i
   w:iCs, w:sz w:szCs: in place of docx.c's own */
void wp27_write_rfonts(Buf *b, Doc *d, const CharFmt *f, const CharFmt *base, unsigned diff, int full);
void wp27_write_bi(Buf *b, const CharFmt *f, unsigned diff);
void wp27_write_sz(Buf *b, const CharFmt *f, unsigned diff);
/* part 0 (after w:vertAlign): w:rtl, w:cs, w:em; part 1 (after w:lang): w:eastAsianLayout */
void wp27_write_rpr(Buf *b, const CharFmt *f, const CharFmt *base, int full, int part);
int  wp27_read_ppr(Xml *x, unsigned *set, ParaFmt *pf);
/* part 0 (after w:tabs): w:kinsoku ... w:bidi, w:snapToGrid; part 1 (after w:jc): w:textAlignment */
void wp27_write_ppr(Buf *b, const ParaFmt *p, const ParaFmt *base, int full, int part);
int  wp27_read_sect(Xml *x, SectFmt *sf);
/* part 0 (after w:titlePg): w:textDirection, w:bidi; part 1 (last): w:docGrid. s may be NULL */
void wp27_write_sect(Buf *o, const SectFmt *s, int part);
/* settings.xml: w:characterSpacingControl, w:strictFirstAndLastChars, w:noLineBreaksAfter/Before */
int  wp27_read_setting(Doc *d, Xml *x);
void wp27_write_settings(Doc *d, Buf *b, int where);

#endif
