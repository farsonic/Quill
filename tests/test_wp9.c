/* test_wp9.c - the native tests of WP9 Character formatting (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp9() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/charfx.h"
#include "../core/zip.h"
#include "../core/layout.h"
#include "../core/pdf.h"

/* a .docx made of a body (and styles, may be NULL), read (also used by test_wp10.c) */
Doc *wp9_mini_docx(const char *body, const char *styles)
{
  ZipWriter z;
  Buf doc;
  Doc *d = NULL;
  char err[100];
  unsigned lost;
  buf_init(&doc);
  buf_adds(&doc, "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                 "xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\"><w:body>");
  buf_adds(&doc, body);
  buf_adds(&doc, "</w:body></w:document>");
  zip_begin(&z);
  zip_add(&z, "word/document.xml", doc.data, doc.len);
  if (styles) {
    Buf st;
    buf_init(&st);
    buf_adds(&st, "<?xml version=\"1.0\"?><w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    buf_adds(&st, styles);
    buf_adds(&st, "</w:styles>");
    zip_add(&z, "word/styles.xml", st.data, st.len);
    buf_free(&st);
  }
  if (zip_finish(&z)) d = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
  zip_free(&z); buf_free(&doc);
  return d;
}
/* d saved: its word/document.xml (malloc'd, NUL-terminated), NULL if that failed */
char *wp9_saved_xml(Doc *d, const char *part)
{
  size_t n, m;
  u8 *data = docx_save(d, &n), *x = NULL;
  ZipReader z;
  char *s = NULL;
  if (data && zip_open(&z, data, n)) {
    int i = zip_find(&z, part);
    if (i >= 0 && (x = zip_read(&z, i, &m))) { s = (char *)malloc(m + 1); if (s) { memcpy(s, x, m); s[m] = 0; } free(x); }
    zip_close(&z);
  }
  free(data);
  return s;
}
static CharFmt *at(Doc *d, u32 pi, const char *word)
{
  const char *f = pi < d->np ? strstr(d->p[pi]->t, word) : NULL;
  static CharFmt none;
  return f ? para_fmt_at(d->p[pi], (u32)(f - d->p[pi]->t)) : &none;
}

static void effects_docx(void)
{
  static const char body[] =
    "<w:p><w:r><w:t xml:space=\"preserve\">plain </w:t></w:r>"
    "<w:r><w:rPr><w:vanish/></w:rPr><w:t xml:space=\"preserve\">secret </w:t></w:r>"
    "<w:r><w:rPr><w:outline/><w:shadow/></w:rPr><w:t xml:space=\"preserve\">hollow </w:t></w:r>"
    "<w:r><w:rPr><w:emboss/></w:rPr><w:t xml:space=\"preserve\">raised </w:t></w:r>"
    "<w:r><w:rPr><w:imprint/></w:rPr><w:t xml:space=\"preserve\">pressed </w:t></w:r>"
    "<w:r><w:rPr><w:u w:val=\"double\" w:color=\"FF0000\"/></w:rPr><w:t xml:space=\"preserve\">redline </w:t></w:r>"
    "<w:r><w:rPr><w:highlight w:val=\"yellow\"/><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9D9D9\"/></w:rPr><w:t xml:space=\"preserve\">both </w:t></w:r>"
    "<w:r><w:rPr><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"00B0F0\"/></w:rPr><w:t xml:space=\"preserve\">shaded </w:t></w:r>"
    "<w:r><w:rPr><w:kern w:val=\"28\"/><w14:glow w14:rad=\"63500\"><w14:schemeClr w14:val=\"accent1\"/></w14:glow><w14:ligatures w14:val=\"standard\"/></w:rPr><w:t>glowing</w:t></w:r></w:p>";
  Doc *d = wp9_mini_docx(body, "<w:style w:type=\"paragraph\" w:styleId=\"Quiet\"><w:name w:val=\"Quiet\"/><w:rPr><w:vanish/><w:outline/></w:rPr></w:style>"), *e;
  char *xml;
  ok("wp9 docx: read", d != NULL);
  if (!d) return;
  ok("wp9 docx: hidden text (w:vanish)", (at(d, 0, "secret")->fx2 & FX2_HIDDEN) && !(at(d, 0, "plain")->fx2 & FX2_HIDDEN));
  ok("wp9 docx: outline and shadow", (at(d, 0, "hollow")->fx2 & (FX2_OUTLINE | FX2_SHADOW)) == (FX2_OUTLINE | FX2_SHADOW));
  ok("wp9 docx: emboss, engrave (w:imprint)", (at(d, 0, "raised")->fx2 & FX2_RELIEF) == FX2_EMBOSS && (at(d, 0, "pressed")->fx2 & FX2_RELIEF) == FX2_ENGRAVE);
  ok("wp9 docx: the underline's colour", at(d, 0, "redline")->ulcolor == (HL_ON | 0xFF0000) && at(d, 0, "redline")->ul == UL_DOUBLE);
  ok("wp9 docx: shading kept apart from the highlight", at(d, 0, "both")->hl == (HL_ON | 0xFFFF00) && at(d, 0, "both")->shade == (HL_ON | 0xD9D9D9) &&
     at(d, 0, "shaded")->hl == 0 && at(d, 0, "shaded")->shade == (HL_ON | 0x00B0F0));
  ok("wp9 docx: w14 text effects and w:kern kept", strstr(rx_text(at(d, 0, "glowing")->rx), "<w14:glow") && strstr(rx_text(at(d, 0, "glowing")->rx), "w14:ligatures") &&
     strstr(rx_text(at(d, 0, "glowing")->rx), "<w:kern w:val=\"28\"/>"));
  {
    int k = doc_style(d, "Quiet");
    ok("wp9 docx: a style's effects", k > 0 && (d->styles[k].cf.fx2 & (FX2_HIDDEN | FX2_OUTLINE)) == (FX2_HIDDEN | FX2_OUTLINE) && (d->styles[k].cmask & CM_FX2));
  }
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("wp9 docx: written as Word does", xml && strstr(xml, "<w:vanish/>") && strstr(xml, "<w:outline/><w:shadow/>") && strstr(xml, "<w:emboss/>") &&
     strstr(xml, "<w:imprint/>") && strstr(xml, "<w:u w:val=\"double\" w:color=\"FF0000\"/>") && strstr(xml, "<w:highlight w:val=\"yellow\"/>") &&
     strstr(xml, "w:fill=\"D9D9D9\"") && strstr(xml, "<w14:glow w14:rad=\"63500\">") && strstr(xml, "xmlns:w14="));
  free(xml);
  e = docx_again(d, NULL);
  ok("wp9 docx: read back the same", e && same_doc(d, e) && at(e, 0, "secret")->fx2 == at(d, 0, "secret")->fx2 && at(e, 0, "hollow")->fx2 == at(d, 0, "hollow")->fx2 &&
     at(e, 0, "redline")->ulcolor == (HL_ON | 0xFF0000) && at(e, 0, "both")->shade == (HL_ON | 0xD9D9D9) && at(e, 0, "both")->hl == (HL_ON | 0xFFFF00) &&
     at(e, 0, "glowing")->rx == at(d, 0, "glowing")->rx && e->styles[doc_style(e, "Quiet")].cf.fx2 == d->styles[doc_style(d, "Quiet")].cf.fx2);
  doc_free(e);
  doc_free(d);
}

static void enclose_docx(void)
{
  /* Word's own structure: no result, the symbol and the text in runs of their own sizes */
  static const char body[] =
    "<w:p><w:r><w:t xml:space=\"preserve\">No </w:t></w:r>"
    "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r>"
    "<w:r><w:rPr><w:position w:val=\"-4\"/><w:sz w:val=\"36\"/></w:rPr><w:instrText xml:space=\"preserve\"> eq \\o\\ac(\xE2\x97\x8B</w:instrText></w:r>"
    "<w:r><w:instrText>,</w:instrText></w:r><w:r><w:instrText>12</w:instrText></w:r><w:r><w:instrText>)</w:instrText></w:r>"
    "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
    "<w:r><w:t xml:space=\"preserve\"> and </w:t></w:r>"
    "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> eq \\o\\ac(\xE2\x96\xA1,A)</w:instrText></w:r>"
    "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>A</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>";
  Doc *d = wp9_mini_docx(body, NULL), *e;
  int shape, enl;
  char t[40], code[80], *xml;
  ok("enclose: a field code parsed", enclose_parse(" eq \\o\\ac(\xE2\x97\x87,12)", &shape, &enl, t, sizeof t) && shape == EN_DIAMOND && !enl && !strcmp(t, "12"));
  enclose_code(code, sizeof code, EN_TRIANGLE, 0, "A");
  ok("enclose: a field code made", !strcmp(code, "eq \\o\\ac(\xE2\x96\xB3,A)"));
  ok("enclose: not other fields", !enclose_parse(" PAGE ", &shape, &enl, t, sizeof t) && !enclose_parse(" eq \\f(1,2)", &shape, &enl, t, sizeof t));
  ok("enclose: read as text in an enclosure", d && text_is(d, "No 12 and A") && ENCLOSURE(at(d, 0, "12")) == EN_CIRCLE && (at(d, 0, "12")->fx2 & FX2_ENLARGE) &&
     ENCLOSURE(at(d, 0, "A")) == EN_SQUARE && !(at(d, 0, "A")->fx2 & FX2_ENLARGE) && !ENCLOSURE(at(d, 0, "No")));
  if (!d) return;
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("enclose: written as Word's field", xml && strstr(xml, "<w:instrText xml:space=\"preserve\"> eq \\o\\ac(\xE2\x97\x8B</w:instrText>") &&
     strstr(xml, "<w:instrText xml:space=\"preserve\">12</w:instrText>") && strstr(xml, "<w:position w:val=\"-4\"/><w:sz w:val=\"30\"/>"));
  free(xml);
  e = docx_again(d, NULL);
  ok("enclose: read back the same", e && text_is(e, "No 12 and A") && at(e, 0, "12")->fx2 == at(d, 0, "12")->fx2 && at(e, 0, "A")->fx2 == at(d, 0, "A")->fx2);
  doc_free(e);
  doc_free(d);
}

static void model(void)
{
  Doc *d = doc_new();
  Pos c = P(0, 0);
  CharFmt v;
  doc_insert(d, &c, "one two three", 13, NULL);
  memset(&v, 0, sizeof v);
  v.fx2 = FX2_OUTLINE;
  doc_format(d, P(0, 0), P(0, 7), CM_FX2, &v);
  /* one bit at a time (fx2mask): the outline stays where hidden is set */
  v.fx2 = FX2_HIDDEN; v.fx2mask = FX2_HIDDEN;
  doc_format(d, P(0, 4), P(0, 13), CM_FX2, &v);
  ok("fx2: a bit set alone keeps the others", para_fmt_at(d->p[0], 0)->fx2 == FX2_OUTLINE && para_fmt_at(d->p[0], 5)->fx2 == (FX2_OUTLINE | FX2_HIDDEN) &&
     para_fmt_at(d->p[0], 9)->fx2 == FX2_HIDDEN && runs_sane(d));
  v.fx2 = 0; v.fx2mask = FX2_HIDDEN;
  doc_format(d, P(0, 0), P(0, 13), CM_FX2, &v);
  ok("fx2: and cleared alone", para_fmt_at(d->p[0], 5)->fx2 == FX2_OUTLINE && para_fmt_at(d->p[0], 9)->fx2 == 0);
  ok("fx2: undo", doc_undo(d, &c) && para_fmt_at(d->p[0], 9)->fx2 == FX2_HIDDEN);
  memset(&v, 0, sizeof v);
  v.ulcolor = HL_ON | 0x00FF00; v.shade = HL_ON | 0xCCCCCC;
  doc_format(d, P(0, 0), P(0, 3), CM_ULCOLOR | CM_SHADE, &v);
  ok("underline colour and shading on a range", para_fmt_at(d->p[0], 1)->ulcolor == (HL_ON | 0x00FF00) && para_fmt_at(d->p[0], 1)->shade == (HL_ON | 0xCCCCCC) &&
     para_fmt_at(d->p[0], 4)->shade == 0);
  doc_clear_format(d, P(0, 0), P(0, 13));
  ok("clear formatting takes them off", para_fmt_at(d->p[0], 1)->shade == 0 && para_fmt_at(d->p[0], 5)->fx2 == 0 && para_fmt_at(d->p[0], 1)->ulcolor == 0);
  {
    static const char kx[] = "<w14:glow/><w14:ligatures w14:val=\"standard\"/>";
    u16 a = rx_add(kx, strlen(kx)), b2 = rx_edit(a, "w14:ligatures", "<w14:ligatures w14:val=\"none\"/>"), c2 = rx_edit(a, "w14:glow", NULL);
    ok("kept properties: the same text the same number", a && rx_add(kx, strlen(kx)) == a);
    ok("kept properties: one element replaced", !strcmp(rx_text(b2), "<w14:glow/><w14:ligatures w14:val=\"none\"/>") && !strcmp(rx_text(c2), "<w14:ligatures w14:val=\"standard\"/>"));
  }
  doc_free(d);
}

/* a PDF's page streams inflated, one after the other (malloc'd, NUL-terminated) */
char *wp9_pdf_streams(const u8 *pdf, size_t n)
{
  Buf o;
  size_t i;
  buf_init(&o);
  for (i = 0; i + 8 < n; i++) {
    const char *l;
    long len;
    if (memcmp(pdf + i, "/FlateDecode /Length ", 21)) continue;
    len = atol((const char *)pdf + i + 21);
    l = strstr((const char *)pdf + i, "stream\n");
    if (l && len > 6) {
      size_t at = (size_t)(l - (const char *)pdf) + 7;
      u8 *out = (u8 *)malloc(400000);
      long m = out && at + (size_t)len <= n ? zinflate(pdf + at + 2, (size_t)len - 6, out, 400000) : -1;
      if (m > 0 && !memchr(out, 0, (size_t)m)) buf_add(&o, out, (size_t)m);     /* (the page contents, not the fonts) */
      free(out);
    }
  }
  buf_addc(&o, 0);
  return buf_take(&o);
}
static void hidden_layout(void)
{
  Doc *d = doc_new();
  Pos c = P(0, 0);
  CharFmt v;
  Layout *L;
  i32 w0, w1;
  u8 *pdf;
  size_t n;
  doc_insert(d, &c, "Shown secretword shown", 22, NULL);
  memset(&v, 0, sizeof v);
  v.fx2 = FX2_HIDDEN; v.fx2mask = FX2_HIDDEN;
  doc_format(d, P(0, 6), P(0, 17), CM_FX2, &v);
  L = lay_new(d, &fs);
  lay_update(L);
  w0 = L->pl[0].lines[0].width;
  L->showhidden = 1;
  lay_update(L);
  w1 = L->pl[0].lines[0].width;
  ok("hidden text: no room unless shown", w0 > 0 && w1 > w0 + 400 && L->pl[0].hidden == 1);
  pdf = pdf_export(L, &n);
  {
    char *st = pdf ? wp9_pdf_streams(pdf, n) : NULL;
    ok("hidden text: left out of the PDF, the layout as it was after", st && strstr(st, "(Shown)") && !strstr(st, "secretword") && L->showhidden == 1 &&
       L->pl[0].lines[0].width == w1);
    free(st);
  }
  free(pdf);
  /* the effects in the PDF */
  memset(&v, 0, sizeof v);
  v.fx2 = FX2_OUTLINE | FX2_SHADOW; v.fx2mask = FX2_RELIEF | FX2_HIDDEN;
  doc_format(d, P(0, 0), P(0, 22), CM_FX2, &v);
  v.fx2 = (u16)(EN_CIRCLE << 5); v.fx2mask = FX2_ENCLOSE;
  doc_format(d, P(0, 0), P(0, 5), CM_FX2, &v);
  lay_update(L);
  ok("enclosed: room for the circle", L->pl[0].boxes[0].w >= 240);
  pdf = pdf_export(L, &n);
  {
    char *st = pdf ? wp9_pdf_streams(pdf, n) : NULL;
    ok("PDF: outline stroked, shadow grey, the enclosure drawn", st && strstr(st, "2 Tr") && strstr(st, "0.501 0.501 0.501 rg") && strstr(st, " c S"));
    free(st);
  }
  free(pdf);
  lay_free(L);
  doc_free(d);
}

void test_wp9(void)
{
  model();
  effects_docx();
  enclose_docx();
  hidden_layout();
}
