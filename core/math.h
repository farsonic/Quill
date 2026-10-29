/*
 * math.h - equations (WP14a): Office Math (OMML, ECMA-376 Part 1 §22.1) as a tree of nodes, read
 * from and written to .docx, built from Unicode linear format (UnicodeMath, a subset) and written
 * back to it, and laid out TeX-style with Quill's own fonts into a list of glyphs, rules and filled
 * outlines that the screen and the PDF draw.
 *
 * An equation is a document object like a picture: the text holds U+10C000 + n for Doc.maths[n]
 * (4 bytes of UTF-8, F4 8C-8F xx xx), so it moves, copies and undoes as one character. Each entry
 * is only ever added to Doc.maths; a changed equation is a new entry put in the old one's place
 * (doc_set_math), as changed objects are, so undo keeps working. An inline equation (m:oMath) sits
 * in the line; a display one (m:oMathPara) takes a line (or several) of its own.
 *
 * The tree: a row (MK_ROW, OMML's m:oMath, m:e, m:num, m:sub...) holds elements; a run (MK_RUN,
 * m:r) holds text; a structure (fraction, radical, n-ary...) holds its arguments, each a row, in
 * OMML's order (kid[]). Properties are kept as OMML has them (absent, on or off), so what is read is
 * written back the same; what Quill doesn't model is kept verbatim (MK_RAW elements, the control
 * properties m:ctrlPr, a run's w:rPr). Rows know where layout put them (x, y, w, asc, desc), for
 * caret placement by an editor (WP14b), which edits a copy (math_clone) with the mn_ functions and
 * puts it back with doc_set_math.
 */
#ifndef QUILL_MATH_H
#define QUILL_MATH_H

#include "doc.h"
#include "xml.h"
#include "fonts.h"

/* ---- the reference in the text */
#define MATH_CP0 0x10C000UL
#define MAX_MATHS 16384
#define IS_MATH(t) ((u8)(t)[0] == 0xF4 && ((u8)(t)[1] & 0xFC) == 0x8C)
int  math_index(const char *t);            /* the equation a reference names (t at its first byte) */
void math_utf8(char *out, int index);      /* a reference: 4 bytes and a NUL */

/* ---- nodes */
enum { MK_ROW, MK_RUN, MK_ACC, MK_BAR, MK_BOX, MK_BORDERBOX, MK_DELIM, MK_EQARR, MK_FRAC, MK_FUNC, MK_GROUPCHR,
       MK_LIMLOW, MK_LIMUPP, MK_MATRIX, MK_NARY, MK_PHANT, MK_RAD, MK_SPRE, MK_SSUB, MK_SSUBSUP, MK_SSUP, MK_RAW, MK_N };
/* tri-state OMML switches (m:val on/off), in MNode.pv[] */
enum { MB_UNSET, MB_ON, MB_OFF };
#define MCH_UNSET 0xFFFFFFFFUL     /* a character property not given (0: given as "", none) */
#define MI_UNSET  (-32768)         /* a number property not given */
/* the properties' slots in pv[] (switches and choices: 0 unset, else MB_ or choice + 1), ch[] (characters)
   and iv[] (numbers), by structure */
