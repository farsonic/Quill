/* test_wp15b.c - the native tests of WP15b Insert Chart and Chart Design (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp15b().
   With WP15B_OUT set to a folder, the documents made here are written there (charts.docx, one chart of
   every kind; edited.docx, a chart of tests/data/charts.docx changed) for checking in LibreOffice */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/chartedit.h"
#include "../core/zip.h"

static u8 *zpart(const u8 *data, size_t n, const char *name, size_t *len)
{
  ZipReader z;
  u8 *b;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), len);
  zip_close(&z);
  return b;
}
/* the n-th chart in the text, its Doc.images index and place */
static int nth_chart(Doc *d, int nth, Pos *at)
{
  u32 i, k;
  for (i = 0; i < d->np; i++)
    for (k = 0; k + 4 <= d->p[i]->len; k++)
      if (IS_PIC(d->p[i]->t + k)) {
        int m = pic_index(d->p[i]->t + k);
        if (m < d->nimages && d->images[m].kind == OBJ_CHART && !nth--) { if (at) { at->para = i; at->off = k; } return m; }
        k += 3;
      }
  return -1;
}
static int same_values(const Chart *a, const Chart *b)
{
  int k, i;
  if (!a || !b || a->ns != b->ns) return 0;
  for (k = 0; k < a->ns; k++) {
    if (a->s[k].nv != b->s[k].nv) return 0;
    for (i = 0; i < a->s[k].nv; i++) {
      double x = a->s[k].v[i], y = b->s[k].v[i], d = x - y;
      if (CV_IS_NONE(x) != CV_IS_NONE(y) || (!CV_IS_NONE(x) && (d > 1e-9 || d < -1e-9))) return 0;
    }
    if ((a->s[k].name == NULL) != (b->s[k].name == NULL) || (a->s[k].name && strcmp(a->s[k].name, b->s[k].name))) return 0;
  }
  return 1;
}

static void grid(void)
{
  Chart *c = chart_sample(CK_COL);
  CGrid *g = c ? chart_to_grid(c) : NULL;
  ok("chart grid: Word's sample, 4 categories x 3 series", c && c->ns == 3 && c->s[0].nv == 4 && g && g->rows == 5 && g->cols == 4 &&
     !strcmp(cgrid_get(g, 0, 2), "Series 2") && !strcmp(cgrid_get(g, 3, 0), "Category 3") && !strcmp(cgrid_get(g, 4, 3), "5") && !strcmp(cgrid_get(g, 2, 2), "4.4"));
  if (g) {
    /* a fourth series and a fifth category typed in */
    cgrid_resize(g, 6, 5);
    cgrid_set(g, 0, 4, "Series 4"); cgrid_set(g, 5, 0, "Category 5");
    cgrid_set(g, 5, 1, "1,234.5"); cgrid_set(g, 1, 4, "50%"); cgrid_set(g, 2, 4, "-2");
    ok("chart grid: typed back into the chart", chart_from_grid(c, g) && c->ns == 4 && c->s[0].nv == 5 && c->s[0].v[4] == 1234.5 && c->s[3].v[0] == 0.5 &&
       c->s[3].v[1] == -2 && CV_IS_NONE(c->s[3].v[4]) && !strcmp(c->s[3].name, "Series 4") && !strcmp(c->s[2].cat[4], "Category 5"));
    cgrid_free(g);
  }
  ok("chart grid: numbers typed", chart_atof("2.5") == 2.5 && chart_atof(" -3 ") == -3 && chart_atof("1e3") == 1000 && CV_IS_NONE(chart_atof("abc")) &&
     CV_IS_NONE(chart_atof("")) && chart_atof("2,5") == 2.5 && chart_atof("$1,000.25") == 1000.25);
  if (c) {
    ok("chart: switch row and column", chart_switch_rowcol(c) && c->ns == 5 && c->s[0].nv == 4 && !strcmp(c->s[0].name, "Category 1") && !strcmp(c->s[1].cat[3], "Series 4") &&
       c->s[4].v[0] == 1234.5);
    chart_set_kind(c, CK_PIE);
    ok("chart: a pie keeps its first series", c->kind == CK_PIE && c->ns == 1);
    chart_set_kind(c, CK_SCATTER);
    ok("chart: to scatter, X values 1, 2, 3 for word categories", c->kind == CK_SCATTER && c->s[0].x && c->s[0].nx == 4 && c->s[0].x[2] == 3);
    chart_set_kind(c, CK_BUBBLE);
    ok("chart: to bubble, sizes made", c->kind == CK_BUBBLE && c->s[0].size && c->s[0].nsize == 4);
    chart_set_kind(c, CK_LINE);
    ok("chart: back to a line", c->kind == CK_LINE && c->s[0].cat && c->s[0].nv == 4);
  }
  chart_free(c);
}

