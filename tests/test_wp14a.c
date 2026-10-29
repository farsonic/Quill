/* test_wp14a.c - the native tests of WP14a Equations: model, layout, .docx (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp14a() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/math.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/zip.h"

/* the equation's OMML as a string (malloc'd) */
static char *omml(const MathEq *e)
{
  Buf b;
  buf_init(&b);
  math_write_omml(&b, e, NULL, 0);
  return buf_take(&b);
}
static char *linear(const MathEq *e)
{
  Buf b;
  buf_init(&b);
  math_to_linear(e, &b);
  if (!b.data) buf_adds(&b, "");
  return buf_take(&b);
}
/* linear text in, linear text out the same */
static int lin_round(const char *s)
{
  MathEq *e = math_from_linear(s, 0);
  char *t = e ? linear(e) : NULL;
  int same = t && !strcmp(s, t);
  if (!same) printf("  linear: \"%s\" came back as \"%s\"\n", s, t ? t : "(null)");
  free(t); math_free(e);
  return same;
}
/* the first structure of an equation's first line */
static const MNode *first(const MathEq *e) { return e && e->nline && e->line[0]->nkid ? e->line[0]->kid[0] : NULL; }
static int count_kind(const MNode *n, int kind)
{
  int c = n->kind == kind;
  u16 i;
  for (i = 0; i < n->nkid; i++) c += count_kind(n->kid[i], kind);
  return c;
}
/* a document's main part out of a saved .docx (malloc'd) */
static char *doc_xml(const u8 *z, size_t n)
{
  ZipReader zr;
  size_t len;
  char *x = NULL;
  if (zip_open(&zr, z, n)) { x = (char *)zip_read(&zr, zip_find(&zr, "word/document.xml"), &len); zip_close(&zr); }
  return x;
}
static int count_str(const char *s, const char *what)
{
  int n = 0;
  const char *p = s;
  while (p && (p = strstr(p, what))) { n++; p += strlen(what); }
  return n;
}

