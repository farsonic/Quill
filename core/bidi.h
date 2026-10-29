/*
 * bidi.h - right-to-left and East Asian text (WP27): the Unicode Bidirectional Algorithm (UAX #9),
 * mirrored glyphs, Arabic shaping by joining types, numeral shapes, scripts, East Asian line
 * breaking (kinsoku) and width.
 *
 * Written from the Unicode Standard Annex #9 (revision for Unicode 15) and the Unicode Character
 * Database (tables in bidi_tab.h, made by tools/mkbidi.py); integer only, no allocation beyond the
 * work arrays the caller gives or bidi_levels mallocs.
 */
#ifndef QUILL_BIDI_H
#define QUILL_BIDI_H

#include "util.h"

/* bidirectional classes, in bidi_tab.h's order */
enum { BC_L, BC_R, BC_AL, BC_EN, BC_ES, BC_ET, BC_AN, BC_CS, BC_NSM, BC_BN, BC_B, BC_S, BC_WS, BC_ON,
       BC_LRE, BC_LRO, BC_RLE, BC_RLO, BC_PDF, BC_LRI, BC_RLI, BC_FSI, BC_PDI, BC_N };
#define BIDI_MAX_DEPTH 125

int  bidi_class(u32 cp);
/* the mirrored glyph (BidiMirroring) of cp, or cp itself */
u32  bidi_mirror(u32 cp);
/* the closing bracket an opening one pairs with (BidiBrackets), 0 if cp opens none */
u32  bidi_bracket_close(u32 cp);
/* the opening bracket a closing one pairs with, 0 if none */
u32  bidi_bracket_open(u32 cp);

/* UAX #9 rules P2-P3, X1-X10, W1-W7, N0-N2, I1-I2 for one paragraph of n code points. cls: each
   one's class (bidi_class, or one overridden by the caller, e.g. a right-to-left run's neutrals made R);
   NULL: worked out from cp. dir: the paragraph's direction, 0 left to right, 1 right to left, -1 from
   its first strong character (P2, P3). levels: n resolved embedding levels (characters X9 removes get
   their neighbour's). Returns the paragraph level (0 or 1); -1 if out of memory (levels then all dir's
   level) */
int  bidi_levels(const u32 *cp, const u8 *cls, int n, int dir, u8 *levels);
/* rule L1 for one line: separators and whitespace at its end, before tabs (segment separators) and
   paragraph separators get the paragraph level. cp[0..n) the line's characters, levels changed in place */
void bidi_line(const u32 *cp, const u8 *cls, int n, int paralevel, u8 *levels);
/* rule L2: the visual order of n items of these levels; order[v] is the logical index shown v-th from
   the left */
void bidi_reorder(const u8 *levels, int n, int *order);
/* the first strong character's direction in UTF-8 text (P2, isolates skipped): 0 left to right,
   1 right to left, -1 none */
int  bidi_first_strong(const char *s, u32 n);
/* a code point of a right-to-left script (Hebrew, Arabic, Syriac, Thaana, N'Ko...) */
int  bidi_is_rtl(u32 cp);
/* invisible formatting characters (marks, embeddings, isolates, joiners): drawn as nothing */
int  bidi_invisible(u32 cp);

/* scripts as Word formats them: SCR_LATIN (ascii and hAnsi fonts), SCR_CS (complex scripts:
   right-to-left, Indic, Thai: the w:cs font, w:szCs, w:bCs, w:iCs), SCR_EA (East Asian: the
   w:eastAsia font) and SCR_NEUTRAL (spaces, digits, punctuation: the run's around them) */
enum { SCR_NEUTRAL, SCR_LATIN, SCR_CS, SCR_EA };
int  bidi_script(u32 cp);
/* East Asian Wide or Fullwidth (EastAsianWidth W, F) */
int  bidi_wide(u32 cp);

/* Arabic shaping: n code points in logical order (a word, with ctxb and ctxa the characters before
   and after it, 0 none) changed in place to their presentation forms by their joining types; lam
   with alef becomes the ligature, the alef BIDI_GONE. has(cp, ctx) (may be NULL) says whether the
   font has a form; a form it lacks stays the letter */
#define BIDI_GONE 0xFFFFFFFFUL
void bidi_shape(u32 *cp, int n, u32 ctxb, u32 ctxa, int (*has)(u32 cp, void *ctx), void *ctx);
/* whether a code point is changed by shaping (an Arabic letter that joins) */
int  bidi_shapes(u32 cp);

/* numerals (File > Options > Advanced: Numeral): Arabic 0-9, Hindi (Arabic-Indic digits), by
   context (Arabic-Indic after Arabic letters), as the system (Arabic) */
enum { NUM_ARABIC, NUM_HINDI, NUM_CONTEXT, NUM_SYSTEM };
extern int bidi_numerals;
/* digit cp (0-9) as shown after prev (the last strong letter before it, 0 none) in a paragraph of
   direction rtl */
u32  bidi_numeral(u32 cp, u32 prev, int rtl);

/* East Asian line breaking (kinsoku): characters that may not start a line, may not end one */
int  bidi_no_start(u32 cp);
int  bidi_no_end(u32 cp);
/* a document's own lists (UTF-8; NULL or "": Word's) in place of those */
void bidi_kinsoku_set(const char *nostart, const char *noend);
/* a line may break between a and b (East Asian rules: between ideographs, kana and wide forms, not
   before a no-start or after a no-end character; kinsoku: rules on). Never between two narrow
   characters (spaces do that) */
int  bidi_can_break(u32 a, u32 b, int kinsoku);

#endif
