/* test_wp2.c - the native tests of WP2 Captions, tables of figures, index, authorities, custom TOC (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp2() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/refs.h"
#include "../core/field.h"
#include "../core/layout.h"
#include "../core/toc.h"
#include "../core/table.h"
#include "../core/pdf.h"

/* a document of paragraphs ('\n' between them); heading paragraphs given Heading 1 or 2 by a leading "#" or "##" */
static Doc *mk(const char *text)
{
  Doc *d = doc_new();
  Pos at = P(0, 0);
  char *t = str_dup(text), *line, *save = t;
  u32 pi = 0;
  int first = 1;
  for (line = t; line; pi++) {
    char *nl = strchr(line, '\n');
    int lv = 0;
    if (nl) *nl = 0;
    while (line[0] == '#') { lv++; line++; }
    if (!first) doc_insert(d, &at, "\n", 1, NULL);
    first = 0;
    doc_insert(d, &at, line, (u32)strlen(line), NULL);
    if (lv) doc_apply_style(d, pi, pi, doc_style(d, lv == 1 ? "Heading1" : "Heading2"));
    else doc_apply_style(d, pi, pi, 0);
    line = nl ? nl + 1 : NULL;
  }
  free(save);
  doc_clear_undo(d);
  return d;
}
static int plain_is(Doc *d, u32 pi, const char *want)
{
  char t[512];
  if (pi >= d->np) { printf("  (no paragraph %u)\n", (unsigned)pi); return 0; }
  refs_plain(d, pi, 0, d->p[pi]->len, t, sizeof t);
  if (strcmp(t, want)) { printf("  paragraph %u is \"%s\", not \"%s\"\n", (unsigned)pi, t, want); return 0; }
  return 1;
}
static void dump(Doc *d)
{
  u32 pi;
  char t[512];
  for (pi = 0; pi < d->np; pi++) { refs_plain(d, pi, 0, d->p[pi]->len, t, sizeof t); printf("  %2u [%s] %s\n", (unsigned)pi, d->styles[d->p[pi]->pf.style].name, t); }
}
static Layout *laid(Doc *d)
{
  Layout *L = lay_new(d, &fs);
  if (L) lay_update(L);
  return L;
}
/* the paragraph whose plain text starts with s, or -1 */
static long find_para(Doc *d, const char *s)
{
  u32 pi;
  char t[512];
  for (pi = 0; pi < d->np; pi++) { refs_plain(d, pi, 0, d->p[pi]->len, t, sizeof t); if (!strncmp(t, s, strlen(s))) return (long)pi; }
  return -1;
}
static long find_exact(Doc *d, const char *s)
{
  u32 pi;
  char t[512];
  for (pi = 0; pi < d->np; pi++) { refs_plain(d, pi, 0, d->p[pi]->len, t, sizeof t); if (!strcmp(t, s)) return (long)pi; }
  return -1;
}
static int count_fields(Doc *d, const char *name)
{
  u32 pi, o;
  int n = 0;
  char nm[16];
  for (pi = 0; pi < d->np; pi++) for (o = 0; o + 4 <= d->p[pi]->len; o++) {
    if (!IS_FSTART(d->p[pi]->t + o)) continue;
    field_name(d->fields[fmark_index(d->p[pi]->t + o)].code, nm, sizeof nm);
    if (!strcmp(nm, name)) n++;
    o += 3;
  }
  return n;
}

