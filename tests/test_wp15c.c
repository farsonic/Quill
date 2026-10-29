/* test_wp15c.c - the native tests of WP15c Insert SmartArt and SmartArt Design (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp15c().
   With WP15C_OUT set to a folder, smartart.docx (every layout) and edited.docx (tests/data/charts.docx's
   SmartArt changed) are written there for checking in LibreOffice */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/chartedit.h"
#include "../core/zip.h"

static int nth_art(Doc *d, int nth, Pos *at)
{
  u32 i, k;
  for (i = 0; i < d->np; i++)
    for (k = 0; k + 4 <= d->p[i]->len; k++)
      if (IS_PIC(d->p[i]->t + k)) {
        int m = pic_index(d->p[i]->t + k);
        if (m < d->nimages && d->images[m].kind == OBJ_SMARTART && !nth--) { if (at) { at->para = i; at->off = k; } return m; }
        k += 3;
      }
  return -1;
}
/* the outline as text: "Lead|-Design|--Sketches" */
static void outline(const SAModel *m, char *out, size_t cap)
{
  int i;
  size_t n = 0;
  out[0] = 0;
  for (i = 0; m && i < m->n && n + 2 < cap; i++) {
    int l;
    if (i) out[n++] = '|';
    for (l = 0; l < m->it[i].level && n + 2 < cap; l++) out[n++] = '-';
    n += (size_t)snprintf(out + n, cap - n, "%s", m->it[i].text);
    if (n >= cap) n = cap - 1;
  }
  out[n] = 0;
}
static int is(const SAModel *m, const char *want)
{
  char t[600];
  outline(m, t, sizeof t);
  if (strcmp(t, want)) { printf("  (outline %s, wanted %s)\n", t, want); return 0; }
  return 1;
}

static void editing(void)
{
  SAModel *m = sa_sample(10);       /* Hierarchy */
  int i;
  ok("SmartArt: a hierarchy's sample", m && !strcmp(sa_layout_name(m->layout), "Hierarchy") && is(m, "Lead|-Design|--Sketches|-Build|--Code|--Tests"));
  if (!m) return;
  i = sa_add(m, 1, SA_AFTER, "Sell");
  ok("SmartArt: add shape after (past the items under it)", i == 3 && is(m, "Lead|-Design|--Sketches|-Sell|-Build|--Code|--Tests"));
  i = sa_add(m, 3, SA_BEFORE, "Ask");
  ok("SmartArt: add shape before", i == 3 && is(m, "Lead|-Design|--Sketches|-Ask|-Sell|-Build|--Code|--Tests"));
  i = sa_add(m, 5, SA_BELOW, "Deploy");
  ok("SmartArt: add shape below (its last item)", i == 8 && is(m, "Lead|-Design|--Sketches|-Ask|-Sell|-Build|--Code|--Tests|--Deploy"));
  i = sa_add(m, 1, SA_ABOVE, "Studio");
  ok("SmartArt: add shape above (a level put over it)", i == 1 && is(m, "Lead|-Studio|--Design|---Sketches|-Ask|-Sell|-Build|--Code|--Tests|--Deploy"));
  ok("SmartArt: promote (with the items under it)", sa_promote(m, 2) == 2 && is(m, "Lead|-Studio|-Design|--Sketches|-Ask|-Sell|-Build|--Code|--Tests|--Deploy"));
  ok("SmartArt: demote", sa_demote(m, 4) == 4 && is(m, "Lead|-Studio|-Design|--Sketches|--Ask|-Sell|-Build|--Code|--Tests|--Deploy"));
  ok("SmartArt: no demote past the item before", sa_demote(m, 1) < 0 && sa_promote(m, 0) < 0);
  ok("SmartArt: move down past its neighbour (with its items)", sa_move(m, 2, 1) == 3 && is(m, "Lead|-Studio|-Sell|-Design|--Sketches|--Ask|-Build|--Code|--Tests|--Deploy"));
  ok("SmartArt: move up", sa_move(m, 6, 0) == 3 && is(m, "Lead|-Studio|-Sell|-Build|--Code|--Tests|--Deploy|-Design|--Sketches|--Ask"));
  ok("SmartArt: no move past the first", sa_move(m, 1, 0) < 0 && sa_move(m, 0, 0) < 0);
  ok("SmartArt: delete (its items up a level)", sa_delete(m, 3) == 3 && is(m, "Lead|-Studio|-Sell|-Code|-Tests|-Deploy|-Design|--Sketches|--Ask"));
  ok("SmartArt: text cleaned", sa_set_text(m, 1, "  Lab\tone \n") == 1 && !strcmp(m->it[1].text, "Lab one"));
  sa_free(m);
}

