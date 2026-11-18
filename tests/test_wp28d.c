/* test_wp28d.c - the native tests of WP28d Word 97-2003 .doc reader (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp28d()

   The .doc files are LibreOffice 24.2's ("MS Word 97" filter): tests/data/doc-fmt.doc and doc-rich.doc
   from tests/data/doc-fmt.rtf and doc-rich.rtf, lo-word/tables/notes/headers.doc from Quill's own
   tests/data/*.docx (tools/lo_doc.sh makes them again and compares each with LibreOffice's .docx of it) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/field.h"
#include "../core/cfb.h"

extern const DocFormat fmt_doc;

static Doc *load(const char *path, unsigned *lost)
{
  char err[200];
  unsigned l = 0;
  Doc *d = doc_load_file(path, &l, err, sizeof err);
  if (!d) printf("  %s: %s\n", path, err);
  if (lost) *lost = l;
  return d;
}
/* the paragraph whose text starts with s, -1 if none */
static int para(Doc *d, const char *s)
{
  u32 i;
  for (i = 0; d && i < d->np; i++) if (!strncmp(d->p[i]->t, s, strlen(s))) return (int)i;
  return -1;
}
/* the formatting at the first occurrence of s */
static const CharFmt *at(Doc *d, const char *s)
{
  u32 i;
  for (i = 0; d && i < d->np; i++) {
    const char *q = strstr(d->p[i]->t, s);
    if (q) return para_fmt_at(d->p[i], (u32)(q - d->p[i]->t));
  }
  return NULL;
}
static int font_is(Doc *d, const CharFmt *f, const char *name) { return f && f->font < d->nfonts && !strcmp(d->fonts[f->font], name); }

/* doc-fmt.doc: text, paragraph and character formatting, a table */
static void formatting(void)
{
  unsigned lost;
  Doc *d = load("tests/data/doc-fmt.doc", &lost);
  const CharFmt *f;
  int k;
  ok("doc: opens", d && lost == 0 && runs_sane(d));
  if (!d) return;
  k = para(d, "Quill Doc Test");
  ok("doc: heading style", k == 0 && !strcmp(d->styles[d->p[0]->pf.style].id, "Heading1") && d->p[0]->pf.before == 240 && d->p[0]->pf.after == 120);
  ok("doc: heading's look", (f = at(d, "Quill Doc")) && (f->flags & CF_BOLD) && f->size == 32 && font_is(d, f, "Liberation Sans"));
  ok("doc: bold, italic", (f = at(d, "bold")) && f->flags == CF_BOLD && (f = at(d, "italic")) && f->flags == CF_ITALIC);
  ok("doc: underline, strike", (f = at(d, "underlined")) && (f->flags & CF_UNDERLINE) && f->ul == UL_SINGLE && (f = at(d, "struck")) && (f->flags & CF_STRIKE));
  ok("doc: colour", (f = at(d, "red")) && f->color == 0xFF0000 && (f = at(d, "Plain")) && f->color == COLOR_AUTO && f->size == 24 && font_is(d, f, "Liberation Serif"));
  ok("doc: centred, right", (k = para(d, "A centred")) >= 0 && d->p[k]->pf.align == AL_CENTER && (k = para(d, "Right")) >= 0 && d->p[k]->pf.align == AL_RIGHT);
  k = para(d, "Justified");
  ok("doc: justified and indented", k >= 0 && d->p[k]->pf.align == AL_JUSTIFY && d->p[k]->pf.left == 720 && d->p[k]->pf.right == 360 &&
     d->p[k]->pf.first == -360 && d->p[k]->pf.before == 120 && d->p[k]->pf.after == 240);
  ok("doc: super, sub, small caps, caps", (f = at(d, "super")) && (f->flags & CF_SUPER) && (f = at(d, "sub")) && (f->flags & CF_SUB) &&
     (f = at(d, "Small Caps")) && (f->fx & FX_SMALLCAPS) && (f = at(d, "caps.")) && (f->flags & CF_CAPS) && f->size == 20 && font_is(d, f, "Liberation Mono"));
  ok("doc: 16-bit text", para(d, "Accents: \xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F and Greek \xCE\xB1\xCE\xB2\xCE\xB3 and a euro \xE2\x82\xAC.") >= 0);
  ok("doc: a table", d->ntables == 1 && d->tables[0].ncols == 2 && (k = para(d, "B2")) >= 0 && d->p[k]->pf.cell.table == 1 &&
     d->p[k]->pf.cell.row == 1 && d->p[k]->pf.cell.col == 1 && d->tables[0].grid[0] > 2900 && d->tables[0].grid[0] < 3100);
  ok("doc: after the table", (k = para(d, "Line one")) >= 0 && !d->p[k]->pf.cell.table && !strcmp(d->p[k]->t, "Line one\vline two in one paragraph, and a tab:\tafter it."));
  ok("doc: size", (f = at(d, "big")) && f->size == 48 && (f = at(d, "Blue on")) && f->color == 0x0000FF && f->hl);
  ok("doc: Letter paper", d->page.width == 12240 && d->page.height == 15840);
  doc_free(d);
}

