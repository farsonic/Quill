/* test_wp20.c - the native tests of WP20 Draw tab and drawing canvas (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp20() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/ink.h"
#include "../core/zip.h"
#include "../core/keep.h"
#include "../core/layout.h"
#include "../core/pdf.h"

/* ---- strokes made up for the recogniser (twips) */
static u32 seg(i32 *p, u32 n, i32 ax, i32 ay, i32 bx, i32 by, int steps, int wobble)
{
  int i;
  for (i = 0; i < steps; i++) {
    p[2 * n] = ax + (bx - ax) * i / steps + (wobble && i ? ((i * 7) % 5 - 2) * wobble : 0);
    p[2 * n + 1] = ay + (by - ay) * i / steps + (wobble && i ? ((i * 3) % 5 - 2) * wobble : 0);
    n++;
  }
  return n;
}
static u32 poly(i32 *p, const i32 *c, int k, int steps, int wobble)
{
  u32 n = 0;
  int i;
  for (i = 0; i < k; i++) n = seg(p, n, c[2 * i], c[2 * i + 1], c[2 * ((i + 1) % k)], c[2 * ((i + 1) % k) + 1], steps, wobble);
  p[2 * n] = c[0]; p[2 * n + 1] = c[1];
  return n + 1;
}
static void recogniser(void)
{
  static i32 p[2 * 2000];
  InkShape r;
  u32 n, i;
  /* a line, a little wobbly, going up to the right */
  n = seg(p, 0, 1000, 3000, 4000, 1000, 40, 4);
  p[2 * n] = 4000; p[2 * n + 1] = 1000; n++;
  ok("ink2shape: a line", ink_recognise(p, n, &r) && r.shape == SH_LINE && r.flip == 2 && r.w > 2900 && r.h > 1900);
  /* a level line snaps level */
  n = seg(p, 0, 1000, 2000, 5000, 2150, 40, 3);
  p[2 * n] = 5000; p[2 * n + 1] = 2150; n++;
  ok("ink2shape: a level line", ink_recognise(p, n, &r) && r.shape == SH_LINE && r.h == 0);
  /* an arrow: shaft left to right, then a barb up and back, a barb down */
  n = seg(p, 0, 1000, 2000, 5000, 2000, 40, 2);
  n = seg(p, n, 5000, 2000, 4400, 1500, 8, 0);
  n = seg(p, n, 4400, 1500, 5000, 2000, 8, 0);
  n = seg(p, n, 5000, 2000, 4400, 2500, 8, 0);
  p[2 * n] = 4400; p[2 * n + 1] = 2500; n++;
  ok("ink2shape: an arrow", ink_recognise(p, n, &r) && r.shape == SH_ARROW && r.flip == 0 && r.w == 4000);
  /* the same drawn head first and pointing left */
  for (i = 0; i < n / 2; i++) { i32 t0 = p[2 * i], t1 = p[2 * i + 1]; p[2 * i] = p[2 * (n - 1 - i)]; p[2 * i + 1] = p[2 * (n - 1 - i) + 1]; p[2 * (n - 1 - i)] = t0; p[2 * (n - 1 - i) + 1] = t1; }
  for (i = 0; i < n; i++) p[2 * i] = 6000 - p[2 * i];
  ok("ink2shape: an arrow drawn head first, pointing left", ink_recognise(p, n, &r) && r.shape == SH_ARROW && (r.flip & 1));
  {
    static const i32 rect[] = { 1000, 1000, 5000, 1000, 5000, 3000, 1000, 3000 };
    static const i32 tri[] = { 3000, 1000, 5000, 4000, 1000, 4000 };
    static const i32 rt[] = { 1000, 1000, 1000, 4000, 4000, 4000 };
    static const i32 dia[] = { 3000, 1000, 5000, 2500, 3000, 4000, 1000, 2500 };
    static const i32 pent[] = { 3000, 1000, 5000, 2400, 4200, 4500, 1800, 4500, 1000, 2400 };
    static const i32 skew[] = { 1000, 1000, 5000, 1800, 2000, 4000 };
    n = poly(p, rect, 4, 20, 5);
    ok("ink2shape: a rectangle", ink_recognise(p, n, &r) && r.shape == SH_RECT && r.w > 3800 && r.h > 1800);
    n = poly(p, tri, 3, 20, 5);
    ok("ink2shape: a triangle", ink_recognise(p, n, &r) && r.shape == SH_TRIANGLE && r.flip == 0);
    n = poly(p, rt, 3, 20, 5);
    ok("ink2shape: a right triangle", ink_recognise(p, n, &r) && r.shape == SH_RTRIANGLE && r.flip == 0);
    n = poly(p, dia, 4, 20, 5);
    ok("ink2shape: a diamond", ink_recognise(p, n, &r) && r.shape == SH_DIAMOND);
    n = poly(p, pent, 5, 20, 5);
    ok("ink2shape: a pentagon is a freeform polygon", ink_recognise(p, n, &r) && r.shape == SH_FREEFORM && r.npoly == 5);
    n = poly(p, skew, 3, 20, 5);
    ok("ink2shape: a lopsided triangle is a freeform", ink_recognise(p, n, &r) && r.shape == SH_FREEFORM && r.npoly == 3);
  }
  /* an ellipse, nearly a circle: a circle */
  for (n = 0; n < 72; n++) { p[2 * n] = 3000 + (i32)((long)1500 * icos((int)n * 50) / TRIG_ONE) + (int)(n % 3) * 6; p[2 * n + 1] = 3000 + (i32)((long)1450 * isin((int)n * 50) / TRIG_ONE); }
  p[2 * n] = p[0]; p[2 * n + 1] = p[1]; n++;
  ok("ink2shape: a circle", ink_recognise(p, n, &r) && r.shape == SH_ELLIPSE && r.w == r.h);
  /* a scribble: nothing */
  for (n = 0; n < 60; n++) { p[2 * n] = 1000 + (i32)(n * 60); p[2 * n + 1] = 1000 + (n & 1 ? 800 : 0) + (i32)((n * 37) % 300); }
  ok("ink2shape: a scribble stays ink", !ink_recognise(p, n, &r));
  /* a tick: too small */
  n = seg(p, 0, 1000, 1000, 1060, 1050, 5, 0);
  ok("ink2shape: a tick stays ink", !ink_recognise(p, n, &r));
}
/* smoothing, making a stroke, hitting, rubbing out, the lasso */
static void strokes(void)
{
  static i32 p[2 * 400], q[2 * 400];
  u32 n, m;
  Image im, out;
  i32 x, y, dx, dy;
  int hit;
  Freeform *f;
  n = seg(p, 0, 1000, 1000, 5000, 1000, 200, 6);
  p[2 * n] = 5000; p[2 * n + 1] = 1000; n++;
  m = ink_smooth(p, n, q, 400, 8);
  ok("smooth: fewer points", m >= 2 && m < n);
  ok("smooth: the ends kept", q[0] == 1000 && q[1] == 1000 && q[2 * m - 2] == 5000 && q[2 * m - 1] == 1000);
  {
    int calm = 1;
    u32 i;
    for (i = 0; i < m; i++) if (q[2 * i + 1] < 990 || q[2 * i + 1] > 1010) calm = 0;
    ok("smooth: the jitter gone", calm);
  }
  { i32 dot[4]; ok("smooth: a dot is two points", ink_smooth(p, 1, dot, 2, 8) == 2); }
  ok("ink_make", ink_make(&im, INK_PEN, 0xC00000, 12, q, m, &x, &y) && im.ff && im.ff->tool == INK_PEN && im.shape == SH_FREEFORM &&
                 im.kind == OBJ_SHAPE && im.wrap == WRAP_FRONT && im.lwidth == 12 && im.lcolor == 0xC00000 && x < 1000 && x > 960 && im.cx > 4000);
  ok("ff_hit: on the stroke", ff_hit(&im, 1000 - x + 2000, 1000 - y + 5, 20));
  ok("ff_hit: off it", !ff_hit(&im, 1000 - x + 2000, 1000 - y + 300, 20));
  f = ff_erase(&im, 1000 - x + 2000, 1000 - y, 200, &hit);
  ok("ff_erase: split in two", hit && f && f->np == 2);
  ff_unref(f);
  f = ff_erase(&im, 1000 - x + 2000, 1000 - y + 2000, 200, &hit);
  ok("ff_erase: a miss leaves it whole", !hit && f && f->np == 1);
  ff_unref(f);
  {
    i32 loop[] = { 500, 500, 6000, 500, 6000, 1500, 500, 1500 }, small[] = { 500, 500, 2000, 500, 2000, 1500, 500, 1500 };
    ok("lasso: round the stroke", ink_in_lasso(&im, x, y, loop, 4));
    ok("lasso: round a bit of it", !ink_in_lasso(&im, x, y, small, 4));
  }
  {
    Image c;
    ok("copies share the paths", img_clone(&c, &im, 0) && c.ff == im.ff && im.ff->refs == 2);
    img_release(&c);
    ok("released: one left", im.ff->refs == 1);
  }
  /* Ink to Shape on the object: a line where it was drawn */
  ok("ink_to_shape: a line", ink_to_shape(&im, &out, &dx, &dy) && out.kind == OBJ_SHAPE && out.shape == SH_LINE && !out.ff && out.lcolor == 0xC00000 && out.lwidth == 12);
  img_release(&out);
  img_release(&im);
  /* a highlighter's stroke goes behind the text */
  ok("highlighter behind", ink_make(&im, INK_HIGHLIGHTER, 0xFFFF00, 96, q, m, &x, &y) && im.wrap == WRAP_BEHIND && im.ff->alpha == 50);
  img_release(&im);
}
/* a document with one paragraph holding object im (floating on the page) */
static Doc *doc_with(Image *im)
{
  Doc *d = doc_new();
  Pos at = { 0, 0 };
  char t[8];
  int k;
  if (!d) return NULL;
  doc_insert(d, &at, "Ink here.", 9, &d->p[0]->r[0].f);
  im->hrel = POS_PAGE; im->vrel = POS_PAGE; im->x = 2000; im->y = 3000;
  k = doc_add_object(d, im);
  pic_utf8(t, k);
  at.para = 0; at.off = 0;
  doc_insert(d, &at, t, 4, &d->p[0]->r[0].f);
  return d;
}
static const Image *first_obj(Doc *d)
{
  u32 i;
  for (i = 0; i < d->np; i++) { const char *t = d->p[i]->t; u32 k; for (k = 0; k + 4 <= d->p[i]->len; k++) if (IS_PIC(t + k)) return &d->images[pic_index(t + k)]; }
  return NULL;
}
extern Doc *wp9_mini_docx(const char *body, const char *styles);
extern char *wp9_saved_xml(Doc *d, const char *part);
static const char *const NS = "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
  "xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\" xmlns:wpc=\"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas\" "
  "xmlns:wpg=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
  "xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"";
