/* test_wp28a.c - the native tests of WP28a RTF (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp28a() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/fmtio.h"

extern const DocFormat fmt_rtf;

/* d written as RTF and read back */
static Doc *rtf_again(Doc *d, unsigned *lost, char **out)
{
  size_t n;
  char err[200];
  unsigned l = 0;
  u8 *data = fmt_rtf.save(d, &n);
  Doc *e = data ? fmt_rtf.load(data, n, &l, err, sizeof err) : NULL;
  if (lost) *lost = l;
  if (out) *out = (char *)data; else free(data);
  return e;
}
static Doc *rtf_str(const char *s, unsigned *lost)
{
  char err[200];
  unsigned l = 0;
  Doc *d = fmt_rtf.load((const u8 *)s, strlen(s), &l, err, sizeof err);
  if (lost) *lost = l;
  return d;
}
static void add(Doc *d, Pos *c, const char *s) { doc_insert(d, c, s, (u32)strlen(s), NULL); }

/* a document with a bit of everything RTF carries, written and read back */
static void rtf_everything(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  CharFmt f;
  ParaFmt pf;
  Table t;
  int tn, k, pic, note, enote, list;
  char ref[8], *rtf = NULL;
  unsigned lost;
  size_t n;
  u8 *png = file_read("tests/data/disc.png", &n);
  add(d, &c, "Heading\nPlain bold italic red \xCE\xB1\xCE\xB2\xCE\xB3 \xF0\x9F\x98\x80 caf\xC3\xA9\tend\nCentred\nBullet one\nBullet two\nSub item\n");
  doc_apply_style(d, 0, 0, doc_style(d, "Heading1"));
  f = *para_fmt_at(d->p[1], 0);
  f.flags = CF_BOLD; doc_format(d, P(1, 6), P(1, 10), CM_BOLD, &f);
  f.flags = CF_ITALIC; doc_format(d, P(1, 11), P(1, 17), CM_ITALIC, &f);
  f.color = 0xC00000; doc_format(d, P(1, 18), P(1, 21), CM_COLOR, &f);
  f.size = 32; f.font = (u8)doc_font(d, "Arial"); doc_format(d, P(1, 0), P(1, 5), CM_SIZE | CM_FONT, &f);
  f.flags = CF_UNDERLINE; f.ul = UL_DOUBLE; doc_format(d, P(1, 22), P(1, 28), CM_UNDERLINE | CM_UL, &f);
  memset(&pf, 0, sizeof pf);
  pf.align = AL_CENTER; pf.left = 720; pf.first = -360; pf.before = 240; pf.after = 120; pf.line = 360; pf.linerule = LS_AUTO;
  pf.tabs[0] = 1440; pf.tabkind[0] = TAB_RIGHT; pf.tablead[0] = LEAD_DOT;
  doc_para_format(d, 2, 2, PM_ALIGN | PM_INDENT | PM_SPACING | PM_LINE | PM_TABS, &pf);
  list = doc_list_preset(d, LP_BULLETS);
  doc_list_apply(d, 3, 5, list);
  memset(&pf, 0, sizeof pf); pf.list = (u16)list; pf.level = 1; pf.left = 1440; pf.first = -360;
  doc_para_format(d, 5, 5, PM_LIST | PM_INDENT, &pf);
  /* a 3 x 3 table: the first row's first two cells merged, the last column merged down two rows */
  memset(&t, 0, sizeof t);
  t.ncols = 3; t.grid[0] = 2000; t.grid[1] = 2500; t.grid[2] = 3000; t.marl = t.marr = 108; t.look = LK_DEFAULT;
  tn = doc_add_table(d, &t);
  {
    static const struct { int row, col, span, vm; const char *text; } cells[] = {
      { 0, 0, 2, VM_NONE, "wide" }, { 0, 2, 1, VM_START, "tall" },
      { 1, 0, 1, VM_NONE, "a" }, { 1, 1, 1, VM_NONE, "b" }, { 1, 2, 1, VM_CONT, "" },
      { 2, 0, 1, VM_NONE, "c" }, { 2, 1, 1, VM_NONE, "d" }, { 2, 2, 1, VM_NONE, "e" } };
    for (k = 0; k < 8; k++) {
      Para *p;
      pf = d->styles[0].pf;
      memset(&pf.cell, 0, sizeof pf.cell);
      pf.cell.table = (u16)tn; pf.cell.row = (u16)cells[k].row; pf.cell.col = (u8)cells[k].col; pf.cell.span = (u8)cells[k].span;
      pf.cell.vmerge = (u8)cells[k].vm; pf.cell.borders = BD_ALL; pf.cell.shade = k == 2 ? 0xFFFF00 : COLOR_AUTO;
      pf.cell.bcolor[0] = pf.cell.bcolor[1] = pf.cell.bcolor[2] = pf.cell.bcolor[3] = COLOR_AUTO;
      p = para_new(&pf, &d->styles[0].cf);
      para_append(p, cells[k].text, (u32)strlen(cells[k].text), &d->styles[0].cf);
      doc_insert_para(d, d->np - 1, p);
    }
  }
  /* after the table: a picture, a footnote, an endnote, a link, a bookmark; then a page break and a
     two-column section */
  c = doc_end_pos(d);
  add(d, &c, "Picture ");
  pic = png ? doc_add_image(d, png, (u32)n, 1440, 1440) : -1;
  if (pic >= 0) { pic_utf8(ref, pic); add(d, &c, ref); }
  add(d, &c, " note");
  note = doc_add_note(d);
  note_utf8(ref, note); add(d, &c, ref);
  story_set_text(d, &d->notes[note], "The footnote's text.", 0, AL_LEFT);
  enote = doc_add_endnote(d);
  enote_utf8(ref, enote); add(d, &c, ref);
  story_set_text(d, &d->enotes[enote], "An endnote.", 0, AL_LEFT);
  add(d, &c, " see Quill site\nAfter the break\nTwo columns");
  {
    u32 pi = d->np - 3;
    Pos a, b;
    char *tx = d->p[pi]->t;
    char *s = strstr(tx, "Quill site");
    a = P(pi, (u32)(s - tx)); b = P(pi, (u32)(s - tx) + 10);
    doc_set_link(d, a, b, "https://example.org/quill", NULL, NULL);
    doc_set_bmark(d, P(pi, 0), P(pi, 7), "pic");
  }
  memset(&pf, 0, sizeof pf); pf.pagebreak = 1;
  doc_para_format(d, d->np - 2, d->np - 2, PM_BREAK, &pf);
  {
    SectFmt s;
    sect_default(&s); s.cols = 2; s.colgap = 360; s.colline = 1;
    d->p[d->np - 2]->pf.sect = (u16)doc_add_sect(d, &s);     /* the section before "Two columns" */
  }
  story_set_text(d, &d->hdr[HF_DEFAULT], "Header page {page} of {pages}", 0, AL_RIGHT);
  story_set_text(d, &d->ftr[HF_DEFAULT], "Footer", 0, AL_CENTER);
  strcpy(d->title, "Everything"); strcpy(d->author, "Quill Tester");
  d->fnpr.set = NPS_FMT | NPS_RESTART | NPS_POS; d->fnpr.fmt = 3; d->fnpr.restart = 2; d->fnpr.pos = 1;     /* (lower roman, each page, beneath the text) */
  d->sects[d->p[d->np - 2]->pf.sect - 1].enpr.set = NPS_START; d->sects[d->p[d->np - 2]->pf.sect - 1].enpr.start = 5;
  /* a tracked insertion */
  {
    int r = doc_add_rev(d, "Ada", "2026-10-24T10:30:00Z");
    f = *para_fmt_at(d->p[2], 0); f.rev = REV_INS; f.rid = (u16)r;
    c = P(2, 0);
    doc_insert(d, &c, "new ", 4, &f);
  }

  e = rtf_again(d, &lost, &rtf);
  ok("rtf: everything written and read back", e != NULL);
  if (!e) { doc_free(d); free(rtf); return; }
  ok("rtf: the same text, paragraph, cell, section and character formatting", same_doc(d, e));
  ok("rtf: styles keep their names", !strcmp(e->styles[e->p[0]->pf.style].name, "heading 1"));
  ok("rtf: Unicode beyond Latin-1 (Greek, an emoji)", strstr(e->p[1]->t, "\xCE\xB1\xCE\xB2\xCE\xB3 \xF0\x9F\x98\x80") != NULL);
  ok("rtf: underline kind", para_fmt_at(e->p[1], 23)->ul == UL_DOUBLE);
  ok("rtf: tabs with leaders, spacing, line spacing", e->p[2]->pf.tabs[0] == 1440 && e->p[2]->pf.tabkind[0] == TAB_RIGHT && e->p[2]->pf.tablead[0] == LEAD_DOT &&
     e->p[2]->pf.right == 0 && e->p[2]->pf.line == 360);
  ok("rtf: lists: bullets and levels", e->p[3]->pf.list && e->p[5]->pf.level == 1 && e->nlists >= 1 &&
     !strcmp(e->lists[e->p[3]->pf.list - 1].lvl[0].text, d->lists[list - 1].lvl[0].text) && e->lists[e->p[3]->pf.list - 1].lvl[0].fmt == NF_BULLET);
  {
    const Table *et = e->p[6]->pf.cell.table ? &e->tables[e->p[6]->pf.cell.table - 1] : NULL;
    ok("rtf: table grid, merged cells and shading", et && et->ncols == 3 && et->grid[1] == 2500 && e->p[6]->pf.cell.span == 2 &&
       e->p[7]->pf.cell.vmerge == VM_START && e->p[10]->pf.cell.vmerge == VM_CONT && e->p[8]->pf.cell.shade == 0xFFFF00);
  }
  {
    u32 pi = 14;
    Para *p = e->p[pi];
    char *s = strstr(p->t, "Quill site");
    int ln = s ? para_fmt_at(p, (u32)(s - p->t))->link : 0;
    ok("rtf: picture with its size", e->nimages == 1 && e->images[0].cx == 1440 && e->images[0].type == d->images[0].type && e->images[0].len == d->images[0].len &&
       !memcmp(e->images[0].data, d->images[0].data, d->images[0].len));
    ok("rtf: footnote and endnote", e->nnotes == 1 && e->nenotes == 1 && e->notes[0].np == 1 && !strcmp(e->notes[0].p[0]->t, "The footnote's text.") &&
       !strcmp(e->enotes[0].p[0]->t, "An endnote.") && strstr(p->t, "\xF3\xB4\x80\x80") != NULL);
    ok("rtf: hyperlink", ln && !strcmp(doc_link_url(e, ln), "https://example.org/quill"));
    ok("rtf: bookmark", para_fmt_at(p, 0)->bmark && !strcmp(doc_bmark_name(e, para_fmt_at(p, 0)->bmark), "pic") && !para_fmt_at(p, 8)->bmark);
  }
  ok("rtf: page break and section with columns", e->p[15]->pf.pagebreak && e->p[15]->pf.sect && doc_sect(e, 15)->cols == 2 && doc_sect(e, 15)->colline);
  {
    char *h = story_text(&e->hdr[HF_DEFAULT]);
    ok("rtf: header with page fields, footer", h && !strcmp(h, "Header page {page} of {pages}") && e->hdr[HF_DEFAULT].p[0]->pf.align == AL_RIGHT &&
       e->ftr[HF_DEFAULT].np == 1 && !strcmp(e->ftr[HF_DEFAULT].p[0]->t, "Footer"));
    free(h);
  }
  ok("rtf: footnote and endnote options, the document's and a section's", e->fnpr.set == d->fnpr.set && e->fnpr.fmt == 3 && e->fnpr.restart == 2 && e->fnpr.pos == 1 &&
     doc_sect(e, 15)->enpr.set == NPS_START && doc_sect(e, 15)->enpr.start == 5);
  ok("rtf: title and author", !strcmp(e->title, "Everything") && !strcmp(e->author, "Quill Tester"));
  ok("rtf: tracked insertion with author and date", para_fmt_at(e->p[2], 0)->rev == REV_INS && para_fmt_at(e->p[2], 5)->rev == REV_NONE &&
     !strcmp(e->revs[para_fmt_at(e->p[2], 0)->rid].author, "Ada") && !strncmp(e->revs[para_fmt_at(e->p[2], 0)->rid].date, "2026-10-24T10:30", 16));
  ok("rtf: written as 7-bit text", rtf && !strchr(rtf, 0x80) && strstr(rtf, "\\u945\\'3f") == NULL && strstr(rtf, "\\u945?") != NULL);
  {
    /* and once more: what was read writes the same file */
    char *rtf2 = NULL;
    Doc *g = rtf_again(e, &lost, &rtf2);
    ok("rtf: a second round trip writes the same file", g && rtf2 && !strcmp(rtf, rtf2));
    if (getenv("QUILL_DUMP")) { file_write("/tmp/q1.rtf", rtf, strlen(rtf)); if (rtf2) file_write("/tmp/q2.rtf", rtf2, strlen(rtf2)); }
    doc_free(g); free(rtf2);
  }
  ok("rtf: what was read is sane", runs_sane(e));
  free(rtf);
  doc_free(e); doc_free(d);
}