/* doc-rich.doc: styles, a character style, lists, a footnote, a link, line spacing, tabs, merged and
   bordered cells, a page break, a second (landscape) section, header and footer */
static void rich(void)
{
  unsigned lost;
  Doc *d = load("tests/data/doc-rich.doc", &lost);
  const CharFmt *f;
  int k, j;
  ok("doc rich: opens", d && runs_sane(d));
  if (!d) return;
  ok("doc rich: heading 2 based on heading 1", (k = para(d, "Second Heading")) >= 0 && !strcmp(d->styles[d->p[k]->pf.style].id, "Heading2") &&
     d->styles[d->styles[d->p[k]->pf.style].based].id[0] && !strcmp(d->styles[d->styles[d->p[k]->pf.style].based].id, "Heading1") &&
     (f = at(d, "Second Heading")) && (f->flags & (CF_BOLD | CF_ITALIC)) == (CF_BOLD | CF_ITALIC) && f->size == 28);
  f = at(d, "strong green");
  ok("doc rich: a character style", f && f->cstyle && !strcmp(d->styles[f->cstyle].name, "Strong Green") && d->styles[f->cstyle].type == ST_CHAR &&
     (f->flags & CF_BOLD) && f->color == 0x008000);
  k = para(d, "First bullet"); j = para(d, "Numbered two");
  ok("doc rich: a bulleted list", k >= 0 && d->p[k]->pf.list && d->p[k]->pf.list <= d->nlists && d->lists[d->p[k]->pf.list - 1].lvl[0].fmt == NF_BULLET &&
     !strcmp(d->lists[d->p[k]->pf.list - 1].lvl[0].text, "\xE2\x80\xA2") && d->p[k]->pf.left == 720 && d->p[k]->pf.first == -360 &&
     d->p[k + 1]->pf.list == d->p[k]->pf.list);
  ok("doc rich: a numbered list", j >= 0 && d->p[j]->pf.list && d->p[j]->pf.list != d->p[k]->pf.list && d->lists[d->p[j]->pf.list - 1].lvl[0].fmt == NF_DECIMAL &&
     !strcmp(d->lists[d->p[j]->pf.list - 1].lvl[0].text, "%1.") && d->lists[d->p[j]->pf.list - 1].lvl[0].start == 1);
  k = para(d, "Some ");
  ok("doc rich: a footnote", d->nnotes == 1 && k >= 0 && strstr(d->p[k]->t, "footnote\xF3\xB4\x80\x80 here.") && d->notes[0].np == 1 &&
     strstr(d->notes[0].p[0]->t, "The footnote text."));
  ok("doc rich: its number not raised twice", (f = at(d, "\xF3\xB4\x80\x80")) && !(f->flags & CF_SUPER));
  f = at(d, "the Quill page");
  ok("doc rich: a link", f && f->link && !strcmp(doc_link_url(d, f->link), "http://example.com/quill") && (f->flags & CF_UNDERLINE) && f->color == 0xFF0000 &&
     para(d, "Visit the Quill page now.") >= 0);
  ok("doc rich: one and a half lines", (k = para(d, "One and a half")) >= 0 && d->p[k]->pf.linerule == LS_AUTO && d->p[k]->pf.line == 360);
  k = para(d, "Tabbed");
  ok("doc rich: tab stops", k >= 0 && d->p[k]->pf.tabs[0] == 2880 && d->p[k]->pf.tabkind[0] == TAB_LEFT && d->p[k]->pf.tabs[1] == 7200 &&
     d->p[k]->pf.tabkind[1] == TAB_RIGHT && d->p[k]->pf.tablead[1] == LEAD_DOT);
  k = para(d, "Shaded");
  ok("doc rich: cell borders and shading", k >= 0 && d->p[k]->pf.cell.table && d->p[k]->pf.cell.borders == BD_ALL && d->p[k]->pf.cell.shade == 0xC8C8FF &&
     d->p[k + 1]->pf.cell.shade == COLOR_AUTO);
  k = para(d, "Merged across");
  ok("doc rich: cells merged", k >= 0 && d->p[k]->pf.cell.col == 0 && d->p[k]->pf.cell.span == 2 && d->p[k]->pf.cell.row == 1 && !d->p[k + 1]->pf.cell.table);
  ok("doc rich: a page break", (k = para(d, "Page two")) >= 0 && d->p[k]->pf.pagebreak);
  ok("doc rich: two sections", d->nsects >= 1 && d->p[k]->pf.sect && d->sects[d->p[k]->pf.sect - 1].haspage &&
     d->sects[d->p[k]->pf.sect - 1].page.width == 11906 && d->sects[d->p[k]->pf.sect - 1].page.left == 1134 &&
     d->page.width == 16838 && d->page.height == 11906 && d->page.left == 1440);
  ok("doc rich: header and footer", d->hdr[HF_DEFAULT].np == 1 && !strcmp(d->hdr[HF_DEFAULT].p[0]->t, "Running Header") &&
     d->hdr[HF_DEFAULT].p[0]->pf.align == AL_CENTER && d->ftr[HF_DEFAULT].np == 1 && !strcmp(d->ftr[HF_DEFAULT].p[0]->t, "Footer Text") &&
     !d->hdr[HF_FIRST].np && !d->evenodd);
  ok("doc rich: only the styles used", d->nstyles < 40);
  doc_free(d);
}

