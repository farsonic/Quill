/* test_wp0.c - the native tests of WP0, the scaffolding for the work packages (docs/wordcraft-gap.md):
   the file format table, parts kept verbatim through a save, and xml_raw */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/keep.h"
#include "../core/xml.h"
#include "../core/zip.h"
#include "../core/info.h"

/* whether the .docx in data has part name, with these bytes (want NULL: any) */
static int has_part(const u8 *data, size_t n, const char *name, const u8 *want, size_t wn)
{
  ZipReader z;
  size_t len;
  u8 *b;
  int r;
  if (!zip_open(&z, data, n)) return 0;
  b = zip_read(&z, zip_find(&z, name), &len);
  r = b && (!want || (len == wn && !memcmp(b, want, wn)));
  free(b);
  zip_close(&z);
  return r;
}
/* a part's text (malloc'd) */
static char *part_text(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  size_t len;
  u8 *b;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), &len);
  zip_close(&z);
  return (char *)b;
}

static void formats(void)
{
  char pat[120], err[200];
  unsigned lost;
  const DocFormat *f;
  ok("formats: .DOCX names Word for saving", (f = doc_format_for_name("RAM:Letter.DOCX", 1)) && !strcmp(f->exts, "docx") && (f->flags & DF_NATIVE));
  ok("formats: .txt reads as text", (f = doc_format_for_name("notes.txt", 0)) && (f->flags & DF_TEXT));
  ok("formats: .doc can be read (refused) but not written", doc_format_for_name("old.doc", 0) && !doc_format_for_name("old.doc", 1));
  ok("formats: rows without functions are left out", !doc_format_for_name("x.rtf", 0) && !doc_format_for_name("x.odt", 1));
  ok("formats: no ending, no format", !doc_format_for_name("README", 0) && !doc_format_for_name("dir.docx/file", 0));
  doc_format_pattern(0, pat, sizeof pat);
  ok("formats: open pattern", !strcmp(pat, "#?.(docx|doc|txt|text|asc)"));
  doc_format_pattern(1, pat, sizeof pat);
  ok("formats: save pattern", !strcmp(pat, "#?.(docx|txt|text|asc)"));
  {
    static const u8 cfb[16] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    Doc *d;
    file_write("/tmp/quill_wp0_old.txt", cfb, sizeof cfb);      /* (sniffed: the name doesn't matter) */
    d = doc_load_file("/tmp/quill_wp0_old.txt", &lost, err, sizeof err);
    ok("formats: an OLE file is refused as .doc", !d && strstr(err, "97-2003") != NULL);
    doc_free(d);
    file_write("/tmp/quill_wp0_plain.docx", "Just text\n", 10);   /* not a zip: read as text whatever its name */
    d = doc_load_file("/tmp/quill_wp0_plain.docx", &lost, err, sizeof err);
    ok("formats: anything else is text", d && text_is(d, "Just text\n"));
    doc_free(d);
  }
}

/* custom XML, extended and custom properties kept byte for byte through a save, with their own
   relationships, and again through a second one */
