/* test_wp1.c - the native tests of WP1 Fields (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp1() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/field.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/zip.h"
#include "../core/info.h"

/* a .docx of these body elements (and these docProps/core.xml contents, NULL none) read */
static Doc *from_body(const char *body, const char *core, const char *hdr)
{
  ZipWriter z;
  Buf doc;
  Doc *g = NULL;
  unsigned lost;
  char err[200];
  buf_init(&doc);
  buf_adds(&doc, "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                 "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>");
  buf_adds(&doc, body);
  if (hdr) buf_adds(&doc, "<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rIdH\"/></w:sectPr>");
  buf_adds(&doc, "</w:body></w:document>");
  zip_begin(&z);
  zip_add(&z, "word/document.xml", doc.data, doc.len);
  if (hdr) {
    static const char *rels = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rIdH\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header1.xml\"/></Relationships>";
    Buf h;
    buf_init(&h);
    buf_adds(&h, "<?xml version=\"1.0\"?><w:hdr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    buf_adds(&h, hdr);
    buf_adds(&h, "</w:hdr>");
    zip_add(&z, "word/_rels/document.xml.rels", rels, strlen(rels));
    zip_add(&z, "word/header1.xml", h.data, h.len);
    buf_free(&h);
  }
  if (core) {
    Buf c;
    buf_init(&c);
    buf_adds(&c, "<?xml version=\"1.0\"?><cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                 "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\">");
    buf_adds(&c, core);
    buf_adds(&c, "</cp:coreProperties>");
    zip_add(&z, "docProps/core.xml", c.data, c.len);
    {
      static const char *pr = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/></Relationships>";
      zip_add(&z, "_rels/.rels", pr, strlen(pr));
    }
    buf_free(&c);
  }
  if (zip_finish(&z)) g = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
  zip_free(&z); buf_free(&doc);
  return g;
}
/* the k'th field's code in the order the start marks come in the body ("" if none) */
static const char *code_n(Doc *d, int n, Pos *at)
{
  u32 pi, o;
  for (pi = 0; pi < d->np; pi++)
    for (o = 0; o + 4 <= d->p[pi]->len; o++)
      if (IS_FSTART(d->p[pi]->t + o) && n-- == 0) {
        const Field *f = field_get(d, fmark_index(d->p[pi]->t + o));
        if (at) { at->para = pi; at->off = o; }
        return f ? f->code : "";
      }
  return "";
}
/* the result text of the field whose start mark is at s */
static void result_of(Doc *d, Pos s, char *out, size_t cap)
{
  Pos e;
  if (!field_end(d, s, &e) || e.para != s.para) { snprintf(out, cap, "?"); return; }
  snprintf(out, cap, "%.*s", (int)(e.off - s.off - 4), d->p[s.para]->t + s.off + 4);
}
/* the text without field marks */
static char *plain(Doc *d)
{
  char *t = all(d), *r = t, *w = t;
  while (*r) { if (r[0] == '\xF4' && ((unsigned char)r[1] & 0xF8) == 0x88) { r += 4; continue; } *w++ = *r++; }
  *w = 0;
  return t;
}
static int count_in(const char *hay, size_t n, const char *needle)
{
  int c = 0;
  size_t l = strlen(needle), i;
  for (i = 0; i + l <= n; i++) if (!memcmp(hay + i, needle, l)) c++;
  return c;
}
/* document.xml of d saved */
static char *saved_xml(Doc *d, size_t *n)
{
  size_t len;
  u8 *data = docx_save(d, &len);
  ZipReader z;
  char *x = NULL;
  if (!data) return NULL;
  if (zip_open(&z, data, len)) { int i = zip_find(&z, "word/document.xml"); if (i >= 0) x = (char *)zip_read(&z, i, n); zip_close(&z); }
  free(data);
  return x;
}

#define R(t) "<w:r><w:t xml:space=\"preserve\">" t "</w:t></w:r>"
#define BEGIN "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r>"
#define INSTR(t) "<w:r><w:instrText xml:space=\"preserve\">" t "</w:instrText></w:r>"
#define SEP "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
#define END "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"

static void formats(void)
{
  FieldTime t;
  char b[256];
  memset(&t, 0, sizeof t);
  t.year = 2026; t.month = 10; t.day = 9; t.hour = 14; t.min = 5; t.sec = 7;
  ok("fields: a date picture's parse", field_parse_time("2026-10-09T14:05:07Z", &t) && t.wday == 5);
  field_format_date("dddd, d MMMM yyyy", &t, b, sizeof b);
  ok("fields: date picture dddd, d MMMM yyyy", !strcmp(b, "Friday, 9 October 2026"));
  field_format_date("M/d/yy h:mm am/pm", &t, b, sizeof b);
  ok("fields: date picture M/d/yy h:mm am/pm", !strcmp(b, "10/9/26 2:05 pm"));
  field_format_date("yyyy-MM-dd'T'HH:mm:ss ddd MMM", &t, b, sizeof b);
  ok("fields: date picture with a literal", !strcmp(b, "2026-10-09T14:05:07 Fri Oct"));
  field_format_date("dddd', 'd\\ MMMM\\ yyyy", &t, b, sizeof b);
  ok("fields: LibreOffice's date picture", !strcmp(b, "Friday, 9 October 2026"));
  field_format_number(" PAGE \\* roman ", 14, b, sizeof b); ok("fields: \\* roman", !strcmp(b, "xiv"));
  field_format_number(" PAGE \\* ROMAN ", 1999, b, sizeof b); ok("fields: \\* ROMAN", !strcmp(b, "MCMXCIX"));
  field_format_number(" SEQ x \\* alphabetic ", 28, b, sizeof b); ok("fields: \\* alphabetic", !strcmp(b, "bb"));
  field_format_number(" SEQ x \\* ALPHABETIC ", 3, b, sizeof b); ok("fields: \\* ALPHABETIC", !strcmp(b, "C"));
  field_format_number(" X \\* Ordinal ", 112, b, sizeof b); ok("fields: \\* Ordinal", !strcmp(b, "112th"));
  field_format_number(" X \\* Ordinal ", 23, b, sizeof b); ok("fields: \\* Ordinal 23rd", !strcmp(b, "23rd"));
  field_format_number(" X \\* CardText ", 123, b, sizeof b); ok("fields: \\* CardText", !strcmp(b, "one hundred twenty-three"));
  field_format_number(" X \\* OrdText ", 21, b, sizeof b); ok("fields: \\* OrdText", !strcmp(b, "twenty-first"));
  field_format_number(" X \\* OrdText ", 40, b, sizeof b); ok("fields: \\* OrdText fortieth", !strcmp(b, "fortieth"));
  field_format_number(" X \\* Hex ", 255, b, sizeof b); ok("fields: \\* Hex", !strcmp(b, "FF"));
  field_format_number(" X \\* ArabicDash ", 4, b, sizeof b); ok("fields: \\* ArabicDash", !strcmp(b, "- 4 -"));
  field_format_number(" X \\# \"000\" ", 7, b, sizeof b); ok("fields: \\# 000", !strcmp(b, "007"));
  field_format_number(" X \\# \"#,##0.00\" ", 1234567, b, sizeof b); ok("fields: \\# #,##0.00", !strcmp(b, "1,234,567.00"));
  field_format_number(" X \\# \"$#,##0;($#,##0)\" ", -1500, b, sizeof b); ok("fields: \\# negative section", !strcmp(b, "($1,500)"));
  field_format_number(" X \\# x## ", 12345, b, sizeof b); ok("fields: \\# x## drops digits", !strcmp(b, "345"));
  field_format_number(" X \\# 0 ", -12, b, sizeof b); ok("fields: \\# 0 negative", !strcmp(b, "-12"));
  snprintf(b, sizeof b, "the quick fox"); field_format_case(" X \\* Caps ", b, sizeof b); ok("fields: \\* Caps", !strcmp(b, "The Quick Fox"));
  snprintf(b, sizeof b, "the quick fox"); field_format_case(" X \\* FirstCap ", b, sizeof b); ok("fields: \\* FirstCap", !strcmp(b, "The quick fox"));
  snprintf(b, sizeof b, "Grüße"); field_format_case(" X \\* Upper ", b, sizeof b); ok("fields: \\* Upper (Latin-1 letters too)", !strcmp(b, "GRÜßE"));
  snprintf(b, sizeof b, "ABC"); field_format_case(" X \\* Lower \\* MERGEFORMAT ", b, sizeof b); ok("fields: \\* Lower with MERGEFORMAT", !strcmp(b, "abc"));
  ok("fields: switches and arguments", field_arg(" SEQ Figure \\* ARABIC \\s 1 ", 0, b, sizeof b) && !strcmp(b, "Figure") &&
     field_switch(" SEQ Figure \\* ARABIC \\s 1 ", 's', 0, b, sizeof b) && !strcmp(b, "1") &&
     field_switch(" DATE \\@ \"d MMM\" \\* MERGEFORMAT ", '*', 0, b, sizeof b) && !strcmp(b, "MERGEFORMAT") &&
     field_arg(" HYPERLINK \"http://x/\" \\l \"a b\" ", 0, b, sizeof b) && !strcmp(b, "http://x/") &&
     !field_arg(" PAGE ", 0, b, sizeof b));
  field_name("  mergefield Name ", b, sizeof b); ok("fields: a field's name", !strcmp(b, "MERGEFIELD"));
  {
    char *c = field_code_parse(" IF { MERGEFIELD Sex } = \"F\" \"Ms\" \"Mr\" ");
    char back[200];
    field_code_text(c ? c : "", back, sizeof back);
    ok("fields: a typed code with a nested field, and back", c && strchr(c, FLD_OPEN) && !strcmp(back, " IF { MERGEFIELD Sex } = \"F\" \"Ms\" \"Mr\" "));
    free(c);
  }
}

static void reading(void)
{
  static const char *body =
    "<w:p>" R("Today ") BEGIN INSTR(" DATE \\@ \"d MMMM yyyy\" ") SEP R("1 January 2020") END
      R(", fig. ") "<w:fldSimple w:instr=\" SEQ Figure \\* ARABIC \"><w:r><w:t>1</w:t></w:r></w:fldSimple>"
      R(" to ") BEGIN INSTR(" MERGEFIELD FirstName ") SEP R("\xC2\xAB" "FirstName" "\xC2\xBB") END "</w:p>"
    "<w:p>" BEGIN INSTR(" ADDIN ZOTERO_ITEM CSL_CITATION {\"citationID\":\"a1\",\"properties\":{\"formattedCitation\":\"(Smith 2020)\"},"
      "\"citationItems\":[{\"id\":7,\"uris\":[\"http://zotero.org/users/1/items/ABC\"]}]} ") SEP R("(Smith 2020)") END
      R(" and ") BEGIN INSTR(" IF ") BEGIN INSTR(" MERGEFIELD Sex ") SEP R("F") END INSTR(" = \"F\" \"Ms\" \"Mr\" ") SEP R("Ms") END
      R(" entry") BEGIN INSTR(" XE \"Smith\" ") END
      "<w:r><w:fldChar w:fldCharType=\"begin\" w:fldLock=\"true\" w:dirty=\"true\"/></w:r>" INSTR(" DOCPROPERTY Company ") SEP R("Acme") END "</w:p>"
    "<w:p>" "<w:r><w:fldChar w:fldCharType=\"begin\"><w:ffData><w:name w:val=\"Check1\"/><w:enabled/><w:checkBox><w:sizeAuto/><w:default w:val=\"1\"/></w:checkBox></w:ffData></w:fldChar></w:r>"
      INSTR(" FORMCHECKBOX ") SEP R("X") END
      R(" p. ") BEGIN INSTR(" PAGE \\* roman ") SEP R("iv") END R(" of ") BEGIN INSTR(" NUMPAGES ") SEP R("9") END
      R(" sec ") BEGIN INSTR(" SECTIONPAGES \\* Arabic ") SEP R("3") END "</w:p>"
    "<w:p>" BEGIN INSTR(" ADDIN ZOTERO_BIBL {\"uncited\":[]} CSL_BIBLIOGRAPHY ") SEP R("Smith, J. (2020). A book.") "</w:p>"
    "<w:p>" R("Jones, K. (2019). Another.") END "</w:p>";
  Doc *g = from_body(body, NULL, NULL), *h;
  Pos at;
  char res[256];
  size_t n;
  char *x;
  ok("fields: read from .docx", g != NULL && g->np == 5);
  if (!g) return;
  ok("fields: DATE kept with code and result", !strcmp(code_n(g, 0, &at), " DATE \\@ \"d MMMM yyyy\" ") && (result_of(g, at, res, sizeof res), !strcmp(res, "1 January 2020")));
  ok("fields: fldSimple SEQ kept", !strcmp(code_n(g, 1, &at), " SEQ Figure \\* ARABIC ") && (result_of(g, at, res, sizeof res), !strcmp(res, "1")));
  ok("fields: MERGEFIELD kept", !strcmp(code_n(g, 2, &at), " MERGEFIELD FirstName "));
  ok("fields: Zotero's ADDIN with its whole code", strstr(code_n(g, 3, &at), "ZOTERO_ITEM CSL_CITATION") && strstr(code_n(g, 3, NULL), "items/ABC\"]}]} ") &&
     (result_of(g, at, res, sizeof res), !strcmp(res, "(Smith 2020)")));
  {
    const char *c = code_n(g, 4, &at);
    char shown[200];
    field_code_text(c, shown, sizeof shown);
    ok("fields: IF with a MERGEFIELD nested in its code", !strcmp(shown, " IF { MERGEFIELD Sex } = \"F\" \"Ms\" \"Mr\" ") && (result_of(g, at, res, sizeof res), !strcmp(res, "Ms")));
  }
  ok("fields: XE without a result", !strcmp(code_n(g, 5, &at), " XE \"Smith\" ") && field_get(g, fmark_index(g->p[at.para]->t + at.off))->nosep &&
     (result_of(g, at, res, sizeof res), !res[0]));
  {
    const Field *f;
    code_n(g, 6, &at);
    f = field_get(g, fmark_index(g->p[at.para]->t + at.off));
    ok("fields: locked and dirty read", f && f->locked && f->dirty && !strcmp(f->code, " DOCPROPERTY Company "));
    code_n(g, 7, &at);
    f = field_get(g, fmark_index(g->p[at.para]->t + at.off));
    ok("fields: a form field's ffData kept", f && f->ffdata && strstr(f->ffdata, "<w:checkBox>") && strstr(f->ffdata, "Check1"));
  }
  ok("fields: PAGE \\* roman becomes a placeholder in its marks", !strcmp(code_n(g, 8, &at), " PAGE \\* roman ") && (result_of(g, at, res, sizeof res), !strcmp(res, FIELD_PAGE)));
  ok("fields: plain NUMPAGES stays a bare placeholder", strstr(g->p[2]->t, " of " FIELD_PAGES) != NULL);
  ok("fields: SECTIONPAGES a placeholder in its marks", !strcmp(code_n(g, 9, &at), " SECTIONPAGES \\* Arabic ") && (result_of(g, at, res, sizeof res), !strcmp(res, FIELD_PAGES)));
  {
    Pos e;
    code_n(g, 10, &at);
    ok("fields: a bibliography's result over two paragraphs", strstr(code_n(g, 10, NULL), "ZOTERO_BIBL") && at.para == 3 && field_end(g, at, &e) && e.para == 4);
  }
  {
    char *t = plain(g);
    ok("fields: the results read as the text", strstr(t, "Today 1 January 2020, fig. 1 to \xC2\xAB" "FirstName\xC2\xBB") && strstr(t, "(Smith 2020) and Ms entryAcme"));
    free(t);
  }
  /* written back and read again: the same codes, results and settings */
  x = saved_xml(g, &n);
  ok("fields: written as complex fields", x && count_in(x, n, "w:fldCharType=\"begin\"") >= 12 && count_in(x, n, "ZOTERO_ITEM CSL_CITATION") == 1 &&
     count_in(x, n, "w:fldLock=\"true\"") == 1 && count_in(x, n, "<w:ffData>") == 1 && count_in(x, n, "MERGEFIELD Sex") == 1 &&
     count_in(x, n, "w:fldCharType=\"begin\"") == count_in(x, n, "w:fldCharType=\"end\""));
  free(x);
  h = docx_again(g, NULL);
  {
    int i, same = h != NULL;
    for (i = 0; i < 11 && same; i++) {
      Pos a1, a2;
      char r1[256], r2[256];
      if (strcmp(code_n(g, i, &a1), code_n(h, i, &a2))) same = 0;
      result_of(g, a1, r1, sizeof r1); result_of(h, a2, r2, sizeof r2);
      if (strcmp(r1, r2)) same = 0;
    }
    { char *ga = all(g); ok("fields: a save and a read keep every code and result", same && text_is(h, ga) && runs_sane(h)); free(ga); }
    if (h) {
      const Field *f;
      code_n(h, 5, &at);
      f = field_get(h, fmark_index(h->p[at.para]->t + at.off));
      ok("fields: XE still without a separator after a save", f && f->nosep);
      code_n(h, 6, &at);
      f = field_get(h, fmark_index(h->p[at.para]->t + at.off));
      ok("fields: lock and form data after a save", f && f->locked && f->dirty && field_get(h, fmark_index(h->p[2]->t + 0)) && field_get(h, fmark_index(h->p[2]->t))->ffdata);
    }
  }
  doc_free(h);
  doc_free(g);
}

static void evaluating(void)
{
  static const char *core = "<dc:title>Report</dc:title><dc:creator>Ada Lovelace</dc:creator><dc:subject>Engines</dc:subject>"
    "<cp:keywords>maths</cp:keywords><cp:lastModifiedBy>Charles</cp:lastModifiedBy>"
    "<dcterms:created>2025-03-04T08:09:10Z</dcterms:created><dcterms:modified>2026-01-02T13:14:15Z</dcterms:modified><cp:revision>7</cp:revision>";
  static const char *body =
    "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>" R("One") "</w:p>"
    "<w:p>" R("Figure ") BEGIN INSTR(" SEQ Figure \\* ARABIC ") SEP R("9") END R(" Table ") BEGIN INSTR(" SEQ Table \\* ROMAN ") SEP R("9") END "</w:p>"
    "<w:p>" R("Figure ") BEGIN INSTR(" SEQ Figure \\* ARABIC ") SEP R("9") END R(" again ") BEGIN INSTR(" SEQ Figure \\c ") SEP R("9") END "</w:p>"
    "<w:p>" BEGIN INSTR(" TITLE \\* Upper ") SEP R("x") END R("|") BEGIN INSTR(" AUTHOR ") SEP R("x") END R("|") BEGIN INSTR(" SUBJECT ") SEP R("x") END
      R("|") BEGIN INSTR(" CREATEDATE \\@ \"yyyy-MM-dd\" ") SEP R("x") END R("|") BEGIN INSTR(" SAVEDATE \\@ \"d MMM yyyy HH:mm\" ") SEP R("x") END
      R("|") BEGIN INSTR(" DOCPROPERTY Keywords ") SEP R("x") END R("|") BEGIN INSTR(" LASTSAVEDBY ") SEP R("x") END R("|") BEGIN INSTR(" REVNUM ") SEP R("x") END
      R("|") BEGIN INSTR(" DATE \\@ \"dddd d MMMM yyyy\" ") SEP R("x") END R("|") BEGIN INSTR(" TIME \\@ \"HH:mm\" ") SEP R("x") END
      R("|") BEGIN INSTR(" FILENAME ") SEP R("x") END R("|") BEGIN INSTR(" NUMWORDS ") SEP R("x") END
      R("|") BEGIN INSTR(" IF ") BEGIN INSTR(" MERGEFIELD Sex ") SEP R("F") END INSTR(" = \"F\" \"Ms\" \"Mr\" ") SEP R("x") END
      R("|") BEGIN INSTR(" QUOTE \"hello\" \\* Caps ") SEP R("x") END R("|") BEGIN INSTR(" SYMBOL 169 ") SEP R("x") END
      R("|") "<w:r><w:fldChar w:fldCharType=\"begin\" w:fldLock=\"1\"/></w:r>" INSTR(" DATE ") SEP R("locked") END
      R("|") BEGIN INSTR(" MERGEFIELD City \\b \"in \" ") SEP R("x") END
      R("|") BEGIN INSTR(" ADDIN EN.CITE <EndNote/> ") SEP R("(kept)") END "</w:p>"
    "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>" R("Two") "</w:p>"
    "<w:p>" R("Figure ") BEGIN INSTR(" SEQ Figure \\s 1 ") SEP R("9") END "</w:p>";
  Doc *g = from_body(body, core, NULL);
  FieldEnv e;
  Pos at;
  char res[256];
  int n;
  ok("fields: a document to evaluate", g != NULL);
  if (!g) return;
  field_env(&e);
  e.now.year = 2026; e.now.month = 10; e.now.day = 29; e.now.hour = 9; e.now.min = 30; e.now.sec = 0; e.now.wday = 4;
  e.filename = "Work:Docs/report.docx";
  n = field_update(g, doc_start(g), doc_end_pos(g), &e);
  ok("fields: Update Fields changes them", n >= 15);
  code_n(g, 0, &at); result_of(g, at, res, sizeof res); ok("fields: SEQ Figure 1", !strcmp(res, "1"));
  code_n(g, 1, &at); result_of(g, at, res, sizeof res); ok("fields: SEQ Table in ROMAN", !strcmp(res, "I"));
  code_n(g, 2, &at); result_of(g, at, res, sizeof res); ok("fields: SEQ Figure 2", !strcmp(res, "2"));
  code_n(g, 3, &at); result_of(g, at, res, sizeof res); ok("fields: SEQ \\c repeats", !strcmp(res, "2"));
  {
    char *t = plain(g);
    char *p = strstr(t, "REPORT|");
    ok("fields: the document's properties and dates", p && !strncmp(p, "REPORT|Ada Lovelace|Engines|2025-03-04|2 Jan 2026 13:14|maths|Charles|7|Thursday 29 October 2026|09:30|report.docx|", 106));
    ok("fields: IF with a nested merge field, QUOTE, SYMBOL, a locked field, merge field without data, ADDIN kept",
       p && strstr(p, "|Ms|Hello|\xC2\xA9|locked|\xC2\xAB" "City\xC2\xBB|(kept)"));
    free(t);
  }
  code_n(g, 22, &at); result_of(g, at, res, sizeof res); ok("fields: SEQ \\s 1 starts again after a heading 1", !strcmp(res, "1"));
  ok("fields: one undo step takes the update back", doc_undo(g, &at) && (code_n(g, 0, &at), result_of(g, at, res, sizeof res), !strcmp(res, "9")));
  doc_redo(g, &at);
  /* NUMWORDS counted */
  {
    char *t = plain(g);
    ok("fields: NUMWORDS", strstr(t, "|report.docx|") && strstr(t, "report.docx|") != NULL);
    free(t);
  }
  /* a mail merge's values through FieldEnv.merge */
  doc_free(g);
}
static int merge_cb(void *ctx, const char *name, char *out, size_t cap)
{
  (void)ctx;
  if (!strcmp(name, "City")) { snprintf(out, cap, "Paris"); return 1; }
  if (!strcmp(name, "Sex")) { snprintf(out, cap, "M"); return 1; }
  return 0;
}

static void editing(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d), s, e, at;
  FieldEnv env;
  int k, k2;
  char res[256];
  Layout *L;
  field_env(&env);
  env.now.year = 2026; env.now.month = 10; env.now.day = 29; env.now.wday = 4;
  doc_insert(d, &c, "Dated ", 6, NULL);
  k = field_insert(d, &c, " DATE \\@ \"d MMMM yyyy\" ", &env, NULL);
  doc_insert(d, &c, " by ", 4, NULL);
  k2 = field_insert(d, &c, " MERGEFIELD City \\* Upper ", &env, NULL);
  doc_insert(d, &c, ".", 1, NULL);
  {
    char *t = plain(d);
    ok("fields: Insert Field puts in its result", k >= 0 && k2 > k && !strcmp(t, "Dated 29 October 2026 by \xC2\xAB" "City\xC2\xBB."));
    free(t);
  }
  env.merge = merge_cb;
  field_update(d, doc_start(d), doc_end_pos(d), &env);
  { char *t = plain(d); ok("fields: a merge field's value from FieldEnv.merge", !strcmp(t, "Dated 29 October 2026 by PARIS.")); free(t); }
  at.para = 0; at.off = 12;
  ok("fields: the field round a position", field_at(d, at, &s, &e) == k && s.off == 6 && e.off == 6 + 4 + 15);
  at.off = 2;
  ok("fields: no field round plain text", field_at(d, at, NULL, NULL) < 0);
  ok("fields: next and previous field", field_next(d, doc_start(d), 1, &s, &e) == k && field_next(d, e, 1, &s, &e) == k2 &&
     field_next(d, s, -1, &s, &e) == k && field_next(d, s, -1, NULL, NULL) < 0);
  /* lock: Update leaves it alone; unlock again */
  field_next(d, doc_start(d), 1, &s, &e);
  ok("fields: Lock makes a new record", field_lock(d, s, s, 1) == 1 && field_at(d, s, NULL, NULL) != k && field_get(d, field_at(d, s, NULL, NULL))->locked);
  env.now.day = 30;
  field_update(d, doc_start(d), doc_end_pos(d), &env);
  { char *t = plain(d); ok("fields: a locked field isn't updated", strstr(t, "29 October") != NULL); free(t); }
  field_lock(d, s, s, 0);
  field_update(d, doc_start(d), doc_end_pos(d), &env);
  { char *t = plain(d); ok("fields: unlocked, it is", strstr(t, "30 October") != NULL); free(t); }
  /* edit the code */
  field_next(d, doc_start(d), 1, &s, &e);
  ok("fields: Edit Field Code", field_set_code(d, s, " DATE \\@ \"yyyy\" ") && !strcmp(field_get(d, field_at(d, s, NULL, NULL))->code, " DATE \\@ \"yyyy\" "));
  field_update(d, s, s, &env);
  result_of(d, s, res, sizeof res);
  ok("fields: updated with its new code", !strcmp(res, "2026"));
  /* codes shown: the layout shows the code instead of the result */
  L = lay_new(d, &fs);
  field_show_codes(d, 1);
  lay_update(L);
  {
    ParaLay *pl = &L->pl[0];
    u32 i;
    char num[LAY_FIELD_TEXT] = "";
    int hidden = 1;
    for (i = 0; i < pl->nboxes; i++) {
      if (pl->boxes[i].kind == BX_FIELD && !num[0]) lay_field_text(L, d->p[0], &pl->boxes[i], 0, num);
      if (pl->boxes[i].kind == BX_TEXT && !memcmp(d->p[0]->t + pl->boxes[i].off, "2026", 4)) hidden = 0;
    }
    ok("fields: Show Field Codes shows { code } and hides the result", !strcmp(num, "{ DATE \\@ \"yyyy\" }") && hidden);
  }
  {
    size_t n;
    u8 *pdf = pdf_export(L, &n);
    ok("fields: the PDF shows the results, the screen still the codes", pdf && d->fieldcodes == 1);
    free(pdf);
  }
  field_show_codes(d, 0);
  ok("fields: Toggle Field Codes on one field", field_toggle(d, s, s) == 1 && field_code_shown(d, field_at(d, s, NULL, NULL)) && !field_code_shown(d, k2));
  field_toggle(d, s, s);
  lay_free(L);
  /* unlink: the result stays as text */
  ok("fields: Unlink", field_unlink(d, doc_start(d), doc_end_pos(d), &env) == 2);
  { char *t = all(d); ok("fields: unlinked results are plain text", !strcmp(t, "Dated 2026 by PARIS.") && field_next(d, doc_start(d), 1, NULL, NULL) < 0); free(t); }
  ok("fields: Unlink undone", doc_undo(d, &at) && field_next(d, doc_start(d), 1, NULL, NULL) >= 0);
  /* a page field in a footer, numbered its own way */
  {
    Doc *g = doc_new();
    Pos p = doc_start(g);
    Para *q;
    char num[LAY_FIELD_TEXT];
    Box b;
    int i;
    for (i = 0; i < 3; i++) { ParaFmt pf; doc_insert(g, &p, "Page\n", 5, NULL); memset(&pf, 0, sizeof pf); pf.pagebreak = 1; doc_para_format(g, p.para, p.para, PM_BREAK, &pf); }
    doc_story_begin(g, SK_FOOTER, HF_DEFAULT, 0, 0, 1);
    p = doc_start(g);
    field_insert(g, &p, " PAGE \\* roman ", &env, NULL);
    doc_insert(g, &p, "/", 1, NULL);
    field_insert(g, &p, " NUMPAGES \\* CardText \\* Caps ", &env, NULL);
    doc_story_end(g, 1);
    L = lay_new(g, &fs); lay_update(L);
    q = g->ftr[HF_DEFAULT].p[0];
    memset(&b, 0, sizeof b);
    b.off = 4; b.f = q->r[0].f;
    {
      Furn f;
      char got[64] = "";
      if (lay_furniture(L, 2, 1, &f)) { snprintf(got, sizeof got, "%s", f.f[0].p->t); lay_furniture_free(&f); }
      {
        char *w = got, *r = got;
        while (*r) { if ((unsigned char)*r == 0xF4) { r += 4; continue; } *w++ = *r++; }
        *w = 0;
      }
      ok("fields: PAGE \\* roman and NUMPAGES \\* CardText in a footer", L->npages == 4 && !strcmp(got, "iii/Four"));
    }
    (void)num;
    lay_free(L);
    {
      Doc *h = docx_again(g, NULL);
      Para *hq = h && h->ftr[HF_DEFAULT].np ? h->ftr[HF_DEFAULT].p[0] : NULL;
      ok("fields: footer fields saved and read back", hq && hq->len == q->len && !memcmp(hq->t, q->t, q->len));
      doc_free(h);
    }
    doc_free(g);
  }
  doc_free(d);
}

static void saving(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d), s, e;
  FieldEnv env;
  size_t n;
  char *x;
  field_env(&env);
  doc_insert(d, &c, "A ", 2, NULL);
  field_insert_result(d, &c, " REF _Ref9 \\h ", "Chapter", &env, NULL);
  doc_insert(d, &c, " B ", 3, NULL);
  field_insert_result(d, &c, " SEQ Table ", "4", &env, NULL);
  /* a mark lost (its end deleted alone): the field is left out, its result kept */
  field_next(d, doc_start(d), 1, &s, &e);
  field_next(d, e, 1, &s, &e);
  { Pos e2 = e; e2.off += 4; doc_delete(d, e, e2); }
  x = saved_xml(d, &n);
  ok("fields: a field missing its end mark is left out on saving", x && count_in(x, n, "SEQ Table") == 0 && count_in(x, n, "REF _Ref9") == 1 &&
     count_in(x, n, "w:fldCharType=\"begin\"") == count_in(x, n, "w:fldCharType=\"end\"") && strstr(x, ">4<"));
  ok("fields: no hyperlink round a REF field's result", x && !strstr(x, "w:hyperlink"));
  free(x);
  /* paste within the document shares the record; into another, only the text */
  {
    Fragment *f = doc_fragment(d, doc_start(d), doc_end_pos(d));
    Doc *o = doc_new();
    Pos p = doc_end_pos(d), q = doc_start(o);
    doc_paste(d, &p, f, PASTE_KEEP);
    doc_paste(o, &q, f, PASTE_KEEP);
    ok("fields: pasted in the same document they stay fields", field_next(d, doc_start(d), 1, &s, &e) >= 0 && field_next(d, e, 1, &s, &e) >= 0);
    ok("fields: pasted into another document only their text", field_next(o, doc_start(o), 1, NULL, NULL) < 0 && strstr(o->p[0]->t, "Chapter"));
    fragment_free(f); doc_free(o);
  }
  /* the TOC keeps its own code */
  {
    static const char *body = "<w:p><w:pPr><w:pStyle w:val=\"TOC1\"/></w:pPr>" BEGIN INSTR(" TOC \\o \"1-4\" \\h \\z \\t \"Title,1\" ") SEP R("Intro\t1") "</w:p>"
                              "<w:p><w:pPr><w:pStyle w:val=\"TOC1\"/></w:pPr>" R("More\t2") END "</w:p>";
    Doc *g = from_body(body, NULL, NULL);
    ok("fields: the TOC's code is kept", g && g->finfo && g->finfo->toccode && strstr(g->finfo->toccode, "\\o \"1-4\""));
    doc_free(g);
  }
  /* properties written back into core.xml, settings' updateFields */
  {
    Doc *g = from_body("<w:p>" R("x") "</w:p>", "<dc:subject>S1</dc:subject><cp:keywords>K1</cp:keywords><dcterms:created>2020-01-02T03:04:05Z</dcterms:created>", NULL), *h;
    if (g) { field_info(g)->updatefields = 1; }
    h = g ? docx_again(g, NULL) : NULL;
    ok("fields: core properties (WP24's) and updateFields through a save", h && h->finfo && !strcmp(info_str(h, IP_SUBJECT), "S1") && !strcmp(info_str(h, IP_KEYWORDS), "K1") &&
       !strcmp(info_str(h, IP_CREATED), "2020-01-02T03:04:05Z") && h->finfo->updatefields);
    doc_free(g); doc_free(h);
  }
  /* fields in a header read and written */
  {
    Doc *g = from_body("<w:p>" R("body") "</w:p>", NULL, "<w:p>" BEGIN INSTR(" STYLEREF \"Heading 1\" ") SEP R("Chapter") END R(" ") BEGIN INSTR(" PAGE ") SEP R("1") END "</w:p>"), *h;
    Para *q = g && g->hdr[HF_DEFAULT].np ? g->hdr[HF_DEFAULT].p[0] : NULL;
    ok("fields: a header's STYLEREF kept, PAGE a bare placeholder", q && IS_FSTART(q->t) && strstr(q->t, "Chapter") && strstr(q->t, " " FIELD_PAGE));
    h = g ? docx_again(g, NULL) : NULL;
    ok("fields: and through a save", h && h->hdr[HF_DEFAULT].np && q && h->hdr[HF_DEFAULT].p[0]->len == q->len);
    if (g) {
      /* Update Fields reaches the header too */
      FieldEnv e;
      Pos p0;
      field_env(&e);
      e.now.year = 2030; e.now.month = 1; e.now.day = 2;
      doc_story_begin(g, SK_HEADER, HF_DEFAULT, 0, 0, 1);
      p0 = doc_end_pos(g);
      field_insert_result(g, &p0, " DATE \\@ \"yyyy\" ", "1999", &e, NULL);
      doc_story_end(g, 1);
      q = g->hdr[HF_DEFAULT].np ? g->hdr[HF_DEFAULT].p[0] : NULL;
      ok("fields: Update Fields updates a header's DATE", field_update_all(g, &e) >= 1 && q != g->hdr[HF_DEFAULT].p[0] && strstr(g->hdr[HF_DEFAULT].p[0]->t, "2030") &&
         strstr(g->hdr[HF_DEFAULT].p[0]->t, "Chapter"));
    }
    doc_free(g); doc_free(h);
  }
  doc_free(d);
}

void test_wp1(void)
{
  formats();
  reading();
  evaluating();
  editing();
  saving();
}