static void docx_ink(void)
{
  static i32 p[2 * 200];
  Image im;
  i32 x, y;
  u32 n;
  Doc *d, *b;
  const Image *o;
  char *xml;
  unsigned lost = 0;
  n = seg(p, 0, 1000, 1000, 3000, 2000, 30, 0);
  ink_make(&im, INK_PENCIL, 0x404040, 8, p, n, &x, &y);
  im.ff->order = 7;
  d = doc_with(&im);
  img_release(&im);
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("ink saved: wps with custGeom", xml && strstr(xml, "<a:custGeom>") && strstr(xml, "name=\"Ink Pencil ") && strstr(xml, "#7\"") && strstr(xml, "cap=\"rnd\""));
  free(xml);
  b = docx_again(d, &lost);
  o = b ? first_obj(b) : NULL;
  ok("ink read back", o && o->kind == OBJ_SHAPE && o->shape == SH_FREEFORM && o->ff && o->ff->tool == INK_PENCIL && o->ff->order == 7 &&
                      o->ff->np == 1 && o->ff->p[0].n == n && o->lcolor == 0x404040 && o->lwidth == 8 && !lost);
  ok("ink read back: its place", o && o->x == 2000 && o->y == 3000 && o->hrel == POS_PAGE);
  ok("next order", b && ink_next_order(b) == 8);
  {
    /* drawn into a PDF */
    Layout *L = b ? lay_new(b, &fs) : NULL;
    size_t len = 0;
    u8 *pdf;
    if (L) lay_update(L);
    pdf = L ? pdf_export(L, &len) : NULL;
    ok("ink in a PDF", pdf != NULL && len > 100);
    free(pdf);
    if (L) lay_free(L);
  }
  if (b) doc_free(b);
  doc_free(d);
  /* Word's freeform: curves, two paths, its own space */
  {
    Buf body;
    buf_init(&body);
    buf_printf(&body, "<w:p><w:r><w:drawing><wp:anchor behindDoc=\"0\" relativeHeight=\"5\" %s><wp:simplePos x=\"0\" y=\"0\"/><wp:positionH relativeFrom=\"column\"><wp:posOffset>0</wp:posOffset></wp:positionH>"
               "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>0</wp:posOffset></wp:positionV><wp:extent cx=\"1270000\" cy=\"635000\"/><wp:wrapNone/><wp:docPr id=\"1\" name=\"Freeform 1\"/>"
               "<a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\"><wps:wsp><wps:cNvSpPr/><wps:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"1270000\" cy=\"635000\"/></a:xfrm>"
               "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"l\" t=\"t\" r=\"r\" b=\"b\"/><a:pathLst><a:path w=\"200\" h=\"100\"><a:moveTo><a:pt x=\"0\" y=\"100\"/></a:moveTo>"
               "<a:cubicBezTo><a:pt x=\"50\" y=\"0\"/><a:pt x=\"150\" y=\"0\"/><a:pt x=\"200\" y=\"100\"/></a:cubicBezTo><a:close/></a:path>"
               "<a:path w=\"20\" h=\"10\" fill=\"none\"><a:moveTo><a:pt x=\"0\" y=\"0\"/></a:moveTo><a:lnTo><a:pt x=\"w\" y=\"h\"/></a:lnTo></a:path></a:pathLst></a:custGeom>"
               "<a:solidFill><a:srgbClr val=\"FF0000\"/></a:solidFill><a:ln w=\"12700\"><a:solidFill><a:srgbClr val=\"0000FF\"/></a:solidFill></a:ln></wps:spPr><wps:bodyPr/></wps:wsp></a:graphicData></a:graphic>"
               "</wp:anchor></w:drawing></w:r></w:p>", NS);
    d = wp9_mini_docx(body.data, NULL);
    buf_free(&body);
    o = d ? first_obj(d) : NULL;
    ok("Word's freeform read", o && o->shape == SH_FREEFORM && o->ff && o->ff->tool == INK_NONE && o->ff->np == 2 && o->ff->p[0].closed && !o->ff->p[1].closed &&
                               o->ff->p[0].n == 9 && o->fill == 0xFF0000 && o->lcolor == 0x0000FF);
    if (o && o->ff) {
      i32 u, v;
      ff_point(o->ff, o->cx, o->cy, 1, 1, &u, &v);
      ok("Word's freeform: each path's own space", u == o->cx && v == o->cy);
    }
    xml = d ? wp9_saved_xml(d, "word/document.xml") : NULL;
    ok("Word's freeform written back", xml && strstr(xml, "<a:custGeom>") && strstr(xml, "<a:close/>") && strstr(xml, "FF0000") && strstr(xml, "name=\"Freeform "));
    free(xml);
    if (d) doc_free(d);
  }
}
static void docx_canvas(void)
{
  Image c, sh;
  Doc *d, *b;
  const Image *o;
  char *xml;
  ok("canvas_make", canvas_make(&c, 5000, 3000) && c.kind == OBJ_GROUP && c.canvas == CANVAS_GROUP && c.nkids == 1 && c.kids[0].canvas == CANVAS_BG);
  img_defaults(&sh);
  sh.kind = OBJ_SHAPE; sh.shape = SH_ELLIPSE; sh.cx = 1000; sh.cy = 800; sh.fill = 0x00FF00; sh.lstyle = PB_SINGLE; sh.lwidth = 6; sh.lcolor = 0;
  ok("canvas_add", canvas_add(&c, &sh, 500, 400) && c.nkids == 2);
  c.kids[0].fill = 0xEEEEEE;
  c.wrap = WRAP_INLINE;
  d = doc_with(&c);
  img_release(&c);
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("canvas saved as wpc:wpc", xml && strstr(xml, "wordprocessingCanvas\"><wpc:wpc") && strstr(xml, "<wpc:bg><a:solidFill><a:srgbClr val=\"EEEEEE\"/>") &&
                                strstr(xml, "<wpc:whole>") && strstr(xml, "prst=\"ellipse\"") && !strstr(xml, "wpg:wgp"));
  free(xml);
  b = docx_again(d, NULL);
  o = b ? first_obj(b) : NULL;
  ok("canvas read back", o && o->kind == OBJ_GROUP && o->canvas == CANVAS_GROUP && o->nkids == 2 && o->kids[0].canvas == CANVAS_BG && o->kids[0].fill == 0xEEEEEE &&
                         o->kids[0].cx == 5000 && o->kids[1].shape == SH_ELLIPSE && o->kids[1].x == 500 && o->kids[1].y == 400 && o->kids[1].cx == 1000);
  if (b) doc_free(b);
  doc_free(d);
  /* an empty canvas survives too */
  canvas_make(&c, 4000, 2000);
  d = doc_with(&c);
  img_release(&c);
  b = docx_again(d, NULL);
  o = b ? first_obj(b) : NULL;
  ok("empty canvas read back", o && o->canvas == CANVAS_GROUP && o->nkids == 1 && o->cx == 4000);
  if (b) doc_free(b);
  doc_free(d);
  /* Word's canvas: a group in it, a line */
  {
    Buf body;
    buf_init(&body);
    buf_printf(&body, "<w:p><w:r><w:drawing><wp:inline %s><wp:extent cx=\"5486400\" cy=\"3200400\"/><wp:docPr id=\"1\" name=\"Canvas 1\"/>"
               "<a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas\"><wpc:wpc><wpc:bg><a:noFill/></wpc:bg><wpc:whole><a:ln><a:solidFill><a:srgbClr val=\"FF0000\"/></a:solidFill></a:ln></wpc:whole>"
               "<wps:wsp><wps:cNvPr id=\"2\" name=\"Rectangle 2\"/><wps:cNvSpPr/><wps:spPr><a:xfrm><a:off x=\"635000\" y=\"635000\"/><a:ext cx=\"1270000\" cy=\"635000\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></wps:spPr><wps:bodyPr/></wps:wsp>"
               "<wpg:wgp><wpg:cNvPr id=\"3\" name=\"Group 3\"/><wpg:cNvGrpSpPr/><wpg:grpSpPr><a:xfrm><a:off x=\"2540000\" y=\"0\"/><a:ext cx=\"1270000\" cy=\"1270000\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"1270000\" cy=\"1270000\"/></a:xfrm></wpg:grpSpPr>"
               "<wps:wsp><wps:cNvPr id=\"4\" name=\"Oval 4\"/><wps:cNvSpPr/><wps:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"635000\" cy=\"635000\"/></a:xfrm><a:prstGeom prst=\"ellipse\"><a:avLst/></a:prstGeom></wps:spPr><wps:bodyPr/></wps:wsp>"
               "<wps:wsp><wps:cNvPr id=\"5\" name=\"Oval 5\"/><wps:cNvSpPr/><wps:spPr><a:xfrm><a:off x=\"635000\" y=\"635000\"/><a:ext cx=\"635000\" cy=\"635000\"/></a:xfrm><a:prstGeom prst=\"ellipse\"><a:avLst/></a:prstGeom></wps:spPr><wps:bodyPr/></wps:wsp>"
               "</wpg:wgp></wpc:wpc></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>", NS);
    d = wp9_mini_docx(body.data, NULL);
    buf_free(&body);
    o = d ? first_obj(d) : NULL;
    ok("Word's canvas read", o && o->canvas == CANVAS_GROUP && o->nkids == 3 && o->kids[0].canvas == CANVAS_BG && o->kids[0].lstyle == PB_SINGLE && o->kids[0].lcolor == 0xFF0000 &&
                             o->kids[1].x == 1000 && o->kids[1].cx == 2000 && o->kids[2].kind == OBJ_GROUP && o->kids[2].nkids == 2 && o->kids[2].x == 4000 && o->cx == 8640 && o->chw == 8640);
    xml = d ? wp9_saved_xml(d, "word/document.xml") : NULL;
    ok("Word's canvas written back", xml && strstr(xml, "<wpc:wpc") && strstr(xml, "<wpg:wgp") && strstr(xml, "FF0000"));
    free(xml);
    if (d) doc_free(d);
  }
}
/* Word's own ink: a w14:contentPart naming an InkML part, with a picture standing in for it */
static void docx_native(void)
{
  static const char ink[] = "<inkml:ink xmlns:inkml=\"http://www.w3.org/2003/InkML\"><inkml:definitions><inkml:brush xml:id=\"br0\">"
    "<inkml:brushProperty name=\"width\" value=\"0.05\" units=\"cm\"/><inkml:brushProperty name=\"color\" value=\"#ED1C24\"/></inkml:brush></inkml:definitions>"
    "<inkml:trace contextRef=\"#ctx0\" brushRef=\"#br0\">100 100 0,'50'0'0,0 0 0,\"10\"0 0,!200 !200 0</inkml:trace><inkml:trace brushRef=\"#br0\">0 300, 300 300</inkml:trace></inkml:ink>";
  ZipWriter z;
  Buf doc, rels;
  Doc *d = NULL, *b;
  char err[100];
  unsigned lost = 0;
  const Image *o;
  char *xml;
  static const char ct[] = "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/ink/ink1.xml\" ContentType=\"application/inkml+xml\"/></Types>";
  static const char pkg[] = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
  buf_init(&doc); buf_init(&rels);
  buf_printf(&doc, "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\" %s><w:body>"
             "<w:p><w:r><w:t>Before</w:t></w:r><w:r><mc:AlternateContent><mc:Choice Requires=\"wpi\"><w:drawing><wp:anchor behindDoc=\"0\" relativeHeight=\"3\"><wp:simplePos x=\"0\" y=\"0\"/>"
             "<wp:positionH relativeFrom=\"column\"><wp:posOffset>635000</wp:posOffset></wp:positionH><wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>0</wp:posOffset></wp:positionV>"
             "<wp:extent cx=\"1270000\" cy=\"1270000\"/><wp:wrapNone/><wp:docPr id=\"7\" name=\"Ink 7\"/><a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingInk\">"
             "<w14:contentPart bwMode=\"auto\" r:id=\"rId9\"><w14:nvContentPartPr><w14:cNvContentPartPr/></w14:nvContentPartPr><w14:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"1270000\" cy=\"1270000\"/></w14:xfrm></w14:contentPart>"
             "</a:graphicData></a:graphic></wp:anchor></w:drawing></mc:Choice><mc:Fallback><w:pict/></mc:Fallback></mc:AlternateContent></w:r>"
             "<w:r><w:drawing><wp:anchor behindDoc=\"0\" relativeHeight=\"4\"><wp:simplePos x=\"0\" y=\"0\"/><wp:positionH relativeFrom=\"column\"><wp:posOffset>0</wp:posOffset></wp:positionH>"
             "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>0</wp:posOffset></wp:positionV><wp:extent cx=\"2540000\" cy=\"1270000\"/><wp:wrapNone/><wp:docPr id=\"8\" name=\"Group 8\"/>"
             "<a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\"><wpg:wgp><wpg:cNvGrpSpPr/><wpg:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"2540000\" cy=\"1270000\"/>"
             "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"2540000\" cy=\"1270000\"/></a:xfrm></wpg:grpSpPr>"
             "<wps:wsp><wps:cNvPr id=\"9\" name=\"Box\"/><wps:cNvSpPr/><wps:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"1270000\" cy=\"1270000\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></wps:spPr><wps:bodyPr/></wps:wsp>"
             "<mc:AlternateContent><mc:Choice Requires=\"wpi\"><w14:contentPart bwMode=\"auto\" r:id=\"rId9\"><w14:nvContentPartPr><w14:cNvPr id=\"10\" name=\"Ink 10\"/><w14:cNvContentPartPr/></w14:nvContentPartPr>"
             "<w14:xfrm><a:off x=\"1270000\" y=\"0\"/><a:ext cx=\"1270000\" cy=\"635000\"/></w14:xfrm></w14:contentPart></mc:Choice><mc:Fallback><pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
             "<pic:nvPicPr><pic:cNvPr id=\"10\" name=\"Ink 10\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdNone\"/></pic:blipFill><pic:spPr/></pic:pic></mc:Fallback></mc:AlternateContent>"
             "</wpg:wgp></a:graphicData></a:graphic></wp:anchor></w:drawing></w:r></w:p></w:body></w:document>", NS);
  buf_adds(&rels, "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                  "<Relationship Id=\"rId9\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml\" Target=\"ink/ink1.xml\"/></Relationships>");
  zip_begin(&z);
  zip_add(&z, "[Content_Types].xml", ct, strlen(ct));
  zip_add(&z, "_rels/.rels", pkg, strlen(pkg));
  zip_add(&z, "word/document.xml", doc.data, doc.len);
  zip_add(&z, "word/_rels/document.xml.rels", rels.data, rels.len);
  zip_add(&z, "word/ink/ink1.xml", ink, strlen(ink));
  if (zip_finish(&z)) d = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
  zip_free(&z); buf_free(&doc); buf_free(&rels);
  o = d ? first_obj(d) : NULL;
  ok("Word's ink read", o && o->shape == SH_FREEFORM && o->ff && o->ff->raw && strstr(o->ff->raw, "contentPart") && o->ff->rawpart && !strcmp(o->ff->rawpart, "word/ink/ink1.xml") && !lost);
  ok("Word's ink: its traces", o && o->ff && o->ff->np == 2 && o->ff->p[0].n == 5 && o->lcolor == 0xED1C24 && o->lwidth >= 10 && o->lwidth <= 12);
  if (o && o->ff && o->ff->np) {
    /* 100,100 then +50 (so 150), 150 again, then second differences: velocity 0 + 10 = 10 -> 160 */
    const i32 *q = o->ff->p[0].xy;
    ok("Word's ink: differences decoded", q[0] < q[2] && q[2] == q[4] && q[6] > q[4] && q[1] == q[3]);
  }
  ok("Word's ink part kept", d && keep_find(d, "word/ink/ink1.xml") >= 0);
  {
    /* the group: a box, and the ink (its picture stand-in not read) */
    u32 i;
    const Image *g = NULL;
    for (i = 0; d && i < (u32)d->nimages; i++) if (d->images[i].kind == OBJ_GROUP) g = &d->images[i];
    ok("Word's ink in a group", g && g->nkids == 2 && g->kids[1].ff && g->kids[1].ff->raw && g->kids[1].x == 2000 && g->kids[1].cx == 2000);
  }
  xml = d ? wp9_saved_xml(d, "word/document.xml") : NULL;
  ok("Word's ink written back", xml && strstr(xml, "uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingInk\"><w14:contentPart") &&
                                strstr(xml, "Requires=\"wpi\"><w14:contentPart"));
  free(xml);
  b = d ? docx_again(d, &lost) : NULL;
  o = b ? first_obj(b) : NULL;
  ok("Word's ink twice", o && o->ff && o->ff->raw && o->ff->np == 2 && b && keep_find(b, "word/ink/ink1.xml") >= 0);
  if (b) doc_free(b);
  if (d) doc_free(d);
}

void test_wp20(void)
{
  recogniser();
  strokes();
  docx_ink();
  docx_canvas();
  docx_native();
}