static void captions(void)
{
  Doc *d = mk("Intro\n\nMiddle\n"), *e;
  Pos at;
  RefsInfo *ri;
  char pv[64];
  int ch;
  at = P(1, 0);
  refs_caption_preview(d, at, 0, pv, sizeof pv);
  ok("caption: preview Figure 1", !strcmp(pv, "Figure 1"));
  ok("caption: inserted in the empty paragraph", refs_insert_caption(d, &at, 0, ": First", -1, NULL) && d->np == 4 && plain_is(d, 1, "Figure 1: First"));
  ok("caption: in the caption style", !strcmp(d->styles[d->p[1]->pf.style].name, "caption") && (d->styles[d->p[1]->pf.style].cf.flags & CF_ITALIC));
  at = P(0, 2);
  ok("caption: a new one after a paragraph with text", refs_insert_caption(d, &at, 0, ": Zero", -1, NULL) && d->np == 5 && plain_is(d, 1, "Figure 1: Zero"));
  ok("caption: the later one numbered again", plain_is(d, 2, "Figure 2: First"));
  ok("caption: one undo step each", doc_undo(d, &at) && plain_is(d, 1, "Figure 1: First") && d->np == 4);
  /* Table numbered I, II and without its label */
  ri = refs_info(d);
  ri->lab[1].fmt = CN_ROMAN_UP;
  at = P(3, 0);
  ok("caption: Table in ROMAN", refs_insert_caption(d, &at, 1, " Data", -1, NULL) && plain_is(d, 3, "Table I Data"));
  ri->lab[2].nolabel = 1;
  at = P(3, 0);
  ok("caption: an equation without its label", refs_insert_caption(d, &at, 2, "", -1, NULL) && plain_is(d, 4, "1"));
  /* a new label, kept in settings.xml */
  ch = refs_add_label(d, "Chart");
  ri->lab[ch].pos = CP_ABOVE; ri->lab[ch].fmt = CN_ALPHA; ri->lab[ch].sep = CSEP_EMDASH;
  at = P(0, 0);
  ok("caption: a new label", ch == 3 && refs_insert_caption(d, &at, ch, "", -1, NULL) && plain_is(d, 1, "Chart a"));
  e = docx_again(d, NULL);
  ok("caption: labels through .docx (w:captions)", e && e->refs && refs_label(e, "Chart") == 3 && e->refs->lab[3].pos == CP_ABOVE &&
     e->refs->lab[3].fmt == CN_ALPHA && e->refs->lab[3].sep == CSEP_EMDASH && e->refs->lab[1].fmt == CN_ROMAN_UP && e->refs->lab[2].nolabel);
  ok("caption: the SEQ fields through .docx", e && count_fields(e, "SEQ") == 4 && plain_is(e, 2, "Figure 1: First"));
  doc_free(e); doc_free(d);

  /* numbered with the chapter: STYLEREF 1 \s, a hyphen */
  d = mk("#One\n\n#Two\n\n");
  doc_heading_numbering(d, LP_HEADINGS);
  ri = refs_info(d);
  ri->lab[0].chap = 1; ri->lab[0].heading = 1; ri->lab[0].sep = CSEP_HYPHEN;
  at = P(1, 0); refs_insert_caption(d, &at, 0, "", -1, NULL);
  at = P(3, 0); refs_insert_caption(d, &at, 0, "", -1, NULL);
  ok("caption: chapter numbers", plain_is(d, 1, "Figure 1-1") && plain_is(d, 3, "Figure 2-1") && count_fields(d, "STYLEREF") == 2);
  at = P(1, 0);
  refs_caption_preview(d, P(1, 0), 0, pv, sizeof pv);
  ok("caption: chapter preview", !strcmp(pv, "Figure 1-2"));
  doc_free(d);

  /* a table's caption above it; a picture's below its paragraph */
  d = mk("Before\nAfter");
  at = P(0, 6);
  {
    Pos t = P(1, 0);
    tbl_insert(d, &t, 2, 2, 4000);
  }
  {
    u32 pi;
    for (pi = 0; pi < d->np && !d->p[pi]->pf.cell.table; pi++) ;
    at = P(pi, 0);
    ok("caption: above a table", pi < d->np && refs_insert_caption(d, &at, 1, "", CP_ABOVE, NULL) && plain_is(d, pi, "Table 1") && !d->p[pi]->pf.cell.table &&
       d->p[pi + 1]->pf.cell.table);
  }
  doc_free(d);
}

