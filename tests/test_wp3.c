/* test_wp3.c - the native tests of WP3 Citations and bibliography (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp3() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/cite.h"
#include "../core/field.h"
#include "../core/keep.h"
#include "../core/zip.h"

/* a b:Sources part as Word writes it: a book by two people, a journal article by one, a web page by
   a corporate author, a placeholder */
static const char *word_sources =
  "<?xml version=\"1.0\" standalone=\"yes\"?>\r\n"
  "<b:Sources SelectedStyle=\"\\APASixthEditionOfficeOnline.xsl\" StyleName=\"APA\" Version=\"6\" "
  "xmlns:b=\"http://schemas.openxmlformats.org/officeDocument/2006/bibliography\" "
  "xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/bibliography\">"
  "<b:Source><b:Tag>Smi20</b:Tag><b:SourceType>Book</b:SourceType><b:Guid>{11111111-2222-4333-8444-555555555555}</b:Guid>"
  "<b:Author><b:Author><b:NameList><b:Person><b:Last>Smith</b:Last><b:First>John</b:First><b:Middle>Robert</b:Middle></b:Person>"
  "<b:Person><b:Last>Jones</b:Last><b:First>Anna</b:First></b:Person></b:NameList></b:Author></b:Author>"
  "<b:Title>The Art of Writing</b:Title><b:Year>2020</b:Year><b:City>London</b:City><b:Publisher>Penguin &amp; Sons</b:Publisher>"
  "<b:RefOrder>1</b:RefOrder></b:Source>"
  "<b:Source><b:Tag>Doe19</b:Tag><b:SourceType>JournalArticle</b:SourceType>"
  "<b:Author><b:Author><b:NameList><b:Person><b:Last>Doe</b:Last><b:First>Jane</b:First></b:Person></b:NameList></b:Author></b:Author>"
  "<b:Title>Reading in the dark</b:Title><b:JournalName>Journal of Light</b:JournalName><b:Year>2019</b:Year>"
  "<b:Volume>12</b:Volume><b:Issue>3</b:Issue><b:Pages>45-67</b:Pages><b:DOI>10.1000/jol.2019.3</b:DOI>"
  "<b:Extra><b:Inner>kept</b:Inner></b:Extra></b:Source>"
  "<b:Source><b:Tag>WHO21</b:Tag><b:SourceType>InternetSite</b:SourceType>"
  "<b:Author><b:Author><b:Corporate>World Health Organization</b:Corporate></b:Author></b:Author>"
  "<b:Title>Clean water</b:Title><b:InternetSiteTitle>WHO</b:InternetSiteTitle><b:Year>2021</b:Year><b:Month>March</b:Month><b:Day>4</b:Day>"
  "<b:YearAccessed>2022</b:YearAccessed><b:MonthAccessed>May</b:MonthAccessed><b:DayAccessed>1</b:DayAccessed>"
  "<b:URL>https://www.who.int/water</b:URL></b:Source>"
  "<b:Source><b:Tag>Placeholder1</b:Tag></b:Source>"
  "</b:Sources>";

