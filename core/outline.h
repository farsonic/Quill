/*
 * outline.h - (WP11) paragraphs' outline levels and the Outline view's editing: a paragraph's level
 * (ParaFmt.outlvl: 1-9, 0 body text; Word's w:outlineLvl), the part of the document a heading heads
 * (it and everything under it up to the next heading of its level or higher), promoting and demoting
 * (by the Heading styles, as Word's Outline view does), moving paragraphs and parts up and down, and
 * which paragraphs a collapsed outline hides.
 */
#ifndef QUILL_OUTLINE_H
#define QUILL_OUTLINE_H

#include "doc.h"

/* 1-9 for a "heading N" style name (Word's built-in names, either capitalisation), else 0 */
int  heading_style_level(const char *name);
/* paragraph pi's outline level: 1-9, 0 body text (and out of range) */
int  outline_level(const Doc *d, u32 pi);
/* one past the last paragraph of pi's part: for a heading, everything after it up to the next
   heading of its level or higher (or the end); body text is a part of its own (pi + 1) */
u32  outline_end(const Doc *d, u32 pi);
/* pi is a heading with something under it (what Outline view shows with a plus) */
int  outline_has_kids(const Doc *d, u32 pi);
/* the "heading N" style (N 1-9), made like the one above it if the document has none; its index or -1 */
int  outline_heading_style(Doc *d, int level);
/* paragraphs pa..pb given outline level level (0: body text): a Heading N style (Normal for body
   text), or, for a paragraph whose own outline level differs from its style's, that level directly.
   One undo step; 0 if nothing changed */
int  outline_set_level(Doc *d, u32 pa, u32 pb, int level);
/* paragraphs pa..pb promoted (dir < 0) or demoted (dir > 0) a level: headings between 1 and 9; body
   text promoted becomes a heading at the level of the heading above it (1 if none), and isn't
   demoted. One undo step; 0 if nothing changed */
int  outline_promote(Doc *d, u32 pa, u32 pb, int dir);
/* paragraphs a..b moved to just before paragraph dest (dest <= a or dest > b + 1; d->np: to the end).
   Not out of or into tables (nor any table moved): 0 then. The document's last paragraph keeps the
   last section's format. One undo step; *newa (if not NULL) gets where paragraph a went */
int  outline_move_to(Doc *d, u32 a, u32 b, u32 dest, u32 *newa);
/* paragraphs a..b moved up (dir < 0) or down past the next paragraph shown: hidden[i] (NULL: none
   hidden) marks paragraphs a collapsed outline hides, which move with the shown one they belong to.
   0 at the document's start or end */
int  outline_move(Doc *d, u32 a, u32 b, int dir, const u8 *hidden, u32 *newa);
/* which paragraphs Outline view hides (out[i] 1): those under a collapsed heading (coll: the
   collapsed headings' paragraphs, ncoll of them), and with levels 1-9 shown only, body text and
   headings deeper than that */
void outline_hidden(const Doc *d, Para *const *coll, u32 ncoll, int levels, u8 *out);

#endif