static void contents_and_figures(void)
{
  Doc *d = mk("Title\n#Alpha\ntext\n##Beta\n\n#Gamma\ntext"), *e;
  Layout *L;
  Pos at = P(1, 0), st, en;
  int again, k;
  u32 pi;
  L = laid(d);
  ok("toc: a field", doc_toc(d, L, &at, &again) && refs_block_find(d, BK_TOC, 0, &st, &en) >= 0 && plain_is(d, 1, "Contents") && plain_is(d, 2, "Alpha "));
  ok("toc: entries linked to _Toc bookmarks", d->p[2]->r[0].f.link || d->p[2]->r[1 < d->p[2]->nr ? 1 : 0].f.link);
  ok("toc: levels indented", d->p[3]->pf.left == 220 && plain_is(d, 3, "Beta "));
  /* a picture's caption, then a table of figures from \c "Figure" */
  pi = (u32)find_para(d, "Gamma");
  at = P(pi + 1, 0);
  doc_insert(d, &at, "\n", 1, NULL);
  at = P(pi + 1, 0);
  refs_insert_caption(d, &at, 0, ": A chart", -1, NULL);
  at = doc_end_pos(d);
  lay_free(L); L = laid(d);
  k = refs_block_insert(d, &at, " TOC \\h \\z \\c \"Figure\" ", NULL, L, LEAD_HYPHEN);
  {
    long fp = find_para(d, "Figure 1: A chart");
    long tp = -1;
    u32 i;
    for (i = (u32)fp + 1; i < d->np; i++) if (!strcmp(d->styles[d->p[i]->pf.style].name, "table of figures")) { tp = (long)i; break; }
    ok("tof: inserted with its entry", k >= 0 && tp > 0 && plain_is(d, (u32)tp, "Figure 1: A chart ") && d->p[tp]->pf.tablead[0] == LEAD_HYPHEN);
  }
  /* \a: without the label and number */
  at = doc_end_pos(d);
  refs_block_insert(d, &at, " TOC \\h \\z \\a \"Figure\" ", NULL, L, -1);
  ok("tof: \\a leaves out the label", plain_is(d, d->np - 1, "A chart "));
  /* a new heading: Update Fields makes the table afresh */
  at = doc_end_pos(d);
  doc_insert(d, &at, "\nDelta", 6, NULL);
  doc_apply_style(d, d->np - 1, d->np - 1, doc_style(d, "Heading1"));
  {
    FieldEnv fe;
    field_env(&fe);
    lay_free(L); L = laid(d); fe.L = L;
    field_update_all(d, &fe);
    ok("toc: Update Fields brings it up to date", find_para(d, "Delta") > 0 && find_para(d, "Delta") < find_para(d, "Alpha") + 10 &&
       plain_is(d, (u32)find_para(d, "Gamma") , "Gamma "));
  }
  {
    int n = 0;
    for (pi = 0; pi < d->np; pi++) if (toc_para(d, d->p[pi])) n++;
    ok("toc: four entries now", n == 4);
  }
  e = docx_again(d, NULL);
  ok("toc: TOC fields through .docx", e && count_fields(e, "TOC") == 3 && refs_block_find(e, BK_TOF, 0, &st, &en) >= 0);
  {
    int n = 0;
    for (pi = 0; e && pi < e->np; pi++) if (toc_para(e, e->p[pi])) n++;
    ok("toc: its entries too", n == 4);
  }
  /* the TOC removed with its title */
  {
    u32 np = d->np;
    ok("toc: removed", doc_toc_remove(d, NULL) && d->np == np - 5 && find_para(d, "Contents") < 0);
  }
  lay_free(L); doc_free(e); doc_free(d);

  /* an old table (toc paragraphs without the field) is taken over */
  d = mk("#Head\nx\n#Next");
  {
    Pos p = P(0, 0);
    Para *q;
    int st1 = refs_style(d, RS_TOC, 1);
    doc_insert(d, &p, "Old entry\t9\n", 12, NULL);
    q = d->p[0];
    q->pf.style = (u8)st1;
    L = laid(d);
    ok("toc: an old table becomes a field", doc_toc(d, L, NULL, &again) && count_fields(d, "TOC") == 1 && find_para(d, "Old entry") < 0 && plain_is(d, 0, "Head "));
    lay_free(L);
  }
  doc_free(d);
}

