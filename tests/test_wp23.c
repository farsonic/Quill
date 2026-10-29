/* test_wp23.c - the native tests of WP23 Templates, building blocks, inserting files (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp23() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/blocks.h"
#include "../core/keep.h"
#include "../core/zip.h"
#include "../core/info.h"
#include "../core/doctheme.h"

#define TMPDIR "/tmp/"
static char *part23(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  size_t l = 0;
  u8 *b;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), &l);
  zip_close(&z);
  return (char *)b;
}
static char *file_part(const char *path, const char *name)
{
  size_t n;
  u8 *f = file_read(path, &n);
  char *p = f ? part23(f, n, name) : NULL;
  free(f);
  return p;
}
static void dump23(Doc *d, const char *name)
{
  const char *dir = getenv("WP23_OUT");
  char path[400];
  if (!dir) return;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  doc_save_file(d, path);
}
static Doc *with_text(const char *t)
{
  Doc *d = doc_new();
  Pos at = P(0, 0);
  doc_insert(d, &at, t, (u32)strlen(t), NULL);
  return d;
}

/* building blocks: saved from a selection, kept in the file, put in elsewhere */
static void blocks(void)
{
  Doc *d = with_text("Yours sincerely,\nA. Writer"), *e, *f;
  CharFmt c;
  int k, j;
  unsigned lost;
  u8 *z;
  size_t n;
  char *x;
  memset(&c, 0, sizeof c); c.flags = CF_BOLD;
  doc_format(d, P(1, 0), P(1, 9), CM_BOLD, &c);
  k = blocks_add(d, d, P(0, 0), P(1, 9), "Signature", GAL_QUICKPARTS, "Letters", "How I sign", BB_PARA);
  ok("blocks: saved from the selection", k == 0 && d->gloss && d->gloss->n == 1 && !strcmp(d->gloss->b[0].name, "Signature") &&
     !strcmp(d->gloss->b[0].gallery, "docParts") && !strcmp(d->gloss->b[0].category, "Letters") && d->gloss->b[0].behavior == BB_PARA);
  j = blocks_add(d, d, P(1, 0), P(1, 9), "aw", GAL_AUTOTEXT, NULL, NULL, BB_CONTENT);
  ok("blocks: AutoText too", j == 1 && blocks_find(d, "AW", GAL_AUTOTEXT) == 1 && blocks_find(d, "aw", GAL_QUICKPARTS) < 0);
  z = docx_save(d, &n);
  x = z ? part23(z, n, "word/glossary/document.xml") : NULL;
  ok("blocks: the glossary written", x && strstr(x, "<w:docPart><w:docPartPr><w:name w:val=\"Signature\"/>") && strstr(x, "<w:gallery w:val=\"autoTxt\"/>") &&
     strstr(x, "Yours sincerely,") && strstr(x, "<w:b/>"));
  free(x);
  x = z ? part23(z, n, "[Content_Types].xml") : NULL;
  ok("blocks: its content type", x && strstr(x, "document.glossary+xml") && strstr(x, "/word/glossary/styles.xml"));
  free(x);
  x = z ? part23(z, n, "word/_rels/document.xml.rels") : NULL;
  ok("blocks: its relationship", x && strstr(x, "relationships/glossaryDocument\" Target=\"glossary/document.xml\""));
  free(x);
  free(z);
  dump23(d, "blocks.docx");
  e = docx_again(d, &lost);
  ok("blocks: read back", e && e->gloss && e->gloss->n == 2 && blocks_find(e, "Signature", GAL_QUICKPARTS) == 0 && !strcmp(e->gloss->b[0].desc, "How I sign"));
  /* put into another document */
  f = with_text("Dear Sir,");
  if (e) {
    Pos at = doc_end_pos(f);
    int r = blocks_insert(f, &at, e, 0);
    char *t = all(f);
    ok("blocks: inserted as paragraphs", r && t && !strncmp(t, "Dear Sir,\nYours sincerely,\nA. Writer", 36));
    ok("blocks: with their formatting", f->np >= 3 && (para_fmt_at(f->p[2], 0)->flags & CF_BOLD));
    free(t);
    at = doc_end_pos(f);
    r = blocks_insert(f, &at, e, blocks_find(e, "aw", GAL_AUTOTEXT));
    t = all(f);
    ok("blocks: AutoText in the text", r && t && strstr(t, "A. Writer") && f->p[f->np - 1]->len >= 9);
    free(t);
    doc_undo(f, &at);
    t = all(f);
    ok("blocks: one undo step", t && !strstr(t + 30, "A. WriterA. Writer"));
    free(t);
    /* changed and deleted */
    ok("blocks: properties changed", blocks_set(e, 0, "Sig", -1, "General", "", BB_CONTENT) && !strcmp(e->gloss->b[0].name, "Sig") &&
       e->gloss->b[0].behavior == BB_CONTENT && blocks_find(e, "Signature", -1) < 0);
    {
      Doc *g = docx_again(e, &lost);
      Doc *h = g ? blocks_doc(g, 0) : NULL;
      char *ht = h ? all(h) : NULL;
      ok("blocks: changed block read back with its content", g && g->gloss && blocks_find(g, "Sig", GAL_QUICKPARTS) == 0 && ht && strstr(ht, "Yours sincerely,"));
      free(ht); doc_free(h);
      ok("blocks: deleted", g && blocks_delete(g, 0) && g->gloss->n == 1);
      doc_free(g);
    }
  }
  doc_free(f);
  doc_free(e);
  doc_free(d);
}

