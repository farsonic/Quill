/* test_wp5.c - the native tests of WP5 Review: comments and markup (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp5() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/review.h"
#include "../core/layout.h"
#include "../core/zip.h"
#include "../core/keep.h"

#define W "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
#define W14 "xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\""
#define W15 "xmlns:w15=\"http://schemas.microsoft.com/office/word/2012/wordml\""
#define HDR "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
#define REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
#define CT "application/vnd.openxmlformats-officedocument.wordprocessingml."

/* a document as Word writes one under review: insertions, a deletion, a move, formatting changes of a
   run and a paragraph, a table with property changes and an inserted row, a section's change, two
   comments (the second a reply, the thread resolved) with their ids, times and people */
static const char *doc_xml =
  HDR "<w:document " W "><w:body>"
  "<w:p><w:r><w:t xml:space=\"preserve\">Plain </w:t></w:r>"
  "<w:ins w:id=\"1\" w:author=\"Ann\" w:date=\"2026-01-02T10:00:00Z\"><w:r><w:t>added</w:t></w:r></w:ins>"
  "<w:del w:id=\"2\" w:author=\"Bob\" w:date=\"2026-01-03T10:00:00Z\"><w:r><w:delText>gone</w:delText></w:r></w:del>"
  "<w:commentRangeStart w:id=\"0\"/><w:commentRangeStart w:id=\"1\"/>"
  "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"3\" w:author=\"Ann\" w:date=\"2026-01-04T10:00:00Z\"><w:rPr><w:i/></w:rPr></w:rPrChange></w:rPr><w:t>bold</w:t></w:r>"
  "<w:commentRangeEnd w:id=\"0\"/><w:r><w:commentReference w:id=\"0\"/></w:r><w:commentRangeEnd w:id=\"1\"/><w:r><w:commentReference w:id=\"1\"/></w:r></w:p>"
  "<w:p><w:pPr><w:jc w:val=\"center\"/><w:pPrChange w:id=\"4\" w:author=\"Bob\" w:date=\"2026-01-05T10:00:00Z\"><w:pPr><w:jc w:val=\"right\"/><w:suppressAutoHyphens/></w:pPr></w:pPrChange></w:pPr>"
  "<w:moveFromRangeStart w:id=\"5\" w:author=\"Ann\" w:date=\"2026-01-06T10:00:00Z\" w:name=\"move1\"/>"
  "<w:moveFrom w:id=\"6\" w:author=\"Ann\" w:date=\"2026-01-06T10:00:00Z\"><w:r><w:delText>moved</w:delText></w:r></w:moveFrom><w:moveFromRangeEnd w:id=\"5\"/>"
  "<w:r><w:t xml:space=\"preserve\"> centred </w:t></w:r>"
  "<w:moveToRangeStart w:id=\"7\" w:author=\"Ann\" w:date=\"2026-01-06T10:00:00Z\" w:name=\"move1\"/>"
  "<w:moveTo w:id=\"8\" w:author=\"Ann\" w:date=\"2026-01-06T10:00:00Z\"><w:r><w:t>moved</w:t></w:r></w:moveTo><w:moveToRangeEnd w:id=\"7\"/></w:p>"
  "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblPrChange w:id=\"9\" w:author=\"Cy\" w:date=\"2026-01-07T10:00:00Z\"><w:tblPr><w:tblW w:w=\"5000\" w:type=\"pct\"/></w:tblPr></w:tblPrChange></w:tblPr>"
  "<w:tblGrid><w:gridCol w:w=\"4000\"/><w:gridCol w:w=\"4000\"/></w:tblGrid>"
  "<w:tr><w:trPr><w:ins w:id=\"10\" w:author=\"Cy\" w:date=\"2026-01-07T10:00:00Z\"/></w:trPr>"
  "<w:tc><w:tcPr><w:tcW w:w=\"4000\" w:type=\"dxa\"/><w:tcPrChange w:id=\"11\" w:author=\"Cy\" w:date=\"2026-01-07T10:00:00Z\"><w:tcPr><w:tcW w:w=\"3000\" w:type=\"dxa\"/></w:tcPr></w:tcPrChange></w:tcPr><w:p><w:r><w:t>A1</w:t></w:r></w:p></w:tc>"
  "<w:tc><w:tcPr><w:tcW w:w=\"4000\" w:type=\"dxa\"/></w:tcPr><w:p><w:r><w:t>B1</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
  "<w:p><w:r><w:t>End</w:t></w:r></w:p>"
  "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/>"
  "<w:sectPrChange w:id=\"12\" w:author=\"Cy\" w:date=\"2026-01-08T10:00:00Z\"><w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr></w:sectPrChange></w:sectPr>"
  "</w:body></w:document>";