static void index_tests(void)
{
  Doc *d = mk("Apples and pears.\nBananas are yellow.\nThe apple tree.\nCherries."), *e;
  Layout *L;
  Pos at, st, en;
  int k;
  long ip;
  at = P(0, 6);
  ok("index: XE marked", refs_mark_entry(d, &at, "Apple", "", "", "", 0, 0) >= 0 && count_fields(d, "XE") == 1);
  at = P(1, 7);
  refs_mark_entry(d, &at, "Fruit", "banana", "", "", 1, 0);
  at = P(0, 16);
  refs_mark_entry(d, &at, "Fruit", "pear", "", "", 0, 1);
  at = P(3, 8);
  refs_mark_entry(d, &at, "Cherry", "", "See Fruit", "", 0, 0);
  at = P(2, 3);
  refs_mark_entry(d, &at, "zeta: with \"quotes\"", "", "", "", 0, 0);
  ok("index: XE code escaped", strstr(d->fields[d->nfields - 1].code, "zeta\\: with \\\"quotes\\\"") != NULL);
  ok("index: Mark All finds 'apple' once a paragraph", refs_mark_all(d, "apple", "Apple", "", "", "", 0, 0) == 1 && count_fields(d, "XE") == 6);
  L = laid(d);
  at = doc_end_pos(d);
  k = refs_block_insert(d, &at, " INDEX \\e \"\t\" \\c \"2\" \\z \"1033\" \\h \"A\" ", NULL, L, LEAD_DOT);
  ip = find_exact(d, "A");
  if (getenv("WP2DUMP")) dump(d);
  ok("index: inserted with letter headings", k >= 0 && ip > 0 && plain_is(d, (u32)ip + 1, "Apple 1") && !strcmp(d->styles[d->p[ip]->pf.style].name, "index heading"));
  ip = find_para(d, "banana");
  ok("index: subentries indented", ip > 0 && d->p[ip]->pf.left == 440 && plain_is(d, (u32)ip, "banana 1"));
  ip = find_exact(d, "A");
  ok("index: cross-reference instead of a page", find_para(d, "Cherry") > 0 && plain_is(d, (u32)find_para(d, "Cherry"), "Cherry. See Fruit"));
  ok("index: an escaped colon stays in the entry", find_para(d, "zeta: with") > 0);
  ok("index: right-aligned page with leader", ip > 0 && d->p[ip + 1]->pf.tabkind[0] == TAB_RIGHT && d->p[ip + 1]->pf.tablead[0] == LEAD_DOT);
  {
    long b = find_para(d, "banana");
    u32 r, bold = 0;
    for (r = 0; b >= 0 && r < d->p[b]->nr; r++) if (d->p[b]->r[r].f.flags & CF_BOLD) bold = 1;
    ok("index: a bold page number", bold);
  }
  {
    u32 last = d->np - 2;
    ok("index: two columns in a section of its own", d->p[last]->pf.sect && d->sects[d->p[last]->pf.sect - 1].cols == 2 &&
       d->sects[d->p[last]->pf.sect - 1].start == SB_CONT && ip > 0 && d->p[ip - 1]->pf.sect && d->p[d->np - 1]->pf.sect &&
       d->sects[d->p[d->np - 1]->pf.sect - 1].start == SB_CONT && !d->p[d->np - 1]->len);
  }
  /* a new entry: Update Fields makes it afresh */
  at = P(1, 3);
  refs_mark_entry(d, &at, "Banana", "", "", "", 0, 0);
  {
    FieldEnv fe;
    field_env(&fe);
    lay_free(L); L = laid(d); fe.L = L;
    field_update_all(d, &fe);
    ok("index: Update Fields adds the new entry", find_para(d, "Banana 1") > 0 && find_para(d, "B") > 0 && refs_block_find(d, BK_INDEX, 0, &st, &en) >= 0);
  }
  e = docx_again(d, NULL);
  ok("index: XE and INDEX through .docx", e && count_fields(e, "XE") == 7 && count_fields(e, "INDEX") == 1 && find_para(e, "Banana 1") > 0);
  ok("index: XE written without a result", e && e->fields[0].nosep);
  doc_free(e);
  /* run-in */
  if (refs_block_find(d, BK_INDEX, 0, &st, &en) >= 0) {
    field_set_code(d, st, " INDEX \\r \\h \"\" \\z \"1033\" ");
    refs_block_update(d, st, NULL, L, -1);
    ok("index: run-in", find_para(d, "Fruit: banana, 1; pear, 1") > 0);
  }
  /* the XE codes show with formatting marks */
  {
    Layout *M = lay_new(d, &fs);
    u32 b;
    int shown = 0;
    M->showhidden = 1;
    lay_update(M);
    for (b = 0; b < M->pl[0].nboxes; b++) if (M->pl[0].boxes[b].kind == BX_FIELD) shown = 1;
    ok("index: XE shows its code with the marks", shown);
    lay_free(M);
  }
  lay_free(L); doc_free(d);

  /* AutoMark from a concordance (tab-separated lines) */
  d = mk("Quill is a word processor.\nThe Amiga runs Quill.\nNothing here.");
  e = mk("Quill\tSoftware:Quill\nAmiga\n");
  ok("index: AutoMark", refs_automark(d, e) == 3 && count_fields(d, "XE") == 3 && strstr(d->fields[0].code, "Software:Quill"));
  doc_free(e);
  /* the Spanish order: n with tilde after n */
  L = laid(d);
  {
    Pos a2 = P(2, 0);
    refs_mark_entry(d, &a2, "\xC3\xB1" "and\xC3\xBA", "", "", "", 0, 0);
    a2 = P(2, 0);
    refs_mark_entry(d, &a2, "nube", "", "", "", 0, 0);
    a2 = P(2, 0);
    refs_mark_entry(d, &a2, "oso", "", "", "", 0, 0);
    a2 = doc_end_pos(d);
    refs_block_insert(d, &a2, " INDEX \\z \"3082\" ", NULL, L, -1);
    ok("index: Spanish sorting", find_para(d, "nube") < find_para(d, "\xC3\xB1" "and") && find_para(d, "\xC3\xB1" "and") < find_para(d, "oso"));
  }
  lay_free(L); doc_free(d);
}

