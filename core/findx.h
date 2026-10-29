/*
 * findx.h - WP12 (docs/wordcraft-gap.md): Word's Advanced Find and Replace, Go To, Select Similar
 * Formatting, column (block) selection and Toggle Unicode Character, on the document model.
 *
 * Find what is read as Word reads it. Without wildcards: literal text (case folded unless FX_CASE)
 * with Word's special characters: ^p paragraph mark, ^t tab, ^l manual line break, ^m manual page
 * break, ^n column break, ^b section break, ^? any character, ^# any digit, ^$ any letter, ^w white
 * space, ^^ a caret, ^+ em dash, ^= en dash, ^s nonbreaking space, ^~ nonbreaking hyphen, ^- optional
 * hyphen, ^% section sign, ^v pilcrow, ^g a graphic (picture, shape, text box), ^f a footnote mark,
 * ^e an endnote mark, ^a a comment mark, ^d a field, and character codes ^13, ^0151 (Windows-1252),
 * ^u8212 (Unicode). With FX_WILD, Word's wildcards (always matching case): ? any character, * any
 * run (as short as can be), [abc] [a-z] [!a-z] one of (or none of) those, {n} {n,} {n,m} and @ the
 * one before repeated, < and > a word's start and end, ( ) groups for \1-\9 in Replace with, \x the
 * character x; the ^ codes work too. Replace with knows the same codes plus ^& (what was found) and
 * ^c (the clipboard's text), and \1-\9 with wildcards.
 *
 * The document is searched as one stream: each paragraph's text, then its paragraph mark (none after
 * the last). Page and column breaks and section ends are marks between paragraphs in Quill (a
 * paragraph starting a page, or ending a section), so ^m, ^n and ^b are kinds of paragraph mark.
 * Positions of a match: from the start of what it covers to its end; a match ending past a mark ends
 * at the next paragraph's start.
 */
#ifndef QUILL_FINDX_H
#define QUILL_FINDX_H

#include "layout.h"

enum { FX_CASE = 1, FX_WORD = 2, FX_BACK = 4, FX_WILD = 8, FX_PREFIX = 16, FX_SUFFIX = 32, FX_NOPUNCT = 64, FX_NOSPACE = 128,
       FX_NOWRAP = 256 };

/* formatting to find (each field "any" unless set) or to give the replacement (each left alone
   unless set). set/clear: CF_ flags the text must have or lack (replace: turned on or off); fxset,
   fxclear: FX_SMALLCAPS, FX_DSTRIKE the same way; font: a family name ("" any); size: half-points
   (0 any); color: 0xRRGGBB or COLOR_AUTO (FXC_ANY any); hl: highlighted 1, not 0, any -1 (replace: 1
   is yellow); pstyle, cstyle: a paragraph or character style's shown name ("" any); align: AL_ (-1 any) */
#define FXC_ANY 0xFE000000UL
typedef struct {
  unsigned set, clear;
  u8 fxset, fxclear;
  char font[48];
  u16 size;
  u32 color;
  signed char hl;
  char pstyle[48], cstyle[48];
  signed char align;
} FxFormat;
void fx_format_none(FxFormat *f);           /* every field "any" */
int  fx_format_empty(const FxFormat *f);    /* nothing set */

/* a Find what compiled (about 5 KB: keep it static or malloc'd, not on a 68k stack) */
#define FX_MAXNODE 160
#define FX_MAXSET 160
typedef struct { u8 type, neg, grp, lazy; u16 min, max; u32 c; u16 set0, nset; } FxNode;
typedef struct {
  FxNode n[FX_MAXNODE]; int nn;
  u32 set[FX_MAXSET][2]; int nsets;      /* the classes' ranges (low, high) */
  unsigned flags;
  int ngroups;
  FxFormat fmt; int hasfmt;
  int empty;                             /* no text: the formatting alone is looked for */
  char err[96];                          /* why it didn't compile (English, for tr()) */
} FxPattern;
/* what (UTF-8) read with flags (FX_) and formatting fmt (NULL none) into p; 0 if it isn't a valid
   pattern (p->err says why), or there is nothing to look for */