static void kept_parts(const char *file, int custom)
{
  char err[200], what[160];
  unsigned lost;
  Doc *a = doc_load_file(file, &lost, err, sizeof err), *b = NULL;
  u8 *orig, *data = NULL, *data2 = NULL;
  size_t on, n, n2;
  int k;
  char *rels, *ct, *prels;
  orig = (u8 *)file_read(file, &on);
  snprintf(what, sizeof what, "keep %s: ", file);
  if (!a || !orig) { ok(what, 0); free(orig); doc_free(a); return; }
  ok("keep: customXml, its rels and itemProps kept", keep_find(a, "customXml/item1.xml") >= 0 && keep_find(a, "customXml/_rels/item1.xml.rels") >= 0 &&
     keep_find(a, "customXml/itemProps1.xml") >= 0);
  /* (WP24 reads app.xml and custom.xml now, core/info.c, and writes them itself) */
  ok("keep: app.xml read (WP24), thumbnail not kept", keep_find(a, "docProps/app.xml") < 0 && a->info && a->info->napp > 0 && keep_find(a, "docProps/thumbnail.jpeg") < 0);
  if (custom) ok("keep: custom.xml read (WP24)", keep_find(a, "docProps/custom.xml") < 0 && a->info);   /* (lo.docx's has no properties in it) */
  k = keep_find(a, "customXml/item1.xml");
  ok("keep: the document's relationship, its id clear of Quill's", k >= 0 && a->kept[k].owner && !strcmp(a->kept[k].owner, "word/document.xml") &&
     a->kept[k].relid && (!strncmp(a->kept[k].relid, "rIdK", 4) || !keep_id_reserved(a->kept[k].relid)));
  data = docx_save(a, &n);
  ok("keep: saved", data != NULL);
  if (data) {
    size_t pl;
    u8 *pb;
    ZipReader z;
    int same = 1, i;
    zip_open(&z, orig, on);
    for (i = 0; i < a->nkept; i++) {
      pb = zip_read(&z, zip_find(&z, a->kept[i].name), &pl);
      if (!pb || !has_part(data, n, a->kept[i].name, pb, pl)) { same = 0; printf("  %s differs\n", a->kept[i].name); }
      free(pb);
    }
    zip_close(&z);
    ok("keep: every kept part written back byte for byte", same && a->nkept >= 3);
    rels = part_text(data, n, "word/_rels/document.xml.rels");
    prels = part_text(data, n, "_rels/.rels");
    ct = part_text(data, n, "[Content_Types].xml");
    ok("keep: relationships to them", rels && strstr(rels, "Target=\"/customXml/item1.xml\"") && strstr(rels, "/customXml\"") &&
       prels && strstr(prels, "Target=\"docProps/app.xml\"") && strstr(prels, "/extended-properties\""));
    ok("keep: their content types", ct && strstr(ct, "PartName=\"/customXml/itemProps1.xml\"") && strstr(ct, "PartName=\"/docProps/app.xml\""));
    free(rels); free(prels); free(ct);
    b = docx_load(data, n, &lost, err, sizeof err);
    ok("keep: read back, the same document and parts", b && same_doc(a, b) && b->nkept == a->nkept);
    if (b && (data2 = docx_save(b, &n2))) ok("keep: a second save keeps them too", has_part(data2, n2, "customXml/itemProps1.xml", NULL, 0) && has_part(data2, n2, "docProps/app.xml", NULL, 0));
    snprintf(what, sizeof what, "/tmp/quill_wp0_kept_%s", strrchr(file, '/') + 1);
    file_write(what, data, n);
  }
  keep_remove(a, "docProps/app.xml");
  ok("keep: keep_remove", keep_find(a, "docProps/app.xml") < 0 && keep_find(a, "customXml/item1.xml") >= 0);
  free(data); free(data2); free(orig); doc_free(a); doc_free(b);
}

/* keep_add's ids: Quill's own are never taken */
static void keep_ids(void)
{
  Doc *d = doc_new();
  int a, b, c;
  a = keep_add(d, "x/one.xml", "text/xml", "word/document.xml", "http://example.com/rel", "rId1", "<a/>", 4);
  b = keep_add(d, "x/two.xml", "text/xml", "word/document.xml", "http://example.com/rel", "rId7", "<b/>", 4);
  c = keep_add(d, "x/three.xml", "text/xml", "word/document.xml", "http://example.com/rel", "rId7", "<c/>", 4);
  ok("keep ids: rId1 is Quill's: renamed", a == 0 && strcmp(d->kept[0].relid, "rId1") && !strncmp(d->kept[0].relid, "rIdK", 4));
  ok("keep ids: a free id kept, a taken one renamed", b == 1 && !strcmp(d->kept[1].relid, "rId7") && c == 2 && strcmp(d->kept[2].relid, "rId7"));
  ok("keep ids: a name kept only once", keep_add(d, "x/one.xml", NULL, NULL, NULL, NULL, "", 0) == -1);
  doc_free(d);
}

static void raw_xml(void)
{
  static const char src[] = "<a><b x=\"1\"><c/>t</b><d/></a>";
  Xml x;
  const char *st = NULL;
  size_t n = 0;
  xml_init(&x, src, strlen(src));
  while (xml_next(&x) != XT_EOF) if (x.type == XT_START && !strcmp(x.name, "b")) { n = xml_raw(&x, &st); break; }
  ok("xml_raw: the element as written", n == 18 && st && !strncmp(st, "<b x=\"1\"><c/>t</b>", n));
  ok("xml_raw: and skipped", xml_next(&x) == XT_START && !strcmp(x.name, "d"));
  xml_free(&x);
  xml_init(&x, src, strlen(src));
  while (xml_next(&x) != XT_EOF) if (x.type == XT_START && !strcmp(x.name, "c")) { n = xml_raw(&x, &st); break; }
  ok("xml_raw: an empty element", n == 4 && !strncmp(st, "<c/>", 4));
  xml_free(&x);
}

void test_wp0(void)
{
  formats();
  kept_parts("tests/data/word.docx", 0);
  kept_parts("tests/data/lo.docx", 1);
  keep_ids();
  raw_xml();
}