/* every picture reference made the first picture's (the order pictures are numbered in differs
   between formats: headers come first in RTF) */
static void unpic(Doc *d)
{
  u32 i, k;
  char ref[8];
  pic_utf8(ref, 0);
  for (i = 0; i < d->np; i++) for (k = 0; k + 4 <= d->p[i]->len; k++) if (IS_PIC(d->p[i]->t + k)) memcpy(d->p[i]->t + k, ref, 4);
}
/* Word's and LibreOffice's .docx test documents through RTF */
static void rtf_docx_files(void)
{
  static const char *const files[] = { "tests/data/word.docx", "tests/data/lo.docx", "tests/data/tables.docx", "tests/data/headers.docx",
                                       "tests/data/notes.docx", "tests/data/pictures.docx", "tests/data/notes-lo.docx" };
  int i;
  for (i = 0; i < (int)(sizeof files / sizeof files[0]); i++) {
    char err[200], what[120];
    unsigned lost;
    Doc *d = doc_load_file(files[i], &lost, err, sizeof err), *e;
    if (!d) { ok(files[i], 0); continue; }
    e = rtf_again(d, &lost, NULL);
    snprintf(what, sizeof what, "rtf: %s through RTF keeps text and formatting", files[i] + 11);
    if (e) { unpic(d); unpic(e); }
    ok(what, e && same_doc(d, e) && e->nnotes == d->nnotes && e->nimages == d->nimages && e->ntables == d->ntables);
    doc_free(e); doc_free(d);
  }
}

