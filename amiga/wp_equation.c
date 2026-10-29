/*
 * wp_equation.c - WP14b Equation editor (docs/wordcraft-gap.md). This file belongs to WP14b: only that
 * package edits it. Its commands are ID_WP14B_FIRST .. ID_WP14B_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The contextual Equation tab (shown while the caret is in an equation, or one is selected): Tools
 * (a new equation), Conversions (Unicode or LaTeX input, Convert between Professional and Linear,
 * Normal Text), Symbols and the eleven Structures galleries (pictures of each template drawn by the
 * equation layout itself, in popups of this file's own), and Equation Options (Display, Inline,
 * equation number, justification, structure commands). The editing itself is the page view's
 * (wp_equation_view.h): this file sends it CMD_EV_ commands (wp_equation.h).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_math.h"
#include "wp_equation.h"
#include "lang.h"
#include "theme.h"

int wp14b_latex, wp14b_normal;

enum { ID_EQ_NEW = ID_WP14B_FIRST, ID_EQ_UNICODE, ID_EQ_LATEX, ID_EQ_CONVERTDD, ID_EQ_PROF, ID_EQ_LIN, ID_EQ_ALLPROF, ID_EQ_ALLLIN,
       ID_EQ_NORMAL, ID_EQ_SYMBOLS, ID_EQ_GAL,      /* ID_EQ_GAL + MG_ (11 galleries) */
       ID_EQ_DISPLAY = ID_EQ_GAL + MG_N, ID_EQ_INLINE, ID_EQ_NUMBER, ID_EQ_JUSTDD, ID_EQ_JLEFT, ID_EQ_JRIGHT, ID_EQ_JCENTER, ID_EQ_JGROUP,
       ID_EQ_STRUCT, ID_EQ_ACT, ID_EQ_PICK, ID_EQ_CLOSE };
/* the state bits (RB_WP(WPI_14B, bit)) */
enum { EQS_TAB = WPS_TAB, EQS_LATEX, EQS_UNICODE, EQS_NORMAL, EQS_DISPLAY, EQS_INLINE, EQS_NUMBERED, EQS_EDIT, EQS_ACTIVE };
#define ST(b) RB_WP(WPI_14B, b)

