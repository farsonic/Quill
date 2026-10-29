/* test_wp14b.c - the native tests of WP14b Equation editor (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp14b() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/mathedit.h"
#include "../core/layout.h"
#include "../core/zip.h"

static char *lin(const MathEq *e)
{
  Buf b;
  buf_init(&b);
  math_to_linear(e, &b);
  if (!b.data) buf_adds(&b, "");
  return buf_take(&b);
}
static int lin_is(const MathEq *e, const char *want)
{
  char *t = lin(e);
  int same = !strcmp(t, want);
  if (!same) printf("  linear is \"%s\", wanted \"%s\"\n", t, want);
  free(t);
  return same;
}
static char *latex(const MathEq *e)
{
  Buf b;
  buf_init(&b);
  math_to_latex(e, &b);
  if (!b.data) buf_adds(&b, "");
  return buf_take(&b);
}
static int has_leftover(const MNode *n)
{
  u16 i;
  if (n->kind == MK_RUN && n->text && (strstr(n->text, "\xe2\xac\x9a") || strstr(n->text, "\xe2\x96\x92") || strstr(n->text, "\xe2\x94\xac") || strstr(n->text, "\xe2\x94\xb4") ||
                                       strstr(n->text, "\xe2\x94\x9c") || strstr(n->text, "\xe2\x94\xa4"))) return 1;
  for (i = 0; i < n->nkid; i++) if (has_leftover(n->kid[i])) return 1;
  return 0;
}
static int count_kind(const MNode *n, int kind)
{
  int c = n->kind == kind;
  u16 i;
  for (i = 0; i < n->nkid; i++) c += count_kind(n->kid[i], kind);
  return c;
}

static void t_gallery(void)
{
  int g, s, k, bad = 0, structs = 0, total = 0;
  for (g = 0; g < MG_N; g++)
    for (s = 0; s < math_galleries[g].n; s++)
      for (k = 0; k < math_galleries[g].s[s].n; k++) {
        const MTemplate *t = &math_galleries[g].s[s].t[k];
        MathEq *e = math_template_eq(t);
        total++;
        if (!e || has_leftover(e->line[0])) { bad++; printf("  template %s left linear operators\n", t->linear); }
        else if (strstr(t->linear, "\xe2\xac\x9a")) {
          u16 i;
          int st = 0;
          for (i = 0; i < e->line[0]->nkid; i++) if (e->line[0]->kid[i]->kind != MK_RUN) st = 1;
          if (st) structs++; else printf("  template %s built no structure\n", t->linear);
        }
        math_free(e);
      }
  ok("gallery: 11 structure galleries with templates", total > 150);
  ok("gallery: every template builds without leftover linear operators", !bad);
  ok("gallery: templates with empty arguments are structures", structs > 100);
  {
    int chars = 0;
    for (s = 0; s < MSYM_N; s++) { const char *p = math_symbol_sets[s]; while (*p) { if (((u8)*p & 0xC0) != 0x80) chars++; p++; } }
    ok("gallery: 8 symbol sets", chars > 400 && !strcmp(math_symbol_set_names[1], "Greek Letters"));
  }
  {
    /* brackets: single ones and the Left/Right forms */
    MathEq *e = math_from_linear("\xe2\x94\x9c]\xe2\xac\x9a[\xe2\x94\xa4", 0);    /* ├]⬚[┤ */
    const MNode *d = e ? e->line[0]->kid[0] : NULL;
    ok("brackets: outward brackets", d && d->kind == MK_DELIM && d->ch[0] == ']' && d->ch[2] == '[' && d->kid[0]->nkid == 0);
    math_free(e);
    e = math_from_linear("{\xe2\x96\x88(\xe2\xac\x9a@\xe2\xac\x9a)\xe2\x94\xa4", 0);   /* {█(⬚@⬚)┤: cases */
    d = e ? e->line[0]->kid[0] : NULL;
    ok("brackets: cases (a left brace alone round an array)", d && d->kind == MK_DELIM && d->ch[0] == '{' && d->ch[2] == 0 && d->kid[0]->kid[0]->kind == MK_EQARR);
    ok("brackets: written back with ├ … ┤", e && lin_is(e, "\xe2\x94\x9c{\xe2\x96\x88(@)\xe2\x94\xa4"));
    math_free(e);
    e = math_from_linear("\xe2\x89\x94\xe2\x94\xb4\xe2\xac\x9a", 0);   /* ≔┴⬚ */
    d = e ? e->line[0]->kid[0] : NULL;
    ok("operators: text over an operator", d && d->kind == MK_LIMUPP);
    math_free(e);
  }
}