static int verbose3;
/* source tag's entry in style cs is want (n: its number) */
static int entry_is(const CiteList *l, const char *tag, int cs, int n, const char *want)
{
  Buf b;
  int r, i = cite_find(l, tag);
  char *c, *o;
  buf_init(&b);
  cite_entry(l, i, cs, n, &b);
  /* italics shown as *...* */
  for (c = b.data, o = b.data; c && *c; c++) *o++ = (*c == CITE_I1 || *c == CITE_I0) ? '*' : *c;
  if (o) *o = 0;
  r = b.data && !strcmp(b.data, want);
  if (!r || verbose3) printf("  %s %s:\n    got  %s\n    want %s\n", cite_styles[cs].shortname, tag, b.data ? b.data : "", want);
  buf_free(&b);
  return r;
}
static int cite_is(const CiteList *l, const char *tag, int cs, int n, const char *pages, int sup, int first, const char *want)
{
  Buf b;
  int r;
  buf_init(&b);
  cite_cite_one(l, cite_find(l, tag), cs, n, pages, NULL, sup, first, &b);
  r = b.data && !strcmp(b.data, want);
  if (!r || verbose3) printf("  cite %s %s:\n    got  %s\n    want %s\n", cite_styles[cs].shortname, tag, b.data ? b.data : "", want);
  buf_free(&b);
  return r;
}
/* the text of paragraph i */
static const char *ptext(Doc *d, u32 i) { return i < d->np ? d->p[i]->t : ""; }
/* paragraph i's text without field marks */
static void visible(Doc *d, u32 i, char *out, size_t cap)
{
  const char *t = ptext(d, i);
  size_t n = 0;
  while (*t && n + 1 < cap) { if ((u8)*t == 0xF4) { t += 4; continue; } out[n++] = *t++; }
  out[n] = 0;
}
static int para_has(Doc *d, u32 i, const char *want)
{
  char v[1024];
  visible(d, i, v, sizeof v);
  if (!strstr(v, want) || verbose3) printf("  para %u: %s\n    want %s\n", (unsigned)i, v, want);
  return strstr(v, want) != NULL;
}
static char *zip_part_text(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  size_t l;
  u8 *p;
  char *s;
  if (!zip_open(&z, data, n)) return NULL;
  p = zip_read(&z, zip_find(&z, name), &l);
  zip_close(&z);
  if (!p) return NULL;
  s = (char *)malloc(l + 1);
  if (s) { memcpy(s, p, l); s[l] = 0; }
  free(p);
  return s;
}

static void sources_xml(void)
{
  CiteList l, m;
  char *x, t[200];
  size_t n;
  const CiteRole *r;
  memset(&l, 0, sizeof l); memset(&m, 0, sizeof m);
  ok("cite: Word's b:Sources read", cite_parse(&l, word_sources, strlen(word_sources)) && l.n == 4);
  ok("cite: style attributes", l.selstyle && !strcmp(l.selstyle, "\\APASixthEditionOfficeOnline.xsl") && cite_style_of(&l) == CS_APA6);
  r = cite_role_c(&l.s[0], "Author");
  ok("cite: authors", r && r->np == 2 && !strcmp(r->p[0].last, "Smith") && !strcmp(r->p[0].middle, "Robert") && !strcmp(r->p[1].first, "Anna"));
  ok("cite: simple fields, entities", !strcmp(cite_get(&l.s[0], "Publisher"), "Penguin & Sons") && !strcmp(cite_get(&l.s[1], "Pages"), "45-67"));
  ok("cite: corporate author", cite_role_c(&l.s[2], "Author") && cite_role_c(&l.s[2], "Author")->corp && !strcmp(cite_role_c(&l.s[2], "Author")->corp, "World Health Organization"));
  ok("cite: unknown elements kept verbatim", l.s[1].extra && strstr(l.s[1].extra, "<b:Inner>kept</b:Inner>"));
  ok("cite: placeholder", cite_is_placeholder(&l.s[3]) && !cite_is_placeholder(&l.s[0]));
  ok("cite: find by tag, any case", cite_find(&l, "doe19") == 1 && cite_find(&l, "Nope") < 0);
  x = cite_xml(&l, &n);
  ok("cite: written back and read again the same", x && cite_parse(&m, x, n) && m.n == 4 && !strcmp(cite_get(&m.s[0], "Publisher"), "Penguin & Sons") &&
     m.s[1].extra && strstr(m.s[1].extra, "kept") && cite_role_c(&m.s[0], "Author")->np == 2 && !strcmp(cite_get(&m.s[2], "DayAccessed"), "1"));
  { size_t n2; char *x2 = cite_xml(&m, &n2); ok("cite: XML stable through two passes", x2 && n2 == n && !memcmp(x, x2, n)); free(x2); }
  free(x);
  /* names typed as Word's Edit Name takes them */
  cite_set_names(&m.s[0], "Editor", "Brown, Carl Dee; Eve Ann White;Plain", 0);
  r = cite_role_c(&m.s[0], "Editor");
  ok("cite: names typed", r && r->np == 3 && !strcmp(r->p[0].last, "Brown") && !strcmp(r->p[0].middle, "Dee") && !strcmp(r->p[1].last, "White") &&
     !strcmp(r->p[1].first, "Eve") && !strcmp(r->p[1].middle, "Ann") && !strcmp(r->p[2].last, "Plain"));
  cite_names_text(&m.s[0], "Editor", t, sizeof t);
  ok("cite: names as text", !strcmp(t, "Brown, Carl Dee; White, Eve Ann; Plain"));
  cite_set_names(&m.s[0], "Editor", "", 0);
  ok("cite: names taken out", !cite_role_c(&m.s[0], "Editor"));
  cite_make_tag(&m, &m.s[0], t, sizeof t);
  ok("cite: a new tag as Word makes them", !strcmp(t, "Smi201"));
  cite_set(&m.s[0], "Year", "1999");
  cite_make_tag(&m, &m.s[0], t, sizeof t);
  ok("cite: tag from surname and year", !strcmp(t, "Smi99"));
  ok("cite: style chosen", cite_list_set_style(&m, CS_IEEE) && cite_style_of(&m) == CS_IEEE && !strcmp(m.version, "2006"));
  cite_list_free(&m);
  { CiteList w; memset(&w, 0, sizeof w); w.stylename = str_dup("MLA"); w.version = str_dup("8");
    ok("cite: a style by StyleName and Version", cite_style_of(&w) == CS_MLA8); cite_list_free(&w); }
  cite_list_free(&l);
}

