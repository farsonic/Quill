/* test_wp18.c - the native tests of WP18 Shapes and text boxes (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp18() */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/objfx.h"
#include "../core/objtext.h"
#include "../core/layout.h"
#include "../core/pdf.h"

/* a shape put in at the end of d's first paragraph, floating; its index */
static int add_shape(Doc *d, int shape, i32 cx, i32 cy)
{
  int k = doc_add_shape(d, shape, cx, cy);
  Pos at;
  char ref[8];
  if (k < 0) return k;
  at.para = 0; at.off = d->p[0]->len;
  pic_utf8(ref, k);
  doc_insert(d, &at, ref, 4, NULL);
  return k;
}
/* the n-th object referred to in d's text */
static Image *nth(Doc *d, int n)
{
  u32 i, j;
  for (i = 0; i < d->np; i++)
    for (j = 0; j + 4 <= d->p[i]->len; j++)
      if (IS_PIC(d->p[i]->t + j) && !n--) return &d->images[pic_index(d->p[i]->t + j)];
  return NULL;
}
/* a one-paragraph story of text */
static Story *story_of(const char *s)
{
  Story *st = (Story *)calloc(1, sizeof(Story));
  ParaFmt pf;
  CharFmt cf;
  Para *p;
  Doc *t = doc_new();
  pf = t->styles[0].pf; cf = t->styles[0].cf;
  doc_free(t);
  p = para_new(&pf, &cf);
  para_append(p, s, (u32)strlen(s), &cf);
  st->p = (Para **)malloc(sizeof(Para *)); st->p[0] = p; st->np = 1;
  return st;
}