static void t_editing(void)
{
  MathEq *e = math_new(1);
  MPos p, a;
  int sel;
  MNode *row;
  mp_start(e, &p);
  me_insert_text(e, &p, "y=", 0);
  ok("edit: typing into an empty equation", lin_is(e, "y=") && p.off == 2);
  /* a fraction from the gallery: the caret goes into its numerator */
  me_insert_row(e, &p, math_template(&math_galleries[MG_FRACTION].s[0].t[0]), NULL);
  ok("edit: a fraction put in, the caret in its numerator", p.depth == 1 && p.arg[0] == 0 && p.off == 0 && p.node[0] == 2);
  me_insert_text(e, &p, "a", 0);
  ok("edit: the next empty argument (Tab)", me_next_slot(e, &p, 0) && p.depth == 1 && p.arg[0] == 1);
  me_insert_text(e, &p, "b", 0);
  ok("edit: typed into numerator and denominator", lin_is(e, "y= a/b"));
  ok("edit: right arrow out of the fraction", me_right(e, &p) && p.depth == 0 && p.off == 3);
  ok("edit: right at the end leaves the equation", !me_right(e, &p));
  me_left(e, &p);
  ok("edit: left goes back into the denominator's end", p.depth == 1 && p.arg[0] == 1 && p.off == 1);
  ok("edit: up into the numerator", me_vertical(e, &p, 1) && p.arg[0] == 0);
  me_home(e, &p, 1); me_home(e, &p, 1);
  ok("edit: End twice: the equation's end", p.depth == 0 && p.off == 3);
  /* Backspace after a structure selects it, then deletes it */
  me_backspace(e, &p, &a, &sel);
  ok("edit: Backspace selects the fraction first", sel && a.off == 2 && p.off == 3);
  {
    MPos q;
    MNode *cut = me_cut(e, &a, &p, &q);
    ok("edit: then it goes", cut && cut->nkid == 1 && cut->kid[0]->kind == MK_FRAC && lin_is(e, "y=") && q.off == 2);
    p = q;
    /* a selection put into a structure: x selected, Radical: √x */
    mn_free(cut);
  }
  me_insert_text(e, &p, "x", 0);
  a = p; a.off = 2;
  {
    MPos q;
    MNode *cut = me_cut(e, &a, &p, &q);
    p = q;
    me_insert_row(e, &p, math_template(&math_galleries[MG_RADICAL].s[0].t[0]), cut);
    ok("edit: the selection goes into the structure", lin_is(e, "y=\xe2\x88\x9a(x)"));
  }
  /* deleting characters */
  mp_end(e, &p);
  me_insert_text(e, &p, "+12", 0);
  me_backspace(e, &p, &a, &sel);
  ok("edit: Backspace deletes a character", lin_is(e, "y=\xe2\x88\x9a(x)+1") && p.off == 5);
  p.off = 0;
  me_delete(e, &p, &a, &sel);
  ok("edit: Delete deletes a character", lin_is(e, "=\xe2\x88\x9a(x)+1"));
  /* Backspace at the start of an argument dissolves its structure */
  p.off = 1; me_right(e, &p);
  ok("edit: right into the radical", p.depth == 1 && p.arg[0] == 1 && p.off == 0);
  me_backspace(e, &p, &a, &sel);
  ok("edit: Backspace at an argument's start takes the structure away, keeping its contents", lin_is(e, "=x+1") && p.depth == 0 && p.off == 1);
  math_free(e);

  /* build-up and AutoCorrect */
  e = math_new(0);
  mp_start(e, &p);
  me_insert_text(e, &p, "x^2", 0);
  ok("build-up: x^2 builds a superscript", me_build_up(e, &p, 0) && e->line[0]->nkid == 1 && e->line[0]->kid[0]->kind == MK_SSUP && p.off == 1);
  me_insert_text(e, &p, "+a/b", 0);
  ok("build-up: a/b after it", me_build_up(e, &p, 0) && count_kind(e->line[0], MK_FRAC) == 1 && lin_is(e, "x^2+ a/b"));
  me_insert_text(e, &p, "+(c", 0);
  ok("build-up: waits for an open bracket", !me_build_up(e, &p, 0));
  me_insert_text(e, &p, "+\\alpha", 0);
  ok("AutoCorrect: \\alpha", me_autocorrect(e, &p) && lin_is(e, "x^2+ a/b +(c+\xce\xb1"));
  math_free(e);
  e = math_new(0);
  mp_start(e, &p);
  me_insert_text(e, &p, "\\frac{1}{2}", 0);
  ok("build-up: LaTeX", me_build_up(e, &p, 1) && count_kind(e->line[0], MK_FRAC) == 1);
  math_free(e);

  /* normal text */
  e = math_new(0);
  mp_start(e, &p);
  me_insert_text(e, &p, "x", 0);
  me_insert_text(e, &p, "if", 1);
  row = e->line[0];
  ok("normal text: its own run", row->nkid == 2 && row->kid[1]->pv[P_R_NOR] == MB_ON && me_is_normal(e, &p));
  math_free(e);

  /* structure commands */
  e = math_from_linear("\xe2\x96\xa0(a&b@c&d)", 1);      /* ■(a&b@c&d) */
  mp_start(e, &p);
  me_right(e, &p);
  ok("matrix: the caret in its first cell", p.depth == 1 && p.arg[0] == 0);
  ok("matrix: insert row below", me_apply(e, &p, MA_ROW_BELOW, -1) && e->line[0]->kid[0]->rows == 3 && e->line[0]->kid[0]->nkid == 6 && p.arg[0] == 2);
  ok("matrix: insert column after", me_apply(e, &p, MA_COL_AFTER, -1) && e->line[0]->kid[0]->cols == 3 && e->line[0]->kid[0]->nkid == 9);
  ok("matrix: written", lin_is(e, "\xe2\x96\xa0(a&&b@&&@c&&d)"));
  ok("matrix: delete column and row", me_apply(e, &p, MA_COL_DELETE, -1) && me_apply(e, &p, MA_ROW_DELETE, -1) && lin_is(e, "\xe2\x96\xa0(a&b@c&d)"));
  {
    MAct acts[40];
    int n = me_actions(e, &p, acts, 40), i, has = 0;
    for (i = 0; i < n; i++) if (acts[i].act == MA_ROW_ABOVE) has = 1;
    ok("matrix: its commands offered", has && !strcmp(me_action_label(MA_REMOVE, e, &p, acts[0].depth), "Remove Matrix"));
  }
  math_free(e);
  e = math_from_linear("(a+b)", 0);
  mp_start(e, &p); me_right(e, &p);
  ok("brackets: changed to square ones", me_apply(e, &p, MA_DELIM_SQUARE, -1) && lin_is(e, "[a+b]"));
  ok("brackets: an argument added", me_apply(e, &p, MA_DELIM_ADD, -1) && e->line[0]->kid[0]->nkid == 2);
  math_free(e);
  e = math_from_linear("x_i^2", 0);
  mp_start(e, &p); me_right(e, &p);
  ok("scripts: remove subscript", me_apply(e, &p, MA_SCRIPT_NOSUB, -1) && lin_is(e, "x^2"));
  math_free(e);
  e = math_from_linear("a/b", 0);
  mp_start(e, &p); me_right(e, &p);
  ok("fraction: to skewed", me_apply(e, &p, MA_FRAC_SKEWED, -1) && lin_is(e, "a\xe2\x88\x95" "b"));
  math_free(e);
}