static void styles(void)
{
  CiteList l;
  memset(&l, 0, sizeof l);
  cite_parse(&l, word_sources, strlen(word_sources));
  /* APA */
  ok("cite: APA 6th book", entry_is(&l, "Smi20", CS_APA6, 1, "Smith, J. R., & Jones, A. (2020). *The Art of Writing*. London: Penguin & Sons."));
  ok("cite: APA 7th book", entry_is(&l, "Smi20", CS_APA7, 1, "Smith, J. R., & Jones, A. (2020). *The Art of Writing*. Penguin & Sons."));
  ok("cite: APA 7th journal", entry_is(&l, "Doe19", CS_APA7, 1, "Doe, J. (2019). Reading in the dark. *Journal of Light*, *12*(3), 45\xE2\x80\x93" "67. https://doi.org/10.1000/jol.2019.3"));
  ok("cite: APA 6th journal", entry_is(&l, "Doe19", CS_APA6, 1, "Doe, J. (2019). Reading in the dark. *Journal of Light*, *12*(3), 45\xE2\x80\x93" "67. doi:10.1000/jol.2019.3"));
  ok("cite: APA 7th web page", entry_is(&l, "WHO21", CS_APA7, 1, "World Health Organization. (2021, March 4). Clean water. WHO. https://www.who.int/water"));
  ok("cite: APA citation", cite_is(&l, "Smi20", CS_APA7, 0, "", 0, 0, "Smith & Jones, 2020"));
  ok("cite: APA citation with pages", cite_is(&l, "Doe19", CS_APA6, 0, "50-52", 0, 0, "Doe, 2019, pp. 50\xE2\x80\x93" "52"));
  ok("cite: suppress author shows the title", cite_is(&l, "Smi20", CS_APA7, 0, "4", CITE_NO_AUTHOR, 0, "The Art of Writing, 2020, p. 4"));
  ok("cite: suppress author and title", cite_is(&l, "Smi20", CS_APA7, 0, "", CITE_NO_AUTHOR | CITE_NO_TITLE, 0, "2020"));
  ok("cite: suppress year", cite_is(&l, "Doe19", CS_APA7, 0, "", CITE_NO_YEAR, 0, "Doe"));
  /* MLA */
  ok("cite: MLA 9th book", entry_is(&l, "Smi20", CS_MLA9, 1, "Smith, John Robert, and Anna Jones. *The Art of Writing*. Penguin & Sons, 2020."));
  ok("cite: MLA 9th journal", entry_is(&l, "Doe19", CS_MLA9, 1, "Doe, Jane. \xE2\x80\x9CReading in the dark.\xE2\x80\x9D *Journal of Light*, vol. 12, no. 3, 2019, pp. 45\xE2\x80\x93" "67, https://doi.org/10.1000/jol.2019.3."));
  ok("cite: MLA 7th book", entry_is(&l, "Smi20", CS_MLA7, 1, "Smith, John Robert, and Anna Jones. *The Art of Writing*. London: Penguin & Sons, 2020. Print."));
  ok("cite: MLA 9th web page", entry_is(&l, "WHO21", CS_MLA9, 1, "World Health Organization. \xE2\x80\x9C" "Clean water.\xE2\x80\x9D *WHO*, 4 Mar. 2021, https://www.who.int/water. Accessed 1 May 2022."));
  ok("cite: MLA citation", cite_is(&l, "Smi20", CS_MLA9, 0, "23", 0, 0, "Smith and Jones 23"));
  /* Chicago, Turabian */
  ok("cite: Chicago 17th book", entry_is(&l, "Smi20", CS_CHICAGO17, 1, "Smith, John Robert, and Anna Jones. 2020. *The Art of Writing*. London: Penguin & Sons."));
  ok("cite: Chicago journal", entry_is(&l, "Doe19", CS_CHICAGO16, 1, "Doe, Jane. 2019. \xE2\x80\x9CReading in the dark.\xE2\x80\x9D *Journal of Light* 12, no. 3: 45\xE2\x80\x93" "67. https://doi.org/10.1000/jol.2019.3."));
  ok("cite: Turabian book", entry_is(&l, "Smi20", CS_TURABIAN, 1, "Smith, John Robert, and Anna Jones. *The Art of Writing*. London: Penguin & Sons, 2020."));
  ok("cite: Chicago citation", cite_is(&l, "Smi20", CS_CHICAGO17, 0, "23", 0, 0, "Smith and Jones 2020, 23"));
  /* Harvard */
  ok("cite: Harvard book", entry_is(&l, "Smi20", CS_HARVARD, 1, "Smith, J.R. and Jones, A., 2020. *The Art of Writing*. London: Penguin & Sons."));
  ok("cite: Harvard journal", entry_is(&l, "Doe19", CS_HARVARD, 1, "Doe, J., 2019. Reading in the dark. *Journal of Light*, 12(3), pp.45\xE2\x80\x93" "67. https://doi.org/10.1000/jol.2019.3."));
  ok("cite: Harvard web page", entry_is(&l, "WHO21", CS_HARVARD, 1, "World Health Organization, 2021. *Clean water*. WHO. [Online] Available at: https://www.who.int/water [Accessed 1 May 2022]."));
  ok("cite: Harvard citation", cite_is(&l, "Smi20", CS_HARVARD, 0, "5", 0, 0, "Smith and Jones, 2020, p. 5"));
  /* IEEE */
  ok("cite: IEEE book", entry_is(&l, "Smi20", CS_IEEE, 2, "[2]\tJ. R. Smith and A. Jones, *The Art of Writing*, London: Penguin & Sons, 2020."));
  ok("cite: IEEE journal", entry_is(&l, "Doe19", CS_IEEE, 1, "[1]\tJ. Doe, \xE2\x80\x9CReading in the dark,\xE2\x80\x9D *Journal of Light*, vol. 12, no. 3, pp. 45\xE2\x80\x93" "67, 2019. doi:10.1000/jol.2019.3."));
  ok("cite: IEEE web page", entry_is(&l, "WHO21", CS_IEEE, 3, "[3]\tWorld Health Organization. (2021, Mar. 4) *Clean water*. WHO. [Online]. Available: https://www.who.int/water"));
  ok("cite: IEEE citation", cite_is(&l, "Smi20", CS_IEEE, 2, "7", 0, 0, "2, p. 7"));
  /* ISO 690 */
  ok("cite: ISO 690 book", entry_is(&l, "Smi20", CS_ISO690, 1, "SMITH, John Robert and JONES, Anna. 2020. *The Art of Writing*. London : Penguin & Sons, 2020."));
  ok("cite: ISO 690 journal", entry_is(&l, "Doe19", CS_ISO690, 1, "DOE, Jane. 2019. Reading in the dark. *Journal of Light*. 2019, Vol. 12, 3, pp. 45\xE2\x80\x93" "67. https://doi.org/10.1000/jol.2019.3."));
  ok("cite: ISO 690 numerical book", entry_is(&l, "Smi20", CS_ISO690NUM, 4, "4.\tSMITH, John Robert and JONES, Anna. *The Art of Writing*. London : Penguin & Sons, 2020."));
  ok("cite: ISO 690 citation", cite_is(&l, "Doe19", CS_ISO690, 0, "", 0, 0, "DOE, 2019"));
  /* GB/T 7714, GOST, SIST02 */
  ok("cite: GB7714 book", entry_is(&l, "Smi20", CS_GB7714, 1, "[1]\tSMITH J R, JONES A. The Art of Writing[M]. London: Penguin & Sons, 2020."));
  ok("cite: GB7714 journal", entry_is(&l, "Doe19", CS_GB7714, 2, "[2]\tDOE J. Reading in the dark[J]. Journal of Light, 2019, 12(3): 45\xE2\x80\x93" "67."));
  ok("cite: GOST book", entry_is(&l, "Smi20", CS_GOSTNAME, 1, "1.\tSmith J. R., Jones A. The Art of Writing. \xE2\x80\x93 London : Penguin & Sons, 2020."));
  ok("cite: SIST02 book", entry_is(&l, "Smi20", CS_SIST02, 1, "1)\tSmith, John Robert; Jones, Anna. The Art of Writing. London, Penguin & Sons, 2020."));
  /* the same author twice in a year: 2020a, 2020b */
  {
    CiteSource s;
    cite_source_copy(&s, &l.s[0]);
    cite_set(&s, "Tag", "Smi20b"); cite_set(&s, "Title", "Another Book");
    cite_add(&l, &s);
    cite_source_free(&s);
    ok("cite: year letters", cite_year_suffix(&l, 0, CS_APA7) == 'b' && cite_year_suffix(&l, 4, CS_APA7) == 'a' && !cite_year_suffix(&l, 1, CS_APA7) &&
       !cite_year_suffix(&l, 0, CS_IEEE));
    ok("cite: year letter in the citation", cite_is(&l, "Smi20b", CS_APA7, 0, "", 0, 0, "Smith & Jones, 2020a"));
  }
  /* many authors */
  {
    CiteSource s;
    memset(&s, 0, sizeof s);
    cite_set(&s, "Tag", "Many"); cite_set(&s, "SourceType", "Book"); cite_set(&s, "Title", "Crowd"); cite_set(&s, "Year", "2001");
    cite_set_names(&s, "Author", "Aa, B; Bb, C; Cc, D; Dd, E; Ee, F", 0);
    cite_add(&l, &s);
    cite_source_free(&s);
    ok("cite: APA 6th lists five authors the first time", cite_is(&l, "Many", CS_APA6, 0, "", 0, 1, "Aa, Bb, Cc, Dd, & Ee, 2001"));
    ok("cite: then et al.", cite_is(&l, "Many", CS_APA6, 0, "", 0, 0, "Aa et al., 2001"));
    ok("cite: MLA et al.", entry_is(&l, "Many", CS_MLA9, 1, "Aa, B, et al. *Crowd*. 2001."));
  }
  cite_list_free(&l);
}

