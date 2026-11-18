/*
 * toc.h - the table of contents: the document's headings (levels 1 to 3, or what its code asks for)
 * with their page numbers, as paragraphs in the "toc 1" to "toc 9" styles (a right tab stop with dot
 * leaders at the margin). Since WP2 it is a TOC field whose result is those paragraphs (core/refs.h):
 * updating builds them afresh; the page numbers are page-reference placeholders to hidden _Toc
 * bookmarks on the headings, so they are always right, and the entries link to the headings.
 */
#ifndef QUILL_TOC_H
#define QUILL_TOC_H

#include "layout.h"

/* is this paragraph part of a table of contents (a "toc N" style)? */
int  toc_para(Doc *d, const Para *p);
/* insert a table of contents at *at (a paragraph of its own), or bring the existing one up to date
   (L: a layout of d; the page numbers fill themselves in). One undo step. Returns 1, or 0 if out of
   memory. An old table (a run of "toc N" paragraphs without the field) becomes the field. *again is
   always 0 now (the numbers follow the pages by themselves) */
int  doc_toc(Doc *d, Layout *L, Pos *at, int *again);
/* the table of contents taken out, its title too (one undo step); *at (if given) where it was.
   0 if there is none */
int  doc_toc_remove(Doc *d, Pos *at);

#endif