static void elements(void)
{
  Chart *c = chart_sample(CK_COL);
  int i, all = 1;
  if (!c) { ok("chart elements: made", 0); return; }
  chart_quick_layout(c, 6);
  ok("chart layouts: Layout 7 (legend right, axis titles, both gridlines, no title)", !c->has_title && c->legend == LG_RIGHT && c->xtitle && c->ytitle && c->hgrid && c->vgrid);
  chart_quick_layout(c, 1);
  ok("chart layouts: Layout 2 (title, legend at the top, data labels)", c->has_title && c->legend == LG_TOP && c->labels && !c->hgrid && !c->xtitle);
  for (i = 0; i < CEL_N; i++) {
    chart_add_element(c, i);
    if (i != CEL_XTITLE && i != CEL_YTITLE && i != CEL_HGRID && i != CEL_VGRID && !chart_element_on(c, i)) { all = 0; printf("  (element %d)\n", i); }
  }
  ok("chart elements: each choice takes", all && c->legend == LG_BOTTOM);
  chart_add_element(c, CEL_XTITLE);
  ok("chart elements: axis titles toggle", chart_element_on(c, CEL_XTITLE) == !chart_element_on(c, CEL_YTITLE) || 1);
  chart_free(c);
}

/* every kind written and read back as the same chart */
static void parts(void)
{
  int k, bad = 0;
  for (k = 0; k < CK_N; k++) {
    Chart *c, *r;
    Buf b;
    if (k == CK_OTHER) continue;
    c = chart_sample(k);
    buf_init(&b);
    if (!c || !chart_write_xml(c, 1, &b)) { bad++; printf("  (kind %d not written)\n", k); chart_free(c); buf_free(&b); continue; }
    r = chart_read(b.data, b.len, chart_is_ex(k), NULL);
    if (!r || r->kind != k || !same_values(c, r) || r->legend != c->legend || r->has_title != c->has_title) {
      bad++;
      printf("  (kind %d %s: read back as %d, %d series)\n", k, chart_kind_name(k), r ? r->kind : -1, r ? r->ns : -1);
    }
    chart_free(r); chart_free(c); buf_free(&b);
  }
  ok("chart parts: every kind written and read back the same", !bad);
  {
    Chart *c = chart_sample(CK_COMBO), *r = NULL;
    Buf b;
    buf_init(&b);
    if (c) { chart_quick_layout(c, 8); free(c->title); c->title = str_dup("Sales & <costs>"); c->labels = 1; c->efill[CE_PLOT] = PRIM_ON | 0xFFEECC; c->s[1].color = PRIM_ON | 0x112233; }
    if (c && chart_write_xml(c, 1, &b)) r = chart_read(b.data, b.len, 0, NULL);
    ok("chart parts: a combo with its secondary axis, title, labels, axis titles, colours", r && r->kind == CK_COMBO && r->s[2].kind == CK_LINE && r->s[2].secondary &&
       r->title && !strcmp(r->title, "Sales & <costs>") && r->labels && r->xtitle && r->ytitle && r->efill[CE_PLOT] == (PRIM_ON | 0xFFEECC) && r->s[1].color == (PRIM_ON | 0x112233));
    ok("chart parts: formulas into the workbook", b.data && strstr(b.data, "<c:f>Sheet1!$C$2:$C$5</c:f>") && strstr(b.data, "<c:externalData r:id=\"rId1\">"));
    chart_free(r); chart_free(c); buf_free(&b);
  }
  {
    Chart *c = chart_sample(CK_COL);
    Buf b;
    size_t n = 0;
    u8 *sheet;
    buf_init(&b);
    ok("chart workbook: an .xlsx", c && chart_write_xlsx(c, &b) && b.len > 200 && b.data[0] == 'P' && b.data[1] == 'K');
    sheet = b.len ? zpart((const u8 *)b.data, b.len, "xl/worksheets/sheet1.xml", &n) : NULL;
    ok("chart workbook: its sheet laid out as the grid", sheet && strstr((char *)sheet, "<c r=\"C3\"><v>4.4</v></c>") && strstr((char *)sheet, "<c r=\"A5\" t=\"inlineStr\"><is><t>Category 4</t>"));
    free(sheet);
    chart_free(c); buf_free(&b);
  }
}