/* a document: sources, citations, a bibliography, styles, static text, .docx */
static void document(void)
{
  Doc *d = doc_new(), *b;
  CiteDoc *c;
  CiteList l;
  Pos at;
  int k, i, found;
  unsigned lost;
  u8 *data;
  size_t n;
  memset(&l, 0, sizeof l);
  cite_parse(&l, word_sources, strlen(word_sources));
  c = cite_doc(d);
  for (i = 0; i < l.n; i++) cite_add(&c->list, &l.s[i]);
  cite_list_set_style(&c->list, CS_APA7);
  ok("cite: the current list into a kept part", cite_sync(d) && c->part[0] && keep_find(d, c->part) >= 0 && keep_find(d, "customXml/itemProps1.xml") >= 0);
  at = P(0, 0);
  doc_insert(d, &at, "As shown ", 9, NULL);
  k = cite_insert(d, &at, "Smi20", "12", 0, NULL, 0);
  doc_insert(d, &at, " and ", 5, NULL);
  { const char *more[1] = { "WHO21" }; cite_insert(d, &at, "Doe19", NULL, 0, more, 1); }
  doc_insert(d, &at, ".", 1, NULL);
  ok("cite: CITATION code", k >= 0 && strstr(d->fields[k].code, "CITATION Smi20") && strstr(d->fields[k].code, "\\p 12") && strstr(d->fields[k].code, "\\l 1033"));
  ok("cite: citations worked out", para_has(d, 0, "As shown (Smith & Jones, 2020, p. 12) and (Doe, 2019; World Health Organization, 2021)."));
  { Pos s, e; ok("cite: the citation at the caret", cite_field_at(d, P(0, 14), 0, &s, &e) == k && s.off == 9 && cite_field_at(d, P(0, 3), 0, NULL, NULL) < 0); }
  at = doc_end_pos(d);
  k = cite_insert_bib(d, &at, 2);
  ok("cite: References heading", d->np >= 2 && !strcmp(ptext(d, 1), "References") && d->p[1]->pf.style == doc_style(d, "Heading1"));
  ok("cite: bibliography field", k >= 0 && strstr(d->fields[k].code, "BIBLIOGRAPHY"));
  ok("cite: bibliography in alphabetical order", d->np == 5 && para_has(d, 2, "Doe, J. (2019).") && para_has(d, 3, "Smith, J. R., & Jones, A. (2020).") &&
     para_has(d, 4, "World Health Organization. (2021, March 4)."));
  ok("cite: entries in the Bibliography style, hanging", d->p[3]->pf.style == doc_style(d, "Bibliography") && d->p[3]->pf.left == 720 && d->p[3]->pf.first == -720);
  { u32 r; found = 0; for (r = 0; r < d->p[3]->nr; r++) if (d->p[3]->r[r].f.flags & CF_ITALIC) found = 1; ok("cite: titles in italics", found); }
  ok("cite: runs sane", runs_sane(d));
  /* another style: numbers in the order cited */
  cite_list_set_style(&c->list, CS_IEEE); cite_sync(d);
  ok("cite: update", cite_update_all(d) > 0);
  ok("cite: IEEE citations", para_has(d, 0, "As shown [1, p. 12] and [2], [3]."));
  ok("cite: IEEE bibliography in citation order", para_has(d, 2, "[1]\tJ. R. Smith and A. Jones") && para_has(d, 3, "[2]\tJ. Doe") && para_has(d, 4, "[3]\tWorld Health"));
  /* a citation put in first takes number 1, the others move up */
  at = P(0, 0);
  cite_insert(d, &at, "WHO21", NULL, 0, NULL, 0);
  ok("cite: IEEE renumbered by a new first citation", para_has(d, 0, "[1]As shown [2, p. 12] and [3], [1].") && para_has(d, 2, "[1]\tWorld Health"));
  doc_undo(d, &at);
  ok("cite: and its undo", para_has(d, 0, "As shown [1, p. 12] and [2], [3]."));
  doc_undo(d, &at);
  ok("cite: one undo step", para_has(d, 0, "(Smith & Jones, 2020, p. 12)"));
  doc_redo(d, &at);
  /* F9 (field_update) rebuilds the bibliography too */
  cite_list_set_style(&c->list, CS_MLA9); cite_sync(d);
  { FieldEnv e; field_env(&e); field_update_all(d, &e); }
  ok("cite: MLA through Update Fields", para_has(d, 0, "As shown (Smith and Jones 12) and (Doe; World Health Organization).") && para_has(d, 2, "Doe, Jane."));
  /* a source added: the bibliography grows */
  { CiteSource s; memset(&s, 0, sizeof s); cite_set(&s, "Tag", "Abe90"); cite_set(&s, "SourceType", "Book"); cite_set(&s, "Title", "Early");
    cite_set(&s, "Year", "1990"); cite_set_names(&s, "Author", "Abel, Tom", 0); cite_add(&c->list, &s); cite_source_free(&s); cite_sync(d); }
  cite_update_all(d);
  ok("cite: a new source in the bibliography", d->np == 6 && para_has(d, 2, "Abel, Tom. Early. 1990."));
  ok("cite: is cited", cite_is_cited(d, "Doe19") && !cite_is_cited(d, "Abe90"));
  /* saved and read back */
  data = docx_save(d, &n);
  ok("cite: saved", data != NULL);
  if (data) {
    char *item = zip_part_text(data, n, c->part), *rels = zip_part_text(data, n, "word/_rels/document.xml.rels");
    char *ct = zip_part_text(data, n, "[Content_Types].xml"), *doc = zip_part_text(data, n, "word/document.xml");
    ok("cite: the sources part written", item && strstr(item, "<b:Sources") && strstr(item, "MLANinthEdition") && strstr(item, "<b:Tag>Abe90</b:Tag>"));
    ok("cite: its relationship and properties", rels && strstr(rels, "customXml/item1.xml") && ct && strstr(ct, "customXmlProperties+xml"));
    ok("cite: the fields written", doc && strstr(doc, "CITATION Smi20") && strstr(doc, "BIBLIOGRAPHY"));
    ok("cite: the Bibliography style written", doc && strstr(doc, "w:val=\"Bibliography\""));
    b = docx_load(data, n, &lost, NULL, 0);
    ok("cite: read back with its sources", b && b->cite && b->cite->list.n == 5 && cite_style_of(&b->cite->list) == CS_MLA9 &&
       cite_find(&b->cite->list, "Abe90") >= 0 && b->cite->list.s[1].extra != NULL);
    ok("cite: and the same text", b && same_doc(d, b));
    if (b) {
      /* unchanged: written back byte for byte */
      size_t n2;
      u8 *data2 = docx_save(b, &n2);
      char *item2 = data2 ? zip_part_text(data2, n2, b->cite->part) : NULL;
      ok("cite: an unchanged list written back as it was", item && item2 && !strcmp(item, item2));
      ok("cite: update after reading changes nothing", cite_update_all(b) == 0);
      free(item2); free(data2);
      doc_free(b);
    }
    free(item); free(rels); free(ct); free(doc);
    free(data);
  }
  /* static text */
  ok("cite: citations to static text", cite_to_static(d, P(0, 0), P(0, d->p[0]->len), 0) == 2 && cite_list_citations(d, &at, 1) == 0 &&
     para_has(d, 0, "As shown (Smith and Jones 12)"));
  ok("cite: a bibliography to static text", cite_to_static(d, P(3, 2), P(3, 2), 1) == 1 && para_has(d, 3, "Doe, Jane.") && !strstr(ptext(d, 2), "\xF4"));
  ok("cite: runs sane after", runs_sane(d));
  doc_free(d);
  cite_list_free(&l);
}