static const char *comments_xml =
  HDR "<w:comments " W " " W14 ">"
  "<w:comment w:id=\"0\" w:author=\"Ann\" w:date=\"2026-01-09T10:00:00Z\" w:initials=\"A\"><w:p w14:paraId=\"1A2B3C4D\" w14:textId=\"77777777\"><w:r><w:t>Why bold?</w:t></w:r></w:p></w:comment>"
  "<w:comment w:id=\"1\" w:author=\"Bob\" w:date=\"2026-01-10T10:00:00Z\" w:initials=\"B\"><w:p w14:paraId=\"00000AB1\" w14:textId=\"77777777\"><w:r><w:t>For emphasis.</w:t></w:r></w:p></w:comment>"
  "</w:comments>";
static const char *cex_xml = HDR "<w15:commentsEx " W15 "><w15:commentEx w15:paraId=\"1A2B3C4D\" w15:done=\"1\"/>"
  "<w15:commentEx w15:paraId=\"00000AB1\" w15:paraIdParent=\"1A2B3C4D\" w15:done=\"0\"/></w15:commentsEx>";
static const char *cid_xml = HDR "<w16cid:commentsIds xmlns:w16cid=\"http://schemas.microsoft.com/office/word/2016/wordml/cid\">"
  "<w16cid:commentId w16cid:paraId=\"1A2B3C4D\" w16cid:durableId=\"5E6F7A8B\"/><w16cid:commentId w16cid:paraId=\"00000AB1\" w16cid:durableId=\"12345678\"/></w16cid:commentsIds>";
static const char *cxt_xml = HDR "<w16cex:commentsExtensible xmlns:w16cex=\"http://schemas.microsoft.com/office/word/2018/wordml/cex\">"
  "<w16cex:commentExtensible w16cex:durableId=\"5E6F7A8B\" w16cex:dateUtc=\"2026-01-09T09:00:00Z\"/></w16cex:commentsExtensible>";
static const char *people_xml = HDR "<w15:people " W15 "><w15:person w15:author=\"Ann\"><w15:presenceInfo w15:providerId=\"AD\" w15:userId=\"S-1-5-21-7\"/></w15:person></w15:people>";
static const char *settings_xml = HDR "<w:settings " W "><w:revisionView w:formatting=\"0\"/><w:doNotTrackMoves/><w:defaultTabStop w:val=\"720\"/></w:settings>";