enum { P_BAR_POS = 0 };                                         /* MPOS_ + 1 */
enum { P_BOX_OPEMU, P_BOX_NOBREAK, P_BOX_DIFF, P_BOX_BRK, P_BOX_ALN };   /* P_BOX_BRK: MB_ON when m:brk is there, iv[0] its alnAt */
enum { P_BB_HIDETOP, P_BB_HIDEBOT, P_BB_HIDELEFT, P_BB_HIDERIGHT, P_BB_STRIKEH, P_BB_STRIKEV, P_BB_STRIKEBLTR, P_BB_STRIKETLBR };
enum { P_D_GROW, P_D_SHP };                                     /* ch[0] begChr, ch[1] sepChr, ch[2] endChr; shp: MSHP_ + 1 */
enum { P_EQ_BASEJC, P_EQ_MAXDIST, P_EQ_OBJDIST };               /* iv[0] rSpRule, iv[1] rSp */
enum { P_F_TYPE };                                              /* MF_ + 1 */
enum { P_G_POS, P_G_VERTJC };                                   /* ch[0] chr */
enum { P_M_BASEJC, P_M_PLCHIDE };                               /* iv[0] rSpRule, [1] cGpRule, [2] rSp, [3] cSp, [4] cGp */
enum { P_N_LIMLOC, P_N_GROW, P_N_SUBHIDE, P_N_SUPHIDE };        /* ch[0] chr; limLoc: MLIM_ + 1 */
enum { P_PH_SHOW, P_PH_ZEROWID, P_PH_ZEROASC, P_PH_ZERODESC, P_PH_TRANSP };
enum { P_RAD_DEGHIDE };
enum { P_SS_ALNSCR };
/* a run's m:rPr */
enum { P_R_LIT, P_R_NOR, P_R_SCR, P_R_STY, P_R_BRK, P_R_ALN };  /* scr: MSCR_ + 1, sty: MSTY_ + 1; P_R_BRK: MB_ON with iv[0] alnAt */
enum { MPOS_TOP, MPOS_BOT };
enum { MSHP_CENTERED, MSHP_MATCH };
enum { MF_BAR, MF_SKW, MF_LIN, MF_NOBAR };
enum { MLIM_UNDOVR, MLIM_SUBSUP };
enum { MJC_TOP, MJC_BOT, MJC_CENTER, MJC_INLINE, MJC_INSIDE, MJC_OUTSIDE };   /* baseJc (ST_YAlign) */
enum { MSCR_ROMAN, MSCR_SCRIPT, MSCR_FRAKTUR, MSCR_DOUBLE, MSCR_SANS, MSCR_MONO };
enum { MSTY_P, MSTY_B, MSTY_I, MSTY_BI };
enum { MPJC_LEFT, MPJC_RIGHT, MPJC_CENTER, MPJC_CENTERGROUP };  /* a display equation's place (oMathParaPr jc) */
enum { MCJ_CENTER, MCJ_LEFT, MCJ_RIGHT, MCJ_INSIDE, MCJ_OUTSIDE };   /* a matrix column's (m:mcJc) */

typedef struct MNode {
  u8  kind;                 /* MK_ */
  u8  pv[8];                /* switches and choices (P_ slots above) */
  u32 ch[3];                /* characters (MCH_UNSET: not given) */
  i16 iv[5];                /* numbers (MI_UNSET: not given) */
  signed char  argsz;                /* a row's m:argPr/m:argSz (-2..2), with hasargsz */
  u8  hasargsz;
  u16 rows, cols;           /* MK_MATRIX: kid[] is rows x cols cells, row by row */
  u8  (*mcs)[2]; u16 nmcs;  /* MK_MATRIX: m:mcs, each column group's count and MCJ_ */
  char *text;               /* MK_RUN: its text (UTF-8); MK_RAW: the element verbatim */
  char *rpr;                /* MK_RUN: its w:rPr verbatim (NULL none) */
  char *ctrl;               /* the m:ctrlPr element of its properties (or a row's) verbatim, NULL none */
  char *xpr;                /* properties Quill doesn't know, verbatim (written before m:ctrlPr) */
  /* from a run's w:rPr: what layout uses */
  u16 size;                 /* half-points, 0 not given */
  u32 color;                /* COLOR_AUTO not given */
  u8  wb, wi;               /* w:b, w:i (normal text) */
  char font[32];            /* w:rFonts' ascii font (normal text), "" none */
  struct MNode **kid; u16 nkid, kcap;
  /* where the last layout put it (twips, x from the equation's left, y its baseline below the
     equation's): rows and runs, for an editor's caret */
  i32 x, y, w, asc, desc;
  /* WP14b: a run's characters' x from its own x (ncx = characters + 1), for the editor's caret */
  i32 *cx; u16 ncx;
} MNode;