/* Word's own file with an empty sources part: read, the part kept byte for byte, a source added */
static void word_file(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/word.docx", &lost, err, sizeof err);
  u8 *data;
  size_t n;
  ok("cite: word.docx's sources part read", d && d->cite && d->cite->list.n == 0 && !strcmp(d->cite->part, "customXml/item1.xml") &&
     cite_style_of(&d->cite->list) == CS_APA6 && keep_find(d, "customXml/item1.xml") >= 0);
  if (!d) return;
  {
    CiteSource s;
    Pos at = P(0, 0);
    memset(&s, 0, sizeof s);
    cite_set(&s, "Tag", "Lee05"); cite_set(&s, "SourceType", "Report"); cite_set(&s, "Title", "Water use"); cite_set(&s, "Year", "2005");
    cite_set(&s, "Publisher", "Agency"); cite_set(&s, "StandardNumber", "42");
    cite_set_names(&s, "Author", "Lee, Kim", 0);
    cite_add(&cite_doc(d)->list, &s);
    cite_source_free(&s);
    cite_sync(d);
    cite_insert(d, &at, "Lee05", NULL, 0, NULL, 0);
  }
  data = docx_save(d, &n);
  if (data) {
    char *item = zip_part_text(data, n, "customXml/item1.xml"), *props = zip_part_text(data, n, "customXml/itemProps1.xml");
    ok("cite: Word's part takes the new source, its properties kept", item && strstr(item, "Lee05") && props && strstr(props, "bibliography"));
    file_write("/tmp/quill_wp3_word.docx", data, n);
    free(item); free(props); free(data);
  }
  doc_free(d);
}