/* the package, zipped (malloc'd, *n its size) */
static u8 *fixture(size_t *n)
{
  ZipWriter z;
  u8 *r;
  {
    const char *ct = HDR "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
      "<Override PartName=\"/word/document.xml\" ContentType=\"" CT "document.main+xml\"/>"
      "<Override PartName=\"/word/comments.xml\" ContentType=\"" CT "comments+xml\"/>"
      "<Override PartName=\"/word/commentsExtended.xml\" ContentType=\"" CT "commentsExtended+xml\"/>"
      "<Override PartName=\"/word/commentsIds.xml\" ContentType=\"" CT "commentsIds+xml\"/>"
      "<Override PartName=\"/word/commentsExtensible.xml\" ContentType=\"" CT "commentsExtensible+xml\"/>"
      "<Override PartName=\"/word/people.xml\" ContentType=\"" CT "people+xml\"/>"
      "<Override PartName=\"/word/settings.xml\" ContentType=\"" CT "settings+xml\"/></Types>";
    zip_begin(&z);
    zip_add(&z, "[Content_Types].xml", ct, strlen(ct));
  }
  {
    const char *r = HDR "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"" REL "officeDocument\" Target=\"word/document.xml\"/></Relationships>";
    zip_add(&z, "_rels/.rels", r, strlen(r));
  }
  {
    const char *r = HDR "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"" REL "comments\" Target=\"comments.xml\"/>"
      "<Relationship Id=\"rId2\" Type=\"http://schemas.microsoft.com/office/2011/relationships/commentsExtended\" Target=\"commentsExtended.xml\"/>"
      "<Relationship Id=\"rId3\" Type=\"http://schemas.microsoft.com/office/2016/09/relationships/commentsIds\" Target=\"commentsIds.xml\"/>"
      "<Relationship Id=\"rId4\" Type=\"http://schemas.microsoft.com/office/2018/08/relationships/commentsExtensible\" Target=\"commentsExtensible.xml\"/>"
      "<Relationship Id=\"rId5\" Type=\"http://schemas.microsoft.com/office/2011/relationships/people\" Target=\"people.xml\"/>"
      "<Relationship Id=\"rId6\" Type=\"" REL "settings\" Target=\"settings.xml\"/></Relationships>";
    zip_add(&z, "word/_rels/document.xml.rels", r, strlen(r));
  }
  zip_add(&z, "word/document.xml", doc_xml, strlen(doc_xml));
  zip_add(&z, "word/comments.xml", comments_xml, strlen(comments_xml));
  zip_add(&z, "word/commentsExtended.xml", cex_xml, strlen(cex_xml));
  zip_add(&z, "word/commentsIds.xml", cid_xml, strlen(cid_xml));
  zip_add(&z, "word/commentsExtensible.xml", cxt_xml, strlen(cxt_xml));
  zip_add(&z, "word/people.xml", people_xml, strlen(people_xml));
  zip_add(&z, "word/settings.xml", settings_xml, strlen(settings_xml));
  zip_finish(&z);
  *n = z.out.len;
  r = (u8 *)buf_take(&z.out);
  zip_free(&z);
  return r;
}
/* a part of a saved package as text (malloc'd), NULL if absent */
static char *part_of(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  size_t len;
  u8 *b;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), &len);
  zip_close(&z);
  return (char *)b;
}
/* the run covering the text found first in paragraph pi */
static const CharFmt *run_of(Doc *d, u32 pi, const char *text)
{
  const char *f = pi < d->np ? strstr(d->p[pi]->t, text) : NULL;
  return f ? para_fmt_at(d->p[pi], (u32)(f - d->p[pi]->t)) : NULL;
}
/* the paragraph whose text contains s, or -1 */
static int para_with(Doc *d, const char *s)
{
  u32 i;
  for (i = 0; i < d->np; i++) if (strstr(d->p[i]->t, s)) return (int)i;
  return -1;
}

/* everything read from Word's parts */
static void check_read(Doc *d, const char *what)
{
  char m[160];
  const CharFmt *f;
  const FmtRev *fr;
  int pi, k;
  snprintf(m, sizeof m, "%s: the bold run's formatting change (was italic, by Ann)", what);
  f = run_of(d, 0, "bold");
  fr = f ? rv_fmt(d, f->frev) : NULL;
  ok(m, f && (f->flags & CF_BOLD) && fr && !fr->para && (fr->ocf.flags & CF_ITALIC) && !(fr->ocf.flags & CF_BOLD) && !strcmp(d->revs[fr->rid].author, "Ann"));
  snprintf(m, sizeof m, "%s: the old w:rPr kept as written", what);
  ok(m, fr && fr->raw && strstr(fr->raw, "<w:i/>"));
  pi = para_with(d, "centred");
  fr = pi >= 0 ? rv_fmt(d, d->p[pi]->pf.prev) : NULL;
  snprintf(m, sizeof m, "%s: the paragraph's change (right aligned before, by Bob)", what);
  ok(m, pi >= 0 && d->p[pi]->pf.align == AL_CENTER && fr && fr->para && fr->opf.align == AL_RIGHT && !strcmp(d->revs[fr->rid].author, "Bob"));
  snprintf(m, sizeof m, "%s: the old w:pPr kept with what Quill doesn't model", what);
  ok(m, fr && fr->raw && strstr(fr->raw, "suppressAutoHyphens"));
  f = pi >= 0 ? run_of(d, (u32)pi, "moved") : NULL;
  snprintf(m, sizeof m, "%s: a move's two halves", what);
  ok(m, f && f->rev == REV_DEL && f->move && !strcmp(rv_move_name(d, f->move), "move1"));
  {
    const char *t = pi >= 0 ? strstr(d->p[pi]->t, "centred ") : NULL;
    const CharFmt *g = t ? para_fmt_at(d->p[pi], (u32)(t - d->p[pi]->t) + 8) : NULL;
    ok(m, g && g->rev == REV_INS && g->move == f->move);
  }
  snprintf(m, sizeof m, "%s: table, row, cell and section changes kept", what);
  ok(m, rv_has_kept(d, RK_TBL, 1, 0, 0, 0) && rv_has_kept(d, RK_TRREV, 1, 0, 0, 0) && rv_has_kept(d, RK_TC, 1, 0, 0, 0) &&
        !rv_has_kept(d, RK_TC, 1, 0, 1, 0) && rv_has_kept(d, RK_SECT, 0, 0, 0, 0));
  snprintf(m, sizeof m, "%s: two comments, the second a reply, the thread resolved", what);
  ok(m, d->ncomments == 2 && !d->comments[0].parent && d->comments[1].parent == 1 && d->comments[0].done && d->comments[1].done);
  snprintf(m, sizeof m, "%s: durable ids and the time in UTC", what);
  ok(m, d->ncomments == 2 && d->comments[0].durable == 0x5E6F7A8B && d->comments[1].durable == 0x12345678 && !strcmp(d->comments[0].dateutc, "2026-01-09T09:00:00Z"));
  snprintf(m, sizeof m, "%s: people.xml read (and no longer kept verbatim)", what);
  for (k = 0; k < d->npeople && strcmp(d->people[k].author, "Ann"); k++) ;
  ok(m, k < d->npeople && d->people[k].raw && strstr(d->people[k].raw, "S-1-5-21-7") && keep_find(d, "word/people.xml") < 0);
  snprintf(m, sizeof m, "%s: revisionView and doNotTrackMoves", what);
  ok(m, (rv_hidden_kinds(d) & RVH_FORMAT) && (d->rvsettings & RVS_NOMOVES) && !(d->rvsettings & RVS_NOFMT));
}

