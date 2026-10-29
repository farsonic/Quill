/* test_wp15a.c - the native tests of WP15a Charts and SmartArt: read, draw, keep (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp15a().
   tests/data/charts.docx is a Word-style document made for these tests: nine charts (column, a floating
   pie, line, bar, area, doughnut, scatter, radar, surface) with embedded workbooks and colour and style
   parts, an Excel 2016 funnel (chartex) in mc:AlternateContent, and a SmartArt process with its saved drawing */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/chart.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/zip.h"

/* a part of a .docx in memory (malloc'd, *len its size), NULL if it isn't there */
static u8 *zpart(const u8 *data, size_t n, const char *name, size_t *len)
{
  ZipReader z;
  u8 *b;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), len);
  zip_close(&z);
  return b;
}
/* part name is the same, byte for byte, in both packages */
static int same_part(const u8 *a, size_t an, const u8 *b, size_t bn, const char *name)
{
  size_t la = 0, lb = 0;
  u8 *pa = zpart(a, an, name, &la), *pb = zpart(b, bn, name, &lb);
  int r = pa && pb && la == lb && !memcmp(pa, pb, la);
  if (!r) printf("  (part %s: %s)\n", name, !pa ? "missing in the first" : !pb ? "missing in the second" : "differs");
  free(pa); free(pb);
  return r;
}
static int count_kind(Doc *d, int kind)
{
  u32 i, k;
  int n = 0;
  for (i = 0; i < d->np; i++)
    for (k = 0; k + 4 <= d->p[i]->len; k++)
      if (IS_PIC(d->p[i]->t + k)) { int m = pic_index(d->p[i]->t + k); if (m < d->nimages && d->images[m].kind == kind && d->images[m].pack) n++; k += 3; }
  return n;
}
/* the n-th object of a kind in the text (its Doc.images index), -1 if none */
static int nth_kind(Doc *d, int kind, int nth, Pos *at)
{
  u32 i, k;
  for (i = 0; i < d->np; i++)
    for (k = 0; k + 4 <= d->p[i]->len; k++)
      if (IS_PIC(d->p[i]->t + k)) {
        int m = pic_index(d->p[i]->t + k);
        if (m < d->nimages && d->images[m].kind == kind && !nth--) { if (at) { at->para = i; at->off = k; } return m; }
        k += 3;
      }
  return -1;
}
static int prims_have_text(const PrimList *l, const char *t)
{
  int i;
  for (i = 0; l && i < l->n; i++) if (l->p[i].t == PR_TEXT && l->p[i].text && !strcmp(l->p[i].text, t)) return 1;
  return 0;
}
static int prims_count(const PrimList *l, int t)
{
  int i, n = 0;
  for (i = 0; l && i < l->n; i++) if (l->p[i].t == t) n++;
  return n;
}

static void numbers(void)
{
  char t[64];
  double mn, mx, mj;
  chart_fmt_value(6, "General", t, sizeof t); ok("chart numbers: 6 in General", !strcmp(t, "6"));
  chart_fmt_value(2.5, "General", t, sizeof t); ok("chart numbers: 2.5", !strcmp(t, "2.5"));
  chart_fmt_value(1234567.891, "#,##0.00", t, sizeof t); ok("chart numbers: thousands and decimals", !strcmp(t, "1,234,567.89"));
  chart_fmt_value(0.25, "0%", t, sizeof t); ok("chart numbers: per cent", !strcmp(t, "25%"));
  chart_fmt_value(-3.75, "$#,##0.00", t, sizeof t); ok("chart numbers: currency, negative", !strcmp(t, "-$3.75"));
  chart_fmt_value(6e9, "General", t, sizeof t); ok("chart numbers: past 2^31 exactly", !strcmp(t, "6000000000"));
  chart_fmt_value(1e12, "General", t, sizeof t); ok("chart numbers: big ones in E notation", !strcmp(t, "1E+12"));
  chart_nice_scale(0, 4.5, 5, &mn, &mx, &mj);
  ok("chart axis: 0 to 4.5 in five steps", mn == 0 && mx == 5 && mj == 1);
  chart_nice_scale(-12, 87, 6, &mn, &mx, &mj);
  ok("chart axis: -12 to 87", mn == -20 && mx == 100 && mj == 20);
}