static void t_convert(void)
{
  MathEq *e = math_from_linear("x=(-b\xc2\xb1\\sqrt(b^2-4ac))/2a", 1);
  char *t;
  ok("convert: to linear form", me_to_linear_form(e, 0) && e->line[0]->nkid == 1 && e->line[0]->kid[0]->kind == MK_RUN && me_is_linear_form(e));
  ok("convert: back to professional", me_to_professional(e, 0) && count_kind(e->line[0], MK_FRAC) == 1 && count_kind(e->line[0], MK_RAD) == 1 && e->display);
  ok("convert: LaTeX linear form", me_to_linear_form(e, 1) && strstr(e->line[0]->kid[0]->text, "\\frac{") && strstr(e->line[0]->kid[0]->text, "\\sqrt"));
  ok("convert: LaTeX back to professional", me_to_professional(e, 1) && count_kind(e->line[0], MK_FRAC) == 1 && count_kind(e->line[0], MK_RAD) == 1);
  math_free(e);
  /* LaTeX in */
  e = math_from_latex("\\sum_{i=1}^{n} i^2 = \\frac{n(n+1)(2n+1)}{6}", 1);
  ok("latex: sum and fraction", e && count_kind(e->line[0], MK_NARY) == 1 && count_kind(e->line[0], MK_FRAC) == 1);
  math_free(e);
  e = math_from_latex("\\sqrt[3]{x} + \\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix} + \\hat{y} + \\mathbb{R} + \\text{if } \\alpha", 0);
  t = e ? lin(e) : NULL;
  ok("latex: root, matrix, accent, letters, text", e && count_kind(e->line[0], MK_RAD) == 1 && count_kind(e->line[0], MK_MATRIX) == 1 && count_kind(e->line[0], MK_ACC) == 1 &&
     t && strstr(t, "\xe2\x84\x9d") && strstr(t, "\"if \"") && strstr(t, "\xce\xb1"));
  free(t);
  math_free(e);
  e = math_from_latex("f(x)=\\left\\{\\begin{array}{ll} -x & x<0 \\\\ x & x\\ge 0\\end{array}\\right.", 0);
  ok("latex: \\left\\{ ... \\right. cases", e && count_kind(e->line[0], MK_DELIM) >= 2 && count_kind(e->line[0], MK_MATRIX) == 1);
  math_free(e);
  /* LaTeX out and back */
  {
    static const char *const forms[] = { "a/b", "x_i^2", "\\sqrt(3&x)", "\\sum_(i=1)^n i", "\\int_0^1 f(x) dx", "sin\\theta", "[a+b]", "\\hat(x)" };
    int i, good = 1;
    for (i = 0; i < 8; i++) {
      MathEq *a = math_from_linear(forms[i], 0), *b;
      char *x = latex(a), *la = lin(a), *lb;
      b = math_from_latex(x, 0);
      lb = b ? lin(b) : NULL;
      if (!lb || strcmp(la, lb)) { good = 0; printf("  latex: %s -> %s -> %s\n", la, x, lb ? lb : "(null)"); }
      free(x); free(la); free(lb); math_free(a); math_free(b);
    }
    ok("latex: written and read back the same", good);
  }
}

