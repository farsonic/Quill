/* test_wp10.c - the native tests of WP10 Paragraph and Home quick commands (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp10() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/charfx.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/paratools.h"

Doc *wp9_mini_docx(const char *body, const char *styles);      /* (test_wp9.c) */
char *wp9_saved_xml(Doc *d, const char *part);
char *wp9_pdf_streams(const u8 *pdf, size_t n);
int wp9_count(const char *s, const char *sub);

static Doc *paras(const char *const *t, int n)
{
  Doc *d = doc_new();
  Pos c = P(0, 0);
  int i;
  for (i = 0; i < n; i++) {
    if (i) doc_insert(d, &c, "\n", 1, NULL);
    doc_insert(d, &c, t[i], (u32)strlen(t[i]), NULL);
  }
  doc_clear_undo(d);
  return d;
}
static int order_is(Doc *d, const char *const *want, int n)
{
  int i;
  for (i = 0; i < n; i++) if ((u32)i >= d->np || strcmp(d->p[i]->t, want[i])) { printf("  paragraph %d: %s\n", i, (u32)i < d->np ? d->p[i]->t : "(none)"); return 0; }
  return 1;
}

static void sorting(void)
{
  static const char *const names[] = { "pear", "Apple", "\xc3\xa9" "clair", "apple", "banana", "" };
  static const char *const by_name[] = { "", "Apple", "apple", "banana", "\xc3\xa9" "clair", "pear" };
  static const char *const by_name_case[] = { "", "Apple", "apple", "banana", "\xc3\xa9" "clair", "pear" };
  static const char *const rows[] = { "Name\tAge\tBorn", "Ann\t42\t3/4/1982", "Bob\t7\t12 March 2017", "Cy\t1,200\t2001-01-31", "Di\t-3\t25/12/1999" };
  static const char *const by_age[] = { "Name\tAge\tBorn", "Di\t-3\t25/12/1999", "Bob\t7\t12 March 2017", "Ann\t42\t3/4/1982", "Cy\t1,200\t2001-01-31" };
  static const char *const by_born_desc[] = { "Name\tAge\tBorn", "Bob\t7\t12 March 2017", "Cy\t1,200\t2001-01-31", "Di\t-3\t25/12/1999", "Ann\t42\t3/4/1982" };
  static const char *const csv[] = { "b,2", "a,2", "c,1" };
  static const char *const csv2[] = { "c,1", "a,2", "b,2" };
  SortOpt o;
  Doc *d = paras(names, 6);
  Pos c;
  int found;
  memset(&o, 0, sizeof o);
  o.nkeys = 1; o.key[0].field = 0; o.key[0].type = SORT_TEXT;
  ok("sort text: A to Z, accents beside their letters, empty first", doc_sort_text(d, 0, 5, &o) && order_is(d, by_name, 6) && runs_sane(d));
  o.casesens = 1;
  ok("sort text: case sensitive (capitals first)", doc_sort_text(d, 0, 5, &o) && order_is(d, by_name_case, 6));
  ok("sort text: one undo step", doc_undo(d, &c) && doc_undo(d, &c) && order_is(d, names, 6));
  o.casesens = 0; o.key[0].desc = 1;
  ok("sort text: Z to A", doc_sort_text(d, 0, 5, &o) && !strcmp(d->p[0]->t, "pear") && !strcmp(d->p[5]->t, ""));
  doc_free(d);
  d = paras(rows, 5);
  memset(&o, 0, sizeof o);
  o.nkeys = 1; o.key[0].field = 2; o.key[0].type = SORT_NUMBER; o.sep = '\t'; o.header = 1;
  ok("sort text: by a field as numbers, the header row kept", doc_sort_text(d, 0, 4, &o) && order_is(d, by_age, 5));
  o.key[0].field = 3; o.key[0].type = SORT_DATE; o.key[0].desc = 1;
  ok("sort text: by a field as dates, newest first", doc_sort_text(d, 0, 4, &o) && order_is(d, by_born_desc, 5));
  doc_free(d);
  d = paras(csv, 3);
  memset(&o, 0, sizeof o);
  o.nkeys = 2; o.sep = ','; o.key[0].field = 2; o.key[0].type = SORT_NUMBER; o.key[1].field = 1; o.key[1].type = SORT_TEXT;
  ok("sort text: commas, then by a second key", doc_sort_text(d, 0, 2, &o) && order_is(d, csv2, 3));
  doc_free(d);
  ok("sort: numbers", sort_number("about 1,234.5 km", 16, &found) == 1234500 && found && sort_number("-7", 2, &found) == -7000 && !(sort_number("none", 4, &found), found));
  ok("sort: dates", sort_date("3/4/1982", 8) > sort_date("4/3/1982", 8) && sort_date("12 March 2017", 13) == sort_date("2017-03-12", 10) &&
     sort_date("March 12, 2017", 14) == sort_date("12/3/17", 7) && sort_date("someday", 7) == 0);
}