static void t_linear(void)
{
  MathEq *e;
  const MNode *n;
  char *x;
  /* structures from linear format */
  e = math_from_linear("(a+b)/c", 0);
  n = first(e);
  ok("linear: (a+b)/c is a fraction", n && n->kind == MK_FRAC && n->nkid == 2);
  ok("linear: the numerator's brackets are dropped", n && n->kid[0]->nkid == 1 && n->kid[0]->kid[0]->kind == MK_RUN && !strcmp(n->kid[0]->kid[0]->text, "a+b"));
  math_free(e);
  e = math_from_linear("x_i^2", 0); n = first(e);
  ok("linear: x_i^2 is a sub-superscript", n && n->kind == MK_SSUBSUP);
  math_free(e);
  e = math_from_linear("\\sum_(i=1)^n i^2", 1); n = first(e);
  ok("linear: \\sum with limits is an n-ary", n && n->kind == MK_NARY && n->ch[0] == 0x2211 && n->pv[P_N_SUBHIDE] != MB_ON && e->display);
  ok("linear: the n-ary's operand", n && n->kid[2]->nkid == 1 && n->kid[2]->kid[0]->kind == MK_SSUP);
  math_free(e);
  e = math_from_linear("\\sqrt(3&x)", 0); n = first(e);
  ok("linear: a radical with a degree", n && n->kind == MK_RAD && n->kid[0]->nkid == 1 && n->pv[P_RAD_DEGHIDE] != MB_ON);
  math_free(e);
  e = math_from_linear("\\matrix(1&0@0&1)", 0); n = first(e);
  ok("linear: a 2x2 matrix", n && n->kind == MK_MATRIX && n->rows == 2 && n->cols == 2 && n->nkid == 4);
  math_free(e);
  e = math_from_linear("sin x + lim_(n\\to\\infty) a_n", 0);
  ok("linear: functions recognised", e && count_kind(e->line[0], MK_FUNC) == 2 && count_kind(e->line[0], MK_LIMLOW) == 1);
  math_free(e);
  e = math_from_linear("a\\hat + \\vec v + \\overbar(z) + \\underbrace(x+y) + \\box(q) + \\rect(r) + \\phantom(p)", 0);
  ok("linear: accents, bars, braces, boxes", e && count_kind(e->line[0], MK_ACC) == 2 && count_kind(e->line[0], MK_BAR) == 1 &&
     count_kind(e->line[0], MK_GROUPCHR) == 1 && count_kind(e->line[0], MK_BOX) == 1 && count_kind(e->line[0], MK_BORDERBOX) == 1 &&
     count_kind(e->line[0], MK_PHANT) == 1);
  math_free(e);
  e = math_from_linear("x\xC2\xB2 + [a] + {b} + |c| + \\langle d\\rangle + \\eqarray(x&=1@y&=2) + a\xE2\x8A\x98" "b + a\xE2\x88\x95" "b + a\xC2\xA6" "b", 0);
  ok("linear: brackets, an array and the other fractions", e && count_kind(e->line[0], MK_DELIM) == 4 && count_kind(e->line[0], MK_EQARR) == 1 &&
     count_kind(e->line[0], MK_FRAC) == 3 && count_kind(e->line[0], MK_SSUP) == 1);
  math_free(e);
  e = math_from_linear("\"if \"x>0", 0); n = first(e);
  ok("linear: quoted text is normal text", n && n->kind == MK_RUN && n->pv[P_R_NOR] == MB_ON && !strcmp(n->text, "if "));
  math_free(e);
  /* and back */
  ok("linear: a fraction round trip", lin_round("(a+b)/c"));
  ok("linear: scripts round trip", lin_round("x_i^2+y^(n+1)"));
  ok("linear: n-ary round trip", lin_round("\xE2\x88\x91_(i=1)^n\xE2\x96\x92i"));
  ok("linear: radical round trip", lin_round("\xE2\x88\x9A(3&x)"));
  ok("linear: matrix round trip", lin_round("(\xE2\x96\xA0(1&0@0&1))"));
  ok("linear: brackets round trip", lin_round("[a+b]"));
  e = math_from_linear("((", 0);
  ok("linear: unbalanced brackets don't fail", e != NULL);
  math_free(e);
  e = math_from_linear("^^__//\xE2\x88\x9A\xE2\x96\xA0(&@", 0);
  ok("linear: nonsense doesn't fail", e != NULL);
  x = e ? omml(e) : NULL;
  ok("linear: nonsense still writes", x && strstr(x, "<m:oMath"));
  free(x); math_free(e);
  ok("keywords: \\alpha is α", math_keyword("alpha", 5) == 0x3B1 && math_keyword("nosuch", 6) == 0);
}

