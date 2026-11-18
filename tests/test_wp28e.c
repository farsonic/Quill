/* test_wp28e.c - the native tests of WP28e (docs/wordcraft-gap.md): Word XML (Flat OPC read and written,
   Word 2003 XML read; core/wordxml.c), plain text's encodings (core/docx.c txt_load/txt_save_enc) and the
   Markdown source view (core/markdown.c md_source_doc/md_apply_source). Checks with ok() (tests/testutil.h);
   coretest.c's main() calls test_wp28e().

   tests/data/w2003-*.xml are LibreOffice 24.2's Word 2003 XML ("MS Word 2003 XML" filter) of Quill's own
   tests/data word, notes, tables, headers and pictures .docx files; the other Word 2003 document here is
   written as Word 2003 writes one (comments, tracked changes, bookmarks, links, lists, merged cells). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/fmtio.h"
#include "../core/zip.h"
#include "../core/review.h"

extern const DocFormat fmt_wordxml, fmt_markdown;
int  wordxml_kind(const u8 *b, size_t n);
u8  *zip_to_flat(const u8 *z, size_t zn, size_t *len);
u8  *flat_to_zip(const u8 *b, size_t n, size_t *zlen);

static Doc *load(const char *path, unsigned *lost)
{
  char err[200];
  unsigned l = 0;
  Doc *d = doc_load_file(path, &l, err, sizeof err);
  if (!d) printf("  %s: %s\n", path, err);
  if (lost) *lost = l;
  return d;
}
static Doc *from(const DocFormat *f, const char *s, size_t n, unsigned *lost)
{
  char err[200] = "";
  unsigned l = 0;
  Doc *d = f->load((const u8 *)s, n, &l, err, sizeof err);
  if (!d) printf("  %s\n", err);
  if (lost) *lost = l;
  return d;
}
static int para(Doc *d, const char *s)
{
  u32 i;
  for (i = 0; d && i < d->np; i++) if (!strncmp(d->p[i]->t, s, strlen(s))) return (int)i;
  return -1;
}
static const CharFmt *at(Doc *d, const char *s)
{
  u32 i, k;
  for (i = 0; d && i < d->np; i++) {
    const char *f = strstr(d->p[i]->t, s);
    if (!f) continue;
    for (k = 0; k < d->p[i]->nr; k++) if (d->p[i]->r[k].end > (u32)(f - d->p[i]->t)) return &d->p[i]->r[k].f;
  }
  return NULL;
}
/* every paragraph of a with text is in b, in order (tabs left out: LibreOffice's Word 2003 export drops
   them, and the paragraphs of cells merged down) */