static void docx_roundtrip(void)
{
  size_t n, n2;
  u8 *data = fixture(&n), *out;
  char err[200], *x;
  unsigned lost = 0;
  Doc *d = docx_load(data, n, &lost, err, sizeof err), *e;
  file_write("/tmp/quill_wp5_word.docx", data, n);         /* (for looking at by hand: LibreOffice) */
  ok("wp5 read: the fixture loads", d != NULL);
  if (!d) { free(data); return; }
  check_read(d, "wp5 read");
  out = docx_save(d, &n2);
  ok("wp5 save: saved", out != NULL);
  if (!out) { doc_free(d); free(data); return; }
  x = part_of(out, n2, "word/document.xml");
  ok("wp5 save: w:rPrChange with the old w:rPr inside the run's w:rPr", x && strstr(x, "<w:b/><w:bCs/><w:rPrChange") && strstr(x, "<w:rPr><w:i/></w:rPr></w:rPrChange></w:rPr>"));
  ok("wp5 save: w:pPrChange last in the w:pPr, the old one verbatim", x && strstr(x, "<w:pPrChange") && strstr(x, "<w:suppressAutoHyphens/></w:pPr></w:pPrChange></w:pPr>"));
  ok("wp5 save: moves as w:moveFrom/w:moveTo in named ranges", x && strstr(x, "<w:moveFromRangeStart") && strstr(x, "w:name=\"move1\"") && strstr(x, "<w:moveFrom ") &&
     strstr(x, "<w:moveTo ") && strstr(x, "<w:moveToRangeEnd") && strstr(x, "\">moved</w:delText>"));
  ok("wp5 save: table, row, cell and section changes verbatim", x && strstr(x, "<w:tblPrChange w:id=\"9\" w:author=\"Cy\"") && strstr(x, "<w:trPr><w:ins w:id=\"10\"") &&
     strstr(x, "<w:tcPrChange w:id=\"11\"") && strstr(x, "<w:sectPrChange w:id=\"12\""));
  free(x);
  x = part_of(out, n2, "word/comments.xml");
  ok("wp5 save: comments carry w14:paraId", x && strstr(x, "xmlns:w14=") && strstr(x, "w14:paraId=\"10000001\"") && strstr(x, "w14:paraId=\"10000002\""));
  free(x);
  x = part_of(out, n2, "word/commentsExtended.xml");
  ok("wp5 save: commentsExtended: the reply's parent, done", x && strstr(x, "w15:paraId=\"10000002\" w15:paraIdParent=\"10000001\" w15:done=\"1\"") &&
     strstr(x, "w15:paraId=\"10000001\" w15:done=\"1\""));
  free(x);
  x = part_of(out, n2, "word/commentsIds.xml");
  ok("wp5 save: commentsIds keeps the durable ids", x && strstr(x, "w16cid:durableId=\"5E6F7A8B\"") && strstr(x, "w16cid:durableId=\"12345678\""));
  free(x);
  x = part_of(out, n2, "word/commentsExtensible.xml");
  ok("wp5 save: commentsExtensible keeps the UTC time", x && strstr(x, "w16cex:dateUtc=\"2026-01-09T09:00:00Z\""));
  free(x);
  x = part_of(out, n2, "word/people.xml");
  ok("wp5 save: people.xml: Ann's own entry (once), then the other authors", x && strstr(x, "S-1-5-21-7") && strstr(x, "w15:author=\"Bob\"") && strstr(x, "w15:author=\"Cy\"") &&
     strstr(x, "w15:author=\"Ann\"") == strstr(x, "w15:author=\"Ann\"><w15:presenceInfo w15:providerId=\"AD\"") && !strstr(strstr(x, "w15:author=\"Ann\"") + 5, "w15:author=\"Ann\""));
  free(x);
  x = part_of(out, n2, "[Content_Types].xml");
  ok("wp5 save: the parts' content types", x && strstr(x, "commentsExtended+xml") && strstr(x, "commentsIds+xml") && strstr(x, "people+xml"));
  free(x);
  x = part_of(out, n2, "word/_rels/document.xml.rels");
  ok("wp5 save: and their relationships", x && strstr(x, "relationships/commentsExtended") && strstr(x, "relationships/commentsIds") && strstr(x, "relationships/people"));
  free(x);
  x = part_of(out, n2, "word/settings.xml");
  ok("wp5 save: revisionView and doNotTrackMoves before defaultTabStop", x && strstr(x, "<w:revisionView w:formatting=\"0\"/><w:doNotTrackMoves/>") &&
     strstr(x, "doNotTrackMoves") < strstr(x, "defaultTabStop"));
  free(x);
  e = docx_load(out, n2, &lost, err, sizeof err);
  ok("wp5 again: reads back", e != NULL);
  if (e) check_read(e, "wp5 again");
  {
    /* and a second save is the same document */
    size_t n3;
    u8 *out2 = e ? docx_save(e, &n3) : NULL;
    char *a = part_of(out, n2, "word/document.xml"), *b = out2 ? part_of(out2, n3, "word/document.xml") : NULL;
    ok("wp5 again: the body is written the same", a && b && strlen(a) == strlen(b));
    free(a); free(b); free(out2);
  }
  doc_free(e);
  /* accepting and rejecting */
  {
    int pi = para_with(d, "centred");
    const CharFmt *f;
    Pos a, b;
    rv_set_hide(d, 0);
    f = run_of(d, 0, "bold");
    a = P(0, (u32)(strstr(d->p[0]->t, "bold") - d->p[0]->t)); b = a; b.off += 4;
    ok("wp5 reject: a run's formatting goes back (italic, not bold)", doc_resolve(d, a, b, 0) && (f = run_of(d, 0, "bold")) && (f->flags & CF_ITALIC) && !(f->flags & CF_BOLD) && !f->frev);
    doc_undo(d, &a);
    f = run_of(d, 0, "bold");
    ok("wp5 reject: undone", f && (f->flags & CF_BOLD) && f->frev);
    a = P(0, (u32)(strstr(d->p[0]->t, "bold") - d->p[0]->t)); b = a; b.off += 4;
    ok("wp5 accept: the new formatting stays", doc_resolve(d, a, b, 1) && (f = run_of(d, 0, "bold")) && (f->flags & CF_BOLD) && !f->frev);
    a = P((u32)pi, 0);
    ok("wp5 next change: the paragraph's formatting is a change", doc_next_change(d, P((u32)pi, 0), 1, &a, &b) && a.para == (u32)pi && a.off == 0 && b.off == d->p[pi]->len);
    ok("wp5 reject: the paragraph is right aligned again", doc_resolve(d, P((u32)pi, 0), P((u32)pi, 0), 0) && d->p[pi]->pf.align == AL_RIGHT && !d->p[pi]->pf.prev);
    ok("wp5 accept all: the table's and section's kept changes go", doc_resolve(d, doc_start(d), doc_end_pos(d), 1) && d->nkchg == 0);
    ok("wp5 accept all: no change left", !doc_next_change(d, doc_start(d), 1, &a, &b));
    ok("wp5 accept all: the move's insertion kept, its deletion gone", strstr(d->p[pi]->t, " centred moved") == d->p[pi]->t);
  }
  free(out);
  doc_free(d);
  free(data);
}