/* OMML parsed straight from text, and written again */
static MathEq *parse(const char *xml)
{
  Xml x;
  MathEq *e = NULL;
  xml_init(&x, xml, strlen(xml));
  while (xml_next(&x) != XT_EOF) if (x.type == XT_START && (!strcmp(x.name, "oMath") || !strcmp(x.name, "oMathPara"))) { e = math_read_omml(&x); break; }
  xml_free(&x);
  return e;
}
static void t_model(void)
{
  static const char src[] = "<m:oMath><m:f><m:fPr><m:type m:val=\"lin\"/><m:futureProp m:val=\"7\"/><m:ctrlPr><w:rPr><w14:glow w14:rad=\"1\"/></w:rPr></m:ctrlPr></m:fPr>"
    "<m:num><m:argPr><m:argSz m:val=\"-1\"/></m:argPr><m:r><m:t>1</m:t></m:r></m:num><m:den><w:ins w:id=\"3\" w:author=\"A\"><m:r><m:t>2</m:t></m:r></w:ins></m:den></m:f>"
    "<m:r><m:rPr><m:sty m:val=\"b\"/></m:rPr><m:t xml:space=\"preserve\"> x </m:t><w:br/></m:r></m:oMath>";
  MathEq *e = parse(src), *c;
  char *a, *b;
  MNode *f, *s;
  ok("omml: parsed straight from text", e && e->nline == 1 && e->line[0]->nkid == 2);
  if (!e) return;
  f = e->line[0]->kid[0];
  ok("omml: properties and argument size", f->kind == MK_FRAC && f->pv[P_F_TYPE] == MF_LIN + 1 && f->kid[0]->hasargsz && f->kid[0]->argsz == -1);
  ok("omml: an unknown property kept", f->xpr && strstr(f->xpr, "futureProp"));
  ok("omml: a tracked insertion kept, its math shown", f->kid[1]->nkid == 1 && f->kid[1]->kid[0]->kind == MK_RAW && f->kid[1]->kid[0]->nkid == 1);
  ok("omml: a run's spaces and other content kept", !strcmp(e->line[0]->kid[1]->text, " x ") && e->line[0]->kid[1]->xpr && strstr(e->line[0]->kid[1]->xpr, "w:br"));
  a = omml(e);
  ok("omml: written back with everything", a && strstr(a, "<m:futureProp m:val=\"7\"/>") && strstr(a, "<w:ins w:id=\"3\"") && strstr(a, "xml:space=\"preserve\"> x <") &&
     strstr(a, "<m:argSz m:val=\"-1\"/>") && strstr(a, "<m:sty m:val=\"b\"/>"));
  ok("omml: a namespace a kept piece uses is declared", a && strstr(a, "xmlns:w14="));
  c = math_clone(e);
  b = c ? omml(c) : NULL;
  ok("model: a clone writes the same", a && b && !strcmp(a, b));
  free(b);
  /* an editor's operations: a structure made, filled, put in, taken out */
  s = mn_struct(MK_RAD, 0, 0);
  ok("model: a radical has its two arguments", s && s->nkid == 2 && s->kid[0]->kind == MK_ROW && mn_nargs(MK_RAD) == 2 && !strcmp(mn_name(MK_RAD), "rad"));
  if (s && c) {
    mn_insert(s->kid[1], -1, mn_run("y", 1));
    ok("model: put in a row", mn_insert(c->line[0], 1, s) && c->line[0]->nkid == 3 && c->line[0]->kid[1] == s);
    mn_free(mn_remove(c->line[0], 1));
    b = omml(c);
    ok("model: taken out again", c->line[0]->nkid == 2 && b && !strcmp(a, b));
    free(b);
  }
  {
    MNode *m = mn_struct(MK_MATRIX, 2, 3);
    ok("model: a 2x3 matrix", m && m->rows == 2 && m->cols == 3 && m->nkid == 6);
    mn_free(m);
  }
  {
    MathEq *k = math_from_linear("\\sqrt(x)+\\alpha", 0);
    Buf o, kw;
    MathEq *back;
    buf_init(&o); buf_init(&kw);
    if (k) math_to_linear(k, &o);
    if (o.data) math_keywords(o.data, &kw);
    back = kw.data ? math_from_linear(kw.data, 0) : NULL;
    b = back ? omml(back) : NULL;
    {
      char *x1 = k ? omml(k) : NULL;
      ok("keywords: control words for the Latin-1 gadget read back the same", kw.data && !strchr(kw.data, (char)0xE2) && x1 && b && !strcmp(x1, b));
      free(x1);
    }
    free(b); math_free(back); math_free(k); buf_free(&o); buf_free(&kw);
  }
  free(a);
  math_free(c);
  math_free(e);
}