/* a chart made in memory (as WP15b will): negative values and an empty point */
static void model(void)
{
  Chart *c = chart_new(CK_LINE), *k;
  CSeries *s = c ? chart_add_series(c) : NULL;
  PrimList l;
  int i, lines = 0, tall = 0;
  static const double v[5] = { -120, -30, CV_NONE, 45, -7.5 };
  ok("chart model: made", c && s);
  if (!c || !s) { chart_free(c); return; }
  s->v = (double *)malloc(5 * sizeof(double));
  if (s->v) { memcpy(s->v, v, sizeof v); s->nv = 5; }
  s->name = str_dup("Profit");
  ok("chart model: the empty point told from the others", CV_IS_NONE(s->v[2]) && !CV_IS_NONE(s->v[0]) && !CV_IS_NONE(-1e300 * 0.5));
  k = chart_copy(c);
  ok("chart model: copied whole", k && k->ns == 1 && k->s[0].nv == 5 && k->s[0].v[0] == -120 && !strcmp(k->s[0].name, "Profit"));
  prims_init(&l);
  ok("chart model: laid out", chart_render(c, 400, 250, NULL, NULL, &l));
  for (i = 0; i < l.n; i++)
    if (l.p[i].t == PR_LINE && l.p[i].elem == CE_SERIES) {
      int j;
      lines++;
      for (j = 0; j < l.p[i].np; j++) if (l.p[i].pts[2 * j + 1] < 0 || l.p[i].pts[2 * j + 1] > 250) tall = 1;
    }
  ok("chart model: the line broken at the empty point, inside the chart", lines == 2 && !tall);
  ok("chart model: the axis down to the negative values", prims_have_text(&l, "-120") || prims_have_text(&l, "-140") || prims_have_text(&l, "-150"));
  prims_free(&l);
  chart_free(k);
  chart_free(c);
}

static void reading(Doc *d)
{
  int k, i, ok1 = 1;
  Pos at;
  const Chart *c;
  ok("charts: nine charts and a chartex read", count_kind(d, OBJ_CHART) == 10);
  ok("charts: one SmartArt read", count_kind(d, OBJ_SMARTART) == 1);
  k = nth_kind(d, OBJ_CHART, 0, &at);
  c = k >= 0 ? d->images[k].pack->chart : NULL;
  ok("charts: the first is a clustered column", c && c->kind == CK_COL && c->ns == 3 && c->title && !strcmp(c->title, "Sales by Quarter"));
  ok("charts: its cached values and labels", c && c->s[0].nv == 4 && c->s[0].v[0] == 4.3 && c->s[2].v[3] == 5 && c->s[0].ncat == 4 && !strcmp(c->s[0].cat[3], "Q4") &&
     c->s[1].name && !strcmp(c->s[1].name, "South"));
  ok("charts: in line, its size", k >= 0 && d->images[k].wrap == WRAP_INLINE && d->images[k].cx == 5760 && d->images[k].cy == 3168);
  ok("charts: its parts kept (chart, workbook, colours, style, .rels)", k >= 0 && d->images[k].pack->np == 5 &&
     pack_find(d->images[k].pack, "word/embeddings/Microsoft_Excel_Worksheet.xlsx") >= 0 && pack_find(d->images[k].pack, "word/charts/_rels/chart1.xml.rels") >= 0);
  k = nth_kind(d, OBJ_CHART, 1, NULL);
  c = k >= 0 ? d->images[k].pack->chart : NULL;
  ok("charts: the floating pie", c && c->kind == CK_PIE && d->images[k].wrap == WRAP_SQUARE && d->images[k].halign == PA_END && c->legend == LG_RIGHT);
  {
    static const u8 kinds[8] = { CK_COL, CK_PIE, CK_LINE, CK_BAR, CK_AREA, CK_DOUGHNUT, CK_SCATTERLINE, CK_RADAR };
    for (i = 0; i < 8; i++) { k = nth_kind(d, OBJ_CHART, i, NULL); if (k < 0 || !d->images[k].pack->chart || d->images[k].pack->chart->kind != kinds[i]) { ok1 = 0; printf("  (chart %d: kind %d)\n", i, k >= 0 && d->images[k].pack->chart ? d->images[k].pack->chart->kind : -1); } }
  }
  ok("charts: the kinds", ok1);
  k = nth_kind(d, OBJ_CHART, 8, NULL);
  ok("charts: a surface chart is one Quill doesn't draw", k >= 0 && d->images[k].pack->chart && d->images[k].pack->chart->kind == CK_OTHER && !strcmp(d->images[k].pack->chart->kindname, "Surface"));
  k = nth_kind(d, OBJ_CHART, 9, NULL);
  c = k >= 0 ? d->images[k].pack->chart : NULL;
  ok("charts: the chartex funnel", k >= 0 && d->images[k].pack->kind == PK_CHARTEX && c && c->kind == CK_FUNNEL && c->ns == 1 && c->s[0].nv == 4 && c->s[0].v[1] == 80 &&
     c->s[0].ncat == 4 && !strcmp(c->s[0].cat[2], "Offers") && c->title && !strcmp(c->title, "Funnel"));
  k = nth_kind(d, OBJ_SMARTART, 0, NULL);
  ok("SmartArt: data, layout, quick style, colours and drawing kept", k >= 0 && d->images[k].pack->np == 5);
  ok("SmartArt: its drawing's shapes and text", k >= 0 && d->images[k].pack->art && d->images[k].pack->art->n == 5 && d->images[k].pack->art->s[0].text &&
     !strcmp(d->images[k].pack->art->s[0].text, "Plan") && d->images[k].pack->art->s[0].shape == SH_ROUNDRECT && d->images[k].pack->art->s[1].shape == SH_RARROW &&
     d->images[k].pack->art->s[0].sz == 2400);
}