static void distributed(void)
{
  Doc *d = doc_new(), *e;
  Pos c = P(0, 0);
  ParaFmt pf;
  Layout *L;
  char *xml;
  doc_insert(d, &c, "Spread me out", 13, NULL);
  pf = d->p[0]->pf; pf.align = AL_DISTRIBUTE;
  doc_para_format(d, 0, 0, PM_ALIGN, &pf);
  L = lay_new(d, &fs);
  lay_update(L);
  {
    i32 room = d->page.width - d->page.left - d->page.right, w = L->pl[0].lines[0].width;
    Box *last = &L->pl[0].boxes[L->pl[0].nboxes - 1];
    ok("distributed: even a last line spread to the margin", w >= room - 40 && w <= room + 40 && last->x + last->w >= room - 400 && last->f.spacing > 0);
  }
  lay_free(L);
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("distributed: w:jc distribute", xml && strstr(xml, "<w:jc w:val=\"distribute\"/>"));
  free(xml);
  e = docx_again(d, NULL);
  ok("distributed: read back", e && e->p[0]->pf.align == AL_DISTRIBUTE);
  doc_free(e); doc_free(d);
}

static void hyphens(void)
{
  static const char body[] = "<w:p><w:r><w:t>non</w:t><w:noBreakHyphen/><w:t>stop super</w:t><w:softHyphen/><w:t>cali A\xc2\xa0" "B</w:t></w:r></w:p>";
  Doc *d = wp9_mini_docx(body, NULL), *e;
  char *xml;
  Layout *L;
  ok("hyphens: read (U+2011, U+00AD, U+00A0)", d && text_is(d, "non\xe2\x80\x91stop super\xc2\xad" "cali A\xc2\xa0" "B"));
  if (!d) return;
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("hyphens: written as Word's elements", xml && strstr(xml, "<w:t>non</w:t><w:noBreakHyphen/><w:t>stop super</w:t><w:softHyphen/>"));
  free(xml);
  e = docx_again(d, NULL);
  ok("hyphens: read back", e && same_doc(d, e));
  doc_free(e);
  L = lay_new(d, &fs);
  lay_update(L);
  {
    /* an optional hyphen takes no room; a nonbreaking hyphen is as wide as a hyphen */
    FaceSize *z = lay_face(L, &d->p[0]->r[0].f);
    Box *b = NULL;
    u32 k;
    for (k = 0; k < L->pl[0].nboxes; k++) if (!strncmp(d->p[0]->t + L->pl[0].boxes[k].off, "super", 5)) b = &L->pl[0].boxes[k];
    ok("hyphens: an optional one takes no room", b && z && b->w == (i32)((fonts_text(z, "supercali", 9) + QFINE / 2) / QFINE));
  }
  lay_free(L);
  doc_free(d);
  {
    /* a long word with an optional hyphen in a narrow column breaks there, shown as a hyphen */
    Doc *n = doc_new();
    Pos c = P(0, 0);
    static const char w[] = "Extraordinarily extra\xc2\xad" "ordinarily";
    PageSetup *pg = &n->page;
    doc_insert(n, &c, w, (u32)strlen(w), NULL);
    pg->left = pg->right = (pg->width - 2600) / 2;
    L = lay_new(n, &fs);
    lay_update(L);
    ok("hyphens: a line breaks at an optional hyphen", L->pl[0].nlines >= 2 && L->pl[0].lines[1].start == 23 && L->pl[0].boxes[L->pl[0].lines[0].box0 + L->pl[0].lines[0].nbox - 1].nspace == 1);
    {
      size_t len;
      u8 *pdf = pdf_export(L, &len);
      char *st = pdf ? wp9_pdf_streams(pdf, len) : NULL;
      ok("hyphens: the PDF shows the hyphen at the break only (four pieces: two words, the hyphen, the rest)", st && wp9_count(st, " Tj") == 4);
      free(st); free(pdf);
    }
    lay_free(L);
    doc_free(n);
  }
}

