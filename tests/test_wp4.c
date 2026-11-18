/* test_wp4.c - the native tests of WP4 Mail merge, envelopes, labels (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp4() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/merge.h"
#include "../core/envlabel.h"
#include "../core/field.h"
#include "../core/zip.h"
#include "../core/table.h"

static const char csv[] =
  "First Name,Last Name,Company,Address Line 1,City,State,ZIP Code,Country,Amount\r\n"
  "Ann,Smith,,\"1 High St, Flat 2\",London,,N1 1AA,United Kingdom,10\r\n"
  "Bob,Jones,Acme,2 Low Rd,Leeds,,LS1,United Kingdom,250\r\n"
  "\"Cy \"\"C\"\"\",Brown,,3 Main St,Boston,MA,02101,United States,30\r\n"
  "Bob,Jones,Acme,2 Low Rd,Leeds,,LS1,United Kingdom,250\r\n";

/* a field with code at the end of paragraph pi */
static void put(Doc *d, u32 pi, const char *code)
{
  Pos at;
  at.para = pi; at.off = d->p[pi]->len;
  field_insert(d, &at, code, NULL, NULL);
}
static void text_end(Doc *d, u32 pi, const char *s)
{
  Pos at;
  at.para = pi; at.off = d->p[pi]->len;
  doc_insert(d, &at, s, (u32)strlen(s), NULL);
}
/* a small .xlsx made here: shared and inline strings, a number, a boolean and a date */
static u8 *mk_xlsx(size_t *len)
{
  ZipWriter w;
  static const char ct[] = "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>";
  static const char wb[] = "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"People\" sheetId=\"1\" r:id=\"rId7\"/></sheets></workbook>";
  static const char rels[] = "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId7\" Type=\"x/worksheet\" Target=\"worksheets/data.xml\"/></Relationships>";
  static const char ss[] = "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><si><t>Name</t></si><si><t>City</t></si><si><r><t>Zo</t></r><r><t>\xC3\xAB</t></r><rPh><t>x</t></rPh></si></sst>";
  static const char st[] = "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><numFmts><numFmt numFmtId=\"164\" formatCode=\"dd/mm/yyyy\"/></numFmts><cellXfs><xf numFmtId=\"0\"/><xf numFmtId=\"164\"/></cellXfs></styleSheet>";
  static const char sh[] = "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
    "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c><c r=\"C1\" t=\"inlineStr\"><is><t>Joined</t></is></c><c r=\"D1\" t=\"inlineStr\"><is><t>Paid</t></is></c><c r=\"E1\" t=\"inlineStr\"><is><t>N</t></is></c></row>"
    "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\" t=\"inlineStr\"><is><t>Paris</t></is></c><c r=\"C2\" s=\"1\"><v>46324</v></c><c r=\"D2\" t=\"b\"><v>1</v></c><c r=\"E2\"><v>0.30000000000000004</v></c></row>"
    "<row r=\"4\"><c r=\"B4\" t=\"inlineStr\"><is><t>Rome</t></is></c></row></sheetData></worksheet>";
  zip_begin(&w);
  zip_add(&w, "[Content_Types].xml", ct, strlen(ct));
  zip_add(&w, "xl/workbook.xml", wb, strlen(wb));
  zip_add(&w, "xl/_rels/workbook.xml.rels", rels, strlen(rels));
  zip_add(&w, "xl/sharedStrings.xml", ss, strlen(ss));
  zip_add(&w, "xl/styles.xml", st, strlen(st));
  zip_add(&w, "xl/worksheets/data.xml", sh, strlen(sh));
  zip_finish(&w);
  {
    u8 *r;
    *len = w.out.len;
    r = (u8 *)buf_take(&w.out);
    zip_free(&w);
    return r;
  }
}
static int has(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

static void t_data(void)
{
  MergeData *m = md_load_text((const u8 *)csv, sizeof csv - 1);
  u8 *x;
  size_t n;
  char err[128], *c;
  ok("WP4 csv read", m && m->ncols == 9 && m->nrows == 4);
  if (!m) return;
  ok("WP4 csv quotes", !strcmp(m->cell[0][3], "1 High St, Flat 2") && !strcmp(m->cell[2][0], "Cy \"C\""));
  ok("WP4 csv col by name", md_col(m, "zip_code") == 6 && md_col(m, "nope") < 0);
  c = md_csv(m, &n);
  { MergeData *m2 = c ? md_load_text((u8 *)c, n) : NULL;
    ok("WP4 csv written and read again", m2 && m2->nrows == 4 && !strcmp(m2->cell[2][0], "Cy \"C\"") && !strcmp(m2->cell[0][3], "1 High St, Flat 2"));
    md_free(m2); }
  free(c);
  md_free(m);
  /* tab-separated, Latin-1 */
  m = md_load_text((const u8 *)"Name\tTown\nJos\xE9\tM\xFCnchen\n", 22);
  ok("WP4 tab-separated Latin-1", m && m->ncols == 2 && m->nrows == 1 && !strcmp(m->cell[0][1], "M\xC3\xBCnchen"));
  md_free(m);
  /* UTF-16 with a byte order mark (Excel's Unicode text) and semicolons */
  { static const u8 u16[] = { 0xFF, 0xFE, 'A', 0, ';', 0, 'B', 0, '\n', 0, '1', 0, ';', 0, 0xE9, 0, '\n', 0 };
    m = md_load_text(u16, sizeof u16);
    ok("WP4 UTF-16 and semicolons", m && m->ncols == 2 && m->nrows == 1 && !strcmp(m->cell[0][1], "\xC3\xA9"));
    md_free(m); }
  /* .xlsx */
  x = mk_xlsx(&n);
  m = md_load_xlsx(x, n, err, sizeof err);
  ok("WP4 xlsx read", m && m->ncols == 5 && m->nrows == 2);
  if (m) {
    ok("WP4 xlsx strings", !strcmp(m->cols[0], "Name") && !strcmp(m->cell[0][0], "Zo\xC3\xAB") && !strcmp(m->cell[0][1], "Paris"));
    ok("WP4 xlsx date by format", !strcmp(m->cell[0][2], "29/10/2026"));
    ok("WP4 xlsx boolean, number", !strcmp(m->cell[0][3], "TRUE") && !strcmp(m->cell[0][4], "0.3"));
    ok("WP4 xlsx gap rows", !strcmp(m->cell[1][1], "Rome") && !m->cell[1][0][0]);
  }
  md_free(m);
  free(x);
  /* a .docx's first table */
  {
    Doc *d = doc_new();
    Pos at = doc_start(d);
    u8 *b;
    tbl_insert(d, &at, 3, 2, 9000);
    { Pos p; const char *t[6] = { "Name", "City", "Ann", "Oslo", "Bo", "Rome" }; int i;
      for (i = 0; i < 6; i++) { p.para = tbl_cell_para(d, doc_start(d), i / 2, i % 2); p.off = 0; doc_insert(d, &p, t[i], (u32)strlen(t[i]), NULL); } }
    b = docx_save(d, &n);
    m = b ? md_load_docx(b, n, err, sizeof err) : NULL;
    ok("WP4 docx table read", m && m->ncols == 2 && m->nrows == 2 && !strcmp(m->cols[1], "City") && !strcmp(m->cell[1][1], "Rome"));
    md_free(m); free(b); doc_free(d);
  }
}

/* a main document: Dear «First Name» «Last Name», IF, address block, greeting */
static Doc *main_doc(void)
{
  Doc *d = doc_new();
  MergeInfo *mi;
  merge_set_data(d, md_load_text((const u8 *)csv, sizeof csv - 1));
  mi = d->merge;
  mi->type = MT_LETTERS;
  put(d, 0, " ADDRESSBLOCK \\f \"<<_TITLE0_ >><<_FIRST0_>><< _LAST0_>>\\n<<_COMPANY_\\n>><<_STREET1_\\n>><<_CITY_>><<, _STATE_>><< _POSTAL_>><<\\n_COUNTRY_>>\" \\l 1033 \\c 2 \\e \"United Kingdom\" \\d ");
  doc_insert(d, &(Pos){ 0, d->p[0]->len }, "\n", 1, NULL);
  put(d, 1, " GREETINGLINE \\f \"<<_BEFORE_ Dear >><<_FIRST0_>><< _LAST0_>><<_AFTER_ ,>>\" \\l 1033 \\e \"Dear Sir or Madam,\" ");
  doc_insert(d, &(Pos){ 1, d->p[1]->len }, "\nYou owe ", 9, NULL);
  put(d, 2, " MERGEFIELD Amount \\b \"$\" ");
  text_end(d, 2, ". ");
  { char *c = field_code_parse(" IF \"{ MERGEFIELD City }\" = \"Leeds\" \"Northern.\" \"Elsewhere.\" "); Pos at = { 2, d->p[2]->len }; field_insert(d, &at, c, NULL, NULL); free(c); }
  text_end(d, 2, " #");
  put(d, 2, " MERGESEQ ");
  return d;
}
static void t_merge(void)
{
  Doc *d = main_doc(), *m;
  MergeInfo *mi = d->merge;
  char out[512], *t;
  int n = 0;
  Buf e;
  ok("WP4 auto match", mi->data && merge_af_col(mi, AF_FIRST) == 0 && merge_af_col(mi, AF_ADDR1) == 3 && merge_af_col(mi, AF_POSTAL) == 6 && merge_af_col(mi, AF_COUNTRY) == 7);
  merge_address(mi, 0, d->fields[0].code, out, sizeof out);
  ok("WP4 address block (country left out)", !strcmp(out, "Ann Smith\v1 High St, Flat 2\vLondon N1 1AA"));
  merge_address(mi, 2, d->fields[0].code, out, sizeof out);
  ok("WP4 address block (country)", !strcmp(out, "Cy \"C\" Brown\v3 Main St\vBoston, MA 02101\vUnited States"));
  merge_greeting(mi, 1, d->fields[1].code, out, sizeof out);
  ok("WP4 greeting line", !strcmp(out, "Dear Bob Jones,"));
  { MergeInfo none; memset(&none, 0, sizeof none); merge_address(&none, -1, " ADDRESSBLOCK ", out, sizeof out);
    ok("WP4 address sample", !strcmp(out, "Mr. Joshua Randall Jr.\vBlue Sky Airlines\v1 Main St.\vAny City, WA 98052")); }
  /* the windows' codes */
  { char code[700];
    merge_address_code(5, 1, 1, 2, "United Kingdom", code, sizeof code);
    merge_address(mi, 1, code, out, sizeof out);
    ok("WP4 address code", !strcmp(out, "Bob Jones\vAcme\v2 Low Rd\vLeeds LS1"));
    merge_address_code(7, 0, 0, 0, "", code, sizeof code);
    merge_address(mi, 1, code, out, sizeof out);
    ok("WP4 address code, name only", !strcmp(out, "Bob Jones"));
    merge_greeting_code(1, 1, 1, "To whom it may concern:", code, sizeof code);
    merge_greeting(mi, 2, code, out, sizeof out);
    ok("WP4 greeting code", !strcmp(out, "To Cy \"C\":"));
    { MergeInfo e2 = *mi; memset(e2.map, 0, sizeof e2.map); strcpy(e2.map[AF_FIRST], "\x01"); strcpy(e2.map[AF_LAST], "\x01");
      merge_greeting(&e2, 2, code, out, sizeof out);
      ok("WP4 greeting for invalid names", !strcmp(out, "To whom it may concern:")); }
    ok("WP4 placeholders", merge_placeholder(code, out, sizeof out) && !strcmp(out, "\xC2\xAB" "GreetingLine" "\xC2\xBB") &&
       merge_placeholder(" MERGEFIELD First_Name ", out, sizeof out) && !strcmp(out, "\xC2\xAB" "First_Name" "\xC2\xBB") && !merge_placeholder(" DATE ", out, sizeof out)); }
  /* preview */
  mi->rec = 1;
  merge_preview(d, 1);
  t = all(d);
  ok("WP4 preview record 2", has(t, "Dear Bob Jones,") && has(t, "$250") && has(t, "Northern.") && has(t, "Acme"));
  free(t);
  merge_preview(d, 0);
  t = all(d);
  ok("WP4 preview off", has(t, "\xC2\xAB" "GreetingLine" "\xC2\xBB") && has(t, "\xC2\xAB" "Amount" "\xC2\xBB") && has(t, "\xC2\xAB" "Merge Sequence #" "\xC2\xBB"));
  free(t);
  /* filter and sort, through the query */
  mi->nfilt = 1;
  strcpy(mi->filt[0].col, "Country"); mi->filt[0].op = MF_EQ; strcpy(mi->filt[0].val, "united kingdom");
  strcpy(mi->sortcol[0], "Amount"); mi->sortdesc[0] = 1;
  merge_apply_view(mi);
  ok("WP4 filter and sort", mi->nview == 3 && mi->view[0] == 1 && mi->view[1] == 3 && mi->view[2] == 0);
  ok("WP4 query", !strcmp(mi->query, "SELECT * FROM `Recipients` WHERE (`Country` = 'united kingdom') ORDER BY `Amount` DESC"));
  { MergeInfo q; memset(&q, 0, sizeof q); strcpy(q.query, "SELECT * FROM `x.csv` WHERE (`A` LIKE '%o''k%') OR (`B` IS NOT NULL) AND (`C` >= 3) ORDER BY `C` ASC, `D` DESC");
    merge_parse_query(&q);
    ok("WP4 query read", q.nfilt == 3 && q.filt[0].op == MF_CONTAINS && !strcmp(q.filt[0].val, "o'k") && q.filt[1].op == MF_NOTBLANK && q.filt[1].or_ &&
       q.filt[2].op == MF_GE && !strcmp(q.filt[2].val, "3") && !strcmp(q.sortcol[1], "D") && q.sortdesc[1] && !q.sortdesc[0]); }
  /* duplicates, an unchecked record */
  { u8 dup[8]; ok("WP4 duplicates", merge_duplicates(mi->data, dup) == 1 && dup[3]); mi->data->on[3] = 0; }
  ok("WP4 count", merge_count(mi) == 2);
  ok("WP4 find recipient", merge_find(mi, 0, "smi", -1) == 2 && merge_find(mi, 0, "zzz", -1) < 0);
  /* merging */
  buf_init(&e);
  m = merge_build(d, 0, merge_count(mi) - 1, NULL, NULL, &n, &e);
  ok("WP4 merged", m && n == 2);
  if (m) {
    t = all(m);
    ok("WP4 merged text", has(t, "Dear Bob Jones,") && has(t, "Dear Ann Smith,") && has(t, "Northern.") && has(t, "Elsewhere.") && has(t, "#2"));
    ok("WP4 merged: fields gone", m->np == 6 && !has(t, "\xC2\xAB") && !has(t, "\xF4\x84"));
    ok("WP4 merged: a section a record", m->p[2]->pf.sect && m->sects[m->p[2]->pf.sect - 1].start == SB_NEXT && !m->p[5]->pf.sect);
    free(t);
    { unsigned lost; Doc *r2 = docx_again(m, &lost); ok("WP4 merged doc saves", r2 && r2->np == 6); doc_free(r2); }
    doc_free(m);
  }
  ok("WP4 no merge errors", e.len == 0);
  {
    int files = 0;
    size_t n1 = 0, n2 = 0;
    u8 *a, *b;
    ok("WP4 merge to one PDF", merge_to_pdf(d, 0, 1, 0, -1, "/tmp/quill_wp4_all.pdf", &fs, NULL, NULL, &files, NULL) && files == 1);
    ok("WP4 merge to a PDF each", merge_to_pdf(d, 0, 1, 1, 1, "/tmp/quill_wp4.pdf", &fs, NULL, NULL, &files, NULL) && files == 2);
    a = file_read("/tmp/Jones.pdf", &n1); b = file_read("/tmp/Smith.pdf", &n2);
    ok("WP4 PDFs named by a column", a && b && n1 > 500 && !memcmp(a, "%PDF", 4));
    free(a); free(b);
  }
  buf_free(&e);
  /* Check for Errors finds a field not in the list */
  put(d, 2, " MERGEFIELD Nickname ");
  buf_init(&e);
  ok("WP4 check for errors", merge_check(d, &e) >= 1 && has(e.data, "Nickname"));
  buf_free(&e);
  doc_free(d);
}
static int ask_cb(void *ctx, const char *prompt, const char *def, char *out, size_t cap)
{
  int *n = (int *)ctx;
  (*n)++;
  snprintf(out, cap, "%s%d", strcmp(prompt, "Your name?") ? "?" : "Z", *n);
  return 1;
}
static void t_rules(void)
{
  Doc *d = doc_new(), *m;
  int n = 0, asked = 0;
  char *t, *c;
  merge_set_data(d, md_load_text((const u8 *)csv, sizeof csv - 1));
  d->merge->type = MT_DIRECTORY;
  c = field_code_parse(" SKIPIF { MERGEFIELD City } = \"Leeds\" ");
  { Pos at = doc_start(d); field_insert(d, &at, c, NULL, NULL); }
  free(c);
  put(d, 0, " SET greet \"Hi\" ");
  put(d, 0, " REF greet ");
  text_end(d, 0, " ");
  put(d, 0, " MERGEFIELD First_Name ");
  text_end(d, 0, "/");
  put(d, 0, " MERGEREC ");
  text_end(d, 0, "/");
  put(d, 0, " FILLIN \"Your name?\" \\d \"x\" \\o ");
  text_end(d, 0, "/");
  put(d, 0, " ASK who \"Who?\" \\d \"d\" ");
  put(d, 0, " who ");
  text_end(d, 0, "/");
  c = field_code_parse(" NEXTIF { MERGEFIELD Amount } > 100 ");
  { Pos at = { 0, d->p[0]->len }; field_insert(d, &at, c, NULL, NULL); }
  free(c);
  put(d, 0, " MERGEFIELD \"Last Name\" ");
  m = merge_build(d, 0, 3, ask_cb, &asked, &n, NULL);
  ok("WP4 rules merged", m && n == 2);
  t = m ? all(m) : NULL;
  ok("WP4 rules text", t && !strcmp(t, "Hi Ann/1/Z1/?2/Smith\nHi Cy \"C\"/3/Z1/?3/Brown"));
  if (!t || strcmp(t, "Hi Ann/1/Z1/?2/Smith\nHi Cy \"C\"/3/Z1/?3/Brown")) printf("  got [%s]\n", t ? t : "(none)");
  free(t); doc_free(m);
  /* NEXTIF moves on: a record over 100 takes the next one's last name */
  doc_free(d);
}
/* IF...THEN...ELSE, MERGESEQ against MERGEREC after a skipped record */
static void t_rules2(void)
{
  Doc *d = doc_new(), *m;
  int n = 0;
  char *t, *c;
  merge_set_data(d, md_load_text((const u8 *)csv, sizeof csv - 1));
  d->merge->type = MT_DIRECTORY;
  c = field_code_parse(" SKIPIF { MERGEFIELD City } = \"London\" ");
  { Pos at = doc_start(d); field_insert(d, &at, c, NULL, NULL); }
  free(c);
  c = field_code_parse(" IF { MERGEFIELD Amount } > 100 \"big\" \"small\" ");
  { Pos at = { 0, d->p[0]->len }; field_insert(d, &at, c, NULL, NULL); }
  free(c);
  text_end(d, 0, "/");
  put(d, 0, " MERGESEQ ");
  text_end(d, 0, "/");
  put(d, 0, " MERGEREC ");
  m = merge_build(d, 0, 3, NULL, NULL, &n, NULL);
  t = m ? all(m) : NULL;
  ok("WP4 IF, MERGESEQ, MERGEREC", m && n == 3 && t && !strcmp(t, "big/1/2\nsmall/2/3\nbig/3/4"));
  if (!t || strcmp(t, "big/1/2\nsmall/2/3\nbig/3/4")) printf("  got [%s] n=%d\n", t ? t : "(none)", n);
  free(t); doc_free(m); doc_free(d);
}
static void t_docx(void)
{
  Doc *d = main_doc(), *r;
  MergeInfo *mi = d->merge;
  unsigned lost = 0;
  u8 *b;
  size_t n;
  strcpy(mi->src, "Work:Lists/people.csv");
  mi->type = MT_LABELS;
  mi->nfilt = 1; strcpy(mi->filt[0].col, "City"); mi->filt[0].op = MF_NE; strcpy(mi->filt[0].val, "Leeds");
  strcpy(mi->sortcol[0], "Last Name");
  strcpy(mi->map[AF_COMPANY], "Company");
  mi->rec = 1; mi->viewmerged = 1;
  merge_apply_view(mi);
  b = docx_save(d, &n);
  {
    ZipReader z;
    char *s = NULL, *rl = NULL;
    size_t sn;
    int i;
    if (b && zip_open(&z, b, n)) {
      if ((i = zip_find(&z, "word/settings.xml")) >= 0) s = (char *)zip_read(&z, i, &sn);
      if ((i = zip_find(&z, "word/_rels/settings.xml.rels")) >= 0) rl = (char *)zip_read(&z, i, &sn);
      zip_close(&z);
    }
    ok("WP4 settings written", has(s, "<w:mailMerge") && has(s, "w:val=\"mailingLabels\"") && has(s, "<w:dataSource r:id=\"rIdMM1\"/>") &&
       has(s, "ORDER BY `Last Name` ASC") && has(s, "<w:viewMergedData/><w:activeRecord w:val=\"2\"/>") && has(s, "w:mappedName w:val=\"Company\""));
    ok("WP4 settings order", s && strstr(s, "<w:mailMerge") < strstr(s, "<w:defaultTabStop"));
    ok("WP4 settings rels", has(rl, "Target=\"Work:Lists/people.csv\" TargetMode=\"External\""));
    free(s); free(rl);
  }
  r = b ? docx_load(b, n, &lost, NULL, 0) : NULL;
  free(b);
  /* an attached template as well (WP23): one settings.xml.rels with both, settings in schema order */
  if (r) {
    u8 *b2;
    size_t n2;
    r->attached = str_dup("Work:Templates/Letter.dotx");
    b2 = docx_save(r, &n2);
    {
      ZipReader z;
      char *s = NULL, *rl = NULL;
      size_t sn;
      int i;
      if (b2 && zip_open(&z, b2, n2)) {
        if ((i = zip_find(&z, "word/settings.xml")) >= 0) s = (char *)zip_read(&z, i, &sn);
        if ((i = zip_find(&z, "word/_rels/settings.xml.rels")) >= 0) rl = (char *)zip_read(&z, i, &sn);
        zip_close(&z);
      }
      ok("WP4 settings rels shared with the attached template", has(rl, "Id=\"rIdStTpl\"") && has(rl, "Target=\"Work:Templates/Letter.dotx\"") &&
         has(rl, "Id=\"rIdMM1\"") && has(rl, "Id=\"rIdMM2\"") && has(rl, "Target=\"Work:Lists/people.csv\""));
      ok("WP4 attachedTemplate before mailMerge", s && strstr(s, "<w:attachedTemplate") && strstr(s, "<w:mailMerge") &&
         strstr(s, "<w:attachedTemplate") < strstr(s, "<w:mailMerge"));
      free(s); free(rl);
    }
    {
      Doc *r3 = b2 ? docx_load(b2, n2, &lost, NULL, 0) : NULL;
      ok("WP4 both read back", r3 && r3->attached && !strcmp(r3->attached, "Work:Templates/Letter.dotx") && r3->merge &&
         !strcmp(r3->merge->src, "Work:Lists/people.csv"));
      doc_free(r3);
    }
    free(b2);
    free(r->attached); r->attached = NULL;
  }
  ok("WP4 mailMerge read back", r && r->merge && r->merge->type == MT_LABELS && !strcmp(r->merge->src, "Work:Lists/people.csv") &&
     r->merge->nfilt == 1 && r->merge->filt[0].op == MF_NE && !strcmp(r->merge->sortcol[0], "Last Name") && r->merge->viewmerged &&
     r->merge->rec == 1 && !strcmp(r->merge->map[AF_COMPANY], "Company") && !strcmp(r->merge->map[AF_FIRST], "First Name"));
  ok("WP4 merge fields kept", r && r->nfields == d->nfields);
  /* a second save writes the same settings */
  if (r) {
    Doc *r2 = docx_again(r, &lost);
    ok("WP4 mailMerge twice", r2 && r2->merge && !strcmp(r2->merge->query, r->merge->query) && r2->merge->type == MT_LABELS);
    doc_free(r2);
  }
  doc_free(r); doc_free(d);
  /* Word's own form: a Windows path, an odso with recipientData and an unknown child kept */
  {
    static const char settings_mm[] = "<w:mailMerge><w:mainDocumentType w:val=\"formLetters\"/><w:linkToQuery/><w:dataType w:val=\"textFile\"/>"
      "<w:connectString w:val=\"\"/><w:query w:val=\"SELECT * FROM C:\\Users\\a\\list.txt\"/><w:dataSource r:id=\"rId1\"/>"
      "<w:doNotSuppressBlankLines/><w:odso><w:udl w:val=\"x\"/><w:src r:id=\"rId2\"/><w:colDelim w:val=\"9\"/><w:type w:val=\"text\"/><w:fHdr/>"
      "<w:fieldMapData><w:column w:val=\"0\"/><w:lid w:val=\"en-US\"/></w:fieldMapData><w:fieldMapData><w:type w:val=\"dbColumn\"/><w:name w:val=\"Anrede\"/>"
      "<w:mappedName w:val=\"Courtesy Title\"/><w:column w:val=\"3\"/><w:lid w:val=\"en-US\"/></w:fieldMapData><w:recipientData r:id=\"rId3\"/></w:odso></w:mailMerge>";
    Doc *w = doc_new();
    Xml x;
    xml_init(&x, settings_mm, strlen(settings_mm));
    xml_next(&x);
    merge_read_setting(w, &x);
    xml_free(&x);
    ok("WP4 Word's mailMerge read", w->merge && w->merge->type == MT_LETTERS && w->merge->linkquery && w->merge->coldelim == 9 &&
       !strcmp(w->merge->map[AF_TITLE], "Anrede") && !w->merge->map[AF_ID][0] && has(w->merge->extra, "doNotSuppressBlankLines") &&
       has(w->merge->odsox, "w:udl") && !has(w->merge->odsox, "recipientData"));
    strcpy(w->merge->src, "C:\\Users\\a\\list.txt");
    { Buf o; buf_init(&o); merge_write_settings(w, &o, 1); buf_addc(&o, 0);
      ok("WP4 Word's mailMerge written", has(o.data, "<w:doNotSuppressBlankLines/>") && has(o.data, "<w:udl w:val=\"x\"/>") && has(o.data, "<w:colDelim w:val=\"9\"/>") &&
         has(o.data, "SELECT * FROM C:\\Users\\a\\list.txt") && strstr(o.data, "<w:dataSource") < strstr(o.data, "<w:doNotSuppressBlankLines"));
      buf_free(&o); }
    doc_free(w);
  }
}
static void t_envlabels(void)
{
  EnvSpec e;
  Doc *d, *body;
  int k, i, nnext = 0;
  memset(&e, 0, sizeof e);
  e.size = 0; e.delivery = "Ann Smith\n1 High St\nLondon"; e.ret = "Quill\nAmiga Way";
  d = env_doc(&e);
  ok("WP4 envelope", d && d->np == 5 && d->page.width == 13680 && d->page.height == 5940 && d->p[2]->pf.left == 5745 - 360 && d->p[2]->pf.before > 1500);
  doc_free(d);
  body = doc_new();
  text_end(body, 0, "Letter");
  ok("WP4 envelope added to document", env_add_to_doc(body, &e) && body->np == 6 && body->p[4]->pf.sect &&
     body->sects[body->p[4]->pf.sect - 1].haspage && body->sects[body->p[4]->pf.sect - 1].page.width == 13680);
  { unsigned lost; Doc *r = docx_again(body, &lost); ok("WP4 envelope section saved", r && r->np == 6 && r->p[4]->pf.sect && doc_sect(r, 0)->haspage && doc_sect(r, 0)->page.width == 13680); doc_free(r); }
  doc_free(body);
  for (k = 0; k < label_ndefs && strncmp(label_defs[k].product, "5160", 4); k++) ;
  d = label_doc(&label_defs[k], "Same\nLabel", 0, 0, 0, 0);
  ok("WP4 labels: full page", d && d->ntables == 1 && d->tables[0].ncols == 5 && d->np == 3 * 10 * 2 + 2 * 10 + 1);
  if (d) {
    char *t = all(d);
    int same = 0;
    const char *p = t;
    while ((p = strstr(p, "Same\nLabel"))) { same++; p++; }
    ok("WP4 labels: 30 the same", same == 30);
    free(t);
    { unsigned lost; Doc *r = docx_again(d, &lost); ok("WP4 labels saved", r && r->ntables == 1 && r->tables[0].grid[1] == 180); doc_free(r); }
    doc_free(d);
  }
  d = label_doc(&label_defs[k], "One", 1, 2, 1, 0);
  { char *t = d ? all(d) : NULL; ok("WP4 labels: single label", t && strstr(t, "One") && d->p[tbl_cell_para(d, doc_start(d), 2, 2)]->len == 3); free(t); doc_free(d); }
  d = label_doc(&label_defs[k], NULL, 0, 0, 0, 1);
  for (i = 0; d && i < d->nfields; i++) { char nm[16]; field_name(d->fields[i].code, nm, sizeof nm); if (!strcmp(nm, "NEXT")) nnext++; }
  ok("WP4 labels: merge (NEXT in 29)", d && nnext == 29);
  if (d) {
    Doc *m;
    int n = 0;
    Pos a = { 0, 0 };
    merge_set_data(d, md_load_text((const u8 *)csv, sizeof csv - 1));
    d->merge->type = MT_LABELS;
    field_insert(d, &a, " MERGEFIELD First_Name ", NULL, NULL);
    ok("WP4 update labels", label_update(d) == 29);
    m = merge_build(d, 0, 3, NULL, NULL, &n, NULL);
    { char *t = m ? all(m) : NULL; ok("WP4 labels merged on one page", m && n == 1 && has(t, "Ann\t\tBob\t\tCy \"C\"\nBob\t") && has(t, "Cy") && m->p[0]->pf.cell.table);
      free(t); }
    doc_free(m);
    doc_free(d);
  }
}
void test_wp4(void)
{
  t_data();
  t_merge();
  t_rules();
  t_rules2();
  t_docx();
  t_envlabels();
}