/* formatting tracked as it is changed */
static void tracking(void)
{
  Doc *d = doc_new();
  Pos a = P(0, 0), b;
  const CharFmt *f;
  const FmtRev *fr;
  ParaFmt pf;
  RvChange ch[8];
  char t[200];
  int n;
  doc_insert(d, &a, "Hello world", 11, NULL);
  d->rvtrack = (u16)(doc_add_rev(d, "Me", "2026-10-29T12:00:00Z") + 1);
  doc_toggle(d, P(0, 0), P(0, 5), CF_BOLD);
  f = run_of(d, 0, "Hello");
  fr = f ? rv_fmt(d, f->frev) : NULL;
  ok("wp5 track: bold recorded as a formatting change", f && (f->flags & CF_BOLD) && fr && !(fr->ocf.flags & CF_BOLD) && !strcmp(d->revs[fr->rid].author, "Me"));
  ok("wp5 track: only the text formatted", !run_of(d, 0, "world")->frev);
  doc_toggle(d, P(0, 0), P(0, 5), CF_ITALIC);
  f = run_of(d, 0, "Hello");
  ok("wp5 track: a second change keeps the first's old look", f && f->frev && !(rv_fmt(d, f->frev)->ocf.flags & (CF_BOLD | CF_ITALIC)));
  rv_fmt_names(d, f->frev, f, NULL, t, sizeof t, NULL);
  ok("wp5 track: described as Bold, Italic", !strcmp(t, "Bold, Italic"));
  memset(&pf, 0, sizeof pf); pf.align = AL_CENTER;
  doc_para_format(d, 0, 0, PM_ALIGN, &pf);
  ok("wp5 track: paragraph formatting recorded", d->p[0]->pf.prev && rv_fmt(d, d->p[0]->pf.prev)->opf.align == AL_LEFT);
  n = rv_changes(d, ch, 8);
  ok("wp5 changes: the paragraph's then the run's", n == 2 && ch[0].kind == RVC_PARA && ch[1].kind == RVC_FORMAT && ch[1].b.off == 5);
  rv_change_text(d, &ch[1], t, sizeof t, NULL);
  ok("wp5 changes: their text", !strcmp(t, "Formatted: Bold, Italic") && !strcmp(rv_change_author(d, &ch[1]), "Me"));
  doc_undo(d, &a);
  ok("wp5 track: undo takes the paragraph's change with it", !d->p[0]->pf.prev && d->p[0]->pf.align == AL_LEFT);
  /* typing while tracking is an insertion, not a formatting change */
  a = P(0, 11);
  {
    CharFmt ins = *para_fmt_at(d->p[0], 10);
    ins.rev = REV_INS; ins.rid = (u16)(d->rvtrack - 1);
    doc_insert(d, &a, "!", 1, &ins);
  }
  ok("wp5 track: typing is not a formatting change", !run_of(d, 0, "!")->frev);
  /* with Track formatting off (the document's setting) nothing is recorded */
  d->rvsettings |= RVS_NOFMT;
  doc_toggle(d, P(0, 6), P(0, 11), CF_UNDERLINE);
  ok("wp5 track: Track formatting off", !run_of(d, 0, "world")->frev);
  d->rvsettings &= (u8)~RVS_NOFMT;
  /* reject all: the text back as it was */
  ok("wp5 track: reject all", doc_resolve(d, doc_start(d), doc_end_pos(d), 0) && !(run_of(d, 0, "Hello")->flags & (CF_BOLD | CF_ITALIC)) && !strstr(d->p[0]->t, "!"));
  ok("wp5 track: nothing left to review", !doc_next_change(d, doc_start(d), 1, &a, &b) && runs_sane(d));
  /* a document saved with Quill's own formatting changes reads them back */
  doc_toggle(d, P(0, 0), P(0, 5), CF_BOLD);
  memset(&pf, 0, sizeof pf); pf.align = AL_RIGHT;
  doc_para_format(d, 0, 0, PM_ALIGN, &pf);
  {
    unsigned lost;
    Doc *e = docx_again(d, &lost);
    const FmtRev *er;
    f = e ? run_of(e, 0, "Hello") : NULL;
    er = f ? rv_fmt(e, f->frev) : NULL;
    ok("wp5 track: Quill's own changes saved and read back", er && !(er->ocf.flags & CF_BOLD) && (f->flags & CF_BOLD) && !strcmp(e->revs[er->rid].author, "Me") &&
       e->p[0]->pf.prev && rv_fmt(e, e->p[0]->pf.prev)->opf.align == AL_LEFT);
    doc_free(e);
  }
  doc_free(d);
}