int  fx_compile(FxPattern *p, const char *what, unsigned flags, const FxFormat *fmt);
/* does p match at a (formatting too)? 1 and where it ends; groups (if not NULL, 10 pairs: 0 the whole
   match, 1-9 the groups, unset ones a == b == the match's start) */
int  fx_match_at(Doc *d, const FxPattern *p, Pos a, Pos *end, Pos *groups);
/* the next match from from (FX_BACK: the one before it), within range[0]..range[1] (NULL: the whole
   document), wrapping round once unless FX_NOWRAP. 1 and the match in *a..*b, else 0 */
int  fx_find(Doc *d, const FxPattern *p, Pos from, const Pos *range, Pos *a, Pos *b);
/* every match in range (NULL: the document) as pairs in out (at most max pairs); how many */
u32  fx_find_all(Doc *d, const FxPattern *p, const Pos *range, Pos *out, u32 max);
/* a..b (a match of p) replaced by with (Word's codes, clip for ^c, NULL none), the new text formatted
   as the match's first character, then rf (NULL: none) given to it; with "^&" or empty with rf set:
   the match keeps its text and only takes rf. One undo step; *end: the replacement's end. 0 if not */
int  fx_replace(Doc *d, const FxPattern *p, Pos a, Pos b, const char *with, const char *clip, const FxFormat *rf, Pos *end);
/* every match in range (NULL: the document) replaced: one undo step (UK_REPLACEALL); how many */
u32  fx_replace_all(Doc *d, const FxPattern *p, const Pos *range, const char *with, const char *clip, const FxFormat *rf);
/* a..b given the formatting rf (part of one undo step if inside doc_begin) */
void fx_apply_format(Doc *d, Pos a, Pos b, const FxFormat *rf);

/* Go To's targets, in Word's order */
enum { GT_PAGE, GT_SECTION, GT_LINE, GT_BOOKMARK, GT_COMMENT, GT_FOOTNOTE, GT_ENDNOTE, GT_FIELD, GT_TABLE, GT_GRAPHIC,
       GT_OBJECT, GT_HEADING, GT_N };
/* where each target of a kind starts (and in b, if not NULL, ends), in document order; at most max;
   returns how many there are (all of them, even past max). Pages and lines need L (laid out), the
   others ignore it */
u32  fx_targets(Doc *d, Layout *L, int kind, Pos *a, Pos *b, u32 max);
/* Go To as Word reads what was typed: "" the next one after cur; "n" the n'th; "+n" / "-n" that
   many after or before the one cur is at; a bookmark's name; for comments, a reviewer's name (the
   next comment of theirs). dir 1 / -1: the Next / Previous button (spec as for ""). 1 and the target
   in *a..*b (a == b for a place), 0 if there is none */
int  fx_goto(Doc *d, Layout *L, int kind, const char *spec, int dir, Pos cur, Pos *a, Pos *b);

/* Select Similar Formatting: the stretches of text formatted like the character at at (how it looks
   and its character style), as pairs in out (at most max pairs); how many */
u32  fx_similar(Doc *d, Pos at, Pos *out, u32 max);
/* column (block) selection: the part of each line between anchor's and caret's lines that lies
   between their x positions, as pairs in out (at most max pairs); how many */
u32  fx_block(Layout *L, Pos anchor, Pos caret, Pos *out, u32 max);
/* the block's text: each line's part on a line of its own ("\n" between), malloc'd */
char *fx_block_text(Doc *d, const Pos *r, u32 n, u32 *len);
/* Toggle Unicode Character (Word's Alt+X): the hex code before the caret (up to six digits, "U+"
   allowed) becomes its character, or the character before it becomes its code; with a selection, the
   selection is read the same way. One undo step; *caret: after the result. 0 if nothing to do */
int  fx_toggle_unicode(Doc *d, Pos a, Pos b, Pos *caret);

#endif