/* a test document for LibreOffice and Word: every style's citations and bibliography */
static void sample(void)
{
  Doc *d = doc_new();
  CiteDoc *c = cite_doc(d);
  CiteList l;
  Pos at = P(0, 0);
  u8 *data;
  size_t n;
  int i;
  memset(&l, 0, sizeof l);
  cite_parse(&l, word_sources, strlen(word_sources));
  for (i = 0; i < 3; i++) cite_add(&c->list, &l.s[i]);
  cite_list_set_style(&c->list, CS_APA7);
  cite_sync(d);
  doc_insert(d, &at, "Writing is an art ", 18, NULL);
  cite_insert(d, &at, "Smi20", "12", 0, NULL, 0);
  doc_insert(d, &at, ", reading too ", 14, NULL);
  cite_insert(d, &at, "Doe19", NULL, 0, NULL, 0);
  doc_insert(d, &at, "; water matters ", 16, NULL);
  cite_insert(d, &at, "WHO21", NULL, CITE_NO_YEAR, NULL, 0);
  doc_insert(d, &at, ".", 1, NULL);
  at = doc_end_pos(d);
  cite_insert_bib(d, &at, 2);
  data = docx_save(d, &n);
  if (data) { file_write("/tmp/quill_wp3_sample.docx", data, n); free(data); }
  doc_free(d);
  cite_list_free(&l);
}

/* the sample saved again by LibreOffice (tests/data/cite-lo.docx): its sources, citations and
   bibliography read, nothing to update */
static void lo_file(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/cite-lo.docx", &lost, err, sizeof err);
  Pos at[8];
  ok("cite: LibreOffice's file read with its sources", d && d->cite && d->cite->list.n == 3 && cite_style_of(&d->cite->list) == CS_APA7);
  if (!d) return;
  ok("cite: its citations", cite_list_citations(d, at, 8) == 3);
  ok("cite: its results as Quill makes them", cite_update_all(d) == 0);
  ok("cite: its bibliography", d->np >= 5 && para_has(d, d->np - 3, "Doe, J. (2019)."));
  doc_free(d);
}

void test_wp3(void)
{
  verbose3 = getenv("WP3_VERBOSE") != NULL;
  sources_xml();
  styles();
  document();
  word_file();
  sample();
  lo_file();
}