/* every layout written: the data part read back as the same model, the drawing read and drawn inside its frame */
static void parts(void)
{
  int k, bad = 0, badd = 0;
  for (k = 0; k < sa_layout_count(); k++) {
    SAModel *m = sa_sample(k), *r;
    ObjPack *pk;
    char a[600], b[600];
    int i, ntop;
    if (!m) { bad++; continue; }
    m->colors = (u8)(k % SA_COLORS); m->style = (u8)(k % SA_STYLES); m->rtl = (u8)(k == 3);
    sa_add(m, m->n - 1, SA_BELOW, "Under <last> & more");
    pk = sa_pack_make(m, 8640, 4320, NULL);
    r = pk ? sa_from_pack(pk) : NULL;
    outline(m, a, sizeof a); outline(r, b, sizeof b);
    if (!r || strcmp(a, b) || r->layout != m->layout || r->colors != m->colors || r->style != m->style || r->rtl != m->rtl) {
      bad++; printf("  (layout %d %s: read back %s / %d %d %d %d)\n", k, sa_layout_name(k), b, r ? r->layout : -1, r ? r->colors : -1, r ? r->style : -1, r ? r->rtl : -1);
    }
    for (i = 0, ntop = 0; i < m->n; i++) if (!m->it[i].level) ntop++;
    if (!pk || !pk->art || pk->art->n < ntop) { badd++; printf("  (layout %d: drawing %d shapes)\n", k, pk && pk->art ? pk->art->n : -1); }
    else {
      for (i = 0; i < pk->art->n; i++) {
        const SAShape *s = &pk->art->s[i];
        if (s->x < -6350 || s->y < -6350 || s->x + s->w > 8640L * 635 + 6350 || s->y + s->h > 4320L * 635 + 6350) { badd++; printf("  (layout %d shape %d outside: %ld %ld %ld %ld)\n", k, i, (long)s->x, (long)s->y, (long)s->w, (long)s->h); break; }
      }
    }
    sa_free(m); sa_free(r); pack_unref(pk);
  }
  ok("SmartArt parts: every layout's data read back as its model", !bad);
  ok("SmartArt parts: every layout's drawing read, inside its frame", !badd);
  {
    SAModel *m = sa_sample(0);
    Buf b;
    buf_init(&b);
    ok("SmartArt parts: the data names its drawing", m && sa_data_xml(m, "rId9", &b) && strstr(b.data, "relId=\"rId9\"") && strstr(b.data, "loTypeId=\"urn:microsoft.com/office/officeart/2005/8/layout/default\""));
    b.len = 0;
    ok("SmartArt parts: layout, colours, quick style", m && sa_layout_xml(m, &b) && strstr(b.data, "<dgm:layoutDef") && (b.len = 0, sa_colors_xml(m, &b)) && strstr(b.data, "accent1_2") &&
       (b.len = 0, sa_quickstyle_xml(m, &b)) && strstr(b.data, "quickstyle/simple1"));
    buf_free(&b);
    sa_free(m);
  }
}