static void authorities(void)
{
  Doc *d = mk("See Smith v. Jones, 1 U.S. 1 (1990).\nAgain Smith v. Jones.\nThe Act, 5 U.S.C. 101."), *e;
  Layout *L;
  Pos at, a, b;
  char **c;
  int n, i;
  at = P(0, 35);
  ok("toa: TA marked", refs_mark_citation(d, &at, "Smith v. Jones, 1 U.S. 1 (1990)", "Smith v. Jones", 1) >= 0);
  at = P(1, 20);
  refs_mark_citation(d, &at, "", "Smith v. Jones", 1);
  at = P(2, 22);
  refs_mark_citation(d, &at, "5 U.S.C. 101", "", 2);
  c = refs_citations(d, &n);
  ok("toa: long citations listed", n == 2 && !strcmp(c[0], "5 U.S.C. 101"));
  for (i = 0; i < n; i++) free(c[i]);
  free(c);
  ok("toa: next citation", refs_next_citation(d, P(0, 0), &a, &b) && a.para == 0 && !strncmp(d->p[0]->t + a.off, "v.", 2));
  L = laid(d);
  at = doc_end_pos(d);
  refs_block_insert(d, &at, " TOA \\h \\c \"1\" \\p ", NULL, L, -1);
  ok("toa: category heading and entry", find_para(d, "Cases") > 0 && plain_is(d, (u32)find_para(d, "Cases") + 1, "Smith v. Jones, 1 U.S. 1 (1990) 1"));
  at = doc_end_pos(d);
  refs_block_insert(d, &at, " TOA \\h \\c \"2\" \\p ", NULL, L, -1);
  ok("toa: statutes", find_para(d, "Statutes") > 0 && find_para(d, "5 U.S.C. 101 1") > 0);
  e = docx_again(d, NULL);
  ok("toa: TA and TOA through .docx", e && count_fields(e, "TA") == 3 && count_fields(e, "TOA") == 2);
  doc_free(e);
  lay_free(L); doc_free(d);
  /* Mark All citations */
  d = mk("Roe v. Wade here.\nRoe again and Roe v. Wade.");
  ok("toa: Mark All", refs_mark_all_citations(d, "Roe v. Wade", "Roe", 1) == 3 && count_fields(d, "TA") == 3);
  {
    int longs = 0, k;
    for (k = 0; k < d->nfields; k++) if (strstr(d->fields[k].code, "\\l \"Roe v. Wade\"")) longs++;
    ok("toa: the first one carries the long citation", longs == 1);
  }
  doc_free(d);
}

