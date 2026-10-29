/* test_wp29.c - the native tests of WP29 PDF export depth and page images (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp29() */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/zip.h"
#include "../core/toc.h"

/* every stream of a PDF inflated and put end to end (NUL-terminated, malloc'd), to look for what
   the pages, fonts and CMaps say */
static char *all_streams(const u8 *pdf, size_t n, size_t *outn)
{
  Buf b;
  const u8 *q = pdf;
  buf_init(&b);
  while ((q = memmem(q, n - (size_t)(q - pdf), "/Length ", 8))) {
    long len = atol((const char *)q + 8);
    const u8 *st = memmem(q, n - (size_t)(q - pdf), "stream\n", 7);
    q += 8;
    if (!st || len <= 6 || st + 7 + len > pdf + n) continue;
    if (st[7] == 0x78) {                     /* (deflated: the zlib header) */
      size_t cap = (size_t)len * 40 + 65536;
      u8 *out = (u8 *)malloc(cap);
      long got = out ? zinflate(st + 7 + 2, (size_t)len - 6, out, cap) : -1;
      if (got > 0) { buf_add(&b, out, (size_t)got); buf_addc(&b, '\n'); }
      free(out);
    }
  }
  *outn = b.len;
  return buf_take(&b);
}
static int count(const u8 *h, size_t n, const char *needle)
{
  int c = 0;
  size_t l = strlen(needle);
  const u8 *q = h;
  while (q && (q = memmem(q, n - (size_t)(q - h), needle, l))) { c++; q += l; }
  return c;
}

/* a document with everything WP29 adds to a PDF: text outside Windows-1252, headings, a table of
   contents, a bookmark and a link to it, a web link, a footnote and an endnote */
static Doc *nav_doc(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d), a, b, e;
  int h1 = doc_style(d, "Heading1"), h2 = doc_style(d, "Heading2"), n, en;
  char ref[8];
  static const char *text = "Introduction\nGreek \xCE\xB1\xCE\xB2\xCE\xB3 and Cyrillic \xD0\x96\xD0\xB8\xD0\xB2 and \xE2\x82\xAC\nDetails\nThe bookmark is here.\nSee the bookmark and the web.\nNote here";
  doc_insert(d, &c, text, (u32)strlen(text), NULL);
  c = doc_start(d);
  c.para = 0;
  d->title[0] = 0;
  strcpy(d->title, "Navigation \xCE\xA9");
  strcpy(d->author, "Quill Tester");
  if (h1 >= 0) doc_apply_style(d, 0, 0, h1);
  if (h2 >= 0) doc_apply_style(d, 2, 2, h2);
  a.para = 3; a.off = 4; b.para = 3; b.off = 12;
  doc_set_bmark(d, a, b, "spot");
  a.para = 4; a.off = 4; b.para = 4; b.off = 16;
  doc_set_link(d, a, b, "#spot", NULL, &e);
  a.para = 4; a.off = 25; b.para = 4; b.off = 28;
  doc_set_link(d, a, b, "https://example.com/", NULL, &e);
  n = doc_add_note(d);
  story_set_text(d, &d->notes[n], "The footnote's text.", 0, AL_LEFT);
  note_utf8(ref, n);
  c.para = 5; c.off = 4;
  doc_insert(d, &c, ref, 4, NULL);
  en = doc_add_endnote(d);
  story_set_text(d, &d->enotes[en], "The endnote's text.", 0, AL_LEFT);
  enote_utf8(ref, en);
  c.para = 5; c.off = d->p[5]->len;
  doc_insert(d, &c, ref, 4, NULL);
  return d;
}

/* the drawing the page pictures must match the PDF in: a diagonal watermark, a dotted page border,
   turned and dashed shapes, a star, a text box, highlighting and underlines */