static void t_numbers(void)
{
  Doc *d = doc_new(), *r;
  Pos at;
  MathEq *e;
  unsigned lost = 0;
  int k1, k2;
  if (!d) return;
  at = doc_start(d);
  e = math_from_linear("E=mc^2", 1);
  ok("number: added", me_number(e, 1, 7) && math_numbered(e) == 2 && lin_is(e, "E=mc^2#(7)"));
  k1 = doc_insert_math(d, &at, e, NULL);
  doc_insert(d, &at, "\n", 1, NULL);
  k2 = doc_insert_math(d, &at, math_from_linear("a^2+b^2=c^2#(9)", 1), NULL);
  ok("number: # in linear format numbers the equation", k2 >= 0 && math_numbered(doc_math(d, k2)) == 2);
  ok("number: renumbered in document order", math_renumber(d) == 2 && lin_is(doc_math(d, k1), "E=mc^2#(1)") && lin_is(doc_math(d, k2), "a^2+b^2=c^2#(2)"));
  {
    /* laid out: as wide as the line, the number at its right edge */
    Layout *L = lay_new(d, &fs);
    if (L && lay_update(L)) {
      i32 room = lay_page(L, 0)->width - lay_page(L, 0)->left - lay_page(L, 0)->right;
      const MLay *ml = math_layout(&fs, d, k1, &d->styles[0].cf);
      MathEq *me = doc_math(d, k1);
      ok("number: the equation spans its line", ml && ml->w == room && me->room == room);
      ok("number: the line isn't shifted", L->pl[0].lines[0].left == 0);
    }
    lay_free(L);
  }
  r = docx_again(d, &lost);
  ok("number: kept through .docx (an equation array of one row with #)", r && r->nmaths == 2 && math_numbered(r->maths[0]) == 2 && math_numbered(r->maths[1]) == 2 && !lost);
  doc_free(r);
  e = math_clone(doc_math(d, k1));
  ok("number: taken away", me_number(e, 0, 0) && !math_numbered(e) && lin_is(e, "E=mc^2"));
  math_free(e);
  doc_free(d);
}