static void untab(const char *s, char *o, size_t cap)
{
  size_t k = 0;
  for (; *s && k + 1 < cap; s++) if (*s != '\t' && *s != '\v' && *s != ' ') o[k++] = *s;
  o[k] = 0;
}
static int same_paras(Doc *a, Doc *b)
{
  u32 i, j = 0;
  char t1[2000], t2[2000];
  if (!a || !b) return 0;
  for (i = 0; i < a->np; i++) {
    untab(a->p[i]->t, t1, sizeof t1);
    if (!t1[0]) continue;
    for (; j < b->np; j++) { untab(b->p[j]->t, t2, sizeof t2); if (!strcmp(t1, t2)) break; }
    if (j == b->np) { printf("  missing: [%s]\n", a->p[i]->t); return 0; }
    j++;
  }
  return 1;
}
/* ---- plain text */
static void text_encodings(void)
{
  static const u8 le[] = { 0xFF, 0xFE, 'a', 0, 0xAC, 0x20, '\r', 0, '\n', 0, 0x3D, 0xD8, 0x00, 0xDE, 0x1B, 0x04 };   /* a€ CRLF 😀 Л */
  static const u8 be[] = { 0xFE, 0xFF, 0, 'x', 0x03, 0xA9 };                                                         /* xΩ */
  static const u8 nobom[] = { 'h', 0, 'i', 0, '\n', 0, 0xE9, 0, 'x', 0, 'y', 0 };                                 /* hi, éxy */
  static const u8 u8bom[] = { 0xEF, 0xBB, 0xBF, 'o', 'k', 0xC5, 0x81 };                                            /* okŁ */
  static const u8 cp[] = { 'c', 'a', 'f', 0xE9, ' ', 0x80, ' ', 0x93, 'q', 0x94, '\r', 'n' };                       /* café € “q” CR n */
  size_t n, skip;
  u8 *o;
  Doc *d;
  ok("txt: encodings told apart", txt_encoding(le, sizeof le, &skip) == (TXT_UTF16LE | TXT_BOM) && skip == 2 && txt_encoding(be, sizeof be, &skip) == (TXT_UTF16BE | TXT_BOM) &&
     txt_encoding(nobom, sizeof nobom, &skip) == TXT_UTF16LE && skip == 0 && txt_encoding(u8bom, sizeof u8bom, &skip) == (TXT_UTF8 | TXT_BOM) && skip == 3 &&
     txt_encoding(cp, sizeof cp, &skip) == TXT_LATIN1 && txt_encoding((const u8 *)"plain", 5, &skip) == TXT_UTF8);
  d = txt_load(le, sizeof le);
  ok("txt: UTF-16LE with a surrogate pair and CR LF", d && d->np == 2 && !strcmp(d->p[0]->t, "a\xE2\x82\xAC") && !strcmp(d->p[1]->t, "\xF0\x9F\x98\x80\xD0\x9B") && d->txtenc == (TXT_UTF16LE | TXT_BOM));
  o = d ? txt_save_enc(d, d->txtenc, &n) : NULL;
  ok("txt: saved back as it was read (UTF-16LE, its mark, LF)", o && n == sizeof le - 2 && !memcmp(o, le, 6) && o[6] == '\n' && !o[7] && !memcmp(o + 8, le + 10, 6));
  free(o);
  o = d ? txt_save_enc(d, TXT_UTF8, &n) : NULL;
  ok("txt: and as UTF-8, every character kept", o && n == 11 && !memcmp(o, "a\xE2\x82\xAC\n\xF0\x9F\x98\x80\xD0\x9B", 11));
  free(o);
  o = d ? txt_save_enc(d, TXT_LATIN1, &n) : NULL;
  ok("txt: Latin-1 can't keep them", o && n >= 3 && o[0] == 'a' && memcmp(o, "a\xE2", 2));
  free(o);
  doc_free(d);
  d = txt_load(be, sizeof be);
  ok("txt: UTF-16BE", d && !strcmp(d->p[0]->t, "x\xCE\xA9"));
  o = d ? txt_save_enc(d, d->txtenc, &n) : NULL;
  ok("txt: UTF-16BE written back", o && n == sizeof be && !memcmp(o, be, n));
  free(o); doc_free(d);
  d = txt_load(nobom, sizeof nobom);
  ok("txt: UTF-16 without a mark, by its zero bytes", d && d->np == 2 && !strcmp(d->p[1]->t, "\xC3\xA9xy"));
  doc_free(d);
  d = txt_load(u8bom, sizeof u8bom);
  o = d ? txt_save_enc(d, d->txtenc, &n) : NULL;
  ok("txt: UTF-8 with its mark, kept", d && !strcmp(d->p[0]->t, "ok\xC5\x81") && o && n == sizeof u8bom && !memcmp(o, u8bom, n));
  free(o); doc_free(d);
  d = txt_load(cp, sizeof cp);
  ok("txt: 8-bit text as Windows-1252 (euro, curly quotes), a lone CR a line end", d && d->np == 2 && !strcmp(d->p[0]->t, "caf\xC3\xA9 \xE2\x82\xAC \xE2\x80\x9Cq\xE2\x80\x9D") &&
     !strcmp(d->p[1]->t, "n") && d->txtenc == TXT_LATIN1);
  doc_free(d);
  {
    /* a new document is saved as UTF-8; a Latin-1 file opened stays Latin-1 */
    Doc *e = doc_new();
    Pos c = doc_start(e);
    const char *tmp = "/tmp/quill_wp28e.txt";
    size_t fl;
    u8 *f = NULL;
    doc_insert(e, &c, "\xC5\x81\xC3\xB3" "d\xC5\xBA", 7, NULL);      /* Łódź */
    ok("txt: a new document is written in UTF-8", doc_save_file(e, tmp) && (f = file_read(tmp, &fl)) && fl == 7 && !memcmp(f, "\xC5\x81\xC3\xB3" "d\xC5\xBA", 7));
    free(f); f = NULL;
    doc_free(e);
    file_write(tmp, "caf\xE9", 4);
    e = load(tmp, NULL);
    ok("txt: a Latin-1 file saved back in Latin-1", e && e->txtenc == TXT_LATIN1 && doc_save_file(e, tmp) && (f = file_read(tmp, &fl)) && fl == 4 && !memcmp(f, "caf\xE9", 4));
    free(f);
    doc_free(e);
    remove(tmp);
  }
}