/* ------------------------------------------------------------------ */
/* the ribbon */
static const char *const convnames[] = { "Current - Professional", "Current - Linear", "All - Professional", "All - Linear" };
static const RibbonList convlist = { convnames, 4, PL_TEXT, NULL };
static const char *const jcnames[] = { "Left", "Right", "Centered", "Centered as Group" };
static const RibbonList jclist = { jcnames, 4, PL_TEXT, NULL };
/* icons, in Quill's own icon language (theme.c) */
#define IC_EQ "ci L 2 3 14 3 L 2 3 1 5 L 5 3 5 13 L 4 13 6 13 L 10 3 10 12 L 10 12 12 13 L 12 13 14 12 ca L 1 15 14 15"
static const Item g_tools[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Equation", ID_EQ_NEW, 0, 0, 0, 0, "Insert a new equation (Alt+=); with text selected, the text made an equation", NULL, IC_EQ } };
static const Item g_conv[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Unicode", ID_EQ_UNICODE, ST(EQS_UNICODE), 0, 0, 0, "Type equations in linear format (UnicodeMath): a/b, x^2, \\sqrt(x)...", NULL, "ci R 1 1 14 14 ca B U" },
  { K_BUTTON, S_SMALL, 0, 0, "LaTeX", ID_EQ_LATEX, ST(EQS_LATEX), 0, 0, 0, "Type equations in LaTeX: \\frac{a}{b}, x^{2}, \\sqrt{x}...", NULL, "ci R 1 1 14 14 ca B Tx" },
  { K_BUTTON, S_SMALL, 0, 0, "Normal Text", ID_EQ_NORMAL, ST(EQS_NORMAL), 0, 0, 0, "Type normal text (not maths) in the equation", NULL, "ci L 1 13 14 13 ca B ab" },
  { K_DROP, S_SMALL, 0, 0, "Convert", ID_EQ_CONVERTDD, 0, ST(EQS_EDIT), F_NEWCOL, 0, "Convert between Professional (built up) and Linear form (Ctrl+= in an equation)", &convlist,
    "ci L 2 5 13 5 L 10 2 13 5 L 10 8 13 5 ca L 2 11 13 11 L 5 8 2 11 L 5 14 2 11" } };
static const Item g_sym[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Symbols", ID_EQ_SYMBOLS, 0, 0, 0, 0, "Mathematical symbols: basic maths, Greek letters, operators, arrows...", NULL, "ca O 5 8 3 O 11 8 3 ci L 1 14 14 14" } };
static const Item g_struct[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Fraction", ID_EQ_GAL + MG_FRACTION, 0, 0, 0, 0, "Fractions: stacked, skewed, linear, no bar", NULL, "ci R 5 1 10 5 ca L 2 8 13 8 ci R 5 10 10 14" },
  { K_BUTTON, S_SMALL, 0, 0, "Script", ID_EQ_GAL + MG_SCRIPT, 0, 0, 0, 0, "Subscripts and superscripts", NULL, "ci R 1 6 8 14 ca R 10 1 14 5" },
  { K_BUTTON, S_SMALL, 0, 0, "Radical", ID_EQ_GAL + MG_RADICAL, 0, 0, 0, 0, "Square roots and roots with a degree", NULL, "ci P 5 1 9 3 7 6 14 9 2 15 2 ca R 10 5 14 12" },
  { K_BUTTON, S_SMALL, 0, 0, "Integral", ID_EQ_GAL + MG_INTEGRAL, 0, 0, 0, 0, "Integrals, with or without limits", NULL, "ci P 6 10 2 9 1 7 2 7 13 5 14 4 13 ca R 10 6 14 10" },
  { K_BUTTON, S_SMALL, 0, 0, "Large Operator", ID_EQ_GAL + MG_LARGEOP, 0, 0, 0, 0, "Summations, products, unions, intersections and other large operators", NULL, "ci P 5 10 2 2 2 7 8 2 14 10 14 ca R 12 6 15 10" },
  { K_BUTTON, S_SMALL, 0, 0, "Bracket", ID_EQ_GAL + MG_BRACKET, 0, 0, 0, 0, "Brackets, brackets with separators, cases and stacks", NULL, "ci P 4 5 1 3 3 3 12 5 14 P 4 10 1 12 3 12 12 10 14 ca R 6 5 9 10" },
  { K_BUTTON, S_SMALL, 0, 0, "Function", ID_EQ_GAL + MG_FUNCTION, 0, 0, F_NEWCOL, 0, "Trigonometric and hyperbolic functions", NULL, "ci I f ca R 10 5 14 11" },
  { K_BUTTON, S_SMALL, 0, 0, "Accent", ID_EQ_GAL + MG_ACCENT, 0, 0, 0, 0, "Accents, overbars, braces and boxed formulas", NULL, "ci P 3 4 4 8 1 12 4 ca R 4 7 12 14" },
  { K_BUTTON, S_SMALL, 0, 0, "Limit and Log", ID_EQ_GAL + MG_LIMLOG, 0, 0, 0, 0, "Limits, logarithms, minimum and maximum", NULL, "ca B lim" },
  { K_BUTTON, S_SMALL, 0, 0, "Operator", ID_EQ_GAL + MG_OPERATOR, 0, 0, F_NEWCOL, 0, "Operators, and arrows with text above or below", NULL, "ci L 2 10 14 10 L 2 13 14 13 ca P 4 8 1 11 6 5 6 8 1" },
  { K_BUTTON, S_SMALL, 0, 0, "Matrix", ID_EQ_GAL + MG_MATRIX, 0, 0, 0, 0, "Matrices, empty or with brackets, and dots", NULL,
    "ci P 4 3 1 1 1 1 14 3 14 P 4 12 1 14 1 14 14 12 14 ca R 4 3 6 5 R 9 3 11 5 R 4 9 6 11 R 9 9 11 11" } };
static const Item g_opts[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Display", ID_EQ_DISPLAY, ST(EQS_DISPLAY), ST(EQS_EDIT), 0, 0, "Change to Display: the equation on a line of its own", NULL, "ci L 1 2 14 2 L 1 13 14 13 ca L 4 6 11 6 L 4 9 11 9" },
  { K_BUTTON, S_SMALL, 0, 0, "Inline", ID_EQ_INLINE, ST(EQS_INLINE), ST(EQS_EDIT), 0, 0, "Change to Inline: the equation in the line of text", NULL, "ci L 1 7 4 7 L 11 7 14 7 ca R 5 4 10 10" },
  { K_BUTTON, S_SMALL, 0, 0, "Equation Number", ID_EQ_NUMBER, ST(EQS_NUMBERED), ST(EQS_EDIT), 0, 0, "Number the equation: (1), (2)... at the right margin (or type # and the number)", NULL, "ca B #" },
  { K_DROP, S_SMALL, 0, 0, "Justification", ID_EQ_JUSTDD, 0, ST(EQS_EDIT), F_NEWCOL, 0, "Where a display equation sits on its line", &jclist, "ci L 1 3 14 3 L 4 7 11 7 L 1 11 14 11 ca L 4 14 11 14" },
  { K_BUTTON, S_SMALL, 0, 0, "Structure Commands", ID_EQ_STRUCT, 0, ST(EQS_ACTIVE), 0, 0, "Commands for the structure at the caret: insert or delete matrix rows and columns, change brackets...", NULL,
    "ci R 1 1 9 9 L 1 5 9 5 L 5 1 5 9 ca L 12 9 12 15 L 9 12 15 12" },
  { K_BUTTON, S_SMALL, 0, 0, "Close Equation", ID_EQ_CLOSE, 0, ST(EQS_ACTIVE), 0, 0, "Leave the equation (Esc)", NULL, "ci R 1 3 10 12 ca L 8 8 15 8 L 12 5 15 8 L 12 11 15 8" } };
#define N(a) (int)(sizeof a / sizeof a[0])
static const WpGroup groups[] = {
  { TAB_EQUATION, -1, { "Tools", G_COLS, g_tools, N(g_tools) } },
  { TAB_EQUATION, -1, { "Conversions", G_COLS, g_conv, N(g_conv) } },
  { TAB_EQUATION, -1, { "Symbols", G_COLS, g_sym, N(g_sym) } },
  { TAB_EQUATION, -1, { "Structures", G_COLS, g_struct, N(g_struct) } },
  { TAB_EQUATION, -1, { "Equation Options", G_COLS, g_opts, N(g_opts) } } };

static const WpMenu menus[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Equation Tools", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Insert New Equation", 0, 0, 0, (APTR)ID_EQ_NEW } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Symbols...", 0, 0, 0, (APTR)ID_EQ_SYMBOLS } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Fraction...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_FRACTION) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Script...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_SCRIPT) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Radical...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_RADICAL) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Integral...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_INTEGRAL) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Large Operator...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_LARGEOP) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Bracket...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_BRACKET) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Function...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_FUNCTION) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Accent...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_ACCENT) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Limit and Log...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_LIMLOG) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Operator...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_OPERATOR) } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Matrix...", 0, 0, 0, (APTR)(ID_EQ_GAL + MG_MATRIX) } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Equation", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Unicode", 0, 0, 0, (APTR)ID_EQ_UNICODE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"LaTeX", 0, 0, 0, (APTR)ID_EQ_LATEX } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Current - Professional", 0, 0, 0, (APTR)ID_EQ_PROF } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Current - Linear", 0, 0, 0, (APTR)ID_EQ_LIN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"All - Professional", 0, 0, 0, (APTR)ID_EQ_ALLPROF } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"All - Linear", 0, 0, 0, (APTR)ID_EQ_ALLLIN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Normal Text", 0, 0, 0, (APTR)ID_EQ_NORMAL } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Change to Display", 0, 0, 0, (APTR)ID_EQ_DISPLAY } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Change to Inline", 0, 0, 0, (APTR)ID_EQ_INLINE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Equation Number", 0, 0, 0, (APTR)ID_EQ_NUMBER } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Left", 0, 0, 0, (APTR)ID_EQ_JLEFT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Right", 0, 0, 0, (APTR)ID_EQ_JRIGHT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Centered", 0, 0, 0, (APTR)ID_EQ_JCENTER } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Centered as Group", 0, 0, 0, (APTR)ID_EQ_JGROUP } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Structure Commands...", 0, 0, 0, (APTR)ID_EQ_STRUCT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Close Equation", 0, 0, 0, (APTR)ID_EQ_CLOSE } } };