/* RTF that LibreOffice wrote (from tests/data's .docx files) reads as the .docx does */
static void rtf_from_libreoffice(void)
{
  static const char *const names[] = { "word", "lo", "tables", "headers", "notes", "pictures" };
  int i;
  for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
    char a[80], b[80], err[200], what[120];
    unsigned lost;
    Doc *d, *e;
    u32 k, same = 0;
    snprintf(a, sizeof a, "tests/data/%s.docx", names[i]);
    snprintf(b, sizeof b, "tests/data/lo-%s.rtf", names[i]);
    d = doc_load_file(a, &lost, err, sizeof err);
    e = doc_load_file(b, &lost, err, sizeof err);
    if (d && e) { unpic(d); unpic(e); }
    if (d && e) {
      /* the non-empty paragraphs' text, in order (LibreOffice drops an empty paragraph holding only a page break) */
      u32 j = 0, nd = 0;
      for (k = 0; k < d->np; k++) {
        if (!d->p[k]->len) continue;
        nd++;
        while (j < e->np && !e->p[j]->len) j++;
        if (j < e->np && !strcmp(d->p[k]->t, e->p[j]->t)) same++;
        j++;
      }
      same = same == nd ? d->np : 0;
    }
    snprintf(what, sizeof what, "rtf: LibreOffice's %s.rtf reads as %s.docx (text, tables, notes, pictures)", names[i], names[i]);
    ok(what, d && e && same == d->np && e->ntables == d->ntables && e->nnotes == d->nnotes && e->nimages == d->nimages);
    if (d && e && i == 0) {
      ok("rtf: LibreOffice's styles, bold and a centred paragraph", !strcmp(e->styles[e->p[1]->pf.style].name, "Heading 1") && e->p[3]->pf.align == AL_CENTER &&
         (para_fmt_at(e->p[2], (u32)(strstr(e->p[2]->t, "bold") - e->p[2]->t))->flags & CF_BOLD) && e->p[6]->pf.list && e->nstyles < 40);
    }
    if (d && e && i == 2) {
      int span2 = 0;
      for (k = 0; k < e->np; k++) if (e->p[k]->pf.cell.span == 2 || e->p[k]->pf.cell.vmerge) span2++;
      ok("rtf: LibreOffice's merged cells", span2 > 0);
    }
    doc_free(d); doc_free(e);
  }
}