static void drawing_doc(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d), a, b;
  Layout *L;
  int k[4], i;
  char ref[8];
  size_t n;
  u8 *pdf;
  CharFmt cf;
  static const char *T = "Shapes, a watermark and a border. Highlighted, double underlined and struck words.";
  doc_insert(d, &c, T, (u32)strlen(T), NULL);
  strcpy(d->wm.text, "DRAFT"); d->wm.color = 0xC0C0C0;
  d->border.style = PB_DOTTED; d->border.width = 12; d->border.space = 360; d->border.color = 0x2050A0;
  k[0] = doc_add_shape(d, SH_ELLIPSE, 3000, 1800);
  k[1] = doc_add_shape(d, SH_RECT, 2600, 1400);
  k[2] = doc_add_shape(d, SH_STAR, 2000, 2000);
  k[3] = doc_add_shape(d, -1, 3600, 1200);
  for (i = 0; i < 4; i++) {
    Image *im = &d->images[k[i]];
    im->wrap = WRAP_FRONT; im->hrel = POS_PAGE; im->vrel = POS_PAGE; im->halign = PA_NONE; im->valign = PA_NONE;
    im->x = 1800 + i * 2400; im->y = 4000 + (i & 1) * 2600;
  }
  d->images[k[0]].fill = 0xF0A030; d->images[k[0]].lstyle = PB_DASHED; d->images[k[0]].lwidth = 16; d->images[k[0]].lcolor = 0x803000;
  d->images[k[1]].rot = 300; d->images[k[1]].fill = 0x80C0F0; d->images[k[1]].lstyle = PB_SINGLE; d->images[k[1]].lwidth = 24; d->images[k[1]].lcolor = 0x000080;
  d->images[k[2]].fill = 0xE02020;
  for (i = 3; i >= 0; i--) { pic_utf8(ref, k[i]); c = doc_start(d); doc_insert(d, &c, ref, 4, NULL); }
  if (!d->images[k[3]].text && (d->images[k[3]].text = (Story *)calloc(1, sizeof(Story))) == NULL) return;
  story_set_text(d, d->images[k[3]].text, "Text in a box", 0, AL_CENTER);
  memset(&cf, 0, sizeof cf);
  a.para = 0; a.off = 16 + 34; b.para = 0; b.off = 16 + 45;
  cf.hl = HL_ON | 0xFFFF00; doc_format(d, a, b, CM_HL, &cf);
  a.off = 16 + 47; b.off = 16 + 64;
  cf.flags = CF_UNDERLINE; cf.ul = UL_DOUBLE; doc_format(d, a, b, CM_UNDERLINE, &cf);
  a.off = 16 + 69; b.off = 16 + 75;
  cf.flags = CF_STRIKE; doc_format(d, a, b, CM_STRIKE, &cf);
  L = lay_new(d, &fs);
  pdf = pdf_export(L, &n);
  ok("wp29 drawing: exported", pdf != NULL);
  if (pdf) file_write("/tmp/quill_wp29_fx.pdf", pdf, n);
  free(pdf);
  {
    u32 w, h;
    u8 *rgb = pdf_page_rgb(L, 0, 100, &w, &h);
    Buf o;
    buf_init(&o);
    ok("wp29 drawing: drawn", rgb && png_encode(&o, rgb, w, h, 100));
    if (rgb) file_write("/tmp/quill_wp29_fx.png", o.data, o.len);
    buf_free(&o); free(rgb);
  }
  lay_free(L); doc_free(d);
}