static void numbering(void)
{
  char o[32];
  Doc *d = doc_new(), *e;
  Pos c = P(0, 0);
  ListDef l;
  int k, n;
  char *xml;
  Layout *L;
  ok("numbers: 01, 1st 2nd 3rd 11th 22nd", nf_counter(o, 1, NF_DECIMAL_ZERO) && !strcmp(o, "01") && nf_counter(o, 2, NF_ORDINAL) && !strcmp(o, "2nd") &&
     nf_counter(o, 11, NF_ORDINAL) && !strcmp(o, "11th") && nf_counter(o, 22, NF_ORDINAL) && !strcmp(o, "22nd"));
  ok("numbers: One, Twenty-one, First, Twenty-second, Fortieth", nf_counter(o, 1, NF_CARDINAL_TEXT) && !strcmp(o, "One") && nf_counter(o, 21, NF_CARDINAL_TEXT) &&
     !strcmp(o, "Twenty-one") && nf_counter(o, 1, NF_ORDINAL_TEXT) && !strcmp(o, "First") && nf_counter(o, 22, NF_ORDINAL_TEXT) && !strcmp(o, "Twenty-second") &&
     nf_counter(o, 40, NF_ORDINAL_TEXT) && !strcmp(o, "Fortieth") && !nf_counter(o, 3, NF_DECIMAL));
  doc_insert(d, &c, "one\ntwo\nthree", 13, NULL);
  memset(&l, 0, sizeof l);
  l.id = 1;
  for (k = 0; k < 9; k++) { l.lvl[k].fmt = NF_DECIMAL; l.lvl[k].start = 1; sprintf(l.lvl[k].text, "%%%d.", k + 1); l.lvl[k].left = 720 * (k + 1); l.lvl[k].hanging = 360; }
  l.lvl[0].fmt = NF_ORDINAL_TEXT; strcpy(l.lvl[0].text, "%1 part:");
  l.lvl[1].fmt = NF_BULLET; strcpy(l.lvl[1].text, "o"); strcpy(l.lvl[1].font, "Courier New");
  n = doc_add_list(d, &l);
  doc_list_apply(d, 0, 2, n);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("numbers: the labels", !strcmp(L->pl[0].label, "First part:") && !strcmp(L->pl[2].label, "Third part:"));
  lay_free(L);
  xml = wp9_saved_xml(d, "word/numbering.xml");
  ok("numbers: w:numFmt ordinalText, a bullet's own font", xml && strstr(xml, "<w:numFmt w:val=\"ordinalText\"/>") && strstr(xml, "w:ascii=\"Courier New\""));
  free(xml);
  e = docx_again(d, NULL);
  ok("numbers: read back", e && e->nlists && e->p[0]->pf.list && e->lists[e->p[0]->pf.list - 1].lvl[0].fmt == NF_ORDINAL_TEXT &&
     !strcmp(e->lists[e->p[0]->pf.list - 1].lvl[1].font, "Courier New"));
  doc_free(e); doc_free(d);
}