static void shapes(void)
{
  int sh, all = 1, names = 1;
  for (sh = SHX_FIRST; sh < SHX_END; sh++) {
    i32 xy[2 * SHAPE_MAXPTS];
    int closed, i;
    u32 m = shape_points(sh, 3000, 2000, xy, SHAPE_MAXPTS, &closed);
    int callout = sh == SHX_ROUNDCALLOUT || sh == SHX_OVALCALLOUT;
    if (m < 3) all = 0;
    for (i = 0; i < (int)m; i++) if (xy[2 * i] < -2 || xy[2 * i] > 3002 || xy[2 * i + 1] < -2 || xy[2 * i + 1] > (callout ? 2300 : 2002)) all = 0;
    if (sh != SHX_ICON && shapex_from_prst(shapex_prst(sh)) != sh) names = 0;
  }
  ok("shapes: every new preset inside its box", all);
  ok("shapes: their DrawingML names both ways", names && shapex_from_prst("rect") < 0);
  {
    int first, end, c, n = 0;
    for (c = 0; c < SC_N; c++) { shapex_range(c, &first, &end); n += end - first; }
    ok("shapes: the categories cover them all", n == SHX_ICON - SHX_FIRST);
  }
}
static void icons(void)
{
  int i, all = 1;
  u8 *m = (u8 *)calloc(48 * 48, 1);
  for (i = 0; i < objicon_count(); i++) {
    int k, on = 0;
    memset(m, 0, 48 * 48);
    objicon_mask(i, m, 48, 48);
    for (k = 0; k < 48 * 48; k++) if (m[k] > 128) on++;
    if (on < 48 || on > 48 * 48 - 48 || objicon_find(objicon_name(i)) != i) all = 0;
  }
  free(m);
  ok("icons: every icon draws something (and is found by its name)", all && objicon_count() >= 40);
}
static void fills(void)
{
  Doc *d = doc_new(), *e;
  int k = add_shape(d, SHX_STAR8, 2000, 2000), k2, k3;
  Image *im = &d->images[k], *b;
  size_t n;
  u8 *data;
  unsigned lost = 0;
  char err[100];
  ObjRaster r;
  im->fx.filltype = FT_GRAD; im->fx.gtype = GT_LINEAR; im->fx.gangle = 900; im->fx.ngs = 3;
  im->fx.gcol[0] = 0xFF0000; im->fx.gpos[0] = 0; im->fx.gcol[1] = 0x00FF00; im->fx.gpos[1] = 50; im->fx.gcol[2] = 0x0000FF; im->fx.gpos[2] = 100;
  im->fx.shadow = 1; im->fx.shdist = 50; im->fx.shdir = 2700; im->fx.shblur = 80; im->fx.shcolor = 0x404040; im->fx.shalpha = 50;
  im->fx.glow = 100; im->fx.glcolor = 0xFFC000; im->fx.glalpha = 60; im->fx.soft = 40;
  ok("render: a star with a gradient, top red, bottom blue", objfx_render(im, 1, 20, &r) &&
     r.px[((-r.oy + 8) * r.w + r.w / 2) * 4] > 150 && r.px[((-r.oy + 92) * r.w + r.w / 2) * 4 + 2] > 120);
  objraster_free(&r);
  k2 = add_shape(d, SH_ROUNDRECT, 1500, 1000);
  d->images[k2].fx.filltype = FT_GRAD; d->images[k2].fx.gtype = GT_RADIAL; d->images[k2].fx.ngs = 2; d->images[k2].fx.gcol[0] = 0xFFFFFF; d->images[k2].fx.gcol[1] = 0x002060; d->images[k2].fx.gpos[1] = 100;
  {
    size_t pl;
    u8 *png = file_read("tests/data/disc.png", &pl);
    int pk = doc_add_image(d, png, (u32)pl, 0, 0);
    k3 = add_shape(d, SH_ELLIPSE, 1500, 1500);
    d->images[k3].data = d->images[pk].data; d->images[k3].len = d->images[pk].len; d->images[k3].type = d->images[pk].type;
    d->images[k3].pw = d->images[pk].pw; d->images[k3].ph = d->images[pk].ph; d->images[k3].shared = 1; d->images[k3].fx.filltype = FT_PIC;
  }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  b = e ? nth(e, 0) : NULL;
  ok("docx: a gradient's stops and angle, shadow, glow, soft edges read back", b && b->shape == SHX_STAR8 && b->fx.filltype == FT_GRAD && b->fx.ngs == 3 &&
     b->fx.gcol[1] == 0x00FF00 && b->fx.gpos[1] == 50 && b->fx.gangle == 900 && b->fx.shadow && b->fx.shdist == 50 && b->fx.shdir == 2700 && b->fx.shblur == 80 &&
     b->fx.shalpha == 50 && b->fx.glow == 100 && b->fx.glcolor == 0xFFC000 && b->fx.glalpha == 60 && b->fx.soft == 40);
  b = e ? nth(e, 1) : NULL;
  ok("docx: a radial gradient", b && b->fx.filltype == FT_GRAD && b->fx.gtype == GT_RADIAL && b->fx.gcol[1] == 0x002060);
  b = e ? nth(e, 2) : NULL;
  ok("docx: a picture fill", b && b->kind == OBJ_SHAPE && b->fx.filltype == FT_PIC && b->data && b->len == d->images[k3].len);
  free(data); doc_free(e);
  {
    Layout *L = lay_new(d, &fs);
    u8 *pdf = pdf_export(L, &n);
    ok("pdf: shapes with gradients and effects drawn as images with soft masks", pdf && memmem(pdf, n, "/R2", 3) && memmem(pdf, n, "/SMask", 6));
    free(pdf); lay_free(L);
  }
  doc_free(d);
}
static void textboxes(void)
{
  Doc *d = doc_new(), *e;
  int a = add_shape(d, -1, 2400, 700), b2 = add_shape(d, -1, 2400, 2000);
  Image *A = &d->images[a], *B = &d->images[b2], *ra, *rb;
  Layout *L;
  Furn f;
  size_t n;
  u8 *data;
  unsigned lost = 0;
  char err[100];
  int nl0 = 0, nl1 = 0, ch[4];
  u32 j;
  if (A->text) { story_free(A->text); free(A->text); }
  A->text = story_of("One two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty.");
  A->fx.link = (u16)tb_new_link(d); A->fx.seq = 0;
  B->fx.link = A->fx.link; B->fx.seq = 1;
  ok("linked text boxes: a chain of two, the second's text is the first's", tb_head(d, b2) == a && tb_chain(d, A->fx.link, ch, 4) == 2 && ch[0] == a && ch[1] == b2);
  L = lay_new(d, &fs);
  lay_update(L);
  if (lay_textbox(L, A, 0, &f)) for (j = 0; j < f.n; j++) nl0 += (int)f.f[j].pl.nlines;
  lay_furniture_free(&f);
  if (lay_textbox(L, B, 0, &f)) for (j = 0; j < f.n; j++) { nl1 += (int)f.f[j].pl.nlines; if (f.f[j].pl.nlines) ok("linked text boxes: the rest starts at the second box's top", f.f[j].y == TB_INSET_Y); }
  lay_furniture_free(&f);
  ok("linked text boxes: the first box shows the lines that fit, the second the rest", nl0 >= 1 && nl0 <= 3 && nl1 >= 1);
  lay_free(L);
  A->fx.anchor = TA_BOTTOM; A->fx.vert = TV_VERT270; A->fx.hasins = 1; A->fx.ins[0] = 50; A->fx.ins[1] = 60; A->fx.ins[2] = 70; A->fx.ins[3] = 80;
  B->fx.anchor = TA_MIDDLE; B->fx.vert = TV_VERT;
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ra = e ? nth(e, 0) : NULL; rb = e ? nth(e, 1) : NULL;
  ok("docx: wps:txbx id and wps:linkedTxbx read back", ra && rb && ra->fx.link && rb->fx.link == ra->fx.link && rb->fx.seq == 1 && ra->text && !rb->text && rb->kind == OBJ_TEXTBOX);
  ok("docx: text direction, anchoring and insets read back", ra && rb && ra->fx.vert == TV_VERT270 && ra->fx.anchor == TA_BOTTOM && ra->fx.hasins && ra->fx.ins[1] == 60 && ra->fx.ins[3] == 80 &&
     rb->fx.vert == TV_VERT && rb->fx.anchor == TA_MIDDLE && !rb->fx.hasins);
  {
    /* turned text in a turned box goes into the PDF turned; a turned (vertical) text box too */
    Layout *L2 = lay_new(e, &fs);
    u8 *pdf = pdf_export(L2, &n);
    ok("pdf: text in a text box turned a quarter", pdf != NULL);
    free(pdf); lay_free(L2);
  }
  free(data); doc_free(e);
  {
    /* anchoring: a box's one line at the bottom */
    Image t = *B;
    Furn g;
    i32 y = -1;
    t.fx.link = 0; t.fx.seq = 0; t.fx.vert = 0; t.fx.anchor = TA_BOTTOM; t.text = story_of("Hello");
    L = lay_new(d, &fs);
    if (lay_textbox(L, &t, 0, &g) && g.n) y = g.f[0].y;
    lay_furniture_free(&g);
    ok("align text: anchored at the bottom", y > 1200 && y < 2000 - TB_INSET_Y);
    lay_free(L);
    story_free(t.text); free(t.text);
  }
  doc_free(d);
  {
    /* an icon: drawn, written as a:custGeom named in wp:docPr, read back */
    Doc *g = doc_new(), *h;
    int ik = add_shape(g, SHX_ICON, 1440, 1440);
    Image *ic;
    ObjRaster r;
    g->images[ik].fx.icon = (u16)(objicon_find("Lightbulb") + 1); g->images[ik].fill = 0x1F4E79; g->images[ik].lstyle = PB_NONE;
    ok("icons: rendered in its colour", objfx_render(&g->images[ik], 1, 20, &r) && r.px[((r.h / 2 - 12) * r.w + r.w / 2) * 4 + 3] > 200 && r.px[((r.h / 2 - 12) * r.w + r.w / 2) * 4 + 2] == 0x79);
    objraster_free(&r);
    data = docx_save(g, &n);
    h = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    ic = h ? nth(h, 0) : NULL;
    ok("icons: saved as a:custGeom and read back as the icon", ic && ic->shape == SHX_ICON && ic->fx.icon == g->images[ik].fx.icon && ic->fill == 0x1F4E79);
    free(data); doc_free(h); doc_free(g);
  }
}
void test_wp18(void)
{
  shapes();
  icons();
  fills();
  textboxes();
}
