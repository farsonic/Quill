/* docx_lang.h - WP31's run properties in .docx (docx_lang.c), for core/docx.c's parse_rpr and w_rpr.
   wp31_read_rpr: a start tag inside w:rPr: 1 if it was w:lang or w:noProof (read; the caller skips
   the element), else 0. wp31_rpr_diff: the CM_LANG / CM_NOPROOF bits in which f differs from base.
   wp31_write_rpr: part 0 w:noProof, part 1 w:lang. This file belongs to WP31. */
#ifndef QUILL_DOCX_LANG_H
#define QUILL_DOCX_LANG_H

#include "doc.h"
#include "xml.h"

int  wp31_read_rpr(Xml *x, unsigned *set, CharFmt *cf);
unsigned wp31_rpr_diff(const CharFmt *f, const CharFmt *base);
void wp31_write_rpr(Buf *b, const CharFmt *f, const CharFmt *base, unsigned diff, int full, int part);

#endif