/* replies, Resolve, deleting threads */
static void threads(void)
{
  Doc *d = doc_new();
  Pos a = P(0, 0);
  int k, r, reps[4];
  unsigned lost;
  Doc *e;
  doc_insert(d, &a, "Some text here", 14, NULL);
  k = doc_comment(d, P(0, 5), P(0, 9), "Is this right?", "Ann", "A", "2026-10-29T10:00:00Z");
  r = rv_reply(d, k, "Yes.", "Bob", "B", "2026-10-29T11:00:00Z");
  ok("wp5 reply: a reply in the thread", k == 0 && r == 1 && d->comments[1].parent == 1 && rv_thread_root(d, r) == k);
  ok("wp5 reply: its marks are in the text", rv_comment_live(d, r) && rv_replies(d, k, reps, 4) == 1 && reps[0] == r);
  ok("wp5 reply: the comment at the text is the thread's", doc_comment_at(d, P(0, 13), NULL, NULL) >= 0 && rv_thread_root(d, doc_comment_at(d, P(0, 13), NULL, NULL)) == k);
  rv_set_done(d, r, 1);
  ok("wp5 resolve: the whole thread", d->comments[0].done && d->comments[1].done);
  e = docx_again(d, &lost);
  ok("wp5 reply: saved and read back", e && e->ncomments == 2 && e->comments[1].parent == 1 && e->comments[0].done && e->comments[1].done && e->comments[0].durable);
  doc_free(e);
  rv_set_done(d, k, 0);
  ok("wp5 resolve: reopened", !d->comments[0].done && !d->comments[1].done);
  ok("wp5 delete: a thread goes whole", rv_delete_thread(d, k) && !rv_comment_live(d, k) && !rv_comment_live(d, r) && text_is(d, "Some text here"));
  doc_undo(d, &a);
  ok("wp5 delete: undone in one step", rv_comment_live(d, k) && rv_comment_live(d, r));
  ok("wp5 delete all comments", rv_delete_all_comments(d) && text_is(d, "Some text here"));
  doc_free(d);
}