static void documents(void)
{
  const char *dir = getenv("WP15C_OUT");
  Doc *d = doc_new(), *e;
  Pos c;
  int k, bad = 0, n;
  size_t len = 0;
  u8 *data;
  unsigned lost = 0;
  char err[200];
  if (!d) return;
  c = doc_start(d);
  doc_insert(d, &c, "SmartArt made by Quill", 22, NULL);
  for (k = 0; k < sa_layout_count(); k++) {
    SAModel *m = sa_sample(k);
    ObjPack *pk;
    int im;
    char t[8], cap[80];
    if (!m) { bad++; continue; }
    m->colors = (u8)(k % SA_COLORS);
    pk = sa_pack_make(m, 5760, 2880, NULL);
    sa_free(m);
    if ((im = doc_add_graphic(d, pk, OBJ_SMARTART, 5760, 2880)) < 0) { bad++; continue; }
    snprintf(cap, sizeof cap, "\n%s: ", sa_layout_name(k));
    doc_insert(d, &c, cap, strlen(cap), NULL);
    pic_utf8(t, im);
    doc_insert(d, &c, t, 4, NULL);
  }
  data = docx_save(d, &len);
  if (dir && data) { char p[400]; snprintf(p, sizeof p, "%s/smartart.docx", dir); file_write(p, data, len); }
  e = data ? docx_load(data, len, &lost, err, sizeof err) : NULL;
  for (k = 0; e && (n = nth_art(e, k, NULL)) >= 0; k++) {
    SAModel *m = sa_from_pack(e->images[n].pack);
    if (!m || m->layout != k || !e->images[n].pack->art) { bad++; printf("  (SmartArt %d read back as %d)\n", k, m ? m->layout : -1); }
    sa_free(m);
  }
  ok("SmartArt made: every layout saved and read back, with its drawing", e && k == sa_layout_count() && !bad);
  {
    ZipReader z;
    char *rels = NULL, *data2 = NULL;
    if (data && zip_open(&z, data, len)) {
      rels = (char *)zip_read(&z, zip_find(&z, "word/_rels/document.xml.rels"), NULL);
      data2 = (char *)zip_read(&z, zip_find(&z, "word/diagrams/data2.xml"), NULL);
      zip_close(&z);
    }
    /* the second graphic's data names the id its drawing's relationship was given */
    if (rels && data2) {
      const char *at = strstr(data2, "relId=\""), *q;
      char want[120];
      int l = 0;
      if (at) { at += 7; q = strchr(at, '"'); l = q ? (int)(q - at) : 0; }
      snprintf(want, sizeof want, "Id=\"%.*s\" Type=\"http://schemas.microsoft.com/office/2007/relationships/diagramDrawing\" Target=\"diagrams/drawing2.xml\"", l, at ? at : "");
      ok("SmartArt made: each data part names its own drawing", l > 0 && strstr(rels, want) != NULL);
    } else ok("SmartArt made: each data part names its own drawing", 0);
    free(rels); free(data2);
  }
  free(data);
  doc_free(e);
  doc_free(d);
  {
    /* Word's SmartArt (a process) changed: read as a model, a shape added, the layout changed */
    size_t on = 0;
    u8 *orig = file_read("tests/data/charts.docx", &on);
    Doc *w = orig ? docx_load(orig, on, &lost, err, sizeof err) : NULL;
    Pos at;
    int i0 = w ? nth_art(w, 0, &at) : -1;
    SAModel *m = i0 >= 0 ? sa_from_pack(w->images[i0].pack) : NULL;
    ok("Word's SmartArt read as a model", m && !strcmp(sa_layout_name(m->layout), "Basic Process") && is(m, "Plan|Build|Ship") && m->colors == 0);
    if (m) {
      Image im = w->images[i0];
      sa_add(m, 2, SA_AFTER, "Support");
      m->layout = 5;                  /* Basic Chevron Process */
      im.pack = sa_pack_make(m, im.cx, im.cy, w->images[i0].pack);
      if (im.pack) doc_set_object(w, at, &im);
      pack_unref(im.pack);
      data = docx_save(w, &len);
      if (dir && data) { char p[400]; snprintf(p, sizeof p, "%s/edited.docx", dir); file_write(p, data, len); }
      e = data ? docx_load(data, len, &lost, err, sizeof err) : NULL;
      n = e ? nth_art(e, 0, NULL) : -1;
      sa_free(m);
      m = n >= 0 ? sa_from_pack(e->images[n].pack) : NULL;
      ok("Word's SmartArt changed: saved and read back", m && m->layout == 5 && is(m, "Plan|Build|Ship|Support") && e->images[n].pack->art && e->images[n].pack->art->n == 4);
      sa_free(m);
      free(data);
      doc_free(e);
    }
    doc_free(w);
    free(orig);
  }
}

/* (for the emulator: a short document with a chart and a SmartArt) */
static void ui_doc(const char *dir)
{
  Doc *d = doc_new();
  Pos c;
  Chart *ch = chart_sample(CK_COL);
  SAModel *m = sa_sample(3);
  int a, b;
  char t[8], p[400];
  size_t len = 0;
  u8 *data;
  if (!d) { chart_free(ch); sa_free(m); return; }
  c = doc_start(d);
  doc_insert(d, &c, "A chart and a SmartArt.\n", 24, NULL);
  a = doc_add_graphic(d, chart_pack_make(ch, NULL), OBJ_CHART, 5760, 2880);
  b = doc_add_graphic(d, sa_pack_make(m, 5760, 2160, NULL), OBJ_SMARTART, 5760, 2160);
  if (a >= 0) { pic_utf8(t, a); doc_insert(d, &c, t, 4, NULL); }
  doc_insert(d, &c, "\n", 1, NULL);
  if (b >= 0) { pic_utf8(t, b); doc_insert(d, &c, t, 4, NULL); }
  doc_insert(d, &c, "\nThe end.", 9, NULL);
  data = docx_save(d, &len);
  snprintf(p, sizeof p, "%s/ui.docx", dir);
  if (data) file_write(p, data, len);
  free(data);
  doc_free(d); chart_free(ch); sa_free(m);
}

void test_wp15c(void)
{
  if (getenv("WP15C_OUT")) ui_doc(getenv("WP15C_OUT"));
  editing();
  parts();
  documents();
}