static void cross_references(void)
{
  Doc *d = mk("#Intro\nSee here.\n#Method\nText\n\nEnd"), *e;
  RefTarget t[16];
  Pos at;
  int n, kinds[8], nk, k;
  doc_heading_numbering(d, LP_HEADINGS);
  at = P(4, 0);
  refs_insert_caption(d, &at, 0, ": Setup", -1, NULL);
  n = refs_targets(d, RT_HEADING, t, 16);
  ok("xref: headings listed", n == 2 && !strcmp(t[1].text, "Method"));
  nk = refs_kinds(RT_HEADING, kinds);
  ok("xref: Word's kinds for headings", nk == 6 && kinds[0] == XK_TEXT && kinds[2] == XK_NUMBER);
  at = P(1, 4);
  k = refs_insert_xref(d, &at, RT_HEADING, &t[1], XK_TEXT, 1, 0, "", NULL);
  ok("xref: heading text", k >= 0 && plain_is(d, 1, "See Methodhere."));
  at = P(1, 4);
  refs_insert_xref(d, &at, RT_HEADING, &t[1], XK_NUMBER, 1, 0, "", NULL);
  ok("xref: heading number", plain_is(d, 1, "See 2Methodhere."));
  at = P(1, 4);
  refs_insert_xref(d, &at, RT_HEADING, &t[1], XK_ABOVEBELOW, 1, 0, "", NULL);
  ok("xref: above/below", plain_is(d, 1, "See below2Methodhere."));
  n = refs_targets(d, RT_LABEL + 0, t, 16);
  ok("xref: captions listed", n == 1 && !strcmp(t[0].text, "Figure 1: Setup"));
  at = P(5, 0);
  ok("xref: only label and number", refs_insert_xref(d, &at, RT_LABEL, &t[0], XK_LABELNUM, 1, 0, "", NULL) >= 0 && plain_is(d, 5, "Figure 1End"));
  at = P(5, 0);
  ok("xref: only caption text", refs_insert_xref(d, &at, RT_LABEL, &t[0], XK_CAPTEXT, 1, 0, "", NULL) >= 0 && plain_is(d, 5, "SetupFigure 1End"));
  at = P(5, 0);
  ok("xref: entire caption can't overlap the other two", refs_insert_xref(d, &at, RT_LABEL, &t[0], XK_CAPTION, 1, 0, "", NULL) < 0);
  at = P(5, 0);
  ok("xref: page of a caption", refs_insert_xref(d, &at, RT_LABEL, &t[0], XK_PAGE, 1, 1, "", NULL) >= 0);
  /* a caption before it: the references follow on Update Fields */
  at = P(1, 0);
  {
    FieldEnv fe;
    Layout *L;
    long fp;
    refs_insert_caption(d, &at, 0, ": Earlier", CP_BELOW, NULL);
    field_env(&fe);
    L = laid(d); fe.L = L;
    field_update_all(d, &fe);
    fp = find_para(d, "Figure 2: Setup");
    ok("xref: references renumbered", fp > 0 && strstr(d->p[d->np - 1]->t, "Figure 2") != NULL);
    lay_free(L);
  }
  /* a footnote's number */
  {
    int nn;
    Pos np = P(0, 0);
    char ref[8];
    int ni = doc_add_note(d);
    note_utf8(ref, ni);
    np = P(2, 2);
    doc_insert(d, &np, ref, 4, NULL);
    nn = refs_targets(d, RT_FOOTNOTE, t, 16);
    at = P(d->np - 1, 0);
    ok("xref: note number", nn == 1 && refs_insert_xref(d, &at, RT_FOOTNOTE, &t[0], XK_NOTEFMT, 1, 0, "", NULL) >= 0 && d->p[d->np - 1]->t[4] == '1' &&
       (para_fmt_at(d->p[d->np - 1], 4)->flags & CF_SUPER));
  }
  e = docx_again(d, NULL);
  ok("xref: REF and NOTEREF fields through .docx", e && count_fields(e, "REF") >= 4 && count_fields(e, "NOTEREF") == 1);
  doc_free(e); doc_free(d);

  /* numbered items: no context, full context, relative */
  d = mk("One\nTwo\nSub\nAfter");
  {
    int l = doc_list_preset(d, LP_LEGAL);
    ParaFmt pf;
    char num[32];
    doc_list_apply(d, 0, 3, l);
    memset(&pf, 0, sizeof pf); pf.list = (u16)l; pf.level = 1;
    doc_para_format(d, 2, 2, PM_LIST, &pf);
    refs_para_number(d, 2, RN_PLAIN, 0, num, sizeof num);
    ok("xref: paragraph number", !strcmp(num, "2.1"));
    refs_para_number(d, 2, RN_FULL, 0, num, sizeof num);
    ok("xref: full context", !strcmp(num, "2.1"));
  }
  doc_free(d);
  d = mk("One\nTwo\nSub\nAfter");
  {
    int l = doc_list_preset(d, LP_OUTLINE);
    ParaFmt pf;
    char num[32];
    doc_list_apply(d, 0, 3, l);
    memset(&pf, 0, sizeof pf); pf.list = (u16)l; pf.level = 1;
    doc_para_format(d, 2, 3, PM_LIST, &pf);
    refs_para_number(d, 3, RN_PLAIN, 0, num, sizeof num);
    ok("xref: no context (I. A. outline)", !strcmp(num, "B"));
    refs_para_number(d, 3, RN_FULL, 0, num, sizeof num);
    ok("xref: full context (I. A. outline)", !strcmp(num, "II.B"));
    refs_para_number(d, 3, RN_REL, 2, num, sizeof num);
    ok("xref: relative context from the same level", !strcmp(num, "B"));
  }
  doc_free(d);
}