static void drawing(Doc *d)
{
  int k;
  const PrimList *l;
  k = nth_kind(d, OBJ_CHART, 0, NULL);
  l = k >= 0 ? pack_prims(d->images[k].pack, d->images[k].cx, d->images[k].cy, &fs, "Calibri") : NULL;
  ok("charts drawn: the column chart's title, legend and bars", l && prims_have_text(l, "Sales by Quarter") && prims_have_text(l, "West") && prims_have_text(l, "Q3") &&
     prims_count(l, PR_RECT) >= 12 + 3);
  ok("charts drawn: laid out once for a size", k >= 0 && pack_prims(d->images[k].pack, d->images[k].cx, d->images[k].cy, &fs, "Calibri") == l);
  k = nth_kind(d, OBJ_CHART, 1, NULL);
  l = k >= 0 ? pack_prims(d->images[k].pack, d->images[k].cx, d->images[k].cy, &fs, "Calibri") : NULL;
  ok("charts drawn: the pie's slices", l && prims_count(l, PR_POLY) == 4 && prims_have_text(l, "Figs"));
  k = nth_kind(d, OBJ_CHART, 8, NULL);
  l = k >= 0 ? pack_prims(d->images[k].pack, d->images[k].cx, d->images[k].cy, &fs, "Calibri") : NULL;
  ok("charts drawn: the surface as a frame with its title", l && prims_have_text(l, "Surface (placeholder)") && prims_have_text(l, "Surface"));
  k = nth_kind(d, OBJ_SMARTART, 0, NULL);
  l = k >= 0 ? pack_prims(d->images[k].pack, d->images[k].cx, d->images[k].cy, &fs, "Calibri") : NULL;
  ok("SmartArt drawn: its shapes and text", l && prims_count(l, PR_POLY) == 5 && prims_have_text(l, "Build") && prims_have_text(l, "Ship"));
  {
    Layout *L = lay_new(d, &fs);
    size_t n = 0;
    u8 *pdf = L ? pdf_export(L, &n) : NULL;
    ok("charts drawn: in the PDF", pdf && n > 1000);
    free(pdf);
    lay_free(L);
  }
}