/* ------------------------------------------------------------------ */
/* the galleries' pictures: each template an equation of a document of its own, drawn as the page draws one */
static Doc *pdoc;
static int *galidx[MG_N];              /* each gallery's templates' equation numbers, section after section */
static int *symidx[MSYM_N]; static int nsym[MSYM_N];
static FontSet *pfs;
static LONG pop_pen(ULONG rgb) { (void)rgb; return tpen(T_TEXT); }
static int gal_count(int g)
{
  int s, n = 0;
  for (s = 0; s < math_galleries[g].n; s++) n += math_galleries[g].s[s].n;
  return n;
}
static int make_gallery(int g)
{
  int s, k, i = 0;
  if (!pdoc && !(pdoc = doc_new())) return 0;
  if (galidx[g]) return 1;
  if (!(galidx[g] = (int *)malloc((size_t)gal_count(g) * sizeof(int)))) return 0;
  for (s = 0; s < math_galleries[g].n; s++)
    for (k = 0; k < math_galleries[g].s[s].n; k++) galidx[g][i++] = doc_add_math(pdoc, math_template_eq(&math_galleries[g].s[s].t[k]));
  return 1;
}
static int make_symbols(int set)
{
  const char *p;
  int n = 0;
  if (!pdoc && !(pdoc = doc_new())) return 0;
  if (symidx[set]) return 1;
  for (p = math_symbol_sets[set]; *p; p++) if (((u8)*p & 0xC0) != 0x80) n++;
  if (!(symidx[set] = (int *)malloc((size_t)n * sizeof(int)))) return 0;
  nsym[set] = 0;
  for (p = math_symbol_sets[set]; *p; ) {
    int used;
    MathEq *e = math_new(0);
    MNode *r;
    utf8_get(p, strlen(p), &used);
    if (e && (r = mn_run(p, (size_t)used)) && mn_insert(e->line[0], -1, r)) symidx[set][nsym[set]++] = doc_add_math(pdoc, e);
    else { math_free(e); symidx[set][nsym[set]++] = -1; }
    p += used;
  }
  return 1;
}
typedef struct { struct RastPort *rp; LONG x, base, zoom; } PSlot;
static void pslot(void *ctx, i32 x, i32 top, i32 w, i32 h)
{
  PSlot *v = (PSlot *)ctx;
  LONG x0 = v->x + x * v->zoom / 2000, y0 = v->base + top * v->zoom / 2000, x1 = v->x + (x + w) * v->zoom / 2000, y1 = v->base + (top + h) * v->zoom / 2000, t;
  if (x1 - x0 < 3) x1 = x0 + 3;
  if (y1 - y0 < 4) y1 = y0 + 4;
  for (t = x0; t <= x1; t += 2) { WritePixel(v->rp, t, y0); WritePixel(v->rp, t, y1); }
  for (t = y0; t <= y1; t += 2) { WritePixel(v->rp, x0, t); WritePixel(v->rp, x1, t); }
}
/* equation k of the pictures' document drawn in the middle of a box, smaller if it doesn't fit */
static void draw_eq(struct RastPort *rp, int k, LONG x0, LONG y0, LONG w, LONG h, int size)
{
  CharFmt f;
  const MLay *ml;
  Layout L;
  Box b;
  char ref[8];
  LONG zoom = 100, x, base;
  MathEq *e;
  if (!pdoc || !pfs || k < 0) return;
  f = pdoc->styles[0].cf; f.size = (u16)size; f.color = COLOR_AUTO;
  if (!(ml = math_layout(pfs, pdoc, k, &f)) || ml->w <= 0) return;
  if (ml->w * zoom / 2000 > w - 4) zoom = (w - 4) * 2000 / ml->w;
  if ((ml->asc + ml->desc) * zoom / 2000 > h - 4) zoom = (h - 4) * 2000 / (ml->asc + ml->desc > 0 ? ml->asc + ml->desc : 1);
  if (zoom < 20) zoom = 20;
  x = x0 + (w - ml->w * zoom / 2000) / 2;
  base = y0 + (h - (ml->asc + ml->desc) * zoom / 2000) / 2 + ml->asc * zoom / 2000;
  memset(&L, 0, sizeof L); L.d = pdoc; L.fs = pfs;
  memset(&b, 0, sizeof b); b.kind = BX_MATH; b.f = f; b.len = 4;
  math_utf8(ref, k);
  wp14a_draw(rp, &L, &b, ref, x, base, zoom, pop_pen);
  if ((e = doc_math(pdoc, k))) {
    PSlot v;
    v.rp = rp; v.x = x; v.base = base; v.zoom = zoom;
    SetAPen(rp, tpen(T_DIM));
    me_slots(e, pslot, &v);
  }
}