/* a glossary as Word writes it: other namespaces, a picture-free block, its own styles */
static void word_glossary(void)
{
  static const char ct[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.template.main+xml\"/>"
    "<Override PartName=\"/word/glossary/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.glossary+xml\"/>"
    "<Override PartName=\"/word/glossary/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/></Types>";
  static const char rels[] = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
  static const char drels[] = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId7\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/glossaryDocument\" Target=\"glossary/document.xml\"/>"
    "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/></Relationships>";
  static const char doc[] = "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t>Template body</w:t></w:r></w:p></w:body></w:document>";
  static const char settings[] = "<?xml version=\"1.0\"?><w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:linkStyles/>"
    "<w:stylePaneFormatFilter w:val=\"1F08\" w:allStyles=\"0\"/><w:stylePaneSortMethod w:val=\"0003\"/><w:defaultTabStop w:val=\"720\"/></w:settings>";
  static const char gloss[] = "<?xml version=\"1.0\"?><w:glossaryDocument xmlns:wpc=\"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas\" "
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
    "<w:docParts><w:docPart><w:docPartPr><w:name w:val=\"Disclaimer\"/><w:style w:val=\"Normal\"/><w:category><w:name w:val=\"Legal\"/><w:gallery w:val=\"custQuickParts\"/></w:category>"
    "<w:behaviors><w:behavior w:val=\"content\"/></w:behaviors><w:guid w:val=\"{0B0B0B0B-1111-2222-3333-444455556666}\"/></w:docPartPr>"
    "<w:docPartBody><w:p><w:pPr><w:pStyle w:val=\"Quote\"/></w:pPr><w:r><w:t xml:space=\"preserve\">No warranty. </w:t></w:r></w:p></w:docPartBody></w:docPart>"
    "<w:docPart><w:docPartPr><w:name w:val=\"DefaultPlaceholder_1081868574\"/><w:category><w:name w:val=\"General\"/><w:gallery w:val=\"placeholder\"/></w:category></w:docPartPr>"
    "<w:docPartBody><w:p><w:r><w:t>Click here.</w:t></w:r></w:p></w:docPartBody></w:docPart></w:docParts></w:glossaryDocument>";
  static const char grels[] = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>";
  static const char gstyles[] = "<?xml version=\"1.0\"?><w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/><w:rPr><w:i/></w:rPr></w:style></w:styles>";
  ZipWriter z;
  Doc *d, *e, *f;
  char err[200], *x;
  unsigned lost = 0;
  zip_begin(&z);
  zip_add(&z, "[Content_Types].xml", ct, sizeof ct - 1); zip_add(&z, "_rels/.rels", rels, sizeof rels - 1);
  zip_add(&z, "word/_rels/document.xml.rels", drels, sizeof drels - 1); zip_add(&z, "word/document.xml", doc, sizeof doc - 1);
  zip_add(&z, "word/settings.xml", settings, sizeof settings - 1); zip_add(&z, "word/glossary/document.xml", gloss, sizeof gloss - 1);
  zip_add(&z, "word/glossary/_rels/document.xml.rels", grels, sizeof grels - 1); zip_add(&z, "word/glossary/styles.xml", gstyles, sizeof gstyles - 1);
  zip_finish(&z);
  d = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
  zip_free(&z);
  ok("word glossary: read", d && d->gloss && d->gloss->n == 2 && blocks_find(d, "Disclaimer", GAL_QUICKPARTS) == 0 && !strcmp(d->gloss->b[0].category, "Legal"));
  ok("word glossary: a template", d && d->tplkind == TK_DOTX);
  ok("word glossary: settings read", d && d->linkstyles && d->spfilter == 0x1F08 && d->spsort == SPS_BASEDON);
  if (!d) return;
  f = with_text("Before ");
  {
    Pos at = doc_end_pos(f);
    char *t;
    blocks_insert(f, &at, d, 0);
    t = all(f);
    ok("word glossary: a block in the text, with its styles", t && !strcmp(t, "Before No warranty. ") && doc_style_by_name(f, "Quote") > 0 &&
       (para_fmt_at(f->p[0], 8)->flags & CF_ITALIC));
    free(t);
  }
  doc_free(f);
  /* a new block in Word's glossary: the others kept verbatim */
  blocks_add(d, d, P(0, 0), P(0, 8), "Body", GAL_QUICKPARTS, NULL, NULL, BB_CONTENT);
  e = docx_again(d, &lost);
  ok("word glossary: kept with a new block", e && e->gloss && e->gloss->n == 3 && blocks_find(e, "DefaultPlaceholder_1081868574", -1) == 1);
  {
    size_t n;
    u8 *zz = docx_save(d, &n);
    x = zz ? part23(zz, n, "word/glossary/document.xml") : NULL;
    ok("word glossary: its own namespaces and blocks verbatim", x && strstr(x, "xmlns:wpc=") && strstr(x, "{0B0B0B0B-1111-2222-3333-444455556666}") && strstr(x, "<w:style w:val=\"Normal\"/>"));
    free(x);
    x = zz ? part23(zz, n, "[Content_Types].xml") : NULL;
    ok("word glossary: saved as a template still", x && strstr(x, "wordprocessingml.template.main+xml"));
    free(x);
    x = zz ? part23(zz, n, "word/settings.xml") : NULL;
    ok("word glossary: settings written in place", x && strstr(x, "<w:linkStyles/><w:stylePaneFormatFilter w:val=\"1F08\"/><w:stylePaneSortMethod w:val=\"0003\"/><w:defaultTabStop"));
    free(x);
    free(zz);
  }
  doc_free(e);
  /* opened as a template: a new document attached to it, without the template's blocks */
  template_to_new(d, "Work:Templates/Legal.dotx");
  ok("templates: a new document from it", d->tplkind == TK_DOC && d->attached && !strcmp(d->attached, "Work:Templates/Legal.dotx") && !d->gloss && !d->dirty &&
     !strcmp(info_str(d, IP_TEMPLATE), "Legal.dotx"));
  {
    size_t n;
    u8 *zz = docx_save(d, &n);
    x = zz ? part23(zz, n, "word/settings.xml") : NULL;
    ok("templates: the attached template written", x && strstr(x, "<w:attachedTemplate xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rIdQTpl\"/>"));
    free(x);
    x = zz ? part23(zz, n, "word/_rels/settings.xml.rels") : NULL;
    ok("templates: and its relationship", x && strstr(x, "relationships/attachedTemplate\" Target=\"Work:Templates/Legal.dotx\" TargetMode=\"External\""));
    free(x);
    x = zz ? part23(zz, n, "word/glossary/document.xml") : NULL;
    ok("templates: no glossary in the new document", !x);
    free(x);
    x = zz ? part23(zz, n, "[Content_Types].xml") : NULL;
    ok("templates: a document's content type", x && strstr(x, "wordprocessingml.document.main+xml"));
    free(x);
    free(zz);
  }
  e = docx_again(d, &lost);
  ok("templates: attached template read back", e && e->attached && !strcmp(e->attached, "Work:Templates/Legal.dotx"));
  doc_free(e);
  doc_free(d);
}

/* Save as Template, the format rows, the organizer, another file at the caret */
static void templates(void)
{
  Doc *d = with_text("Letterhead"), *e;
  char err[200], *x;
  unsigned lost;
  const DocFormat *f = doc_format_for_name("Letter.dotx", 1);
  ok("templates: .dotx and .dotm rows", f && !strcmp(f->name, "Word Template") && (f->flags & DF_NATIVE) && doc_format_for_name("x.dotm", 0) && !doc_format_for_name("x.dotm", 1)->flags);
  ok("templates: saved as a template", template_save(d, TMPDIR "quill_wp23.dotx", TK_DOTX) && d->tplkind == TK_DOC);
  x = file_part(TMPDIR "quill_wp23.dotx", "[Content_Types].xml");
  ok("templates: its content type", x && strstr(x, "wordprocessingml.template.main+xml"));
  free(x);
  e = doc_load_file(TMPDIR "quill_wp23.dotx", &lost, err, sizeof err);
  ok("templates: read as one", e && e->tplkind == TK_DOTX);
  doc_free(e);
  ok("templates: .dotm", template_save(d, TMPDIR "quill_wp23.dotm", TK_DOTM));
  x = file_part(TMPDIR "quill_wp23.dotm", "[Content_Types].xml");
  ok("templates: a macro-enabled template's content type", x && strstr(x, "macroEnabledTemplate.main+xml"));
  free(x);
  e = doc_load_file(TMPDIR "quill_wp23.dotm", &lost, err, sizeof err);
  ok("templates: .dotm read as one", e && e->tplkind == TK_DOTM);
  doc_free(e);
  /* another file at the caret */
  {
    FILE *t = fopen(TMPDIR "quill_wp23.txt", "w");
    Pos at = P(0, 6);
    char *s;
    if (t) { fputs("one\ntwo\n", t); fclose(t); }
    ok("insert file: text", doc_insert_file(d, &at, TMPDIR "quill_wp23.txt", NULL, err, sizeof err));
    s = all(d);
    ok("insert file: at the caret", s && !strcmp(s, "Letterone\ntwo\nhead"));
    free(s);
    doc_undo(d, &at);
    s = all(d);
    ok("insert file: one undo step", s && !strcmp(s, "Letterhead"));
    free(s);
    at = doc_end_pos(d);
    ok("insert file: a .docx", doc_insert_file(d, &at, "tests/data/word.docx", NULL, err, sizeof err) && d->np > 3);
    ok("insert file: a missing bookmark refused", !doc_insert_file(d, &at, "tests/data/word.docx", "nothere", err, sizeof err) && strstr(err, "bookmark"));
    ok("insert file: a missing file refused", !doc_insert_file(d, &at, TMPDIR "quill_wp23_none.docx", NULL, err, sizeof err));
  }
  doc_free(d);
}

void test_wp23(void)
{
  blocks();
  word_glossary();
  templates();
}