typedef struct MLay MLay;
typedef struct MathEq {
  u8 display;               /* an m:oMathPara (a line of its own) */
  u8 jc;                    /* its m:oMathParaPr m:jc: MPJC_ + 1, 0 not given */
  MNode **line; u16 nline;  /* its equations (m:oMath, rows): an inline one has one */
  char *ppr;                /* m:oMathParaPr's other content verbatim (NULL none) */
  u32 ver;                  /* bumped by an editor that changes it in place */
  /* the last layout (math_layout's cache) */
  MLay *lay; u16 laysize; u32 laycolor, layver; void *layfs; u8 laydisp; u8 layfont;
  /* WP14b: the width of the line a numbered display equation spans (math_fit), and the layout's */
  i32 room, layroom;
} MathEq;

MNode *mn_new(int kind);                    /* empty, properties unset; NULL if out of memory */
void   mn_free(MNode *n);                   /* and everything in it; NULL fine */
MNode *mn_clone(const MNode *n);            /* a deep copy, NULL if out of memory */
/* a structure with its arguments (empty rows; a matrix rows x cols, an equation array or delimiter n) */
MNode *mn_struct(int kind, int n, int cols);
MNode *mn_run(const char *text, size_t len);   /* a run of math text */
int    mn_insert(MNode *parent, int at, MNode *kid);   /* kid taken (freed on failure); at -1: at the end. 0 if out of memory */
MNode *mn_remove(MNode *parent, int at);    /* taken out (the caller's now) */
int    mn_nargs(int kind);                  /* a structure's arguments (-1: any number, MK_DELIM/EQARR/MATRIX) */
const char *mn_name(int kind);              /* its OMML element name ("f", "sSup"...) */

MathEq *math_new(int display);              /* with one empty line; NULL if out of memory */
void    math_free(MathEq *e);
MathEq *math_clone(const MathEq *e);
int     math_empty(const MathEq *e);        /* nothing in it */
u16     math_size(const MathEq *e);         /* the size (half-points) of its first run that gives one, 0 none */

/* ---- in the document (Doc.maths, only ever added to) */
int     doc_add_math(Doc *d, MathEq *e);    /* takes e (freed on failure); its index or -1 */
MathEq *doc_math(const Doc *d, int k);      /* NULL if not one */
void    math_doc_free(Doc *d);              /* every entry and the settings (doc_free) */
/* e put in at *at (moved after it) as one character in f (NULL: the formatting there): one undo
   step; its index or -1 */
int     doc_insert_math(Doc *d, Pos *at, MathEq *e, const CharFmt *f);
/* the equation whose reference is at at changed to e (a new entry in its place): one undo step;
   its index or -1 */
int     doc_set_math(Doc *d, Pos at, MathEq *e);
/* the document's math settings (m:mathPr, settings.xml) layout uses */
typedef struct { u8 intsub, narysub, smallfrac, defjc; i32 wrapind; } MathPr;
void    math_settings(const Doc *d, MathPr *mp);
/* how far right a display equation's line starts in extra twips of room: its justification (or the
   document's default), centred, left or right */
i32     math_line_shift(const Doc *d, int k, i32 extra);

/* ---- OMML */
/* x just after the START of an m:oMath or m:oMathPara (local name "oMath"/"oMathPara"): the
   equation, read to the element's END; NULL if out of memory */
MathEq *math_read_omml(Xml *x);
/* the equation as an m:oMath (inline) or m:oMathPara (display) element, its m namespace declared on
   it (so it can go in any part); rpr (rl bytes): the w:rPr of the text it sits in, for runs that have
   none of their own (NULL: none) */