static void keeping(Doc *d, const u8 *orig, size_t on)
{
  static const char *const parts[] = {
    "word/charts/chart1.xml", "word/charts/_rels/chart1.xml.rels", "word/embeddings/Microsoft_Excel_Worksheet.xlsx", "word/charts/colors1.xml", "word/charts/style1.xml",
    "word/charts/chart2.xml", "word/charts/chart9.xml", "word/embeddings/Microsoft_Excel_Worksheet8.xlsx", "word/charts/chartEx1.xml", "word/charts/_rels/chartEx1.xml.rels",
    "word/embeddings/Microsoft_Excel_Worksheet_cx.xlsx", "word/diagrams/data1.xml", "word/diagrams/layout1.xml", "word/diagrams/quickStyle1.xml", "word/diagrams/colors1.xml",
    "word/diagrams/drawing1.xml" };
  size_t n = 0, n2 = 0, i;
  unsigned lost = 99;
  u8 *a = docx_save(d, &n), *b = NULL;
  char err[200], *rels, *doc, *ct;
  int all = 1;
  Doc *e = a ? docx_load(a, n, &lost, err, sizeof err) : NULL;
  ok("charts kept: saved and read back, nothing lost", e && lost == 0 && count_kind(e, OBJ_CHART) == 10 && count_kind(e, OBJ_SMARTART) == 1);
  for (i = 0; a && i < sizeof parts / sizeof parts[0]; i++) if (!same_part(orig, on, a, n, parts[i])) all = 0;
  ok("charts kept: every part byte for byte", a && all);
  rels = a ? (char *)zpart(a, n, "word/_rels/document.xml.rels", NULL) : NULL;
  ok("charts kept: their relationships, with the ids they had", rels && strstr(rels, "Id=\"rId11\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" Target=\"charts/chart1.xml\"") &&
     strstr(rels, "Id=\"rId30\"") && strstr(rels, "Target=\"charts/chartEx1.xml\"") && strstr(rels, "Id=\"rId44\" Type=\"http://schemas.microsoft.com/office/2007/relationships/diagramDrawing\""));
  doc = a ? (char *)zpart(a, n, "word/document.xml", NULL) : NULL;
  ok("charts kept: the drawings refer to them", doc && strstr(doc, "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"") && strstr(doc, "r:id=\"rId12\"") &&
     strstr(doc, "r:dm=\"rId40\" r:lo=\"rId41\" r:qs=\"rId42\" r:cs=\"rId43\"") && strstr(doc, "<mc:Choice xmlns:cx1=") && strstr(doc, "<cx:chart "));
  ok("charts kept: the floating one floats", doc && strstr(doc, "<wp:anchor") && strstr(doc, "<wp:align>right</wp:align>") && strstr(doc, "<wp:wrapSquare"));
  ct = a ? (char *)zpart(a, n, "[Content_Types].xml", NULL) : NULL;
  ok("charts kept: their content types", ct && strstr(ct, "/word/charts/chart1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"") &&
     strstr(ct, "/word/embeddings/Microsoft_Excel_Worksheet.xlsx\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\"") &&
     strstr(ct, "diagramDrawing+xml"));
  b = e ? docx_save(e, &n2) : NULL;
  all = 1;
  for (i = 0; b && i < sizeof parts / sizeof parts[0]; i++) if (!same_part(orig, on, b, n2, parts[i])) all = 0;
  ok("charts kept: through a second save too", b && all && same_part(a, n, b, n2, "word/_rels/document.xml.rels"));
  free(rels); free(doc); free(ct); free(a); free(b);
  doc_free(e);
}

/* a chart pasted into a document that has its own chart1.xml: the pasted one gets new names, and its
   .rels its parts' new names */
static void pasting(Doc *d, const u8 *orig, size_t on)
{
  char err[200];
  unsigned lost;
  Doc *e = docx_load(orig, on, &lost, err, sizeof err), *f;
  Pos a, b, at;
  Fragment *fr;
  size_t n = 0;
  u8 *out = NULL;
  int k;
  k = nth_kind(d, OBJ_CHART, 0, &a);
  b = a; b.off += 4;
  fr = k >= 0 ? doc_fragment(d, a, b) : NULL;
  at = doc_end_pos(e);
  ok("charts pasted: into another document", e && fr && doc_paste(e, &at, fr, PASTE_KEEP) && count_kind(e, OBJ_CHART) == 11);
  ok("charts pasted: the parts shared, counted", k >= 0 && d->images[k].pack->refs >= 2);
  out = e ? docx_save(e, &n) : NULL;
  f = out ? docx_load(out, n, &lost, err, sizeof err) : NULL;
  ok("charts pasted: saved under new names, read back", f && lost == 0 && count_kind(f, OBJ_CHART) == 11);
  {
    size_t l = 0;
    char *r = out ? (char *)zpart(out, n, "word/charts/_rels/chart10.xml.rels", &l) : NULL;
    u8 *x = out ? zpart(out, n, "word/charts/chart10.xml", NULL) : NULL;
    ok("charts pasted: the copy's .rels follows its renamed parts", r && x && strstr(r, "Target=\"/word/embeddings/Microsoft_Excel_Worksheet9.xlsx\"") && strstr(r, "Target=\"/word/charts/colors10.xml\""));
    free(r); free(x);
  }
  k = nth_kind(f ? f : e, OBJ_CHART, 10, NULL);
  ok("charts pasted: the copy shows the same chart", f && k >= 0 && f->images[k].pack->chart && f->images[k].pack->chart->ns == 3 && f->images[k].pack->chart->kind == CK_COL);
  fragment_free(fr);
  free(out);
  doc_free(e); doc_free(f);
  /* deleted: not saved */
  {
    Doc *g = docx_load(orig, on, &lost, err, sizeof err);
    Pos p0, p1;
    u8 *o2;
    size_t n2 = 0;
    char *rels;
    k = g ? nth_kind(g, OBJ_SMARTART, 0, &p0) : -1;
    p1 = p0; p1.off += 4;
    if (k >= 0) doc_delete(g, p0, p1);
    o2 = g ? docx_save(g, &n2) : NULL;
    rels = o2 ? (char *)zpart(o2, n2, "word/_rels/document.xml.rels", NULL) : NULL;
    ok("SmartArt deleted: its parts aren't saved", k >= 0 && rels && !strstr(rels, "diagram") && !zpart(o2, n2, "word/diagrams/data1.xml", NULL));
    free(rels); free(o2);
    doc_free(g);
  }
}

void test_wp15a(void)
{
  char err[200];
  unsigned lost = 99;
  size_t on = 0;
  u8 *orig = file_read("tests/data/charts.docx", &on);
  Doc *d = orig ? docx_load(orig, on, &lost, err, sizeof err) : NULL;
  numbers();
  model();
  ok("charts: the test document reads, nothing reported lost", d && lost == 0);
  if (d) {
    reading(d);
    drawing(d);
    keeping(d, orig, on);
    pasting(d, orig, on);
  }
  doc_free(d);
  free(orig);
}