static void cnt(void *c, i32 x0, i32 t0, i32 w0, i32 h0) { (void)x0; (void)t0; if (w0 > 0 && h0 > 0) (*(int *)c)++; }
static void t_layout(void)
{
  Doc *d = doc_new();
  Pos at;
  int k;
  MathEq *e;
  const MLay *ml;
  MPos p;
  i32 x, top, h;
  if (!d) return;
  at = doc_start(d);
  k = doc_insert_math(d, &at, math_new(0), NULL);
  ml = math_layout(&fs, d, k, &d->styles[0].cf);
  ok("placeholder: an empty equation keeps room for its placeholder", ml && ml->w > 100 && ml->asc > 100);
  e = math_template_eq(&math_galleries[MG_FRACTION].s[0].t[0]);
  e->display = 0;
  k = doc_add_math(d, e);
  ml = math_layout(&fs, d, k, &d->styles[0].cf);
  {
    int n = 0;
    me_slots(e, cnt, &n);
    ok("placeholder: both arguments of an empty fraction shown", n == 2);
  }
  /* the caret: numerator above the baseline, denominator below */
  mp_start(e, &p); me_right(e, &p);
  me_caret_box(e, &p, &x, &top, &h);
  ok("caret: in the numerator, above the baseline", top + h < 0 && h > 50);
  me_vertical(e, &p, 0);
  me_caret_box(e, &p, &x, &top, &h);
  ok("caret: in the denominator, below it", top > -60);
  {
    MPos q;
    me_hit(e, x + 10, top + h / 2, &q);
    ok("hit: a click in the denominator", q.depth == 1 && q.arg[0] == 1);
  }
  /* characters' places in a run */
  e = math_from_linear("abc", 0);
  k = doc_add_math(d, e);
  math_layout(&fs, d, k, &d->styles[0].cf);
  ok("caret: each character's x", e->line[0]->kid[0]->ncx == 4 && e->line[0]->kid[0]->cx[1] > 0 && e->line[0]->kid[0]->cx[2] > e->line[0]->kid[0]->cx[1]);
  /* an edit is one undo step through doc_set_math */
  {
    MathEq *c = math_clone(doc_math(d, 0));
    MPos q;
    Pos ref = doc_start(d), cpos;
    mp_start(c, &q);
    me_insert_text(c, &q, "z", 0);
    ok("undo: the edited copy put back", doc_set_math(d, ref, c) >= 0 && lin_is(doc_math(d, math_index(d->p[0]->t)), "z"));
    doc_undo(d, &cpos);
    ok("undo: undone in one step", lin_is(doc_math(d, math_index(d->p[0]->t)), ""));
  }
  doc_free(d);
}

void test_wp14b(void)
{
  t_gallery();
  t_editing();
  t_convert();
  t_numbers();
  t_layout();
}
