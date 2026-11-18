#define _GNU_SOURCE
/* test_wp16.c - the native tests of WP16 Content controls (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp16() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/sdt.h"
#include "../core/field.h"
#include "../core/zip.h"
#include "../core/keep.h"
#include "../core/layout.h"
#include "../core/pdf.h"

#define W_NS "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" " \
  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" " \
  "xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\" " \
  "xmlns:w15=\"http://schemas.microsoft.com/office/word/2012/wordml\" " \
  "xmlns:w16sdtdh=\"http://schemas.microsoft.com/office/word/2020/wordml/sdtdatahash\" " \
  "xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"w14 w15 w16sdtdh\""

/* a .docx of these body elements (a header's too, NULL none; a custom XML part, NULL none) */
static const char *ct_xml = "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/></Types>";
static u8 *make_docx(const char *body, const char *hdr, const char *cxml, size_t *len)
{
  ZipWriter z;
  Buf doc, rels;
  u8 *out = NULL;
  buf_init(&doc); buf_init(&rels);
  buf_adds(&doc, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<w:document " W_NS "><w:body>");
  buf_adds(&doc, body);
  if (hdr) buf_adds(&doc, "<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rIdH\"/></w:sectPr>");
  buf_adds(&doc, "</w:body></w:document>");
  zip_begin(&z);
  zip_add(&z, "[Content_Types].xml", ct_xml, strlen(ct_xml));
  {
    static const char *pr = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
    zip_add(&z, "_rels/.rels", pr, strlen(pr));
  }
  buf_adds(&rels, "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  if (hdr) {
    Buf h;
    buf_init(&h);
    buf_adds(&rels, "<Relationship Id=\"rIdH\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header1.xml\"/>");
    buf_adds(&h, "<?xml version=\"1.0\"?><w:hdr " W_NS ">");
    buf_adds(&h, hdr);
    buf_adds(&h, "</w:hdr>");
    zip_add(&z, "word/header1.xml", h.data, h.len);
    buf_free(&h);
  }
  if (cxml) {
    buf_adds(&rels, "<Relationship Id=\"rIdX\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml\" Target=\"../customXml/item1.xml\"/>");
    zip_add(&z, "customXml/item1.xml", cxml, strlen(cxml));
    {
      static const char *ip = "<?xml version=\"1.0\"?><ds:datastoreItem ds:itemID=\"{11111111-2222-3333-4444-555555555555}\" xmlns:ds=\"http://schemas.openxmlformats.org/officeDocument/2006/customXml\"><ds:schemaRefs/></ds:datastoreItem>";
      static const char *ir = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps\" Target=\"itemProps1.xml\"/></Relationships>";
      zip_add(&z, "customXml/itemProps1.xml", ip, strlen(ip));
      zip_add(&z, "customXml/_rels/item1.xml.rels", ir, strlen(ir));
    }
  }
  buf_adds(&rels, "</Relationships>");
  zip_add(&z, "word/_rels/document.xml.rels", rels.data, rels.len);
  zip_add(&z, "word/document.xml", doc.data, doc.len);
  if (zip_finish(&z)) { out = (u8 *)malloc(z.out.len); if (out) { memcpy(out, z.out.data, z.out.len); *len = z.out.len; } }
  zip_free(&z); buf_free(&doc); buf_free(&rels);
  return out;
}
static Doc *load(const u8 *b, size_t n)
{
  unsigned lost;
  char err[200];
  return docx_load(b, n, &lost, err, sizeof err);
}
/* a part of a saved package */
static char *part_of(Doc *d, const char *name, size_t *n)
{
  size_t len;
  u8 *data = docx_save(d, &len);
  ZipReader z;
  char *x = NULL;
  if (!data) return NULL;
  if (zip_open(&z, data, len)) { int i = zip_find(&z, name); if (i >= 0) x = (char *)zip_read(&z, i, n); zip_close(&z); }
  free(data);
  return x;
}
static int has(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }
static int count_of(const char *hay, const char *needle)
{
  int c = 0;
  const char *p = hay;
  size_t l = strlen(needle);
  while (p && (p = strstr(p, needle))) { c++; p += l; }
  return c;
}
/* the n'th control's start mark (in the order the marks come) */
static int nth(Doc *d, int n, Pos *at)
{
  Pos all_[256];
  int c = sdt_all(d, all_, 256);
  if (n >= c) return -1;
  *at = all_[n];
  return smark_index(d->p[at->para]->t + at->off);
}
static void content(Doc *d, Pos s, char *out, size_t cap)
{
  Pos e;
  if (!sdt_end(d, s, &e)) { snprintf(out, cap, "?"); return; }
  sdt_text(d, s, e, out, cap);
}

static Pos undo_at;
/* the same text but for which records the control marks name */
static int same_plain(Doc *a, Doc *b)
{
  u32 i, k;
  if (a->np != b->np) return 0;
  for (i = 0; i < a->np; i++) {
    const char *s = a->p[i]->t, *t = b->p[i]->t;
    if (a->p[i]->len != b->p[i]->len) { printf("  paragraph %lu: %s / %s\n", (unsigned long)i, s, t); return 0; }
    for (k = 0; k < a->p[i]->len; k++) {
      if (k + 4 <= a->p[i]->len && IS_SMARK(s + k) && IS_SMARK(t + k) && (u8)s[k + 1] == (u8)t[k + 1]) { k += 3; continue; }
      if (s[k] != t[k]) return 0;
    }
  }
  return 1;
}
#define R(t) "<w:r><w:t xml:space=\"preserve\">" t "</w:t></w:r>"
#define PLC "<w:r><w:rPr><w:rStyle w:val=\"PlaceholderText\"/></w:rPr><w:t>Click or tap here to enter text.</w:t></w:r>"

/* Word's own way of writing each kind of control (as Word 365 saves them) */
static const char *sample_body =
  /* a rich text control showing its placeholder, with title, tag, colour, appearance and a lock */
  "<w:p><w:r><w:t xml:space=\"preserve\">Name: </w:t></w:r>"
  "<w:sdt><w:sdtPr><w:rPr><w:b/></w:rPr><w:alias w:val=\"Full name\"/><w:tag w:val=\"name\"/><w:id w:val=\"-1543907611\"/>"
  "<w15:color w:val=\"FF0000\"/><w15:appearance w15:val=\"tags\"/><w:lock w:val=\"sdtLocked\"/>"
  "<w:placeholder><w:docPart w:val=\"DefaultPlaceholder_-1854013440\"/></w:placeholder><w:showingPlcHdr/>"
  "<w16sdtdh:storeItemChecksum w16sdtdh:val=\"abc\"/></w:sdtPr><w:sdtEndPr><w:rPr><w:i/></w:rPr></w:sdtEndPr>"
  "<w:sdtContent>" PLC "</w:sdtContent></w:sdt></w:p>"
  /* plain text, bound to custom XML */
  "<w:p><w:sdt><w:sdtPr><w:alias w:val=\"City\"/><w:id w:val=\"100\"/>"
  "<w:dataBinding w:prefixMappings=\"xmlns:ns0='urn:quill' \" w:xpath=\"/ns0:form[1]/ns0:city[1]\" w:storeItemID=\"{11111111-2222-3333-4444-555555555555}\"/>"
  "<w:text w:multiLine=\"1\"/></w:sdtPr><w:sdtContent>" R("Amsterdam") "</w:sdtContent></w:sdt></w:p>"
  /* a check box */
  "<w:p><w:sdt><w:sdtPr><w:id w:val=\"101\"/><w14:checkbox><w14:checked w14:val=\"1\"/>"
  "<w14:checkedState w14:val=\"2612\" w14:font=\"MS Gothic\"/><w14:uncheckedState w14:val=\"2610\" w14:font=\"MS Gothic\"/></w14:checkbox></w:sdtPr>"
  "<w:sdtContent><w:r><w:rPr><w:rFonts w:ascii=\"MS Gothic\" w:eastAsia=\"MS Gothic\" w:hAnsi=\"MS Gothic\" w:hint=\"eastAsia\"/></w:rPr><w:t>\xE2\x98\x92</w:t></w:r></w:sdtContent></w:sdt>"
  "<w:r><w:t xml:space=\"preserve\"> Agree</w:t></w:r></w:p>"
  /* combo box and drop-down list */
  "<w:p><w:sdt><w:sdtPr><w:id w:val=\"102\"/><w:comboBox w:lastValue=\"b\"><w:listItem w:displayText=\"Alpha\" w:value=\"a\"/><w:listItem w:displayText=\"Beta\" w:value=\"b\"/></w:comboBox></w:sdtPr>"
  "<w:sdtContent>" R("Beta") "</w:sdtContent></w:sdt>"
  "<w:sdt><w:sdtPr><w:id w:val=\"103\"/><w:placeholder><w:docPart w:val=\"DefaultPlaceholder_-1854013438\"/></w:placeholder><w:showingPlcHdr/>"
  "<w:dropDownList><w:listItem w:displayText=\"Choose an item.\" w:value=\"\"/><w:listItem w:displayText=\"Red\" w:value=\"Red\"/><w:listItem w:displayText=\"Green\" w:value=\"Green\"/></w:dropDownList></w:sdtPr>"
  "<w:sdtContent><w:r><w:rPr><w:rStyle w:val=\"PlaceholderText\"/></w:rPr><w:t>Choose an item.</w:t></w:r></w:sdtContent></w:sdt></w:p>"
  /* a date picker */
  "<w:p><w:sdt><w:sdtPr><w:id w:val=\"104\"/><w:date w:fullDate=\"2026-10-29T00:00:00Z\"><w:dateFormat w:val=\"d MMMM yyyy\"/><w:lid w:val=\"en-GB\"/>"
  "<w:storeMappedDataAs w:val=\"dateTime\"/><w:calendar w:val=\"gregorian\"/></w:date></w:sdtPr><w:sdtContent>" R("29 October 2026") "</w:sdtContent></w:sdt></w:p>"
  /* a building block gallery round a paragraph (block level) */
  "<w:sdt><w:sdtPr><w:id w:val=\"105\"/><w:docPartObj><w:docPartGallery w:val=\"Quick Parts\"/><w:docPartCategory w:val=\"General\"/></w:docPartObj></w:sdtPr>"
  "<w:sdtContent><w:p>" R("Block one") "</w:p><w:p>" R("Block two") "</w:p></w:sdtContent></w:sdt>"
  /* a repeating section of table rows */
  "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr><w:tblGrid><w:gridCol w:w=\"3000\"/><w:gridCol w:w=\"3000\"/></w:tblGrid>"
  "<w:tr><w:tc><w:p>" R("Item") "</w:p></w:tc><w:tc><w:p>" R("Qty") "</w:p></w:tc></w:tr>"
  "<w:sdt><w:sdtPr><w:id w:val=\"106\"/><w15:repeatingSection><w15:sectionTitle w:val=\"Lines\"/></w15:repeatingSection></w:sdtPr><w:sdtContent>"
  "<w:sdt><w:sdtPr><w:id w:val=\"107\"/><w15:repeatingSectionItem/></w:sdtPr><w:sdtContent>"
  "<w:tr><w:tc><w:p>" R("Apples") "</w:p></w:tc><w:tc><w:p>" R("3") "</w:p></w:tc></w:tr>"
  "</w:sdtContent></w:sdt></w:sdtContent></w:sdt>"
  "<w:tr><w:sdt><w:sdtPr><w:id w:val=\"108\"/><w:alias w:val=\"Cell\"/></w:sdtPr><w:sdtContent><w:tc><w:p>" R("Total") "</w:p></w:tc></w:sdtContent></w:sdt>"
  "<w:tc><w:sdt><w:sdtPr><w:id w:val=\"109\"/><w:lock w:val=\"contentLocked\"/></w:sdtPr><w:sdtContent><w:p>" R("3") "</w:p></w:sdtContent></w:sdt></w:tc></w:tr>"
  "</w:tbl>"
  /* a picture control (empty), a group, and a legacy check box form field */
  "<w:p><w:sdt><w:sdtPr><w:id w:val=\"110\"/><w:showingPlcHdr/><w:picture/></w:sdtPr><w:sdtContent>" R("Click or tap to insert a picture.") "</w:sdtContent></w:sdt></w:p>"
  "<w:sdt><w:sdtPr><w:id w:val=\"111\"/><w:group/></w:sdtPr><w:sdtContent><w:p>" R("Grouped text ")
  "<w:sdt><w:sdtPr><w:id w:val=\"112\"/><w:text/></w:sdtPr><w:sdtContent>" R("fill me") "</w:sdtContent></w:sdt></w:p></w:sdtContent></w:sdt>"
  "<w:p><w:r><w:fldChar w:fldCharType=\"begin\"><w:ffData><w:name w:val=\"Check1\"/><w:enabled/><w:calcOnExit w:val=\"0\"/><w:checkBox><w:sizeAuto/><w:default w:val=\"0\"/></w:checkBox></w:ffData></w:fldChar></w:r>"
  "<w:r><w:instrText xml:space=\"preserve\"> FORMCHECKBOX </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
  "<w:r><w:fldChar w:fldCharType=\"begin\"><w:ffData><w:name w:val=\"Text1\"/><w:enabled/><w:calcOnExit w:val=\"0\"/><w:textInput/></w:ffData></w:fldChar></w:r>"
  "<w:r><w:instrText xml:space=\"preserve\"> FORMTEXT </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>" R("\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82") "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
  "<w:r><w:fldChar w:fldCharType=\"begin\"><w:ffData><w:name w:val=\"Dropdown1\"/><w:enabled/><w:calcOnExit w:val=\"0\"/><w:ddList><w:result w:val=\"1\"/><w:listEntry w:val=\"One\"/><w:listEntry w:val=\"Two\"/></w:ddList></w:ffData></w:fldChar></w:r>"
  "<w:r><w:instrText xml:space=\"preserve\"> FORMDROPDOWN </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>";
static const char *sample_hdr =
  "<w:sdt><w:sdtPr><w:id w:val=\"200\"/><w:docPartObj><w:docPartGallery w:val=\"Page Numbers (Top of Page)\"/><w:docPartUnique/></w:docPartObj></w:sdtPr>"
  "<w:sdtContent><w:p>" R("Header text") "</w:p></w:sdtContent></w:sdt>";
static const char *sample_xml = "<?xml version=\"1.0\"?><form xmlns=\"urn:quill\"><name>Ada</name><city>Amsterdam</city></form>";

static void reading(Doc *d)
{
  Pos s;
  Sdt *r;
  char t[256];
  int k;
  ok("controls: read (14 in the body)", sdt_all(d, &s, 0) == 14);
  k = nth(d, 0, &s); r = sdt_get(d, k);
  ok("controls: rich text read", r && r->type == SDT_RICH && r->level == SL_INLINE && r->alias && !strcmp(r->alias, "Full name") && r->tag && !strcmp(r->tag, "name"));
  ok("controls: its id, colour, appearance, lock", r && r->id == -1543907611 && r->hascolor && r->color == 0xFF0000 && r->appearance == SAP_TAGS && r->lock == SLK_DEL);
  ok("controls: its placeholder", r && r->showplc && r->plctext && !strcmp(r->plctext, "Click or tap here to enter text.") && r->plcpart && !strcmp(r->plcpart, "DefaultPlaceholder_-1854013440"));
  ok("controls: rPr, unknown children and sdtEndPr kept verbatim", r && r->rpr && !strcmp(r->rpr, "<w:rPr><w:b/></w:rPr>") && has(r->other, "w16sdtdh:storeItemChecksum") && r->endpr && !strcmp(r->endpr, "<w:rPr><w:i/></w:rPr>"));
  ok("controls: the namespace its kept parts need", r && has(r->nsdecl, "xmlns:w16sdtdh=\"http://schemas.microsoft.com/office/word/2020/wordml/sdtdatahash\""));
  content(d, s, t, sizeof t);
  ok("controls: its content", !strcmp(t, "Click or tap here to enter text."));
  k = nth(d, 1, &s); r = sdt_get(d, k);
  ok("controls: plain text with data binding", r && r->type == SDT_TEXT && r->multiline && has(r->binding, "w:xpath=\"/ns0:form[1]/ns0:city[1]\""));
  k = nth(d, 2, &s); r = sdt_get(d, k);
  ok("controls: check box", r && r->type == SDT_CHECK && r->chk_on == 0x2612 && r->chk_off == 0x2610 && !strcmp(r->chk_onfont, "MS Gothic") && sdt_checked(d, s) == 1);
  k = nth(d, 3, &s); r = sdt_get(d, k);
  ok("controls: combo box items", r && r->type == SDT_COMBO && r->nitems == 2 && !strcmp(r->items[1].text, "Beta") && !strcmp(r->items[1].value, "b"));
  k = nth(d, 4, &s); r = sdt_get(d, k);
  ok("controls: drop-down list", r && r->type == SDT_DROP && r->nitems == 3 && r->showplc);
  k = nth(d, 5, &s); r = sdt_get(d, k);
  ok("controls: date picker", r && r->type == SDT_DATE && r->datefmt && !strcmp(r->datefmt, "d MMMM yyyy") && r->fulldate && !strcmp(r->fulldate, "2026-10-29T00:00:00Z"));
  k = nth(d, 6, &s); r = sdt_get(d, k);
  ok("controls: gallery at block level", r && r->type == SDT_GALLERY && r->level == SL_BLOCK && r->category && !strcmp(r->category, "General"));
  content(d, s, t, sizeof t);
  ok("controls: its two paragraphs", !strcmp(t, "Block one\nBlock two"));
  k = nth(d, 7, &s); r = sdt_get(d, k);
  ok("controls: repeating section of rows", r && r->type == SDT_REPEAT && r->level == SL_ROW && r->sectitle && !strcmp(r->sectitle, "Lines"));
  k = nth(d, 8, &s); r = sdt_get(d, k);
  ok("controls: its item", r && r->type == SDT_REPITEM && r->level == SL_ROW);
  ok("controls: the table keeps its rows", in_table(&d->p[s.para]->pf) && d->p[s.para]->pf.cell.row == 1);
  k = nth(d, 9, &s); r = sdt_get(d, k);
  ok("controls: a cell control", r && r->level == SL_CELL && r->alias && !strcmp(r->alias, "Cell"));
  k = nth(d, 10, &s); r = sdt_get(d, k);
  ok("controls: a block control in a cell", r && r->level == SL_BLOCK && r->depth == 1 && r->lock == SLK_EDIT);
  k = nth(d, 11, &s); r = sdt_get(d, k);
  ok("controls: picture", r && r->type == SDT_PICTURE);
  k = nth(d, 12, &s); r = sdt_get(d, k);
  ok("controls: group", r && r->type == SDT_GROUP && r->level == SL_BLOCK);
  {
    /* the legacy form fields */
    int f, nck = 0, ntx = 0, ndd = 0;
    for (f = 0; f < d->nfields; f++) { int kk = ff_kind(d, f); nck += kk == FF_CHECK; ntx += kk == FF_TEXT; ndd += kk == FF_DROP; }
    ok("form fields: the three kinds", nck == 1 && ntx == 1 && ndd == 1);
    for (f = 0; f < d->nfields; f++) {
      if (ff_kind(d, f) == FF_CHECK) { ok("form fields: check box drawn unchecked", ff_box(d, f, t, sizeof t) && !strcmp(t, "\xE2\x98\x90") && field_code_shown(d, f)); }
      if (ff_kind(d, f) == FF_DROP) { ok("form fields: drop-down shows its entry", ff_box(d, f, t, sizeof t) && !strcmp(t, "Two") && ff_entries(d, f, 0, t, sizeof t) == 2); }
    }
  }
}

static void roundtrip(Doc *d, const char *name, int sample)
{
  size_t n1 = 0, n2 = 0;
  char *x1 = part_of(d, "word/document.xml", &n1), *x2, *h;
  Doc *e;
  size_t len;
  u8 *data = docx_save(d, &len);
  ok("controls: saved", x1 && data);
  if (sample) {
  ok("controls: every sdt written", count_of(x1, "<w:sdt ") == 14 && count_of(x1, "</w:sdt>") == 14);
  ok("controls: rPr, alias, tag, id, colour, appearance, lock, placeholder written", has(x1, "<w:sdtPr><w:rPr><w:b/></w:rPr><w:alias w:val=\"Full name\"/><w:tag w:val=\"name\"/><w:id w:val=\"-1543907611\"/><w15:color w:val=\"FF0000\"/><w15:appearance w15:val=\"tags\"/><w:lock w:val=\"sdtLocked\"/><w:placeholder><w:docPart w:val=\"DefaultPlaceholder_-1854013440\"/></w:placeholder><w:showingPlcHdr/>"));
  ok("controls: unknown child and its namespace written", has(x1, "<w16sdtdh:storeItemChecksum w16sdtdh:val=\"abc\"/>") && has(x1, "xmlns:w16sdtdh="));
  ok("controls: sdtEndPr written", has(x1, "<w:sdtEndPr><w:rPr><w:i/></w:rPr></w:sdtEndPr>"));
  ok("controls: data binding written", has(x1, "w:storeItemID=\"{11111111-2222-3333-4444-555555555555}\"/><w:text w:multiLine=\"1\"/>"));
  ok("controls: check box written", has(x1, "<w14:checkbox><w14:checked w14:val=\"1\"/><w14:checkedState w14:val=\"2612\" w14:font=\"MS Gothic\"/>"));
  ok("controls: combo box written", has(x1, "<w:comboBox w:lastValue=\"b\"><w:listItem w:displayText=\"Alpha\" w:value=\"a\"/>"));
  ok("controls: date written", has(x1, "<w:date w:fullDate=\"2026-10-29T00:00:00Z\"><w:dateFormat w:val=\"d MMMM yyyy\"/>"));
  ok("controls: block control round two paragraphs", has(x1, "<w:sdtContent><w:p>") && has(x1, "Block two</w:t></w:r></w:p></w:sdtContent></w:sdt>"));
  ok("controls: repeating section round a row", has(x1, "<w15:repeatingSection><w15:sectionTitle w:val=\"Lines\"/></w15:repeatingSection></w:sdtPr><w:sdtContent><w:sdt ") &&
                                                  has(x1, "<w15:repeatingSectionItem/></w:sdtPr><w:sdtContent><w:tr>"));
  ok("controls: cell control round a cell", has(x1, "<w:alias w:val=\"Cell\"/>") && has(x1, "<w:sdtContent><w:tc>"));
  ok("controls: block control in a cell", has(x1, "<w:lock w:val=\"contentLocked\"/></w:sdtPr><w:sdtContent><w:p>"));
  ok("controls: legacy form fields kept", has(x1, "FORMCHECKBOX") && has(x1, "<w:checkBox><w:sizeAuto/><w:default w:val=\"0\"/></w:checkBox>") && has(x1, "<w:listEntry w:val=\"Two\"/>"));
  h = part_of(d, "word/header1.xml", &n2);
  ok("controls: in a header", has(h, "<w:docPartGallery w:val=\"Page Numbers (Top of Page)\"/><w:docPartUnique/>") && has(h, "<w:sdtContent><w:p>"));
  free(h);
  {
    char *ci = part_of(d, "customXml/item1.xml", &n2);
    ok("controls: the bound custom XML kept", has(ci, "<city>Amsterdam</city>"));
    free(ci);
  }
  }
  e = load(data, len);
  ok("controls: read back", e != NULL);
  if (e) {
    x2 = part_of(e, "word/document.xml", &n2);
    ok("controls: a second save is the same", x2 && n1 == n2 && !memcmp(x1, x2, n1));
    if (!(x2 && n1 == n2 && !memcmp(x1, x2, n1)) && x2) { FILE *f = fopen("/tmp/wp16_a.xml", "wb"); if (f) { fwrite(x1, 1, n1, f); fclose(f); } f = fopen("/tmp/wp16_b.xml", "wb"); if (f) { fwrite(x2, 1, n2, f); fclose(f); } }
    free(x2);
    doc_free(e);
  }
  if (name && getenv("WP16_OUT")) {
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", getenv("WP16_OUT"), name);
    if ((f = fopen(path, "wb"))) { fwrite(data, 1, len, f); fclose(f); }
  }
  free(data);
  free(x1);
}

/* the other formats and PDF: controls are plain there (their content only, no marks, no frames);
   check boxes the font lacks drawn as boxes rather than question marks */
static void plain_elsewhere(Doc *d)
{
  static const char *const names[] = { "x.html", "x.md", "x.rtf", "x.odt", "x.txt" };
  int i;
  for (i = 0; i < 5; i++) {
    const DocFormat *df = doc_format_for_name(names[i], 1);
    size_t n = 0;
    u8 *out = df && df->save ? df->save(d, &n) : NULL;
    char what[80];
    snprintf(what, sizeof what, "plain elsewhere: %s keeps the content", names[i] + 2);
    if (i == 3) { ok(what, out && n > 0); free(out); continue; }      /* (.odt: zipped; its writer skips marks itself) */
    ok(what, out && memmem(out, n, "Amsterdam", 9) && memmem(out, n, "Block two", 9));
    snprintf(what, sizeof what, "plain elsewhere: %s has no control or field marks", names[i] + 2);
    ok(what, out && !memmem(out, n, "\xF4\x87", 2) && !memmem(out, n, "\xF4\x8B", 2) && !memmem(out, n, "\xF4\x84", 2) && !memmem(out, n, "\xF4\x88", 2));
    free(out);
  }
  {
    Layout *L = lay_new(d, &fs);
    size_t n = 0;
    u8 *pdf = L ? pdf_export(L, &n) : NULL, *page = NULL;
    const u8 *q;
    long got = 0;
    int boxes = 0, frames = 0;
    ok("pdf: the controls' document exports", pdf != NULL);
    for (q = pdf; pdf && q && (q = memmem(q, n - (size_t)(q - pdf), "/FlateDecode /Length ", 21)); q++) {
      long len = atol((const char *)q + 21);
      const u8 *st = memmem(q, n - (size_t)(q - pdf), "stream\n", 7);
      if (!page) page = (u8 *)malloc(262144);
      got = st && page && len > 6 ? zinflate(st + 7 + 2, (size_t)len - 6, page, 262143) : -1;
      if (got > 0) {
        char *c;
        page[got] = 0;
        for (c = (char *)page; (c = strstr(c, " re S\n")); c++) boxes++;
        for (c = (char *)page; (c = strstr(c, " re\n")); c++) frames++;     /* (the screen's frames would be rectangles too) */
        if (getenv("WP16_DEBUG")) printf("%s\n", (char *)page);
      }
    }
    /* the content control's checked box (a cross after it) and the form field's empty one */
    ok("pdf: check boxes drawn as boxes", boxes == 2);
    ok("pdf: no frames or titles round the controls", frames == 0 && !memmem(pdf, n, "Rich Text", 9));
    free(page); free(pdf);
    if (L) lay_free(L);
  }
}

/* a picture control: a picture in place of its placeholder, written inside the w:sdt */
static void picture(Doc *d)
{
  Pos s;
  size_t n, pn;
  u8 *png = file_read("tests/data/disc.png", &pn);
  int k = nth(d, 11, &s), img = png ? doc_add_image(d, png, (u32)pn, 0, 0) : -1;
  char t[256];
  ok("picture: a picture control", k >= 0 && sdt_get(d, k)->type == SDT_PICTURE && img >= 0);
  ok("picture: the picture set", img >= 0 && sdt_set_picture(d, s, img));
  nth(d, 11, &s);
  {
    Pos e;
    sdt_end(d, s, &e);
    ok("picture: its content is the picture", e.para == s.para && e.off == s.off + 8 && IS_PIC(d->p[s.para]->t + s.off + 4) && pic_index(d->p[s.para]->t + s.off + 4) == img);
  }
  {
    char *x = part_of(d, "word/document.xml", &n);
    const char *a = x ? strstr(x, "<w:picture/>") : NULL;
    ok("picture: written in the control", a && strstr(a, "<w:sdtContent>") && strstr(a, "<w:drawing>") && strstr(a, "<w:drawing>") < strstr(a, "</w:sdtContent>"));
    free(x);
  }
  doc_undo(d, &undo_at);
  nth(d, 11, &s);
  content(d, s, t, sizeof t);
  ok("picture: undone, the placeholder back", !strcmp(t, "Click or tap to insert a picture."));
}

static void edits(Doc *d)
{
  Pos s, e, s2;
  char t[256];
  int k, k2;
  Sdt *r;
  /* check box toggled, then undone */
  k = nth(d, 2, &s);
  ok("controls: check box toggles", sdt_toggle(d, s) && sdt_checked(d, s) == 0);
  content(d, s, t, sizeof t);
  ok("controls: its symbol", !strcmp(t, "\xE2\x98\x90"));
  ok("controls: typing in a check box refused", !doc_insert(d, &(Pos){ s.para, s.off + 4 }, "x", 1, NULL));
  doc_undo(d, &undo_at);
  ok("controls: the toggle undone", sdt_checked(d, s) == 1);
  /* drop-down chosen; combo box chosen */
  k = nth(d, 4, &s);
  ok("controls: drop-down entry chosen", sdt_choose(d, s, 2));
  content(d, s, t, sizeof t);
  ok("controls: its text", !strcmp(t, "Green"));
  {
    size_t n;
    char *x = part_of(d, "word/document.xml", &n);
    ok("controls: no placeholder once chosen, last value", x && has(x, "<w:dropDownList w:lastValue=\"Green\">") && count_of(x, "<w:showingPlcHdr/>") == 2);
    free(x);
  }
  ok("controls: back to the placeholder", sdt_choose(d, s, 0));
  {
    size_t n;
    char *x = part_of(d, "word/document.xml", &n);
    ok("controls: placeholder showing again", x && count_of(x, "<w:showingPlcHdr/>") == 3);
    free(x);
  }
  /* date */
  k = nth(d, 5, &s);
  ok("controls: date set", sdt_set_date(d, nth(d, 5, &s) >= 0 ? s : s, 2027, 2, 3));
  k = nth(d, 5, &s); r = sdt_get(d, k);
  content(d, s, t, sizeof t);
  ok("controls: date's text and value", !strcmp(t, "3 February 2027") && r && r->fulldate && !strcmp(r->fulldate, "2027-02-03T00:00:00Z"));
  /* locks: the rich text control can't be deleted; the content-locked cell can't be edited */
  k = nth(d, 0, &s);
  sdt_end(d, s, &e); e.off += 4;
  ok("controls: a control locked against deleting stays", !doc_delete(d, s, e));
  ok("controls: its content can be edited", sdt_set_text(d, s, "Ada Lovelace"));
  k = nth(d, 10, &s);
  sdt_end(d, s, &e);
  ok("controls: content locked", !doc_insert(d, &(Pos){ s.para, s.off + 4 }, "x", 1, NULL) && !doc_delete(d, (Pos){ s.para, s.off + 4 }, e));
  /* group: the group's own text is locked, the control inside isn't */
  k = nth(d, 12, &s);
  ok("controls: a group's text can't be typed in", !doc_insert(d, &(Pos){ s.para, s.off + 6 }, "x", 1, NULL));
  k2 = nth(d, 13, &s2);
  ok("controls: a control in a group can", doc_insert(d, &(Pos){ s2.para, s2.off + 5 }, "x", 1, NULL));
  ok("controls: ungroup", sdt_ungroup(d, (Pos){ s.para, s.off + 6 }) && doc_insert(d, &(Pos){ s.para, 2 }, "y", 1, NULL));
  (void)k2;
  /* forms: inside a control's content, a text form field's result; not elsewhere */
  k = nth(d, 1, &s);
  ok("forms: a control's content is fillable", sdt_forms_ok(d, (Pos){ s.para, s.off + 4 }, (Pos){ s.para, s.off + 6 }));
  ok("forms: text outside isn't", !sdt_forms_ok(d, (Pos){ 0, 0 }, (Pos){ 0, 2 }));
  {
    int f;
    u32 pi;
    Pos fs, fe;
    for (pi = 0; pi < d->np; pi++) if (strstr(d->p[pi]->t, "\xE2\x80\x82")) break;
    f = pi < d->np ? field_next(d, (Pos){ pi, 0 }, 1, &fs, &fe) : -1;
    while (f >= 0 && ff_kind(d, f) != FF_TEXT) { Pos nx = fs; nx.off += 4; f = field_next(d, nx, 1, &fs, &fe); }
    ok("forms: a text form field's result is fillable", f >= 0 && sdt_forms_ok(d, (Pos){ fs.para, fs.off + 4 }, fe));
    /* the legacy check box toggled */
    f = field_next(d, (Pos){ pi, 0 }, 1, &fs, &fe);
    while (f >= 0 && ff_kind(d, f) != FF_CHECK) { Pos nx = fs; nx.off += 4; f = field_next(d, nx, 1, &fs, &fe); }
    ok("form fields: check box toggles", f >= 0 && ff_toggle(d, fs) && ff_checked(d, fmark_index(d->p[fs.para]->t + fs.off)));
    f = fmark_index(d->p[fs.para]->t + fs.off);
    ok("form fields: its ffData", has(d->fields[f].ffdata, "<w:default w:val=\"0\"/><w:checked/></w:checkBox>"));
    ok("form fields: toggled back", ff_toggle(d, fs) && !ff_checked(d, fmark_index(d->p[fs.para]->t + fs.off)));
    f = field_next(d, (Pos){ pi, 0 }, 1, &fs, &fe);
    while (f >= 0 && ff_kind(d, f) != FF_DROP) { Pos nx = fs; nx.off += 4; f = field_next(d, nx, 1, &fs, &fe); }
    ok("form fields: drop-down entry chosen", f >= 0 && ff_choose(d, fs, 0) && ff_result(d, fmark_index(d->p[fs.para]->t + fs.off)) == 0);
  }
  /* the repeating section: an item added after the first, then taken out */
  {
    u32 np0 = d->np;
    Pos it, ni;
    int ri = nth(d, 8, &it);
    ok("repeating: an item added", ri >= 0 && sdt_item_add(d, it, 0, &ni) && d->np == np0 + 2);
    ok("repeating: the new row's text", d->np == np0 + 2 && strstr(d->p[ni.para]->t, "Apples") != NULL);
    ok("repeating: two items now", sdt_get(d, smark_index(d->p[ni.para]->t + ni.off))->type == SDT_REPITEM);
    {
      size_t n;
      char *x = part_of(d, "word/document.xml", &n);
      ok("repeating: written with two items", x && count_of(x, "<w15:repeatingSectionItem/>") == 2 && count_of(x, "<w15:repeatingSection>") == 1);
      free(x);
    }
    ok("repeating: an item removed", sdt_item_remove(d, ni) && d->np == np0);
    nth(d, 8, &it);
    ok("repeating: the last item stays", !sdt_item_remove(d, it));
  }
  ok("controls: runs sane after the edits", runs_sane(d));
}

static void inserting(void)
{
  Doc *d = doc_new();
  Pos a, s;
  int k, t;
  char tx[256];
  doc_insert(d, &(Pos){ 0, 0 }, "Hello world\nSecond", 18, NULL);
  for (t = SDT_RICH; t <= SDT_GALLERY; t++) {
    a.para = 1; a.off = d->p[1]->len;
    k = sdt_insert(d, a, a, t, 0);
    ok("insert: a control of each kind", k >= 0 && sdt_get(d, k)->type == t);
  }
  ok("insert: eight controls", sdt_all(d, &s, 0) == 8);
  nth(d, 3, &s);
  content(d, s, tx, sizeof tx);
  ok("insert: the check box's symbol", !strcmp(tx, "\xE2\x98\x90"));
  nth(d, 0, &s);
  content(d, s, tx, sizeof tx);
  ok("insert: the placeholder text", !strcmp(tx, "Click or tap here to enter text.") && d->p[s.para]->r[1].f.cstyle == doc_style(d, "PlaceholderText"));
  /* a block control round the first paragraph; a repeating section round both */
  k = sdt_insert(d, (Pos){ 0, 0 }, (Pos){ 0, d->p[0]->len }, SDT_RICH, 0);
  ok("insert: round a whole paragraph: block", k >= 0 && sdt_get(d, k)->level == SL_BLOCK);
  k = sdt_insert(d, (Pos){ 0, 0 }, (Pos){ 1, d->p[1]->len }, SDT_REPEAT, 0);
  ok("insert: a repeating section", k >= 0 && sdt_get(d, k)->level == SL_BLOCK);
  ok("insert: undo takes it out", doc_undo(d, &undo_at) && sdt_all(d, &s, 0) == 9);
  doc_redo(d, &undo_at);
  {
    Pos it;
    u32 np0 = d->np;
    int i;
    for (i = 0; i < 16; i++) { if (nth(d, i, &it) >= 0 && sdt_get(d, smark_index(d->p[it.para]->t + it.off))->type == SDT_REPITEM) break; }
    ok("insert: an item copied", i < 16 && sdt_item_add(d, it, 0, NULL) && d->np == np0 + 2);
    ok("insert: and one before", sdt_item_add(d, it, 1, NULL) && d->np == np0 + 4);
  }
  {
    unsigned lost;
    Doc *e = docx_again(d, &lost);
    ok("insert: the new controls read back", e && sdt_all(e, &s, 0) == sdt_all(d, &a, 0));
    ok("insert: the same text (marks name new records)", e && same_plain(d, e));
    if (e) roundtrip(e, NULL, 0), doc_free(e);
  }
  /* legacy form fields inserted */
  a.para = 0; a.off = 0;
  ok("insert: text form field", ff_insert(d, &a, FF_TEXT) >= 0);
  ok("insert: check box form field", ff_insert(d, &a, FF_CHECK) >= 0);
  ok("insert: drop-down form field", ff_insert(d, &a, FF_DROP) >= 0);
  ok("insert: runs sane", runs_sane(d));
  doc_free(d);
}

/* Properties: a new title, tag and locks written; typing in a control: the placeholder goes, and a
   temporary one ("remove when edited") goes with it, its text kept; a combo box takes typing, a
   drop-down list doesn't */
static void props_and_typing(void)
{
  Doc *d = doc_new();
  Pos s, e, at;
  Sdt nr;
  char t[256];
  int k;
  size_t n;
  doc_insert(d, &(Pos){ 0, 0 }, "Name: ", 6, NULL);
  k = sdt_insert(d, (Pos){ 0, 6 }, (Pos){ 0, 6 }, SDT_RICH, 0);
  nth(d, 0, &s);
  memset(&nr, 0, sizeof nr);
  sdt_copy(&nr, sdt_get(d, k));
  free(nr.alias); free(nr.tag);
  nr.alias = (char *)malloc(8); strcpy(nr.alias, "Surname");
  nr.tag = (char *)malloc(8); strcpy(nr.tag, "sname");
  nr.lock = SLK_DEL;
  ok("properties: changed", sdt_set_props(d, s, &nr));
  sdt_rec_free(&nr);
  nth(d, 0, &s);
  {
    char *x = part_of(d, "word/document.xml", &n);
    ok("properties: title, tag and lock written", has(x, "<w:alias w:val=\"Surname\"/><w:tag w:val=\"sname\"/>") && has(x, "<w:lock w:val=\"sdtLocked\"/>"));
    free(x);
  }
  doc_undo(d, &undo_at);
  nth(d, 0, &s);
  ok("properties: undone", sdt_get(d, smark_index(d->p[0]->t + s.off))->alias == NULL);
  /* typing over the placeholder */
  at = s; at.off += 6;
  ok("typing: the placeholder taken out", sdt_typing(d, &at) && at.para == 0 && at.off == s.off + 4 && sdt_end(d, s, &e) && e.off == s.off + 4);
  doc_insert(d, &at, "Ada", 3, NULL);
  content(d, s, t, sizeof t);
  ok("typing: the text in the control", !strcmp(t, "Ada"));
  {
    char *x = part_of(d, "word/document.xml", &n);
    ok("typing: no placeholder written now", x && !has(x, "<w:showingPlcHdr/>") && has(x, ">Ada</w:t>"));
    free(x);
  }
  /* a temporary control goes when typed in */
  k = sdt_insert(d, (Pos){ 0, d->p[0]->len }, (Pos){ 0, d->p[0]->len }, SDT_TEXT, 0);
  nth(d, 1, &s);
  memset(&nr, 0, sizeof nr);
  sdt_copy(&nr, sdt_get(d, k));
  nr.temporary = 1;
  sdt_set_props(d, s, &nr);
  sdt_rec_free(&nr);
  {
    char *x = part_of(d, "word/document.xml", &n);
    ok("temporary: written", has(x, "<w:temporary/>"));
    free(x);
  }
  nth(d, 1, &s);
  at = s; at.off += 4;
  ok("temporary: typed in, the control goes", sdt_typing(d, &at) && sdt_all(d, &s, 0) == 1 && at.off == d->p[0]->len);
  doc_insert(d, &at, " Lovelace", 9, NULL);
  ok("temporary: its text plain now", strstr(d->p[0]->t, "Ada") && !strcmp(d->p[0]->t + d->p[0]->len - 9, " Lovelace"));
  /* combo box and drop-down list */
  k = sdt_insert(d, (Pos){ 0, d->p[0]->len }, (Pos){ 0, d->p[0]->len }, SDT_COMBO, 0);
  nth(d, 1, &s);
  sdt_end(d, s, &e);
  ok("combo box: typing allowed", sdt_insert_ok(d, (Pos){ 0, s.off + 4 }));
  k = sdt_insert(d, (Pos){ 0, d->p[0]->len }, (Pos){ 0, d->p[0]->len }, SDT_DROP, 0);
  nth(d, 2, &s);
  at = s; at.off += 4;
  ok("drop-down list: typing refused", !sdt_insert_ok(d, at) && !sdt_typing(d, &at));
  ok("props and typing: runs sane", runs_sane(d));
  doc_free(d);
}

void test_wp16(void)
{
  size_t len;
  u8 *b = make_docx(sample_body, sample_hdr, sample_xml, &len);
  Doc *d = b ? load(b, len) : NULL;
  ok("controls: the sample reads", d != NULL);
  if (d) {
    reading(d);
    roundtrip(d, "wp16-sample.docx", 1);
    plain_elsewhere(d);
    picture(d);
    edits(d);
    {
      size_t n;
      char *x = part_of(d, "word/document.xml", &n);
      ok("controls: the edited document saves well formed", x && count_of(x, "<w:sdt ") == count_of(x, "</w:sdt>"));
      free(x);
    }
    if (getenv("WP16_OUT")) {
      char path[512];
      size_t l2;
      u8 *data = docx_save(d, &l2);
      FILE *f;
      snprintf(path, sizeof path, "%s/wp16-edited.docx", getenv("WP16_OUT"));
      if (data && (f = fopen(path, "wb"))) { fwrite(data, 1, l2, f); fclose(f); }
      free(data);
      snprintf(path, sizeof path, "%s/wp16-word.docx", getenv("WP16_OUT"));
      if ((f = fopen(path, "wb"))) { fwrite(b, 1, len, f); fclose(f); }
    }
    doc_free(d);
  }
  free(b);
  inserting();
  props_and_typing();
}