void    math_write_omml(Buf *o, const MathEq *e, const char *rpr, size_t rl);
/* settings.xml's m:mathPr kept verbatim (x at its START, read to its END), and written back */
void    math_read_mathpr(Doc *d, Xml *x);
void    math_write_mathpr(const Doc *d, Buf *o);

/* ---- Unicode linear format (UnicodeMath, Word's linear equations) */
/* an equation from linear text (UTF-8; \alpha-style control words become their characters): a/b,
   x^2, x_i, x_i^2, √(x), √(3&x), ∑_(i=1)^n▒i, ∫_0^1, (…) […] {…} |…| ⟨…⟩, sin x, lim┬(x→0), a ̂ (combining
   accents), ¯(x) ▁(x), ⏞(x) ⏟(x), ■(a&b@c&d), █(x&=1@y&=2), □(x), ▭(x), ⟡(x), "normal text",
   a⊘b (linear), a∕b (skewed), a¦b (no bar). NULL if out of memory */
MathEq *math_from_linear(const char *s, int display);
/* the equation as linear text (UTF-8), appended to o */
void    math_to_linear(const MathEq *e, Buf *o);
/* the code point a \name control word stands for ("alpha" -> α), 0 if none */
u32     math_keyword(const char *name, size_t n);
/* linear text with every character Latin-1 lacks written as its control word ("√(x)" -> "\sqrt (x)"),
   for the Amiga's Latin-1 gadgets; math_from_linear reads it back the same. Appended to o */
void    math_keywords(const char *s, Buf *o);

/* ---- layout */
#define BX_MATH 0x40        /* layout.h box kind for an equation's reference (outside layout.h's own enum) */
enum { MI_GLYPH, MI_RECT, MI_POLY };
enum { MIF_FLIPX = 1, MIF_FLIPY = 2 };
/* one thing to draw, from the equation's origin (its left edge on its baseline), twips, y down:
   MI_GLYPH: glyph of face (MLay.faces) at size twips to the em, pen at x, y (code point cp, for PDF
   text); MI_RECT: a filled rectangle, top left x, y, w x h; MI_POLY: a filled outline (non-zero
   winding) of h points from MLay.pt[w] (x, y pairs) */
typedef struct { u8 kind, flags, face, pad; i32 x, y, w, h; i32 size; u32 cp; u16 glyph; u32 color; } MItem;
#define ML_FACES 12
struct MLay {
  MItem *it; u32 n, cap;
  i32 *pt; u32 npt, ptcap;
  Face *faces[ML_FACES]; int nfaces;
  i32 w, asc, desc;         /* its extent from its origin */
  int oom;
};
/* equation k of d laid out for text in f (its size; its colour for runs with none), with display set
   as the entry says (inline equations in text style, display ones in display style); kept with the
   entry until something differs. A layout with nothing in it if k isn't an equation; NULL only
   without memory */
const MLay *math_layout(FontSet *fs, Doc *d, int k, const CharFmt *f);
void  math_lay_free(MLay *l);
void  math_fonts_free(void);                /* the outline readers math layout opened (at the end) */
struct TTRaster;
struct TTRaster *math_raster(Face *f);      /* the outline reader math layout uses for a face (NULL if none) */
/* a glyph's outline (face's glyph) as segments, font units y up, as ttr_segments; -1 if it can't */
int   math_glyph_segments(Face *f, u16 g, i32 *seg, int max);
/* filled outlines as spans: an outline's edges (nseg of them, x0 y0 x1 y1 each, in sixteenths of a
   pixel, y down) filled with the non-zero rule, its pixel rows from y0 to y1: every pixel the outline
   touches at any of four sample rows through it (so thin strokes stay whole), each row's stretches
   passed to span(ctx, y, x0, x1) (x1 inclusive) */
void  math_fill(const i32 *seg, int nseg, int y0, int y1, void (*span)(void *ctx, int y, int x0, int x1), void *ctx);

#endif
