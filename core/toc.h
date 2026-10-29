/*
 * toc.h - the table of contents: the document's headings (levels 1 to 3) with their page numbers,
 * as paragraphs in the "toc 1" to "toc 3" styles (a right tab stop with dot leaders at the margin).
 * The run of such paragraphs is the table: updating replaces it. Saved as Word's TOC field, so Word
 * can update it too.
 */
#ifndef QUILL_TOC_H
#define QUILL_TOC_H

#include "layout.h"

/* is this paragraph part of a table of contents (a "toc N" style)? */
int  toc_para(Doc *d, const Para *p);
/* insert a table of contents at *at (a paragraph of its own), or bring the existing one up to date
   (L: a layout of d, up to date, for the page numbers; NULL: numbers left as "0"). One undo step.
   Returns 1, or 0 if there are no headings or out of memory. Pages change as it grows, so the
   caller lays out again and calls it once more when the result said so (*again) */
int  doc_toc(Doc *d, Layout *L, Pos *at, int *again);
/* the table of contents taken out, its title too (one undo step); *at (if given) where it was.
   0 if there is none */
int  doc_toc_remove(Doc *d, Pos *at);

#endif