/* Paragraph Tools (WordCraft #5043, core/paratools.c) */
static void paratools(void)
{
  static const char *const t1[] = { "one\vtwo\vthree", "", "  four  five ", "", "six" };
  Doc *d = paras(t1, 5);
  Pos c = P(0, 0);
  int rid;
  u32 np;
  ok("paragraph tools: split lines into paragraphs", para_tool(d, PT_SPLITLINES, 0, 0, -1, &c) == 2 && d->np == 7 &&
     !strcmp(d->p[0]->t, "one") && !strcmp(d->p[2]->t, "three") && runs_sane(d));
  ok("paragraph tools: one undo step", doc_undo(d, &c) && d->np == 5 && !strcmp(d->p[0]->t, "one\vtwo\vthree"));
  ok("paragraph tools: join lines", para_tool(d, PT_JOINLINES, 0, 4, -1, &c) == 2 && !strcmp(d->p[0]->t, "onetwothree"));
  doc_undo(d, &c);
  ok("paragraph tools: delete blank paragraphs", para_tool(d, PT_REMOVEBLANK, 0, 4, -1, &c) == 2 && d->np == 3 && !strcmp(d->p[1]->t, "  four  five "));
  ok("paragraph tools: remove extra spaces (leading ones stay)", para_tool(d, PT_EXTRASPACES, 1, 1, -1, &c) && !strcmp(d->p[1]->t, "  four five "));
  ok("paragraph tools: remove leading spaces", para_tool(d, PT_TRIMLEADING, 0, 2, -1, &c) == 1 && !strcmp(d->p[1]->t, "four five "));
  np = d->np;
  ok("paragraph tools: blank paragraphs between", para_tool(d, PT_BLANKBETWEEN, 0, 2, -1, &c) == 2 && d->np == np + 2 && d->p[1]->len == 0 &&
     d->p[3]->len == 0 && !strcmp(d->p[4]->t, "six"));
  doc_undo(d, &c);
  ok("paragraph tools: separate paragraphs (one after the last too)", para_tool(d, PT_SEPARATE, 0, 2, -1, &c) == 3 && d->np == np + 3 && d->p[5]->len == 0);
  ok("paragraph tools: nothing to do changes nothing", para_tool(d, PT_EXTRASPACES, 0, 0, -1, &c) == 0);
  doc_free(d);
  /* indents and spaces: 4 spaces at 12 pt make a 240-twip first-line indent and back */
  {
    static const char *const t2[] = { "    Indented", "Plain" };
    d = paras(t2, 2);
    ok("paragraph tools: leading spaces become an indent", para_tool(d, PT_SPACESTOINDENT, 0, 1, -1, &c) == 1 && !strcmp(d->p[0]->t, "Indented") &&
       d->p[0]->pf.first == 4 * (i32)para_fmt_at(d->p[0], 0)->size * 5 / 2);
    ok("paragraph tools: and the indent spaces again", para_tool(d, PT_INDENTTOSPACES, 0, 1, -1, &c) == 1 && !strcmp(d->p[0]->t, "    Indented") &&
       d->p[0]->pf.first == 0 && d->p[0]->pf.left == 0 && runs_sane(d));
    doc_free(d);
  }
  /* tracked: deletions marked, not made; text already deleted is passed over */
  {
    static const char *const t3[] = { "a  b\vc" };
    d = paras(t3, 1);
    rid = doc_add_rev(d, "Tester", "2026-11-18T00:00:00Z");
    ok("paragraph tools: tracked, the extra space is marked deleted", para_tool(d, PT_EXTRASPACES, 0, 0, rid, &c) == 1 && !strcmp(d->p[0]->t, "a  b\vc") &&
       para_fmt_at(d->p[0], 1)->rev == REV_DEL && para_fmt_at(d->p[0], 2)->rev != REV_DEL);
    ok("paragraph tools: tracked, a deleted space no longer counts", para_tool(d, PT_EXTRASPACES, 0, 0, rid, &c) == 0);
    ok("paragraph tools: tracked split keeps the break, marked deleted", para_tool(d, PT_SPLITLINES, 0, 0, rid, &c) == 1 && d->np == 2 &&
       !strcmp(d->p[0]->t, "a  b\v") && para_fmt_at(d->p[0], 4)->rev == REV_DEL && !strcmp(d->p[1]->t, "c"));
    doc_free(d);
  }
  /* a table cell's only paragraph stays; a section's last paragraph stays */
  {
    static const char *const t4[] = { "x", "", "", "y" };
    SectFmt s;
    ParaFmt pf;
    d = paras(t4, 4);
    sect_default(&s);
    pf = d->p[1]->pf; pf.sect = (u16)doc_add_sect(d, &s);
    doc_para_format(d, 1, 1, PM_SECT, &pf);
    ok("paragraph tools: an empty paragraph ending a section stays", para_tool(d, PT_REMOVEBLANK, 0, 3, -1, &c) == 1 && d->np == 3 && d->p[1]->pf.sect);
    doc_free(d);
  }
}

void test_wp10(void)
{
  paratools();
  sorting();
  distributed();
  hyphens();
  numbering();
}