/* Display for Review and Show Markup as the layout shows them */
static const Box *box_at(Layout *L, u32 pi, u32 off)
{
  u32 k;
  for (k = 0; k < L->pl[pi].nboxes; k++) if (L->pl[pi].boxes[k].off == off) return &L->pl[pi].boxes[k];
  return NULL;
}
static int hidden_bytes(Layout *L, u32 pi)
{
  u32 k;
  int n = 0;
  for (k = 0; k < L->pl[pi].nboxes; k++) if (L->pl[pi].boxes[k].kind == BX_REVHIDE) n += L->pl[pi].boxes[k].len;
  return n;
}
static void display(void)
{
  Doc *d = doc_new();
  Pos a = P(0, 0);
  CharFmt ins, del;
  Layout *L;
  RvItem it[8];
  int n;
  u8 mk;
  char t[200];
  doc_insert(d, &a, "keep ", 5, NULL);
  ins = *para_fmt_at(d->p[0], 0); ins.rev = REV_INS; ins.rid = (u16)doc_add_rev(d, "Ann", "");
  del = ins; del.rev = REV_DEL; del.rid = (u16)doc_add_rev(d, "Bob", "");
  doc_insert(d, &a, "new", 3, &ins);
  doc_insert(d, &a, "old", 3, &del);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("wp5 display: All Markup hides nothing", hidden_bytes(L, 0) == 0);
  rv_set_mode(d, RVM_NONE);
  lay_update(L);
  ok("wp5 display: No Markup hides the deletion", hidden_bytes(L, 0) == 3);
  {
    u32 k;
    int plain = 0;
    for (k = 0; k < L->pl[0].nboxes; k++) if (L->pl[0].boxes[k].off == 5 && !L->pl[0].boxes[k].f.rev) plain = 1;
    ok("wp5 display: and shows the insertion as plain text", plain);
  }
  rv_set_mode(d, RVM_ORIGINAL);
  lay_update(L);
  ok("wp5 display: Original hides the insertion", hidden_bytes(L, 0) == 3 && box_at(L, 0, 5) && box_at(L, 0, 5)->kind == BX_REVHIDE);
  rv_set_mode(d, RVM_SIMPLE);
  lay_update(L);
  ok("wp5 display: Simple Markup: as No Markup, with a bar", hidden_bytes(L, 0) == 3 && box_at(L, 0, 5) && box_at(L, 0, 5)->rvmark == RVK_INS);
  rv_set_mode(d, RVM_ALL);
  rv_show_person(d, "Bob", 0);
  lay_update(L);
  ok("wp5 display: Bob's markup hidden: his deletion goes, Ann's insertion shows", hidden_bytes(L, 0) == 3 && box_at(L, 0, 5) && box_at(L, 0, 5)->f.rev == REV_INS);
  ok("wp5 display: people shown", rv_person_shown(d, "Ann") && !rv_person_shown(d, "Bob"));
  rv_show_person(d, NULL, 1);
  rv_set_hide(d, RVH_INSDEL);
  lay_update(L);
  ok("wp5 display: insertions and deletions not shown", hidden_bytes(L, 0) == 3);
  rv_set_hide(d, 0);
  rv_set_balloons(d, RVB_ALL);
  lay_update(L);
  n = rv_page_items(L, 0, it, 8);
  ok("wp5 balloons: the deletion in a balloon", hidden_bytes(L, 0) == 3 && n == 1 && it[0].kind == RVI_DELETE && it[0].a.off == 8 && it[0].b.off == 11);
  rv_item_text(d, &it[0], t, sizeof t, NULL);
  ok("wp5 balloons: its text", !strcmp(t, "Deleted: old"));
  ok("wp5 balloons: the margin is needed", rv_any_margin(d));
  rv_set_balloons(d, RVB_FORMAT);
  lay_update(L);
  ok("wp5 balloons: comments and formatting only: none", rv_page_items(L, 0, it, 8) == 0 && !rv_any_margin(d));
  /* a formatting change's balloon */
  d->rvtrack = (u16)(doc_add_rev(d, "Cy", "") + 1);
  doc_toggle(d, P(0, 0), P(0, 4), CF_ITALIC);
  d->rvtrack = 0;
  lay_update(L);
  n = rv_page_items(L, 0, it, 8);
  ok("wp5 balloons: a formatting change", n == 1 && it[0].kind == RVI_FORMAT && it[0].a.off == 0 && it[0].b.off == 4);
  rv_item_text(d, &it[0], t, sizeof t, NULL);
  ok("wp5 balloons: Formatted: Italic", !strcmp(t, "Formatted: Italic"));
  rv_set_mode(d, RVM_ORIGINAL);
  lay_update(L);
  ok("wp5 display: Original shows the old formatting", !(L->pl[0].boxes[0].f.flags & CF_ITALIC));
  ok("wp5 display: settings change the layout's key", rv_sig(d) != 0);
  rv_set_mode(d, RVM_ALL);
  /* Accept All Changes Shown: only Ann's when Bob's are hidden */
  rv_show_person(d, "Bob", 0);
  rv_show_person(d, "Cy", 0);
  ok("wp5 accept shown: only what is shown", rv_resolve(d, doc_start(d), doc_end_pos(d), 1, 1) && para_fmt_at(d->p[0], 5)->rev == REV_NONE &&
     para_fmt_at(d->p[0], 8)->rev == REV_DEL && para_fmt_at(d->p[0], 0)->frev);
  rv_show_run(d, &del, &mk);
  lay_free(L);
  doc_free(d);
}