/* ---- Flat OPC */
static void flat_opc(void)
{
  static const char *const files[] = { "tests/data/word.docx", "tests/data/pictures.docx", "tests/data/notes.docx", "tests/data/headers.docx", "tests/data/tables.docx" };
  int k;
  for (k = 0; k < 5; k++) {
    size_t n, fn, zn;
    u8 *data = file_read(files[k], &n), *flat = data ? zip_to_flat(data, n, &fn) : NULL;
    unsigned lost = 0, lost2 = 0;
    char err[200], what[120];
    Doc *a = data ? docx_load(data, n, &lost, err, sizeof err) : NULL, *b = flat ? from(&fmt_wordxml, (const char *)flat, fn, &lost2) : NULL;
    u8 *z;
    snprintf(what, sizeof what, "Flat OPC: %s as one XML file reads as the .docx", files[k] + 11);
    ok(what, flat && wordxml_kind(flat, fn) == 1 && strstr((const char *)flat, "<pkg:package") && a && b && same_doc(a, b) && lost == lost2 &&
       a->nimages == b->nimages && a->nnotes == b->nnotes && (!a->nimages || (b->images[0].len == a->images[0].len && !memcmp(b->images[0].data, a->images[0].data, a->images[0].len))));
    if (k == 0 && b) {
      /* Quill's own Flat OPC: written, read back, the same; and the zip it stands for has the parts */
      size_t on;
      u8 *own = fmt_wordxml.save(b, &on);
      Doc *c = own ? from(&fmt_wordxml, (const char *)own, on, NULL) : NULL;
      ok("Flat OPC: written by Quill and read back", own && !strncmp((const char *)own, "<?xml", 5) && strstr((const char *)own, "progid=\"Word.Document\"") &&
         strstr((const char *)own, "pkg:name=\"/word/document.xml\" pkg:contentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"><pkg:xmlData><w:document") &&
         strstr((const char *)own, "pkg:name=\"/_rels/.rels\"") && !strstr((const char *)own, "<pkg:xmlData><?xml") && c && same_doc(b, c));
      z = own ? flat_to_zip(own, on, &zn) : NULL;
      {
        ZipReader r;
        ok("Flat OPC: the package again, with its content types", z && zip_open(&r, z, zn) && zip_find(&r, "word/document.xml") >= 0 && zip_find(&r, "[Content_Types].xml") >= 0 &&
           zip_find(&r, "word/styles.xml") >= 0);
        if (z) zip_close(&r);
      }
      free(z); free(own); doc_free(c);
    }
    free(data); free(flat); doc_free(a); doc_free(b);
  }
  ok("Flat OPC: the format by name", doc_format_for_name("report.xml", 1) == &fmt_wordxml && (fmt_wordxml.flags & DF_NATIVE));
  {
    unsigned lost;
    static const char junk[] = "<?xml version=\"1.0\"?><pkg:package xmlns:pkg=\"http://schemas.microsoft.com/office/2006/xmlPackage\"><pkg:part pkg:name=\"/x\"><pkg:binaryData>@@</pkg:binaryData></pkg:part></pkg:package>";
    char err[200] = "";
    ok("Flat OPC: a package without a document is refused with a message", !fmt_wordxml.load((const u8 *)junk, sizeof junk - 1, &lost, err, sizeof err) && err[0]);
    ok("Word XML: other XML isn't taken for it", !wordxml_kind((const u8 *)"<?xml version=\"1.0\"?><html/>", 28) && !wordxml_kind((const u8 *)"<package/>", 10));
  }
}

/* ---- Word 2003 XML */
static const char w2003[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<?mso-application progid=\"Word.Document\"?>\n"
  "<w:wordDocument xmlns:w=\"http://schemas.microsoft.com/office/word/2003/wordml\" xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:w10=\"urn:schemas-microsoft-com:office:word\""
  " xmlns:sl=\"http://schemas.microsoft.com/schemaLibrary/2003/core\" xmlns:aml=\"http://schemas.microsoft.com/aml/2001/core\" xmlns:wx=\"http://schemas.microsoft.com/office/word/2003/auxHint\""
  " xmlns:o=\"urn:schemas-microsoft-com:office:office\" xmlns:dt=\"uuid:C2F41010-65B3-11d1-A29F-00AA00C14882\" w:macrosPresent=\"no\" w:embeddedObjPresent=\"no\" w:ocxPresent=\"no\" xml:space=\"preserve\">"
  "<o:DocumentProperties><o:Title>Amiga notes</o:Title><o:Author>Ada</o:Author><o:Subject>Testing</o:Subject></o:DocumentProperties>"
  "<w:fonts><w:defaultFonts w:ascii=\"Times New Roman\" w:fareast=\"Times New Roman\" w:h-ansi=\"Times New Roman\" w:cs=\"Times New Roman\"/>"
  "<w:font w:name=\"Arial\"><w:panose-1 w:val=\"020B0604020202020204\"/><w:charset w:val=\"00\"/></w:font></w:fonts>"
  "<w:lists><w:listDef w:listDefId=\"0\"><w:lsid w:val=\"1A2B3C4D\"/><w:plt w:val=\"HybridMultilevel\"/>"
  "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:nfc w:val=\"4\"/><w:lvlText w:val=\"%1)\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:tabs><w:tab w:val=\"list\" w:pos=\"720\"/></w:tabs><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl></w:listDef>"
  "<w:listDef w:listDefId=\"1\"><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:nfc w:val=\"23\"/><w:lvlText w:val=\"\xEF\x82\xB7\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Symbol\" w:h-ansi=\"Symbol\" w:hint=\"default\"/></w:rPr></w:lvl></w:listDef>"
  "<w:list w:ilfo=\"1\"><w:ilst w:val=\"0\"/></w:list><w:list w:ilfo=\"2\"><w:ilst w:val=\"1\"/></w:list></w:lists>"
  "<w:styles><w:versionOfBuiltInStylenames w:val=\"4\"/><w:latentStyles w:defLockedState=\"off\" w:latentStyleCount=\"156\"/>"
  "<w:style w:type=\"paragraph\" w:default=\"on\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:rPr><wx:font wx:val=\"Times New Roman\"/><w:sz w:val=\"24\"/><w:sz-cs w:val=\"24\"/><w:lang w:val=\"EN-US\" w:fareast=\"EN-US\" w:bidi=\"AR-SA\"/></w:rPr></w:style>"
  "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><wx:uiName wx:val=\"Heading 1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:rsid w:val=\"00A1\"/>"
  "<w:pPr><w:pStyle w:val=\"Heading1\"/><w:keepNext/><w:spacing w:before=\"240\" w:after=\"60\"/><w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Arial\" w:h-ansi=\"Arial\" w:cs=\"Arial\"/><wx:font wx:val=\"Arial\"/><w:b/><w:b-cs/><w:kern w:val=\"32\"/><w:sz w:val=\"32\"/><w:sz-cs w:val=\"32\"/></w:rPr></w:style>"
  "<w:style w:type=\"character\" w:default=\"on\" w:styleId=\"DefaultParagraphFont\"><w:name w:val=\"Default Paragraph Font\"/><w:semiHidden/></w:style>"
  "<w:style w:type=\"character\" w:styleId=\"Hyperlink\"><w:name w:val=\"Hyperlink\"/><w:basedOn w:val=\"DefaultParagraphFont\"/><w:rPr><w:color w:val=\"0000FF\"/><w:u w:val=\"single\"/></w:rPr></w:style>"
  "<w:style w:type=\"paragraph\" w:styleId=\"Header\"><w:name w:val=\"header\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:pStyle w:val=\"Header\"/><w:tabs><w:tab w:val=\"center\" w:pos=\"4320\"/></w:tabs></w:pPr></w:style>"
  "<w:style w:type=\"table\" w:default=\"on\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/><w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/><w:tblCellMar><w:left w:w=\"108\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style>"
  "</w:styles>"
  "<w:docPr><w:view w:val=\"print\"/><w:zoom w:percent=\"100\"/><w:evenAndOddHeaders/><w:defaultTabStop w:val=\"720\"/><w:characterSpacingControl w:val=\"DontCompress\"/>"
  "<w:footnotePr><w:footnote w:type=\"separator\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote><w:footnote w:type=\"continuation-separator\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote></w:footnotePr>"
  "<w:compat><w:breakWrappedTables/></w:compat></w:docPr>"
  "<w:body><wx:sect>"
  "<wx:sub-section><w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Notes on the Amiga</w:t></w:r></w:p>"
  "<w:p><w:pPr><w:jc w:val=\"both\"/><w:spacing w:line=\"360\" w:line-rule=\"auto\"/><w:ind w:first-line=\"284\"/></w:pPr>"
  "<w:r><w:t>Plain, </w:t></w:r><w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>bold italic</w:t></w:r><w:r><w:t>, </w:t></w:r>"
  "<w:r><w:rPr><w:u w:val=\"dotted-heavy\"/><w:color w:val=\"FF0000\"/><w:highlight w:val=\"dark-cyan\"/></w:rPr><w:t>red dotted</w:t></w:r><w:r><w:t> and a note</w:t></w:r>"
  "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr><w:footnote><w:p><w:pPr><w:pStyle w:val=\"FootnoteText\"/></w:pPr><w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr><w:footnoteRef/></w:r><w:r><w:t> The note's text.</w:t></w:r></w:p></w:footnote></w:r>"
  "<w:r><w:t>.</w:t></w:r></w:p>"
  "<w:p><w:r><w:t>A </w:t></w:r><w:hlink w:dest=\"http://www.amiga.org/\"><w:r><w:rPr><w:rStyle w:val=\"Hyperlink\"/></w:rPr><w:t>link</w:t></w:r></w:hlink>"
  "<w:r><w:t> and a </w:t></w:r><aml:annotation aml:id=\"0\" w:type=\"Word.Bookmark.Start\" w:name=\"mark\"/><w:r><w:t>bookmark</w:t></w:r><aml:annotation aml:id=\"0\" w:type=\"Word.Bookmark.End\"/>"
  "<w:r><w:t> to </w:t></w:r><w:hlink w:bookmark=\"mark\"><w:r><w:t>go to</w:t></w:r></w:hlink><w:r><w:t>.</w:t></w:r></w:p>"
  "<w:p><w:r><w:t>Kept </w:t></w:r>"
  "<aml:annotation aml:id=\"1\" aml:author=\"Ada\" aml:createdate=\"2004-05-06T07:08:00Z\" w:type=\"Word.Insertion\"><aml:content><w:r><w:t>added </w:t></w:r></aml:content></aml:annotation>"
  "<aml:annotation aml:id=\"2\" aml:author=\"Ada\" aml:createdate=\"2004-05-06T07:09:00Z\" w:type=\"Word.Deletion\"><aml:content><w:r><w:delText>gone </w:delText></w:r></aml:content></aml:annotation>"
  "<aml:annotation aml:id=\"3\" w:type=\"Word.Comment.Start\"/><w:r><w:t>commented</w:t></w:r><aml:annotation aml:id=\"3\" w:type=\"Word.Comment.End\"/>"
  "<w:r><w:rPr><w:rStyle w:val=\"CommentReference\"/></w:rPr><aml:annotation aml:id=\"3\" aml:author=\"Bob\" aml:createdate=\"2004-05-06T08:00:00Z\" w:type=\"Word.Comment\" w:initials=\"B\">"
  "<aml:content><w:p><w:pPr><w:pStyle w:val=\"CommentText\"/></w:pPr><w:r><w:rPr><w:rStyle w:val=\"CommentReference\"/></w:rPr><w:annotationRef/></w:r><w:r><w:t>Is this right?</w:t></w:r></w:p></aml:content></aml:annotation></w:r>"
  "<w:r><w:t> text.</w:t></w:r></w:p>"
  "<w:p><w:pPr><w:listPr><w:ilvl w:val=\"0\"/><w:ilfo w:val=\"1\"/><wx:t wx:val=\"a)\"/><wx:font wx:val=\"Times New Roman\"/></w:listPr></w:pPr><w:r><w:t>First lettered</w:t></w:r></w:p>"
  "<w:p><w:pPr><w:listPr><w:ilvl w:val=\"0\"/><w:ilfo w:val=\"1\"/></w:listPr></w:pPr><w:r><w:t>Second lettered</w:t></w:r></w:p>"
  "<w:p><w:pPr><w:listPr><w:ilvl w:val=\"0\"/><w:ilfo w:val=\"2\"/></w:listPr></w:pPr><w:r><w:t>A bullet</w:t></w:r></w:p>"
  "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders><w:top w:val=\"single\" w:sz=\"4\" wx:bdrwidth=\"10\" w:space=\"0\" w:color=\"auto\"/><w:insideH w:val=\"thin-thick-small-gap\" w:sz=\"12\" w:space=\"0\" w:color=\"auto\"/></w:tblBorders></w:tblPr>"
  "<w:tblGrid><w:gridCol w:w=\"2000\"/><w:gridCol w:w=\"3000\"/></w:tblGrid>"
  "<w:tr><w:tc><w:tcPr><w:tcW w:w=\"2000\" w:type=\"dxa\"/><w:vmerge w:val=\"restart\"/></w:tcPr><w:p><w:r><w:t>Down</w:t></w:r></w:p></w:tc>"
  "<w:tc><w:tcPr><w:tcW w:w=\"3000\" w:type=\"dxa\"/><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"FFFF00\"/></w:tcPr><w:p><w:r><w:t>Yellow</w:t></w:r></w:p></w:tc></w:tr>"
  "<w:tr><w:tc><w:tcPr><w:tcW w:w=\"2000\" w:type=\"dxa\"/><w:vmerge/></w:tcPr><w:p/></w:tc><w:tc><w:tcPr><w:tcW w:w=\"3000\" w:type=\"dxa\"/></w:tcPr><w:p><w:r><w:t>Plain</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
  "<w:p><w:r><w:t>A picture: </w:t></w:r><w:r><w:pict><v:shapetype id=\"_x0000_t75\" coordsize=\"21600,21600\" o:spt=\"75\"><v:stroke joinstyle=\"miter\"/></v:shapetype>"
  "<w:binData w:name=\"wordml://02000001.png\">@PNG@</w:binData><v:shape id=\"_x0000_i1025\" type=\"#_x0000_t75\" style=\"width:72pt;height:36pt\"><v:imagedata src=\"wordml://02000001.png\" o:title=\"disc\"/></v:shape></w:pict></w:r>"
  "<w:r><w:t> and again </w:t></w:r><w:r><w:pict><v:shape id=\"_x0000_i1026\" type=\"#_x0000_t75\" style=\"width:1in;height:1in\"><v:imagedata src=\"wordml://02000001.png\" o:title=\"disc\"/></v:shape></w:pict></w:r></w:p>"
  "<w:p><w:pPr><w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1800\" w:bottom=\"1440\" w:left=\"1800\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/><w:cols w:space=\"720\"/></w:sectPr></w:pPr></w:p>"
  "</wx:sub-section></wx:sect><wx:sect><w:p><w:r><w:t>Second section, two columns.</w:t></w:r><w:r><w:br w:type=\"page\"/></w:r><w:r><w:t>After a page break</w:t></w:r></w:p>"
  "<w:sectPr><w:hdr w:type=\"odd\"><w:p><w:pPr><w:pStyle w:val=\"Header\"/></w:pPr><w:r><w:t>Odd header</w:t></w:r></w:p></w:hdr>"
  "<w:hdr w:type=\"even\"><w:p><w:pPr><w:pStyle w:val=\"Header\"/></w:pPr><w:r><w:t>Even header</w:t></w:r></w:p></w:hdr>"
  "<w:ftr w:type=\"odd\"><w:p><w:r><w:t>Page </w:t></w:r><w:fldSimple w:instr=\" PAGE \"><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p></w:ftr>"
  "<w:pgSz w:w=\"16838\" w:h=\"11906\" w:orient=\"landscape\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" w:header=\"567\" w:footer=\"567\" w:gutter=\"0\"/>"
  "<w:cols w:num=\"2\" w:space=\"567\"/><w:type w:val=\"next-page\"/></w:sectPr></wx:sect></w:body></w:wordDocument>";
static void word2003(void)
{
  size_t pn, n;
  u8 *png = file_read("tests/data/disc.png", &pn);
  Buf b, x;
  const char *at_png;
  unsigned lost = 0;
  Doc *d;
  int i;
  buf_init(&b); buf_init(&x);
  if (png) b64_encode(&b, png, pn);
  at_png = strstr(w2003, "@PNG@");
  buf_add(&x, w2003, (size_t)(at_png - w2003)); buf_add(&x, b.data, b.len); buf_adds(&x, at_png + 5);
  ok("Word 2003: known by its root", wordxml_kind((const u8 *)x.data, x.len) == 2);
  d = from(&fmt_wordxml, x.data, x.len, &lost);
  ok("Word 2003: read", d && !lost && para(d, "Notes on the Amiga") == 0 && !strcmp(d->title, "Amiga notes") && !strcmp(d->author, "Ada"));
  if (d) {
    const CharFmt *f;
    int k = para(d, "Plain, bold italic");
    ok("Word 2003: styles (heading 1 in Arial, bold, 16 pt)", !strcmp(d->styles[d->p[0]->pf.style].id, "Heading1") && (at(d, "Notes")->flags & CF_BOLD) && at(d, "Notes")->size == 32 &&
       !strcmp(d->fonts[at(d, "Notes")->font], "Arial") && !strcmp(d->fonts[at(d, "Plain")->font], "Times New Roman"));
    ok("Word 2003: paragraph formatting (justified, 1.5 lines, first line)", k > 0 && d->p[k]->pf.align == AL_JUSTIFY && d->p[k]->pf.line == 360 && d->p[k]->pf.first == 284);
    f = at(d, "bold italic");
    ok("Word 2003: bold italic", f && (f->flags & CF_BOLD) && (f->flags & CF_ITALIC));
    f = at(d, "red dotted");
    ok("Word 2003: colour, highlight and a hyphenated underline kind", f && f->color == 0xFF0000 && (f->flags & CF_UNDERLINE) && f->ul == UL_DOTTED && f->hl);
    ok("Word 2003: a footnote in its run", d->nnotes == 1 && d->notes[0].np == 1 && strstr(d->notes[0].p[0]->t, "The note's text.") && IS_NOTE(strstr(d->p[k]->t, "a note") + 6));
    f = at(d, "link");
    ok("Word 2003: links (w:hlink) to a page and to a bookmark", f && f->link && !strcmp(doc_link_url(d, f->link), "http://www.amiga.org/") &&
       (f = at(d, "go to")) && f->link && !strcmp(doc_link_url(d, f->link), "#mark") && d->nbmarks == 1 && !strcmp(d->bmarks[0], "mark"));
    k = para(d, "Kept ");
    ok("Word 2003: tracked insertion and deletion", k > 0 && (f = at(d, "added")) && f->rev == REV_INS && (f = at(d, "gone")) && f->rev == REV_DEL);
    ok("Word 2003: a comment on its range", d->ncomments == 1 && !strcmp(d->comments[0].author, "Bob") && d->comments[0].text.np && strstr(d->comments[0].text.p[0]->t, "Is this right?"));
    k = para(d, "First lettered");
    ok("Word 2003: lists (w:listPr, w:lists) lettered and bulleted", k > 0 && d->p[k]->pf.list && d->p[k + 1]->pf.list == d->p[k]->pf.list &&
       d->lists[d->p[k]->pf.list - 1].lvl[0].fmt == NF_LOWER_LETTER && d->p[k + 2]->pf.list && d->p[k + 2]->pf.list != d->p[k]->pf.list &&
       d->lists[d->p[k + 2]->pf.list - 1].lvl[0].fmt == NF_BULLET);
    k = para(d, "Down");
    ok("Word 2003: a table with a cell merged down (w:vmerge) and shading", k > 0 && d->p[k]->pf.cell.table && d->p[k]->pf.cell.vmerge && !strcmp(d->p[k + 1]->t, "Yellow") &&
       d->p[k + 1]->pf.cell.shade == 0xFFFF00);
    k = para(d, "A picture: ");
    ok("Word 2003: a picture (w:binData) at its size, and used again by its name", k > 0 && d->nimages == 2 && d->images[0].cx == 1440 && d->images[0].cy == 720 && d->images[1].cx == 1440 &&
       d->images[0].len == pn && !memcmp(d->images[0].data, png, pn));
    ok("Word 2003: sections (page size, landscape, columns), headers (odd, even), footer page field", d->nsects >= 1 && d->page.width == 16838 && d->p[d->np - 1]->pf.sect && d->sects[d->p[d->np - 1]->pf.sect - 1].cols == 2 &&
       d->evenodd && d->hdr[HF_DEFAULT].np && !strcmp(d->hdr[HF_DEFAULT].p[0]->t, "Odd header") && d->hdr[HF_EVEN].np && !strcmp(d->hdr[HF_EVEN].p[0]->t, "Even header") &&
       d->ftr[HF_DEFAULT].np && strstr(d->ftr[HF_DEFAULT].p[0]->t, FIELD_PAGE));
    {
      Doc *e = docx_again(d, NULL);
      ok("Word 2003: saved as .docx and read back the same", e && same_doc(d, e) && e->nnotes == 1 && e->ncomments == 1 && e->nimages == 2);
      doc_free(e);
    }
  }
  doc_free(d);
  /* damaged: cut anywhere, it reads safely */
  for (i = 1; i < 60; i++) {
    size_t cut = x.len * (size_t)i / 60;
    char err[200];
    Doc *e = fmt_wordxml.load((const u8 *)x.data, cut, &lost, err, sizeof err);
    doc_free(e);
  }
  ok("Word 2003: cut short anywhere, read safely", 1);
  buf_free(&b); buf_free(&x); free(png);
  (void)n;
  {
    /* LibreOffice's Word 2003 XML of Quill's test documents: the same paragraphs as the .docx */
    static const char *const names[] = { "word", "notes", "tables", "headers", "pictures" };
    for (i = 0; i < 5; i++) {
      char p1[80], p2[80], what[100];
      Doc *a, *c;
      snprintf(p1, sizeof p1, "tests/data/%s.docx", names[i]);
      snprintf(p2, sizeof p2, "tests/data/w2003-%s.xml", names[i]);
      a = load(p1, NULL); c = load(p2, &lost);
      snprintf(what, sizeof what, "Word 2003: LibreOffice's %s.xml has the .docx's paragraphs", names[i]);
      ok(what, same_paras(a, c) && c->nnotes == a->nnotes && c->nimages == a->nimages && c->ntables == a->ntables);
      if (i == 3) ok("Word 2003: LibreOffice's headers and footers", c && c->hdr[HF_DEFAULT].np && !strcmp(c->hdr[HF_DEFAULT].p[0]->t, a->hdr[HF_DEFAULT].p[0]->t));
      doc_free(a); doc_free(c);
    }
  }
}

void test_wp28e(void)
{
  text_encodings();
  flat_opc();
  word2003();
}