static void t_docx(void)
{
  size_t n = 0, n2 = 0, n3 = 0;
  u8 *data = file_read("tests/data/equations.docx", &n), *s1 = NULL, *s2 = NULL;
  unsigned lost = 0;
  char err[128];
  Doc *d = data ? docx_load(data, n, &lost, err, sizeof err) : NULL, *d2 = NULL, *d3 = NULL;
  int i, kinds[MK_N], all_kinds = 1, same = 1;
  char *x1 = NULL, *x2 = NULL;
  ok("docx: Word-style OMML sample loads", d != NULL);
  if (!d) { free(data); return; }
  ok("docx: every equation read", d->nmaths == 22);
  memset(kinds, 0, sizeof kinds);
  for (i = 0; i < d->nmaths; i++) { int k; for (k = 0; k < MK_N; k++) kinds[k] += count_kind(d->maths[i]->line[0], k); }
  for (i = MK_ACC; i < MK_RAW; i++) if (!kinds[i]) { all_kinds = 0; printf("  docx: no %s read\n", mn_name(i)); }
  ok("docx: every OMML construct read", all_kinds);
  ok("docx: display equations from m:oMathPara", d->maths[1]->display && !d->maths[0]->display && d->maths[1]->jc == MPJC_CENTERGROUP + 1);
  ok("docx: two equations in one m:oMathPara", d->maths[20]->nline == 2);
  ok("docx: a manual break in a display equation", count_kind(d->maths[21]->line[0], MK_RUN) >= 5);
  ok("docx: m:mathPr kept", d->mathpr && strstr(d->mathpr, "intLim"));
  {
    MathPr mp;
    math_settings(d, &mp);
    ok("docx: math settings read", mp.intsub == 1 && mp.narysub == 0 && mp.defjc == MPJC_CENTERGROUP + 1);
  }
  ok("docx: properties read (skewed fraction, ∑, limLoc)",
     d->maths[0]->line[0]->kid[2]->pv[P_F_TYPE] == MF_SKW + 1 && d->maths[6]->line[0]->kid[0]->ch[0] == 0x2211 &&
     d->maths[6]->line[0]->kid[0]->pv[P_N_LIMLOC] == MLIM_UNDOVR + 1);
  ok("docx: control properties kept verbatim", d->maths[0]->line[0]->kid[0]->ctrl && strstr(d->maths[0]->line[0]->kid[0]->ctrl, "Cambria Math"));
  ok("docx: run properties read (size, colour, style)", count_kind(d->maths[18]->line[0], MK_RUN) > 3);
  /* saved and read back: the same equations, the same OMML */
  s1 = docx_save(d, &n2);
  d2 = s1 ? docx_load(s1, n2, &lost, err, sizeof err) : NULL;
  ok("docx: equations saved and read back", d2 && d2->nmaths == d->nmaths && d2->mathpr != NULL);
  for (i = 0; d2 && i < d->nmaths && i < d2->nmaths; i++) {
    char *a = omml(d->maths[i]), *b = omml(d2->maths[i]);
    if (!a || !b || strcmp(a, b)) { same = 0; printf("  docx: equation %d differs after saving:\n  %s\n  %s\n", i, a ? a : "-", b ? b : "-"); }
    free(a); free(b);
  }
  ok("docx: each equation's OMML the same after a round trip", same && d2);
  s2 = d2 ? docx_save(d2, &n3) : NULL;
  x1 = s1 ? doc_xml(s1, n2) : NULL; x2 = s2 ? doc_xml(s2, n3) : NULL;
  ok("docx: document.xml stable over two saves", x1 && x2 && !strcmp(x1, x2));
  ok("docx: m namespace declared on each equation", x1 && count_str(x1, "<m:oMath xmlns:m=") + count_str(x1, "<m:oMathPara xmlns:m=") == 22);
  {
    char *ta = all(d), *tb = d2 ? all(d2) : NULL;
    ok("docx: text around equations kept", d2 && d2->np == d->np && ta && tb && !strcmp(ta, tb));
    free(ta); free(tb);
  }
  free(x1); free(x2);
  /* a new equation saved, read again */
  d3 = doc_new();
  if (d3) {
    Pos at = doc_start(d3);
    MathEq *e = math_from_linear("E=mc^2", 0);
    char *a, *b;
    doc_insert(d3, &at, "Energy: ", 8, NULL);
    ok("insert: an equation goes in", doc_insert_math(d3, &at, e, NULL) == 0 && d3->p[0]->len == 12);
    ok("insert: undo takes it out", doc_undo(d3, &at) && d3->p[0]->len == 8);
    ok("insert: redo puts it back", doc_redo(d3, &at) && d3->p[0]->len == 12);
    {
      Doc *d4 = docx_again(d3, &lost);
      a = omml(d3->maths[0]); b = d4 && d4->nmaths ? omml(d4->maths[0]) : NULL;
      ok("insert: a new equation round trips", a && b && !strcmp(a, b) && strstr(b, "Cambria Math"));
      free(a); free(b);
      doc_free(d4);
    }
    {
      /* changed: a new entry in its place, one undo step */
      Pos p = P(0, 8);
      MathEq *e2 = math_from_linear("a/b", 0);
      int k = doc_set_math(d3, p, e2);
      ok("set: an equation replaced", k == 1 && math_index(d3->p[0]->t + 8) == 1 && d3->p[0]->len == 12);
      ok("set: undo brings the old one back", doc_undo(d3, &p) && math_index(d3->p[0]->t + 8) == 0);
    }
    {
      /* copied into another document: the copy has its own entry */
      Fragment *f = doc_fragment(d3, P(0, 0), P(0, 12));
      Doc *o = doc_new();
      Pos q = o ? doc_start(o) : P(0, 0);
      ok("paste: an equation pasted into another document", f && o && doc_paste(o, &q, f, PASTE_KEEP) && o->nmaths == 1 &&
         o->p[0]->len == 12 && IS_MATH(o->p[0]->t + 8) && math_index(o->p[0]->t + 8) == 0);
      fragment_free(f); doc_free(o);
    }
    doc_free(d3);
  }
  free(s1); free(s2);
  doc_free(d2);
  doc_free(d);
  free(data);
}