static void options(void)
{
  RvOpts o, p;
  Buf b;
  Doc *d = doc_new();
  CharFmt f;
  u32 c;
  u8 m;
  rv_opts_default(&o);
  o.insmark = RVO_DUNDERLINE; o.delcolor = 0x123456; o.lines = RVL_LEFT; o.moveto = RVO_BOLD;
  buf_init(&b);
  rv_opts_save(&o, &b);
  rv_opts_default(&p);
  ok("wp5 options: saved and read back", rv_opts_load(&p, b.data) == 12 && p.insmark == RVO_DUNDERLINE && p.delcolor == 0x123456 && p.lines == RVL_LEFT && p.moveto == RVO_BOLD);
  buf_free(&b);
  memset(&f, 0, sizeof f);
  f.rev = REV_INS; f.rid = (u16)doc_add_rev(d, "Ann", "");
  rv_ink(d, &f, &c, &m);
  ok("wp5 options: insertions underlined in the author's colour", m == rv_opts.insmark && c == rv_author_color(d, f.rid));
  f.rid = (u16)doc_add_rev(d, "Bob", "");
  ok("wp5 options: each reviewer a colour of their own", rv_author_color(d, f.rid) != rv_author_color(d, 0));
  f.move = 1;
  rv_ink(d, &f, &c, &m);
  ok("wp5 options: moves in the move colour", c == rv_opts.movecolor && m == rv_opts.moveto);
  doc_free(d);
}

void test_wp5(void)
{
  docx_roundtrip();
  tracking();
  threads();
  display();
  options();
}