/* LibreOffice's .doc of Quill's own test documents: the same text and look as the .docx */
static int same_text(Doc *a, Doc *b)
{
  u32 i;
  if (!a || !b) return 0;
  if (a->np != b->np) { printf("  %lu vs %lu paragraphs\n", (unsigned long)a->np, (unsigned long)b->np); return 0; }
  for (i = 0; i < a->np; i++) if (strcmp(a->p[i]->t, b->p[i]->t)) { printf("  paragraph %lu: [%s] vs [%s]\n", (unsigned long)i, a->p[i]->t, b->p[i]->t); return 0; }
  return 1;
}
static void others(void)
{
  unsigned lost;
  Doc *x = load("tests/data/tables.docx", NULL), *d = load("tests/data/lo-tables.doc", &lost), *n;
  u32 i;
  int same = 1, k;
  ok("doc LibreOffice: tables' text", same_text(x, d) && lost == 0);
  for (i = 0; x && d && i < x->np && i < d->np; i++) {
    const ParaFmt *a = &x->p[i]->pf, *b = &d->p[i]->pf;
    if (a->cell.table != b->cell.table || a->cell.row != b->cell.row || a->cell.col != b->cell.col || a->cell.span != b->cell.span || a->align != b->align) {
      printf("  paragraph %lu: cell %u/%u/%u/%u vs %u/%u/%u/%u\n", (unsigned long)i, a->cell.table, a->cell.row, a->cell.col, a->cell.span, b->cell.table, b->cell.row, b->cell.col, b->cell.span);
      same = 0;
    }
  }
  ok("doc LibreOffice: tables' cells", same && d && x && d->ntables == x->ntables && d->tables[0].ncols == x->tables[0].ncols && d->tables[0].marl == x->tables[0].marl);
  doc_free(x); doc_free(d);
  x = load("tests/data/word.docx", NULL); d = load("tests/data/lo-word.doc", &lost);
  same = same_text(x, d);
  for (i = 0; same && i < x->np; i++) {
    u32 r;
    if (x->p[i]->nr != d->p[i]->nr || strcmp(x->styles[x->p[i]->pf.style].id, d->styles[d->p[i]->pf.style].id) || x->p[i]->pf.before != d->p[i]->pf.before || x->p[i]->pf.line != d->p[i]->pf.line) same = 0;
    for (r = 0; same && r < x->p[i]->nr; r++) {
      const CharFmt *a = &x->p[i]->r[r].f, *b = &d->p[i]->r[r].f;
      if (x->p[i]->r[r].end != d->p[i]->r[r].end || a->flags != b->flags || a->size != b->size || a->color != b->color || strcmp(d->fonts[b->font], x->fonts[a->font])) same = 0;
    }
    if (!same) printf("  word.docx paragraph %lu differs\n", (unsigned long)i);
  }
  ok("doc LibreOffice: word's runs and paragraphs as the .docx's", same);
  doc_free(x); doc_free(d);
  x = load("tests/data/notes.docx", NULL); n = load("tests/data/lo-notes.doc", &lost);
  same = same_text(x, n) && x->nnotes == n->nnotes;
  for (k = 0; same && k < x->nnotes; k++) if (x->notes[k].np != n->notes[k].np || strcmp(x->notes[k].p[0]->t, n->notes[k].p[0]->t)) same = 0;
  ok("doc LibreOffice: footnotes", same && x->nnotes == 3);
  doc_free(x); doc_free(n);
  x = load("tests/data/headers.docx", NULL); d = load("tests/data/lo-headers.doc", &lost);
  ok("doc LibreOffice: headers' text", same_text(x, d));
  ok("doc LibreOffice: headers", d && d->hdr[HF_DEFAULT].np && !strcmp(d->hdr[HF_DEFAULT].p[0]->t, x->hdr[HF_DEFAULT].p[0]->t) && d->titlepg == x->titlepg &&
     d->hdr[HF_FIRST].np && !strcmp(d->hdr[HF_FIRST].p[0]->t, "First page header"));
  {
    /* "Page { PAGE } of { NUMPAGES }": the page fields are placeholders, as in .docx */
    const char *t = d && d->ftr[HF_DEFAULT].np ? d->ftr[HF_DEFAULT].p[0]->t : "";
    ok("doc LibreOffice: page number fields", strstr(t, FIELD_PAGE) && strstr(t, FIELD_PAGES) && !strstr(t, "3"));
  }
  doc_free(x); doc_free(d);
}