typedef struct { int n, w; } SpanAcc;
static void test_span(void *ctx, int y, int x0, int x1)
{
  SpanAcc *a = (SpanAcc *)ctx;
  (void)y;
  a->n++; a->w += x1 - x0 + 1;
}
static void t_layout(void)
{
  size_t n = 0, plen = 0;
  u8 *data = file_read("tests/data/equations.docx", &n), *pdf;
  unsigned lost = 0;
  char err[128];
  Doc *d = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  Layout *L;
  int i, sane = 1, polys = 0, glyphs = 0;
  if (!d) { free(data); return; }
  L = lay_new(d, &fs);
  ok("layout: the equations document lays out", L && lay_update(L) && L->npages >= 1);
  for (i = 0; i < d->nmaths; i++) {
    const MLay *ml = math_layout(&fs, d, i, &d->styles[0].cf);
    u32 k;
    if (!ml || ml->oom || ml->w <= 0 || ml->asc <= 0 || ml->n == 0) { sane = 0; printf("  layout: equation %d: %ld wide, %lu items\n", i, ml ? (long)ml->w : -1L, ml ? (unsigned long)ml->n : 0UL); }
    for (k = 0; ml && k < ml->n; k++) { if (ml->it[k].kind == MI_POLY) polys++; if (ml->it[k].kind == MI_GLYPH) glyphs++; }
  }
  ok("layout: every equation has a size and something to draw", sane);
  ok("layout: glyphs and drawn shapes", glyphs > 100 && polys > 10);
  {
    /* a stacked fraction is taller than its text; a display one taller than an inline one */
    const MLay *in = math_layout(&fs, d, 0, &d->styles[0].cf);
    i32 inh = in->asc + in->desc;
    const MLay *dp = math_layout(&fs, d, 1, &d->styles[0].cf);
    i32 dph = dp->asc + dp->desc;
    ok("layout: a fraction is taller than its text's em", inh > d->styles[0].cf.size * 10);
    ok("layout: a display fraction is taller than an inline one", dph > inh);
  }
  {
    /* the line holding a display equation: the equation centred on it */
    u32 pi;
    int found = 0;
    for (pi = 0; pi < d->np && !found; pi++) {
      Para *p = d->p[pi];
      if (p->len == 4 && IS_MATH(p->t) && d->maths[math_index(p->t)]->display) {
        ParaLay *pl = &L->pl[pi];
        i32 room = lay_page(L, 0)->width - lay_page(L, 0)->left - lay_page(L, 0)->right;
        found = 1;
        ok("layout: a display equation is centred", pl->nlines == 1 && pl->lines[0].left > 0 &&
           abs(pl->lines[0].left * 2 + pl->lines[0].width - room) < 40);
      }
    }
    ok("layout: found a display equation's paragraph", found);
  }
  {
    /* the size follows the text's */
    CharFmt big = d->styles[0].cf;
    const MLay *a = math_layout(&fs, d, 2, &d->styles[0].cf);
    i32 w1 = a->w;
    big.size = (u16)(big.size * 2);
    a = math_layout(&fs, d, 2, &big);
    ok("layout: twice the text size, twice as wide", a->w > w1 * 18 / 10 && a->w < w1 * 22 / 10);
  }
  {
    /* rows know where they are (an editor's caret) */
    const MNode *f = d->maths[0]->line[0]->kid[0];
    math_layout(&fs, d, 0, &d->styles[0].cf);
    ok("layout: arguments placed (numerator above, denominator below)", f->kid[0]->y < 0 && f->kid[1]->y > 0 && f->kid[0]->w > 0);
  }
  {
    /* glyphs Quill's fonts lack: drawn */
    MathEq *e = math_from_linear("x\\in A\\subseteq B\\Rightarrow \\forall y\\exists z\\nabla", 0);
    Doc *t = doc_new();
    int k = t && e ? doc_add_math(t, e) : -1;
    const MLay *ml = k >= 0 ? math_layout(&fs, t, k, &t->styles[0].cf) : NULL;
    u32 j;
    int pp = 0, gg = 0, flipped = 0;
    for (j = 0; ml && j < ml->n; j++) { if (ml->it[j].kind == MI_POLY) pp++; if (ml->it[j].kind == MI_GLYPH) { gg++; if (ml->it[j].flags) flipped++; } }
    ok("fallback: missing symbols drawn as outlines or stand-in glyphs", ml && pp >= 6 && flipped >= 2 && gg >= 6);
    doc_free(t);
  }
  {
    /* filling: a square's spans */
    static const i32 sq[16] = { 0, 0, 160, 0, 160, 0, 160, 160, 160, 160, 0, 160, 0, 160, 0, 0 };
    SpanAcc acc;
    acc.n = 0; acc.w = 0;
    math_fill(sq, 4, 0, 12, test_span, &acc);
    ok("fill: a 10 pixel square fills 10 rows of 10", acc.n == 10 && acc.w == 100);
  }
  pdf = L ? pdf_export(L, &plen) : NULL;
  ok("pdf: the equations export", pdf && plen > 1000);
  free(pdf);
  lay_free(L);
  doc_free(d);
  free(data);
  {
    /* a document with equations made here: open in LibreOffice by hand (see the report) */
    Doc *o = doc_new();
    if (o) {
      Pos at = doc_start(o);
      static const char *const eqs[] = { "x=(-b\xC2\xB1\\sqrt(b^2-4ac))/2a", "\\int_0^\\infty e^(-x^2) dx=\\sqrt\\pi/2",
                                         "\\matrix(a&b@c&d)", "f(x)={\\eqarray(1&x>0@0&x\\le 0)}", "\\sum_(k=0)^n \\binom" };
      size_t len;
      u8 *z;
      int j;
      doc_insert(o, &at, "Inline: ", 8, NULL);
      doc_insert_math(o, &at, math_from_linear("a^2+b^2=c^2", 0), NULL);
      for (j = 0; j < 4; j++) { doc_insert(o, &at, "\n", 1, NULL); doc_insert_math(o, &at, math_from_linear(eqs[j], 1), NULL); }
      z = docx_save(o, &len);
      if (z) file_write("/tmp/quill_wp14a_new.docx", z, len);
      free(z);
      doc_free(o);
    }
  }
}

void test_wp14a(void)
{
  t_linear();
  t_model();
  t_docx();
  t_layout();
  math_fonts_free();
}