void test_wp29(void)
{
  Doc *d = nav_doc();
  Layout *L = lay_new(d, &fs);
  size_t n = 0, sn = 0;
  u8 *pdf;
  char *st;
  Pos at;
  int again = 0;
  /* a table of contents at the top, laid out twice for its page numbers */
  at.para = 0; at.off = 0;
  lay_update(L);
  doc_toc(d, L, &at, &again);
  lay_update(L);
  doc_toc(d, L, &at, &again);
  pdf = pdf_export(L, &n);
  st = pdf ? all_streams(pdf, n, &sn) : NULL;
  ok("wp29 pdf: made", pdf && n > 1000 && !memcmp(pdf + n - 6, "%%EOF\n", 6));
  ok("wp29 pdf: fonts as Type 0 with Identity-H, a ToUnicode CMap and a TrueType subset", pdf && memmem(pdf, n, "/Subtype /Type0", 15) && memmem(pdf, n, "/Encoding /Identity-H", 21) &&
     memmem(pdf, n, "/Subtype /CIDFontType2", 22) && memmem(pdf, n, "/CIDToGIDMap /Identity", 22) && memmem(pdf, n, "/ToUnicode", 10) && memmem(pdf, n, "/FontFile2", 10));
  {
    /* the subset tag: six capitals and a plus */
    const u8 *q = pdf ? memmem(pdf, n, "/BaseFont /", 11) : NULL;
    int k, tagged = q != NULL;
    for (k = 0; q && k < 6; k++) if (q[11 + k] < 'A' || q[11 + k] > 'Z') tagged = 0;
    ok("wp29 pdf: subset fonts named with a tag", tagged && q[17] == '+');
  }
  ok("wp29 pdf: the CMap maps glyphs to Greek, Cyrillic and the euro", st && memmem(st, sn, "beginbfchar", 11) && memmem(st, sn, "<03B1>", 6) && memmem(st, sn, "<0416>", 6) && memmem(st, sn, "<20AC>", 6));
  ok("wp29 pdf: text as glyph ids", st && memmem(st, sn, "> Tj", 4) && !memmem(st, sn, "(Greek", 6));
  ok("wp29 pdf: the outline from the headings", pdf && memmem(pdf, n, "/Type /Outlines", 15) && memmem(pdf, n, "/Title (Introduction)", 21) && memmem(pdf, n, "/Title (Details)", 16) &&
     memmem(pdf, n, "/PageMode /UseOutlines", 22));
  ok("wp29 pdf: Details nested under Introduction", pdf && memmem(pdf, n, "/Count 1", 8));
  ok("wp29 pdf: the web link opens its address", pdf && memmem(pdf, n, "/URI (https://example.com/)", 27));
  ok("wp29 pdf: links inside the document (bookmark, contents, notes and back)", pdf && count(pdf, n, "/Dest [") >= 6);
  ok("wp29 pdf: the bookmark a named destination", pdf && memmem(pdf, n, "/Names <</Dests", 15) && memmem(pdf, n, "(spot) [", 8));
  ok("wp29 pdf: title in UTF-16 and the author", pdf && memmem(pdf, n, "/Title <FEFF004E", 16) && memmem(pdf, n, "03A9>", 5) && memmem(pdf, n, "/Author (Quill Tester)", 22));
  if (pdf) file_write("/tmp/quill_wp29_nav.pdf", pdf, n);
  {
    /* (the same document as .docx, for trying the export in Quill itself) */
    size_t dn = 0;
    u8 *docx = docx_save(d, &dn);
    if (docx) file_write("/tmp/quill_wp29_nav.docx", docx, dn);
    free(docx);
  }
  free(st); free(pdf);
  {
    /* options: a page range, subject and keywords, no outline */
    PdfOpts o = pdf_options;
    o.outline = 0; strcpy(o.subject, "Tests"); strcpy(o.keywords, "pdf, quill");
    o.first = 1; o.last = 1;
    pdf = pdf_export_opts(L, &o, &n);
    ok("wp29 pdf: options: one page, subject, keywords, no outline", pdf && memmem(pdf, n, "/Count 1>>", 10) && memmem(pdf, n, "/Subject (Tests)", 16) &&
       memmem(pdf, n, "/Keywords (pdf, quill)", 22) && !memmem(pdf, n, "/Outlines", 9));
    free(pdf);
  }
  {
    /* the subset against the whole font */
    Face *f = fonts_face(&fs, "Liberation Sans", 0, 0);
    size_t flen = 0, slen = 0;
    u8 *file = f ? fonts_face_file(f, &flen) : NULL, used[8192], *sub;
    memset(used, 0, sizeof used);
    if (f && ttf_load(f)) { used[ttf_glyph(f, 'A') >> 3] |= (u8)(1 << (ttf_glyph(f, 'A') & 7)); used[ttf_glyph(f, 0x3B1) >> 3] |= (u8)(1 << (ttf_glyph(f, 0x3B1) & 7)); }
    sub = file ? font_subset(file, flen, used, f->nglyphs, &slen) : NULL;
    ok("wp29 subset: a TrueType font cut to two glyphs, under a tenth of its size", sub && slen > 100 && slen * 10 < flen && !memcmp(sub, "\0\1\0\0", 4));
    printf("  (subset of Liberation Sans: %lu of %lu bytes)\n", (unsigned long)slen, (unsigned long)flen);
    if (sub) file_write("/tmp/quill_wp29_sub.ttf", sub, slen);
    free(sub); free(file);
  }
  {
    /* a page drawn: the text dark on white */
    u32 w = 0, h = 0, x, y, dark = 0;
    u8 *rgb = pdf_page_rgb(L, 0, 72, &w, &h);
    ok("wp29 image: A4 at 72 dpi", rgb && w == 595 && h == 842);
    for (y = 0; rgb && y < h; y++) for (x = 0; x < w; x++) if (rgb[(y * w + x) * 3] < 100) dark++;
    ok("wp29 image: some text drawn, mostly white", rgb && dark > 200 && dark < w * h / 10);
    if (rgb) {
      Buf b;
      buf_init(&b);
      ok("wp29 png: encoded", png_encode(&b, rgb, w, h, 72) && !memcmp(b.data, "\x89PNG\r\n\x1A\n", 8) && !memcmp(b.data + 12, "IHDR", 4));
      {
        /* its pixels back: inflate the IDAT and undo the filters of the first rows */
        const u8 *q = memmem(b.data, b.len, "IDAT", 4);
        u32 len = q ? (u32)q[-4] << 24 | (u32)q[-3] << 16 | (u32)q[-2] << 8 | q[-1] : 0;
        size_t raw = (size_t)(w * 3 + 1) * h;
        u8 *out = (u8 *)malloc(raw), *prev = (u8 *)calloc(w * 3, 1);
        long got = q && out ? zinflate(q + 4 + 2, len - 6, out, raw) : -1;
        int same = got == (long)raw;
        for (y = 0; same && y < h; y++) {
          u8 *row = out + y * (w * 3 + 1), ft = row[0];
          for (x = 0; x < w * 3; x++) {
            u8 v = row[1 + x];
            if (ft == 1) v = (u8)(v + (x >= 3 ? row[1 + x - 3] : 0));
            else if (ft == 2) v = (u8)(v + prev[x]);
            else if (ft != 0) same = 0;
            row[1 + x] = v;
            if (v != rgb[(y * w) * 3 + x]) same = 0;
          }
          memcpy(prev, row + 1, w * 3);
        }
        ok("wp29 png: the pixels read back", same);
        free(out); free(prev);
      }
      file_write("/tmp/quill_wp29_page.png", b.data, b.len);
      buf_free(&b);
      buf_init(&b);
      ok("wp29 ilbm: a FORM ILBM of 24 planes", ilbm_encode(&b, rgb, w, h, 72) && !memcmp(b.data, "FORM", 4) && !memcmp(b.data + 8, "ILBMBMHD", 8) &&
         (u8)b.data[28] == 24 && b.data[30] == 1 && memmem(b.data, b.len, "BODY", 4) &&
         ((u32)(u8)b.data[4] << 24 | (u32)(u8)b.data[5] << 16 | (u32)(u8)b.data[6] << 8 | (u8)b.data[7]) + 8 == b.len);
      file_write("/tmp/quill_wp29_page.iff", b.data, b.len);
      buf_free(&b);
    }
    free(rgb);
  }
  {
    char name[64];
    page_image_name("Work:Docs/Report.docx.png", 3, name, sizeof name);
    ok("wp29 image: page file names", !strcmp(name, "Work:Docs/Report.docx-3.png"));
    page_image_name("RAM:Page", 12, name, sizeof name);
    ok("wp29 image: page file names without an ending", !strcmp(name, "RAM:Page-12"));
  }
  lay_free(L); doc_free(d);
  drawing_doc();
}