/* hand-written RTF: code pages, \u fallbacks, \bin pictures, metafiles, old lists, merges, fields */
static void rtf_details(void)
{
  unsigned lost;
  char err[200];
  Doc *d;
  d = rtf_str("{\\rtf1\\ansi\\ansicpg1251\\deff0{\\fonttbl{\\f0 Arial;}{\\f1\\fcharset238 Arial CE;}{\\f2\\fcharset2 Symbol;}}"
              "\\pard \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2 {\\f1 \\'b9\\'e8} {\\f2 a\\'b7} \\uc2\\u8364\\'80\\'80 \\uc1\\u-10179?\\u-8704? A\\~B\\par}", &lost);
  ok("rtf: code pages (1251, a 1250 font), Symbol, \\uc2 fallbacks, surrogates", d && d->np == 1 &&
     !strcmp(d->p[0]->t, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xC4\x85\xC4\x8D \xCE\xB1\xE2\x80\xA2 \xE2\x82\xAC \xF0\x9F\x98\x80 A\xC2\xA0" "B"));
  doc_free(d);
  {
    /* a PNG as \bin bytes and as hex, and a metafile (a box in its place) */
    size_t n;
    u8 *png = file_read("tests/data/disc.png", &n);
    Buf b;
    size_t i;
    buf_init(&b);
    buf_printf(&b, "{\\rtf1 x{\\pict\\pngblip\\picwgoal720\\pichgoal360\\bin%lu ", (unsigned long)n);
    buf_add(&b, png, n);
    buf_adds(&b, "}y{\\pict\\pngblip\\picscalex50\\picscaley50\\picwgoal1440\\pichgoal1440 ");
    for (i = 0; i < n; i++) buf_printf(&b, "%02x", png[i]);
    buf_adds(&b, "}z{\\*\\shppict{\\pict\\jpegblip 00}}{\\nonshppict{\\pict\\wmetafile8 0102}}{\\pict\\wmetafile8\\picw2540\\pich1270 0102}\\par}");
    d = fmt_rtf.load((const u8 *)b.data, b.len, &lost, err, sizeof err);
    ok("rtf: \\bin and hex pictures, scaled", d && d->nimages >= 2 && d->images[0].len == n && !memcmp(d->images[0].data, png, n) && d->images[0].cx == 720 &&
       d->images[0].cy == 360 && d->images[1].cx == 720 && d->images[1].len == n);
    ok("rtf: a metafile becomes a box of its size; the non-shppict copy is skipped", d && d->nimages == 4 && d->images[3].kind == OBJ_SHAPE &&
       d->images[3].cx == 1440 && d->images[3].cy == 720 && (lost & LOST_PICTURES));
    doc_free(d); buf_free(&b); free(png);
  }
  d = rtf_str("{\\rtf1{\\fonttbl{\\f0 Times;}{\\f1\\fcharset2 Symbol;}}"
              "{\\pntext\\f1\\'b7\\tab}{\\*\\pn\\pnlvlblt\\pnf1\\pnindent0{\\pntxtb\\'b7}}\\li720\\fi-360 one\\par "
              "{\\pntext 1.\\tab}{\\*\\pn\\pnlvlbody\\pndec\\pnstart1{\\pntxta .}}\\li720\\fi-360 two\\par}", &lost);
  ok("rtf: Word 6 lists (\\pn): bullets and numbers, the labels' text left out", d && d->np == 2 && !strcmp(d->p[0]->t, "one") && d->p[0]->pf.list &&
     d->lists[d->p[0]->pf.list - 1].lvl[0].fmt == NF_BULLET && d->p[1]->pf.list && d->lists[d->p[1]->pf.list - 1].lvl[0].fmt == NF_DECIMAL);
  doc_free(d);
  d = rtf_str("{\\rtf1 \\trowd\\trgaph108\\trleft-108\\clmgf\\cellx1000\\clmrg\\cellx2000\\cellx3000"
              "\\pard\\intbl ab\\cell \\cell c\\cell\\row"
              "\\trowd\\trgaph108\\trleft-108\\cellx2000\\cellx3000\\pard\\intbl d\\cell e\\cell\\row"
              "\\pard after\\par}", &lost);
  ok("rtf: \\clmgf/\\clmrg and wider cells merge horizontally", d && d->ntables == 1 && d->tables[0].ncols == 2 && d->p[0]->pf.cell.span == 1 &&
     d->tables[0].grid[0] == 2108 && !d->p[d->np - 1]->pf.cell.table && !strcmp(d->p[d->np - 1]->t, "after"));
  doc_free(d);
  d = rtf_str("{\\rtf1 \\pard\\intbl\\itap1 out\\par \\pard\\intbl\\itap2 in\\nestcell{\\*\\nesttableprops\\trowd\\cellx500\\nestrow}"
              "\\pard\\intbl\\itap1\\cell\\trowd\\cellx3000\\row\\pard x\\par}", &lost);
  ok("rtf: a nested table is flattened into its cell", d && d->ntables == 1 && !strcmp(d->p[1]->t, "in") && d->p[1]->pf.cell.table && (lost & LOST_TABLES));
  doc_free(d);
  d = rtf_str("{\\rtf1{\\header \\pard\\qc Page {\\field{\\*\\fldinst PAGE}{\\fldrslt 7}} of {\\field{\\*\\fldinst NUMPAGES}{\\fldrslt 9}}\\par}"
              "\\pard Go {\\field{\\*\\fldinst {HYPERLINK \"http://a.example/\" }}{\\fldrslt {\\ul here}}} or {\\field{\\*\\fldinst HYPERLINK \\\\l \"top\"}{\\fldrslt there}}; "
              "{\\field{\\*\\fldinst DATE}{\\fldrslt 1 May}}\\par}", &lost);
  {
    char *h = d ? story_text(&d->hdr[HF_DEFAULT]) : NULL;
    ok("rtf: fields: page numbers become Quill's, links keep their text, others their result", d && h && !strcmp(h, "Page {page} of {pages}") &&
       !strcmp(d->p[0]->t, "Go here or there; 1 May") && !strcmp(doc_link_url(d, para_fmt_at(d->p[0], 3)->link), "http://a.example/") &&
       !strcmp(doc_link_url(d, para_fmt_at(d->p[0], 11)->link), "#top") && !para_fmt_at(d->p[0], 17)->link);
    free(h);
  }
  doc_free(d);
  d = rtf_str("{\\rtf1{\\stylesheet{\\s0 Normal;}{\\s1\\sb240\\b\\fs32\\sbasedon0\\snext0 heading 1;}{\\s5\\i\\sbasedon1 Mine;}{\\s6 Unused;}}"
              "\\pard\\plain\\s5 styled\\par}", &lost);
  ok("rtf: styles: based on, own settings, only used ones made", d && !strcmp(d->styles[d->p[0]->pf.style].name, "Mine") &&
     (d->styles[d->p[0]->pf.style].cf.flags & (CF_BOLD | CF_ITALIC)) == (CF_BOLD | CF_ITALIC) && d->styles[d->p[0]->pf.style].pf.before == 240 &&
     doc_style_by_name(d, "Unused") < 0 && !(para_fmt_at(d->p[0], 0)->flags & CF_ITALIC));
  doc_free(d);
  ok("rtf: garbage and truncated files don't crash", (d = rtf_str("{\\rtf1 {{{\\b x\\'4", &lost)) != NULL && d->p[0]->t[0] == 'x');
  doc_free(d);
  ok("rtf: the format is known by its first bytes and its name", fmt_rtf.sniff((const u8 *)"{\\rtf1", 6) && doc_format_for_name("A.RTF", 1) == &fmt_rtf);
}

/* comments read and written (WordCraft #1053): a range, its author, initials, date and text */
static void rtf_comments(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d), a, b;
  char *out = NULL;
  int k;
  add(d, &c, "Before the commented words after.");
  k = doc_comment(d, P(0, 11), P(0, 25), "Check this.", "Ann Example", "AE", "2026-11-18T10:30:00Z");
  e = rtf_again(d, NULL, &out);
  ok("rtf comments: written as atrfstart, annotation", k == 0 && out && strstr(out, "{\\*\\atrfstart 0}") && strstr(out, "{\\*\\atnauthor Ann Example}") &&
     strstr(out, "\\chatn") && strstr(out, "{\\*\\annotation{\\*\\atnref 0}"));
  ok("rtf comments: read back with author, initials and date", e && e->ncomments == 1 && !strcmp(e->comments[0].author, "Ann Example") &&
     !strcmp(e->comments[0].initials, "AE") && !strcmp(e->comments[0].date, "2026-11-18T10:30:00Z") && e->comments[0].text.np == 1 &&
     !strcmp(e->comments[0].text.p[0]->t, "Check this."));
  ok("rtf comments: the same range", e && doc_comment_mark(e, 0, 0, &a) && doc_comment_mark(e, 0, 1, &b) && a.para == 0 &&
     doc_comment_mark(d, 0, 0, &c) && a.off == c.off && doc_comment_mark(d, 0, 1, &c) && b.off == c.off);
  free(out); doc_free(e); doc_free(d);
  /* Word's form, with no range: the comment marks its place */
  e = rtf_str("{\\rtf1\\ansi{\\fonttbl{\\f0 Times;}}\\pard Text{\\*\\atnid JD}{\\*\\atnauthor John Doe}\\chatn{\\*\\annotation{\\*\\atndate 1240588044}\\pard\\plain {\\chatn }Note.}\\par}", NULL);
  ok("rtf comments: Word's point comment", e && e->ncomments == 1 && !strcmp(e->comments[0].author, "John Doe") && !strcmp(e->comments[0].initials, "JD") &&
     e->comments[0].text.np >= 1 && !strcmp(e->comments[0].text.p[0]->t, "Note.") && strncmp(e->p[0]->t, "Text", 4) == 0 && e->comments[0].date[0] == '2');
  doc_free(e);
}

void test_wp28a(void)
{
  rtf_comments();
  rtf_everything();
  rtf_docx_files();
  rtf_from_libreoffice();
  rtf_details();
}
