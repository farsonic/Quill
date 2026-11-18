/*
 * paratools.h - Home > Paragraph > Paragraph Tools (WordCraft #5043): nine commands that convert and
 * clean up paragraphs, on the selected paragraphs or the whole document.
 *
 *   PT_SPLITLINES      every line break (Shift-Return, '\v') becomes a paragraph break
 *   PT_BLANKBETWEEN    an empty paragraph between each two of them
 *   PT_SEPARATE        the same, and one after the last as well
 *   PT_REMOVEBLANK     empty paragraphs deleted
 *   PT_TRIMLEADING     the spaces at the start of each paragraph deleted
 *   PT_EXTRASPACES     every run of spaces inside the text made one space (leading spaces stay)
 *   PT_SPACESTOINDENT  leading spaces deleted and turned into a first-line indent
 *   PT_INDENTTOSPACES  a paragraph's left and first-line indents turned into leading spaces
 *   PT_JOINLINES       every line break deleted, the lines joined
 *
 * One undo step each. Tracked changes: with rid >= 0 (an index from doc_add_rev) deleted characters
 * are marked deleted by rid and inserted spaces inserted by it, and text already marked deleted is
 * passed over as if it were not there; paragraph marks are not tracked in Quill, so the paragraphs
 * PT_REMOVEBLANK takes away and those the others add are not marked. A paragraph that ends a section,
 * starts a page or column, or is the only one of its table cell is never removed; an empty paragraph
 * is added between two only where they are in the same cell (or both outside tables).
 *
 * A space is taken as a quarter of the paragraph's first character's font size wide (60 twips at 12
 * points), both ways (WordCraft takes 72 points a space).
 */
#ifndef QUILL_PARATOOLS_H
#define QUILL_PARATOOLS_H

#include "doc.h"

enum { PT_SPLITLINES, PT_BLANKBETWEEN, PT_SEPARATE, PT_REMOVEBLANK, PT_TRIMLEADING, PT_EXTRASPACES,
       PT_SPACESTOINDENT, PT_INDENTTOSPACES, PT_JOINLINES, PT_N };
#define PT_MAXPARAS 10000          /* at most this many paragraphs (and breaks, and blank paragraphs) at once */

/* tool on paragraphs pa..pb (inclusive; clamped to the document). rid: the tracked change's author
   (doc_add_rev), -1 not tracking. *caret (if not NULL) is kept in the document. Returns how many
   changes were made (0: nothing to do, too many paragraphs, or out of memory) */
int para_tool(Doc *d, int tool, u32 pa, u32 pb, int rid, Pos *caret);

#endif