/* refused with a message: Word 6/95, encrypted, not a Word file; damaged files read safely */
static void hostile(void)
{
  size_t n;
  u8 *f = file_read("tests/data/doc-rich.doc", &n), *g;
  char err[200];
  unsigned lost;
  Doc *d;
  int i, opened = 0;
  Cfb c;
  if (!f) { ok("doc hostile: file", 0); return; }
  /* the FIB is the WordDocument stream's first bytes: find them in the file and change them */
  g = (u8 *)malloc(n);
  if (!g) { free(f); return; }
  {
    size_t o, fib = 0;
    for (o = 0; o + 512 <= n; o += 512) if (f[o] == 0xEC && f[o + 1] == 0xA5) { fib = o; break; }
    memcpy(g, f, n);
    g[fib + 11] |= 0x01;                               /* fEncrypted */
    err[0] = 0;
    d = fmt_doc.load(g, n, &lost, err, sizeof err);
    ok("doc hostile: encrypted refused", !d && strstr(err, "encrypted"));
    memcpy(g, f, n);
    g[fib + 2] = 0x65; g[fib + 3] = 0;               /* nFib of Word 6 */
    d = fmt_doc.load(g, n, &lost, err, sizeof err);
    ok("doc hostile: Word 6 refused", !d && strstr(err, "Word 6"));
  }
  /* a compound file without a WordDocument stream */
  {
    CfbWriter *w = cfb_writer();
    size_t cn;
    u8 *cf;
    cfb_add(w, "Workbook", "x", 1);
    cf = cfb_finish(w, &cn);
    d = cf ? fmt_doc.load(cf, cn, &lost, err, sizeof err) : NULL;
    ok("doc hostile: not a Word document", cf && !d && strstr(err, "not a Word"));
    free(cf);
  }
  /* bytes changed all over the streams, and the file cut short: never read out of bounds */
  for (i = 0; i < 300; i++) {
    size_t k;
    u32 s = (u32)i * 2654435761u;
    memcpy(g, f, n);
    for (k = 0; k < 24; k++) { s = s * 1103515245u + 12345u; g[1536 + (s >> 8) % (n - 1536)] ^= (u8)(s >> 3 | 1); }
    d = fmt_doc.load(g, i % 10 == 9 ? n - (size_t)(s % 4096) : n, &lost, err, sizeof err);
    if (d) { opened++; if (!runs_sane(d)) { ok("doc hostile: damaged files' runs", 0); doc_free(d); break; } }
    doc_free(d);
  }
  ok("doc hostile: damaged files read safely", opened > 0);
  ok("doc hostile: the reader's open", cfb_open(&c, f, n) && (cfb_close(&c), 1));
  free(g); free(f);
}

void test_wp28d(void)
{
  formatting();
  rich();
  others();
  hostile();
}
