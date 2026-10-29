/*
 * mathedit.h - WP14b: editing an equation in place (the caret inside its tree), its gallery of
 * structures and symbols, LaTeX in and out, Professional/Linear conversion and equation numbers.
 *
 * A place in an equation (MPos) is a path from one of its lines down through structures' arguments
 * to a row, and an offset in that row counted in units: each character of a run is one unit, each
 * structure one. The editor works on a copy of the equation (math_clone) and puts it back with
 * doc_set_math, so every change is one undo step; a place stays valid on the copy because it holds
 * indexes, not pointers. The navigation rules (which argument comes next, what Backspace does at
 * the start of one) follow WordCraft's math_edit.rs (MIT OR Apache-2.0, (c) its authors), written
 * again in C; the gallery's templates and symbol sets are WordCraft's math_gallery.rs lists.
 */
#ifndef QUILL_MATHEDIT_H
#define QUILL_MATHEDIT_H

#include "math.h"

#define MP_DEPTH 24
typedef struct {
  u16 line;                 /* which of the equation's lines (MathEq.line) */
  u16 depth;                /* how many steps down */
  u16 node[MP_DEPTH];       /* each step: the structure's place in its row, in units (so joining or
                               dropping runs before it leaves the place right) ... */
  u16 arg[MP_DEPTH];        /* ... and which of its arguments (kid[]) */
  u16 off;                  /* units into the row reached */
} MPos;

/* ---- places */
MNode *mp_row(const MathEq *e, const MPos *p);     /* the row a place is in (NULL: not valid) */
int    mp_units(const MNode *row);                 /* its length in units */
void   mp_clamp(const MathEq *e, MPos *p);         /* made valid: the longest valid path, the offset cut */
void   mp_start(const MathEq *e, MPos *p);         /* the equation's very start, end */
void   mp_end(const MathEq *e, MPos *p);
int    mp_same_row(const MPos *a, const MPos *b);
int    mp_equal(const MPos *a, const MPos *b);
/* the arguments of a structure in reading order, hidden ones left out (n-ary limits hidden, a hidden
   radical degree); returns how many, their kid[] numbers in out */
int    mn_nav(const MNode *n, int *out, int max);
int    mn_main_arg(const MNode *n);                /* the argument a selection goes into when it is put in one */

/* ---- moving: 0 when the move would leave the equation (the caret goes on in the text) */
int  me_left(const MathEq *e, MPos *p);
int  me_right(const MathEq *e, MPos *p);
int  me_vertical(const MathEq *e, MPos *p, int up);    /* into the argument above or below (denominator, superscript...) */
void me_home(const MathEq *e, MPos *p, int end);       /* the argument's start or end; from there the equation's */
int  me_next_slot(const MathEq *e, MPos *p, int back); /* the next (previous) empty argument, round; 0 if none */
void me_word(const MathEq *e, MPos *p, int right);     /* a word (a run of letters or digits) or structure left or right */

/* ---- changing (on an editor's copy); each returns 1 if the equation changed */
int  me_insert_text(MathEq *e, MPos *p, const char *utf8, int normal);   /* normal: as normal text (m:nor) */
/* the structures and runs of row src (taken) put in at p; a selection (a row of what was cut, taken,
   may be NULL) goes into the first structure's main argument. The caret goes to the first empty
   argument of what was put in, or after it */
int  me_insert_row(MathEq *e, MPos *p, MNode *src, MNode *cut);
/* Backspace and Delete. *sel is set to 1 when the step selected a structure (Word selects a
   structure before deleting it): the selection runs from *anchor to *p */
int  me_backspace(MathEq *e, MPos *p, MPos *anchor, int *sel);
int  me_delete(MathEq *e, MPos *p, MPos *anchor, int *sel);
/* a selection between two places: the row both are in (the nearest common one) and the units
   [*u0, *u1) of it, the structures holding either end taken whole. 0 if the places are in
   different lines */
int  me_sel_range(const MathEq *e, const MPos *a, const MPos *b, MPos *row, int *u0, int *u1);
MNode *me_cut(MathEq *e, const MPos *a, const MPos *b, MPos *p);   /* the selection taken out (a row), caret at its place */
MNode *me_copy(const MathEq *e, const MPos *a, const MPos *b);    /* a copy of it (a row) */
void me_tidy(MathEq *e);
int  me_split_line(MathEq *e, MPos *p);    /* a display equation's line broken at the caret (Shift+Return) */                                           /* empty runs gone, plain neighbours joined */
/* autobuild-up (Word's): the plain linear text before the caret in its row built into structures
   (latex: read as LaTeX). 1 if it changed; the caret ends after what was built */
int  me_build_up(MathEq *e, MPos *p, int latex);
/* Math AutoCorrect: \name just before the caret becomes its character */
int  me_autocorrect(MathEq *e, MPos *p);
/* normal text (m:nor) on or off for the selection's runs; 1 if changed */
int  me_set_normal(MathEq *e, const MPos *a, const MPos *b, int on);
int  me_is_normal(const MathEq *e, const MPos *p);   /* the run before the caret is normal text */