/* WP2OUT=dir: a document with all of it written as dir/wp2.docx and dir/wp2.pdf, to look at in LibreOffice */
static void sample(const char *dir)
{
  Doc *d = mk("Report\n#Introduction\nApples grow on trees. See Smith v. Jones.\n\n##Background\nPears are fruit too.\n#Results\n\nThe table follows.\nEnd of the results.\n#Appendix\n");
  Layout *L;
  Pos at;
  RefTarget t[8];
  char path[512];
  FieldEnv fe;
  int n, i;
  size_t len;
  u8 *data;
  FILE *f;
  doc_heading_numbering(d, LP_HEADINGS);
  at = P(3, 0); refs_insert_caption(d, &at, 0, ": Apple trees", -1, NULL);
  at = P(7, 0); refs_insert_caption(d, &at, 0, ": Results chart", -1, NULL);
  at = P(2, 6); refs_mark_entry(d, &at, "Apple", "", "", "", 0, 0);
  at = P(5, 5); refs_mark_entry(d, &at, "Fruit", "pear", "", "", 1, 0);
  at = P(2, 0); refs_mark_entry(d, &at, "Tree", "", "See Apple", "", 0, 0);
  at = P(2, 41); refs_mark_citation(d, &at, "Smith v. Jones, 1 U.S. 1 (1990)", "Smith v. Jones", 1);
  n = refs_targets(d, RT_LABEL, t, 8);
  at = P(8, 18);
  if (n >= 2) { refs_insert_xref(d, &at, RT_LABEL, &t[1], XK_LABELNUM, 1, 0, "", NULL); doc_insert(d, &at, " on page ", 9, NULL); refs_insert_xref(d, &at, RT_LABEL, &t[1], XK_PAGE, 1, 0, "", NULL); }
  n = refs_targets(d, RT_HEADING, t, 8);
  at = P(9, 18);
  if (n >= 3) { doc_insert(d, &at, " (see section ", 14, NULL); refs_insert_xref(d, &at, RT_HEADING, &t[0], XK_NUMBER, 1, 0, "", NULL); doc_insert(d, &at, ", ", 2, NULL);
    refs_insert_xref(d, &at, RT_HEADING, &t[0], XK_TEXT, 1, 1, "", NULL); doc_insert(d, &at, ")", 1, NULL); }
  L = laid(d);
  at = P(1, 0); doc_toc(d, L, &at, &i);
  at = doc_end_pos(d); refs_block_insert(d, &at, " TOC \\h \\z \\c \"Figure\" ", NULL, L, -1);
  at = doc_end_pos(d); refs_block_insert(d, &at, " TOA \\h \\c \"1\" \\p ", NULL, L, -1);
  at = doc_end_pos(d); refs_block_insert(d, &at, " INDEX \\e \"\t\" \\h \"A\" \\c \"2\" \\z \"1033\" ", NULL, L, -1);
  lay_free(L); L = laid(d);
  field_env(&fe); fe.L = L;
  field_update_all(d, &fe);
  lay_free(L); L = laid(d);
  snprintf(path, sizeof path, "%s/wp2.docx", dir);
  if ((data = docx_save(d, &len)) && (f = fopen(path, "wb"))) { fwrite(data, 1, len, f); fclose(f); }
  free(data);
  snprintf(path, sizeof path, "%s/wp2.pdf", dir);
  pdf_save_file(L, path);
  dump(d);
  lay_free(L); doc_free(d);
}

void test_wp2(void)
{
  if (getenv("WP2OUT")) sample(getenv("WP2OUT"));
  captions();
  contents_and_figures();
  index_tests();
  authorities();
  cross_references();
}