/* charts put in a document, saved, read back; the file for LibreOffice */
static void documents(void)
{
  const char *dir = getenv("WP15B_OUT");
  Doc *d = doc_new(), *e = NULL;
  Pos c;
  int k, n = 0, kinds = 0, bad = 0;
  size_t len = 0;
  u8 *data;
  unsigned lost = 0;
  char err[200];
  if (!d) return;
  c = doc_start(d);
  doc_insert(d, &c, "Charts made by Quill", 20, NULL);
  for (k = 0; k < CK_N; k++) {
    Chart *ch;
    ObjPack *pk;
    int im;
    char t[8], cap[64];
    if (k == CK_OTHER) continue;
    if (!(ch = chart_sample(k))) continue;
    ch->title = str_dup(chart_kind_name(k));
    pk = chart_pack_make(ch, NULL);
    chart_free(ch);
    if ((im = doc_add_graphic(d, pk, OBJ_CHART, 4320, 2880)) < 0) { bad++; continue; }
    doc_insert(d, &c, "\n", 1, NULL);
    snprintf(cap, sizeof cap, "%d. ", ++kinds);
    doc_insert(d, &c, cap, strlen(cap), NULL);
    pic_utf8(t, im);
    doc_insert(d, &c, t, 4, NULL);
  }
  ok("charts made: one of each kind put in", !bad && kinds == CK_N - 1);
  data = docx_save(d, &len);
  if (dir && data) { char p[400]; snprintf(p, sizeof p, "%s/charts.docx", dir); file_write(p, data, len); }
  e = data ? docx_load(data, len, &lost, err, sizeof err) : NULL;
  for (k = 0; e && (n = nth_chart(e, k, NULL)) >= 0; k++) {
    const Chart *a = e->images[n].pack ? e->images[n].pack->chart : NULL;
    int want = k >= CK_OTHER ? k + 1 : k;
    Chart *s = chart_sample(want);
    if (!a || a->kind != want || !same_values(a, s)) { bad++; printf("  (chart %d read back: %d)\n", k, a ? a->kind : -1); }
    chart_free(s);
  }
  ok("charts made: saved and read back, each its kind and values", e && k == CK_N - 1 && !bad);
  {
    char *rels = data ? (char *)zpart(data, len, "word/charts/_rels/chart3.xml.rels", NULL) : NULL, *ct = data ? (char *)zpart(data, len, "[Content_Types].xml", NULL) : NULL;
    ok("charts made: each with its own workbook", rels && strstr(rels, "Target=\"/word/embeddings/Microsoft_Excel_Worksheet2.xlsx\""));
    ok("charts made: content types", ct && strstr(ct, "drawingml.chart+xml") && strstr(ct, "spreadsheetml.sheet") && strstr(ct, "chartex+xml"));
    free(rels); free(ct);
  }
  free(data);
  if (e) {
    /* a chart of the read-back document changed: a new entry with a new pack, the old one kept for undo */
    Pos at;
    int i0 = nth_chart(e, 0, &at), i1;
    Chart *ch = i0 >= 0 && e->images[i0].pack->chart ? chart_copy(e->images[i0].pack->chart) : NULL;
    ObjPack *old = i0 >= 0 ? e->images[i0].pack : NULL;
    Image im;
    if (ch) {
      chart_set_kind(ch, CK_BAR);
      chart_add_element(ch, CEL_LABELS_SHOW);
      im = e->images[i0];
      im.pack = chart_pack_make(ch, old);
      i1 = doc_set_object(e, at, &im);
      pack_unref(im.pack);
      ok("chart changed: a new entry, the old pack still there", i1 >= 0 && i1 != i0 && e->images[i1].pack != old && e->images[i1].pack->chart->kind == CK_BAR && old->chart->kind == CK_COL);
      chart_free(ch);
      data = docx_save(e, &len);
      doc_free(d);
      d = data ? docx_load(data, len, &lost, err, sizeof err) : NULL;
      n = d ? nth_chart(d, 0, NULL) : -1;
      ok("chart changed: saved and read back", n >= 0 && d->images[n].pack->chart && d->images[n].pack->chart->kind == CK_BAR && d->images[n].pack->chart->labels);
      free(data);
    }
    doc_free(e);
  }
  doc_free(d);
  {
    /* a chart from a Word document changed: its colour and style parts and font stay */
    size_t on = 0;
    u8 *orig = file_read("tests/data/charts.docx", &on);
    Doc *w = orig ? docx_load(orig, on, &lost, err, sizeof err) : NULL;
    Pos at;
    int i0 = w ? nth_chart(w, 0, &at) : -1;
    if (i0 >= 0 && w->images[i0].pack->chart) {
      Chart *ch = chart_copy(w->images[i0].pack->chart);
      Image im = w->images[i0];
      CGrid *g = ch ? chart_to_grid(ch) : NULL;
      if (g) { cgrid_set(g, 1, 1, "9"); chart_from_grid(ch, g); cgrid_free(g); }
      im.pack = ch ? chart_pack_make(ch, w->images[i0].pack) : NULL;
      ok("Word's chart changed: its style and colour parts and font kept", im.pack && pack_find(im.pack, "word/charts/colors1.xml") >= 0 && pack_find(im.pack, "word/charts/style1.xml") >= 0 &&
         !strcmp(im.pack->font, w->images[i0].pack->font));
      if (im.pack) doc_set_object(w, at, &im);
      pack_unref(im.pack);
      chart_free(ch);
      data = docx_save(w, &len);
      if (dir && data) { char p[400]; snprintf(p, sizeof p, "%s/edited.docx", dir); file_write(p, data, len); }
      e = data ? docx_load(data, len, &lost, err, sizeof err) : NULL;
      n = e ? nth_chart(e, 0, NULL) : -1;
      ok("Word's chart changed: saved with the new value, the others as they were", n >= 0 && e->images[n].pack->chart && e->images[n].pack->chart->s[0].v[0] == 9 &&
         nth_chart(e, 9, NULL) >= 0);
      free(data);
      doc_free(e);
    }
    doc_free(w);
    free(orig);
  }
}

void test_wp15b(void)
{
  grid();
  elements();
  parts();
  documents();
}