/* ------------------------------------------------------------------ */
/* the popup: a gallery's templates by section, the symbol sets, or the structure commands */
enum { POP_GALLERY, POP_SYMBOLS, POP_LIST };
#define TW 44
#define TH 34
#define MAXT 400
typedef struct { WORD x, y, w, h; short kind, index; } Tile;     /* kind: 0 a heading, 1 a template or symbol, 2 a symbol set, 3 a list entry */
struct PopData {
  int mode, gal, set, hot;
  LONG w, h;
  Tile t[MAXT]; int nt;
};
static struct MUI_CustomClass *popcls;
static Object *popwin, *popobj;
static const MTemplate *picked_t;        /* what the popup chose (ID_EQ_PICK) */
static int picked_sym = -1, picked_set, picked_act = -1;
static EvInfo info;                      /* the equation as the last state call saw it */
static MAct listact[EV_MAXACT]; static const char *listlab[EV_MAXACT]; static int nlist;

static LONG fh(void) { return theme_font()->tf_YSize; }
static void pop_layout(struct PopData *p)
{
  int s, k, i = 0;
  LONG x, y;
  p->nt = 0;
  if (p->mode == POP_GALLERY) {
    const MGallery *g = &math_galleries[p->gal];
    LONG W = 8 * TW + 8;
    y = 2;
    for (s = 0; s < g->n; s++) {
      const MSection *sc = &g->s[s];
      int wide = !strncmp(sc->name, "Common", 6), any = 0;
      for (k = 0; k < sc->n; k++) if (strstr(sc->t[k].linear, "\xe2\xac\x9a")) any = 1;
      if (!any && sc->n <= 3) wide = 1;
      if (p->nt < MAXT) { Tile *t = &p->t[p->nt++]; t->x = 2; t->y = (WORD)y; t->w = (WORD)(W - 4); t->h = (WORD)(fh() + 4); t->kind = 0; t->index = (short)s; }
      y += fh() + 6;
      x = 4;
      for (k = 0; k < sc->n; k++, i++) {
        LONG tw = wide ? 2 * TW + 2 : TW, th = wide ? TH + 8 : TH;
        if (x + tw > W - 2) { x = 4; y += th + 2; }
        if (p->nt < MAXT) { Tile *t = &p->t[p->nt++]; t->x = (WORD)x; t->y = (WORD)y; t->w = (WORD)tw; t->h = (WORD)th; t->kind = 1; t->index = (short)i; }
        x += tw + 2;
        if (k + 1 == sc->n) y += th + 4;
      }
    }
    p->w = W; p->h = y + 2;
  } else if (p->mode == POP_SYMBOLS) {
    LONG cw = 0, gx, rows = 0;
    struct RastPort rp;
    InitRastPort(&rp);
    for (s = 0; s < MSYM_N; s++) { LONG l = theme_textlen(&rp, theme_font(), tr(math_symbol_set_names[s])); if (l > cw) cw = l; }
    cw += 12;
    for (s = 0; s < MSYM_N; s++) {
      Tile *t = &p->t[p->nt++];
      t->x = 2; t->y = (WORD)(2 + s * (fh() + 6)); t->w = (WORD)cw; t->h = (WORD)(fh() + 5); t->kind = 2; t->index = (short)s;
    }
    gx = cw + 8;
    for (k = 0; k < nsym[p->set] && p->nt < MAXT; k++) {
      Tile *t = &p->t[p->nt++];
      t->x = (WORD)(gx + (k % 12) * 22); t->y = (WORD)(2 + (k / 12) * 22); t->w = 21; t->h = 21; t->kind = 1; t->index = (short)k;
    }
    for (s = 0; s < MSYM_N; s++) { int n = 0; const char *q; for (q = math_symbol_sets[s]; *q; q++) if (((u8)*q & 0xC0) != 0x80) n++; if ((n + 11) / 12 > rows) rows = (n + 11) / 12; }
    p->w = gx + 12 * 22 + 4;
    p->h = rows * 22 + 4;
    if (p->h < 2 + MSYM_N * (fh() + 6)) p->h = 2 + MSYM_N * (fh() + 6);
    p->h += fh() + 6;                                   /* the line naming the symbol under the pointer */
  } else {
    LONG w = 120;
    struct RastPort rp;
    InitRastPort(&rp);
    for (k = 0; k < nlist && p->nt < MAXT; k++) {
      Tile *t = &p->t[p->nt++];
      LONG l = theme_textlen(&rp, theme_font(), tr(listlab[k])) + 16;
      if (l > w) w = l;
      t->x = 2; t->y = (WORD)(2 + k * (fh() + 5)); t->h = (WORD)(fh() + 4); t->kind = 3; t->index = (short)k;
    }
    for (k = 0; k < p->nt; k++) p->t[k].w = (WORD)(w - 4);
    p->w = w; p->h = 4 + nlist * (fh() + 5);
  }
}
static ULONG pop_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PopData *p;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    p = INST_DATA(cl, obj);
    memset(p, 0, sizeof *p);
    p->hot = -1;
    return (ULONG)obj;
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    MUI_RequestIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    return TRUE;
  case MUIM_Cleanup:
    MUI_RejectIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    p = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += p->w; m->MinMaxInfo->DefWidth += p->w; m->MinMaxInfo->MaxWidth += p->w;
    m->MinMaxInfo->MinHeight += p->h; m->MinMaxInfo->DefHeight += p->h; m->MinMaxInfo->MaxHeight += p->h;
    return 0;
  }
  case MUIM_Draw: {
    struct RastPort *rp;
    int i;
    LONG ox, oy;
    DoSuperMethodA(cl, obj, msg);
    p = INST_DATA(cl, obj);
    rp = _rp(obj); ox = _mleft(obj); oy = _mtop(obj);
    theme_fill(rp, ox, oy, _mright(obj), _mbottom(obj), tpen(T_WHITE));
    theme_rect(rp, ox, oy, _mright(obj), _mbottom(obj), tpen(T_BORDER2));
    for (i = 0; i < p->nt; i++) {
      Tile *t = &p->t[i];
      LONG x0 = ox + t->x, y0 = oy + t->y, x1 = x0 + t->w - 1, y1 = y0 + t->h - 1;
      if (t->kind == 0) {
        theme_fill(rp, x0, y0, x1, y1, tpen(T_SELECTION));
        theme_text(rp, theme_bold() ? theme_bold() : theme_font(), x0 + 4, y0 + 2, tr(math_galleries[p->gal].s[t->index].name), tpen(T_TEXT));
        continue;
      }
      if (t->kind == 2 || t->kind == 3) {
        int on = t->kind == 2 ? t->index == p->set : 0;
        if (i == p->hot || on) theme_fill(rp, x0, y0, x1, y1, tpen(i == p->hot ? T_SELECTION : T_BORDER2));
        theme_text(rp, theme_font(), x0 + 6, y0 + 2, tr(t->kind == 2 ? math_symbol_set_names[t->index] : listlab[t->index]), tpen(T_TEXT));
        continue;
      }
      theme_rect(rp, x0, y0, x1, y1, tpen(i == p->hot ? T_ACCENT : T_BORDER2));
      if (i == p->hot) theme_rect(rp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, tpen(T_ACCENT));
      if (p->mode == POP_GALLERY) draw_eq(rp, galidx[p->gal][t->index], x0 + 1, y0 + 1, t->w - 2, t->h - 2, 24);
      else draw_eq(rp, symidx[p->set][t->index], x0 + 1, y0 + 1, t->w - 2, t->h - 2, 24);
    }
    if (p->mode == POP_SYMBOLS && p->hot >= 0 && p->t[p->hot].kind == 1) {
      /* the symbol under the pointer: its code point */
      const char *q = math_symbol_sets[p->set];
      int k = p->t[p->hot].index, used = 0;
      char b[24];
      u32 c = 0;
      while (*q && k >= 0) { c = utf8_get(q, strlen(q), &used); q += used; k--; }
      sprintf(b, "U+%04lX", (unsigned long)c);
      theme_text(rp, theme_font(), ox + 6, _mbottom(obj) - fh() - 3, b, tpen(T_DIM));
    }
    return 0;
  }
  case MUIM_HandleInput: {
    struct MUIP_HandleInput *m = (struct MUIP_HandleInput *)msg;
    p = INST_DATA(cl, obj);
    if (m->imsg && (m->imsg->Class == IDCMP_MOUSEMOVE || m->imsg->Class == IDCMP_MOUSEBUTTONS)) {
      WORD mx = (WORD)(m->imsg->MouseX - _mleft(obj)), my = (WORD)(m->imsg->MouseY - _mtop(obj));
      int i, hit = -1;
      for (i = 0; i < p->nt; i++) {
        Tile *t = &p->t[i];
        if (t->kind && mx >= t->x && mx < t->x + t->w && my >= t->y && my < t->y + t->h) { hit = i; break; }
      }
      if (m->imsg->Class == IDCMP_MOUSEMOVE && hit != p->hot) { p->hot = hit; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      if (m->imsg->Class == IDCMP_MOUSEBUTTONS && m->imsg->Code == SELECTUP && hit >= 0) {
        Tile *t = &p->t[hit];
        if (t->kind == 2) {
          /* another symbol set */
          if (t->index != p->set && make_symbols(t->index)) { p->set = t->index; p->hot = -1; pop_layout(p); MUI_Redraw(obj, MADF_DRAWOBJECT); }
          return 0;
        }
        if (t->kind == 1 && p->mode == POP_GALLERY) {
          int s, n = t->index;
          const MGallery *g = &math_galleries[p->gal];
          picked_t = NULL;
          for (s = 0; s < g->n && !picked_t; s++) { if (n < g->s[s].n) picked_t = &g->s[s].t[n]; else n -= g->s[s].n; }
          picked_sym = -1; picked_act = -1;
        } else if (t->kind == 1) { picked_sym = t->index; picked_set = p->set; picked_t = NULL; picked_act = -1; }
        else if (t->kind == 3) { picked_act = t->index; picked_t = NULL; picked_sym = -1; }
        DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)_win(obj), 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(_app(obj), MUIM_Application_ReturnID, ID_EQ_PICK);
      }
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static void open_popup(int mode, int g)
{
  struct PopData *p;
  struct Window *w = NULL;
  LONG x = 40, y = 60;
  if (!popcls && !(popcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PopData), (APTR)pop_dispatch))) return;
  if (!popwin) {
    popobj = NewObject(popcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
    popwin = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
      MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE, MUIA_Window_Activate, TRUE,
      WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, popobj, End, End;
    if (!popwin) { popobj = NULL; return; }
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)popwin);
    DoMethod(popwin, MUIM_Notify, MUIA_Window_Activate, FALSE, (ULONG)popwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  {
    EvInfo in;
    wp14b_view_info(wp_doc(), &in);
    pfs = in.fs;
  }
  if (!pfs) { DisplayBeep(NULL); return; }
  if (mode == POP_GALLERY && !make_gallery(g)) return;
  if (mode == POP_SYMBOLS && !make_symbols(picked_set)) return;
  set(popwin, MUIA_Window_Open, FALSE);
  p = INST_DATA(popcls->mcc_Class, popobj);
  p->mode = mode; p->gal = g; p->set = picked_set; p->hot = -1;
  pop_layout(p);
  if (wp_win()) get(wp_win(), MUIA_Window_Window, &w);
  if (w) {
    x = w->LeftEdge + w->MouseX - 24; y = w->TopEdge + w->MouseY + 8;
    if (x + p->w + 4 > w->WScreen->Width) x = w->WScreen->Width - p->w - 4;
    if (y + p->h + 4 > w->WScreen->Height) y = w->WScreen->Height - p->h - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
  }
  SetAttrs(popwin, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, TAG_DONE);
  set(popwin, MUIA_Window_Open, TRUE);
}

/* ------------------------------------------------------------------ */
static void view_cmd(ULONG cmd, ULONG arg) { if (wp_view()) DoMethod(wp_view(), MUIM_Quill_Command, cmd, arg); }
/* the structure commands that apply at the caret (the list popup, the right-button menu) */
static int take_actions(void)
{
  int i;
  wp14b_view_info(wp_doc(), &info);
  nlist = info.nact;
  for (i = 0; i < nlist; i++) { listact[i] = info.act[i]; listlab[i] = info.label[i]; }
  return nlist;
}
int wp14b_command(ULONG id, LONG value)
{
  if (id >= ID_EQ_GAL && id < ID_EQ_GAL + MG_N) { open_popup(POP_GALLERY, (int)(id - ID_EQ_GAL)); return WPR_DONE; }
  switch (id) {
  case ID_EQ_NEW: view_cmd(CMD_EV_NEW, 0); break;
  case ID_EQ_UNICODE: wp14b_latex = 0; wp_refresh(); break;
  case ID_EQ_LATEX: wp14b_latex = 1; wp_refresh(); break;
  case ID_EQ_CONVERTDD: if (value >= 0 && value < 4) view_cmd(CMD_EV_CONVERT, (ULONG)value); break;
  case ID_EQ_PROF: view_cmd(CMD_EV_CONVERT, 0); break;
  case ID_EQ_LIN: view_cmd(CMD_EV_CONVERT, 1); break;
  case ID_EQ_ALLPROF: view_cmd(CMD_EV_CONVERT, 2); break;
  case ID_EQ_ALLLIN: view_cmd(CMD_EV_CONVERT, 3); break;
  case ID_EQ_NORMAL: wp14b_normal = !wp14b_normal; view_cmd(CMD_EV_NORMAL, 0); wp_refresh(); break;
  case ID_EQ_SYMBOLS: open_popup(POP_SYMBOLS, 0); break;
  case ID_EQ_DISPLAY: view_cmd(CMD_EV_DISPLAY, 1); break;
  case ID_EQ_INLINE: view_cmd(CMD_EV_DISPLAY, 0); break;
  case ID_EQ_NUMBER: view_cmd(CMD_EV_NUMBER, 0); break;
  case ID_EQ_JUSTDD: if (value >= 0 && value < 4) view_cmd(CMD_EV_JUSTIFY, (ULONG)value); break;     /* left, right, centred, as a group: MPJC_ */
  case ID_EQ_JLEFT: case ID_EQ_JRIGHT: case ID_EQ_JCENTER: case ID_EQ_JGROUP: view_cmd(CMD_EV_JUSTIFY, id - ID_EQ_JLEFT); break;
  case ID_EQ_STRUCT:
    if (!take_actions()) { DisplayBeep(NULL); break; }
    open_popup(POP_LIST, 0);
    break;
  case ID_EQ_ACT:                       /* from the right-button menu */
    if (value >= 0 && value < nlist) view_cmd(CMD_EV_APPLY, ((ULONG)listact[value].act << 8) | listact[value].depth);
    break;
  case ID_EQ_PICK:
    if (picked_t) { MNode *row = math_template(picked_t); if (row) view_cmd(CMD_EV_INSERT, (ULONG)row); picked_t = NULL; }
    else if (picked_sym >= 0) {
      const char *q = math_symbol_sets[picked_set];
      int k = picked_sym, used = 0;
      char one[8];
      while (*q && k > 0) { utf8_get(q, strlen(q), &used); q += used; k--; }
      if (*q) { utf8_get(q, strlen(q), &used); memcpy(one, q, (size_t)used); one[used] = 0; view_cmd(CMD_EV_TEXT, (ULONG)one); }
      picked_sym = -1;
    } else if (picked_act >= 0 && picked_act < nlist) {
      view_cmd(CMD_EV_APPLY, ((ULONG)listact[picked_act].act << 8) | listact[picked_act].depth);
      picked_act = -1;
    }
    if (wp_win()) set(wp_win(), MUIA_Window_Activate, TRUE);
    break;
  case ID_EQ_CLOSE: view_cmd(CMD_EV_CLOSE, 0); break;
  }
  return WPR_DONE;
}
static void wp14b_state(ULONG *on, const WpCtx *c)
{
  wp14b_view_info(c->doc, &info);
  if (info.selected) *on |= RBIT(EQS_TAB) | RBIT(EQS_EDIT) | (info.display ? RBIT(EQS_DISPLAY) : RBIT(EQS_INLINE)) | (info.numbered ? RBIT(EQS_NUMBERED) : 0);
  if (info.active) *on |= RBIT(EQS_ACTIVE);
  *on |= wp14b_latex ? RBIT(EQS_LATEX) : RBIT(EQS_UNICODE);
  if (wp14b_normal || info.normal) *on |= RBIT(EQS_NORMAL);
}
static void wp14b_ctxmenu(Object *menu, const WpCtx *c)
{
  if (c->where != WPC_TEXT) return;
  wp14b_view_info(c->doc, &info);
  if (!info.active) return;
  take_actions();
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr(info.linear ? "Professional" : "Linear"), WP_CTX_ID(info.linear ? ID_EQ_PROF : ID_EQ_LIN), 1);
  wp_ctx_item(menu, tr(info.display ? "Change to Inline" : "Change to Display"), WP_CTX_ID(info.display ? ID_EQ_INLINE : ID_EQ_DISPLAY), 1);
  if (info.display) wp_ctx_sub(menu, tr("Justification"), ID_EQ_JUSTDD, jcnames, 4);
  wp_ctx_item(menu, tr("Equation Number"), WP_CTX_ID(ID_EQ_NUMBER), 1);
  if (nlist) wp_ctx_sub(menu, tr("Structure Commands"), ID_EQ_ACT, listlab, nlist);
}
static void wp14b_cleanup(void)
{
  int i;
  popwin = popobj = NULL;                /* (disposed with the application, before this is called) */
  if (popcls) { MUI_DeleteCustomClass(popcls); popcls = NULL; }
  for (i = 0; i < MG_N; i++) { free(galidx[i]); galidx[i] = NULL; }
  for (i = 0; i < MSYM_N; i++) { free(symidx[i]); symidx[i] = NULL; }
  doc_free(pdoc); pdoc = NULL;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp14b_package = {
  "WP14b", ID_WP14B_FIRST, ID_WP14B_LAST,
  wp14b_command, wp14b_state, NULL, wp14b_ctxmenu, NULL, NULL, wp14b_cleanup,
  groups, N(groups),
  menus, N(menus)
};