/* ---- structure commands (Word's right-click actions and Equation Options): the ones that apply
   to the structures round the caret, innermost first, each with the depth of the structure */
enum { MA_REMOVE, MA_FRAC_BAR, MA_FRAC_SKEWED, MA_FRAC_LINEAR, MA_FRAC_NOBAR, MA_RAD_DEGREE,
       MA_NARY_SIDE, MA_NARY_UNDER, MA_NARY_LOWER, MA_NARY_UPPER, MA_NARY_GROW,
       MA_DELIM_ADD, MA_DELIM_DELETE, MA_DELIM_GROW,
       MA_SCRIPT_NOSUB, MA_SCRIPT_NOSUP, MA_SCRIPT_ADDSUB, MA_SCRIPT_ADDSUP,
       MA_ROW_ABOVE, MA_ROW_BELOW, MA_COL_BEFORE, MA_COL_AFTER, MA_ROW_DELETE, MA_COL_DELETE,
       MA_EQ_BEFORE, MA_EQ_AFTER, MA_EQ_DELETE, MA_GROUP_FLIP, MA_BAR_FLIP,
       MA_DELIM_PAREN, MA_DELIM_SQUARE, MA_DELIM_BRACE, MA_DELIM_ANGLE, MA_DELIM_BARS, MA_DELIM_NORM,
       MA_DELIM_FLOOR, MA_DELIM_CEIL, MA_DELIM_NONE, MA_N };
typedef struct { u8 act; u8 depth; } MAct;
int  me_actions(const MathEq *e, const MPos *p, MAct *out, int max);
const char *me_action_label(int act, const MathEq *e, const MPos *p, int depth);   /* English, for tr() */
/* act on the innermost structure it applies to (depth -1) or the one at that depth; 1 if done */
int  me_apply(MathEq *e, MPos *p, int act, int depth);

/* ---- conversion */
/* the equation as linear text (latex: LaTeX) in one plain run: Word's Linear form */
int  me_to_linear_form(MathEq *e, int latex);
/* built up from its linear text (or LaTeX): Professional form */
int  me_to_professional(MathEq *e, int latex);
int  me_is_linear_form(const MathEq *e);      /* one row of plain runs that would build up */

/* ---- LaTeX */
MathEq *math_from_latex(const char *s, int display);     /* NULL if out of memory */
void    math_to_latex(const MathEq *e, Buf *o);
/* a LaTeX expression as UnicodeMath linear text (what math_from_latex parses), appended to o */
void    latex_to_linear(const char *s, Buf *o);

/* ---- equation numbers: a display equation whose single line is an equation array of one row
   with a # in it (E=mc^2#(1), as Word writes a numbered equation); the number after the # sits at
   the line's right edge */
int  math_numbered(const MathEq *e);          /* 1 numbered, 2 numbered automatically: #(digits) */
int  me_number(MathEq *e, int on, int n);     /* the number added (as #(n)) or taken away */
/* the automatic numbers brought up to date in document order, in place (like updating SEQ fields;
   no undo step): the count changed */
int  math_renumber(Doc *d);
/* the width a display equation's line gives it (layout calls this before placing it): a numbered
   one spans it, its number at the right. Returns the laid-out width */
i32  math_fit(FontSet *fs, Doc *d, int k, const CharFmt *f, i32 room);

/* ---- geometry (from the last math_layout of the equation; twips from its origin, y down) */
/* the caret: its x, its top and its height */
void me_caret_box(const MathEq *e, const MPos *p, i32 *x, i32 *top, i32 *h);
/* the place nearest a point */
void me_hit(const MathEq *e, i32 x, i32 y, MPos *p);
/* every empty argument shown (a placeholder box drawn there): f(ctx, x, top, w, h) */
void me_slots(const MathEq *e, void (*f)(void *ctx, i32 x, i32 top, i32 w, i32 h), void *ctx);
/* the selection's box (x0..x1, top, height); 0 if empty */
int  me_sel_box(const MathEq *e, const MPos *a, const MPos *b, i32 *x0, i32 *x1, i32 *top, i32 *h);

/* ---- the gallery */
typedef struct { const char *linear; signed char lim; } MTemplate;   /* linear format (UTF-8); lim: -1 the default, else MLIM_ */
typedef struct { const char *name; const MTemplate *t; int n; } MSection;
typedef struct { const char *name; const MSection *s; int n; } MGallery;
enum { MG_FRACTION, MG_SCRIPT, MG_RADICAL, MG_INTEGRAL, MG_LARGEOP, MG_BRACKET, MG_FUNCTION, MG_ACCENT,
       MG_LIMLOG, MG_OPERATOR, MG_MATRIX, MG_N };
extern const MGallery math_galleries[MG_N];
#define MSYM_N 8
extern const char *const math_symbol_set_names[MSYM_N];
extern const char *const math_symbol_sets[MSYM_N];    /* UTF-8 */
/* a template as a row to insert (U+2B1A an empty argument), its limits placed as it says */
MNode  *math_template(const MTemplate *t);
MathEq *math_template_eq(const MTemplate *t);          /* as an equation of its own (for previews) */

#endif
